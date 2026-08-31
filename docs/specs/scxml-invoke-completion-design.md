# SCXML invoke 完成与返回 Event 来源设计

## 背景与范围

本设计覆盖 W3C SCXML 1.0 强制测试 228、232、235、236 和 247。范围只包括
TurboSCXML 已有公开 invoke adapter 返回路径的严格见证：活动 invocation 的完成
Event 带有精确 `invokeid`，多个返回 Event 保持 FIFO，完成 Event 暴露为
`done.invoke.<exact-id>`，完成被消费后 token 失效，以及 host 拥有的真实子 session
到达顶层 final 后向真实父 session 报告一次完成。

核心不创建或嵌入子解释器。子 session 的构造、运行、完成观察和销毁仍由 host
负责。本批次不增加全局 session 查找、transport、线程、文件/网络 I/O、无界
存储或 fallback interpreter，也不改变公开 adapter ABI/API。

## 规范与参考证据矩阵

| IRP | 完整断言的本地保留方式 | 生成变体 | 只读 uSCXML 比较 | 采用/拒绝 | 所有者 | 验证 |
|---|---|---|---|---|---|---|
| 228 | 显式 ID `invoke228` 的 live token 返回 completion Event；父 CMeta guard 比较 `_event.invokeid == \"invoke228\"` | `namespace/test228.scxml` | child completion Event 携带创建服务的 invoke ID | 采用精确 completion 来源；不复制代码 | session invocation row | `test228.scxml` + `report_invoke_done()` + 严格 host 状态/计数 |
| 232 | 同一 token 连续返回 `childToParent1`、`childToParent2`，再返回完成；父状态序列要求 FIFO | `namespace/test232.scxml` | `ParentQueueImpl::enqueue()` 将子 Event 转交 parent external queue | 采用 FIFO；拒绝内建 parent queue/线程 | CFlow external mailbox | `test232.scxml` + 三次 `CFLOW_MAILBOX_OK` |
| 235 | ID `foo` 的完成只匹配 `done.invoke.foo` | `namespace/test235.scxml` | `SCXMLInvoker::run()` 构造 `done.invoke.` + child invoke ID | 采用动态可见名称；核心仍用 descriptor 的有限 compiled Event ID | descriptor + live row | `test235.scxml` + 完成统计 |
| 236 | `childToParent` 先于完成；完成被消费后同 token 的普通 Event 明确拒绝，随后独立确认 Event 才能到达 pass | `namespace/test236.scxml` | `SCXMLInvoker::run()` 在 child step 完成后才 enqueue done；inactive parent queue 拒绝后续 child Event | 采用 terminal token；拒绝静默接受/丢弃 host 报告 | invocation row lifecycle | `test236.scxml` + `INVALID_ARGUMENT` + selection sentinel |
| 247 | host 启动第二个真实 TurboSCXML session；只有观察到 child `stats.done` 后，才用 parent committed token 报告完成一次 | `namespace/test247.scxml` | `SCXMLInvoker` 拥有 child interpreter 并在其自然完成后 enqueue done | 采用因果顺序；拒绝把子解释器嵌入 core，也拒绝无 child witness 的直接完成 | host owns child; parent owns row | 两真实 session、child top-level final、parent completed=1 |

参考基线为 `qigao/scxml@c80cedfa43b559861a054e992137685cdd29af16`；复制
代码：**none**。uSCXML 仅用于比较完成与 parent-queue 边界，不进入构建或运行依赖。

## 数据与生命周期协议

- **数据单元：** session 在成功 admission 时复制 `cflow_event_view`，并附带 invocation
  token。完成使用 invocation descriptor 的有限 compiled done Event ID，以及活动 row
  的 invocation ID 元数据；动态名称只在当前 Event envelope 中暴露。
- **事实源：** 固定、session-owned invocation row 是 token、ID 与活动状态的唯一事实源；
  descriptor 是 compiled done Event ID 的只读事实源。host 不复制或推进核心状态。
- **所有权/生命周期：** adapter callback 中的 request 字段只借用至 callback 返回；host
  只保留复制后的 ID/token。session 复制获准 Event payload。完成、取消或关闭清除 row
  后 token 立即 stale；child program/session/executor 全由 host 创建并销毁。
- **拓扑：** 允许潜在并发 host producer；一个 owning serialized session consumer；一个
  single-consumer external FIFO。测试使用两个独立 serial executors，不新增线程协议。
- **顺序：** 已接受的 returned Events 保持 external FIFO；internal Events 保持优先；只有
  matching completion 在 dequeue/preprocess 时终结 matching token。普通 Event、第二个普通
  Event、done 的 admission 顺序即 selection 顺序。
- **容量/背压：** 使用固定 external mailbox 和 invocation registry。满额返回既有明确
  `cflow_mailbox_status`，无重试、fallback 或扩容。测试容量只覆盖声明的最大在途事件。
- **失败：** zero/stale/cancelled/completed token 为 `INVALID_ARGUMENT`。admission 后在
  preprocessing 再次校验，关闭 cancellation race；race loser 被丢弃并计入 rejected。
- **关闭：** session 沿既有 close/cancel/destroy 协议停止接受；host adapter 必须 quiescent。
  测试在销毁前要求 invocation `active == 0`，并要求 parent/child destroy 均成功。
- **可观测性：** 每个场景精确检查 `returned_accepted`、`returned_rejected`、`completed`
  和 `active`。start prepare/commit/discard 与 terminal result prepare/commit/discard 也精确检查。

## 现有实现边界与预期改动

`scxml_session_report_invoke_event()` 在 registry lock 下验证 live token，然后通过
`cflow_statechart_instance_try_send_tagged()` 复制到 external mailbox；
`scxml_runtime_preprocess_invocation_external()` 在 selection 前再次验证 token。
`scxml_session_report_invoke_done()` 从 matching descriptor 取得有限 done Event ID；
preprocess 在消费 matching done 时清空 row、减少 `active` 并增加 `completed`。
当前 Event envelope 从仍活动的 row 复制 ID，并在 `idlocation` 情况下动态构造
`done.invoke.<id>`。

因此预期不修改 production source/public headers。若 strict test 暴露真实缺口，只允许修改
拥有该不变量的 `src/scxml_session.c` 或 `src/scxml_runtime.c` 最小路径；若需要公开语义、
ABI、容量或依赖变化，本任务停止为 `BLOCKED`。

## 兼容性、验证与回滚

公开行为、数据格式、配置、CMake target 和依赖方向保持不变；新增内容是 executable
conformance evidence。主要风险是 fixture 只观察最终 pass 而漏掉错误来源或顺序，因此
严格 helper 独立检查 request/token、所有 report status、结果 effect、invoke stats 和销毁。

验证顺序为：先注册无 fixture 的五个测试并保存真实 RED；再增加最小 fixture/host logic；
focused Release filters；完整 W3C executable；Release build 与 CTest 8/8；可用时 Debug/ASan
focused filters；manifest 202/168/34 与 PASS/UNSUPPORTED/N/A 精确复算；`git diff --check`。
回滚只需撤销测试 helper、五个 fixture（以及 247 child fixture）、manifest/README 晋级和
本设计/计划；没有公开 API、持久数据或部署迁移需要逆转。
