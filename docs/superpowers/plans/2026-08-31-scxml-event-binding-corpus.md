# SCXML Event Binding Corpus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an exact CMeta `_event` binding query and promote W3C-derived tests 318, 319, 339, and 396 to executable mandatory coverage.

**Architecture:** Keep `scxml_session_impl.system_values` as the sole current-Event fact source. Add one parser-recognized Boolean operand, `isBound(_event)`, that reads the existing event-name binding witness; use the existing strict CMeta W3C harness for all four transformed fixtures.

**Tech Stack:** C11, TurboUtils CMeta/CFlow/QueryVM, cxml, TinyTest, CMake Presets.

**Spec:** `docs/specs/scxml-event-binding-design.md`

## Global Constraints

- Work only on `turbo-scxml/main`; TurboUtils remains an installed dependency.
- Preserve existing public ABI, event ownership, queue ordering, and fail-fast diagnostics.
- Do not equate an unbound `_event` with a bound Event whose optional fields are empty.
- `tests/w3c/manifest.tsv` remains the corpus fact source.
- Use Release presets for focused and full verification on Windows.

---

### Task 1: Add the `_event` binding query

**Files:**
- Modify: `tests/scxml_expr_test.c`
- Modify: `src/scxml_expr.c`
- Modify: `docs/specs/scxml-event-binding-design.md`

**Interfaces:**
- Consumes: `scxml_expr_system_values.event_name` as the existing binding witness.
- Produces: the additive CMeta condition syntax `isBound(_event) -> bool`.

- [x] **Step 1: Write the failing expression test**

  Add a TinyTest case that compiles `isBound(_event)`, evaluates it against
  `{0}` and `{.event_name = {"go", 2u}}`, and expects `false` then `true`. A
  third evaluation with `system_values == NULL` must return
  `SCXML_EXPR_EVALUATION_ERROR` without changing the output Boolean.

- [x] **Step 2: Run the focused test and verify RED**

  Run:

  ```powershell
  cmake --build --preset win-release-user --target scxml_expr_test
  build\Msvc-Release\tests\scxml_expr_test.exe --filter "reports whether the current event is bound"
  ```

  Expected: the new test fails because `isBound` is currently resolved as an
  unknown CMeta location.

- [x] **Step 3: Implement the minimal parser and resolver operand**

  Add `EXPR_OPERAND_SYSTEM_EVENT_BOUND`. Parse only the exact token sequence
  `isBound ( _event )`, emit a Boolean `QVM_OP_LOAD_CONST`, and resolve it as:

  ```c
  if (context->system_values == NULL) {
      context->failed = true;
      return 0;
  }
  make_value(out, EXPR_VALUE_BOOL);
  out->boolean = context->system_values->event_name.data != NULL;
  return 1;
  ```

- [x] **Step 4: Verify GREEN and malformed syntax**

  Extend the invalid-expression table with `isBound()`,
  `isBound(_event.name)`, and `isBound(other)`, each expecting
  `SCXML_EXPR_SYNTAX_ERROR`, then run the complete `scxml_expr_test` target.

### Task 2: Promote four mandatory Event-binding assertions

**Files:**
- Create: `tests/w3c/test318.scxml`
- Create: `tests/w3c/test319.scxml`
- Create: `tests/w3c/test339.scxml`
- Create: `tests/w3c/test396.scxml`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`

**Interfaces:**
- Consumes: `run_w3c_cmeta_fixture()` and the existing `result.pass` Event I/O probe.
- Produces: four executable PASS rows with synchronized provenance documentation.

- [x] **Step 1: Add exact transformed fixtures and test cases**

  Use `datamodel="cmeta"`. Tests 318, 339, and 396 compare `_event` fields;
  test 319 branches on `isBound(_event)`. Every fixture ends in `pass` or
  `fail`, and each final state sends its matching `result.*` Event.

- [x] **Step 2: Run the four focused corpus cases**

  Run:

  ```powershell
  cmake --build --preset win-release-user --target scxml_w3c_conformance_test
  build\Msvc-Release\tests\scxml_w3c_conformance_test.exe --filter "test 318"
  build\Msvc-Release\tests\scxml_w3c_conformance_test.exe --filter "test 319"
  build\Msvc-Release\tests\scxml_w3c_conformance_test.exe --filter "test 339"
  build\Msvc-Release\tests\scxml_w3c_conformance_test.exe --filter "test 396"
  ```

  Expected: all four reach the strict `result.pass` observation once.

- [x] **Step 3: Update the corpus fact source and provenance**

  Change the four manifest rows from `UNSUPPORTED/NOT_RUN/NONE` to
  `PASS/TERMINAL_PASS` with concrete transformation and preserved-assertion
  rationale. Add the four upstream links and assertions to the README table.
  Update aggregate counts to 46 PASS and 122 UNSUPPORTED and replace stale
  repository-owner wording with TurboSCXML.

- [x] **Step 4: Run manifest validation and the complete corpus executable**

  Run `scxml_w3c_conformance_test.exe` without a filter. Expected: the inventory
  remains 202 documents, with 168 mandatory, 34 optional, 46 PASS, 122
  UNSUPPORTED, and 34 N/A.

### Task 3: Verify and commit the batch

**Files:**
- Modify: `docs/superpowers/plans/2026-08-31-scxml-event-binding-corpus.md`

**Interfaces:**
- Consumes: all changes from Tasks 1 and 2.
- Produces: one locally committed, reproducible `turbo-scxml/main` milestone.

- [x] **Step 1: Reconfigure and run the full Release suite**

  Run through the configured Visual Studio environment:

  ```powershell
  cmake --fresh --preset win-release-user
  cmake --build --preset win-release-user
  ctest --preset win-release-user --output-on-failure
  ```

  Expected: all registered tests pass.

- [x] **Step 2: Check repository integrity**

  Run `git diff --check`, verify `.codegraph/` is untracked/ignored and absent
  from the staged set, and inspect the complete staged diff.

- [x] **Step 3: Mark this plan complete and commit**

  Mark every checkbox `[x]`, then commit the implementation, fixtures,
  manifest, design, and plan with message:

  ```text
  feat(scxml): cover current event binding corpus
  ```
