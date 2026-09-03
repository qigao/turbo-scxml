# CCXML Create Conference Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded `<createconference>` compilation, transactional provider
dispatch, and real left-value writeback through a CMeta-capable datamodel
boundary.

**Architecture:** Compile the left-value as program-owned dotted-path bytes.
At session initialization the datamodel validates it. During dispatch the
telephony provider reserves a conference and returns its ID; the datamodel
copies that ID into a second effect ticket. Both effects commit together or
discard in reverse order.

**Tech Stack:** C11, CMeta, Turbo XML parser, CFlow effect tickets, CMake,
TinyTest

**Spec:** `docs/specs/ccxml-createconference-design.md`

## Global constraints

- Never pass `conferenceid` location bytes to the telephony provider as a
  resource identifier.
- Validate all retained lengths with checked arithmetic and charge NUL bytes.
- Preserve append-only adapter ABI checks using caller-provided `struct_size`.
- Perform every potentially failing allocation or copy during prepare; commit
  remains nonblocking and infallible.
- Preserve document-order commit and reverse-order discard across both effect
  kinds.

### Task 1: Compile bounded create-conference actions

**Files:** `tests/ccxml_program_test.c`, `src/ccxml_internal.h`,
`src/ccxml_program.c`

- [x] Add RED tests for the required left-value, optional literal name,
  unsupported attributes/content/expressions, invalid path syntax, and exact
  retained-byte limits.
- [x] Admit and copy the action, record `uses_create_conference`, and measure
  two runtime effects per action.
- [x] Run focused compiler tests and commit the compiler slice.

### Task 2: Add provider and datamodel transactions

**Files:** `tests/ccxml_session_test.c`, `include/ccxml/ccxml.h`,
`src/ccxml_session.c`

- [x] Add RED tests for provider ID output, separate location writeback,
  commit ordering, rollback, invalid IDs/tickets, session-time validation, and
  append-only callback-tail compatibility.
- [x] Append `prepare_create_conference`, add the versioned datamodel table,
  validate required capabilities, and dispatch two tickets per action.
- [x] Run focused runtime tests and commit the transaction slice.

### Task 3: Provide the CMeta datamodel adapter

**Files:** `tests/ccxml_cmeta_test.c`, `include/ccxml/ccxml.h`,
`src/ccxml_cmeta.c`, `CMakeLists.txt`

- [x] Add RED tests for nested owned-string resolution, staged assignment,
  rollback, size bounds, schema/type rejection, and old-value replacement.
- [x] Implement the explicit CMeta adapter owner and no-fail commit ticket.
- [x] Build and run focused tests, then commit the adapter slice.

### Task 4: Document, verify, and publish

**Files:** `README.md`, this plan, and the design specification

- [x] Document supported syntax, ownership, event responsibility, CMeta setup,
  and unsupported optional forms.
- [x] Run Release, Debug/MSVC ASan, CHTTP, QuickJS, install, and installed C/C++
  consumer verification.
- [ ] Self-review the diff, check formatting/test hygiene, commit docs, push,
  update PR #35, and confirm its merge state.
