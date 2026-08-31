# SCXML Event Envelope Corpus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose bounded field-presence queries for the current SCXML Event and promote W3C mandatory tests 330, 331, 333, 335, 337, and 342 to executable coverage without changing the public ABI.

**Architecture:** `scxml_session_impl.system_values` remains the sole Event-envelope fact source. The internal CMeta expression grammar gains exact `isBound(_event.<field>)` operands; the runtime continues to populate the seven required fields. Test-only host adapters inject or loop back external Events only after committed effects, preserving the existing transaction boundary.

**Tech Stack:** C11, TurboUtils CMeta/CFlow/QueryVM, cxml, TinyTest, CMake Presets.

**Spec:** `docs/specs/scxml-event-envelope-design.md`

## Global Constraints

- Work directly on the explicitly selected `turbo-scxml/main`; TurboUtils is an installed dependency.
- Preserve public C ABI, Event ownership, internal-before-external ordering, effect commit/discard semantics, and fail-fast diagnostics.
- A current Event is bound when `event_name.data != NULL`. Once bound, all seven required Event fields are present; an optional empty field is represented by a non-`NULL`, zero-length view.
- Structured `_event.data` is present when its schema/object witness is bound even when the scalar string view is `NULL`.
- `tests/w3c/manifest.tsv` remains the corpus fact source.

---

### Task 1: Specify and test exact Event-field binding

**Files:**
- Create: `docs/specs/scxml-event-envelope-design.md`
- Modify: `tests/scxml_expr_test.c`
- Modify: `src/scxml_expr.c`

- [x] **Step 1: Document ownership and compatibility**

  Describe the call-scoped Event-envelope view, field-presence witnesses,
  structured-data handling, external admission, loopback transaction order,
  error semantics, compatibility, verification, and rollback.

- [x] **Step 2: Write the failing expression tests**

  Compile all seven exact `isBound(_event.<field>)` forms. Require `false`
  without a current Event, `true` for every field of a bound Event including
  empty optional fields and structured data, and evaluation failure when the
  system-values context is absent. Keep unknown and nested fields invalid.

- [x] **Step 3: Verify RED**

  Build `scxml_expr_test` and run the new TinyTest filter. Expected: the new
  field-qualified syntax is rejected by the current parser.

- [x] **Step 4: Implement the finite parser/evaluator extension**

  Reuse the existing finite Event-field operand kinds. Parse exactly one
  supported field inside `isBound(...)`; evaluate presence from the current
  Event witness while handling structured data explicitly. Do not add public
  types, allocation, fallback, or unrestricted path lookup.

- [x] **Step 5: Verify GREEN and expression regression**

  Run the focused case and complete `scxml_expr_test`.

### Task 2: Add strict W3C Event-envelope fixtures

**Files:**
- Create: `tests/w3c/test330.scxml`
- Create: `tests/w3c/test331.scxml`
- Create: `tests/w3c/test333.scxml`
- Create: `tests/w3c/test335.scxml`
- Create: `tests/w3c/test337.scxml`
- Create: `tests/w3c/test342.scxml`
- Modify: `tests/scxml_w3c_conformance_test.c`

- [x] **Step 1: Add bounded test-only external admission**

  Extend the CMeta fixture runner with an optional prequeued external Event.
  Use `scxml_program_event` and `scxml_session_try_send_with_metadata`; require exact
  admission status and preserve internal-before-external processing.

- [x] **Step 2: Add a committed loopback adapter for test 342**

  Copy the evaluated Event name during `prepare_send`, expose it only after
  ticket commit, pump it through the public external admission API after the
  executor becomes idle, then wait again. Capture `result.pass` separately.
  Discarded effects must never be delivered.

- [x] **Step 3: Add transformed fixtures without weakening assertions**

  Test 330 checks all seven fields on internal and external Events. Test 331
  checks internal, platform, and external classifications. Tests 333, 335, and
  337 check empty metadata in their required contexts. Test 342 retains an
  `eventexpr` send and compares the received `_event.name` with that value.

- [x] **Step 4: Run the focused W3C cases**

  Build `scxml_w3c_conformance_test` and run the six named TinyTest cases.
  Each must finish without runtime error and commit exactly one
  `result.pass` Event.

### Task 3: Synchronize corpus facts, verify, and commit

**Files:**
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `docs/superpowers/plans/2026-08-31-scxml-event-envelope-corpus.md`

- [x] **Step 1: Promote the six manifest rows**

  Change 330, 331, 333, 335, 337, and 342 to `PASS/TERMINAL_PASS`; document
  each transformation and preserved assertion. Update README provenance and
  totals from 49/119 to 55/113.

- [x] **Step 2: Run focused and full verification**

  Run the complete expression and W3C tests, then a fresh Release configure,
  full build, and CTest suite with `--output-on-failure`.

- [x] **Step 3: Inspect repository integrity**

  Run `git diff --check`; inspect status, diff stat, and the complete diff;
  confirm `.codegraph/` and unrelated files are not staged.

- [x] **Step 4: Commit locally and update tracking**

  Mark all plan steps complete, commit only this batch on `main`, and update
  `qigao/turbo-scxml#2` with the commit, corpus totals, and verification
  evidence. Do not push unless the user separately authorizes it.
