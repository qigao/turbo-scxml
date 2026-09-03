# CCXML Destroy Conference Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans
> to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for
> tracking.

**Goal:** Add bounded `<destroyconference>` compilation, evaluated conference
ID reads, and transactional provider dispatch.

**Architecture:** Compile a quoted value or dotted location into program-owned
bytes. Validate readable locations when a session starts, resolve their string
values synchronously at dispatch, and pass only evaluated bytes to an
append-only telephony prepare operation.

**Tech Stack:** C11, CMeta, Turbo XML parser, CFlow effect tickets, CMake,
TinyTest

**Spec:** `docs/specs/ccxml-destroyconference-design.md`

## Global constraints

- Never pass location or expression source bytes as a provider resource ID.
- Preserve old telephony and datamodel table prefixes using `struct_size`.
- Charge every retained NUL byte and use checked arithmetic.
- Keep datamodel reads synchronous and side-effect free.
- Preserve document-order commit and reverse-order discard.

### Task 1: Compile bounded destroy-conference actions

**Files:** `tests/ccxml_program_test.c`, `src/ccxml_internal.h`,
`src/ccxml_program.c`

- [x] Add RED tests for literal/location forms, missing/empty IDs,
  unsupported expressions/attributes/content, and retained-byte limits.
- [x] Admit and copy the action, record destroy/read capabilities, and measure
  one runtime effect.
- [x] Run focused compiler tests and commit the compiler slice.

### Task 2: Add datamodel-read and provider transactions

**Files:** `tests/ccxml_session_test.c`, `include/ccxml/ccxml.h`,
`src/ccxml_session.c`

- [x] Add RED tests for exact evaluated requests, capability/location checks,
  malformed reads/tickets, refusal rollback, and old callback prefixes.
- [x] Append read operations and `prepare_destroy_conference`, validate only
  the capabilities each program needs, and transact one provider ticket.
- [x] Run focused runtime tests and commit the transaction slice.

### Task 3: Provide CMeta string reads

**Files:** `tests/ccxml_cmeta_test.c`, `src/ccxml_cmeta.c`

- [x] Add RED tests for nested reads, type/path/size rejection, and
  non-mutation.
- [x] Reuse bounded path resolution while keeping writable-owned validation
  separate from readable-string validation.
- [x] Build and run focused tests, then commit the adapter slice.

### Task 4: Document, verify, and publish

**Files:** `README.md`, this plan, and the design specification

- [x] Document supported syntax, ownership, provider event responsibility,
  and unsupported optional forms.
- [x] Run Release, Debug/MSVC ASan, CHTTP, QuickJS, install, and installed C/C++
  consumer verification.
- [x] Self-review the diff, commit docs, push, update PR #35, and confirm its
  merge state.
