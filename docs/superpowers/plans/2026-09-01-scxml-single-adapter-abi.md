# TurboSCXML Single Adapter ABI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the three historical TurboSCXML host adapter generations with one exact content-aware public contract.

**Architecture:** Promote the current content-aware representation to suffix-free Event I/O, invocation, request, payload, and metadata types. Store the two copied adapter tables directly in the session and remove every runtime ABI branch while preserving the existing prepare/commit/discard and close/quiescence protocols.

**Tech Stack:** C11, TurboUtils CFlow Statechart, CMeta, TinyTest, CMake Presets

**Spec:** `docs/specs/scxml-single-adapter-abi-design.md`

## Global Constraints

- This is an intentional source and ABI break with no aliases or fallback.
- Statechart transition, configuration, queue, and run-to-completion algorithms do not change.
- Adapter ops are copied; user pointers and callback request views remain borrowed under the existing lifetime contract.
- Every successful prepare transfers exactly one ticket that reaches commit or discard.
- Existing capacities, error mapping, shutdown, and quiescence behavior remain unchanged.

---

### Task 1: Establish the suffix-free public contract

**Files:**
- Modify: `tests/scxml_test.c`
- Modify: `include/scxml/scxml.h`

**Interfaces:**
- Consumes: existing `scxml_session_config`, adapter callbacks, and TinyTest fixture helpers.
- Produces: `SCXML_ADAPTER_ABI`, `scxml_event_io_adapter`, `scxml_invoke_adapter`, suffix-free current requests and metadata, and `scxml_session_try_send_with_metadata()`.

- [x] **Step 1: Write the failing test**

Add a session initialization test using `scxml_event_io_adapter` and
`SCXML_ADAPTER_ABI`, then mutate `struct_size` to prove exact-shape rejection.
The production mutation caught is accepting a prefix, oversized table, or an
adapter whose version does not match the sole current contract.

- [x] **Step 2: Run the focused target and verify RED**

Run the `scxml_test` target through `win-release-user`. Expected: compilation
fails because the suffix-free adapter contract does not yet exist.

- [x] **Step 3: Publish the minimal current header**

Replace the V1/V2/V3 constants and structs with the single current contract,
put current adapter pointers in `scxml_session_config`, remove versioned init and
send declarations, and declare `scxml_session_try_send_with_metadata()`.

### Task 2: Remove runtime compatibility dispatch

**Files:**
- Modify: `src/scxml_impl.h`
- Modify: `src/scxml_session.c`
- Modify: `src/scxml_runtime.h`
- Modify: `src/scxml_runtime.c`

**Interfaces:**
- Consumes: the suffix-free public contract from Task 1.
- Produces: one exact adapter validator, one session initialization path, one
  metadata admission path, and direct adapter calls.

- [x] **Step 1: Collapse session storage and validation**

Keep one Event I/O table and one invoke table in `scxml_session_impl`; validate
exact ABI/size and capability/callback invariants before copying them.

- [x] **Step 2: Collapse payload and request materialization**

Materialize only the content-aware payload form and pass it directly to the
single prepare callbacks. Remove scalar-to-content conversion and all ABI
selection branches.

- [x] **Step 3: Collapse lifecycle and metadata admission**

Call close, quiescence, cancel, forward, send, and start directly through the
single copied tables. Rename V3 metadata admission to
`scxml_session_try_send_with_metadata()` and preserve its atomic reservation and
rollback behavior.

### Task 3: Migrate every in-repository consumer

**Files:**
- Modify: `tests/scxml_test.c`
- Modify: `tests/scxml_cmeta_test.c`
- Modify: `tests/scxml_event_io_contract_test.c`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `tests/w3c/README.md`
- Modify: current `docs/specs/*.md` and `docs/superpowers/plans/*.md` files that describe callable APIs

**Interfaces:**
- Consumes: suffix-free session config and content-aware callbacks.
- Produces: real test adapters using the sole current contract; historical plans may retain explicit historical discussion only when marked superseded.

- [x] **Step 1: Migrate callbacks and fixtures**

Change adapter tables, callback request types, config wiring, session init calls,
payload assertions, and metadata admission calls to the suffix-free API.

- [x] **Step 2: Remove compatibility-only tests**

Delete tests whose only behavior is accepting V1/V2 tables or mixing adapter
generations. Preserve each real send, invoke, payload, rollback, shutdown, and
error-mapping behavior under the current adapter.

- [x] **Step 3: Run focused GREEN tests**

Build and run `scxml_test`, `scxml_cmeta_test`,
`scxml_event_io_contract_test`, and `scxml_w3c_conformance_test` with the
Release preset. Expected: all pass.

### Task 4: Verify the repository boundary

**Files:**
- Modify: `docs/specs/scxml-single-adapter-abi-design.md`
- Modify: `docs/superpowers/plans/2026-09-01-scxml-single-adapter-abi.md`

**Interfaces:**
- Consumes: completed implementation and migrated tests.
- Produces: reproducible verification evidence and a legacy-symbol-free current tree.

- [x] **Step 1: Scan for legacy API residue**

Use `rg.exe` over public headers, production sources, tests, README, and current
design documents. No V1/V2/V3 adapter type, capability, init, send, or runtime
ABI dispatch may remain outside explicitly superseded historical records.

- [x] **Step 2: Run full verification**

Run Release and Debug configure/build/CTest using the repository presets.
Expected: every target passes. Run sanitizer validation when the repository
provides a sanitizer preset; none exists at the time of this change.

- [x] **Step 3: Review and commit**

Run `git diff --check`, CodeGraph affected-test analysis, inspect the complete
diff, and commit the verified breaking cleanup as one coherent change.
