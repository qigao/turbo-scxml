# SCXML invoke 取消与迟到 Event 隔离设计

## 背景与范围

本设计覆盖 W3C SCXML 1.0 强制测试 237 和 252：父 session 离开包含
`<invoke>` 的状态时必须取消被调用服务；取消提交后，来自该 invocation 的普通
Event 与 `done.invoke` 都不得进入父 session 的外部队列。

测试 250 要求取消 SCXML 子 session 时依次执行所有活动状态的 `onexit`。现有
`scxml_session_cancel()` 直接调用 CFlow 的受控取消，停止 admission 并终结 instance，
但 CFlow 没有“按当前 configuration 执行退出 action 后取消”的公开操作。因此 250
继续标为 `UNSUPPORTED`；本批次不会用 probe 日志或普通 transition 冒充取消退出。

核心继续只管理 invocation token/descriptor，不创建子解释器。严格测试 host 拥有一个
真实 TurboSCXML child program/session/executor，并通过既有 invoke adapter 的事务票据把
父 session 的取消提交映射为 child session 的受控取消。

## 证据与决策

| IRP | 规范断言 | 本地见证 | 决策 |
|---|---|---|---|
| 237 | invoking state 退出先于 child 完成时，自动取消 child 并停止其处理 | 父 session 退出后必须提交一次 matching cancel；真实 child 的后续 Event admission 被拒绝；同 token 的 completion 被父 session 拒绝 | 晋级 `PASS` |
| 250 | child 取消时执行所有活动状态 `onexit` | 当前 CFlow cancel 不运行 state exit actions | 保持 `UNSUPPORTED`，后续设计受控退出协议 |
| 252 | 取消后收到的 child Event 不得进入父 external queue | cancel commit 后，同 token 的普通 Event 与 completion 均返回 `INVALID_ARGUMENT`；独立 parent timeout Event 才能到达 pass | 晋级 `PASS` |

规范来源：

- [W3C IRP 237](https://www.w3.org/Voice/2013/scxml-irp/237/test237.txml)
- [W3C IRP 250](https://www.w3.org/Voice/2013/scxml-irp/250/test250.txml)
- [W3C IRP 252](https://www.w3.org/Voice/2013/scxml-irp/252/test252.txml)

uSCXML 的 `FastMicroStep`/`LargeMicroStep` 在顶层完成时先按活动 configuration 的
逆序运行 `onexit`，再取消 invoker。该实现仅作为算法边界参考，不复制代码，也不进入
TurboSCXML 的构建或运行依赖。

## 所有权与有界数据路径

- **数据单元：** 父 invocation 的固定 `uint64_t` token、复制后的 invocation ID，
  以及 CFlow mailbox 中复制的有限 Event。
- **事实源：** 父 `scxml_session_impl.invocation_rows` 是 token 活性与 ID 的唯一事实源；
  child instance 的 terminal 状态由 child 自身 CFlow instance 拥有。host 只保存一次
  committed start 的 token/ID 和测试计数，不维护第二份生命周期状态机。
- **所有权：** host 创建并最终销毁 child program/session/executor；父 session 只借用
  adapter user。adapter request 只在 callback 内借用，host 复制需要跨 callback 保留的 ID。
- **拓扑：** 两个独立 SerialExecutor，各自单 consumer；测试线程是 admission producer。
  不增加线程、共享 executor、递归 pump 或 callback 内 wait。
- **顺序：** 父 exit microstep 先清除 live row，再准备并提交 cancel ticket；ticket commit
  调用 `scxml_session_cancel(child)`。只有父、子 executor 都 idle 后，测试才尝试迟到 Event。
- **容量与背压：** parent/child 使用固定 external/internal/completion/effect 容量；每种
  迟到 Event 只尝试一次。满、关闭、取消和非法 token 使用既有明确返回值，不重试、不扩容。
- **关闭：** 先停止 parent/child admission，等待两个 executor idle，要求 adapter
  quiescent，再依次 destroy parent、child 和 executors。每个成功 prepare ticket 恰好 commit
  或 discard 一次。
- **观测：** 精确检查 start/cancel prepare/commit/discard，child `stats.cancelled`，父
  `invoke_stats.cancelled/returned_rejected/active`，最终 result effect，以及所有 destroy 结果。

## 运行时状态与错误语义

父 exit lifecycle commit 在 registry lock 下把 matching active row 清零并减少 `active`，
释放锁后才调用 host `prepare_cancel`。因此 adapter callback 不在 registry mutex 内运行，
而 row 失效先于任何 host cancellation side effect。取消后的：

- `scxml_session_report_invoke_event()` 返回 `CFLOW_MAILBOX_INVALID_ARGUMENT`；
- `scxml_session_report_invoke_done()` 返回 `CFLOW_MAILBOX_INVALID_ARGUMENT`；
- 两次拒绝均增加 `returned_rejected`，不会进入父 external queue；
- child 的普通 Event admission 返回非 `CFLOW_MAILBOX_OK`，其 state 不再推进。

adapter contract 错误仍按既有路径产生 processor error Event；本批次不增加 fallback、静默
丢弃、重试或新错误码。

## 兼容性、验证与回滚

不修改 production source、公开 API/ABI、XML 语法、数据格式、依赖、容量或 CMake target。
新增内容只提供真实双 session 的 executable conformance evidence。两行晋级后 corpus 为
136 mandatory PASS、32 mandatory UNSUPPORTED、34 optional N/A；invoke 未完成 9 项。

验证顺序：注册缺失 fixture 得到 RED；加入最小父/子 fixture 与严格 host；运行 237/252
focused Release；完整 W3C executable；fresh Release build 与全 CTest；复算 manifest；
`git diff --check` 与 focus-marker/占位符扫描。回滚只需移除 helper、fixture、manifest/README
晋级以及本设计/计划，不涉及持久状态或迁移。
