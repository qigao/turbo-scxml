# SCXML Expression Error Corpus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote W3C SCXML tests 312 and 314 with executable CMeta witnesses for illegal value-expression errors and their required evaluation point.

**Architecture:** Keep `tests/w3c/manifest.tsv` as the corpus fact source and exercise the existing transactional executable-content path. Both fixtures use the legal CMeta expression `_event.data.sequence` while `_event` is unbound, so evaluation fails at runtime rather than document load; the owning block raises one internal `error.execution`, aborts the following `raise`, and preserves the runtime's existing rollback boundary. No production source, public API/ABI, dependency, allocation policy, or data format changes are required.

**Tech Stack:** C11, TurboSCXML, TurboUtils CFlow/CMeta, TinyTest, CMake/CTest presets.

**Spec:** W3C SCXML IRP [test 312](https://www.w3.org/Voice/2013/scxml-irp/312/test312.txml) and [test 314](https://www.w3.org/Voice/2013/scxml-irp/314/test314.txml); tracking issue is `qigao/turbo-scxml#2`.

## Global Constraints

- Preserve `tests/w3c/manifest.tsv` as the single machine-readable corpus fact source with exactly 202 rows: 168 mandatory and 34 optional.
- A `PASS` row must name an existing nonempty fixture, use `TERMINAL_PASS`, describe a non-`NONE` transformation, and have synchronized README provenance.
- Derive the runtime failure from a legal, compiled CMeta expression; do not replace the upstream assertion with a protected-system-variable write or a compile-time syntax rejection.
- Test 312 must prove `error.execution` is selected and the later `raise event="foo"` is not executed after the assignment fails.
- Test 314 must prove no error is raised in `s01` or `s02`, then the same failure is raised only when `s03` evaluates the expression.
- Keep tests 307, 309, 311, 313, and 344 `UNSUPPORTED/NOT_RUN/NONE`; this batch does not claim late binding, recoverable transition-guard errors, invalid-location evaluation, or compile-rejection corpus semantics.
- Keep test 528 unsupported. TurboSCXML already enqueues its processor error and leaves completion data empty, but current CFlow selection pops and processes that completion before the newly enqueued internal Event.
- Work inline on `main` as requested; commit locally and do not push without a separate user request.

---

### Task 1: Register both mandatory witnesses and verify RED

**Files:**
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Missing during RED: `tests/w3c/test312.scxml`
- Missing during RED: `tests/w3c/test314.scxml`

**Interfaces:**
- Consumes: `validate_w3c_manifest()` and `run_w3c_cmeta_fixture()`.
- Produces: strict inventory totals of 71 `PASS`, 97 `UNSUPPORTED`, and 34 `N/A`, plus one registered TinyTest case per promoted W3C document.

- [x] **Step 1: Promote manifest rows 312 and 314**

  Change both rows to `PASS/TERMINAL_PASS`. For 312, record that the generated illegal value expression becomes `_event.data.sequence` during initialization and that the following `foo` raise remains a failure sentinel. For 314, record the same runtime expression only in `s03`, retaining the upstream early-error failure transition on the compound parent.

- [x] **Step 2: Synchronize provenance and inventory assertions**

  Add exact source rows to `tests/w3c/README.md`, document why unbound `_event.data.sequence` is a runtime evaluation error, and update aggregate counts from 69/99/34 to 71/97/34. Do not imply support for syntactically invalid documents or recoverable transition-guard failures.

- [x] **Step 3: Register focused TinyTest cases**

  Add these cases immediately after test 310:

  ```c
  it("test 312 raises error.execution for an illegal value expression") {
      check_true(run_w3c_cmeta_fixture("test312.scxml"));
  }

  it("test 314 raises a value-expression error only when evaluated") {
      check_true(run_w3c_cmeta_fixture("test314.scxml"));
  }
  ```

- [x] **Step 4: Run the focused RED gate**

  Build `scxml_w3c_conformance_test` through `win-release-user`, then run filters 312 and 314. Each registered test must fail because its fixture is absent; a compile error or unrelated harness failure is not the expected RED.

### Task 2: Add both runtime-error fixtures and verify GREEN

**Files:**
- Create: `tests/w3c/test312.scxml`
- Create: `tests/w3c/test314.scxml`

**Interfaces:**
- Consumes: CMeta system-value access, transactional assignment, executable-block abort, internal `error.execution`, and the W3C result adapter.
- Produces: strict terminal witnesses for one illegal value expression and its delayed evaluation point.

- [x] **Step 1: Add test 312**

  Create this CMeta fixture:

  ```xml
  <?xml version="1.0"?>
  <scxml xmlns="http://www.w3.org/2005/07/scxml"
         version="1.0" datamodel="cmeta" initial="s0">
    <state id="s0">
      <onentry>
        <assign location="sequence" expr="_event.data.sequence"/>
        <raise event="foo"/>
      </onentry>
      <transition event="error.execution" target="verify"/>
      <transition event="*" target="fail"/>
    </state>
    <state id="verify">
      <onentry><raise event="check.312"/></onentry>
      <transition event="foo" target="fail"/>
      <transition event="check.312" target="pass"/>
      <transition event="*" target="fail"/>
    </state>
    <final id="pass"><onentry><send event="result.pass"/></onentry></final>
    <final id="fail"><onentry><send event="result.fail"/></onentry></final>
  </scxml>
  ```

  The mutation caught is continuing the executable block after a failed value expression, omitting the processor error, or selecting `foo` before the error.

- [x] **Step 2: Add test 314**

  Create this CMeta fixture:

  ```xml
  <?xml version="1.0"?>
  <scxml xmlns="http://www.w3.org/2005/07/scxml"
         version="1.0" datamodel="cmeta" initial="s0">
    <state id="s0" initial="s01">
      <transition event="error.execution" target="fail"/>
      <state id="s01">
        <onentry><raise event="advance.314.1"/></onentry>
        <transition event="advance.314.1" target="s02"/>
      </state>
      <state id="s02">
        <onentry><raise event="advance.314.2"/></onentry>
        <transition event="advance.314.2" target="s03"/>
      </state>
      <state id="s03">
        <onentry>
          <assign location="sequence" expr="_event.data.sequence"/>
          <raise event="foo"/>
        </onentry>
        <transition event="error.execution" target="verify"/>
        <transition event="*" target="fail"/>
      </state>
    </state>
    <state id="verify">
      <onentry><raise event="check.314"/></onentry>
      <transition event="foo" target="fail"/>
      <transition event="check.314" target="pass"/>
      <transition event="*" target="fail"/>
    </state>
    <final id="pass"><onentry><send event="result.pass"/></onentry></final>
    <final id="fail"><onentry><send event="result.fail"/></onentry></final>
  </scxml>
  ```

  The internal `advance.314.*` Events make any premature processor error compete before the next state entry, so the parent transition catches early evaluation. `s03` catches missing or late processor-error delivery; after that error, the verify state's `check.314` Event proves that a wrongly continued `foo` did not remain ahead of it in the FIFO internal queue.

- [x] **Step 3: Verify focused and adjacent behavior**

  Run Release filters 312/314, complete Release W3C and CMeta tests, then Debug/ASan filters 312/314. The two fixtures must reach exactly one committed `result.pass` through the real session and Event I/O adapter.

- [x] **Step 4: Run full regression, review, and update tracking**

  Run all eight Release CTest targets, recompute the manifest, and request an independent read-only review. Update `qigao/turbo-scxml#2` to 71 PASS / 97 UNSUPPORTED / 34 N/A and reduce `Expressions` from seven to five remaining documents. Commit locally with `test(scxml): cover value expression errors`; do not push unless requested.
