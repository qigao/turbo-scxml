# SCXML Invoke Finalize Conformance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote W3C SCXML tests 233 and 234 with executable witnesses that prove matching-only finalize execution before transition selection.

**Architecture:** Keep the session invocation registry and CMeta state as the only fact sources. A strict bounded host captures committed invocation tokens, reports one normal Event through the selected token, and observes result/cancellation effects through existing public adapters; no production API or runtime change is planned.

**Tech Stack:** C11, TurboSCXML, TurboUtils CFlow/CMeta, TinyTest, CMake Presets.

**Spec:** `docs/specs/scxml-invoke-finalize-design.md`

## Global Constraints

- Preserve production source and public API/ABI.
- Use `scxml_session_report_invoke_event()` as the only returned-Event admission path.
- Keep invocation, Event, effect, and queue capacities explicit and bounded.
- Verify behavior through committed adapter tickets and terminal `result.pass`, not source-text assertions.
- Do not promote a manifest row until its focused executable witness passes.

---

### Task 1: Add strict finalize fixtures and runner

**Files:**
- Create: `tests/w3c/test233.scxml`
- Create: `tests/w3c/test234.scxml`
- Modify: `tests/scxml_w3c_conformance_test.c`

**Interfaces:**
- Consumes: `scxml_session_report_invoke_event(scxml_session *, uint64_t, const cflow_event_view *)` and the existing v1 invoke/Event-I/O adapters.
- Produces: `run_w3c_invoke_finalize_fixture(const char *, w3c_invoke_finalize_case)` plus TinyTest cases for 233 and 234.

- [x] **Step 1: Add the two local W3C-derived SCXML fixtures**

  Test 233 initializes `sequence` to `1`, assigns `2` in the matching finalize,
  and selects `pass` only when the returned `childToParent` Event observes
  `sequence == 2`. Test 234 initializes `sequence` to `11`; invocation `first`
  assigns `21`, invocation `second` assigns `12`, and the Event reported through
  `first` selects `pass` only for `sequence == 21`.

- [x] **Step 2: Add a strict two-token host probe and fixture runner**

  The start callback accepts only IDs declared by the chosen fixture, copies
  each ID and token in document order, and returns a valid effect ticket. The
  cancel callback verifies the token belongs to the captured set. The runner
  requires exact start/commit/discard counts, reports `childToParent` through
  the first token, waits for the SerialExecutor, and checks the terminal result,
  invocation stats, and cleanup counts.

- [x] **Step 3: Run focused tests against the real runtime**

  Run:
  `build/Msvc-Release/tests/scxml_w3c_conformance_test.exe --filter "test 233"`
  and the equivalent `test 234` filter from `VsDevCmd.bat`.
  Expected: both PASS with no framework errors.

- [x] **Step 4: Prove mutation sensitivity**

  Temporarily replace the matching finalize call in
  `scxml_runtime_preprocess_invocation_external()` with a successful no-op,
  rebuild, and rerun both filters. Expected: both FAIL because `result.fail` is
  observed. Restore the exact production source immediately, rebuild, and
  require both filters to PASS again. Do not commit the mutation.

### Task 2: Promote corpus provenance and counts

**Files:**
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `tests/scxml_w3c_conformance_test.c`

**Interfaces:**
- Consumes: the focused executable witnesses from Task 1.
- Produces: corpus baseline 132 PASS / 36 UNSUPPORTED / 34 N/A.

- [x] **Step 1: Promote manifest rows 233 and 234**

  Mark both rows `PASS` with `TERMINAL_PASS`, name the local transformation,
  and state the exact matching-only/finalize-before-selection witness.

- [x] **Step 2: Update checked manifest totals**

  Change `W3C_PASS_DOCUMENT_COUNT` from `130` to `132` and
  `W3C_UNSUPPORTED_DOCUMENT_COUNT` from `38` to `36`.

- [x] **Step 3: Document fixture provenance**

  Add table rows linking both upstream `.txml` files and a bounded-host note
  explaining token selection, finalize ordering, two-invocation isolation, and
  cancellation cleanup.

- [x] **Step 4: Verify the focused and full Release suite**

  From `VsDevCmd.bat`, run the focused TinyTest filters, then
  `cmake --build --preset win-release-user` and
  `ctest --preset win-release-user --output-on-failure`.
  Expected: both focused cases and all nine CTest entries pass; manifest totals
  are 202 = 168 mandatory + 34 optional = 132 PASS + 36 UNSUPPORTED + 34 N/A.

- [x] **Step 5: Inspect the final diff**

  Run `git diff --check`, confirm `.codegraph/` and build products are absent,
  and verify the diff contains only the planned fixtures, typed AST, host
  transaction integration, tests, CMake registration, and design documents.
