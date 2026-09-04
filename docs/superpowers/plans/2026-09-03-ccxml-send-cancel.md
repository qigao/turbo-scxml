# CCXML Send Identifier and Cancel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded CCXML `send/@sendid` and `<cancel>` support through the shared transactional Event I/O and CMeta datamodel contracts.

**Architecture:** Compile send identifier destinations and cancel operands into bounded action rows. Generate per-session namespaced send identifiers, prepare Event I/O and datamodel tickets atomically, and route cancel through the existing Event I/O adapter without adding another delayed-send registry.

**Tech Stack:** C11, Salts XML/UUID/CMeta/CFlow, shared SCXML Event I/O, TinyTest, CMake Presets, MSVC/Ninja.

**Spec:** `docs/specs/ccxml-send-cancel-design.md`

## Global Constraints

- Admit only dotted NCName LHS locations for `send/@sendid`.
- Admit dotted readable locations and quoted literals for `cancel/@sendid`.
- Generate bounded `send.<uuid>.<token>` identifiers and never reuse a consumed token.
- Commit datamodel assignment before the externally visible send while preserving reverse rollback.
- Reuse Event I/O delayed-send/cancel ownership and capability negotiation.
- Keep arbitrary ECMAScript expressions and same-transition sendid dependencies unsupported.
- Preserve adapter close/quiescence and public ABI contracts.

---

### Task 1: Specify compiler admission with failing tests

**Files:**
- Modify: `tests/ccxml_send_test.c`
- Create: `docs/specs/ccxml-send-cancel-design.md`

- [x] Add valid compiler cases for sendid dotted locations and cancel dotted/literal operands.
- [x] Add rejection cases for missing, empty, duplicate, malformed, dynamic, namespaced, and nonempty forms.
- [x] Add retained-byte and effect-count boundary cases.
- [x] Build and run `ccxml_send_test`, confirming the new cases fail before implementation.

### Task 2: Compile send identifiers and cancel actions

**Files:**
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_program.c`
- Test: `tests/ccxml_send_test.c`

- [x] Add `CCXML_ACTION_CANCEL`, program usage flags, and action-row storage conventions.
- [x] Extend send validation/materialization with an optional dotted `sendid` location.
- [x] Validate/materialize cancel location or decoded literal operands.
- [x] Count sendid as two transition effects and cancel as one.
- [x] Run focused compiler tests to green.

### Task 3: Implement transactional runtime behavior

**Files:**
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_session.c`
- Modify: `tests/ccxml_send_test.c`

- [x] Add focused runtime probes for send IDs, datamodel writes, and cancel requests.
- [x] Confirm runtime tests fail before implementation.
- [x] Validate write/read locations and cancel capability during session admission.
- [x] Initialize a Salts UUID namespace only for programs using sendid.
- [x] Generate IDs and atomically prepare send plus datamodel assignment with datamodel-first commit order.
- [x] Resolve cancel literals/locations and retain `prepare_cancel` tickets through the shared transaction path.
- [x] Prove uniqueness, exact forwarding, failure mapping, and reverse rollback in focused tests.

### Task 4: Prove built-in CMeta integration

**Files:**
- Modify: `tests/ccxml_cmeta_adapter_test.c` or the repository's focused CCXML CMeta test
- Test: corresponding CMeta test target

- [x] Add a CMeta state with sufficient owned-string capacity for generated send IDs.
- [x] Dispatch a delayed send, observe the committed ID, then dispatch cancel and verify exact bytes.
- [x] Run the focused CMeta integration test to green.

### Task 5: Document, verify, review, and publish

**Files:**
- Modify: `README.md`
- Modify: `docs/specs/ccxml-send-design.md`
- Modify: `docs/superpowers/plans/2026-09-03-ccxml-send-cancel.md`

- [x] Update supported/deferred feature documentation and host-owned cancel-result event semantics.
- [x] Fresh-configure, build, and test Dev, ASan, Release, QuickJS, and CHTTP matrices.
- [x] Run `git diff --check`, inspect the complete diff, and request independent review.
- [x] Resolve findings and rerun affected tests.
- [x] Commit and push the increment to pull request 36; do not merge without an explicit user request.
