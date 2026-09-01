# SCXML invoke 参数物化设计

## 背景与范围

本设计覆盖 W3C SCXML 1.0 强制测试 215、216、220、225、226、530 和
554。它们共同约束 `invoke` 的启动输入：运行时 `typeexpr`/`srcexpr`、标准
SCXML processor type、同 session 唯一 ID、`src`/`param`/`content` 传递、
`content` 的求值时点，以及参数求值失败时不得启动外部服务。

范围止于 TurboSCXML 的编译描述符、session 内物化、单一 invoke adapter
事务和本地 conformance harness。文件/HTTP 获取、子解释器、服务发现、认证、
授权和持久化仍由 host 实现；本任务不把这些能力嵌入核心，也不把 uSCXML 作为
构建或运行依赖。

## 依据

- W3C SCXML 1.0 [`invoke`](https://www.w3.org/TR/scxml/#invoke) 语义以及
  IRP 215、216、220、225、226、530、554 的官方测试文档。
- 只读参考 `qigao/scxml@c80cedfa43b559861a054e992137685cdd29af16`：
  `BasicContentExecutor::invoke()` 在真正调用 invoker 前读取当前 data model，
  依次物化 type、src、ID、namelist/param 和 content；`SCXMLInvoker` 负责
  host 侧子解释器生命周期。本设计只比较边界和求值时点，不复制代码。
- TurboSCXML 现有 `scxml_runtime_start_stable_invocations_transaction()`、
  CMeta expression/location program 与当前 content-aware invoke adapter 契约。

## 候选边界与选择

候选一是在 TurboSCXML 内直接解析 `src` 并创建子 session。它能提供一体化
体验，但会把资源解析、网络、子解释器配置和安全策略反向带入核心，并绕开
现有 adapter 生命周期。

候选二是把原始 XML attribute 和 expression 交给 host。它保持核心简单，
却让各 host 重复解释 CMeta、ID 和错误语义，无法保证同一 session 内一致的
事务边界。

采用第三种边界：TurboSCXML 在稳定宏步事务中物化所有 SCXML 语义字段，host
只接收一个有界、只借用于 prepare 调用期间的 start request。这样 CFlow 与
TurboSCXML 分别保留配置和 invocation 状态的唯一事实源，host 专注于外部服务。

## 状态、输入与所有权

`cflow_statechart_instance` 独占活动配置、staged CMeta state、事件队列和宏步
事务。`scxml_session_impl` 独占 invocation descriptors、逐 descriptor row、
单调 token、运行统计及 adapter attachment。host 只拥有被接受的 effect ticket
及其预留资源，不得修改 session row。

编译期 descriptor 保存静态 type/src、已编译 `typeexpr`/`srcexpr`、
`idlocation` location program、namelist/param expression programs、content
descriptor、autoforward 和 owner state。字符串、payload 与 content view 只在
`prepare_start` 调用期间有效；异步 host 必须在返回 `ACCEPTED` 前复制所需数据。

每次实际执行 invocation descriptor 都分配一个非零、session 单调递增 token。
显式 `id` 直接进入 request；无 `id` 且无 `idlocation` 时使用编译期稳定的
`owner.invoke.ordinal` descriptor ID；存在 `idlocation` 时才由 owner state ID
与本次 token 组成运行时动态 ID，并写入 staged state。因此同一 session 的并行
或重复 idlocation 执行不会复用 ID，事务回滚也不会泄露半提交值。

## 精确物化与发布顺序

一个调用只在其 owner 于当前稳定配置中仍活动且 row 为 `PENDING` 时处理：

1. 入口 executable content 已经执行，CMeta staged state 包含本宏步的
   `onentry` 赋值；运行时表达式不得读取初始快照或编译期值。
2. 分配 token，并在需要时生成动态 ID、写入 `idlocation`。
3. 在同一个 staged state 上求值 `typeexpr` 和 `srcexpr`；没有表达式时使用
   编译期静态 type/src。标准 type `http://www.w3.org/TR/scxml/` 不被核心拒绝，
   原样交给 host 的 SCXML invoker 实现。
4. 求值 namelist/param，并在统一边界物化 scalar/structured/inline content。
   `content expr` 此刻求值，不能在 XML admission 时提前求值。
5. 构造唯一的 `scxml_invoke_start_request`；其中 payload 可表达 scalar、named
   entries 与结构化 content。所有计数、字符串和动态 ID 均受既有编译/初始化
   容量限制。
6. 在 session lock 外调用 `prepare_start`。只有 `ACCEPTED` 且 ticket 合同完整时，
   才把 START row 和 adapter ticket 一起 stage；CFlow 发布状态后恰好 commit 一次。

host 若以 canonical SCXML type 启动子解释器，可通过已有
`scxml_session_report_invoke_event()`/`scxml_session_report_invoke_done()` 返回
子事件或 `done.invoke.<id>`。这条返回路径不改变启动物化的事实源。

## 错误语义与失败收束

动态 ID 或 `idlocation`、type/src、namelist/param、content 任一求值或转换失败，
都终止当前 invocation element 的后续处理：不调用 `prepare_start`，stage FAIL
row，并向内部队列加入一个 `error.execution`。其他 descriptor 仍按文档顺序处理，
除非 effect/internal-event 容量或不变量失败使整个稳定事务成为 FATAL。

adapter 的业务拒绝沿用其明确的 execution/communication 分类；adapter 合同无效、
token 耗尽、容量不足或 rollback 失败均 fail fast。稳定事务后续失败时，所有已
prepare 的 tickets 恰好 discard，staged ID/data/row 不发布，不存在隐式重试或
无界 fallback。

## 并发、容量与安全边界

物化运行于 session 所属 SerialExecutor；共享 invocation registry 只在读取或
预留 row 时短暂持锁，表达式求值和 host callback 不在锁内。核心不访问网络、
文件系统或 bearer data，不解释 host 凭据。host 可按 type/src、租户/session、
payload size 和权限独立拒绝请求，拒绝结果必须经 adapter status 显式返回。

`invocation_capacity`、payload scratch、event/effect queues、metadata/ID 上限均由
已有配置或编译上限约束。本批次不增加新分配器、队列、锁、缓存或隐藏容量。

## 兼容性与迁移

本设计后续由 [Single Adapter ABI Design](scxml-single-adapter-abi-design.md)
收口为一个公开 adapter ABI。需要 param/content 的文档要求 host 提供对应
payload/content capability。canonical SCXML type 的“支持”由完整平台的
host adapter 提供，TurboSCXML 的保证是接受、物化并原样路由，而不是内建传输。

本批次预计只增加 characterization、W3C fixture 与 corpus 状态。如果直接测试
暴露 production 缺口，只修改 owning runtime/analyzer 层的最小逻辑；任何公开
ABI、依赖或部署变化都视为计划缺陷并停止扩展。

## 验证与回滚

先登记七个 named W3C tests 并保存缺 fixture RED，再增加一个严格 content-aware adapter
characterization：它必须同时观察运行时 type/src、两个不同动态 ID、canonical
type、named payload、执行时 content，以及失败 invocation 的零 start。fixture
随后通过公开 session/adapter/report API 形成终态 witness。

只有七个 fixture 确定通过后，manifest 才从 118/50 更新为 125/43。验证包括
focused TinyTest、完整 W3C executable、Release 全量 CTest、manifest 202 行与
168/34 分类、CodeGraph affected 和 `git diff --check`。若需要回滚，先撤销 corpus
promotion 和 fixture；若存在生产修复，再单独撤销该修复，同时保留失败测试作为
证据。
