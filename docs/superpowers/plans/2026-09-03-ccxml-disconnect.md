# CCXML Disconnect Slice Implementation Plan

**Goal:** Add bounded `<disconnect/>` compilation and transactional provider
dispatch using the current event connection identifier.

**Architecture:** Represent disconnect as a zero-payload compact action and
append one size-guarded callback to the telephony adapter. Reuse the existing
prepare/commit/reverse-discard transaction; leave asynchronous outcomes and
the connection registry with the provider.

**Tech Stack:** C11, Turbo XML parser, CFlow effect tickets, CMake, TinyTest

**Spec:** `docs/specs/ccxml-disconnect-design.md`

## Constraints

- Admit only empty, attribute-free `<disconnect/>`.
- Default exclusively to the current event `connection_id`.
- Do not add an expression engine, connection registry, or outcome synthesis.
- Preserve safe reads of older size-versioned adapter prefixes.
- Preserve document-order prepare/commit and reverse-order discard.

## Task 1: Compile disconnect actions

**Files:** `tests/ccxml_program_test.c`, `src/ccxml_internal.h`,
`src/ccxml_program.c`

- [x] Add a passing empty-form case and rejection cases for explicit
  `connectionid`, `reason`, and nested actions.
- [x] Run the focused compiler test and observe the valid form fail before
  production support.
- [x] Add `CCXML_ACTION_DISCONNECT`, `uses_disconnect`, validation, and compact
  action copying.
- [x] Run the focused compiler test green and commit the compiler slice.

## Task 2: Dispatch transactional disconnect requests

**Files:** `tests/ccxml_session_test.c`, `include/ccxml/ccxml.h`,
`src/ccxml_session.c`

- [x] Add a recording provider callback and tests for exact request bytes,
  missing/malformed event IDs, mixed commit order, reverse rollback, legacy
  prefixes, and truncated tails.
- [x] Build and observe failure because the public request and callback do not
  exist.
- [x] Append `prepare_disconnect`, validate it only for disconnect programs,
  and dispatch through the shared transaction machinery.
- [x] Run focused and complete CCXML tests green and commit the runtime slice.

## Task 3: Document and verify

**Files:** `README.md`, `docs/specs/ccxml-disconnect-design.md`, this plan

- [x] Document supported syntax, adapter semantics, asynchronous outcomes,
  and deferred attributes/registry work.
- [x] Run Release, Debug/ASan, CHTTP, QuickJS, install, and all three installed
  consumer checks.
- [x] Run `git diff --check` and scan for focused tests/placeholders.
- [ ] Mark this plan complete, commit documentation, push, update PR #35, and
  verify its merge state.
