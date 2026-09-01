# SCXML Param Partial Failure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve successful CMeta completion params while omitting each failed param and queuing `error.execution`, then promote W3C mandatory test343.

**Architecture:** Keep immutable candidate assignments and fields in the compiled program. Preallocate one field-projection slice and stable-ID slice per completion slot at session initialization, then derive a slot-owned compatible subset schema without runtime allocation for projection metadata. Existing CMeta object-copy and field-adapter allocation semantics remain unchanged. The completion slot remains the sole owner of the copied object and publishes either the full static schema, a mixed-success projection, or empty data.

**Tech Stack:** C11, TurboSCXML, TurboUtils CMeta/CFlow, TinyTest, CMake Presets

**Spec:** `docs/specs/scxml-completion-param-failure-design.md`

## Global Constraints

- Preserve the W3C rule that every invalid `param/@location` or failing `param/@expr` queues `error.execution` and contributes no name/value pair.
- Evaluate all params in XML document order from one immutable state snapshot.
- Keep completion storage single-owner, bounded, preallocated, and free of runtime heap allocation.
- Preserve successful-only test294 and all-failed tests 298 and 488.
- Change no public C API/ABI, CMake target, dependency version, or serialized format.
- Keep the strict corpus at 202 documents: 168 mandatory and 34 optional.

---

### Task 1: Add mixed-success failing regressions

**Files:**
- Modify: `tests/scxml_cmeta_test.c`
- Create: `tests/w3c/test343.scxml`
- Modify: `tests/scxml_w3c_conformance_test.c`

**Interfaces:**
- Consumes: existing CMeta session helpers and `bool run_w3c_cmeta_fixture(const char *fixture_name)`.
- Produces: two real interpreter witnesses that fail under whole-object discard and require one successful projected field.

- [x] **Step 1: Rewrite the focused CMeta behavior test**

Rename the existing `discards failed donedata params and processes error.execution` test to `preserves valid donedata params when a sibling fails`. Replace its SCXML document with this state flow while retaining its session setup and copy/move/destroy counters:

```xml
<state id="parent" initial="work">
  <transition event="error.execution" target="waiting"/>
  <transition event="done.state.parent" target="failed"/>
  <state id="work"><transition target="childDone"/></state>
  <final id="childDone">
    <donedata>
      <param name="enabled" expr="true"/>
      <param name="count" expr="source"/>
    </donedata>
  </final>
</state>
<state id="waiting">
  <transition event="done.state.parent"
              cond="_event.data.enabled == true" target="probeMissing"/>
  <transition event="done.state.parent" target="failed"/>
</state>
<state id="probeMissing">
  <transition cond="_event.data.count == 7" target="failed"/>
  <transition target="success"/>
</state>
```

Run with `source=SCXML_PUBLIC_SOURCE_FAIL`; require the session to reach the top-level `success` final without a fatal runtime error. The first completion guard proves the successful field survived. The second guard reaches `failed` if the failed field is still admitted; an omitted field makes the guard error/false and selects the eventless fallback. Retain the existing copy/move/destroy balance assertions through session destruction.

- [x] **Step 2: Add and register the W3C test343 witness**

Create `tests/w3c/test343.scxml` with a successful `sequence=7` param followed by failing `send_id` location `_event.data.send_id`. Require `error.execution` before `done.state.s0`, then require `_event.data.sequence == 7`; in the entered probe state, send `result.fail` if `_event.data.send_id == ""` evaluates true and otherwise take an unconditional transition to `result.pass`.

Register:

```c
it("test 343 omits only a failed completion param") {
    check_true(run_w3c_cmeta_fixture("test343.scxml"));
}
```

- [x] **Step 3: Run both focused tests to verify RED**

Build `scxml_cmeta_test` and `scxml_w3c_conformance_test` with `win-dev-user`. Run TinyTest filters `preserves valid donedata params` and `343`.

Expected: both behavior witnesses fail because the current runtime publishes empty completion data after the first failed assignment. Compilation and fixture loading must succeed.

---

### Task 2: Add bounded per-slot schema projections

**Files:**
- Modify: `src/scxml_impl.h`
- Modify: `src/scxml_session.c`
- Modify: `src/scxml_runtime.c`
- Test: `tests/scxml_cmeta_test.c`
- Test: `tests/w3c/test343.scxml`

**Interfaces:**
- Consumes: `scxml_done_data_descriptor`, `scxml_completion_data_slot`, `scxml_emit_allocate_rows`, `scxml_analyze_checked_add`, and `scxml_analyze_checked_multiply`.
- Produces: session-owned projection slices and `materialize_done_data_object()` partial-failure semantics; no public interface.

- [x] **Step 1: Extend internal completion storage**

Add these internal slot members:

```c
cmeta_data_field_desc *projection_fields;
size_t projection_field_capacity;
char *projection_stable_id;
size_t projection_stable_id_capacity;
cmeta_data_struct_shape projection_shape;
cmeta_data_desc projection_schema;
```

Add session-owned backing arrays and per-slot capacities:

```c
cmeta_data_field_desc *completion_projection_fields;
char *completion_projection_stable_ids;
size_t completion_projection_field_capacity;
size_t completion_projection_stable_id_capacity;
```

- [x] **Step 2: Compute and allocate projection budgets at session initialization**

Scan `program->done_data` for the maximum `assignment_count` and for the maximum checked stable-ID capacity:

```text
strlen(descriptor->schema.stable_id)
+ strlen("#runtime-subset:")
+ descriptor->assignment_count
+ 1 terminator
```

Use checked multiplication by `completion_data_capacity` before allocating the two backing arrays. Return `CFLOW_STATECHART_INSTANCE_LIMIT_EXCEEDED` on arithmetic overflow and `CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED` on allocation failure. Assign each slot a disjoint slice after all allocations succeed. Free both arrays after destroying live completion objects in `session_free_storage()`.

- [x] **Step 3: Preserve projection slices across slot reuse**

Replace the whole-structure zeroing in `completion_data_release()` with a reset that saves the four projection pointer/capacity members, destroys the live payload, clears transient state, then restores those members. `completion_data_publish_empty()` must clear only payload state and retain the preallocated slices.

- [x] **Step 4: Materialize successful fields and omit failed fields**

In `materialize_done_data_object()`:

1. Copy the source object once and mark it live under the descriptor schema.
2. Initialize the projection shape from `descriptor->shape.layout`, the projection schema from `descriptor->schema`, and the stable-ID buffer with the descriptor ID plus `#runtime-subset:` and zero markers.
3. For every assignment in document order, apply it from the immutable source. On success, append `descriptor->fields[assignment]` to `projection_fields` and set that marker to `1`. On failure, call `raise_done_data_execution_error()`; continue only when it returns `SCXML_EXECUTE_CONTINUE`, otherwise release the slot and propagate the fatal outcome.
4. When no field succeeds, destroy the copied object and publish empty data. When every field succeeds, publish `descriptor->schema`. Otherwise terminate the projected stable ID, validate the projected descriptor with `cmeta_data_desc_valid()`, and publish `slot->projection_schema`.
5. Treat storage mismatch or invalid projected schema as a fatal internal invariant failure with the slot released exactly once.

- [x] **Step 5: Run focused tests to verify GREEN**

Rebuild both Debug targets and run filters `preserves valid donedata params`, `343`, `294`, `298`, and `488`.

Expected: every filter passes; managed copy/move/destroy counts balance after destruction; test294 retains full structured data; tests 298 and 488 retain empty data; test343 exposes only the successful field.

Review hardening adds focused filters `each failed donedata`, `different subsets`, and `projection capacity`. Mutation checks must prove that these fail respectively when execution stops at the first failed param, slot release clears projection slices, or projection checked multiplication is removed.

---

### Task 3: Promote provenance and verify the repository

**Files:**
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `docs/superpowers/plans/2026-09-01-scxml-param-partial-failure.md`

**Interfaces:**
- Consumes: strict corpus manifest/inventory checker and the completed test343 behavior.
- Produces: corpus baseline 155 mandatory PASS / 13 mandatory UNSUPPORTED and documented mixed-param ownership semantics.

- [x] **Step 1: Promote test343 and update counts**

Change test343 to `PASS / TERMINAL_PASS`. Document the legal runtime-failing `_event.data.send_id` location, the preceding successful `sequence` param, error-before-completion order, and the projected schema that admits only `sequence`. Update test constants and README totals from 154/14 to 155/13, replace the atomic-discard limitation with the preallocated projection contract, and add the test343 provenance row and narrative.

- [x] **Step 2: Verify the strict inventory and recompute totals**

Run the TinyTest `inventory` filter. Recompute `manifest.tsv` totals from the TSV columns.

Expected: 202 documents, 168 mandatory, 155 mandatory PASS, 13 mandatory UNSUPPORTED, 34 optional, and 34 N/A.

- [x] **Step 3: Run full Debug and Release verification**

Run:

```text
ctest --preset win-dev-user --output-on-failure
ctest --preset win-release-user --output-on-failure
git diff --check
```

Expected: both CTest presets pass all registered tests and the diff check is clean.

- [x] **Step 4: Commit the verified change**

```text
git add docs/specs/scxml-completion-param-failure-design.md docs/superpowers/plans/2026-09-01-scxml-param-partial-failure.md src/scxml_impl.h src/scxml_session.c src/scxml_runtime.c tests/scxml_cmeta_test.c tests/scxml_w3c_conformance_test.c tests/w3c/test343.scxml tests/w3c/manifest.tsv tests/w3c/README.md
git commit -m "feat(scxml): preserve valid completion params"
```
