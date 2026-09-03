# CCXML Detached Dialog Preparation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded detached `<dialogprepare>` path that reserves an
addressable VoiceXML dialog without choosing a media target.

**Architecture:** Compile a source URI and prepared-dialog-ID write location
into program-owned bytes. At dispatch, reserve preparation through an appended
provider callback, stage the returned ID through the existing datamodel write
boundary, then commit writeback before asynchronous provider publication.

**Tech Stack:** C11, CMeta, Turbo XML parser, CFlow effect tickets, CMake,
TinyTest

**Spec:** `docs/specs/ccxml-dialogprepare-design.md`

## Global constraints

- Admit only `dialogid` plus a literal `src`, with no connection/conference.
- Keep provider requests format-neutral except for the standard VoiceXML MIME.
- Charge retained NUL bytes and use checked size arithmetic.
- Append provider capability behind its exact `struct_size` boundary.
- Allocate/copy during prepare; commit remains nonblocking and infallible.
- Commit dialog-ID writeback before provider publication and discard in
  reverse order.

### Task 1: Compile bounded detached dialog-prepare actions

**Files:** `tests/ccxml_program_test.c`, `src/ccxml_internal.h`,
`src/ccxml_program.c`

- [x] Add RED tests for the admitted form, required attributes, unsupported
  expressions/options/content, source copying, and byte limits.
- [x] Add the action kind/capability, strict validator, bounded copy, and two
  effect slots.
- [x] Run the focused compiler test and commit the compiler slice.

### Task 2: Transact provider preparation and ID writeback

**Files:** `tests/ccxml_session_test.c`, `include/ccxml/ccxml.h`,
`src/ccxml_session.c`

- [x] Add RED tests for exact request bytes, session-time location validation,
  write-before-publish order, rollback, malformed results/tickets, and provider
  callback tail size.
- [x] Append the request/callback API, validate required capabilities, and
  prepare/reorder provider and datamodel tickets.
- [x] Run the focused runtime test and commit the transaction slice.

### Task 3: Verify through the real CMeta adapter

**Files:** `tests/ccxml_cmeta_test.c`

- [x] Add an end-to-end prepare-then-terminate test proving the generated ID
  reaches a nested owned CMeta string before provider publication.
- [x] Reuse the production adapter without adding dialog-specific CMeta code.
- [x] Run focused CMeta/runtime tests and commit the integration slice.

### Task 4: Document, verify, and publish

**Files:** `README.md`, this plan, and the design specification

- [x] Document syntax, ownership, event responsibility, and deferred forms.
- [x] Run Release, Debug/MSVC ASan, CHTTP, QuickJS, install, and installed C/C++
  consumer verification serially because presets share `vcpkg_installed`.
- [ ] Self-review, scan test hygiene, commit, push, update PR #35, and confirm
  its final merge state.
