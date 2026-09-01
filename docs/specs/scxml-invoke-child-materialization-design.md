# SCXML invoked child materialization 设计

## 背景与范围

本设计覆盖 W3C SCXML 1.0 强制测试 239–245。它们共同要求 canonical
SCXML invocation 的 host 能够：

- 把 `src` 指向的 SCXML 文档和 `<content>` 中的 SCXML markup 当作文档执行；
- 把 `namelist` 与 `<param>` 物化出的值注入被调用 session 的数据模型；
- 对匹配 child 顶层数据字段的名称应用初值，对未知名称不扩展 child schema；
- 让 `src`/`content`、`namelist`/`param` 在 child 可见行为上分别等价。

规范来源：

- [W3C IRP 239](https://www.w3.org/Voice/2013/scxml-irp/239/test239.txml)
- [W3C IRP 240](https://www.w3.org/Voice/2013/scxml-irp/240/test240.txml)
- [W3C IRP 241](https://www.w3.org/Voice/2013/scxml-irp/241/test241.txml)
- [W3C IRP 242](https://www.w3.org/Voice/2013/scxml-irp/242/test242.txml)
- [W3C IRP 243](https://www.w3.org/Voice/2013/scxml-irp/243/test243.txml)
- [W3C IRP 244](https://www.w3.org/Voice/2013/scxml-irp/244/test244.txml)
- [W3C IRP 245](https://www.w3.org/Voice/2013/scxml-irp/245/test245.txml)
- [W3C SCXML invoke](https://www.w3.org/TR/scxml/#invoke)

## 现有证据

`scxml_invocation_descriptor` 已保存 literal/dynamic type、src、content 和有序
payload descriptors。`materialize_invocation_payload()` 已在 parent 的
SerialExecutor 上把它们转换为 callback-scoped `scxml_payload_view`：inline markup
为 `SCXML_CONTENT_XML_UTF8`，param/namelist 为保序的
`SCXML_PAYLOAD_NAMED`。`scxml_invoke_start_request` 同时携带 token、ID、type、src
与 payload，并明确要求 host 在 callback 返回前复制所有需保留字段。当前 tagged
payload 一次只能表达 inline content 或 named params，分析器也拒绝两者出现在同一个
invoke；原样支持该组合需要新的公开 ABI，不属于本批次。

因此 production 已拥有 parser/eval/runtime 边界，不缺新的 SCXML 语法。缺口位于
host integration：现有测试只检查 start request，尚未证明 host 真正编译 request
指定的 child 文档、以 payload 构造 child 初始状态、运行 child session，再通过 live
token 把 child Event 或 completion 返回 parent。

## 候选方案

| 方案 | 优点 | 代价与风险 | 结论 |
|---|---|---|---|
| core 自动创建 child session | 调用方代码少 | core 需拥有文件加载、schema、executor、child registry，反转现有 host-owned 边界 | 不采用 |
| 扩展 session init API，增加通用 named override | 可直接表达任意 child schema 注入 | 改变公开 ABI；需要通用 schema 写入、转换和错误协议 | 本批次不采用 |
| test host 消费现有 request 并创建真实 child | 验证当前公开边界；无 ABI 变化；与 247/253 的 host-owned child 一致 | 测试 host 需实现有界复制、allowlist 解析与关闭协议 | 采用 |

## 架构与状态归属

新增独立 `scxml_invoke_child_materialization_test`，避免继续扩张已超过 5000 行的
通用 W3C runner。每个 case 运行一个真实 parent session，并按 committed start 顺序
最多创建两个真实 child program/session/executor。

```text
parent runtime
  -> prepare_start(borrowed request)
  -> fixed start ticket row (copied source/payload)
  -> test-thread child runner
       -> resolve allowlisted src OR compile copied XML content
       -> map recognized named scalars into child CMeta initial state
       -> run child session to idle
       -> relay one child #_parent Event OR done through parent token
  -> parent transition / cancel ticket / next invocation
```

Parent program/session 是 invocation token 与 active invocation 的唯一事实源。start
ticket row 只保存 callback 的不可变快照和 terminal 状态；child program/session 是当前
start snapshot 的派生执行实例。child CMeta schema 是可注入字段集合的唯一事实源：host
只映射 exact `child_value` integer entry；仅供 child 自证的 `expected_value` 由 case
配置直接初始化，不接受 payload 写入。未知名称计入 ignored counter，不能创建字段或万能 map。

## 有界数据与并发协议

- **数据单元：** 两个 fixed start rows、两个 fixed cancel rows；每个 start row 复制一份
  token/ID/type/src、最多 4096 bytes XML content，以及最多两个 named scalar entries。
- **所有权：** parent runtime 只借用 request；prepare callback 在返回前完成深复制。
  child runner 拥有 child program/session/executor，销毁后才释放 row。
- **拓扑：** parent 与当前 child 各有一个 SerialExecutor；adapter callback 是单 producer，
  测试线程在 `cflow_executor_wait_idle()` 后成为唯一 consumer。不在 callback 中编译、I/O、
  等待 executor 或递归推进 parent。
- **顺序：** start/cancel ticket 各自按 document order 占用固定 row；commit/discard 恰好一次。
  runner 只消费下一条 READY start，child 返回被 parent 完整处理后才消费后续 start。
- **容量：** 每个 fixture 最多 2 个 invocation、1 个 named entry、1 个 child send；超过容量、
  非 canonical type、同时出现 src/content、非 XML content、非 scalar payload 或过长字段均
  fail fast 为 `SCXML_ADAPTER_INVALID_CONTRACT`。
- **资源解析：** `src` 只接受测试表中精确列出的相对 fixture 名；不接受路径穿越、网络 URL
  或任意文件。inline XML 直接从 row 的 owned bytes 编译。
- **背压/失败：** 无空 row 返回 `SCXML_ADAPTER_FULL`；child compile/init/send/relay 失败使
  case 失败，不重试、不降级到另一种 source。
- **关闭：** 停止 parent admission，等待 parent/child executor idle；销毁 child session，
  再销毁 child executor/program；最后销毁 parent session/executor/program。任何 session
  destroy 失败时保留其依赖，避免 use-after-free。
- **观测：** exact start/cancel prepare/commit/discard、ticket violation、source/content 使用
  次数、recognized/ignored payload 数、child success Event 数、parent/child terminal stats。

## W3C 本地转换

- 239：src child 和 inline child 分别发送不同见证 Event，证明两份 markup 都实际执行。
- 240：把上游 inline child 移为 allowlisted src；先用 namelist、再用 param 注入
  `child_value=1`，两个 child 都必须发送 success。
- 241：使用同一 src child；namelist 与 param 分别注入同一非默认整数，host snapshot
  和 child 结果必须相同。
- 242：src child 与 inline child 执行同一行为，parent 必须观察两次相同 child Event。
- 243：使用 src child；param 的 name 与 child CMeta schema field 匹配，child 必须观察
  注入值。
- 244：使用 src child；namelist key 与同一 field 匹配，child 必须观察注入值。
- 245：使用专用 src child；unknown parent key 不存在于 child schema；host 必须记录
  ignored，child 已知字段仍为 zero，并只能据此发送 success。

CMeta schema 是 TurboSCXML typed data-model 的声明事实源，因此本地转换用 schema field
对应上游 `<data id>`；不复制上游生成器的数字变量名或 ECMAScript-specific 绑定语法。

## 兼容性、验证与回滚

本批次不改变 production source、公开 API/ABI、XML 格式、依赖方向、默认容量或安装导出。
新增一个 test target、十一个 fixture（七 parent、四个 src child）、设计/计划与 corpus 文档。
完成后 corpus 由 137 PASS / 31 UNSUPPORTED 变为 144 PASS / 24 UNSUPPORTED，invoke 只剩
test 250。

验证采用 TDD：先注册七个缺失 fixture 观察 RED；再实现真实 host runner 和 fixture，逐项
GREEN；对 XML content kind、payload 名称/值、unknown ignore 和 ticket terminal 状态做
mutation/failure checks；最后 fresh Release build、完整 CTest、manifest 复算、CodeGraph
同步与 `git diff --check`。回滚只删除新 test target/helper/fixtures 和 corpus 文档变更，
不涉及数据迁移。
