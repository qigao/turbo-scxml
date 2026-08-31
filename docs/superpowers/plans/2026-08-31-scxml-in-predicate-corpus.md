# SCXML In Predicate Corpus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote W3C SCXML tests 310 and 436 with executable witnesses for `In(id)` in every data model supported by TurboSCXML.

**Architecture:** Keep `tests/w3c/manifest.tsv` as the corpus fact source. Test 310 uses the CMeta session path and test 436 uses the null-model native Statechart path; both query the same native active configuration and terminate through strict pass/fail witnesses. No production API, runtime state, dependency, or allocation behavior changes.

**Tech Stack:** C11, TurboSCXML, TurboUtils CFlow/CMeta, TinyTest, CMake/CTest presets.

**Spec:** `docs/specs/scxml-core-design.md`; normative assertions are the W3C SCXML IRP documents linked by manifest rows 310 and 436; tracking issue is `qigao/turbo-scxml#2`.

## Global Constraints

- Preserve `tests/w3c/manifest.tsv` as the single machine-readable corpus fact source with exactly 202 rows: 168 mandatory and 34 optional.
- A `PASS` row must name an existing nonempty fixture, use `TERMINAL_PASS`, describe a non-`NONE` transformation, and have synchronized README provenance.
- Preserve the upstream state-membership assertions; do not replace `In(id)` with a test-only state probe.
- The native CFlow active configuration remains the sole state-membership fact source for both supported data models.
- Work inline on `main` as previously requested; do not push without a separate user request.

---

### Task 1: Register the two mandatory witnesses and verify RED

**Files:**
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Missing during RED: `tests/w3c/test310.scxml`
- Missing during RED: `tests/w3c/test436.scxml`

**Interfaces:**
- Consumes: `validate_w3c_manifest()`, `check_w3c_fixture()`, and `run_w3c_cmeta_fixture()`.
- Produces: strict inventory totals of 65 `PASS`, 103 `UNSUPPORTED`, and 34 `N/A`, plus one registered test per W3C document.

- [x] **Step 1: Promote manifest rows 310 and 436**

  Change both rows to `PASS` and `TERMINAL_PASS`. Describe test 310 as a CMeta rewrite of the generator predicate to `In("s1")`; describe test 436 as a null-model rewrite that first proves an inactive state false, then an active parallel sibling true.

- [x] **Step 2: Synchronize provenance**

  Add both exact local fixture names and W3C source URLs to `tests/w3c/README.md`. State that CMeta and null are the complete currently supported data-model set and share the native active configuration.

- [x] **Step 3: Register tests and update inventory assertions**

  Add these TinyTest cases:

  ```c
  it("test 310 exposes In through the CMeta data model") {
      check_true(run_w3c_cmeta_fixture("test310.scxml"));
  }

  it("test 436 exposes exact In membership in the null data model") {
      check_w3c_fixture("test436.scxml");
  }
  ```

  Change inventory assertions from 63/105/34 to 65/103/34.

- [x] **Step 4: Run the focused RED gate**

  Run the `scxml_w3c_conformance_test` target and its CTest filter through `win-release-user` under `VsDevCmd.bat`.

  Expected: inventory validation fails because `test310.scxml` and `test436.scxml` do not exist. This proves the promoted rows cannot pass without executable local witnesses.

### Task 2: Add the CMeta and null-model fixtures and verify GREEN

**Files:**
- Create: `tests/w3c/test310.scxml`
- Create: `tests/w3c/test436.scxml`

**Interfaces:**
- Consumes: CMeta `In("state")`, null-model `In(state)`, native parallel configuration, and existing strict pass/fail harnesses.
- Produces: one true-membership CMeta witness and one false-then-true null-model witness.

- [x] **Step 1: Add the CMeta test 310 fixture**

  Create a CMeta document whose root enters parallel `p`. State `s0` takes an eventless transition guarded by `In("s1")` while sibling `s1` is active; the true branch reaches `pass`, and the fallback reaches `fail`. Final-state entry sends exactly `result.pass` or `result.fail` for the existing CMeta harness.

- [x] **Step 2: Add the null-model test 436 fixture**

  Create a null-model document whose root enters parallel `p`. State `ps0` first checks inactive root sibling `s1` and fails if it is reported active, then checks active parallel sibling `ps1` and reaches `pass`; the final fallback reaches `fail`.

- [x] **Step 3: Run focused GREEN**

  Rebuild and run only `scxml_w3c_conformance_test` through the Release preset. Expected: both new cases pass and inventory validation reports 65/103/34.

- [x] **Step 4: Run adjacent and full regression**

  Run `scxml_test`, `scxml_cmeta_test`, and `scxml_w3c_conformance_test`, then all eight Release CTest targets. Run the two new W3C test filters under Debug/ASan as the profile-adjacent safety check. Full Debug W3C remains separately blocked by the pre-existing test 415 final-state selection difference reproduced before this batch.

- [x] **Step 5: Update tracker and commit**

  Update `qigao/turbo-scxml#2` to 65 `PASS` / 103 `UNSUPPORTED` / 34 `N/A`, check off `minimal-profile`, reduce `Expressions` from eight to seven remaining documents, and record verification evidence. Commit the independently verified files with:

  ```text
  test(scxml): cover In predicate conformance
  ```
