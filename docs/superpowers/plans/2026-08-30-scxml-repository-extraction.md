# TurboSCXML Repository Extraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract the existing `cflow-scxml` library into a standalone `TurboSCXML` repository without deleting or changing the source copy in TurboUtils.

**Architecture:** The new repository owns the SCXML-to-CFlow compiler, SCXML session runtime adapters, public `<cflow/scxml.h>` API, conformance corpus, and tests. It consumes an installed TurboUtils SDK through `TURBOUTILS_ROOT`; CFlow, CMeta, QueryVM, XmlParser, Core, STL, and TinyTest remain owned by TurboUtils. The initial extraction preserves C behavior and header paths while publishing the new CMake target `TurboSCXML::SCXML`.

**Tech Stack:** C11, CMake 3.20+, CMake Presets, Ninja/MSVC or GCC, CTest, TurboUtils TinyTest.

**Spec:** `docs/specs/cflow-scxml-core-design.md`

## Global Constraints

- Do not delete or modify `C:\projects\cpp\turbonet\turbo-utils\cflow-scxml` during this extraction.
- Copy source, public headers, tests, fixtures, and W3C corpus byte-for-byte before standalone build changes.
- Resolve TurboUtils only from `$ENV{TURBOUTILS_ROOT}` with `NO_DEFAULT_PATH`; missing or incomplete roots fail configuration.
- Preserve `<cflow/scxml.h>` and all existing `cflow_scxml_*` C symbols and ABI constants.
- Keep `TurboUtils::CFlow` independent of XML, CSerde, and SCXML.
- Use version-controlled `CMakeUserPresets.json` for configure, build, test, and install entry points.
- Do not add QuickJS, HTTP, persistence, or server behavior to this extraction.

---

### Task 1: Preserve the SCXML source and design evidence

**Files:**
- Create: `include/cflow/scxml.h`
- Create: `src/scxml.c`
- Create: `src/cmeta_expr.c`
- Create: `src/cmeta_expr.h`
- Create: `src/cmeta_assign.c`
- Create: `src/cmeta_assign.h`
- Create: `src/cmeta_location.c`
- Create: `src/cmeta_location.h`
- Create: `src/cmeta_sequence.c`
- Create: `src/cmeta_sequence.h`
- Create: `src/cmeta_foreach.c`
- Create: `src/cmeta_foreach.h`
- Create: `tests/` copied from the source module
- Create: `docs/specs/cflow-scxml-core-design.md`

**Interfaces:**
- Consumes: source module at `C:\projects\cpp\turbonet\turbo-utils\cflow-scxml`.
- Produces: an unchanged standalone source/test tree with the existing C ABI.

- [x] **Step 1: Record the source commit and dirty-state boundary**

Run:

```powershell
git -C C:\projects\cpp\turbonet\turbo-utils status --short --branch
git -C C:\projects\cpp\turbonet\turbo-utils log -1 --format=%H -- cflow-scxml
```

Expected: `cflow-scxml/` has no local modifications; unrelated TurboUtils changes remain untouched.

- [x] **Step 2: Copy the module and normative design spec**

Copy `include/`, `src/`, `tests/`, and the core design spec without editing source content.

- [x] **Step 3: Verify byte parity**

Run `git diff --no-index` independently for `include`, `src`, and `tests`.

Expected: no differences before standalone build files are introduced.

### Task 2: Add a fail-fast standalone CMake package

**Files:**
- Create: `CMakeLists.txt`
- Create: `cmake/TurboSCXMLConfig.cmake.in`
- Create: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: installed `TurboUtils::CFlow`, `CMeta`, `XmlParser`, `Core`, and `QueryVM` targets.
- Produces: build-tree alias and installed target `TurboSCXML::SCXML`.

- [x] **Step 1: Verify the missing standalone build fails**

Run `cmake --list-presets` in the copied source before adding preset/build files.

Expected: failure because no standalone CMake preset entry exists.

- [x] **Step 2: Add the library and exact dependency boundary**

The root build creates `turbo_scxml`, assigns `EXPORT_NAME SCXML`, and links exactly:

```cmake
target_link_libraries(turbo_scxml
  PUBLIC TurboUtils::CFlow TurboUtils::CMeta TurboUtils::XmlParser
  PRIVATE TurboUtils::Core TurboUtils::QueryVM)
add_library(TurboSCXML::SCXML ALIAS turbo_scxml)
```

It validates `TURBOUTILS_ROOT`, calls:

```cmake
find_package(TurboUtils CONFIG REQUIRED
  PATHS "$ENV{TURBOUTILS_ROOT}" NO_DEFAULT_PATH)
```

and installs headers, the library, `TurboSCXMLTargets.cmake`, config, and version files under `${CMAKE_INSTALL_LIBDIR}/cmake/TurboSCXML`.

- [x] **Step 3: Replace TurboUtils-only test helpers with native CMake**

Define one local `turboscxml_add_test(name source)` function that creates the executable, links `TurboSCXML::SCXML` plus declared extra targets, and registers it with CTest. Preserve fixture directory definitions and private `src/` include paths.

- [x] **Step 4: Configure and confirm dependency contract**

Expected: configuration fails if `TURBOUTILS_ROOT` is absent and succeeds only against the selected installed TurboUtils SDK.

### Task 3: Add first-party presets and repository metadata

**Files:**
- Create: `CMakePresets.json`
- Create: `CMakeUserPresets.json`
- Create: `presets/*.json`
- Create: `vcpkg.json`
- Create: `.gitignore`
- Create: `README.md`

**Interfaces:**
- Consumes: `PROJECT_ROOT`, `VCPKG_ROOT`, and installed TurboUtils SDK profiles.
- Produces: `win-dev-user`, `win-release-user`, `linux-dev-user`, and `linux-release-user` configure/build/test presets plus matching `install-*` build presets.

- [x] **Step 1: Copy portable shared preset definitions**

Copy the existing TurboUtils `presets/` definitions and retain compiler/platform behavior.

- [x] **Step 2: Add project-specific user profiles**

Each profile defines `TURBOUTILS_ROOT=$env{PKG_ROOT}/turboutils/<profile>` and installs this project to `$env{PKG_ROOT}/turboscxml/<profile>`. Runtime paths contain this build, matching vcpkg binaries, TurboUtils binaries, and `$penv{PATH}`.

- [x] **Step 3: Add bounded repository metadata**

Ignore only generated build trees, vcpkg installed trees, IDE state, and CodeGraph output. Document ownership, dependency direction, supported build entry points, installed target, public header, and rollback policy. Do not claim unverified platform support.

- [x] **Step 4: List all public presets**

Run:

```powershell
cmake --list-presets
cmake --build --list-presets
ctest --list-presets
```

Expected: configure/build/test/install entries are visible and no user preset is missing.

### Task 4: Verify build, behavior, installation, and source preservation

**Files:**
- Create: `tests/install_consumer/CMakeLists.txt`
- Create: `tests/install_consumer/main.c`

**Interfaces:**
- Consumes: installed `TurboSCXMLConfig.cmake` and `TurboUtilsConfig.cmake`.
- Produces: proof that a downstream project can include `<cflow/scxml.h>` and link `TurboSCXML::SCXML`.

- [x] **Step 1: Configure through the Windows user preset**

Run from `VsDevCmd.bat`:

```powershell
cmake --fresh --preset win-release-user
```

- [x] **Step 2: Build and run the full extracted test suite**

Run:

```powershell
cmake --build --preset win-release-user
ctest --preset win-release-user
```

Expected: every extracted TinyTest/CTest target passes, including W3C corpus coverage.

- [x] **Step 3: Install through the install preset**

Run:

```powershell
cmake --build --preset install-win-release-user
```

Expected: library, public header, package config, version config, and targets export exist under the TurboSCXML release prefix.

- [x] **Step 4: Build and run the install consumer**

The consumer calls `cflow_scxml_default_limits()` so its link verifies a real public symbol, not just header discovery.

- [x] **Step 5: Recheck source preservation and initialize history**

Run `git status` in TurboUtils to confirm no extraction changes occurred there, initialize the new repository on `main`, and commit only the verified standalone extraction.
