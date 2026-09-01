# SCXML Param Location Error Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote W3C mandatory test 298 by proving that an invalid runtime `<donedata><param location>` queues `error.execution` and contributes no name/value pair.

**Architecture:** Reuse the existing atomic completion-data materialization path. Map the generated invalid location to `_event.data.sequence`, which is legal CMeta syntax but cannot resolve while no Event is bound; the existing param assignment failure path publishes an empty completion slot and raises the internal execution error. Add only a strict corpus witness and provenance metadata, with no production or public API/ABI changes.

**Tech Stack:** C11, TurboSCXML, TurboUtils CMeta/CFlow, TinyTest, CMake Presets, W3C SCXML Implementation Report corpus

**Spec:** `tests/w3c/manifest.tsv` test 298 row and `tests/w3c/README.md` provenance rules

## Global Constraints

- Preserve the upstream assertion from `https://www.w3.org/Voice/2013/scxml-irp/298/test298.txml`.
- Preserve the W3C `<param>` rule that an invalid `location` queues `error.execution` and ignores the name/value pair.
- Use the existing CMeta fixture schema and `run_w3c_cmeta_fixture` harness.
- Preserve atomic completion-data ownership and bounded storage.
- Do not change production source, public API/ABI, dependency versions, or CMake configuration.
- Keep the strict corpus at 202 documents: 168 mandatory and 34 optional.

---

### Task 1: Add and promote the test298 corpus witness

**Files:**
- Create: `tests/w3c/test298.scxml`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`

**Interfaces:**
- Consumes: `bool run_w3c_cmeta_fixture(const char *fixture_name)`, `w3c_cmeta_state`, and the existing completion-data materialization path.
- Produces: one executable `test298.scxml` witness and one TinyTest case; no new C interface.

- [x] **Step 1: Write the failing corpus test reference**

Add this TinyTest case beside tests 294 and 488:

```c
it("test 298 raises error.execution for an invalid param location") {
    check_true(run_w3c_cmeta_fixture("test298.scxml"));
}
```

- [x] **Step 2: Run the focused suite to verify RED**

Build `scxml_w3c_conformance_test` with `win-dev-user`, then run TinyTest filter `test 298`.

Expected: FAIL at the new `check_true` because `tests/w3c/test298.scxml` does not exist. Compilation and test discovery must succeed.

- [x] **Step 3: Add the minimal local CMeta transformation**

Create `tests/w3c/test298.scxml`:

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
        <param name="sequence" location="_event.data.sequence"/>
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

The test catches removal of the param failure-to-internal-error mapping, accidental publication of the invalid name/value pair, or wrong internal Event ordering: any such mutation reaches a failure transition or never commits `result.pass`.

- [x] **Step 4: Run the focused suite to verify GREEN**

Run TinyTest filter `test 298` under the Debug profile.

Expected: PASS; the fixture selects `error.execution` before `done.state.s0`, requires the latter Event's `_event.data` to be empty, and `run_w3c_cmeta_fixture` observes exactly one committed `result.pass`, no `result.fail`, a completed session, and no fatal runtime error.

- [x] **Step 5: Promote manifest status and document provenance**

Change test298 from `UNSUPPORTED / NOT_RUN / NONE` to `PASS / TERMINAL_PASS`. Record that the generated invalid param location is mapped to `_event.data.sequence` while no Event is bound, and that the fixture observes both the internal error and the later completion Event. State that the unavailable Event-data location contributes no name/value pair, queues `error.execution` before `done.state.s0`, and leaves that completion Event's data empty. Add the same provenance to `tests/w3c/README.md`, update totals to 154 PASS / 14 UNSUPPORTED, and reduce the param remainder to test343 only in the tracker after merge.

- [x] **Step 6: Verify focused, adjacent, inventory, Debug, and Release tests**

Run focused filters for tests 298, 294, and 488 plus `validates the complete strict upstream inventory`; then run full `win-dev-user` and `win-release-user` CTest presets.

Expected: every command passes; manifest recomputation yields 202 documents, 168 mandatory, 154 mandatory PASS, 14 mandatory UNSUPPORTED, 34 optional, and 34 N/A.

- [x] **Step 7: Commit the verified change**

```text
git add docs/superpowers/plans/2026-09-01-scxml-param-location-error.md tests/scxml_w3c_conformance_test.c tests/w3c/test298.scxml tests/w3c/manifest.tsv tests/w3c/README.md
git commit -m "test(scxml): promote param location error corpus"
```
