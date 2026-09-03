# CCXML Send Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development while implementing each task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded CCXML `<send>` vertical slice that reuses the existing Event I/O transaction boundary without importing SCXML-specific routing semantics.

**Architecture:** The compiler retains literal `target`, `name`, and optional `targettype` values in compact program storage and marks programs that require send. Session initialization copies a shared `scxml_event_io_adapter` only for such programs, validates its send capability, and executes send through the same move-only CFlow effect journal used by telephony and datamodel actions. Telephony and Event I/O adapters retain independent close and quiescence lifecycles.

**Tech Stack:** C11, Salts XML, Salts CFlow statecharts, TinyTest, CMake Presets, MSVC/Ninja.

**Spec:** `docs/specs/ccxml-send-design.md`

## Global constraints

- Support only quoted nonempty literal expressions for `target`, `name`, and explicit `targettype`.
- Default omitted `targettype` to `ccxml`.
- Reject `delay`, `sendid`, `namelist`, arbitrary expressions, and inline content explicitly.
- Share `scxml_event_io_adapter`, `scxml_send_request`, and the existing effect-ticket transaction protocol.
- Keep CCXML routing semantics session-bound; do not reinterpret CCXML targets using SCXML target rules in the core.
- Preserve compatibility for programs that do not use `<send>`.
- Bound all retained strings with `max_name_bytes` and add no dispatch-time allocation.

---

### Task 1: Specify and test compiler admission

**Files:**
- Add: `docs/specs/ccxml-send-design.md`
- Add: `tests/ccxml_send_test.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: CCXML XML source and `ccxml_limits`.
- Produces: deterministic compile status and one retained send action.

- [x] **Step 1: Add failing valid-form tests**

Test explicit and default target types, source-buffer overwrite, and action counting. Build and run the focused target to observe the current unsupported-executable failure.

- [x] **Step 2: Add failing invalid-form tests**

Cover missing and empty required attributes, nonliteral expressions, invalid event names, unsupported attributes, extra attributes, and nested content.

### Task 2: Implement bounded send compilation

**Files:**
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_program.c`

**Interfaces:**
- Adds: `CCXML_ACTION_SEND`, retained request fields, and `uses_send`.
- Preserves: compact two-pass measurement/allocation and existing limits.

- [x] **Step 1: Validate send syntax and limits**

Add one validator for the restricted profile, including ASCII event-name validation, exact attribute admission, empty-content enforcement, checked retained-size arithmetic, action limits, and one effect count.

- [x] **Step 2: Copy send rows into program storage**

Decode and retain target/name/explicit targettype, bind omitted targettype to static `ccxml`, set `uses_send`, and keep storage cursor accounting equal to measurement.

- [x] **Step 3: Run compiler-focused tests**

Build and run `ccxml_send_test` until compiler admission and rejection cases pass.

### Task 3: Test and implement shared runtime dispatch

**Files:**
- Modify: `include/ccxml/ccxml.h`
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_session.c`
- Modify: `tests/ccxml_send_test.c`

**Interfaces:**
- Adds: optional `event_io` and `event_io_user` session configuration fields.
- Consumes: `SCXML_EVENT_IO_CAP_SEND` and `prepare_send`.
- Produces: one staged `scxml_send_request` effect.

- [x] **Step 1: Add failing adapter-contract tests**

Test absent, malformed, and incapable Event I/O tables for send programs while proving programs without send still initialize unchanged.

- [x] **Step 2: Add failing transaction tests**

Test exact request fields, commit, adapter rejection, malformed tickets, mixed telephony/send document-order commit, and reverse-order rollback.

- [x] **Step 3: Implement adapter admission and send execution**

Copy the bounded table, validate ABI/size/capability/callbacks when `uses_send`, construct an empty-payload immediate request, and retain its ticket in the existing effect journal.

- [x] **Step 4: Implement independent lifecycle handling**

Close attached telephony and Event I/O adapters exactly once and require both to be quiescent before destruction.

- [x] **Step 5: Run focused runtime tests**

Build and run `ccxml_send_test` and `ccxml_session_test` with no failures.

### Task 4: Document and verify the public slice

**Files:**
- Modify: `README.md`
- Modify: `docs/superpowers/plans/2026-09-03-ccxml-send.md`

**Interfaces:**
- Produces: user-facing capability and remaining-scope documentation.

- [x] **Step 1: Update capability documentation**

Document the admitted `<send>` profile, shared adapter boundary, and deferred attributes/content.

- [x] **Step 2: Run full source-tree verification**

Fresh-configure, build, and test the Dev/ASan, Release, QuickJS, and CHTTP presets. Require zero failed tests.

- [x] **Step 3: Verify installed consumers and static checks**

Install each applicable profile, rebuild/run installed consumers, run `git diff --check`, and scan for debug residue or unintended dependency regressions.

- [x] **Step 4: Commit and publish for review**

Commit the verified slice on `feat/ccxml-send`, push it, and open or update a pull request without merging it.
