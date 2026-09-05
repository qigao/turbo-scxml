# CCXML Dynamic String Expression Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add typed, staged CMeta evaluation for dynamic `<createcall dest="...">` expressions while preserving literal and legacy-adapter compatibility.

**Architecture:** Extend the size-versioned datamodel adapter with an owned compiled-string-expression handle. Compile one handle per dynamic action at session admission, evaluate it into a call-scoped borrowed string at dispatch, and route CMeta foreach expressions through the existing staged/committed supplemental scopes.

**Tech Stack:** C11, Salts CMeta expression VM, CFlow statechart effect tickets, TinyTest, CMake presets.

**Spec:** `docs/specs/ccxml-dynamic-string-expression-design.md`

## Global Constraints

- XPath is not used.
- `CCXML_DATAMODEL_ADAPTER_ABI_V1` remains `1`; new callbacks are an optional `struct_size`-gated tail.
- Literal-only programs remain compatible with old adapter prefixes and do not require expression callbacks.
- Dynamic results are borrowed only through the immediately following telephony prepare callback.
- Dynamic strings are nonempty, contain no embedded NUL, and are bounded by the configured CMeta `max_string_bytes`.
- Foreach evaluation reads staged scope while a transaction is pending and committed scope otherwise.
- The default Release build must not require QuickJS.

---

### Task 1: Classify and retain dynamic createcall destinations

**Files:**
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_program.c`
- Test: `tests/ccxml_program_test.c`

**Interfaces:**
- Consumes: existing `validate_string_literal(...)`, XML attribute views, and `ccxml_limits.max_name_bytes`.
- Produces: `ccxml_action_row.destination_is_dynamic` and `ccxml_program_impl.uses_string_expression`; literal rows retain the unquoted value, dynamic rows retain the complete expression source.

- [x] **Step 1: Replace the existing nonliteral-rejection test with a classification test**

  Compile `<createcall dest='destination'/>`, assert `CCXML_OK`, then inspect the internal row and assert `destination == "destination"`, `destination_size == 11`, `destination_is_dynamic == true`, and `program->uses_string_expression == true`. Extend the quoted-literal assertion with `destination_is_dynamic == false`.

- [x] **Step 2: Run the focused program test and verify RED**

  Run `ctest --preset win-release-user -R ccxml_program_test --output-on-failure` after rebuilding `ccxml_program_test`. Expected: the nonliteral case fails because compilation still returns `CCXML_UNSUPPORTED_FEATURE`.

- [x] **Step 3: Implement literal-or-expression validation and retention**

  Add a helper that recognizes matching outer quotes and delegates quoted input to `validate_string_literal`; accept an unquoted nonempty XML attribute as dynamic source. Account for `expression.size + 1` dynamic storage versus `expression.size - 1` literal storage, set both flags, and copy the appropriate byte range into immutable program storage.

- [x] **Step 4: Verify program tests are GREEN**

  Rebuild and run `ctest --preset win-release-user -R ccxml_program_test --output-on-failure`. Expected: PASS.

- [x] **Step 5: Commit the parser slice**

  ```powershell
  git add src/ccxml_internal.h src/ccxml_program.c tests/ccxml_program_test.c
  git commit -m "feat(ccxml): retain dynamic createcall destinations"
  ```

### Task 2: Add the size-versioned string-expression adapter tail

**Files:**
- Modify: `include/ccxml/ccxml.h`
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_session.c`
- Test: `tests/ccxml_session_test.c`

**Interfaces:**
- Consumes: `ccxml_program_impl.uses_string_expression` and each action's retained expression source.
- Produces: public `ccxml_string_expression`; adapter callbacks `compile_string_expression`, `evaluate_string_expression`, and `destroy_string_expression`; session-owned `ccxml_string_expression *action_string_expressions`.

- [x] **Step 1: Add adapter-contract tests**

  Add a probe adapter whose compile callback allocates a tiny handle, whose evaluate callback returns a borrowed configured string, and whose destroy callback counts and clears handles. Test that a dynamic-destination program rejects an adapter truncated at `offsetof(ccxml_datamodel_adapter_v1, compile_string_expression)`, accepts the complete tail, and destroys the compiled handle exactly once. Confirm the same truncated adapter still initializes a literal-only program.

- [x] **Step 2: Run the focused session test and verify RED**

  Rebuild and run `ctest --preset win-release-user -R ccxml_session_test --output-on-failure`. Expected: compilation fails because the public handle and callbacks do not exist yet.

- [x] **Step 3: Implement admission and exact-once cleanup**

  Append the public handle and callbacks, add `datamodel_string_expression_adapter_valid(...)`, copy the tail through the existing bounded adapter copy, allocate one zeroed handle per action only when needed, compile each dynamic action before publishing the session, and centralize destruction in `destroy_string_expressions(...)`. Map `SCXML_ADAPTER_FULL` to `CCXML_ALLOCATION_FAILED`, invalid contracts to `CCXML_INVALID_CONTRACT`, and other failures to `CCXML_ADAPTER_ERROR`.

- [x] **Step 4: Exercise allocation and partial-compile cleanup**

  Add two dynamic createcall actions and configure the second compile to fail. Assert initialization fails and the first handle is destroyed once while the failed output remains clear.

- [x] **Step 5: Verify session tests are GREEN**

  Rebuild and run `ctest --preset win-release-user -R ccxml_session_test --output-on-failure`. Expected: PASS.

- [x] **Step 6: Commit the adapter lifecycle slice**

  ```powershell
  git add include/ccxml/ccxml.h src/ccxml_internal.h src/ccxml_session.c tests/ccxml_session_test.c
  git commit -m "feat(ccxml): add string expression adapter lifecycle"
  ```

### Task 3: Evaluate dynamic destinations transactionally

**Files:**
- Modify: `src/ccxml_session.c`
- Test: `tests/ccxml_session_test.c`

**Interfaces:**
- Consumes: session-owned expression handles and `evaluate_string_expression(...)`.
- Produces: `evaluate_action_string_expression(...)`, returning a validated borrowed `ccxml_string_view` for the immediate provider prepare call.

- [x] **Step 1: Add successful dispatch and invalid-view tests**

  Configure the probe evaluator to return `tel:+12025550123`, dispatch the matching event, and assert the provider receives those exact bytes. Add separate cases returning an empty view and a byte range containing `\0`; assert `CCXML_INVALID_CONTRACT`, zero provider calls, and zero committed effects.

- [x] **Step 2: Run the focused session test and verify RED**

  Rebuild and run `ctest --preset win-release-user -R ccxml_session_test --output-on-failure`. Expected: the provider still receives the retained expression source instead of the evaluated value.

- [x] **Step 3: Implement immediate evaluation before provider prepare**

  In the createcall action branch, select the literal view by default. For a dynamic row, evaluate its compiled handle, require `SCXML_ADAPTER_ACCEPTED`, non-NULL/nonempty bytes, and no embedded NUL, then construct `ccxml_create_call_request` from that view. On failure call `discard_tickets(impl->tickets, prepared)` and return the mapped status so earlier transition effects do not commit.

- [x] **Step 4: Add provider-failure rollback coverage**

  Put an accepted effect before a dynamic createcall whose provider rejects. Assert the earlier effect is discarded exactly once and the evaluated destination was borrowed only for the provider call.

- [x] **Step 5: Verify session tests are GREEN**

  Rebuild and run `ctest --preset win-release-user -R ccxml_session_test --output-on-failure`. Expected: PASS.

- [x] **Step 6: Commit the dispatch slice**

  ```powershell
  git add src/ccxml_session.c tests/ccxml_session_test.c
  git commit -m "feat(ccxml): evaluate dynamic createcall destinations"
  ```

### Task 4: Implement typed CMeta and staged foreach evaluation

**Files:**
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_cmeta.c`
- Modify: `src/ccxml_session.c`
- Test: `tests/ccxml_cmeta_test.c`
- Test: `tests/ccxml_session_test.c`

**Interfaces:**
- Consumes: `scxml_expr_compile_value_with_scope_and_policy(...)`, `scxml_expr_program_value_kind(...)`, `scxml_expr_evaluate_value_with_system(...)`, and the session's staged/committed `scxml_scope_view`.
- Produces: `ccxml_cmeta_compile_string_expression_with_scope(...)` and `ccxml_cmeta_evaluate_string_expression_with_scope(...)`; the public CMeta adapter delegates to the same functions with a NULL supplemental scope.

- [x] **Step 1: Add root-field CMeta tests**

  Define a reflected root string field named `destination`, compile it through the public adapter tail, evaluate it, and assert the returned view matches the field. Add compile cases for an integer field and an unknown field; both must reject without publishing a handle.

- [x] **Step 2: Run the focused CMeta test and verify RED**

  Rebuild and run `ctest --preset win-release-user -R ccxml_cmeta_test --output-on-failure`. Expected: compilation fails because the CMeta string-expression callbacks are absent.

- [x] **Step 3: Implement the CMeta compiled handle**

  Store one `scxml_expr_program` in a private string-expression object. Compile with the datamodel's root, optional supplemental schema, an `_event.name`-only system-operand policy, `max_path_depth`, and `max_string_bytes`; require `SCXML_EXPR_VALUE_STRING`. Evaluate with `_event.name`, optional supplemental view, and the borrowed CMeta root state. Validate the resulting kind, size bound, nonempty data, and embedded-NUL rule before publishing the view. Destroy the expression program and clear the public handle exactly once.

- [x] **Step 4: Add a staged foreach integration test**

  Define a bounded reflected sequence whose element descriptor exposes a string member `destination`. Compile a program containing `<foreach array='calls' item='item'><createcall dest='item.destination'/></foreach>`, dispatch it, and assert provider requests follow element order. Make the second provider prepare fail and assert every earlier ticket is discarded and committed scope is restored.

- [x] **Step 5: Route scoped admission and evaluation**

  When `program->uses_foreach`, compile dynamic expressions with `&impl->foreach_scope`. During dispatch call the scoped CMeta evaluator with `foreach_scope_staged` while `foreach_transaction_pending` is true, otherwise `foreach_scope_committed`. Keep non-CMeta adapters on their public callbacks and preserve the existing rule that foreach requires the built-in CMeta adapter.

- [x] **Step 6: Verify CMeta and session tests are GREEN**

  Rebuild and run `ctest --preset win-release-user -R "ccxml_(cmeta|session)_test" --output-on-failure`. Expected: both PASS.

- [x] **Step 7: Commit the typed/staged CMeta slice**

  ```powershell
  git add src/ccxml_internal.h src/ccxml_cmeta.c src/ccxml_session.c tests/ccxml_cmeta_test.c tests/ccxml_session_test.c
  git commit -m "feat(ccxml): evaluate typed staged CMeta strings"
  ```

### Task 5: Verify compatibility and document the boundary

**Files:**
- Modify: `README.md`
- Modify: `docs/specs/ccxml-dynamic-string-expression-design.md`
- Modify: `docs/superpowers/plans/2026-09-05-ccxml-dynamic-string-expression.md`

**Interfaces:**
- Consumes: all completed dynamic-expression behavior.
- Produces: user-facing capability documentation and checked-off execution evidence.

- [x] **Step 1: Add the CCXML capability note**

  Document that CMeta-backed `<createcall dest="expression">` supports typed root and foreach-scope string paths, literal destinations remain backend-free, XPath is intentionally absent, and QuickJS support requires a future session-neutral adapter runtime.

- [x] **Step 2: Run formatting and warning-clean Release verification**

  Run `cmake --build --preset win-release-user`. Expected: the build succeeds without warnings introduced by this feature.

- [x] **Step 3: Run the complete Release suite**

  Run `ctest --preset win-release-user --output-on-failure`. Expected: all tests pass.

- [x] **Step 4: Inspect the final diff and ABI ordering**

  Run `git diff main...HEAD --check` and inspect `git diff main...HEAD -- include/ccxml/ccxml.h`. Expected: no whitespace errors; all pre-existing datamodel fields retain their order and the three new callbacks are last.

- [x] **Step 5: Commit documentation and plan evidence**

  ```powershell
  git add README.md docs/specs/ccxml-dynamic-string-expression-design.md docs/superpowers/plans/2026-09-05-ccxml-dynamic-string-expression.md
  git commit -m "docs(ccxml): describe dynamic string expressions"
  ```
