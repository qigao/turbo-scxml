# TurboSCXML

TurboSCXML 将 W3C SCXML 文档编译为 Rocida CFlow Statechart，并提供有界、版本化的宿主事件 I/O、调用与 CMeta 数据模型适配接口。

## 模块边界

- 本仓库拥有 SCXML 文档编译、SCXML session、适配器契约、测试 fixtures 与 W3C conformance corpus。
- Rocida 继续拥有 CFlow、CMeta、QueryVM、XmlParser、Core、STL 与 TinyTest。
- 依赖方向固定为 `TurboSCXML -> installed Rocida`；Rocida 不依赖 TurboSCXML。
- HTTP ingress/egress、认证、持久化和服务部署不属于解释器核心；可选
  `TurboSCXML::CHttpResource` 仅负责受宿主授权的同步资源读取。

公开 C API 继续通过 `<scxml/scxml.h>` 提供，函数与类型保持 `scxml_*` 命名。CMake 消费目标为 `TurboSCXML::SCXML`。

## 构建

构建要求：

- CMake 3.20 或更新版本
- Ninja
- Windows 使用 Visual Studio 2022 开发者命令环境
- `PROJECT_ROOT` 指向包含 `external/pkgs` 的工程根
- `VCPKG_ROOT` 指向 vcpkg checkout
- 对应 profile 的 Rocida 已安装到 `$PROJECT_ROOT/external/pkgs/rocida/<profile>`

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

启用可选 CHTTP 资源适配器时，匹配 profile 的 Rocida SDK 必须包含
`Rocida::CHTTP`。仓库提供独立的 `win-dev-chttp-user` 与
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

`TurboSCXMLConfig.cmake` 会从 `ROCIDA_ROOT` 精确解析 Rocida；缺少变量、目录或 package config 时直接失败，不回退到系统路径。

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

## 提取来源与回滚

初始源码从原 TurboUtils 仓库 HEAD `3b0c77a707ff8c5062f333c6f6208fee2510821f` 的 `cflow-scxml/` 提取；该目录最近一次内容变更来自提交 `f4bc1ea571ca0b7e5d989a5122c177e22cb474a3`。在 Rocida 完成依赖切换并通过独立安装消费验证前，原目录保留为回滚副本。
