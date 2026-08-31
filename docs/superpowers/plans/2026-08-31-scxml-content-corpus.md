# SCXML Content Corpus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote W3C SCXML tests 527 and 529 with executable CMeta witnesses for expression and inline-child `donedata/content` output.

**Architecture:** Keep `tests/w3c/manifest.tsv` as the corpus fact source and exercise the existing completion-event data path. Both fixtures enter a compound final state, consume the real `done.state.<parent>` internal Event, and inspect `_event.data`; no adapter, production source, public API/ABI, dependency, or data format changes are required. Test 528 remains unsupported because its required error-before-completion ordering is not established by the current runtime.

**Tech Stack:** C11, TurboSCXML, TurboUtils CFlow/CMeta, TinyTest, CMake/CTest presets.

**Spec:** W3C SCXML IRP [test 527](https://www.w3.org/Voice/2013/scxml-irp/527/test527.txml) and [test 529](https://www.w3.org/Voice/2013/scxml-irp/529/test529.txml); tracking issue is `qigao/turbo-scxml#2`.

## Global Constraints

- Preserve `tests/w3c/manifest.tsv` as the single machine-readable corpus fact source with exactly 202 rows: 168 mandatory and 34 optional.
- A `PASS` row must name an existing nonempty fixture, use `TERMINAL_PASS`, describe a non-`NONE` transformation, and have synchronized README provenance.
- Observe content only through the selected completion Event's public CMeta `_event.data`; do not inspect internal descriptors or runtime scratch storage.
- Preserve the upstream literal expectations exactly: expression output `foo` for 527 and inline output `21` for 529.
- Keep 528 `UNSUPPORTED/NOT_RUN/NONE`; do not weaken its requirement that `error.execution` precede the completion Event and that the later completion Event carry empty data.
- Work inline on `main` as requested; do not push without a separate user request.

---

### Task 1: Register both mandatory witnesses and verify RED

**Files:**
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Missing during RED: `tests/w3c/test527.scxml`
- Missing during RED: `tests/w3c/test529.scxml`

**Interfaces:**
- Consumes: `validate_w3c_manifest()` and `run_w3c_cmeta_fixture()`.
- Produces: strict inventory totals of 69 `PASS`, 99 `UNSUPPORTED`, and 34 `N/A`, plus one registered TinyTest case per W3C document.

- [x] **Step 1: Promote manifest rows 527 and 529**

  Change both rows to `PASS/TERMINAL_PASS`. For 527, record the generated expression rewrite to CMeta string expression `&quot;foo&quot;`. For 529, record preservation of the inline text child `21`. State that both are observed from `_event.data` on the real parent completion Event.

- [x] **Step 2: Synchronize provenance and inventory assertions**

  Add exact local fixture/source rows to `tests/w3c/README.md`, explain the two local transformations, and update aggregate counts from 67/101/34 to 69/99/34. Keep the README explicit that 528 is not claimed.

- [x] **Step 3: Register focused TinyTest cases**

  Add:

  ```c
  it("test 527 evaluates expression content for completion data") {
      check_true(run_w3c_cmeta_fixture("test527.scxml"));
  }

  it("test 529 preserves inline completion content") {
      check_true(run_w3c_cmeta_fixture("test529.scxml"));
  }
  ```

- [x] **Step 4: Run the focused RED gate**

  Build `scxml_w3c_conformance_test` through `win-release-user`, then run filters 527 and 529. Expected: each registered test fails with its fixture absent. This proves the new PASS rows cannot succeed without executable witnesses.

### Task 2: Add expression and inline-content fixtures and verify GREEN

**Files:**
- Create: `tests/w3c/test527.scxml`
- Create: `tests/w3c/test529.scxml`

**Interfaces:**
- Consumes: CMeta scalar content evaluation, retained inline UTF-8 content, parent `done.state.*` generation, and `_event.data` binding.
- Produces: strict terminal witnesses for both currently supported `donedata/content` forms.

- [x] **Step 1: Add test 527**

  Create a CMeta document whose compound `s0` reaches final child `s02` with `<donedata><content expr="&quot;foo&quot;"/></donedata>`. The exact `done.state.s0` transition with `_event.data == "foo"` reaches `pass`; the unguarded exact fallback reaches `fail`.

- [x] **Step 2: Add test 529**

  Create the same completion structure with `<donedata><content>21</content></donedata>`. Require `_event.data == "21"` on `done.state.s0`; the exact fallback reaches `fail`.

- [x] **Step 3: Verify focused and adjacent behavior**

  Run Release filters 527/529, the complete Release W3C and CMeta tests, then Debug/ASan filters 527/529. The mutation each fixture catches is a missing/incorrect content evaluation, retained byte sequence, completion binding, or Event data lifetime.

- [x] **Step 4: Run full regression and update tracking**

  Run all eight Release CTest targets. Update `qigao/turbo-scxml#2` to 69 PASS / 99 UNSUPPORTED / 34 N/A and reduce `content` from three to one remaining document. Record test 528 as the explicit remaining semantic gap. Commit locally with `test(scxml): cover completion content conformance`; do not push unless requested.
