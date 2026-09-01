# SCXML Invoke Event Processor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote W3C mandatory test 253 with one strict, real parent/child TurboSCXML round trip through the SCXML Event I/O Processor.

**Architecture:** Reuse the bounded host router in `scxml_event_io_contract_test.c`. Add a strict invoke lifecycle probe so a canonical SCXML `<invoke id="foo">` is active before the host starts the child session, then route exactly three committed external Events through `#_parent` and `#_foo`, preserving `origintype="scxml"` at both receiving sessions.

**Tech Stack:** C11, TurboSCXML public session APIs, CFlow SerialExecutor/mailboxes, TurboUtils mutex, TinyTest, CMake Presets.

**Spec:** `docs/specs/scxml-invoke-event-processor-design.md`

## Global Constraints

- Do not change production API/ABI, XML production semantics, dependency direction, CMake targets, or default capacities.
- Keep parent/child program, session, executor, endpoint, and routing ownership in the test host.
- Use fixed endpoint/message/ticket storage; no recursion, callback wait, hidden retry, fallback, drop, or unbounded allocation.
- Preserve one invocation-row fact source and require every accepted effect ticket to commit or discard exactly once.
- Test 253 must fail if either received Event loses the SCXML `origintype` mapping.

---

### Task 1: Register the missing conformance behavior

**Files:**
- Modify: `tests/scxml_event_io_contract_test.c`

**Interfaces:**
- Consumes: `host_run_w3c_route_fixture(const char *, host_w3c_route_kind)`.
- Produces: enum value `HOST_W3C_INVOKED_EVENT_IO` and one TinyTest case named `test 253 uses SCXML Event I/O in both invoke directions`.

- [x] **Step 1: Write the failing test**

  Add `HOST_W3C_INVOKED_EVENT_IO` to `host_w3c_route_kind`, then add an `it(...)` that calls `host_run_w3c_route_fixture("test253.scxml", HOST_W3C_INVOKED_EVENT_IO)` without adding the fixture or runner support.

- [x] **Step 2: Run the focused test to verify RED**

  Run `build/Msvc-Release/tests/scxml_event_io_contract_test.exe --filter "test 253" --no-color` after rebuilding the target. Expected: the assertion fails because the new kind/fixture is not implemented.

### Task 2: Implement strict host-owned invoked-session routing

**Files:**
- Modify: `tests/scxml_event_io_contract_test.c`
- Create: `tests/w3c/test253.scxml`
- Create: `tests/w3c/test253-child.scxml`

**Interfaces:**
- Consumes: `scxml_invoke_adapter`, `scxml_event_io_adapter`, `host_router_reserve()`, `host_router_set_parent()`, `host_router_set_invoke_alias()`, `host_router_pump()`, and `scxml_session_get_invoke_stats()`.
- Produces: one bounded test-only invoke probe that validates/copies `token`, `id`, `type`, and `src`, plus an end-to-end 253 witness.

- [x] **Step 1: Add the minimal invoke transaction probe**

  Add distinct fixed start/cancel ticket rows and counters for prepare, commit, and discard. Accept start only for nonzero token, ID `foo`, type `http://www.w3.org/TR/scxml/`, src `test253-child.scxml`, no payload, and no autoforward. Accept cancel only for the same token/ID. `close` marks admission closed; `is_quiescent` requires close and no live ticket.

- [x] **Step 2: Add parent and child fixtures**

  Parent fixture: active `<invoke id="foo" type="http://www.w3.org/TR/scxml/" src="test253-child.scxml"/>`; require incoming `childRunning` to have `_event.origintype == "scxml"`; send `parentToChild` to `#_foo`; accept only returned `success` as terminal pass.

  Child fixture: send `childRunning` to `#_parent` on entry; require incoming `parentToChild` to have `_event.origintype == "scxml"`; send `success` to `#_parent`; route missing/wrong metadata or wrong Events to fail.

- [x] **Step 3: Extend the route runner**

  For `HOST_W3C_INVOKED_EVENT_IO`, compile both fixture files, reserve and relate endpoints before parent initialization, configure the parent invoke adapter/capacity, wait for exactly one committed start, start/activate the child, pump exactly three messages with executor-idle barriers, and require both sessions done without errors plus exact start/cancel counters and `active == 0`.

- [x] **Step 4: Run the focused test to verify GREEN**

  Rebuild `scxml_event_io_contract_test`; run the exact `test 253` filter. Expected: one passing test and no framework errors.

- [x] **Step 5: Prove the metadata assertion is effective**

  Temporarily change the router delivery `origin_type` from `scxml` to a non-SCXML value, rebuild, and rerun the exact filter. Expected: RED. Restore the line, rebuild, and require GREEN.

- [x] **Step 6: Verify ticket identity and failure cleanup**

  Give start/cancel distinct fixed ticket rows and fail on duplicate terminal callbacks, including a callback injected during shutdown. Inject failure after the child's first committed READY message, close both sessions, wait for executor idle, discard bounded pending rows, and require both sessions and endpoints to release before their dependencies.

### Task 3: Promote the corpus and verify the repository

**Files:**
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `docs/superpowers/plans/2026-09-01-scxml-invoke-event-processor.md`

**Interfaces:**
- Consumes: the executable 253 witness from Task 2.
- Produces: corpus counts 137 mandatory PASS / 31 mandatory UNSUPPORTED / 34 optional N/A and one documented invoke milestone.

- [x] **Step 1: Promote manifest row 253**

  Set status to `PASS`, expected outcome to `TERMINAL_PASS`, and record that a committed canonical SCXML invocation starts a real host-owned child while three copied messages prove both directions and both `origintype` checks.

- [x] **Step 2: Update README provenance and counts**

  Change the top counts and strict harness constants to 31 unsupported and 137 local PASS transformations; add `test253.scxml` to the invoke coverage table and explain the separate child fixture transformation and strict lifecycle/metadata witness.

- [x] **Step 3: Run focused and full verification**

  Run a fresh `win-release-user` configure, build, and full CTest. Recalculate manifest applicability/status totals with PowerShell TSV grouping, run `git diff --check`, and use `rg.exe` to reject `fit(`, `it_only(`, unowned `TODO/FIXME/HACK`, and placeholder implementation strings in changed files.

- [x] **Step 4: Review and commit**

  Review the complete diff for ownership, cleanup, strict counters, and corpus accuracy. Mark completed plan checkboxes, then commit all scoped files with `test(scxml): cover invoked Event I/O routing`.
