# SCXML 宏步调用与外部事件调度设计

## 目标与范围

本设计刻画 W3C SCXML 1.0 强制测试 422 和 423 所要求的两条连续语义：

1. 一个宏步的 eventless、internal 与 completion 工作全部稳定后，只启动自上次稳定边界以来已进入且仍然活动的状态所拥有的 `invoke`，并按文档顺序处理这些调用。
2. 调用事务发布后才从外部队列按 FIFO 取事件；不启用任何转换的外部事件仍被消费，处理器继续取下一个事件，直到某个事件启用非空最优转换集。

范围只包括现有 TurboSCXML 到 CFlow 的同步边界、真实 invocation adapter 事务，以及本地 W3C fixture。子解释器、传输、跨 session 路由、计时器、授权和持久化仍属于 host，不在本改动中实现。

## 依据

- W3C IRP [`test422.txml`](https://www.w3.org/Voice/2013/scxml-irp/422/test422.txml) 与 [`test423.txml`](https://www.w3.org/Voice/2013/scxml-irp/423/test423.txml)。
- 只读参考基线 `qigao/scxml@c80cedfa43b559861a054e992137685cdd29af16`：`FastMicroStep.cpp` 在内部队列清空后先取消失活调用、再按 configuration/文档顺序启动仍活动调用，随后才报告稳定并读取外部事件。本设计只比较调度语义，不复制实现代码。
- CFlow V4 `on_host_transaction` 的 `PREPARE_QUIESCENCE` 契约：回调位于 internal/eventless/completion drain 之后、宏步 settle 和下一次 external admission 之前；staged state、internal Events 与 effect tickets 作为一个事务提交或丢弃。

## 状态归属与依赖边界

`cflow_statechart_instance` 是活动配置、配置版本、internal/completion 队列、external FIFO、最优转换选择和宏步状态的唯一事实源。`scxml_session_impl` 是编译后 invocation descriptors、逐 descriptor invocation rows、token、统计和 adapter attachment 的唯一事实源。host adapter 只拥有已经接受的 prepare ticket 及其外部资源；它不拥有 SCXML 活动配置，也不决定下一个事件。

依赖方向保持 `TurboSCXML -> installed Salts/CFlow`。TurboSCXML 通过 `scxml_session_init_model()` 注册唯一的 CFlow V4 `on_host_transaction`，在 `PREPARE_TRIGGER` 处理 Event 观察与 external preprocess，在 `PREPARE_QUIESCENCE` 启动仍活动的调用；不新增第二个 scheduler、全局 session registry、子解释器或反向依赖。

## 精确调度序列

一个宏步按以下顺序推进：

1. CFlow 执行初始进入或已选转换的 exit、transition content 与 entry，并事务性提交 TurboSCXML 的 `INVOKE_ENTER`/`INVOKE_EXIT` lifecycle effects。
2. CFlow 反复选择 eventless 转换，随后按既有优先级处理 internal Events、adapter-internal Events 与 completion；任何新内部工作都会继续当前宏步。
3. 上述队列清空且宏步仍活动时，CFlow 进入 V4 `PREPARE_QUIESCENCE`，TurboSCXML 调用 `scxml_runtime_start_pending_invocations()`。published state 不可变，首次请求编辑时由 CFlow 懒构造唯一 staged state，活动配置通过 call-scoped host context 查询。
4. TurboSCXML 按 `program->invocations[0..count)` 扫描 descriptor。只有 row 为 `PENDING` 且 owner 在当前配置中活动时才 prepare start。因此同一宏步进入后又退出的 transient owner 已由 EXIT effect 清空，不会启动；仍活动的 ancestor 和 leaf 按编译保留的文档顺序启动。
5. 每个成功 prepare 的 adapter ticket 与 invocation row 变更一起 stage。CFlow 先原子发布 staged state 和 staged internal Events，再按 staging 顺序 commit effects；因此 adapter `commit` 是调用对 host 可见的边界。
6. 若稳定事务生成新的 internal Event，CFlow 回到内部工作并再次稳定；只有没有内部工作时才 settle 当前宏步。
7. 下一 quantum 才从 external mailbox FIFO dequeue。dequeue 立即减少 pending 数并前移队首；即使该事件最终选择零条转换，它也不会返回队列。CFlow settle 这个空宏步并继续 external admission，直到后续事件启用非空转换集。
8. 被启用的 external Event 开始下一宏步；其转换执行不得早于步骤 5 的 invocation commits。

测试中的顺序 trace 必须区分 `prepare_start`、ticket `commit` 和后来由 external Event 产生的可见结果，不能把 prepare 当作已发布调用。

## 锁、回调与生命周期边界

CFlow hooks 在 SerialExecutor 上运行且不持有 instance mutex。TurboSCXML 只在检查或修改 invocation row、token、effect slot 和统计时短暂持有 `registry_lock`；调用 `prepare_start`、ticket `commit/discard`、`prepare_cancel` 及其 ticket 回调时不持有该锁。adapter 不得保留 call-scoped CFlow context 或 borrowed request 字段；若异步使用，必须在 prepare 内复制。

成功 ENTER/EXIT 与 START/FAIL 都通过 CFlow effect journal 发布。EXIT commit 对活动 row 先清除 session 事实，再在锁外 prepare/commit cancel；这避免 host callback 重入 session lock。`scxml_session_close()`/destroy 先关闭 CFlow admission，再对已 attachment 的 invoke adapter 恰好调用一次非阻塞 `close`。只有 adapter 在 close 后报告 quiescent，session 才能销毁；否则返回 `WOULD_BLOCK`，不释放仍可能被 adapter 访问的 session/user。

## 有界容量

所有结构在初始化时定界并校验：

- `invocation_capacity >= program->invocation_count`，每个 descriptor 对应一个 session row。
- invocation lifecycle effect storage 为 checked `effect_capacity + 1`；额外 probe row 只用于让 CFlow effect journal 继续作为 `EFFECT_JOURNAL_FULL` 的事实源。
- CFlow internal、adapter-internal、completion 和 external queue 均使用显式 capacity；external FIFO 满时 admission 返回可区分错误，不扩为无界队列。
- payload、metadata、ID 与 staged effect 都受既有编译期/初始化期上限约束，并继续使用 checked arithmetic。

本任务不增加任何隐藏容量或动态 fallback。

## Prepare/commit/discard 与失败收束

调用 start 的事务规则如下：

- 参数求值、payload materialization 或 `idlocation` 写入失败：stage 一个 FAIL row 和首个可用 `error.execution`；若 staging 也失败，整个稳定事务 FATAL。
- adapter 返回 `ACCEPTED`：必须同时给出非空 commit/discard；TurboSCXML reserve row 并 stage ticket。CFlow 成功发布后恰好 commit 一次。
- adapter 拒绝：不取得 ticket，stage FAIL row 和对应 adapter error。
- adapter 合同无效、effect storage 满、内部事件满、token/ID 上限耗尽：fail fast，保留首个有效错误，稳定事务 FATAL。
- 稳定事务任一后续步骤失败：CFlow 丢弃所有 staged Events/state/effects；START row 恢复为 `PENDING`，已取得的 adapter ticket 恰好 discard 一次，不发布部分 invocation。

由此 session row 是调用生命周期事实源，adapter 的 reserved/committed 资源只是事务性外部效果，不存在双向同步。

## 测试设计

直接 adapter-order 回归必须走公开 TurboSCXML session 与真实 CFlow executor：初始配置同时包含 active ancestor、同一宏步进入后退出的 transient child、最终 active descendant，并预排两个 external Events。严格 adapter 记录文档顺序的 start prepare/commit；断言 transient 从未 prepare、两个 live invokes 依次 commit，且 enabling external 的结果发生在 commits 之后。该测试若在当前生产代码上 GREEN，记录为 characterization，不制造 production diff。

W3C 422 的本地 bounded null/CMeta transformation 用 host invoke adapter 替代上游子解释器：ancestor 与 final descendant 的 start commit 分别把可观察事件送回 session，transient state 的 start 是立即失败 sentinel；只有两个期望事件均被处理后才能进入 `pass`。W3C 423 以受控 public external admission 替代上游延迟 send：先预排一个 unmatched external Event 和一个 enabling external Event，同时在初始 entry raise internal Event；fixture 只有在 internal 先转入等待态、unmatched 被消费、后来 enabling Event 被选择时进入 `pass`。

manifest 仍是唯一 conformance 事实源，行数保持 202（168 mandatory、34 optional）。只有两个 fixture 通过完整 public path 后才把 422/423 改为 `PASS/TERMINAL_PASS`，并把说明统计由 116/52 更新为 118/50；不改变“本地 corpus 通过不等于 W3C certification”的声明。

## 兼容性、修复与回滚

SCXML 公开 API、adapter ABI、package target、数据格式、依赖关系和用户配置不变。其依赖的 CFlow StateChart hook ABI 已统一为 V4-only，因此 TurboSCXML 与 Salts 必须配套重编译；TurboSCXML 内部不保留 V1-V3 context 适配层。首先登记 W3C 测试并保存缺 fixture RED，再添加忠实 fixture 与直接 adapter-order 回归。若直接回归已通过，生产语义保持不变。

只有直接回归在现有 runtime 上因调度语义失败时，才在暴露失败的 owning layer 做最小修复：活动配置/外部 FIFO 属于 CFlow，invocation row/adapter 事务属于 TurboSCXML；不得跨层复制状态。修复必须保持 callbacks outside locks、事务性发布和现有错误语义。回滚方式是单独撤销该最小生产改动并保留 characterization 测试为失败证据；若修复需要公开 ABI、依赖或数据格式变化，则视为计划缺陷，停止而不扩展范围。
