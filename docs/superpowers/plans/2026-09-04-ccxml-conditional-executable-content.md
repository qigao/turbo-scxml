# CCXML Conditional Executable Content Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Support bounded CCXML `<if>`, `<elseif>`, and `<else>` executable content using the existing CMeta condition adapter.

**Architecture:** Lower each conditional block into immutable control rows plus ordinary action rows. A CCXML session compiles every control-row condition during admission and evaluates it while walking the selected transition's rows; only the selected branch reaches the existing transactional action preparation path. Nested blocks use preallocated session frames bounded by the compiled transition action limit.

**Tech Stack:** C11, Salts XmlParser, CMeta-backed `ccxml_datamodel_adapter_v1`, CFlow effect tickets, TinyTest, CMake presets.

**Spec:** `docs/specs/ccxml-core-mvp-design.md`

## Global Constraints

- Reuse the version-1 optional condition adapter tail; do not add XPath, JavaScript, or a second expression runtime.
- Programs remain immutable and source-owned; sessions compile and own opaque condition handles.
- Every parsed action and control row counts toward `max_actions`; only leaf actions count toward effect-ticket capacity.
- Control-flow execution is bounded by the program's maximum transition actions and performs no allocation during dispatch.
- Any condition compile/evaluate error aborts admission or dispatch before later external effects can commit.
- Follow RED-GREEN-REFACTOR with TinyTest and do not commit without explicit user direction.

---

### Task 1: Admit and lower conditional CCXML syntax

**Files:**
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_program.c`
- Test: `tests/ccxml_program_test.c`

**Interfaces:**
- Consumes: a transition's XML executable-content children and `ccxml_limits.max_actions`.
- Produces: `CCXML_ACTION_IF`, `CCXML_ACTION_ELSEIF`, `CCXML_ACTION_ELSE`, `CCXML_ACTION_ENDIF`, decoded control conditions, and branch/end indices in `ccxml_action_row`.

- [ ] **Step 1: Write failing compiler tests**

```c
it("lowers an if elseif else action chain") {
    const char *source =
        "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
        "<eventprocessor><transition event='connection.alerting'>"
        "<if cond='first'><accept/><elseif cond='second'/><reject/>"
        "<else/><disconnect/></if></transition></eventprocessor></ccxml>";
    ccxml_program program = {0};
    check_equal(compile_document(&program, source), CCXML_OK);
    check_equal(ccxml_program_action_count(&program), (size_t)7);
    ccxml_program_destroy(&program);
}
```

Add separate cases that reject a missing `if@cond`, top-level `<elseif>`, content inside `<else>`, and an `<elseif>` following `<else>`.

- [ ] **Step 2: Run the focused compiler test and verify RED**

Run: `cmake --build --preset win-dev-user --target ccxml_program_test; ctest --preset win-dev-user -R "^ccxml_program_test$" --output-on-failure`

Expected: the valid document fails with `CCXML_UNSUPPORTED_FEATURE` because `<if>` is not currently executable content.

- [ ] **Step 3: Implement minimal bounded lowering**

Add control action kinds and the two absolute row indices required for the next branch and block end. Recursively validate branch content, decode each nonempty `cond`, charge all control rows and decoded bytes during measurement, and recursively emit an ordered flattened row sequence. Preserve existing leaf-action validators and their effect accounting.

- [ ] **Step 4: Re-run the focused compiler test and verify GREEN**

Run the Task 1 command. Expected: valid lowering has seven rows and all malformed branch structures are rejected.

### Task 2: Compile and destroy control-row conditions

**Files:**
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_session.c`
- Test: `tests/ccxml_session_test.c`

**Interfaces:**
- Consumes: control row condition source and `ccxml_datamodel_adapter_v1.compile_condition`.
- Produces: one session-owned `ccxml_condition` per conditional control row, destroyed on every session failure or destruction path.

- [ ] **Step 1: Write the failing admission-lifecycle test**

```c
it("compiles and destroys every conditional action condition") {
    datamodel_probe datamodel = {0};
    ccxml_program program = {0};
    ccxml_session session = {0};
    check_equal(compile_document(&program, conditional_source), CCXML_OK);
    check_equal(init_session_with_datamodel(&session, &program, &provider,
                                            &datamodel), CCXML_OK);
    check_equal(datamodel.condition_compile_count, (size_t)2);
    check_equal(ccxml_session_destroy(&session), CCXML_OK);
    check_equal(datamodel.condition_destroy_count, (size_t)2);
    ccxml_program_destroy(&program);
}
```

Also prove that a second action-condition compile failure destroys the first handle and leaves the session unpublished.

- [ ] **Step 2: Run the focused session test and verify RED**

Run: `cmake --build --preset win-dev-user --target ccxml_session_test; ctest --preset win-dev-user -R "^ccxml_session_test$" --output-on-failure`

Expected: action conditions are not compiled, so the compile-count assertion fails.

- [ ] **Step 3: Implement session-owned control handles**

Allocate a zeroed handle array indexed by program action row when conditional controls exist. Compile only `IF` and `ELSEIF` sources during `ccxml_session_init`; extend all cleanup paths and `destroy_conditions` to destroy initialized action handles exactly once.

- [ ] **Step 4: Re-run the focused session test and verify GREEN**

Run the Task 2 command. Expected: successful and failed admission both report exact compile/destroy counts.

### Task 3: Evaluate branches before effect preparation

**Files:**
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_session.c`
- Test: `tests/ccxml_session_test.c`

**Interfaces:**
- Consumes: flattened control rows, session-owned conditions, and the selected event.
- Produces: branch selection that invokes the existing leaf action executor only for the selected branch.

- [ ] **Step 1: Write failing dispatch tests**

```c
it("prepares only the true if branch") {
    datamodel_probe datamodel = {.condition_result = true};
    check_equal(dispatch_conditional_source(&datamodel, &provider), CCXML_OK);
    check_equal(provider.accept_prepare_count, (size_t)1);
    check_equal(provider.reject_prepare_count, (size_t)0);
    check_equal(provider.disconnect_prepare_count, (size_t)0);
}

it("uses else after false if and elseif") {
    datamodel_probe datamodel = {.condition_result = false};
    check_equal(dispatch_conditional_source(&datamodel, &provider), CCXML_OK);
    check_equal(provider.disconnect_prepare_count, (size_t)1);
}
```

Add a nested test with per-condition probe results proving that a false inner condition selects its inner else, and an evaluation-error test proving that no later leaf effect is prepared.

- [ ] **Step 2: Run the focused session test and verify RED**

Run the Task 2 command. Expected: conditional actions are unsupported or all branches are incorrectly treated as executable leaf rows.

- [ ] **Step 3: Implement bounded control execution**

Add a preallocated per-session conditional-frame scratch array. During transition action walking, evaluate `IF`/`ELSEIF` with the adapter, jump to the next marker after false, skip remaining branches after true, and pop at the matching `ENDIF`. Reject malformed compiled control flow with `CCXML_INVALID_CONTRACT`. Keep ticket discard/commit ordering unchanged.

- [ ] **Step 4: Re-run the focused session test and verify GREEN**

Run the Task 2 command. Expected: exactly one branch's provider effect is prepared and condition evaluation failures leave no prepared effect.

### Task 4: Refactor, document, and verify

**Files:**
- Modify: `docs/specs/ccxml-core-mvp-design.md`
- Modify: `README.md`
- Modify only as required by verification: `src/ccxml_program.c`, `src/ccxml_session.c`, `tests/ccxml_program_test.c`, `tests/ccxml_session_test.c`

- [ ] **Step 1: Document the accepted conditional profile**

Document direct-child `<if cond>`, marker-form `<elseif cond/>` and `<else/>`, nesting, CMeta expression ownership, transactional error behavior, and that XPath/general ECMAScript remain unsupported.

- [ ] **Step 2: Run focused and complete CCXML verification**

Run:

```powershell
cmake --build --preset win-dev-user --target ccxml_program_test ccxml_session_test ccxml_cmeta_test ccxml_send_test
ctest --preset win-dev-user -R "^ccxml_" --output-on-failure
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
git diff --check
```

Expected: all CCXML and full Release tests pass with no whitespace errors.

- [ ] **Step 3: Inspect the final scoped diff**

Run: `git diff -- src/ccxml_internal.h src/ccxml_program.c src/ccxml_session.c tests/ccxml_program_test.c tests/ccxml_session_test.c docs/specs/ccxml-core-mvp-design.md README.md`

Expected: no XPath dependency, no dispatch-time allocation, and no unrelated changes.
