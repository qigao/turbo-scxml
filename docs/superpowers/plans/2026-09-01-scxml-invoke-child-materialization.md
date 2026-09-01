# SCXML Invoked Child Materialization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote W3C mandatory tests 239–245 by running real host-owned child sessions from committed `src`/inline-markup requests and injecting recognized param/namelist values into the child CMeta state.

**Architecture:** Add one focused TinyTest executable whose bounded host copies callback-scoped invocation requests into fixed ticket rows. Outside callbacks, the runner resolves an allowlisted fixture or compiles copied XML, constructs a closed-schema child initial state, runs one child at a time, and relays its Event/completion through the parent's live token.

**Tech Stack:** C11, TurboSCXML public compile/session/invoke APIs, CFlow SerialExecutor, CMeta struct descriptors, TinyTest, CMake Presets.

**Spec:** `docs/specs/scxml-invoke-child-materialization-design.md`

## Global Constraints

- Do not modify production source, public API/ABI, default capacities, dependency direction, or installed exports.
- Keep source resolution, child program/session/executor ownership, payload-to-schema mapping, and transport pumping in the host test.
- Copy every callback-scoped field before `prepare_start` returns; use two fixed start rows, two fixed cancel rows, at most 4096 XML bytes, two named entries, and one child Event.
- Keep inline content and named params in separate witnesses because the current tagged payload cannot represent both in one request; data-injection cases use an allowlisted src child.
- Accept only canonical SCXML type and exact allowlisted relative source names; reject malformed, over-capacity, mixed, or unsupported requests without fallback.
- Every accepted start/cancel ticket terminalizes exactly once; destroy sessions before their executors/programs.

---

### Task 1: Register the missing executable behavior

**Files:**
- Create: `tests/scxml_invoke_child_materialization_test.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `scxml_compile_cmeta()`, `scxml_session_init_cmeta()`, `cflow_executor_serial_init()`, TinyTest `spec()`/`it()`.
- Produces: `run_child_materialization_fixture(const char *, child_case_kind)` and seven named conformance cases.

- [x] **Step 1: Add the test target and failing case table**

  Add this target:

  ```cmake
  turboscxml_add_test(
    scxml_invoke_child_materialization_test
    scxml_invoke_child_materialization_test.c
    DEFINITIONS
      SCXML_W3C_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/w3c")
  ```

  Define `child_case_kind` values `CHILD_CASE_239` through `CHILD_CASE_245`, declare
  `run_child_materialization_fixture()`, and add one `it(...)` per W3C ID. Each case
  must call the runner with the literal fixture name and assert `check_true(...)`.

- [x] **Step 2: Verify RED**

  Run a fresh configure, build only `scxml_invoke_child_materialization_test`, then run
  the executable. Expected: seven assertion failures because none of the seven parent
  fixtures exists; the target itself must compile and run without framework errors.

### Task 2: Implement the bounded host-owned child runner

**Files:**
- Modify: `tests/scxml_invoke_child_materialization_test.c`
- Create: `tests/w3c/test239.scxml`
- Create: `tests/w3c/test239-child.scxml`
- Create: `tests/w3c/test240.scxml`
- Create: `tests/w3c/test240-child.scxml`
- Create: `tests/w3c/test241.scxml`
- Create: `tests/w3c/test242.scxml`
- Create: `tests/w3c/test242-child.scxml`
- Create: `tests/w3c/test243.scxml`
- Create: `tests/w3c/test244.scxml`
- Create: `tests/w3c/test245.scxml`
- Create: `tests/w3c/test245-child.scxml`

**Interfaces:**
- Consumes: callback-scoped `scxml_invoke_start_request`, `scxml_payload_view`, `scxml_content_view`, `scxml_session_report_invoke_event()`, and `scxml_session_report_invoke_done()`.
- Produces: fixed `child_start_row[2]`, fixed `child_cancel_row[2]`, exact-copy prepare callbacks, allowlisted source resolution, `materialize_child_initial_state()`, and sequential `run_one_child()`.

- [x] **Step 1: Define schemas and transactional rows**

  Define `parent_state { int child_value; int unknown_value; }` and
  `child_state { int child_value; }` with complete CMeta descriptors. Define a row
  state enum `FREE/RESERVED/READY/CONSUMED/DISCARDED`, immutable ticket kind, token,
  fixed text/XML storage, two named scalar slots, and duplicate-terminal violation
  counters. Ticket callbacks may only move `RESERVED` to one terminal state.

- [x] **Step 2: Copy and validate start/cancel requests**

  `prepare_start` must require canonical type, nonzero token, bounded ID, no
  autoforward, and exactly one source form: nonempty `src` with optional one/two
  scalar named entries, or XML content with empty `src`. Deep-copy strings and XML
  bytes into the selected row.
  `prepare_cancel` must match a prior start token/ID and use a distinct fixed cancel row.

- [x] **Step 3: Resolve and execute a real child**

  `run_one_child()` must map only `test239-child.scxml`, `test240-child.scxml`,
  `test242-child.scxml`, and `test245-child.scxml` under
  `SCXML_W3C_FIXTURE_DIR`, or compile copied `SCXML_CONTENT_XML_UTF8` bytes directly.
  Build a zero `child_state`, copy an exact
  integer `child_value` named entry when present, increment ignored count for every
  other name, then initialize and run one real child session with its own executor.
  Relay one committed `#_parent` Event by reconstructing the Event in the parent
  program and calling `scxml_session_report_invoke_event()`; otherwise report child
  top-level completion with `scxml_session_report_invoke_done()`.

- [x] **Step 4: Add the seven parent witnesses and four source children**

  Use canonical `type="http://www.w3.org/TR/scxml/"`. Cases 239/242 must sequence
  src then inline XML; cases 240/241 must use `test240-child.scxml` and sequence
  namelist then param; 243 and 244 use that src with one matching named integer; 245
  uses `test245-child.scxml` and sends only `unknown_value`. Every wrong
  child Event transitions to non-final `fail`; only the exact expected Event sequence
  reaches final `pass`.

- [x] **Step 5: Verify GREEN and mutation sensitivity**

  Run all seven cases and require zero failures. Then temporarily mutate one copied
  XML content kind, one recognized payload name, and the unknown-name ignore branch;
  each affected focused case must fail. Restore each mutation and rerun the complete
  executable, requiring all cases green and exact ticket/child counters.

### Task 3: Promote the corpus and complete the invoke-materialization milestone

**Files:**
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `docs/superpowers/plans/2026-09-01-scxml-invoke-child-materialization.md`

**Interfaces:**
- Consumes: seven executable witnesses from Task 2.
- Produces: 144 mandatory PASS / 24 mandatory UNSUPPORTED / 34 optional N/A, with invoke remaining count 1.

- [x] **Step 1: Promote rows 239–245**

  Change each row to `PASS`/`TERMINAL_PASS`. Record whether its witness uses src,
  inline XML, namelist, param, recognized schema injection, or unknown-name ignore;
  do not claim core-owned file loading or child orchestration.

- [x] **Step 2: Synchronize provenance and strict counts**

  Add all seven fixtures and upstream URLs to the invoke table, document the two
  source child fixtures and closed-schema mapping, update README totals to 144/24/34,
  and set `W3C_PASS_DOCUMENT_COUNT=144` and
  `W3C_UNSUPPORTED_DOCUMENT_COUNT=24`.

- [x] **Step 3: Run final verification and review**

  Run the focused executable, adjacent `scxml_event_io_contract_test` and
  `scxml_w3c_conformance_test`, then fresh `win-release-user` configure/build/full
  CTest. Recompute TSV totals and invoke remainder, sync CodeGraph, run
  `git diff --check`, reject focus markers and unowned placeholders, and review the
  complete diff for ownership, ticket identity, bounded copies, shutdown order,
  corpus accuracy, and public-surface stability.

- [x] **Step 4: Commit**

  Stage only the design, plan, test target/source, eleven fixtures, corpus README,
  manifest, and strict count update. Commit with:

  ```text
  test(scxml): execute invoked child documents
  ```
