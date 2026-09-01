# SCXML Invoke Cancellation Conformance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. This task explicitly forbids subagents.

**Goal:** Promote W3C mandatory tests 237 and 252 with a real host-owned child session, automatic parent-exit cancellation, stopped child processing, and rejection of all post-cancel child returns.

**Architecture:** Keep the parent invocation row as the sole token fact source. A bounded test host owns one child TurboSCXML session and maps the existing transactional cancel ticket to `scxml_session_cancel(child)`; all post-cancel reports traverse public APIs and must be rejected before parent selection.

**Tech Stack:** C11, TurboSCXML public adapter/session APIs, installed TurboUtils CFlow/CMeta/TinyTest, CMake user presets, MSVC Release.

**Spec:** `docs/specs/scxml-invoke-cancellation-design.md`

## Global Constraints

- Work only on `feat/w3c-invoke-cancellation`; do not push or merge.
- Do not claim W3C 250 until cancellation can execute the active child configuration's `onexit` handlers.
- Keep child program/session/executor host-owned and bounded; add no global lookup, transport, thread, retry, fallback, or production API change.
- No callback may wait for an executor, recursively pump a session, retain borrowed request pointers, or run while the parent registry mutex is held.
- Manifest remains exactly 202 documents: 168 mandatory and 34 optional.

---

### Task 1: Register strict cancellation tests and capture RED

**Files:**
- Modify: `tests/scxml_w3c_conformance_test.c`
- Create later: `tests/w3c/test237.scxml`, `tests/w3c/test237-child.scxml`, `tests/w3c/test252.scxml`, `tests/w3c/test252-child.scxml`

- [x] Add a cancellation probe that owns one preinitialized child session and copies one committed parent token/ID.
- [x] Require exact start/cancel prepare/commit/discard counts and cancel request identity.
- [x] Register named TinyTest cases 237 and 252 before the four fixture files exist.
- [x] Build the W3C target and run both focused filters; preserve the missing-fixture failures as RED.

### Task 2: Add faithful fixtures and reach focused GREEN

**Files:**
- Create: `tests/w3c/test237.scxml`
- Create: `tests/w3c/test237-child.scxml`
- Create: `tests/w3c/test252.scxml`
- Create: `tests/w3c/test252-child.scxml`
- Modify: `tests/scxml_w3c_conformance_test.c`

- [x] Test 237: start a real waiting child, make the parent leave the invoking state, require cancel commit, require a later child Event admission to fail, and require parent completion reporting through the cancelled token to fail.
- [x] Test 252: cancel from a nested invoking state, attempt both a normal returned Event and completion through the stale token, require both `INVALID_ARGUMENT`, then admit one independent timeout Event that alone reaches pass.
- [x] Wait for executors only outside adapter callbacks and require both sessions to destroy successfully.
- [x] Run the 237/252 focused filters and the complete W3C executable.

### Task 3: Promote corpus facts

**Files:**
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`

- [x] Change only rows 237 and 252 to `PASS` with truthful transformation and rationale fields.
- [x] Document the real child ownership/cancel witness and explicitly retain test 250 as unsupported.
- [x] Recompute exact totals: 202 rows, 168 mandatory, 34 optional, 136 PASS, 32 UNSUPPORTED, 34 N/A, and 9 remaining invoke rows.

### Task 4: Verify and commit locally

**Files:**
- Modify: this plan to check completed steps.

- [x] Run focused Release filters, complete W3C, fresh Release configure/build, and full CTest.
- [x] Run manifest calculation, `git diff --check`, focus-marker/placeholder scan, and review ownership, ordering, rejection, cleanup, and fixture provenance.
- [x] Commit the verified change once; do not push or merge.
