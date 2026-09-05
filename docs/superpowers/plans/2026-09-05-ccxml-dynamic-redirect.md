# CCXML Dynamic Redirect Destination Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Evaluate typed CMeta string expressions for `redirect@dest` through the existing bounded CCXML datamodel boundary.

**Architecture:** Reuse the action-level `destination_is_dynamic` flag and per-session string-expression handle array introduced for createcall. Extend redirect parsing to admit dynamic sources, then evaluate the action handle only after validating the current connection and immediately before telephony prepare. The existing scoped compiler/evaluator supplies root, `_event.name`, and staged foreach behavior.

**Tech Stack:** C11, CCXML core, CMeta descriptors and expression VM, CFlow effect tickets, TinyTest, CMake presets.

**Spec:** `docs/specs/ccxml-dynamic-redirect-design.md`

## Global Constraints

- Do not change the public ABI or adapter version constants.
- Decode XML entities before literal/dynamic classification.
- Preserve literal redirect compatibility with datamodel adapters that omit the string-expression tail.
- Validate the current Event connection before expression evaluation.
- Evaluate borrowed strings only for the immediately following provider prepare callback.
- Preserve document-order commit and reverse-order rollback, including staged foreach rollback.
- Do not introduce XPath, QuickJS, concatenation, coercion, or new redirect options.

---

### Task 1: Admit dynamic redirect destinations

**Files:**
- Modify: `tests/ccxml_program_test.c`
- Modify: `src/ccxml_program.c`

**Interfaces:**
- Consumes: `decode_destination_value(attribute, allow_dynamic, ...)` and `ccxml_action_row.destination_is_dynamic`.
- Produces: redirect action rows whose decoded unquoted destination source is retained with `destination_is_dynamic == true`, and programs whose `uses_string_expression` flag is set.

- [x] **Step 1: Write the failing compiler tests**

  Replace the old nonliteral-rejection case with cases that compile
  `<redirect dest='route.destination'/>` and an XML-decoded equivalent such as
  `<redirect dest='route&#46;destination'/>`, while keeping empty and malformed
  literal rejection coverage.

- [x] **Step 2: Run the focused test and verify RED**

  Run `build/Msvc-Release/tests/ccxml_program_test.exe --filter "redirect"`.
  Expected: the two dynamic cases fail because redirect still passes
  `allow_dynamic = false`.

- [x] **Step 3: Implement the minimal compiler change**

  Pass `true` for both createcall and redirect in the validation and copy
  calls to `decode_destination_value`. Retain the returned dynamic bit through
  the existing row field and `uses_string_expression` aggregation.

- [x] **Step 4: Run the focused test and verify GREEN**

  Rebuild `ccxml_program_test`, rerun the redirect filter, and confirm all
  redirect compiler cases pass.

- [x] **Step 5: Commit**

  Commit as `feat(ccxml): admit dynamic redirect destinations`.

### Task 2: Evaluate redirect expressions transactionally

**Files:**
- Modify: `tests/ccxml_session_test.c`
- Modify: `src/ccxml_session.c`

**Interfaces:**
- Consumes: `evaluate_action_string_expression(impl, action_index, event, out_value)` and the action-indexed compiled handle lifecycle.
- Produces: `prepare_redirect` receives the evaluated borrowed destination after current-connection validation; expression failure rolls back earlier tickets.

- [x] **Step 1: Write failing session tests**

  Add redirect cases proving a dynamic action requires the expression adapter
  tail, compiles and destroys one handle, evaluates once before provider
  prepare, does not evaluate when the current connection is invalid, rejects
  empty/NUL results, and discards an earlier accepted ticket on evaluation
  failure.

- [x] **Step 2: Run the focused test and verify RED**

  Run `build/Msvc-Release/tests/ccxml_session_test.exe --filter "redirect"`.
  Expected: dynamic redirect initialization or dispatch assertions fail because
  runtime still passes program-owned expression source bytes to the provider.

- [x] **Step 3: Implement minimal redirect evaluation**

  In the redirect action branch, call `connection_id_valid(event)` first.
  Initialize a local `ccxml_string_view` from the literal row, replace it via
  `evaluate_action_string_expression` when `destination_is_dynamic`, discard
  earlier tickets on failure, then build `ccxml_redirect_request` from that
  view and call the provider.

- [x] **Step 4: Run focused and neighboring tests**

  Rebuild `ccxml_session_test`; run its redirect and createcall filters. Both
  groups must pass with no TinyTest framework errors.

- [x] **Step 5: Commit**

  Commit as `feat(ccxml): evaluate dynamic redirect destinations`.

### Task 3: Prove typed staged CMeta behavior and document the slice

**Files:**
- Modify: `tests/ccxml_cmeta_test.c`
- Modify: `README.md`
- Modify: `docs/specs/ccxml-dynamic-string-expression-design.md`
- Create: `docs/specs/ccxml-dynamic-redirect-design.md`
- Create: `docs/superpowers/plans/2026-09-05-ccxml-dynamic-redirect.md`

**Interfaces:**
- Consumes: built-in CMeta scoped compile/evaluate functions and the existing `foreach_record.destination` semantic descriptor.
- Produces: evidence that `<redirect dest='item.destination'/>` reads each staged typed iteration without changing the application root.

- [x] **Step 1: Write the CMeta integration test**

  Add a foreach test with two record elements and
  `<redirect dest='item.destination'/>`; dispatch an Event with a valid current
  connection and assert two provider calls receive `tel:111` then `tel:222`.

- [x] **Step 2: Run the focused integration test**

  Run `build/Msvc-Release/tests/ccxml_cmeta_test.exe --filter "redirect"`.
  Expected: the test passes after Tasks 1 and 2 expose dynamic redirect through
  the shared scoped pipeline.

- [x] **Step 3: Complete documentation**

  Update README and the general dynamic-string design to list redirect among
  supported consumers and remove it from the non-goal list. Keep all remaining
  string-valued attributes explicitly out of scope.

- [x] **Step 4: Run full verification**

  Run `cmake --build --preset win-release-user`,
  `ctest --preset win-release-user --output-on-failure`, and
  `git diff --check main...HEAD`. Expected: 15/15 tests pass and diff check has
  no output.

- [x] **Step 5: Commit**

  Commit tests and docs as `docs(ccxml): describe dynamic redirect destinations`.
