# CCXML Prepared Dialog Start Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded prepared `<dialogstart>` path that attaches an existing
detached VoiceXML dialog to the current connection.

**Architecture:** Compile a prepared-dialog-ID read location into
program-owned bytes. At dispatch, validate the current connection, read the ID
through the existing CMeta-capable datamodel boundary, and stage one appended
provider ticket that owns lookup, attachment, execution, and event delivery.

**Tech Stack:** C11, CMeta, Turbo XML parser, CFlow effect tickets, CMake,
TinyTest

**Spec:** `docs/specs/ccxml-prepared-dialogstart-design.md`

## Global constraints

- Admit only a dotted `prepareddialogid` plus exact
  `connectionid="event$.connectionid"`.
- Preserve the existing direct-source `<dialogstart>` profile unchanged.
- Validate the event connection before datamodel or provider callbacks.
- Keep the read value borrowed through the provider prepare callback only.
- Append provider capability behind its exact `struct_size` boundary.
- Retain exactly one provider ticket; commit remains nonblocking and
  infallible.

### Task 1: Compile bounded prepared-dialog-start actions

**Files:** `tests/ccxml_program_test.c`, `src/ccxml_internal.h`,
`src/ccxml_program.c`

- [x] Add RED tests for the admitted form, required attributes, unsupported
  expressions/options/content, source copying, and byte limits.
- [x] Add the action kind/capability, strict profile selection and validator,
  bounded location copy, datamodel-read marker, and one effect slot.
- [x] Run the focused compiler test and commit the compiler slice.

### Task 2: Read and start through the provider transaction

**Files:** `tests/ccxml_session_test.c`, `include/ccxml/ccxml.h`,
`src/ccxml_session.c`

- [x] Add RED tests for exact request bytes, validation ordering, refusal,
  rollback, malformed values/tickets, and callback tail size.
- [x] Append the request/callback API, validate required capabilities and read
  locations, and stage the provider ticket transactionally.
- [x] Run the focused runtime test and commit the transaction slice.

### Task 3: Verify the real CMeta lifecycle

**Files:** `tests/ccxml_cmeta_test.c`

- [x] Extend the detached prepare lifecycle through prepared start and normal
  termination, proving nested CMeta reads reach the provider unchanged.
- [x] Reuse the production CMeta adapter without dialog-specific evaluation
  code.
- [x] Run focused CMeta/runtime tests and commit the integration slice.

### Task 4: Document, verify, and publish

**Files:** `README.md`, this plan, and the design specification

- [x] Document syntax, ownership, asynchronous event responsibility, and
  deferred forms.
- [x] Run Release, Debug/MSVC ASan, CHTTP, QuickJS, install, and installed C/C++
  consumer verification serially because presets share `vcpkg_installed`.
- [x] Self-review, scan test hygiene, commit, push, update PR #35, and confirm
  its final merge state.
