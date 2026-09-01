# SCXML Param Expression Error Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote W3C mandatory test 488 by proving that a failing `<donedata><param expr>` queues `error.execution` before an empty completion Event.

**Architecture:** Reuse the existing atomic completion-data materialization path: `materialize_done_data_object` publishes an empty completion slot and stages `error.execution` when any param assignment fails. Add only a strict local corpus transformation and provenance metadata; production code and public API/ABI remain unchanged.

**Tech Stack:** C11, TurboSCXML, TurboUtils CMeta/CFlow, TinyTest, CMake Presets, W3C SCXML Implementation Report corpus

**Spec:** `tests/w3c/manifest.tsv` test 488 row and `tests/w3c/README.md` provenance rules

## Global Constraints

- Preserve the upstream assertion from `https://www.w3.org/Voice/2013/scxml-irp/488/test488.txml`.
- Use the existing CMeta fixture schema and `run_w3c_cmeta_fixture` harness.
- Preserve atomic completion-data behavior: a param failure publishes empty `_event.data` and does not expose partial fields.
- Do not change production source, public API/ABI, dependency versions, or CMake configuration.
- Keep the strict corpus at 202 documents: 168 mandatory and 34 optional.

---

### Task 1: Add and promote the test488 corpus witness

**Files:**
- Create: `tests/w3c/test488.scxml`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`

**Interfaces:**
- Consumes: `bool run_w3c_cmeta_fixture(const char *fixture_name)` and the existing `w3c_cmeta_state` descriptor.
- Produces: one executable `test488.scxml` witness and one TinyTest case; no new C interface.

- [x] **Step 1: Write the failing corpus test reference**

Add this TinyTest case beside tests 286, 287, and 294:

```c
it("test 488 orders a param expression error before empty completion data") {
    check_true(run_w3c_cmeta_fixture("test488.scxml"));
}
```

- [x] **Step 2: Run the focused suite to verify RED**

Run the Debug `scxml_w3c_conformance_test` with TinyTest filter `*test 488*`.

Expected: FAIL because `tests/w3c/test488.scxml` does not exist and the fixture reader reports `fixture=test488.scxml read failed`.

- [x] **Step 3: Add the minimal local CMeta transformation**

Create `tests/w3c/test488.scxml` with a nested final state whose param expression reads `_event.data.sequence` while no Event is bound. The recognized expression compiles, fails when completion data is materialized, selects `error.execution` before `done.state.s0`, and then verifies `_event.data == ""` on the retained completion Event:

```xml
<?xml version="1.0"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml"
       version="1.0" datamodel="cmeta" initial="s0">
  <state id="s0" initial="s01">
    <transition event="error.execution" target="verify"/>
    <transition event="done.state.s0" target="fail"/>
    <state id="s01"><transition target="s02"/></state>
    <final id="s02">
      <donedata>
        <param name="sequence" expr="_event.data.sequence"/>
      </donedata>
    </final>
  </state>
  <state id="verify">
    <transition event="done.state.s0"
                cond="_event.data == &quot;&quot;" target="pass"/>
    <transition event="*" target="fail"/>
  </state>
  <final id="pass"><onentry><send event="result.pass"/></onentry></final>
  <final id="fail"><onentry><send event="result.fail"/></onentry></final>
</scxml>
```

- [x] **Step 4: Run the focused suite to verify GREEN**

Run the Debug `scxml_w3c_conformance_test` with TinyTest filter `*test 488*`.

Expected: PASS; the result adapter observes exactly one committed `result.pass` send and no `result.fail` send.

- [x] **Step 5: Promote manifest status and document provenance**

Change test488 from `UNSUPPORTED / NOT_RUN / NONE` to `PASS / TERMINAL_PASS / <specific local transformation>` and state that `_event.data.sequence` is the runtime-failing legal CMeta expression. Add README provenance explaining that the fixture preserves both required observations: error-before-completion ordering and empty completion data.

- [x] **Step 6: Verify focused, adjacent, corpus, Debug, and Release tests**

Run the focused test488 filter, adjacent test294/test527/test528/test529 filters, the strict corpus bookkeeping test, then complete Debug and Release CTest presets.

Expected: every command passes; corpus totals become 153 mandatory PASS, 15 mandatory UNSUPPORTED, and 34 optional N/A.

- [x] **Step 7: Commit the verified change**

```text
git add docs/superpowers/plans/2026-09-01-scxml-param-expression-error.md tests/scxml_w3c_conformance_test.c tests/w3c/test488.scxml tests/w3c/manifest.tsv tests/w3c/README.md
git commit -m "test(scxml): promote param expression error corpus"
```
