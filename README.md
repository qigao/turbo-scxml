# TurboSCXML

TurboSCXML 将 W3C SCXML 文档编译为 Salts CFlow Statechart，并提供有界、版本化的宿主事件 I/O、调用与 CMeta 数据模型适配接口。

## 模块边界

- 本仓库拥有 SCXML 文档编译、SCXML session、适配器契约、测试 fixtures 与 W3C conformance corpus。
- Salts 继续拥有 CFlow、CMeta、QueryVM、XmlParser、Core、CSTL 与 TinyTest。
- 依赖方向固定为 `TurboSCXML -> installed Salts`；Salts 不依赖 TurboSCXML。
- HTTP、认证、持久化和服务部署不属于解释器核心；可选
  `TurboSCXML::CHttpResource` 负责受宿主授权的同步资源读取，
  `TurboSCXML::CHttpEventIO` 提供有界 BasicHTTP Event I/O。

公开 C API 继续通过 `<scxml/scxml.h>` 提供，函数与类型保持 `scxml_*` 命名。CMake 消费目标为 `TurboSCXML::SCXML`。

## 构建

构建要求：

- CMake 3.20 或更新版本
- Ninja
- Windows 使用 Visual Studio 2022 开发者命令环境
- `PROJECT_ROOT` 指向包含 `external/pkgs` 的工程根
- `VCPKG_ROOT` 指向 vcpkg checkout
- 对应 profile 的 Salts 已安装到 `$PROJECT_ROOT/external/pkgs/salts/<profile>`

Windows Release：

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user
cmake --build --preset install-win-release-user
```

Linux Release：

```bash
cmake --fresh --preset linux-release-user
cmake --build --preset linux-release-user
ctest --preset linux-release-user
cmake --build --preset install-linux-release-user
```

安装位置由版本化 `CMakeUserPresets.json` 管理：Debug 为 `$PKG_ROOT/turboscxml/debug`，Release 为 `$PKG_ROOT/turboscxml/release`。

启用任一可选 CHTTP 适配器时，匹配 profile 的 Salts SDK 必须包含
`Salts::CHTTP`。仓库提供独立的 `win-dev-chttp-user` 与
`win-release-chttp-user` configure/build/test preset；该 feature 默认关闭，
不会改变 `TurboSCXML::SCXML` 的依赖闭包。

## 下游消费

下游必须显式提供两个同 profile 的安装根，并只从 TurboSCXML 根查找：

```cmake
if(NOT DEFINED ENV{TURBOSCXML_ROOT}
   OR NOT IS_DIRECTORY "$ENV{TURBOSCXML_ROOT}")
  message(FATAL_ERROR "TURBOSCXML_ROOT must name the TurboSCXML install prefix")
endif()
file(TO_CMAKE_PATH "$ENV{TURBOSCXML_ROOT}" TURBOSCXML_ROOT_PATH)
find_package(TurboSCXML CONFIG REQUIRED
  PATHS "${TURBOSCXML_ROOT_PATH}" NO_DEFAULT_PATH)
target_link_libraries(app PRIVATE TurboSCXML::SCXML)
```

`TurboSCXMLConfig.cmake` 会从 `SALTS_ROOT` 精确解析 Salts；缺少变量、目录或 package config 时直接失败，不回退到系统路径。

## VoiceXML 非媒体 Core MVP

安装包导出 `<voicexml/voicexml.h>` 和 `TurboSCXML::VoiceXML`。这是
VoiceXML 2.0/2.1 的孵化、非媒体 profile，不表示完整 VoiceXML conformance：

```cmake
find_package(TurboSCXML CONFIG REQUIRED COMPONENTS VoiceXML
  PATHS "${TURBOSCXML_ROOT_PATH}" NO_DEFAULT_PATH)
target_link_libraries(app PRIVATE TurboSCXML::VoiceXML)
```

该 target 的唯一公开依赖是 `Salts::XmlParser`；VoiceXML core 不依赖 CFlow、
CCXML、CMeta、QuickJS、CHTTP 或 XPath。精确接纳的文档语法为：

```text
document := <vxml xmlns="http://www.w3.org/2001/vxml"
                  version=("2.0" | "2.1")> form+ </vxml>
form     := <form [id=XML-NCName]> block+ </form>
block    := <block/> | <block></block> | <block><exit/></block>
```

`form@id` 若存在，必须非空且在整个文档中唯一。除 `form@id` 外，`vxml`、
`form`、`block` 和 `exit` 不接受其他属性；元素之间仅允许空白文本、XML comment
和 processing instruction，后两者会被忽略。元素按 namespace URI 与 local name
匹配，namespace prefix 不影响接纳。foreign-namespace 元素、无效 UTF-8、重复 ID、
非空 `exit` 内容以及任何不在语法中的结构都会在编译期被拒绝，不会被近似执行。
编译器保存全部 form，但 session 只从第一个 form 开始；当前没有 `goto`。

`vxml_compile` 仅在调用期间读取 source bytes，成功后 program 拥有一份不可变、
与 source 独立的表示；调用方最终用 `vxml_program_destroy` 释放它。session 拥有
自己的运行时存储，但只借用 program，因此 program 必须存活到
`vxml_session_destroy` 之后。session 由调用方串行驱动且不创建线程：成功初始化
进入 `READY`，唯一合法的 `start` 从 `READY` 经 `RUNNING` 同步到 `EXITED`（显式
`exit` 或第一个 form 耗尽）或 `FAILED`。重复 start 确定性失败；`close` 可从任意
非销毁状态调用、可重复调用并进入 `CLOSED`。销毁 session 不会销毁 program。

`vxml_program` 与 `vxml_session` 都是 single-owner、non-copyable handle。不要复制
一个已初始化 handle 后从两个副本销毁同一实现。若需移动所有权，应把完整 handle
传给一个空目标并立即把源 handle 清零；任何 handle 被重新用于 compile/init 前，
必须先销毁它当前拥有的对象。

默认 XML 限制来自 `salts_xml_default_limits()`；此外 `max_forms = 64`、
`max_blocks = 1024`、`max_actions = 4096`、`max_name_bytes = 256 * 1024`。
每个限制都必须大于零；编译失败时输出 program 保持为空。

### Typed CMeta datamodel profile

默认启用的 `TURBOSCXML_ENABLE_VOICEXML_CMETA` 会额外安装
`<voicexml/cmeta.h>` 与 `TurboSCXML::VoiceXMLCMeta`：

```cmake
find_package(TurboSCXML CONFIG REQUIRED COMPONENTS VoiceXMLCMeta
  PATHS "${TURBOSCXML_ROOT_PATH}" NO_DEFAULT_PATH)
target_link_libraries(app PRIVATE TurboSCXML::VoiceXMLCMeta)
```

该组件的外部闭包仅增加 `Salts::CMeta` 与 private `Salts::QueryVM`；它仍通过
`TurboSCXML::VoiceXML` 使用 `Salts::XmlParser`，不发现或链接 SCXML、CFlow、
CSerde、CBind、QuickJS、网络或媒体 package。关闭该选项的 base-only 安装中，
required `VoiceXMLCMeta` 请求会明确失败，`OPTIONAL_COMPONENTS VoiceXMLCMeta`
则令 `TurboSCXML_VoiceXMLCMeta_FOUND` 为 false 且不创建该 target。

只有 `vxml_compile_cmeta` 接纳显式 `datamodel="cmeta"`；普通
`vxml_compile` 会拒绝它。接纳的 data/executable 语法为：

```xml
<vxml xmlns="http://www.w3.org/2001/vxml" version="2.1"
      datamodel="cmeta">
  <var name="documentValue" expr="1"/>
  <form id="main">
    <var name="formValue"/>
    <block name="alreadyVisited" expr="false"/>
    <block name="guard" cond="documentValue > 0">
      <var name="localValue" expr="documentValue"/>
      <assign name="formValue" expr="localValue + 1"/>
      <if cond="formValue == 2">
        <clear namelist="formValue"/>
        <elseif cond="false"/>
        <clear/>
        <else/>
        <exit namelist="formValue localValue"/>
      </if>
    </block>
  </form>
</vxml>
```

根级 `var` 必须位于 `form` 前，form 级 `var` 必须位于 `block` 前；`var`
可省略 `expr`，`assign` 必须同时具有 `name` 与 `expr`。`clear` 可为空，或用
非空、空白分隔的 `namelist`；`if` 必须有 Boolean `cond`，其中零个或多个
带 Boolean `cond` 的空 `elseif` marker 后可有至多一个空 `else` marker。
`block` 可用 `name`、Boolean `expr` 初值及 Boolean `cond` guard；任何 defined
`block@expr` 值（包括 false）都会令该 form item 初始为 ineligible。`exit` 可为空，
或在互斥的 `expr` 与空白分隔 `namelist` 中选择一个。所有未列出的属性、放置、
内容与非空 executable text 都会被拒绝。

宿主提供的 CMeta root struct 同时是 application scope 初值和类型目录：每个
source `var@name` 必须匹配一个 top-level root field，并继承其 semantic type。
root `var` 创建 document scope，form `var` 创建 form/dialog scope，block body
中的 `var` 创建 anonymous block scope；查找顺序为 anonymous、form、document、
application。descriptor 和 `semantic_data` 中的 metadata 以
`cmeta_type_equal` 判断语义相等，不使用指针地址。每个 slot 区分 undeclared、
declared-but-undefined 与 defined（零、false、空字符串仍是 defined）三种状态。
anonymous scope 在同一 dialog 的 `clear` revisit 之间持续存在，到 dialog 执行
结束才释放。

非媒体 FIA 先复制 application fields，按源码顺序初始化 document variables，
进入第一个 form，再按源码顺序初始化 form variables 与 `block@expr`。随后每轮按
文档顺序选择第一个 form-item 为 undefined 且 `cond` 为 true 的 block，先把它
标为已访问，再执行 actions；`if` 只执行第一个匹配 branch，`clear` 可令 block
再次 eligible。`max_execution_steps` 同时计 selection 与 action step，因此自清除
循环会以 `VXML_LIMIT_EXCEEDED` 结束，而不会无限运行。

一次完整 block 执行是一个 transaction：application、document、form、form-item
及 active anonymous frame 全部先进入 staging，成功后统一 commit；表达式、转换、
分配或 limit 失败会销毁 staging、保留全部 committed bytes/bound bits 并进入
`FAILED`。这种 whole-block rollback 是刻意的非标准安全偏差；标准 VoiceXML 的
imperative executable content 不会因后续 element 抛错而撤销先前写入。加入 scoped
catch handler 前不得把本行为描述成标准兼容语义。

所有 options struct 都按 ABI version 与 `struct_size` 验证；v1 会忽略完整 v1
prefix 后的 forward tail。除 core 的 XML/form/block/action/name 限制外，compile
必须提供正数 `max_expression_bytes`、`max_expression_instructions`、
`max_expression_operands`、`max_expression_depth`、`max_path_depth`、
`max_literal_bytes`、`max_string_bytes`、`max_scope_slots`、
`max_scope_storage_bytes`、`max_conditional_depth`；session 还必须提供正数
`max_transaction_bytes` 与 `max_execution_steps`。越界会明确失败，不会无界增长。

root/可达 descriptor 借用到 program 销毁；`semantic_data` 指针数组会复制，但
descriptor 对象仍借用。`initial_root` 只在 session init 期间借用，各 field 随即
复制到 session-owned storage。普通 `vxml_session_cmeta_read` 的 string view 使用
session-owned scratch，有效到下一次 read、任何 mutating session call、close 或
destroy；exit result 的 name/string storage 由 session 拥有，有效到 close 或
destroy。

本 profile 不支持 `application.x`、`document.x`、`dialog.x`、`session.x` 等
qualified scope object，也不支持 application-root/document aliasing。QuickJS、
ECMAScript、DOM、`script`、`data`、resource retrieval，以及 `prompt`、`audio`、
SSML、所有媒体 API/callback、grammar/recognition、`field`、`filled`、`record`、
`transfer` 均仍是 deferred/unsupported；这里没有完整 VoiceXML conformance 声明。

| VoiceXML surface | 当前状态 |
|---|---|
| `form` / `block` / `exit` 与同步 `EXITED` 生命周期 | 支持（仅限上述语法） |
| 显式 `datamodel="cmeta"`、typed `var` / `assign` / `clear` / `if` / exit data | `VoiceXMLCMeta` 支持（仅限上述 profile） |
| `prompt`、`audio`、SSML | 延后且不支持 |
| recognition、grammar、collect、`field`、`filled` | 延后且不支持 |
| `record`、`transfer` 与 resource/script/data | 延后且不支持 |
| 所有 media API/function、callback、ticket、token、wait state | 不存在；延后且不支持 |

不支持的元素或属性返回明确的编译错误。core 没有隐藏的媒体对象、媒体函数、
callback、effect ticket、completion token 或等待状态。总体 roadmap 见
[#41](https://github.com/qigao/turbo-scxml/issues/41)，本非媒体 core 范围见
[#42](https://github.com/qigao/turbo-scxml/issues/42)，prompt/audio/SSML 与媒体
provider 工作见 [#47](https://github.com/qigao/turbo-scxml/issues/47)。架构、
所有权与未来 adapter 隔离详见
[`docs/specs/voicexml-architecture-design.md`](docs/specs/voicexml-architecture-design.md)，
typed profile 的完整契约见
[`docs/specs/voicexml-cmeta-design.md`](docs/specs/voicexml-cmeta-design.md)。

## 可选 CHTTP 资源适配器

启用 `TURBOSCXML_ENABLE_CHTTP_RESOURCE` 后会额外安装
`<scxml/chttp_resource.h>` 和 `TurboSCXML::CHttpResource`：

```cmake
find_package(TurboSCXML CONFIG REQUIRED COMPONENTS CHttpResource
  PATHS "${TURBOSCXML_ROOT_PATH}" NO_DEFAULT_PATH)
target_link_libraries(app PRIVATE TurboSCXML::CHttpResource)
```

适配器借用一个 single-owner `chttp_client`。宿主 resolver 必须把逻辑 URI
映射为经过授权的 `connection_uri`、`authority`、origin-form `target` 和精确
`Content-Type`；数据资源还需返回一个 CSerde decoder。适配器在任何网络
调用前拒绝未授权、无界、格式错误或 `https:` 输入，只接受最终 2xx 响应，
不跟随重定向、不重试请求、不降级 TLS，并在 generic resource `close` 前
保留 CHTTP response 与 decoder reader 的所有权。由于当前 CHTTP 仅支持
`tcp://` 与 `pipe://`，TLS 资源应由另一个宿主资源 adapter 提供。

宿主必须用正数 connect/read/write deadline 初始化借入的 CHTTP client，且
其 `max_response_body_bytes` 不得大于 adapter 的同名上限。adapter 的
`timeout_ms` 约束 HTTP 结果等待；CHTTP 为保证 client 可安全复用，可能在
deadline 后继续完成取消与 drain，因此它不是整个 `open()` 的墙钟硬上限。

完整 C/C++ 安装消费入口位于 `tests/install_consumer/`；传输策略与释放语义
的可执行例子位于 `tests/scxml_chttp_resource_test.c`。

## 可选 BasicHTTP Event I/O Processor

启用 `TURBOSCXML_ENABLE_CHTTP_EVENT_IO` 后会额外安装
`<scxml/chttp_event_io.h>` 和 `TurboSCXML::CHttpEventIO`：

```cmake
find_package(TurboSCXML CONFIG REQUIRED COMPONENTS CHttpEventIO
  PATHS "${TURBOSCXML_ROOT_PATH}" NO_DEFAULT_PATH)
target_link_libraries(app PRIVATE TurboSCXML::CHttpEventIO)
```

该组件实现 SCXML 1.0 可选 BasicHTTP Event I/O Processor：每个 binding
发布独立 `_ioprocessors.basichttp.location`，以真实 HTTP POST 接收入站
Event，并在有界 worker 上发送出站 Event。宿主仍须提供标准 SCXML Event
router、默认拒绝的目标 resolver、正数网络 deadline、固定容量，以及把入站
数据转换为 session 可复制 `scxml_content_view` 的 decoder。

生命周期顺序固定为：processor init/start、binding init、把 binding descriptor
与私有 composite adapter 放入 session config、session init、binding activate；
关闭时依次销毁 session 和 binding，再 stop/destroy processor。该组件当前仅启用
明文 HTTP transport；resolver 不得把 `https:` 降级为明文。

完整配置、错误/背压语义和可编译集成骨架见
[`docs/scxml-chttp-event-io.md`](docs/scxml-chttp-event-io.md)。

## CCXML Core MVP

安装包同时导出 `<ccxml/ccxml.h>` 与 `TurboSCXML::CCXML`。该组件是构建在
TurboSCXML 公共 adapter/effect 契约上的 CCXML 1.0 孵化实现，不表示完整
CCXML conformance。当前垂直切片支持一个 `<eventprocessor>`、按文档顺序的
大小写不敏感 `<transition event="...">` glob 匹配（`*` 匹配任意长度子串，
省略 `event` 表示 catch-all）、一个 CMeta-backed 字符串 `<var>`、
`eventprocessor@statevariable`、空白分隔的 `transition@state` 和字符串字面量
`<assign>`、由 datamodel adapter 编译的 CMeta 布尔 `transition@cond` 和可嵌套的
`<if cond="...">` / `<elseif cond="..."/>` / `<else/>` executable content，以及空
`<accept/>`/`<exit/>` 和带有
字符串字面量或 typed CMeta 字符串 `dest` 表达式的 `<createcall/>`/`<redirect/>`
和默认目标的 `<disconnect/>`/`<reject/>`，以及两个字面量资源 ID 的默认
全双工 `<join/>`、双资源 `<unjoin/>`、两个连接的 `<merge/>`，以及受限
`<createconference/>`（可选字面量或 typed CMeta `confname`）/
`<destroyconference/>` conference 生命周期和
detached `<dialogprepare/>`、prepared/direct `<dialogstart/>` 和 normal
`<dialogterminate/>` VoiceXML provider 生命周期，以及受限 `<send/>`（字面量
`target`/`name`、可选字面量 `targettype` 与 CSS 时间 `delay`、可选点分
`sendid` 写回位置）和 `<cancel/>`，默认 `ccxml` 和零延迟：

```xml
<transition event="ccxml.loaded">
  <createcall dest="'tel:+12025550123'"/>
</transition>
<transition event="dial.next">
  <createcall dest="call.destination"/>
</transition>
<transition event="connection.connected">
  <disconnect/>
</transition>
<transition event="connection.alerting">
  <reject/>
</transition>
<transition event="connection.connected">
  <redirect dest="call.redirectDestination"/>
</transition>
<transition event="conference.request">
  <join id1="'call-a'" id2="'conference-b'"/>
</transition>
<transition event="conference.release">
  <unjoin id1="'call-a'" id2="'conference-b'"/>
</transition>
<transition event="connection.transfer">
  <merge connectionid1="'call-a'" connectionid2="'call-b'"/>
</transition>
<transition event="conference.request">
  <createconference conferenceid="conference.id" confname="conference.name"/>
</transition>
<transition event="conference.release">
  <destroyconference conferenceid="conference.id"/>
</transition>
<transition event="ccxml.loaded">
  <dialogprepare dialogid="dialog.prepared"
                 src="'https://voice.example/menu.vxml'"/>
</transition>
<transition event="connection.connected">
  <dialogstart prepareddialogid="dialog.prepared"
               connectionid="event$.connectionid"/>
</transition>
<transition event="dialog.stop">
  <dialogterminate dialogid="dialog.prepared"/>
</transition>
<transition event="connection.connected">
  <dialogstart dialogid="dialog.id"
               src="'https://voice.example/menu.vxml'"
               connectionid="event$.connectionid"/>
</transition>
<transition event="dialog.stop">
  <dialogterminate dialogid="dialog.id"/>
</transition>
<transition event="call.notice.ready">
  <send target="'session:supervisor'" name="'call.notice'" delay="'250ms'"
        sendid="request.pending"/>
</transition>
<transition event="call.notice.cancel">
  <cancel sendid="request.pending"/>
</transition>
```

受限状态机切片使用一个 root 字符串变量；session 初始化成功时才提交初值，
state guard 在事件选择阶段通过 `ccxml_datamodel_adapter_v1` 读取当前值。`state`
可列出多个空白分隔的、大小写敏感的值，`assign` 只能写已声明变量且当前只接受
非空字符串字面量。assignment 和同一 transition 的电话操作共享 CFlow effect
journal，任一后续 prepare 失败都会回滚状态写入：

```xml
<var name="mode" expr="'waiting'"/>
<eventprocessor statevariable="mode">
  <transition state="waiting idle" event="connection.connected"
              cond="mode != &quot;disabled&quot;">
    <assign name="mode" expr="'active'"/>
  </transition>
  <transition state="active" event="dialog.exit">
    <exit/>
  </transition>
</eventprocessor>
```

`cond` 的 XML 实体在 program compile 阶段解码并计入 retained-byte 上限；
session admission 再通过 `ccxml_datamodel_adapter_v1` 的可选 condition tail 编译
一次。每次 dispatch 在 event/state 匹配后求值，false 继续检查下一条 transition，
adapter 错误则停止本次选择。内置 CMeta adapter 复用 TurboSCXML 的有界布尔
表达式 VM，可读取 CMeta root 字段和 `_event.name`；不支持 SCXML `In()` 或通用
ECMAScript。旧 adapter 不使用 `cond` 时仍按原 size prefix 工作。

`<if>` 的 `cond` 与每个 `<elseif>` 的 `cond` 使用相同的 CMeta condition tail：
program 会把控制流 flatten 为有界 action rows，session admission 一次性编译所有
条件，dispatch 只求值直到选中一个 branch。`<elseif>` 和 `<else>` 是空 branch
marker，必须直接位于 `<if>` 内；`<else>` 至多一次且之后不能再出现 `<elseif>`。
false branch 跳至下一个 marker，任何 condition 错误都会回滚该 transition 已准备的
effects。XPath 与通用 ECMAScript 仍不在此 profile 内。

受限 `<foreach array="..." item="...">` profile 复用 SCXML 的 CMeta sequence
与 scope 运行时。`array` 必须是点分 CMeta sequence location，`item` 是由 session
拥有的强类型 supplemental value；应用 root 不会因绑定 loop item 而被修改。
session 为该 scope 预分配 committed/staged/checkpoint 三个 view，并在整个
transition 的 provider tickets 都成功后才提交最后一个 item。任一 sequence
snapshot、iteration 或 action prepare 失败都会释放 snapshot、逆序 discard 已准备
tickets，并丢弃 staged scope。`max_foreach_iterations` 限制单个循环的展开次数，
`max_foreach_storage_bytes` 限制三个 view、最大 snapshot 与 scratch 的总存储。
循环 body 可包含同一套有界 `<if>`/`<elseif>`/`<else>` 控制流；条件编译时绑定
typed supplemental schema，运行时读取当前 staged item，因此失败仍随整个 transition
回滚。可选 `index` 是不与 item 或 application-root location 重名的 NCName，也是
session-owned staged supplemental value，类型固定为 `size_t`；每次迭代与 item
一起更新并参与同一提交/回滚。当前 CCXML profile 只接纳具有
trivial-copy/trivial-destroy traits 的 element，且 element 必须有内置、可从 root
schema 到达、或通过 `ccxml_cmeta_datamodel_config_v1.semantic_data` 显式注册的
semantic `cmeta_data_desc`。显式 registry 按 CMeta semantic type identity 匹配，
因此 `Vec<Struct>` 无需为了 `item.member` 条件而在 application root 中添加虚假字段。
datamodel owner 会复制 descriptor 指针数组，但 descriptor 对象本身仍是 borrowed，
必须存活到所有关联 session 销毁。managed copy 的内部堆分配无法由 byte limit 计量，
因此在 admission 阶段拒绝。嵌套 foreach 尚不支持，而且 foreach 要求直接使用内置
`ccxml_cmeta_datamodel_adapter()`。

```cmake
find_package(TurboSCXML CONFIG REQUIRED COMPONENTS SCXML CCXML
  PATHS "${TURBOSCXML_ROOT_PATH}" NO_DEFAULT_PATH)
target_link_libraries(app PRIVATE TurboSCXML::CCXML)
```

```c
#include <ccxml/ccxml.h>

ccxml_program program = {0};
ccxml_diagnostic diagnostic = {0};
ccxml_status status = ccxml_compile(
    &program, document, document_size, NULL, &diagnostic);
```

session 通过 `ccxml_telephony_adapter_v1` 注入电话平台。`<accept/>` 默认使用
当前 Event 的 connection identifier；provider 在 prepare 阶段返回 move-only
effect ticket，同一 transition 的 effects 全部准备成功后才按顺序 commit，任一
失败则逆序 discard。`<exit/>` 在已准备 effects 提交后终止 session 并且只关闭
adapter 一次。

`<createcall/>` commit 后由 provider 异步发起呼叫，并通过已有 event dispatch
边界回送 `connection.progressing`、`connection.connected` 或
`connection.failed`。非字面量 `dest` 通过 size-versioned datamodel expression
tail 在 session admission 编译，并在 provider prepare 前求值；内置 CMeta adapter
支持 root 字符串路径以及 `<foreach>` 中当前 staged `item.member` 字符串路径。
求值结果是只在紧随其后的 provider prepare callback 内有效的 borrowed view，
provider 若需保留必须复制。字面量 program 不要求该 tail，旧 adapter prefix 继续
可用。此 profile 不使用 XPath；QuickJS 若要支持同一边界，需要先抽取当前
SCXML-session-specific runtime。核心不包含 SIP/RTP backend，也不会伪造平台结果。

`<send/>` 复用 `scxml_event_io_adapter` 的 move-only prepare/commit/discard
边界；`scxml_send_request.type` 承载 CCXML `targettype`，具体 `ccxml`、`dialog`
或 `basichttp` 路由由 session-bound 宿主实现，而不是套用 SCXML target 规则。
字面量 `delay` 在 compile 阶段解析为精确毫秒；只有实际包含非零延迟的 program
才要求 adapter 声明 `SCXML_EVENT_IO_CAP_DELAYED_SEND`，省略或零延迟仍可使用
仅支持 SEND 的 adapter。
commit 后的 `send.successful` 或 `error.send.*` 也由宿主通过串行 CCXML event
dispatch 边界回送，核心不自行合成平台结果。
`sendid` 当前接受点分 NCName 可写字符串位置；核心按 session UUID 与递增 token
生成有界 ID，先提交 datamodel 写回，再发布 send。`<cancel sendid="..."/>`
接受点分可读字符串位置或字符串字面量，并复用 adapter 的
`SCXML_EVENT_IO_CAP_CANCEL`/`prepare_cancel`。宿主继续拥有 delayed registry、
取消竞态及 `cancel.successful`/`error.notallowed` 结果事件。`namelist` 接受最多
`SCXML_PAYLOAD_MAX_ENTRIES` 个 XML 解码后的点分 NCName 位置，保留顺序和限定名，
并通过共享 `scxml_payload_view` 传递标量或 CMeta 对象视图；非空列表要求
datamodel payload-read 尾部能力与 `SCXML_EVENT_IO_CAP_PAYLOAD`。Event I/O
provider 必须在 prepare 返回前复制需保留的数据。由于外部 datamodel
写入只在 transition commit 后可见，send 与读取其 ID 的 cancel 应位于不同
transition；同理，namelist 不会看到同一 transition 中更早 staged 的 assign。
当前尚不支持动态 `delay`、任意 ECMAScript `sendid`、通用 ECMAScript namelist
表达式和 inline content，这些形式会在 compile 阶段被明确拒绝。

adapter 的 `prepare_create_call` 是 `struct_size` 保护的尾部 capability；只使用
原有 action 的旧 v1 provider 前缀继续可用。

`<disconnect/>` 默认使用当前 Event 的非空 connection identifier。commit
仅向 provider 提交断开请求，不终止 CCXML session；provider 应随后通过同一
event dispatch 边界回送 `connection.disconnected` 或失败事件。缺失或非法的
当前 connection identifier 返回 `CCXML_INVALID_EVENT`，并回滚同一 transition
中更早准备的 effects。`prepare_disconnect` 同样是 `struct_size` 保护的追加
capability，不使用该 action 的旧 provider 仍保持兼容。connection registry
与实际电话连接生命周期继续由 provider 管理。

`<reject/>` 同样默认使用当前 Event 的非空 connection identifier，并通过追加的
`prepare_reject` capability 提交。核心不猜测连接是否处于 `ALERTING`：provider
依据权威连接状态执行，并异步回送 `connection.disconnected`、
`connection.reject.failed` 或 `connection.failed`。提交 reject 不会终止
CCXML session；缺失或非法 identifier 仍按事务规则回滚。

`<redirect/>` 接收非空字符串字面量或 typed CMeta 字符串 `dest`，默认重定向当前
Event 的 connection identifier。动态目标复用 createcall 的 size-versioned
datamodel expression tail，在确认当前 connection 合法后、紧邻 provider prepare
之前求值；`<foreach>` body 中读取当前 staged `item.member`。核心通过追加的
`prepare_redirect` capability 提交请求，
不终止 CCXML session；provider 负责校验 `ALERTING`/`CONNECTED` 状态、解除已有
bridge，并异步回送 `connection.redirected`、`connection.redirect.failed`、
`connection.failed` 及需要的 `conference.unjoined`。连接或目标字节若需在
callback 返回后继续使用，provider 必须自行复制。

`<join/>` 要求 `id1` 和 `id2` 都是非空字符串字面量；省略 `duplex` 使用标准的
全双工默认值。追加的 `prepare_join` 只提交 bridge 请求，不终止 session。
provider 权威校验 connection/conference/dialog ID、session ownership、已有
bridge 与媒体容量，并异步回送 `conference.joined` 或
`error.conference.join`；核心不维护资源/bridge registry，也不伪造结果事件。

`<unjoin/>` 同样要求两个非空字符串字面量 ID。追加的 `prepare_unjoin`
capability 只提交 bridge teardown 请求；provider 校验资源、session ownership
和已有 bridge，负责媒体拆除及通知 fan-out，并异步回送
`conference.unjoined` 或 `error.conference.unjoin`。它不依赖当前 Event 的
connection identifier，也不终止 CCXML session；不使用 unjoin 的 join-era
adapter 前缀继续有效。

`<merge/>` 要求 `connectionid1` 和 `connectionid2` 都是非空字符串字面量。
追加的 `prepare_merge` capability 提交 network-level merge 请求；provider
权威校验两个连接及 session ownership，执行 signaling，并拆除受影响的 bridge
和媒体路径。成功时 provider 为两个连接分别回送 `connection.merged`，并回送
所需的 `conference.unjoined`；失败时回送单个 `connection.merge.failed`。核心
不修改 provider connection 状态，也不终止 CCXML session。不使用 merge 的
unjoin-era adapter 前缀继续有效。

`<createconference/>` 要求 `conferenceid` 是点分 NCName 左值；它不会作为普通
ID 传给电话 provider。可选 `confname` 接受非空字符串字面量或 typed CMeta
字符串表达式；动态值在 session admission 编译，并在 provider prepare 前求值，
`<foreach>` 内可读取当前 staged `item.member`。provider 的
`prepare_create_conference` 在预留电话资源的同时返回生成的 conference ID，
核心随即通过独立 `ccxml_datamodel_adapter_v1` 准备左值写回；成功时先提交写回、
再发布 provider operation，避免结果事件观察到旧值；任一 prepare 失败则逆序
discard。内置
`ccxml_cmeta_datamodel` 可将 ID 写入 CMeta schema 中的嵌套 owned-string 字段，
且 live state 在 commit 前保持不变。provider 继续拥有全局 conference registry、
同名 attach/lookup、session 终止时的隐式 detach，并异步回送
`conference.created` 或 `error.conference.create`。

`<destroyconference/>` 的 `conferenceid` 接受非空字符串字面量，或点分 NCName
datamodel location。location 由追加的只读 datamodel capability 在 session
初始化时验证，并在 dispatch 时求值；`prepare_destroy_conference` 只会收到
求值后的 ID，不会收到左值路径或表达式原文。provider 提交后负责 detach 当前
session，只在没有其他 session attachment 时销毁全局 conference，并异步回送
`conference.destroyed` 或 `error.conference.destroy`。literal 形式不要求
datamodel；旧 createconference writeback 前缀在只使用写回时继续有效。

`<dialogstart/>` 当前要求 `dialogid` 是点分 NCName 写入位置，`src` 是非空
字符串字面量，`connectionid` 必须严格为 `event$.connectionid`。追加的
`prepare_dialog_start` 接收 source、默认 `application/voicexml+xml` MIME 和
当前 Event 的 connection ID，并返回 provider 生成的 dialog ID 与启动 ticket。
核心先提交 CMeta/datamodel ID 写回，再提交 provider 启动，因此异步
`dialog.started`、`error.dialog.notstarted` 或最终 `dialog.exit` 不会观察到旧
dialog ID。URI 策略、抓取、VoiceXML 解释器、媒体 bridge 和结果事件均由
provider 负责；核心不内置这些实现。

`<dialogprepare/>` 当前要求 `dialogid` 是点分 NCName 写入位置，`src` 是非空
字符串字面量，并且不接受 connection/conference 媒体目标。追加的
`prepare_dialog_prepare` 接收 source 和默认 `application/voicexml+xml` MIME，
返回 provider 生成的 prepared dialog ID 与准备 ticket。核心同样先提交
CMeta/datamodel ID 写回，再发布异步准备；provider 负责 URI 抓取、VoiceXML
环境准备以及 `dialog.prepared`/`error.dialog.notprepared`。已准备或正在准备的
dialog 可由现有 `<dialogterminate/>` 取消。

prepared `<dialogstart/>` 要求 `prepareddialogid` 是点分 NCName 可读位置，
`connectionid` 必须严格为 `event$.connectionid`。核心先验证当前 Event 的
connection ID，再通过 CMeta/datamodel 读取 prepared dialog ID，并把两个求值后
的 ID 交给追加的 `prepare_prepared_dialog_start`。它不会创建或写回新的 dialog
ID；provider 负责 prepared registry 查找、状态校验、媒体 attach、VoiceXML
执行，以及异步 `dialog.started`/`error.dialog.notstarted` 和最终
`dialog.exit`。该 profile 与现有 direct-source `<dialogstart/>` 并存。

`<dialogterminate/>` 的 `dialogid` 接受非空字符串字面量或点分 NCName
datamodel location。省略 `immediate` 使用 normal termination（`false`）；核心
把求值后的 ID 和该模式交给追加的 `prepare_dialog_terminate`，不要求当前 Event
携带 connection ID，也不会终止 CCXML session。provider 负责权威 dialog 状态、
normal cleanup/返回值、媒体 bridge teardown、`conference.unjoined` 和唯一的
最终 `dialog.exit`。literal 形式不要求 datamodel。

```c
ccxml_cmeta_datamodel model = {0};
const cmeta_data_desc *const semantic_data[] = {
    &application_item_schema};
ccxml_cmeta_datamodel_config_v1 model_config = {
    .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
    .struct_size = sizeof(model_config),
    .root = &application_state_schema,
    .state = &application_state,
    .max_path_depth = 8u,
    .max_string_bytes = 256u,
    .semantic_data = semantic_data,
    .semantic_data_count = sizeof(semantic_data) /
        sizeof(semantic_data[0])};
ccxml_cmeta_datamodel_init(&model, &model_config);

ccxml_session_config session_config = {
    .program = &program,
    .telephony = &telephony,
    .telephony_user = telephony_user,
    .datamodel = ccxml_cmeta_datamodel_adapter(),
    .datamodel_user = &model};
```

当前明确不支持 SIP/RTP backend、完整 dialog 生命周期（`<dialogprepare/>` 的
connection/conference、parameters、media direction、显式 MIME、fetch/hints 等
可选形式，`<dialogterminate/>` 的显式 `immediate`/`hints`，以及
`<dialogstart/>` 的 conference、parameters、media direction、
显式 MIME、fetch/hints 等形式）、
通用 ECMAScript、非 CMeta datamodel 的条件表达式、多 root 变量、非字符串变量以及
非字面量 `<var>`/`<assign>` 表达式，
`<createcall>` 的可选属性、非字符串 typed 表达式或通用 ECMAScript、`<disconnect>` 的
`connectionid`/`reason`/`hints` 属性、`<reject>` 的
`connectionid`/`reason`/`hints` 属性、`<redirect>` 的
`connectionid`/`reason`/`hints` 属性、`<join>` 的
`duplex`/`hints`/tone/gain/clamp 属性或非字面量 ID、`<unjoin>` 的 `hints`
属性或非字面量 ID、`<merge>` 的 `hints` 属性或非字面量 connection ID、
`<createconference>` 的 `reservedtalkers`/`reservedlisteners`/`hints` 属性、
通用 ECMAScript `conferenceid` 左值、
`<destroyconference>` 的 `hints` 属性、escaped literal 或任意 ECMAScript
expression、
`<send>` 的动态 `delay`、任意 ECMAScript `sendid`、非点分位置 namelist、
inline content 或其他非字面量表达式、
嵌套 `<foreach>`、managed/opaque foreach element、
文档切换和内置 VoiceXML interpreter；编译器会拒绝这些 construct，
而不是近似执行。核心边界见
[`docs/specs/ccxml-core-mvp-design.md`](docs/specs/ccxml-core-mvp-design.md)，
send 切片与共享 Event I/O 边界见
[`docs/specs/ccxml-send-design.md`](docs/specs/ccxml-send-design.md)，
literal delay 增量见
[`docs/specs/ccxml-send-delay-design.md`](docs/specs/ccxml-send-delay-design.md)，
send identifier 与 cancel 增量见
[`docs/specs/ccxml-send-cancel-design.md`](docs/specs/ccxml-send-cancel-design.md)，
send namelist 与结构化 payload 增量见
[`docs/specs/ccxml-send-namelist-design.md`](docs/specs/ccxml-send-namelist-design.md)，
外呼切片的所有权与 ABI 语义见
[`docs/specs/ccxml-createcall-design.md`](docs/specs/ccxml-createcall-design.md)，
动态外呼字符串表达式见
[`docs/specs/ccxml-dynamic-string-expression-design.md`](docs/specs/ccxml-dynamic-string-expression-design.md)，
断开连接切片见
[`docs/specs/ccxml-disconnect-design.md`](docs/specs/ccxml-disconnect-design.md)，
拒接切片见
[`docs/specs/ccxml-reject-design.md`](docs/specs/ccxml-reject-design.md)，
重定向切片见
[`docs/specs/ccxml-redirect-design.md`](docs/specs/ccxml-redirect-design.md)，
bridge join 切片见
[`docs/specs/ccxml-join-design.md`](docs/specs/ccxml-join-design.md)，bridge unjoin
切片见
[`docs/specs/ccxml-unjoin-design.md`](docs/specs/ccxml-unjoin-design.md)，network
merge 切片见
[`docs/specs/ccxml-merge-design.md`](docs/specs/ccxml-merge-design.md)，conference
创建与 CMeta 写回边界见
[`docs/specs/ccxml-createconference-design.md`](docs/specs/ccxml-createconference-design.md)，conference
动态名称增量见
[`docs/specs/ccxml-dynamic-conference-name-design.md`](docs/specs/ccxml-dynamic-conference-name-design.md)，conference
销毁与 CMeta 读取边界见
[`docs/specs/ccxml-destroyconference-design.md`](docs/specs/ccxml-destroyconference-design.md)，
直接 dialog 启动与 provider/ID 写回边界见
[`docs/specs/ccxml-dialogstart-design.md`](docs/specs/ccxml-dialogstart-design.md)，normal
dialog termination 边界见
[`docs/specs/ccxml-dialogterminate-design.md`](docs/specs/ccxml-dialogterminate-design.md)，
detached dialog preparation 边界见
[`docs/specs/ccxml-dialogprepare-design.md`](docs/specs/ccxml-dialogprepare-design.md)，
prepared dialog 启动与 CMeta 读取边界见
[`docs/specs/ccxml-prepared-dialogstart-design.md`](docs/specs/ccxml-prepared-dialogstart-design.md)。

## CMeta 表达式

`datamodel="cmeta"` 使用 TurboSCXML 内置的有限、强类型表达式语言。CMeta
提供字段与标量类型描述，TurboSCXML 负责语法解析和 SCXML 错误语义；它不是
完整 ECMAScript，也不会隐式调用 QuickJS。

表达式支持 reflected location、标量字面量、`In()`、SCXML system values、
比较、`!`、`&&`、`||`，以及以下算术优先级：

```text
unary + -
* / %
+ -
< <= == != >= >
&&
||
```

同型 signed/unsigned integer 运算保持原类型并检查溢出；任一操作数为
floating 时结果为 `double`。signed 与 unsigned integer 不能直接混合运算，
`%` 只接受同型整数，unsigned subtraction 不允许下溢。整数溢出、除零、
remainder by zero 或非有限浮点结果都会 fail fast，并由已有 executable
content 边界转换为 `error.execution`。

例如：

```xml
<assign location="invoice.total" expr="invoice.subtotal + invoice.tax"/>
<transition cond="attempts + 1 &lt; maxAttempts" target="retry"/>
```

CMeta profile 也支持编译期注册的自定义 executable action。元素由 namespace
URI 和 local name 精确匹配；每个无 namespace 的 XML attribute 按注册顺序编译
为 callable 参数表达式。首版支持 `bool`、`int`、`long`、`float`、`double`
参数和返回值（返回值丢弃）：

```c
typed_any_raw(CMETA_EFFECT_IO, CMETA_PROP_DETERMINISTIC,
              int, record_value, (int value)) {
    observe(value);
    return value;
}

static const char *const params[] = {"value"};
const scxml_cmeta_custom_action_v1 actions[] = {{
    .namespace_uri = "urn:example:actions",
    .namespace_uri_size = sizeof("urn:example:actions") - 1u,
    .local_name = "record",
    .local_name_size = sizeof("record") - 1u,
    .callable = record_value,
    .parameter_names = params,
    .parameter_count = 1u
}};
scxml_cmeta_compile_options_v2 options =
    scxml_cmeta_default_compile_options_v2(&root_schema);
options.actions = actions;
options.action_count = sizeof(actions) / sizeof(actions[0]);
```

对应文档可在 `if`、`foreach`、`finalize` 等 executable-content 位置写
`<a:record value="count + 1"/>`。未注册元素、参数缺失/多余、嵌套子内容和
不支持的 callable signature 会在编译期拒绝；调用失败在运行时产生
`error.execution`。

内部 `<send target="#_internal">` 的 `namelist`/`param` 会形成结构化
`_event.data`。结构类型的 `<donedata><content expr="...">` 将选中对象本身作为
CMeta 根路径，例如 `expr="nested"` 由接收方读取为
`_event.data.invoke_id`（即直接读取 `nested` 的字段）。两条路径都复制数据到
session 拥有的有界存储，队列中
不保留临时表达式 view。

## 提取来源与回滚

初始源码从原 TurboUtils 仓库 HEAD `3b0c77a707ff8c5062f333c6f6208fee2510821f` 的 `cflow-scxml/` 提取；该目录最近一次内容变更来自提交 `f4bc1ea571ca0b7e5d989a5122c177e22cb474a3`。在 Salts 完成依赖切换并通过独立安装消费验证前，原目录保留为回滚副本。
