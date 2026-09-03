# CCXML Unjoin Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded `<unjoin>` compilation and transactional provider
dispatch for two copied literal resource identifiers.

**Architecture:** Reuse compact bridge-action storage and append a
size-guarded unjoin command to the telephony adapter. The core owns syntax,
bounded storage, and effect transactions; the provider owns resource and
bridge validation, media teardown, and asynchronous outcomes.

**Tech Stack:** C11, Turbo XML parser, CFlow effect tickets, CMake, TinyTest

**Spec:** `docs/specs/ccxml-unjoin-design.md`

## Global Constraints

- Admit exactly `id1` and `id2`, once each, as nonempty quoted literals.
- Reject `hints`, nested content, escapes, and expressions.
- Copy both identifiers into bounded program storage.
- Append `prepare_unjoin` after `prepare_join` and obey `struct_size`.
- Preserve document-order commit and reverse-order discard.
- Keep registries, bridge/media state, ownership checks, and result events in
  the provider.

---

### Task 1: Compile bounded unjoin identifiers

**Files:** `tests/ccxml_program_test.c`, `src/ccxml_internal.h`,
`src/ccxml_program.c`

- [x] Add TinyTest coverage for valid/source-overwrite, missing and empty IDs,
  nonliteral and escaped values, duplicate/extra attributes, nested content,
  and retained-name budget exhaustion.
- [x] Run focused `ccxml_program` tests RED; valid unjoin is unsupported.
- [x] Add `CCXML_ACTION_UNJOIN`, `uses_unjoin`, strict shared bridge-action
  validation, and checked retained-byte accounting.
- [x] Copy both decoded identifiers and set the distinct action/feature bits.
- [x] Run focused compiler tests GREEN and commit
  `feat(ccxml): compile bounded unjoin endpoints`.

### Task 2: Dispatch transactional unjoin commands

**Files:** `tests/ccxml_session_test.c`, `include/ccxml/ccxml.h`,
`src/ccxml_session.c`

- [x] Add provider-probe tests for exact bytes, non-termination, mixed ordered
  commit, reverse rollback, join-era prefix compatibility, and absent, NULL,
  or truncated callback tails.
- [x] Build runtime RED before the public request and callback exist.
- [x] Add `ccxml_unjoin_request`, append `prepare_unjoin`, and require its full
  non-NULL field only when `uses_unjoin` is true.
- [x] Dispatch through the existing effect-ticket transaction without reading
  Event connection state or synthesizing result events.
- [x] Run focused CCXML tests GREEN and commit
  `feat(ccxml): dispatch transactional unjoin`.

### Task 3: Document, verify, and publish

**Files:** `README.md`, this plan, and the unjoin design.

- [x] Document syntax, provider responsibility, ABI rule, outcomes, and the
  unsupported boundary.
- [x] Fresh-configure/build/test all four supported presets and install/run the
  core C, CCXML C, and CCXML C++ consumers.
- [x] Run diff/style scans, self-review against the design, commit docs, push,
  update PR #35, verify it is clean/mergeable, check this final step, and push.
