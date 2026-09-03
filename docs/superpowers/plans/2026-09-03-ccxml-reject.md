# CCXML Reject Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded `<reject/>` compilation and transactional provider
dispatch using the current event connection identifier.

**Architecture:** Represent reject as a zero-payload compact action and append
one size-guarded callback to the telephony adapter. Reuse the existing
prepare/commit/reverse-discard transaction; leave authoritative connection
state and asynchronous outcomes with the provider.

**Tech Stack:** C11, Turbo XML parser, CFlow effect tickets, CMake, TinyTest

**Spec:** `docs/specs/ccxml-reject-design.md`

## Global Constraints

- Admit only empty, attribute-free `<reject/>`.
- Default exclusively to the current event `connection_id`.
- Do not add an expression engine, connection registry, or outcome synthesis.
- Append `prepare_reject` after `prepare_disconnect` and preserve safe reads of
  every older size-versioned adapter prefix.
- Preserve document-order prepare/commit and reverse-order discard.
- Run Windows preset commands inside the Visual Studio x64 developer
  environment so compiler and ASan runtime paths remain profile-correct.

---

### Task 1: Compile reject actions

**Files:**
- Modify: `tests/ccxml_program_test.c`
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_program.c`

**Interfaces:**
- Consumes: `ccxml_compile`, `validate_empty_action`, compact action rows.
- Produces: `CCXML_ACTION_REJECT` and `ccxml_program_impl.uses_reject`.

- [x] **Step 1: Write the failing compiler tests**

Add one TinyTest case expecting `<reject/>` to compile, plus independent cases
expecting explicit `connectionid`, `reason`, and nested content to return
`CCXML_UNSUPPORTED_FEATURE`. Removing reject admission, accepting an optional
attribute, or accepting a child must fail at least one case.

- [x] **Step 2: Run the focused compiler test and verify RED**

Run the `ccxml_program_test` target and `ctest -R ccxml_program` through
`win-release-user`. The valid case must fail with
`CCXML_UNSUPPORTED_FEATURE` before production support exists.

- [x] **Step 3: Implement compact reject compilation**

Add `CCXML_ACTION_REJECT`, admit the exact element name through
`validate_empty_action`, copy its kind in the second pass, and set
`uses_reject`. Do not add retained payload fields.

- [x] **Step 4: Run focused compiler tests GREEN and commit**

Rebuild and run `ccxml_program_test`; all cases must pass without warnings.
Commit the three compiler/test files as `feat(ccxml): compile bounded reject
actions`.

### Task 2: Dispatch transactional reject requests

**Files:**
- Modify: `tests/ccxml_session_test.c`
- Modify: `include/ccxml/ccxml.h`
- Modify: `src/ccxml_session.c`

**Interfaces:**
- Consumes: `CCXML_ACTION_REJECT`, `uses_reject`, current event connection ID,
  and `cflow_statechart_effect_ticket`.
- Produces: `ccxml_reject_request` and append-only
  `ccxml_telephony_adapter_v1.prepare_reject`.

- [x] **Step 1: Write failing runtime and ABI tests**

Add a recording provider callback and independent TinyTest cases for exact
connection bytes, missing and embedded-NUL identifiers, commit order with an
earlier create-call, reverse rollback on semantic/provider failure, acceptance
of the disconnect-era adapter prefix for older programs, and rejection of an
absent, NULL, or truncated reject callback.

- [x] **Step 2: Build and verify RED**

Build `ccxml_session_test`. Compilation must fail because the public reject
request and callback field do not exist.

- [x] **Step 3: Implement the append-only adapter command**

Declare `ccxml_reject_request`, append `prepare_reject`, validate the complete
field only when `uses_reject` is true, and dispatch it with the shared
connection validation and ticket-retention machinery. Do not mutate session
termination state on commit.

- [x] **Step 4: Run focused CCXML tests GREEN and commit**

Rebuild `ccxml_program_test` and `ccxml_session_test`, then run
`ctest -R ccxml_`. All cases must pass without warnings. Commit the public API,
runtime, and tests as `feat(ccxml): dispatch transactional reject`.

### Task 3: Document, verify, and publish

**Files:**
- Modify: `README.md`
- Create: `docs/specs/ccxml-reject-design.md`
- Create: `docs/superpowers/plans/2026-09-03-ccxml-reject.md`

**Interfaces:**
- Consumes: the completed compiler and runtime contract.
- Produces: installed API documentation and an updated PR revision.

- [x] **Step 1: Document the incubation boundary**

Document the empty syntax, default connection, provider-owned state check,
asynchronous result events, appended callback, and deferred optional
attributes.

- [x] **Step 2: Run the full verification matrix**

Fresh-configure, build, and test Release, Debug/ASan, CHTTP, and QuickJS user
presets. Install Release through `install-win-release-user`, then build and run
the core C, CCXML C, and CCXML C++ installed consumers with the matching Rocida
and vcpkg runtime paths. Expected counts are 13/13, 13/13, 18/18, and 13/13;
all consumers must exit zero.

- [ ] **Step 3: Run static checks and publish**

Run `git diff --check` across the slice and scan changed source/tests for
focused tests and placeholder markers. Commit documentation, push, update PR
#35's summary, verify `OPEN / CLEAN / MERGEABLE`, then mark this plan complete
in a final documentation commit and push it.
