# TurboSCXML

TurboSCXML 将 W3C SCXML 文档编译为 TurboUtils CFlow Statechart，并提供有界、版本化的宿主事件 I/O、调用与 CMeta 数据模型适配接口。

## 模块边界

- 本仓库拥有 SCXML 文档编译、SCXML session、适配器契约、测试 fixtures 与 W3C conformance corpus。
- TurboUtils 继续拥有 CFlow、CMeta、QueryVM、XmlParser、Core、STL 与 TinyTest。
- 依赖方向固定为 `TurboSCXML -> installed TurboUtils`；TurboUtils 不依赖 TurboSCXML。
- HTTP、QuickJS、持久化和服务部署不属于本次仓库提取范围。

公开 C API 继续通过 `<scxml/scxml.h>` 提供，函数与类型保持 `scxml_*` 命名。CMake 消费目标为 `TurboSCXML::SCXML`。

## 构建

构建要求：

- CMake 3.20 或更新版本
- Ninja
- Windows 使用 Visual Studio 2022 开发者命令环境
- `PROJECT_ROOT` 指向包含 `external/pkgs` 的工程根
- `VCPKG_ROOT` 指向 vcpkg checkout
- 对应 profile 的 TurboUtils 已安装到 `$PROJECT_ROOT/external/pkgs/turboutils/<profile>`

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

`TurboSCXMLConfig.cmake` 会从 `TURBOUTILS_ROOT` 精确解析 TurboUtils；缺少变量、目录或 package config 时直接失败，不回退到系统路径。

## 提取来源与回滚

初始源码从 TurboUtils HEAD `3b0c77a707ff8c5062f333c6f6208fee2510821f` 的 `cflow-scxml/` 提取；该目录最近一次内容变更来自提交 `f4bc1ea571ca0b7e5d989a5122c177e22cb474a3`。在 TurboUtils 完成依赖切换并通过独立安装消费验证前，原目录保留为回滚副本。
