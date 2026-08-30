# SCXML Implementation Split Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan inline. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Split the roughly 10K-line `src/scxml.c` into focused `.h`/`.c` module pairs without changing the public API, runtime state ownership, or error semantics.

**Architecture:** Compile five responsibility-oriented C translation units. `src/scxml_impl.h` owns the shared private data model; each module header declares only the cross-module functions owned by its matching `.c` file. Module-local helpers remain `static`, while cross-module names use an `scxml_<module>_*` prefix. These headers stay private and are not installed.

**Tech Stack:** C11, CMake/Ninja, TurboUtils CFlow/CMeta/QueryVM/XmlParser, TinyTest.

**Spec:** `docs/specs/scxml-core-design.md`

## Global Constraints

- Keep `<scxml/scxml.h>`, `scxml_*`, and `SCXML_*` as the public SCXML contract.
- Keep real TurboUtils CFlow and CMeta types named `cflow_*` and `cmeta_*`.
- Do not change state ownership, allocation, locking, callback semantics, or error mapping during extraction.
- Compile `scxml_analyze.c`, `scxml_runtime.c`, `scxml_emit.c`, `scxml_program.c`, and `scxml_session.c` independently.
- Keep shared structures in `scxml_impl.h`, cross-module declarations in the owning private module header, and all other helpers `static`.
- Do not commit until the user explicitly requests a commit.

---

### Task 1: Capture the verified baseline

**Files:**
- Inspect: `src/scxml_impl.h`
- Inspect: `include/scxml/scxml.h`
- Test: `tests/scxml_test.c`
- Test: `tests/scxml_cmeta_test.c`
- Test: `tests/scxml_event_io_contract_test.c`

**Interfaces:**
- Consumes: current `scxml_*` public API and the eight registered CTest executables.
- Produces: a clean Release build and 8/8 passing baseline against which the split is compared.

- [x] **Step 1: Configure the verified Release profile**

  Run from the VS 2022 developer environment:

  ```powershell
  cmake --fresh --preset win-release-user
  ```

  Expected: configure and generation succeed in `build/Msvc-Release`.

- [x] **Step 2: Build and run the baseline**

  ```powershell
  cmake --build --preset win-release-user
  ctest --preset win-release-user
  ```

  Expected: the library and all eight tests build; 8/8 tests pass.

### Task 2: Extract the private model and implementation modules

**Files:**
- Create: `src/scxml_impl.h`
- Create: `src/scxml_analyze.h`, `src/scxml_analyze.c`
- Create: `src/scxml_runtime.h`, `src/scxml_runtime.c`
- Create: `src/scxml_emit.h`, `src/scxml_emit.c`
- Create: `src/scxml_program.h`, `src/scxml_program.c`
- Create: `src/scxml_session.h`, `src/scxml_session.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: the declarations and definitions originally present in `src/scxml.c`.
- Produces: five independently compiled modules with explicit private interfaces and unchanged public behavior.

- [x] **Step 1: Move includes, constants, private types, structs, and forward declarations into `scxml_impl.h`**

  Add guard `SCXML_IMPL_H`; keep shared constants, enums, structures, and typedefs here. Do not place function declarations in this shared model header when one module clearly owns them.

- [x] **Step 2: Move XML validation and analysis into `scxml_analyze.h/.c`**

  Preserve the ordered definitions from `checked_add()` through `build_event_names()`. This partition owns checked arithmetic, XML lexical validation, tree analysis, count admission, invocation declaration analysis, and event-name construction.

- [x] **Step 3: Move runtime execution into `scxml_runtime.h/.c`**

  Preserve the ordered definitions from `scxml_execute_outcome` through `execute_scxml_session_block()`. This partition owns transactional effect preparation, invoke lifecycle, event metadata, send/cancel execution, executable ranges, and runtime block dispatch.

- [x] **Step 4: Move compiler emission into `scxml_emit.h/.c`**

  Preserve the ordered definitions from `resolve_condition_state()` through `copy_program_names()`. This partition owns expression admission, state/transition guard compilation, executable emission, transition emission, and build cleanup.

- [x] **Step 5: Move public program compilation and inspection into `scxml_program.h/.c`**

  Preserve the ordered definitions from `scxml_default_limits()` through `scxml_program_instance_bindings()`. This partition owns defaults, model compilation, CMeta compilation options, program destruction, and immutable program queries.

- [x] **Step 6: Move session and adapter lifecycle into `scxml_session.h/.c`**

  Preserve the remaining ordered definitions from `event_io_adapter_valid()` through `scxml_program_guard_bindings()`. This partition owns adapter validation, session storage, initialization, mailbox APIs, stats, location copying, cancellation, and destruction.

- [x] **Step 7: Register the independent sources in CMake**

  The library source list must contain:

  ```cmake
  src/scxml_analyze.c
  src/scxml_runtime.c
  src/scxml_emit.c
  src/scxml_program.c
  src/scxml_session.c
  ```

  Remove the obsolete `src/scxml.c` composition entry point and all `.inc` files.

### Task 3: Verify structural and behavioral equivalence

**Files:**
- Verify: `src/scxml_impl.h`
- Verify: `src/scxml_{analyze,runtime,emit,program,session}.h/.c`
- Test: `tests/*.c`

**Interfaces:**
- Consumes: the independent translation units from Task 2.
- Produces: proof that declarations and definitions agree, no implementation was dropped, and the installed public contract is unchanged.

- [x] **Step 1: Run residual and formatting checks**

  ```powershell
  rg.exe -n "cflow_scxml|CFLOW_SCXML|cflow/scxml\.h" CMakeLists.txt README.md src include tests docs/specs
  git diff --check
  ```

  Expected: no legacy SCXML namespace matches and no whitespace errors.

- [x] **Step 2: Reconfigure from source dependencies and build**

  ```powershell
  cmake --fresh --preset win-release-user
  cmake --build --preset win-release-user
  ```

  Expected: the compiler compiles each module independently and all test targets link.

- [x] **Step 3: Run the full behavior suite**

  ```powershell
  ctest --preset win-release-user
  ```

  Expected: 8/8 tests pass.

- [x] **Step 4: Confirm partition sizes and symbol privacy**

  ```powershell
  fd.exe -a "scxml_(impl|analyze|runtime|emit|program|session)" src
  fd.exe -a -e inc .
  rg.exe -n "^(static )?.*scxml_.*\(" src/scxml_*.c
  ```

  Expected: no `.inc` file remains; every module has one documented responsibility, module-only definitions remain `static`, and cross-module definitions have an owning private header.

### Task 4: Verify the installed consumer contract

**Files:**
- Test: `tests/install_consumer/main.c`
- Verify: installed `include/scxml/scxml.h`

**Interfaces:**
- Consumes: installed `TurboSCXML::SCXML` and `<scxml/scxml.h>`.
- Produces: a downstream executable that calls `scxml_default_limits()` and exits successfully.

- [x] **Step 1: Install the Release package**

  ```powershell
  cmake --build --preset install-win-release-user
  ```

  Expected: the package installs `include/scxml/scxml.h`; no source install rule references `include/cflow`.

- [x] **Step 2: Reconfigure, build, and run the install consumer**

  Configure `tests/install_consumer` with `TURBOSCXML_ROOT` and `TURBOUTILS_ROOT` set to the matching Release roots, build it with Ninja, then run `turboscxml_install_consumer.exe`.

  Expected: configure, compile, and link succeed; the executable exits with code 0 because every default limit checked by `main.c` is nonzero.

- [x] **Step 3: Review the complete diff without committing**

  ```powershell
  git diff --check
  git status --short
  git diff HEAD --stat
  ```

  Expected: only the requested namespace cleanup, implementation partitioning, tests, CMake install path, README, design/plan updates are present.
