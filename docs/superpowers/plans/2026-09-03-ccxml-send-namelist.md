# CCXML Send Namelist Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded CCXML `<send namelist>` support through the shared structured payload and CMeta boundaries.

**Architecture:** Compile decoded dotted locations into a flat payload table referenced by send actions. Validate and read those locations through struct-size-protected datamodel adapter tails, materialize callback-scoped `scxml_payload_entry` rows in preallocated session scratch, and pass the existing `scxml_payload_view` to Event I/O.

**Tech Stack:** C11, Salts XML/CMeta/CFlow, shared SCXML Event I/O, TinyTest, CMake Presets, MSVC/Ninja.

**Spec:** `docs/specs/ccxml-send-namelist-design.md`

## Global Constraints

- Accept at most `SCXML_PAYLOAD_MAX_ENTRIES` decoded dotted NCName locations per send.
- Preserve entry order, duplicates, and exact qualified names.
- Charge retained token bytes to `ccxml_limits.max_name_bytes` with checked arithmetic.
- Allocate payload descriptor and scratch arrays before session publication; do not allocate payload arrays during dispatch.
- Reuse `scxml_payload_view`, `SCXML_CONTENT_SCALAR`, and `SCXML_CONTENT_CMETA`.
- Preserve CFlow prepare/commit/discard atomicity and the existing sendid-before-send commit order.
- Keep inline content and general ECMAScript expressions unsupported.

---

### Task 1: Compiler Admission and Compact Payload Rows

**Files:**
- Modify: `tests/ccxml_send_test.c`
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_program.c`

**Interfaces:**
- Consumes: decoded XML attribute bytes, `dotted_location_valid`, and `SCXML_PAYLOAD_MAX_ENTRIES`.
- Produces: `ccxml_payload_row { name, name_size }`, action `payload_first`/`payload_count`, program `payload_count`/`max_send_payload_entries`/`uses_send_payload`.

- [x] **Step 1: Write failing compiler tests**

Add TinyTest cases that compile `namelist='conference.id count'`, an empty or
whitespace-only namelist, and entity-decoded names; reject `a..b`, more than
`SCXML_PAYLOAD_MAX_ENTRIES`, and a decoded retained-name overflow.

- [x] **Step 2: Run the focused test and verify RED**

Run `ccxml_send_test.exe --filter "namelist"` from the Debug preset. Expect the
valid namelist cases to fail with `CCXML_UNSUPPORTED_FEATURE`.

- [x] **Step 3: Implement admission and emission**

Decode the complete attribute once in each pass, split on XML whitespace,
validate each non-empty token with `dotted_location_valid`, count/allocate flat
rows with checked arithmetic, retain one terminated copy per token, and attach
the row range to the send action.

- [x] **Step 4: Run the focused compiler tests and verify GREEN**

Run the same TinyTest filter. Expect all compiler namelist cases to pass.

### Task 2: Datamodel Payload Tail and Transactional Dispatch

**Files:**
- Modify: `include/ccxml/ccxml.h`
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_session.c`
- Modify: `tests/ccxml_send_test.c`

**Interfaces:**
- Consumes: program payload ranges and shared `scxml_content_view`/
  `scxml_payload_entry`/`scxml_payload_view`.
- Produces: adapter callbacks
  `validate_payload_location(user, location, size, out_error)` and
  `read_payload(user, location, size, out_value, out_error)`.

- [x] **Step 1: Write failing session tests**

Add tests proving payload programs require the adapter tail and
`SCXML_EVENT_IO_CAP_PAYLOAD`, preserve two named scalar entries in source order,
and discard an earlier staged action when a payload read fails.

- [x] **Step 2: Run the focused tests and verify RED**

Run `ccxml_send_test.exe --filter "payload"`. Expect initialization or payload
assertions to fail because the core still sends `SCXML_PAYLOAD_NONE`.

- [x] **Step 3: Implement initialization and dispatch**

Validate every compiled location before publication, allocate
`max_send_payload_entries` scratch rows, map adapter failures through existing
CCXML status rules, validate scalar/CMETA callback views, and call
`prepare_send` with an ordered `SCXML_PAYLOAD_NAMED` view. Free scratch during
session destruction.

- [x] **Step 4: Run send tests and verify GREEN**

Build and run the complete `ccxml_send_test` target under Debug/ASan.

### Task 3: Built-in CMeta Payload Projection

**Files:**
- Modify: `src/ccxml_cmeta.c`
- Modify: `tests/ccxml_cmeta_test.c`

**Interfaces:**
- Consumes: existing dotted CMeta resolver, configured string bound, and shared
  `scxml_content_view`.
- Produces: scalar views for bool/integer/float/enum/string and borrowed
  `SCXML_CONTENT_CMETA` views for schema-backed structured values.

- [x] **Step 1: Write failing CMeta integration tests**

Send `conference.id count conference` through a real CCXML session and assert
the Event I/O callback sees exact names, a bounded string scalar, an integer
scalar, and the original conference schema/object view.

- [x] **Step 2: Run the focused CMeta test and verify RED**

Run `ccxml_cmeta_test.exe --filter "namelist"`. Expect session initialization
to reject the absent built-in adapter tail.

- [x] **Step 3: Implement CMeta validation and reads**

Resolve each dotted path safely. Convert supported scalar descriptors using
their reflected bit widths and checked buffer/enum readers; return other valid
schema/object pairs as `SCXML_CONTENT_CMETA`. Reject malformed descriptors,
out-of-range strings, and invalid callback arguments.

- [x] **Step 4: Run the CMeta tests and verify GREEN**

Build and run the complete `ccxml_cmeta_test` target under Debug/ASan.

### Task 4: Documentation and Verification

**Files:**
- Modify: `README.md`
- Modify: `docs/specs/ccxml-send-design.md`
- Modify: `docs/specs/ccxml-send-cancel-design.md`

**Interfaces:**
- Consumes: implemented compiler, adapter, CMeta, and Event I/O behavior.
- Produces: current supported/deferred feature descriptions and verification evidence.

- [x] **Step 1: Update user-facing support notes**

Document namelist bounds, CMeta/object lifetime, Event I/O capability
negotiation, same-transition committed-state visibility, and leave only inline
content/general expressions in the deferred send list.

- [x] **Step 2: Run focused and full verification**

Build the changed targets first, then run complete `win-dev-user`,
`win-release-user`, `win-release-quickjs-user`, and
`win-release-chttp-user` CTest presets. All tests must pass.

- [x] **Step 3: Review and commit**

Inspect the diff for ABI-tail checks, checked arithmetic, callback lifetimes,
rollback coverage, and unrelated changes. Commit with
`feat(ccxml): support send namelist payloads`, push `feat/ccxml-send`, and update
the existing pull request description with this increment and test evidence.
