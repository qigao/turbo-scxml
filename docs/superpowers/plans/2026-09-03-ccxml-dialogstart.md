# CCXML Direct Dialog Start Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded direct `<dialogstart>` path that creates an addressable
VoiceXML dialog on the current event connection.

**Architecture:** Compile a source URI and dialog-ID write location into
program-owned bytes. At dispatch, reserve a provider dialog using the current
event connection, stage the returned ID through the existing datamodel write
boundary, then commit writeback before asynchronous provider publication.

**Tech Stack:** C11, CMeta, Turbo XML parser, CFlow effect tickets, CMake,
TinyTest

**Spec:** `docs/specs/ccxml-dialogstart-design.md`

## Global constraints

- Admit only direct source start with exact `event$.connectionid` binding.
- Never pass `dialogid` location bytes as a provider resource identifier.
- Charge retained NUL bytes and use checked size arithmetic.
- Append provider capability behind its exact `struct_size` boundary.
- Allocate/copy during prepare; commit remains nonblocking and infallible.
- Commit dialog-ID writeback before provider publication and discard in
  reverse order.

### Task 1: Compile bounded direct dialog-start actions

**Files:** `tests/ccxml_program_test.c`, `src/ccxml_internal.h`,
`src/ccxml_program.c`

- [x] Add RED tests for the admitted form, required attributes, exact current
  event expression, unsupported options/content, source copying, and byte
  limits.
- [x] Add the action kind/capability, strict validator, bounded copy, and two
  effect slots.
- [x] Run the focused compiler test and commit the compiler slice.

### Task 2: Transact provider start and ID writeback

**Files:** `tests/ccxml_session_test.c`, `include/ccxml/ccxml.h`,
`src/ccxml_session.c`

- [ ] Add RED tests for exact request bytes, session-time location validation,
  current-event ID checks, write-before-publish order, rollback, malformed
  results/tickets, and provider callback tail size.
- [ ] Append the request/callback API, validate required capabilities, and
  prepare/reorder the provider and datamodel tickets.
- [ ] Run the focused runtime test and commit the transaction slice.

### Task 3: Verify through the real CMeta adapter

**Files:** `tests/ccxml_cmeta_test.c`

- [ ] Add a RED end-to-end session test proving the provider-generated dialog
  ID reaches a nested owned CMeta string before provider publication.
- [ ] Reuse the production adapter without adding dialog-specific CMeta code.
- [ ] Run focused CMeta/runtime tests and commit the integration slice.

### Task 4: Document, verify, and publish

**Files:** `README.md`, this plan, and the design specification

- [ ] Document syntax, ownership, event responsibility, and deferred forms.
- [ ] Run Release, Debug/MSVC ASan, CHTTP, QuickJS, install, and installed C/C++
  consumer verification serially because presets share `vcpkg_installed`.
- [ ] Self-review, scan test hygiene, commit, push, update PR #35, and confirm
  its final merge state.
