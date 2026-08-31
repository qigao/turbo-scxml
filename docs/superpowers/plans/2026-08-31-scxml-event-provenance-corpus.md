# SCXML Event Provenance Corpus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement failed-send `sendid` provenance and promote W3C mandatory tests 332, 336, and 338 without changing public ABI, error classification, or queue ordering.

**Architecture:** Existing bounded Event metadata rows carry failed-send identity on tagged internal platform Events. One executor-scoped restore record reapplies only a generated send `idlocation` after the executable block rolls back. Test-only committed routing proves external reply provenance, while the public invocation-return API proves child `invokeid` provenance.

**Tech Stack:** C11, TurboUtils CMeta/CFlow, cxml, TinyTest, CMake Presets.

**Spec:** `docs/specs/scxml-event-provenance-design.md`

## Global Constraints

- Work directly on the explicitly selected `turbo-scxml/main`; TurboUtils remains an installed dependency.
- Preserve public ABI, adapter status mapping, platform/internal/external classification, internal-before-external ordering, and block rollback for every location except the failed send's own generated `idlocation`.
- Metadata and restore storage remain fixed at `SCXML_EVENT_METADATA_CAPACITY`; no allocation or fallback is introduced.
- Host routing stays test-only and admits only committed reservations through the public bounded session API.
- `tests/w3c/manifest.tsv` remains the corpus fact source.

---

### Task 1: Establish failed-send provenance with TDD

**Files:**
- Modify: `tests/scxml_cmeta_test.c`
- Modify: `src/scxml_impl.h`
- Modify: `src/scxml_runtime.c`

**Interfaces:**
- Consumes: `scxml_send_request.id`, `scxml_effect_descriptor.id_location`, `scxml_runtime_reserve_event_metadata()`, and `raise_internal_tagged`.
- Produces: a platform error Event whose `_event.sendid` equals the preserved generated send `idlocation`.

- [x] **Step 1: Write the failing runtime test**

  Add a TinyTest case with `<send event='out' target='peer'
  idlocation='send_id'/>`. Configure the real v1 adapter boundary to return
  `SCXML_ADAPTER_ERROR_COMMUNICATION`. The error transition must require all of
  these literal behaviors: `send_id != ""`, `_event.sendid == send_id`,
  `_event.type == "platform"`, and an earlier ordinary assignment has its
  original value after rollback. Its fallback enters a non-final state.

- [x] **Step 2: Verify RED**

  Build `scxml_cmeta_test` through `win-release-user` and run the named filter.
  Expected: the session does not reach the success final because the current
  error Event has an empty `sendid` and the generated location was rolled back.

- [x] **Step 3: Add the bounded restore record**

  Add internal session fields for one live flag, copied `scxml_location`, ID
  size, and `SCXML_EVENT_METADATA_CAPACITY + 1` bytes. Clear the flag before
  each executable block and on fatal exit. On recoverable failed send with a
  generated `idlocation`, copy the location and generated ID into this record.

- [x] **Step 4: Raise tagged platform errors with send metadata**

  Add a send-specific adapter-failure path. For a nonempty materialized request
  ID, reserve a metadata row with only `send_id`, raise the same error Event via
  `raise_internal_tagged`, and stage the existing commit/release ticket. Empty
  IDs retain the existing untagged path. Cancel and invoke errors stay on their
  current path.

- [x] **Step 5: Reapply only the generated location after rollback**

  After `SCXML_EXECUTE_BLOCK_ABORTED` restores the input state, call
  `scxml_location_assign_owned_string()` with the copied location and ID. A
  failed reapply is fatal; unrelated staged state remains rolled back. Clear
  the restore flag before returning.

- [x] **Step 6: Verify GREEN and adjacent rollback tests**

  Run the focused case, the complete `scxml_cmeta_test`, and the existing
  rollback/platform-envelope filters.

### Task 2: Preserve three W3C provenance assertions

**Files:**
- Create: `tests/w3c/test332.scxml`
- Create: `tests/w3c/test336.scxml`
- Create: `tests/w3c/test338.scxml`
- Modify: `tests/scxml_w3c_conformance_test.c`

**Interfaces:**
- Consumes: `scxml_session_copy_location()`, `scxml_session_try_send_v2()`, `scxml_session_report_invoke_event()`, and committed Event I/O/invoke tickets.
- Produces: strict terminal evidence for failed-send identity, reply routing, and child-return invocation identity.

- [x] **Step 1: Extend the test CMeta schema and send probe**

  Add an owned `send_id` field. Extend the CMeta send probe with one configured
  recoverable rejection and a fixed maximum loopback count. Result Events use
  their existing probe; rejected or looped business Events are copied before
  returning their exact adapter status.

- [x] **Step 2: Generalize committed loopback pumping**

  Replace the one-message assumption with a bounded idle/pump loop. For the
  routable mode, copy the session location after initialization, attach that
  location and the SCXML Event I/O processor URI as metadata, and require the
  second request's evaluated target/type to match them exactly. Never admit a
  prepared or discarded message.

- [x] **Step 3: Generalize invocation return injection**

  Let the existing invoke fixture runner either report the compiled done Event
  or resolve a named child Event and call `scxml_session_report_invoke_event()`
  with the captured live token. Keep the existing 223/224 behavior unchanged.

- [x] **Step 4: Add the three transformed fixtures**

  Test 332 compares its generated `send_id` with `_event.sendid` after the
  configured adapter rejection. Test 336 sends `foo`, then replies with `bar`
  using `targetexpr='_event.origin'` and `typeexpr='_event.origintype'`. Test
  338 compares the generated invoke `idlocation` with `_event.invokeid` on the
  injected child Event. Every fixture has a strict result pass/fail terminal.

- [x] **Step 5: Run focused W3C verification**

  Build the W3C target and run separate TinyTest filters for 332, 336, and 338.
  Each must commit one `result.pass`; 336 must perform exactly two committed
  business loopbacks, and 338 must use the normal invocation Event API.

### Task 3: Synchronize corpus facts and deliver

**Files:**
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `docs/superpowers/plans/2026-08-31-scxml-event-provenance-corpus.md`

- [x] **Step 1: Promote and document the three rows**

  Change 332, 336, and 338 to `PASS/TERMINAL_PASS`, record the exact host
  transformations, add provenance table entries, and update totals from
  55/113 to 58/110. SystemVariables remaining count becomes five.

- [x] **Step 2: Run fresh full verification**

  Run focused tests, `cmake --fresh --preset win-release-user`, the complete
  Release build, and `ctest --preset win-release-user --output-on-failure` from
  the VS developer environment.

- [x] **Step 3: Review and commit locally**

  Run `git diff --check`, inspect the complete diff and status, confirm no
  `.codegraph/` artifact is staged, mark the plan complete, and commit only
  this batch. Do not push without separate authorization.

- [x] **Step 4: Update TurboSCXML tracking**

  Update `qigao/turbo-scxml#2` with the local commit, 58/110 totals, focused
  evidence, and full CTest result while explicitly noting that code remains
  unpushed.
