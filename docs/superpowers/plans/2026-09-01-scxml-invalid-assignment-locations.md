# SCXML Invalid Assignment Locations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote W3C SCXML tests 286 and 311 by executing syntactically valid invalid CMeta assignment locations as internal `error.execution` failures.

**Architecture:** Add an explicit strict/runtime location policy to the internal assignment compiler. Only executable XML `<assign>` uses runtime admission; data initializers and completion-data materialization remain strict. A deferred invalid destination is represented in the compiled program and rejected before value evaluation or state access, allowing the existing transactional executable-content boundary to abort the block, roll back staged state, and enqueue one internal processor error.

**Tech Stack:** C11, TurboSCXML, TurboUtils CFlow/CMeta, TinyTest, CMake/CTest presets.

**Spec:** [SCXML 1.0 location expressions and assign](https://www.w3.org/TR/scxml/#DataModel); W3C IRP [test 286](https://www.w3.org/Voice/2013/scxml-irp/286/test286.txml) and [test 311](https://www.w3.org/Voice/2013/scxml-irp/311/test311.txml); architecture decision is `docs/specs/scxml-invalid-assignment-location-design.md`; tracking issue is `qigao/turbo-scxml#2`.

## Global Constraints

- Preserve the caller-owned typed CMeta object as the sole data-model fact source.
- Defer only syntactically valid unresolved executable `<assign>` destinations; missing attributes, malformed paths, invalid roots, limits, adapter failures, and malformed expressions remain admission failures.
- Keep data initializers and `<donedata><param>` assignment programs strict.
- Reject a deferred destination before evaluating its right-hand expression or accessing staged state.
- Reuse the existing executable-block error boundary; do not add a queue, fallback, dynamic map, runtime path copy, or alternate state store.
- Preserve `tests/w3c/manifest.tsv` as the 202-row corpus fact source and synchronize README provenance and aggregate counts.
- Keep test 307 unsupported because static CMeta does not expose the required loaded-instance missing-substructure semantic.
- Work in `C:\projects\cpp\turbonet\scxml-expression-completion-worktree` on `feat/w3c-expression-completion`; do not modify the dirty main worktree.

---

### Task 1: Specify the direct assignment contract and verify RED

**Files:**
- Modify: `tests/scxml_expr_test.c`
- Modify later: `src/scxml_assign.h`
- Modify later: `src/scxml_assign.c`

**Interfaces:**
- Consumes: `scxml_assign_compile()`, `scxml_assign_apply()`, static CMeta schema lookup.
- Produces: strict rejection and executable-runtime admission as separately testable policies.

- [x] **Step 1: Add the direct policy test**

  Require strict compilation of `missing` to return `SCXML_EXPR_UNKNOWN_LOCATION` with no program. Require runtime-policy compilation of the same lexical location and legal expression to succeed, then require apply to return `SCXML_EXPR_UNKNOWN_LOCATION` and leave the entire destination object unchanged.

- [x] **Step 2: Run the focused RED gate**

  Build `scxml_expr_test`. The test must fail to compile because the location-policy interface does not exist; an unrelated configure or dependency failure is not the expected RED.

### Task 2: Register W3C runtime witnesses and verify RED

**Files:**
- Modify: `tests/scxml_w3c_conformance_test.c`
- Create: `tests/w3c/test286.scxml`
- Create: `tests/w3c/test311.scxml`
- Modify later: `tests/w3c/manifest.tsv`
- Modify later: `tests/w3c/README.md`

**Interfaces:**
- Consumes: the real CMeta session runtime, internal Event priority, executable-block abortion, and the terminal result adapter.
- Produces: one focused TinyTest registration per upstream assertion.

- [x] **Step 1: Add test 286**

  Use unknown top-level location `missing`, retain a following `raise event="foo"`, route `error.execution` to a verification state, and use a later internal confirmation Event so an incorrectly continued `foo` reaches `fail` before `pass`.

- [x] **Step 2: Add test 311**

  Use lexical location `sequence.missing` so the path traverses a scalar and cannot yield a valid location. Route `error.execution` to `pass` and any other selected Event to `fail`.

- [x] **Step 3: Register and run the focused RED gate**

  Register both fixtures with `run_w3c_cmeta_fixture()`, build the W3C target, and run filters 286 and 311. Both must fail during document compilation with the current implementation; a missing fixture or unrelated runtime failure is not the expected RED.

### Task 3: Implement the narrow runtime policy and verify GREEN

**Files:**
- Modify: `src/scxml_assign.h`
- Modify: `src/scxml_assign.c`
- Modify: `src/scxml_location.c`
- Modify: `src/scxml_emit.c`
- Modify: `tests/scxml_expr_test.c`
- Modify: `tests/scxml_cmeta_test.c`

**Interfaces:**
- Consumes: `scxml_location_compile()`, value-expression compilation, and the existing assignment execution boundary.
- Produces: one compiled invalid-destination classification with deterministic runtime failure.

- [x] **Step 1: Add the internal policy**

  Define strict and runtime enum values, pass the policy explicitly at every assignment compiler call, and validate the enum as an input invariant.

- [x] **Step 2: Admit only unresolved runtime locations**

  Preserve all non-`SCXML_EXPR_UNKNOWN_LOCATION` failures. Distinguish missing or non-addressable document locations from corrupt child descriptors and impossible storage bounds. Under runtime policy, retain an invalid-destination marker only for the former, compile the right-hand expression, and skip destination adapter/type checks that require a resolved type.

- [x] **Step 3: Reject before evaluation**

  In assignment apply, return `SCXML_EXPR_UNKNOWN_LOCATION` before evaluating the expression or deriving a destination address. Keep protected system destinations on their existing read-only error path.

- [x] **Step 4: Keep non-executable destinations strict**

  Select runtime policy only in `emit_assign_step()`. Pass strict policy from data initializer, done-data param, and direct tests whose contract is ordinary static assignment.

- [x] **Step 5: Update compilation regression expectations**

  Require unknown lexical executable destinations to compile, while missing attributes, malformed paths, unknown `_` roots, scalar system subpaths, strict internal destinations, and null-datamodel assignments retain their current failures.

- [x] **Step 6: Run focused GREEN**

  Run the direct expression test, CMeta compilation test, and W3C filters 286/311. All must pass with exact state and Event-order witnesses.

### Task 4: Promote corpus facts and run full verification

**Files:**
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `tests/scxml_w3c_conformance_test.c`

**Interfaces:**
- Consumes: strict manifest validation and documented W3C provenance.
- Produces: aggregate inventory of 152 PASS, 16 UNSUPPORTED, and 34 N/A.

- [x] **Step 1: Promote manifest rows 286 and 311**

  Mark both `PASS/TERMINAL_PASS`, record the exact local transformations, and state how each terminal witness preserves the upstream invalid-location assertion.

- [x] **Step 2: Synchronize README and aggregate assertions**

  Add both provenance rows, replace the obsolete unknown-location limitation, update counts to 152/16/34, and explicitly retain test 307 as the sole unsupported Expressions document.

- [x] **Step 3: Run focused and adjacent regression**

  Build and run expression, CMeta, W3C, runtime, and Event I/O tests in Debug. Run the complete Debug suite.

- [x] **Step 4: Run fresh Release verification**

  Configure with `cmake --fresh --preset win-release-user`, build, and run the complete Release CTest suite with output on failure.

- [x] **Step 5: Inspect impact and repository hygiene**

  Run `codegraph sync .`, affected analysis for changed production files, `git diff --check`, and a read-only local review. Confirm `.codegraph/` remains untracked and no dirty-main file entered the branch.

- [x] **Step 6: Prepare branch handoff**

  Review the final diff and report exact verification evidence. Use the finishing-development-branch workflow before any merge action.
