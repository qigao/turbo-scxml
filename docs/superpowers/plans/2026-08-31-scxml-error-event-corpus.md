# SCXML Processor Error Event Corpus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote W3C SCXML tests 401 and 402 with executable witnesses that processor-generated errors enter the internal queue and obey normal Event ordering and matching semantics.

**Architecture:** Keep `tests/w3c/manifest.tsv` as the corpus fact source and exercise the existing CMeta runtime error path. Test 401 uses a test-only serial-executor gate so two external Events are already queued before a protected system-variable assignment raises `error.execution`; the terminal outcome proves the later internal Event wins. Test 402 raises `event1`, generates `error.execution`, then raises `event2` while consuming the queue, proving FIFO placement and ordinary event-descriptor matching. No production API, runtime state, dependency, or allocation behavior changes.

**Tech Stack:** C11, TurboSCXML, TurboUtils CFlow/CMeta/thread primitives, TinyTest, CMake/CTest presets.

**Spec:** W3C SCXML IRP [test 401](https://www.w3.org/Voice/2013/scxml-irp/401/test401.txml) and [test 402](https://www.w3.org/Voice/2013/scxml-irp/402/test402.txml); tracking issue is `qigao/turbo-scxml#2`.

## Global Constraints

- Preserve `tests/w3c/manifest.tsv` as the single machine-readable corpus fact source with exactly 202 rows: 168 mandatory and 34 optional.
- A `PASS` row must name an existing nonempty fixture, use `TERMINAL_PASS`, describe a non-`NONE` transformation, and have synchronized README provenance.
- Preserve `error.execution` as a processor-generated internal Event; do not replace it with an explicit fixture `<raise>`.
- Preserve one runtime state fact source. The test gate controls only when the serial executor may consume already-admitted Events.
- Work inline on `main` as requested; do not push without a separate user request.

## Test Gate Protocol

- **Data unit and capacity:** one borrowed blocker descriptor and exactly two named external Events (`start`, then `foo`) in the existing two-slot external mailbox.
- **Ownership and lifetime:** the test thread owns the stack blocker, session, program, and event views. The executor borrows the blocker only from accepted post until `wait_idle`/destroy completes. Event admission copies the program event identity into the session mailbox according to the existing CFlow contract.
- **Topology and order:** one test-thread producer and one serial-executor consumer. The worker stores `entered`, waits on `release`, and cannot consume the two FIFO external admissions until the test thread publishes `release`.
- **Backpressure and failure:** every post and mailbox admission is checked and fails the fixture immediately. There is no retry or fallback.
- **Shutdown:** every cleanup path stores `release=true` before session or executor destruction. Destruction occurs only after the executor can drain, so the borrowed blocker never outlives its owner.
- **Observability:** the terminal `result.pass`/`result.fail` send is the semantic witness; focused TinyTest diagnostics report runtime status and adapter counts.

---

### Task 1: Register the mandatory witnesses and verify RED

**Files:**
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Missing during RED: `tests/w3c/test401.scxml`
- Missing during RED: `tests/w3c/test402.scxml`

**Interfaces:**
- Consumes: `validate_w3c_manifest()`, `run_w3c_cmeta_fixture()`, `cflow_executor_try_post()`, and `scxml_session_try_send_with_metadata()`.
- Produces: strict inventory totals of 67 `PASS`, 101 `UNSUPPORTED`, and 34 `N/A`, plus deterministic ordered-external admission for test 401.

- [x] **Step 1: Add the ordered-external test gate**

  Extend only the W3C test harness options with a second external Event and an executor-gate flag. Reuse TurboUtils atomics/thread yield and the repository's existing blocker pattern. Centralize named external admission so both Events share one checked path.

- [x] **Step 2: Promote and register tests 401 and 402**

  Change both manifest rows to `PASS/TERMINAL_PASS`, add precise local transformation and assertion text, synchronize README source links, register one TinyTest case per fixture, and change inventory assertions from 65/103/34 to 67/101/34.

- [x] **Step 3: Run the focused RED gate**

  Build `scxml_w3c_conformance_test` and run filters 401 and 402. Expected: both fail because their promoted fixture files are absent. This proves the manifest and registered cases cannot pass without executable witnesses.

### Task 2: Add both fixtures and verify GREEN

**Files:**
- Create: `tests/w3c/test401.scxml`
- Create: `tests/w3c/test402.scxml`

**Interfaces:**
- Consumes: protected `_name` assignment failure, internal Event FIFO ordering, external mailbox ordering, and event-descriptor prefix matching.
- Produces: one internal-over-external priority witness and one ordinary-error-Event FIFO/matching witness.

- [x] **Step 1: Add test 401**

  Start in a gate state. The harness queues external `start` then `foo` while the executor is held. `start` enters the assertion state, whose protected `_name` write raises `error.execution`; `error` reaches pass while the already-queued `foo` reaches fail.

- [x] **Step 2: Add test 402**

  In initial entry, raise `event1` and then fail a protected `_name` write. Consuming `event1` enters a state that raises `event2`; require `error` before `event2`, then consume `event2` to pass. Wildcard alternatives reach fail.

- [x] **Step 3: Verify focused, adjacent, and full regression gates**

  Run filters 401/402 in Release and Debug/ASan, adjacent `scxml_cmeta_test`, the complete W3C target, and all Release CTest targets. Record any pre-existing unrelated Debug failure separately with evidence.

- [x] **Step 4: Update tracker and commit**

  Update `qigao/turbo-scxml#2` to 67 `PASS` / 101 `UNSUPPORTED` / 34 `N/A`, complete the `events` group, record verification evidence, and commit locally. Do not push unless requested.
