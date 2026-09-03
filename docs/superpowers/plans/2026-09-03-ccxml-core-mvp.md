# CCXML Core MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a bounded CCXML compiler and synchronous session that executes `<accept/>` and `<exit/>` through an injected transactional telephony adapter.

**Architecture:** Add `TurboSCXML::CCXML` as a thin library above `TurboSCXML::SCXML`. The CCXML compiler owns a compact immutable transition/action program; the session selects exact events and uses existing SCXML/CFlow adapter status and effect-ticket semantics without adding CCXML syntax to the SCXML compiler.

**Tech Stack:** C11, Salts XmlParser, TurboSCXML public adapter contracts, CFlow effect tickets, TinyTest, CMake presets.

**Spec:** `docs/specs/ccxml-core-mvp-design.md`

## Global Constraints

- Root namespace is exactly `http://www.w3.org/2002/09/ccxml` and version is exactly `1.0`.
- The MVP accepts exactly one `<eventprocessor>`, exact nonempty transition events, and empty `<accept/>`/`<exit/>` actions only.
- All document counts and retained bytes have positive hard bounds and checked arithmetic.
- Programs own copied syntax; sessions borrow programs and adapter users.
- Provider effects use prepare/commit/discard; partial prepare failure discards in reverse order.
- Session operations are caller-serialized and contain no process-global mutable state.
- New behavior follows strict RED-GREEN-REFACTOR TinyTest cycles.

---

### Task 1: Public CCXML target and compile contract

**Files:**
- Create: `include/ccxml/ccxml.h`
- Create: `src/ccxml_program.c`
- Create: `src/ccxml_internal.h`
- Create: `tests/ccxml_program_test.c`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `salts_xml_parse`, `salts_xml_document_destroy`, and the immutable XmlParser node/attribute accessors.
- Produces: `ccxml_default_limits`, `ccxml_compile`, `ccxml_program_destroy`, opaque `ccxml_program`, diagnostics, transition/action rows used by Task 2.

- [ ] **Step 1: Write the failing compile tests**

Create `tests/ccxml_program_test.c` with TinyTest cases that compile the exact fixture from the design, overwrite the source buffer after compile, and assert the retained transition count/event through internal test accessors. Add independent cases for wrong namespace, wrong version, a second eventprocessor, missing event, and unsupported `<createcall/>`.

```c
spec("CCXML program") {
    it("copies a bounded eventprocessor program") {
        char source[] =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='connection.alerting'>"
            "<accept/></transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};
        check_equal(ccxml_compile(&program, source, strlen(source), NULL,
                                  &diagnostic), CCXML_OK);
        memset(source, 'x', sizeof(source) - 1u);
        check_equal(ccxml_program_transition_count(&program), (size_t)1);
        check_equal(ccxml_program_transition_event(&program, 0u),
                    "connection.alerting");
        ccxml_program_destroy(&program);
    }
}
```

- [ ] **Step 2: Configure/build and verify RED**

Run the Release configure and build through `win-release-user`. Expected: compilation fails because `<ccxml/ccxml.h>` and `TurboSCXML::CCXML` do not exist.

- [ ] **Step 3: Implement the bounded compiler and target**

Define the public enums/handles/limits and private immutable rows. Parse in two passes: validation/measurement followed by checked allocation/copy. Keep testing-only program accessors in `ccxml_internal.h`, not the public API. Add `turbo_ccxml`, alias it as `TurboSCXML::CCXML`, and link it publicly to `TurboSCXML::SCXML`.

- [ ] **Step 4: Build and verify GREEN**

Run `cmake --build --preset win-release-user --target ccxml_program_test` and `ctest --preset win-release-user -R ccxml_program_test`. Expected: one executable passes all compile cases.

- [ ] **Step 5: Commit**

```text
git add CMakeLists.txt tests/CMakeLists.txt include/ccxml/ccxml.h src/ccxml_program.c src/ccxml_internal.h tests/ccxml_program_test.c docs/specs/ccxml-core-mvp-design.md docs/superpowers/plans/2026-09-03-ccxml-core-mvp.md
git commit -m "feat(ccxml): add bounded core compiler"
```

### Task 2: Transactional session dispatch

**Files:**
- Create: `src/ccxml_session.c`
- Create: `tests/ccxml_session_test.c`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `include/ccxml/ccxml.h`
- Modify: `src/ccxml_internal.h`

**Interfaces:**
- Consumes: immutable transition/action rows from Task 1, `scxml_adapter_status`, and `cflow_statechart_effect_ticket`.
- Produces: `ccxml_session_init`, `ccxml_session_dispatch`, `ccxml_session_close`, `ccxml_session_destroy`, `ccxml_session_is_terminated`, and `ccxml_telephony_adapter_v1`.

- [ ] **Step 1: Write failing dispatch and lifecycle tests**

Create a real in-memory provider whose prepare reserves one fixed row and whose commit/discard mutate observable counters. Add separate TinyTest cases for accepted commit, unmatched event, missing connection ID, rejection after an earlier prepared action, malformed accepted ticket, exit/close idempotence, dispatch after exit, and busy destruction.

```c
static scxml_adapter_status prepare_accept(
    void *user, const ccxml_accept_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    provider_probe *probe = user;
    probe->prepared += 1u;
    memcpy(probe->connection_id, request->connection_id,
           request->connection_id_size);
    *out_ticket = make_probe_ticket(probe);
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}
```

- [ ] **Step 2: Build and verify RED**

Run the focused target. Expected: link or compile failure because session functions are absent.

- [ ] **Step 3: Implement minimal session behavior**

Copy and validate the adapter table during init. Allocate transaction ticket scratch to the program's maximum transition action count. Select the first byte-exact event in document order. Prepare actions in order, validate accepted tickets, discard prepared tickets in reverse on failure, commit in order on success, and close once after committed exit.

- [ ] **Step 4: Run focused and full GREEN verification**

Run `ctest --preset win-release-user -R ccxml_` and then the full Release CTest preset. Expected: both CCXML tests and all existing SCXML tests pass.

- [ ] **Step 5: Commit**

```text
git add CMakeLists.txt tests/CMakeLists.txt include/ccxml/ccxml.h src/ccxml_session.c src/ccxml_internal.h tests/ccxml_session_test.c
git commit -m "feat(ccxml): dispatch transactional telephony actions"
```

### Task 3: Installed C and C++ component contract

**Files:**
- Create: `tests/install_consumer/ccxml_main.c`
- Create: `tests/install_consumer/ccxml_main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `cmake/TurboSCXMLConfig.cmake.in`
- Modify: `tests/install_consumer/CMakeLists.txt`
- Modify: `README.md`

**Interfaces:**
- Consumes: `TurboSCXML::CCXML` and `<ccxml/ccxml.h>` from Tasks 1-2.
- Produces: installed component `CCXML` usable from C11 and C++17 consumers.

- [ ] **Step 1: Add consumer sources and make installed consumption fail**

Both consumers include `<ccxml/ccxml.h>`, call `ccxml_default_limits`, and validate the ABI constants without implementing their own main framework. Configure the consumer to request components `SCXML CCXML`. Expected before packaging changes: `find_package` or target resolution fails.

- [ ] **Step 2: Export the target and header**

Install `turbo_ccxml` in `TurboSCXMLTargets`, install `include/ccxml/ccxml.h`, and set `TurboSCXML_CCXML_FOUND TRUE` before `check_required_components`. Add C and C++ consumer executables linked only to `TurboSCXML::CCXML`.

- [ ] **Step 3: Document the incubation boundary**

Add a README section listing supported CCXML syntax, explicit non-goals, ownership, and a short compile/session example. State that this is an MVP component and not complete CCXML conformance.

- [ ] **Step 4: Verify build, tests, install, and consumers**

Run Release configure/build/test/install through documented presets, then configure and build `tests/install_consumer` against the installed Release root using its existing profile mechanism. Expected: C and C++ CCXML consumers link and exit zero.

- [ ] **Step 5: Commit**

```text
git add CMakeLists.txt cmake/TurboSCXMLConfig.cmake.in tests/install_consumer/CMakeLists.txt tests/install_consumer/ccxml_main.c tests/install_consumer/ccxml_main.cpp README.md
git commit -m "build(ccxml): export installed component"
```

### Task 4: Debug/ASan verification and final review

**Files:**
- Modify only files required by failures reproduced during verification.

**Interfaces:**
- Consumes: completed CCXML MVP.
- Produces: verified Release and Debug/ASan behavior with no known Critical or Important defects.

- [ ] **Step 1: Run Debug/ASan configure, build, and focused tests**

Use `win-dev-user` from the VS developer environment. Run `ctest --preset win-dev-user -R ccxml_`. Expected: both tests pass under the configured sanitizer.

- [ ] **Step 2: Run full Debug/ASan tests**

Run the full `win-dev-user` CTest preset. Expected: all tests pass without sanitizer reports.

- [ ] **Step 3: Review the public contract and mutation coverage**

Check that wrong event selection, missing prepare, missing commit/discard, reversed transaction terminalization, duplicate close, and premature destroy each cause at least one existing test to fail. Fix uncovered behavior with a new RED-GREEN cycle.

- [ ] **Step 4: Inspect the final diff and status**

Run `git diff origin/main...HEAD --check`, inspect `git diff --stat` and the complete diff, and confirm no generated build artifacts are tracked.

- [ ] **Step 5: Commit verification-only fixes if any**

```text
git add tests/ccxml_session_test.c
git commit -m "test(ccxml): lock transaction ordering contracts"
```
