# CMeta Data Initializer Errors Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Recover from runtime-illegal CMeta `<data>` initializers with ordered internal `error.execution` Events and promote W3C test 277.

**Architecture:** Compile all early data assignments as before, but execute their exact document-wide span through one private root entry block owned by the CFlow transaction. Early and late blocks share per-assignment recovery: preserve the typed pre-initialization slot, stage one internal error, continue siblings, and commit only through the existing StateChart transaction.

**Tech Stack:** C11, Turbo::CMeta, Turbo::CFlow StateChart, TinyTest, CMake Presets.

**Spec:** `docs/specs/scxml-data-initializer-error-design.md`

## Global Constraints

- Preserve the public V1/V2 API and existing successful initializer behavior.
- Keep CFlow staged state as the only mutable runtime fact source.
- Use test-first RED/GREEN cycles and real StateChart execution.
- Keep storage bounded by analyzed rows and the configured internal queue.
- Fail fast if an internal error Event cannot be staged; never drop it.
- Use `win-dev-user` for focused development and both Windows presets for final verification.

---

### Task 1: Execute early initializers at the root transaction boundary

**Files:**
- Modify: `tests/scxml_cmeta_test.c`
- Modify: `src/scxml_impl.h`
- Modify: `src/scxml_program.c`
- Modify: `src/scxml_emit.c`
- Modify: `src/scxml_runtime.c`
- Modify: `src/scxml_session.c`

**Interfaces:**
- Consumes: compiled `scxml_assign_program` rows and `scxml_session_data_initializer_is_overridden()`.
- Produces: private `SCXML_STEP_EARLY_INITIALIZE` with `assignment` and `assignment_count` span fields.

- [x] **Step 1: Add the failing early-binding behavior test**

  Add a TinyTest document whose `data/@expr` is the legal CMeta expression
  `_event.data.sequence` while `_event` is unbound. Its initial state's
  `onentry` raises `sentinel`; `error.execution` must be selected first, a
  later `<assign location='sequence' expr='1'/>` must succeed, and only
  `sequence == 1` may reach the final state.

- [x] **Step 2: Run the focused test and verify RED**

  Run:

  ```powershell
  cmake --build --preset win-dev-user --target scxml_cmeta_test
  build\Msvc\tests\scxml_cmeta_test.exe --filter "recovers from an illegal early data initializer"
  ```

  Expected: the test fails because session initialization returns
  `CFLOW_STATECHART_INSTANCE_INVALID_CONFIGURATION`.

- [x] **Step 3: Add bounded early block accounting and emission**

  Add `SCXML_STEP_EARLY_INITIALIZE`. After analysis, reserve exactly one
  executable, block, step, and state-action row when early CMeta initializer
  rows exist. Emit a root entry action with order zero over the existing
  initializer assignment span:

  ```c
  build->steps[step_index] = (scxml_step){
      .kind = SCXML_STEP_EARLY_INITIALIZE,
      .next = step_index + 1u,
      .assignment = 0u,
      .assignment_count = build->data_initializer_count};
  ```

- [x] **Step 4: Execute recoverable initializers in the staged state**

  Move early assignment application out of `initialize_cmeta_state()`. In the
  new runtime step, skip environment overrides, apply each assignment with an
  initialization membership callback that always returns false, and call
  `context->raise_internal()` once for each evaluation failure without
  aborting successful siblings.

- [x] **Step 5: Run the focused and adjacent CMeta tests to GREEN**

  Run the filtered behavior, early/late initializer, environment override,
  and managed lifecycle groups from `scxml_cmeta_test`.

- [x] **Step 6: Commit the root initialization transaction**

  ```text
  feat(scxml): recover data initializer errors
  ```

### Task 2: Align late binding and promote W3C test 277

**Files:**
- Modify: `tests/scxml_cmeta_test.c`
- Modify: `src/scxml_runtime.c`
- Create: `tests/w3c/test277.scxml`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: repository count summaries located with `rg.exe`

**Interfaces:**
- Consumes: the internal initializer-error staging helper from Task 1.
- Produces: recoverable first-entry late initialization and executable W3C test 277 coverage.

- [x] **Step 1: Add and verify the failing late-binding test**

  Use `binding='late'` with the runtime-illegal initializer inside the target
  state. Require `error.execution` before that state's `onentry` sentinel,
  then exit and re-enter the state to prove the initializer is not retried.
  Run only this filter and observe the current fatal instance error.

- [x] **Step 2: Reuse per-assignment recovery in the late step**

  Replace the fatal evaluation branch with the same internal-error staging
  helper. Continue sibling assignments; let the existing late-initializer
  ticket commit the DONE marker only if all required Events were staged.

- [x] **Step 3: Add W3C test 277 in RED state**

  Transform the upstream fixture to CMeta with `sequence` as the declared
  location and `_event.data.sequence` as the runtime-illegal initializer.
  Preserve the initial `sentinel`, error-first transition, later assignment to
  `1`, and final equality guard. Register the fixture before changing the
  manifest and verify the strict corpus rejects the missing/unsupported row.

- [x] **Step 4: Promote the manifest and provenance to GREEN**

  Mark test 277 `PASS/TERMINAL_PASS`, document the exact transformation, and
  update mandatory PASS/UNSUPPORTED counts by `+1/-1`. Run the focused W3C
  filter and the complete conformance executable.

- [x] **Step 5: Commit late recovery and corpus promotion**

  ```text
  test(scxml): promote data initializer error corpus
  ```

### Task 2A: Close independent review findings

- [x] Preserve early initialization through public program-level CFlow
  bindings where no owning `scxml_session` exists.
- [x] Preserve action-time `In()` membership for late initializers while early
  initialization continues to observe the empty configuration.
- [x] Snapshot each assignment input so a failing string or enum adapter
  restores its prior value without rolling back successful siblings.
- [x] Add RED/GREEN raw-binding, late-membership, and early/late failing-adapter
  regression tests.

### Task 3: Verify design invariants and release configurations

**Files:**
- Modify: `docs/specs/scxml-data-initializer-error-design.md` only if implementation evidence changes the contract.
- Modify: `docs/superpowers/plans/2026-09-02-scxml-data-initializer-errors.md` to mark completed steps.

**Interfaces:**
- Consumes: Tasks 1 and 2 commits.
- Produces: reproducible Debug/Release evidence and a clean PR-ready branch.

- [x] **Step 1: Run CodeGraph impact analysis and inspect every affected caller**

  Run `codegraph affected -p .` for modified production files, then read the
  reported callers and adjacent tests. Confirm root action order, assignment
  range bounds, override matching, and late ticket settlement.

- [x] **Step 2: Run focused and full Debug verification**

  Run fresh configure, full build, and `ctest --preset win-dev-user
  --output-on-failure` from `VsDevCmd.bat`.

- [x] **Step 3: Run full Release verification**

  Run fresh configure, full build, and `ctest --preset win-release-user
  --output-on-failure` from `VsDevCmd.bat`.

- [x] **Step 4: Review artifacts and commit final documentation state**

  Run `git diff --check`, verify `.codegraph/` and build trees are untracked or
  ignored, inspect `git status --short`, and commit only any final plan/design
  synchronization needed after verification.
