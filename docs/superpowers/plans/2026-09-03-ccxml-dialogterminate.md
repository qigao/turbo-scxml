# CCXML Normal Dialog Termination Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded normal `<dialogterminate>` execution for literal or CMeta-backed dialog identifiers.

**Architecture:** Compile the identifier into the existing literal/location action storage and append one capability to the copied telephony adapter. Location reads reuse the CMeta string boundary; termination remains a provider-owned asynchronous effect with ordinary rollback and commit ordering.

**Tech Stack:** C11, TurboParser XML, CMeta, CFlow effect tickets, TinyTest, CMake presets

**Spec:** `docs/specs/ccxml-dialogterminate-design.md`

## Global Constraints

- Support only omitted `immediate`, which means normal termination (`false`).
- Accept only nonempty unescaped quoted IDs or dotted NCName readable locations.
- Keep provider and datamodel tables append-only and `struct_size` guarded.
- Retain no caller-owned document or dispatch bytes after the relevant call.
- Keep dialog state, media teardown, and asynchronous result events provider-owned.

---

### Task 1: Compile the bounded action

**Files:** `tests/ccxml_program_test.c`, `src/ccxml_internal.h`,
`src/ccxml_program.c`

**Interfaces:** Produces `CCXML_ACTION_DIALOG_TERMINATE`,
`uses_dialog_terminate`, and an action row with exactly one of `id1` or
`location` populated.

- [x] Add RED TinyTest cases for literal/location forms, required dialog ID,
  invalid expressions, unsupported `immediate`/`hints`, children, retained
  copies, and exact byte limits.
- [x] Add strict compiler validation, bounded measurement/copy, capability
  flags, datamodel-read marking, and one effect slot.
- [x] Run `ccxml_program_test` through `win-release-user` and commit.

### Task 2: Execute through the provider transaction

**Files:** `tests/ccxml_session_test.c`, `include/ccxml/ccxml.h`,
`src/ccxml_session.c`

**Interfaces:** Appends `ccxml_dialog_terminate_request` with resolved ID and
`bool immediate`, plus `prepare_dialog_terminate(user, request, ticket,
out_error)`.

- [x] Add RED TinyTest cases for literal and location values, `immediate ==
  false`, location validation/read failures, malformed values/tickets,
  provider refusal rollback, document-order commit, required capability, and
  prior provider-prefix compatibility.
- [x] Append the public request/callback, validate feature-specific adapter
  tails, resolve the identifier, and retain the provider ticket.
- [x] Run focused program/session tests and commit.

### Task 3: Verify lifecycle, document, and publish

**Files:** `tests/ccxml_cmeta_test.c`, `README.md`, this plan, and the design
specification

**Interfaces:** Uses the production CMeta adapter and provider callbacks to
start a dialog on one dispatch and terminate its written ID on a later
dispatch.

- [x] Add a two-dispatch CMeta lifecycle test and commit the integration slice.
- [ ] Document supported termination semantics and deferred immediate mode.
- [ ] Run Release, Debug/MSVC ASan, CHTTP, QuickJS, install, and installed C/C++
  consumer verification serially.
- [ ] Self-review, scan test hygiene, commit, push, update PR #35, and confirm
  its final merge state.
