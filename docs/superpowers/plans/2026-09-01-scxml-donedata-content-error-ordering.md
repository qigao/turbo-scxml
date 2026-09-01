# SCXML Donedata Content Error Ordering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Evaluate `<donedata><content expr>` during final-state entry so `error.execution` is processed before the associated completion Event and the later completion data is empty.

**Architecture:** TurboSCXML lowers done-data into a final-entry synthetic action and stores its bounded result in a session-owned completion-data row. CFlow remains the sole StateChart and Event-order owner. Completion observation consumes the oldest matching ready row instead of evaluating an expression after completion selection has begun.

**Tech Stack:** C11, TurboUtils CFlow/CMeta, TurboParser XML, TinyTest, CMake Presets.

**Spec:** `docs/specs/scxml-donedata-content-error-ordering-design.md`, W3C SCXML IRP test 528, and `tests/w3c/manifest.tsv`.

## Global Constraints

- Do not change public TurboSCXML or CFlow APIs, adapter ABI, configuration format, or package dependencies.
- Keep CFlow as the only active-configuration, queue-order, and transition-selection fact source.
- Bound retained results by checked `completion_capacity + 1`, `SCXML_EVENT_METADATA_CAPACITY`, and `SCXML_EVENT_DATA_CAPACITY`; the extra row belongs to the current Event borrow.
- Evaluate done-data after explicit final-state onentry actions and before CFlow stages the parent completion.
- Publish failed expression results as empty completion data and enqueue exactly one `error.execution`; never expose partial CMeta objects.
- Destroy every managed CMeta object exactly once on consume, fatal cleanup, or session destruction.
- Do not add fallback evaluation in the completion observer.

---

### Task 1: Register a strict failing test528 witness

**Files:**
- Create: `tests/w3c/test528.scxml`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `tests/scxml_cmeta_test.c`

**Interfaces:**
- Consumes: existing W3C CMeta fixture runner and completion Event envelope.
- Produces: one strict corpus witness plus one focused lifecycle/order regression.

- [x] **Step 1: Promote test528 before adding its fixture and save RED**

  Change only the manifest/test registration first. Build the W3C target and run filter `test 528`; it must fail because the fixture is absent.

- [x] **Step 2: Add the faithful bounded fixture and confirm semantic RED**

  Enter a child final whose content expression reads an unavailable structured Event field. Route `error.execution` to an intermediate state, reject an early `done.state.s0`, then require the later completion Event to have empty data before reaching pass. Run the focused filter; current code must fail because completion is selected before the newly staged error.

- [x] **Step 3: Add a direct TinyTest regression**

  Reproduce the same order through public session execution and assert no terminal failure, the expected error transition, empty completion data, and successful destroy.

### Task 2: Lower done-data into final-entry execution

**Files:**
- Modify: `src/scxml_impl.h`
- Modify: `src/scxml_analyze.c`
- Modify: `src/scxml_emit.c`

**Interfaces:**
- Produces: `SCXML_STEP_DONEDATA`, block descriptor index, and one synthetic entry block per final state with done-data.
- Consumes: existing done-data descriptors and executable block/step lowering.

- [x] **Step 1: Count bounded synthetic storage**

  Count one block and one step for each admitted `<donedata>`, with checked arithmetic and existing compile diagnostics.

- [x] **Step 2: Emit the synthetic entry block**

  Emit it after all explicit onentry blocks for that final state. Store the descriptor index in the step and preserve invocation lifecycle ordering.

- [x] **Step 3: Extend validation and cleanup invariants**

  Reject descriptor/step index mismatches during emission and ensure partial build cleanup remains complete.

### Task 3: Add bounded completion-data rows

**Files:**
- Modify: `src/scxml_impl.h`
- Modify: `src/scxml_session.c`
- Modify: `src/scxml_runtime.c`
- Modify: `include/scxml/scxml.h`

**Interfaces:**
- Produces: internal FREE/RESERVED/READY row lifecycle and oldest-ready-by-parent lookup.
- Consumes: `completion_capacity`, CMeta copy/destroy traits, existing registry lock, and current Event bindings.

- [x] **Step 1: Allocate checked fixed-capacity storage**

  Allocate checked `completion_capacity + 1` rows during session storage initialization. Document the retained-memory formula next to the public configuration field without changing its type or native queue meaning.

- [x] **Step 2: Implement reserve, publish, consume, and destroy helpers**

  Use a monotonic checked sequence; never evaluate expressions or destroy managed objects while holding the registry lock. A full registry or exhausted sequence must fail fast.

- [x] **Step 3: Materialize every supported done-data form**

  Scalar expressions copy text, inline content copies immutable bytes, and params build an atomic CMeta object from one state snapshot. Expression failure publishes EMPTY and stages one `error.execution`.

- [x] **Step 4: Make completion observation consume precomputed data**

  Remove observer-side expression evaluation. Bind the oldest matching row, retain it for the current Event lifetime, and release the previous current row exactly once when Event bindings advance.

### Task 4: Reach GREEN and verify adjacent behavior

**Files:**
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `docs/superpowers/plans/2026-09-01-scxml-donedata-content-error-ordering.md`

**Interfaces:**
- Produces: corpus baseline 147 PASS / 21 UNSUPPORTED / 34 N/A.

- [x] **Step 1: Run focused GREEN checks**

  Build `scxml_cmeta_test` and `scxml_w3c_conformance_test`; run the new TinyTest case and filter `test 528`.

- [x] **Step 2: Run adjacent conformance checks**

  Run filters for tests 294, 527, and 529 to cover structured, scalar-expression, and inline completion data.

- [x] **Step 3: Run full Release and Debug verification**

  Configure/build/test `win-release-user`; then configure/build/test the repository's matching Debug preset when available. All registered CTest targets must pass.

- [x] **Step 4: Verify corpus and diff hygiene**

  Run the strict inventory test, recompute manifest counts with `rg.exe`, scan changed code for forbidden placeholders, run `git diff --check`, and inspect `git status --short`.

- [x] **Step 5: Review and commit locally**

  Review ownership, error ordering, capacity, and public compatibility against the design; commit the verified change locally with message `feat(scxml): order donedata errors before completion`. Do not push without explicit user direction.
