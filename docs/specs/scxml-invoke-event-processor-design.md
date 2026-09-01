# SCXML invoke Event I/O 双向通信设计

## 背景与范围

本设计覆盖 W3C SCXML 1.0 强制测试 253：被调用方是 SCXML session 时，父、子
session 必须能通过 SCXML Event I/O Processor 双向通信。上游测试同时要求父端收到
`childRunning`、子端收到 `parentToChild`，且两端接收 Event 的 `origintype` 都标识
SCXML Event I/O Processor，最后由子端返回 `success`。

TurboSCXML 核心继续只拥有 invocation descriptor/token 与单 session 执行状态。host
拥有被调用的 child program/session/executor、父子 endpoint 关系和 transport pump；不把
子解释器、全局 session registry、文件加载或 transport 线程嵌入 core。

规范来源：

- [W3C IRP 253](https://www.w3.org/Voice/2013/scxml-irp/253/test253.txml)
- [W3C SCXML Event I/O Processor](https://www.w3.org/TR/scxml/#SCXMLEventProcessor)

## 现有证据与缺口

| 证据 | 已证明 | 尚未证明 |
|---|---|---|
| `tests/scxml_event_io_contract_test.c` 的 191 | child 的 `#_parent` 能到达 parent external queue | parent 到同一 invoked child 的回程 |
| 同文件的 192 | parent 的 `#_<invokeid>` 与 child 的 `#_parent` 可双向解析 | 两端 `origintype` 与真实 invoke start/cancel 生命周期 |
| 同文件的 347 | 两个真实 session 可完成三次 Event I/O 往返 | 这两个 session 确实由活动 `<invoke id="foo">` 关联 |
| `scxml_session_try_send_with_metadata()` | 接收 session 原子复制 Event 与有限 metadata | 253 的端到端因果见证 |

因此本批次不修改 production source 或公开 API。若严格测试暴露 production 缺陷，只允许
在拥有该不变量的 session/runtime 路径做最小修复；若需要 ABI、协议或依赖变化，应停止并
重新设计。

## 架构与状态归属

父 fixture 保留一个 `id="foo"` 的 canonical SCXML invocation，并以独立 child fixture
替代上游 inline markup。该转换只隔离测试 239 所跟踪的“解释 invoke markup”能力，不改变
253 所断言的双向 Event I/O 行为。

host 在 parent 初始化前预留 parent/child endpoint，建立：

```text
child #_parent  -> parent endpoint
parent #_foo    -> child endpoint
```

parent 的 invoke adapter 必须观察并提交一次 `foo` start，且请求 type 为
`http://www.w3.org/TR/scxml/`、src 为 `test253-child.scxml`。start 提交后 host 才启动真实
child session。随后按以下因果顺序 pump：

```text
child --childRunning/#_parent--> parent
parent --parentToChild/#_foo----> child
child --success/#_parent--------> parent pass
```

parent 达到 final 并退出 invoking state 时，matching cancel ticket 必须提交一次；此时
child 已达到其 top-level final。该 cancel 只终结 parent invocation row，不制造第二个
child 生命周期事实源。

## 所有权与有界数据路径

- **数据单元：** 固定 host message row，复制 event、target、send ID；start/cancel 各有
  一个带不可变 kind 的固定 ticket row。delivery metadata 复制 sender location，并使用
  `scxml` 作为 W3C 允许的 SCXML processor type 短名。
- **事实源：** parent session invocation row 是 token/ID 活性的唯一事实源；每个 session
  的 CFlow instance 是其 active configuration 的唯一事实源；host router 只拥有 endpoint
  映射和待投递 row。
- **所有权：** adapter request 仅在 callback 期间借用；host 复制需跨 callback 保存的
  invoke ID/token 与 message 字段。host 创建并销毁两个 program/session/executor。
- **拓扑：** 两个独立 SerialExecutor；各 session 单 consumer。adapter callback 是消息
  producer，测试线程是唯一 pump consumer；callback 内不等待 executor、不递归 pump。
- **顺序：** commit 为 message 分配单调 sequence；pump 每次选择最小 READY sequence，
  因而维持全局 FIFO。每次 pump 后等待两个 executor idle，再观察下一条消息。
- **容量/背压：** endpoint 上限 4、message 上限 4；253 最大同时 READY 为 1。无空槽返回
  `SCXML_ADAPTER_FULL`，target mailbox 满返回 `HOST_PUMP_WOULD_BLOCK` 并保留原 row；不扩容、
  不重试隐藏错误、不丢弃已提交消息。
- **失败：** 无效 type、target、invoke ID/src 或生命周期返回明确 adapter 错误；失败不得让
  半初始化 row 可见。每个成功 prepare ticket 恰好 commit 或 discard 一次。
- **关闭：** 先关闭两个 session 停止 admission，再等待 executor idle；host 在 router
  锁下有界释放 RESERVED/READY row，若观察到 INFLIGHT 则 fail fast 且保留依赖。adapter
  quiescent 后销毁 child/parent session，再注销 endpoint、销毁 executor/program/router。
  session 未成功销毁时不得释放其 executor、program 或 router。
- **观测：** 严格检查 3 次 delivery、最终 event `success`、父子 done 且无 error、invoke
  start/cancel prepare/commit/discard 计数、逐 ticket terminal 状态、shutdown 后重复终结
  violation 仍为零，以及 invocation `active == 0`。失败注入在首条 child READY message 后进入
  cleanup，并要求两个 session 均销毁、两个 endpoint 均注销；另一注入在 close 时重复终结
  cancel ticket，证明最终检查不会漏掉迟到 callback。

## 兼容性、验证与回滚

本设计不改变公开 API/ABI、XML 生产语义、数据格式、依赖方向、CMake target 或默认容量。
唯一用户可见变化是 corpus 把 253 从 `UNSUPPORTED` 晋级为可执行 `PASS`：mandatory 变为
137 PASS / 31 UNSUPPORTED，optional 保持 34 N/A，invoke 未完成项降为 8。

验证顺序：先注册新 test kind 并观察 focused RED；实现严格 invoke probe 与父子 fixture；
focused Release GREEN；临时破坏 `origintype` 映射确认测试变 RED 后恢复；fresh Release 全构建
与 CTest；复算 manifest；运行 `git diff --check` 和 focus-marker/占位符扫描。回滚只需移除
测试 helper/fixture、corpus 文档晋级和本设计/计划，不涉及持久数据或部署迁移。
