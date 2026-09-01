# SCXML DoneData Param Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make valid `<donedata><param .../></donedata>` content appear as a bounded structured CMeta object in the selected `done.state.*` Event, covering W3C-derived test 294.

**Architecture:** TurboSCXML continues to own XML admission, typed IR, and completion-data materialization; CFlow remains the StateChart executor and Event-order fact source. A done-data descriptor retains compiled parameter assignments plus an immutable subset schema. At completion observation, the session copies its state into bounded Event storage, evaluates every parameter against the unchanged source snapshot, writes only into the Event copy, and exposes only declared parameter fields through `_event.data`.

**Tech Stack:** C11, TurboUtils CMeta/CFlow, TurboParser XML, TinyTest, CMake Presets.

**Spec:** W3C SCXML Implementation Report test 294 and `tests/w3c/manifest.tsv`.

## Global Constraints

- Do not change the public TurboSCXML API or adapter ABI.
- Keep the session state as the sole source snapshot; completion payload storage is a derived Event-owned copy.
- Bound the payload by `SCXML_EVENT_DATA_CAPACITY` and `SCXML_PAYLOAD_MAX_ENTRIES`.
- Evaluate each param against the same unchanged state; one param must not affect another param expression.
- Destroy any copied managed CMeta object exactly once on replacement, failure, or session destruction.
- On materialization failure, leave completion data empty and enqueue `error.execution`; do not report success with partial data.
- This increment intentionally implements an atomic CMeta profile rather than
  W3C per-param partial retention; test 343 remains `UNSUPPORTED`.

---

### Task 1: Add the executable conformance witness

**Files:**
- Create: `tests/w3c/test294.scxml`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`

**Interfaces:**
- Consumes: existing `run_w3c_cmeta_fixture(const char *)` and strict result Event probe.
- Produces: one executable W3C-derived test proving named param data followed by inline content data.

- [x] **Step 1: Write the failing fixture and TinyTest case**

  Add a CMeta transformation whose first compound-state completion carries `<param name='sequence' expr='1'/>` and requires `_event.data.sequence == 1`; its second completion carries `<content>foo</content>` and requires `_event.data == "foo"`.

- [x] **Step 2: Run the focused test to verify RED**

  Run the Release `scxml_w3c_conformance_test` filtered to `test 294`. Expected: FAIL because current admission reports `donedata admits exactly one content child`.

### Task 2: Add bounded typed IR for done-data params

**Files:**
- Modify: `src/scxml_analyze.c`
- Modify: `src/scxml_emit.c`
- Modify: `src/scxml_impl.h`

**Interfaces:**
- Consumes: `analyze_param`, `scxml_assign_compile`, `cmeta_data_struct_find_field`, and the program root schema.
- Produces: `scxml_done_data_descriptor.assignment_first`, `.assignment_count`, `.fields`, `.shape`, and `.schema`.

- [x] **Step 1: Admit either content or one-or-more params**

  Reject content/param mixing, empty donedata, non-param children, more than `SCXML_PAYLOAD_MAX_ENTRIES`, unknown CMeta parameter fields, and root storage that cannot fit aligned Event storage. Count one assignment row per param.

- [x] **Step 2: Emit compiled param assignments and a subset schema**

  Compile each param name as the destination location and its `expr` or `location` as the source expression. Copy the matching root field descriptors into descriptor-owned storage and give the subset descriptor a program-owned stable ID.

- [x] **Step 3: Extend cleanup**

  `scxml_emit_destroy_done_data` destroys content expressions and frees descriptor-owned fields/stable IDs for both partial-build and completed-program paths.

### Task 3: Materialize completion params without mutating source state

**Files:**
- Modify: `src/scxml_assign.h`
- Modify: `src/scxml_assign.c`
- Modify: `src/scxml_runtime.c`

**Interfaces:**
- Produces: `scxml_assign_apply_from_with_system(const scxml_assign_program *, const void *source_root, void *destination_root, scxml_expr_is_active_fn, void *, const scxml_expr_system_values *, scxml_expr_diagnostic *)`.
- Consumes: session-owned `current_event_data_object`, descriptor subset schema, and CMeta copy/destroy traits.

- [x] **Step 1: Split assignment evaluation source from destination**

  Preserve existing `scxml_assign_apply*` behavior by forwarding both roots as the same pointer. The new internal function evaluates against `source_root` and writes to `destination_root` using the already-compiled offset and checked conversion logic.

- [x] **Step 2: Bind structured completion Event data**

  Copy the immutable completion state into bounded Event storage, apply every param from the unchanged source into the copy, and publish `_event.data` with the subset schema only after all assignments succeed.

- [x] **Step 3: Handle failures atomically**

  Destroy the copy, clear structured bindings, and raise one internal `error.execution` when copying or evaluating fails. Never expose a partially materialized object.

### Task 4: Verify, document, and commit

**Files:**
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `docs/superpowers/plans/2026-09-01-donedata-param-completion.md`

**Interfaces:**
- Produces: corpus baseline 146 PASS / 22 UNSUPPORTED / 34 N/A if only test 294 is promoted.

- [x] **Step 1: Run focused GREEN checks**

  Build `scxml_w3c_conformance_test`; run TinyTest filter `test 294`; run adjacent filters for tests 527 and 529.

- [x] **Step 2: Run full Release regression**

  Configure with `cmake --fresh --preset win-release-user`, build with the matching build preset, and run `ctest --preset win-release-user --output-on-failure`. Expected: 10/10 CTest targets pass.

- [x] **Step 3: Verify corpus and diff hygiene**

  Run the strict inventory test, `git diff --check`, confirm `.codegraph/` is untracked/ignored, and inspect `git status --short`.

- [x] **Step 4: Commit**

  Commit production code, focused tests, fixture, corpus facts, provenance, and this plan together with message `feat(scxml): materialize donedata params`.
