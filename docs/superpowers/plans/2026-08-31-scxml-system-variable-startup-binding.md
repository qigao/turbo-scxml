# SCXML System-Variable Startup Binding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded CMeta binding queries for `_sessionid`, `_name`, and `_ioprocessors`, then promote W3C-derived tests 321, 323, and 325 to executable mandatory coverage.

**Architecture:** Keep `scxml_session_impl.system_values` as the sole protected-variable fact source. Extend the finite `isBound(...)` grammar with three parser-selected Boolean operands and use the existing strict CMeta W3C harness to observe initialization behavior.

**Tech Stack:** C11, TurboUtils CMeta/CFlow/QueryVM, cxml, TinyTest, CMake Presets.

**Spec:** `docs/specs/scxml-system-variable-binding-design.md`

## Global Constraints

- Work directly on the explicitly selected `turbo-scxml/main`; TurboUtils remains an installed dependency.
- Preserve public C ABI, ownership, queue ordering, assignment admission behavior, and fail-fast diagnostics.
- A non-`NULL`, zero-length view is bound; a `NULL` view is unbound.
- `tests/w3c/manifest.tsv` remains the corpus fact source.
- Use the checked-in Windows Release presets for focused and full verification.

---

### Task 1: Extend the finite system-variable binding query

**Files:**
- Modify: `tests/scxml_expr_test.c`
- Modify: `src/scxml_expr.c`

**Interfaces:**
- Consumes: `scxml_expr_system_values.name`, `.session_id`, and `.scxml_location`.
- Produces: `isBound(_name)`, `isBound(_sessionid)`, and `isBound(_ioprocessors)`, each returning a Boolean.

- [x] **Step 1: Write the failing expression test**

  Add one TinyTest case that compiles all three exact expressions. For each,
  evaluate a zero-initialized system-values object and expect `false`, then
  evaluate a context whose corresponding witness is non-`NULL` and expect
  `true`. Use `{ "", 0u }` for `_name` to prove an empty value remains bound.
  Evaluate one compiled program with `system_values == NULL` and require
  `SCXML_EXPR_EVALUATION_ERROR` without changing the output Boolean.

- [x] **Step 2: Run the focused test and verify RED**

  Run:

  ```powershell
  cmake --build --preset win-release-user --target scxml_expr_test
  build\Msvc-Release\tests\scxml_expr_test.exe --filter "reports startup system-variable bindings"
  ```

  Expected: compilation fails because `isBound` currently accepts only
  `_event`.

- [x] **Step 3: Implement the minimal parser and evaluator operands**

  Add three `expr_operand_kind` values. In the `isBound` parser branch, map
  `_event`, `_name`, `_sessionid`, and `_ioprocessors` to their exact operand
  kinds before requiring `)`. In `resolve_operand`, select the matching view,
  return a Boolean based on `view->data != NULL`, and retain the existing
  evaluation error for a `NULL` system context.

- [x] **Step 4: Verify GREEN and malformed syntax**

  Keep `isBound()`, `isBound(_event.name)`, and `isBound(other)` as syntax
  errors, add `isBound(_ioprocessors.scxml)` as another syntax error, and run
  the complete `scxml_expr_test` executable.

### Task 2: Promote three mandatory startup-binding assertions

**Files:**
- Create: `tests/w3c/test321.scxml`
- Create: `tests/w3c/test323.scxml`
- Create: `tests/w3c/test325.scxml`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`

**Interfaces:**
- Consumes: `run_w3c_cmeta_fixture()` and the existing strict `result.*` Event I/O probe.
- Produces: three executable PASS rows with synchronized source provenance.

- [x] **Step 1: Add transformed fixtures and harness cases**

  Use `datamodel="cmeta"`. Each initial state has a guarded transition using
  its exact `isBound(...)` query followed by an unconditional failure
  transition; test 323 also requires `_name == "machineName"`. `pass` and
  `fail` finals send `result.pass` and `result.fail`. Add one named TinyTest
  case per fixture using `run_w3c_cmeta_fixture()`.

- [x] **Step 2: Run the focused corpus cases**

  Run:

  ```powershell
  cmake --build --preset win-release-user --target scxml_w3c_conformance_test
  ctest --preset win-release-user -R scxml_w3c_conformance_test --output-on-failure
  ```

  Expected: the complete fast corpus passes, including all three cases reaching
  exactly one strict `result.pass` observation. The CTest preset supplies the
  first-party runtime DLL path required by the executable.

- [x] **Step 3: Synchronize corpus facts and provenance**

  Change rows 321, 323, and 325 from
  `UNSUPPORTED/NOT_RUN/NONE` to `PASS/TERMINAL_PASS` with concrete
  transformations and preserved-assertion rationales. Add their upstream
  links and assertions to the README and update totals from 46/122 to 49/119.

- [x] **Step 4: Validate the complete corpus executable**

  Run `scxml_w3c_conformance_test.exe` without a filter. Expected inventory:
  202 documents, 168 mandatory, 34 optional, 49 PASS, 119 UNSUPPORTED, and
  34 N/A.

### Task 3: Verify and commit the batch

**Files:**
- Modify: `docs/superpowers/plans/2026-08-31-scxml-system-variable-startup-binding.md`

**Interfaces:**
- Consumes: Tasks 1 and 2.
- Produces: one local `turbo-scxml/main` conformance milestone and an updated TurboSCXML issue #2 record.

- [x] **Step 1: Reconfigure and run the full Release suite**

  Run inside the Visual Studio toolchain environment:

  ```powershell
  cmake --fresh --preset win-release-user
  cmake --build --preset win-release-user
  ctest --preset win-release-user --output-on-failure
  ```

  Expected: all registered tests pass.

- [x] **Step 2: Check repository integrity**

  Run `git diff --check`, inspect `git diff --stat` and the complete diff, and
  confirm `.codegraph/` is not staged.

- [x] **Step 3: Mark the plan complete and commit**

  Mark every checkbox `[x]`, stage only this batch, and commit with:

  ```text
  feat(scxml): cover startup system-variable bindings
  ```

**Post-commit tracking action:** Update the remote issue without pushing code.

Comment on `qigao/turbo-scxml#2` with commit ID, 49/119 corpus totals, and
focused/full verification evidence. This external action does not change the
versioned implementation plan. Do not push the six earlier commits or this new
commit unless the user separately authorizes a push.
