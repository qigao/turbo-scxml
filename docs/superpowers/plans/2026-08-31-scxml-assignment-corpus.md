# SCXML Assignment Corpus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote W3C SCXML tests 287 and 487 with executable CMeta witnesses for a valid assignment and a runtime value that cannot be stored at its valid location.

**Architecture:** Keep `tests/w3c/manifest.tsv` as the corpus fact source and exercise the existing compiled CMeta assignment path through the real session runtime. Test 287 observes the committed integer value from a later eventless guard; test 487 uses the statically numeric expression `1e100`, which compiles for an integer destination but fails exact conversion at execution time, then verifies both `error.execution` delivery and executable-block abortion. No production source, public API/ABI, dependency, allocation policy, or data format changes are required.

**Tech Stack:** C11, TurboSCXML, TurboUtils CFlow/CMeta, TinyTest, CMake/CTest presets.

**Spec:** W3C SCXML IRP [test 287](https://www.w3.org/Voice/2013/scxml-irp/287/test287.txml) and [test 487](https://www.w3.org/Voice/2013/scxml-irp/487/test487.txml); tracking issue is `qigao/turbo-scxml#2`.

## Global Constraints

- Preserve `tests/w3c/manifest.tsv` as the single machine-readable corpus fact source with exactly 202 rows: 168 mandatory and 34 optional.
- A `PASS` row must name an existing nonempty fixture, use `TERMINAL_PASS`, describe a non-`NONE` transformation, and have synchronized README provenance.
- Test 287 must fail if assignment is skipped, stores a wrong value, or is not visible to the following eventless guard.
- Test 487 must use a valid compiled expression and valid location whose evaluated numeric value cannot be represented exactly by the integer destination; a syntax error or unknown location does not preserve the upstream assertion.
- Test 487 must prove `error.execution` is selected and the later `raise event="foo"` is not executed after conversion fails.
- Keep test 286 `UNSUPPORTED/NOT_RUN/NONE`. TurboSCXML currently rejects unknown CMeta assignment locations during document compilation; changing that public load/runtime boundary is outside this no-production-code batch.
- Work inline on `main` as requested; commit locally and do not push without a separate user request.

---

### Task 1: Register both assignment witnesses and verify RED

**Files:**
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Missing during RED: `tests/w3c/test287.scxml`
- Missing during RED: `tests/w3c/test487.scxml`

**Interfaces:**
- Consumes: `validate_w3c_manifest()` and `run_w3c_cmeta_fixture()`.
- Produces: strict inventory totals of 73 `PASS`, 95 `UNSUPPORTED`, and 34 `N/A`, plus one registered TinyTest case per promoted W3C document.

- [x] **Step 1: Promote manifest rows 287 and 487**

  Change both rows to `PASS/TERMINAL_PASS`. For 287, record the generated data ID as `sequence` and the legal value as integer `1`. For 487, record `1e100` as a legal floating expression whose value cannot be represented by the valid integer `sequence` location, plus the retained `foo` failure sentinel.

- [x] **Step 2: Synchronize provenance and inventory assertions**

  Update aggregate counts from 71/97/34 to 73/95/34. Add exact upstream-source rows and explain why test 487 is a runtime conversion failure rather than a compile-time type or syntax rejection. Explicitly keep test 286 unsupported.

- [x] **Step 3: Register focused TinyTest cases**

  Add these cases after the test 224 registration:

  ```c
  it("test 287 assigns a legal value to a valid location") {
      check_true(run_w3c_cmeta_fixture("test287.scxml"));
  }

  it("test 487 raises error.execution for an unrepresentable value") {
      check_true(run_w3c_cmeta_fixture("test487.scxml"));
  }
  ```

- [x] **Step 4: Run the focused RED gate**

  Build `scxml_w3c_conformance_test` through `win-release-user`, then run filters 287 and 487. Each registered test must fail because its fixture is absent; a compile error or unrelated harness failure is not the expected RED.

### Task 2: Add both assignment fixtures and verify GREEN

**Files:**
- Create: `tests/w3c/test287.scxml`
- Create: `tests/w3c/test487.scxml`

**Interfaces:**
- Consumes: CMeta integer assignment, exact numeric conversion, transactional executable-block rollback, internal `error.execution`, and the W3C result adapter.
- Produces: strict terminal witnesses for valid assignment and invalid runtime conversion at a valid location.

- [x] **Step 1: Add test 287**

  Create this CMeta fixture:

  ```xml
  <?xml version="1.0"?>
  <scxml xmlns="http://www.w3.org/2005/07/scxml"
         version="1.0" datamodel="cmeta" initial="s0">
    <state id="s0">
      <onentry><assign location="sequence" expr="1"/></onentry>
      <transition cond="sequence == 1" target="pass"/>
      <transition target="fail"/>
    </state>
    <final id="pass"><onentry><send event="result.pass"/></onentry></final>
    <final id="fail"><onentry><send event="result.fail"/></onentry></final>
  </scxml>
  ```

  The mutation caught is skipping the assignment, storing a different value, or failing to publish the staged value before the next guard evaluation.

- [x] **Step 2: Add test 487**

  Create this CMeta fixture:

  ```xml
  <?xml version="1.0"?>
  <scxml xmlns="http://www.w3.org/2005/07/scxml"
         version="1.0" datamodel="cmeta" initial="s0">
    <state id="s0">
      <onentry>
        <assign location="sequence" expr="1e100"/>
        <raise event="foo"/>
      </onentry>
      <transition event="error.execution" target="verify"/>
      <transition event="*" target="fail"/>
    </state>
    <state id="verify">
      <onentry><raise event="check.487"/></onentry>
      <transition event="foo" target="fail"/>
      <transition event="check.487" target="pass"/>
      <transition event="*" target="fail"/>
    </state>
    <final id="pass"><onentry><send event="result.pass"/></onentry></final>
    <final id="fail"><onentry><send event="result.fail"/></onentry></final>
  </scxml>
  ```

  `1e100` is finite and parses as a floating value, so compilation accepts the numeric source/destination family. Runtime exact conversion to the CMeta integer fails. After the processor error is selected, the verify state's `check.487` Event proves a wrongly continued `foo` did not remain ahead of it in the FIFO internal queue.

- [x] **Step 3: Run focused GREEN and adjacent assignment coverage**

  Run Release and Debug/ASan filters 287 and 487, then run the complete Release W3C and CMeta tests. Both fixtures must reach exactly one committed `result.pass` through the real session and Event I/O adapter.

- [x] **Step 4: Run full regression, review, and update tracking**

  Run all eight Release CTest targets, recompute the manifest, and request an independent read-only review. Update `qigao/turbo-scxml#2` to 73 PASS / 95 UNSUPPORTED / 34 N/A and reduce `assign` from three to one remaining document (test 286). Commit locally with `test(scxml): cover assignment conformance`; do not push unless requested.
