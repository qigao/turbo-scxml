# SCXML System-variable Write Protection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute recognized system-variable assignment attempts as atomic runtime failures and promote W3C mandatory tests 322, 324, 326, 329, and 346.

**Architecture:** The internal location layer classifies the four standard system roots without resolving mutable storage. An internal assignment-program flag returns a deterministic evaluation failure through the existing block-abort and prioritized `error.execution` path; session-owned system facts remain untouched.

**Tech Stack:** C11, TurboUtils CMeta/CFlow, cxml, TinyTest, CMake Presets.

**Spec:** `docs/specs/scxml-system-variable-write-protection-design.md`

## Global Constraints

- Work directly on the explicitly selected `turbo-scxml/main`; do not create a worktree or dispatch subagents.
- Add no public member, function, ABI tag, status value, queue, dependency, or runtime allocation.
- Admit only syntactically valid `_sessionid`, `_name`, `_event[.<path>]`, and `_ioprocessors[.<path>]` targets; unknown underscore roots remain admission failures.
- Every runtime attempt raises one internal `error.execution`, aborts its current executable block, and leaves system and CMeta state unchanged.
- `tests/w3c/manifest.tsv` remains the corpus fact source.

---

### Task 1: Establish the assignment boundary with TDD

**Files:**
- Modify: `tests/scxml_expr_test.c`
- Modify: `tests/scxml_cmeta_test.c`
- Modify: `src/scxml_location.h`
- Modify: `src/scxml_location.c`
- Modify: `src/scxml_assign.c`

**Interfaces:**
- Produces: internal `scxml_location_is_read_only_system()` classification and `scxml_assign_program_impl.read_only_system_destination`.
- Preserves: `scxml_assign_compile()` and `scxml_assign_apply_with_system()` signatures and all public status values.

- [x] **Step 1: Write direct and owning-session failing tests**

  Change the direct `_event` assignment expectation from admission failure to
  successful compilation followed by `SCXML_EXPR_EVALUATION_ERROR`, with the
  input record byte-for-byte unchanged. Add an owning-session fixture that
  attempts `_sessionid`, `_event`, `_ioprocessors`, and `_name` in separate
  states, requires four ordered `error.execution` transitions, and places a
  failing sentinel after each assignment to prove block abortion.

- [x] **Step 2: Verify RED**

  Build `scxml_expr_test` and `scxml_cmeta_test` with `win-release-user`. Run
  the two named TinyTest filters. The direct case must fail because `_event`
  is rejected at compilation; the session case must fail at its compile
  assertion for the same missing runtime behavior.

- [x] **Step 3: Classify recognized system locations**

  Add one internal Boolean classifier in `scxml_location.c`. Reuse the existing
  dotted-path validator, accept the exact scalar roots and object-root dotted
  paths specified above, and reject malformed paths and unknown underscore
  roots. Keep ordinary writable location resolution unchanged.

- [x] **Step 4: Compile read-only assignment programs**

  In `scxml_assign_compile()`, set the internal read-only flag for a recognized
  system target, compile the source expression with existing limits, and skip
  only mutable-destination resolution and type matching. On every failure,
  destroy the partially compiled expression and leave `out->impl == NULL`.

- [x] **Step 5: Fail before evaluation or mutation**

  In the shared apply function, after validating call arguments and before
  evaluating the value expression, return `SCXML_EXPR_EVALUATION_ERROR` with
  the diagnostic `CMeta system locations are read-only`. The existing runtime
  assignment branch must remain the sole conversion point to
  `error.execution` and block abortion.

- [x] **Step 6: Verify GREEN and adjacent regressions**

  Run both focused filters, complete `scxml_expr_test`, complete
  `scxml_cmeta_test`, and the existing invalid/read-only admission case after
  narrowing its compile-rejection table to malformed or unknown targets.

### Task 2: Preserve all five W3C assertions

**Files:**
- Create: `tests/w3c/test322.scxml`
- Create: `tests/w3c/test324.scxml`
- Create: `tests/w3c/test326.scxml`
- Create: `tests/w3c/test329.scxml`
- Create: `tests/w3c/test346.scxml`
- Modify: `tests/scxml_w3c_conformance_test.c`

**Interfaces:**
- Consumes: owning CMeta session system values and existing strict result adapter.
- Produces: independent terminal witnesses for binding lifetime, failed writes, and one error Event per attempt.

- [x] **Step 1: Add strict transformed fixtures**

  Test 322 snapshots `_sessionid`, attempts replacement, waits for
  `error.execution`, and compares the original value. Test 324 checks the
  literal root name before and after its failed write. Test 326 checks the
  SCXML processor location before and after its failed write. Test 329 performs
  all four attempts and checks the still-bound values/current Event at each
  state. Test 346 advances only on four separate `error.execution` Events and
  places a distinct raised sentinel after every failed assignment.

- [x] **Step 2: Register five focused TinyTest cases**

  Use `run_w3c_cmeta_fixture()` for each fixture. Every success path sends
  exactly one committed `result.pass`; every fallback sends `result.fail`.

- [x] **Step 3: Verify focused and complete corpus behavior**

  Build the W3C target, run filters 322/324/326/329/346, then run the complete
  executable. Confirm each new case terminates strictly and the manifest test
  remains the only expected failure until Task 3 updates its totals.

### Task 3: Synchronize facts and deliver

**Files:**
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `docs/specs/scxml-system-variable-binding-design.md`
- Modify: `docs/superpowers/plans/2026-08-31-scxml-system-variable-write-protection.md`

**Interfaces:**
- Produces: corpus totals `63 PASS / 105 UNSUPPORTED / 34 N/A` and zero remaining SystemVariables rows.

- [x] **Step 1: Promote and document all five rows**

  Record the precise transformations and witnesses, add the five provenance
  table entries, update totals from 58/110 to 63/105, and link the startup
  design to this write-protection decision.

- [x] **Step 2: Run fresh full verification**

  Run focused tests, `cmake --fresh --preset win-release-user`, the complete
  Release build, and `ctest --preset win-release-user --output-on-failure`
  from the VS developer environment.

- [x] **Step 3: Review and commit locally**

  Run `git diff --check`, inspect the complete diff and status, confirm no
  `.codegraph/` artifact is staged, mark this plan complete, and create one
  local commit. Do not push without separate authorization.

- [x] **Step 4: Update TurboSCXML tracking**

  Update `qigao/turbo-scxml#2` to `63/105`, mark SystemVariables complete,
  and report the local commit plus focused and full verification evidence,
  explicitly noting that code remains unpushed.
