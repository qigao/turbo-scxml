# SCXML `donedata/content` 错误顺序设计

## 背景与目标

W3C SCXML 1.0 test 528 要求：final state 的 `<donedata><content expr>` 求值失败时，
解释器先将 `error.execution` 加入内部队列；包含该 final 的复合状态随后产生
`done.state.*`，其 `_event.data` 为空。由于内部事件优先于 completion event，状态机必须
先观察到 `error.execution`。

当前 TurboSCXML 在 CFlow 已开始准备 completion trigger 后才调用
`bind_completion_done_data()` 求值。此时即使求值失败并成功暂存 `error.execution`，本次
completion transition 仍会立即完成选择，错误事件因此晚于 completion。问题属于 done-data
物化时机，不属于 CFlow 的通用 completion 优先级。

本设计只修正 TurboSCXML 内部时序，不修改公开 API、CFlow StateChart 语义、adapter ABI、
配置格式或依赖方向。

## 候选方案

| 方案 | 结果 | 取舍 |
|---|---|---|
| 在 CFlow completion prepare 中求值后重新进入 internal drain | 拒绝 | 把 SCXML 特有的表达式副作用泄漏进通用 StateChart 调度器，并改变已经稳定的 completion 契约 |
| 在 completion 观察时同步求值并直接触发 error transition | 拒绝 | 绕过事件队列和最优转换选择，形成第二套状态推进事实源 |
| final entry 时预物化 done data，completion 只消费结果 | 采用 | 与 SCXML “进入 final 后产生 completion” 的因果顺序一致；错误可在 completion 前进入既有 internal FIFO；CFlow 仍是状态与事件顺序的唯一事实源 |

## 分层与状态归属

- XML admission 与 typed IR 仍由 `scxml_analyze.c`、`scxml_emit.c` 拥有。
- CMeta 表达式求值与对象复制仍通过现有 expression/assignment API 完成。
- `cflow_statechart_instance` 继续拥有活动配置、internal FIFO、completion queue 和转换选择。
- `scxml_session_impl` 新增固定容量的 completion-data rows，作为已进入 final 的派生 payload
  事实源；它们不拥有或推进状态机配置。
- completion Event envelope 只借用当前被消费 row 的标量文本或结构化对象；现有当前 Event
  生命周期结束前借用有效，随后 row 被释放。

依赖保持单向 `TurboSCXML -> Salts/CFlow/CMeta`。不增加回调到 CFlow 内部实现，也不
复制 StateChart 状态。

## 编译与执行模型

每个具有 `<donedata>` 的 final state 生成一个内部 `SCXML_STEP_DONEDATA`。该 step 被放在
final state 的所有显式 `<onentry>` executable blocks 之后。这样表达式看到的是 onentry 已经
提交到当前 staged state 的值；step 又早于 CFlow 在 entry 完成后暂存父状态 completion。
该 step 只有 owning `scxml_session` 才物化 Event envelope；直接使用公开 raw StateChart 的
调用者没有 SCXML Event envelope，继续保持既有的 StateChart-only completion 行为。

执行顺序如下：

1. CFlow 进入 final state，并按文档顺序执行显式 onentry blocks。
2. `SCXML_STEP_DONEDATA` 从本次 staged state 快照求值并写入 session-owned bounded row。
3. 求值成功时 row 标记为 scalar、structured 或 inline；求值失败时 row 标记为空，并通过
   现有 host transaction 暂存一个 `error.execution`。
4. entry microstep 成功提交后，CFlow 暂存父状态 completion。
5. 下一次 drain 先消费 internal `error.execution`，再考虑 pending completion。
6. completion Event 被观察时按 parent state ID 取最早 ready row，绑定其数据并消费该 row。

如果执行 step 或暂存错误事件发生致命失败，本次 CFlow transaction 失败；未提交 row 在
session 销毁时释放。这里不新增 staged-effect ticket：done-data step 是 final entry 的最后一个
内部 action，consumer 只有在 entry transaction 成功后才可能运行，且 fatal transaction 后
session 不会继续可靠执行。这样也避免把当前不需要 effect journal 的文档强制升级为
`effect_capacity > 0`。

## Row 状态机与匹配规则

每个 row 使用内部状态：

```text
FREE -> BUILDING -> READY -> BOUND -> FREE
```

- `BUILDING`：已取得唯一 row，正在构造 payload；对 consumer 不可见。
- `READY`：物化已完成；payload kind 可为 EMPTY、SCALAR、INLINE 或 OBJECT。
- `BOUND`：当前 completion Event 正在借用 row；下一 Event observation 或 session destroy
  结束借用。
- `FREE`：不含 live CMeta object，也不向 Event 暴露借用。

row 记录 completion parent 的 compiled state ID 和单调递增 sequence。consumer 在所有匹配
parent 的 READY rows 中选择最小 sequence，因此同一复合状态退出再进入时仍保持产生顺序。
消费依据 row，而不是重新要求原 final 仍活动；这是因为内部错误 transition 可能已使原 final
失活，但其随后排队的 completion 仍必须携带该次 final entry 的空数据。

每个成功 reserve 恰好到达以下终态之一：READY 后被消费、fatal 后由 session destroy 清理，
或物化/错误暂存无法继续时立即释放。结构化对象只在 row 中有一个 owner，并在消费、复用或
session destroy 时恰好销毁一次。

## 容量、内存与错误协议

row 数固定为 checked `completion_capacity + 1`；`completion_capacity` 个 row 对应 native
completion queue，额外一个 row 供当前 Event 借用，避免 completion transition 直接进入下一个
final 时产生伪满。初始化后不运行时扩容。
一个 row 的 retained payload 上限为：

```text
sizeof(row metadata) + (SCXML_EVENT_METADATA_CAPACITY + 1)
                     + SCXML_EVENT_DATA_CAPACITY
```

因此新增预算为 `(completion_capacity + 1) * sizeof(scxml_completion_data_slot)`，另有数组
对齐开销。
满额返回明确 fatal 错误，不丢弃旧 completion、不覆盖 payload、不转为无界分配。sequence
溢出同样 fail fast。

表达式失败是 SCXML 可处理错误：清理任何部分构造的对象，将 row 发布为 EMPTY，并暂存一个
`error.execution`。若错误事件也无法进入有界 internal queue，则升级为 fatal，不能带着半可信
状态继续。inline content 只复制编译期已验证长度的不可变字节；scalar expression 结果复制到
row 的 metadata buffer；structured params 从同一不可变状态快照构造，失败时原子丢弃整个对象。

所有 producer/consumer 操作只运行在 CFlow serial executor 上；外部 producer 不访问这些
rows，因此不增加第二把锁，也不扩大既有 registry lock 的职责。session destroy 只在 CFlow
与 adapter 均 quiescent 后清理 rows。

## 兼容性与迁移

公开接口和 XML 格式不变。成功的 scalar、inline 与 structured done-data 行为保持不变，只把
求值从 completion observation 提前到 final entry；因此 onentry 后状态仍是表达式输入。用户可见
差异仅是此前错误的失败顺序得到修正。

主要兼容性风险是同一 parent 的重复 completion、managed CMeta 对象生命周期和极小
`completion_capacity`。验证必须覆盖顺序、空 Event data、成功三种 payload、失败清理、满额
错误和 session destroy。回滚只需移除 synthetic step、row storage 与 test528 fixture；没有
持久数据或部署迁移。

## 验证标准

- W3C-derived test528 必须先处理 `error.execution`，后处理 data 为空的 `done.state.s0`。
- 现有 scalar test527、inline test529、structured test294 继续通过。
- 聚焦 TinyTest 验证表达式失败不选择 completion fallback，且 session 可正常销毁。
- Release/Debug 构建与完整 CTest 通过；manifest 精确为 147 PASS、21 UNSUPPORTED、34 N/A。
- `git diff --check`、严格 corpus inventory 和占位符扫描通过。
