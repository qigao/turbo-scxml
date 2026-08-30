# SCXML Final/Completion Corpus Implementation Plan

**Goal:** Promote W3C SCXML IRP documents 372, 570, and 415 from
`UNSUPPORTED` to executable local `PASS` witnesses without changing the
production API or runtime state ownership.

**Architecture:** `tests/w3c/manifest.tsv` remains the corpus fact source.
Tests 372 and 570 use one test-only CMeta integer to observe completion-event
ordering and an Event I/O probe to distinguish terminal `pass` from `fail`.
Test 415 compiles the null datamodel directly to CFlow and uses the existing
versioned `on_event` hook to prove that the internal event raised by a
top-level final state's `onentry` is never selected. All observation state is
owned by the test harness and borrowed only through initialization/destruction.
Test 415 exposed a CFlow driver defect: an already queued root completion was
selected after ordinary internal Events. The runtime fix processes a ready
root completion before ordinary internal work, preserving root completion
transitions and terminating immediately when that completion is unhandled.

**Compatibility:** No public header, data format, or dependency changes. The
only production change is the internal CFlow driver ordering at a completed
root configuration; existing handled and unhandled root-completion regression
tests remain authoritative. The corpus totals move from 39/129/34 to
42/126/34 only after all three executable witnesses pass.

## Tasks

- [x] Add failing manifest/test expectations for documents 372 and 570.
- [x] Add the bounded CMeta result probe and local fixtures for 372 and 570.
- [x] Add a failing manifest/test expectation for document 415.
- [x] Add the CFlow event-selection probe and local fixture for 415.
- [x] Run the focused W3C test, adjacent regression tests, and all CTest targets.
- [x] Update corpus documentation and tracking issue #122 with exact evidence.

## Verification

```powershell
cmake --build --preset win-release-user --target scxml_w3c_conformance_test
ctest --preset win-release-user -R "^scxml_w3c_conformance_test$" --output-on-failure
ctest --preset win-release-user --output-on-failure
```
