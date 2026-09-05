# CCXML Dynamic Conference Name Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Evaluate typed CMeta string expressions for optional `createconference@confname` while preserving bounded storage, ID writeback ordering, and transaction rollback.

**Architecture:** Reuse `decode_destination_value`, `ccxml_action_row.destination_is_dynamic`, and the per-session expression-handle array. Compile dynamic names during session admission, then evaluate immediately before provider prepare; the existing provider-ticket and datamodel-ticket sequence remains responsible for conference ID publication. Scoped CMeta evaluation supplies staged foreach values.

**Tech Stack:** C11, CCXML core, CMeta expression VM and descriptors, CFlow effect tickets, TinyTest, CMake presets.

**Spec:** `docs/specs/ccxml-dynamic-conference-name-design.md`

## Global Constraints

- Do not change public structures, ABI constants, or provider callbacks.
- Decode `confname` XML entities before literal/dynamic classification.
- Keep omitted and quoted literal names compatible with the existing datamodel write prefix.
- Compile one single-owner expression handle per dynamic action and destroy it exactly once.
- Evaluate the borrowed name only for the immediately following provider prepare callback.
- Preserve conference ID write-before-provider commit order and reverse rollback.
- Use staged supplemental CMeta scope inside foreach.
- Do not add XPath, QuickJS integration, coercion, concatenation, or dynamic conference IDs.

---

### Task 1: Admit dynamic conference names

**Files:**
- Modify: `tests/ccxml_program_test.c`
- Modify: `src/ccxml_program.c`

**Interfaces:**
- Consumes: `decode_destination_value(attribute, true, out_value, out_size, out_dynamic, diagnostic)`.
- Produces: createconference rows whose optional decoded name is retained in `destination`, with `destination_is_dynamic` and `uses_string_expression` set for dynamic sources.

- [x] **Step 1: Write failing compiler tests**

  Replace the nonliteral rejection case with one that compiles
  `<createconference conferenceid='conference.id' confname='conference.name'/>`
  and asserts the retained source and dynamic flags. Add an entity-decoding
  case using `conference&#46;name`.

- [x] **Step 2: Run the program test and verify RED**

  Run `ctest --preset win-release-user -R ccxml_program_test --output-on-failure`.
  Expected: both new cases fail with `CCXML_UNSUPPORTED_FEATURE` because
  `confname` still requires a literal.

- [x] **Step 3: Implement bounded classification and copying**

  In validation, call `decode_destination_value` for optional `confname`, free
  its temporary buffer on every path, and charge `decoded_size + 1`. In copy,
  decode again, populate the existing destination fields, propagate the
  dynamic bit, and aggregate `uses_string_expression`.

- [x] **Step 4: Rebuild and verify GREEN**

  Rebuild `ccxml_program_test`, rerun its CTest entry, and confirm all compiler
  tests pass.

- [x] **Step 5: Commit**

  Commit as `feat(ccxml): admit dynamic conference names`.

### Task 2: Evaluate names before conference creation

**Files:**
- Modify: `tests/ccxml_session_test.c`
- Modify: `src/ccxml_session.c`

**Interfaces:**
- Consumes: `evaluate_action_string_expression(impl, action_index, event, out_value)` and the generic per-action handle lifecycle.
- Produces: `prepare_create_conference` receives the evaluated borrowed name, followed by the unchanged provider-ID validation and datamodel writeback transaction.

- [x] **Step 1: Write failing session tests**

  Add cases proving a dynamic name requires the expression tail, compiles and
  destroys one handle, reaches the provider as its evaluated value, rejects
  empty and embedded-NUL results before provider prepare, and discards an
  earlier accepted ticket when expression evaluation fails.

- [x] **Step 2: Run the session test and verify RED**

  Run `ctest --preset win-release-user -R ccxml_session_test --output-on-failure`.
  Expected: provider-delivery and invalid-result assertions fail because the
  runtime still forwards retained expression source bytes.

- [x] **Step 3: Implement minimal runtime evaluation**

  Initialize a local conference-name view from the action row. For dynamic
  actions, evaluate it and discard earlier tickets on error. Construct
  `ccxml_create_conference_request` from the resulting view immediately before
  provider prepare; leave returned-ID validation, writeback prepare, ticket
  swap, and commit order unchanged.

- [x] **Step 4: Rebuild and verify GREEN**

  Rebuild `ccxml_session_test`, rerun its CTest entry, and confirm createcall,
  redirect, and createconference behavior remains green.

- [x] **Step 5: Commit**

  Commit as `feat(ccxml): evaluate dynamic conference names`.

### Task 3: Prove staged CMeta behavior and document the slice

**Files:**
- Modify: `tests/ccxml_cmeta_test.c`
- Modify: `README.md`
- Modify: `docs/specs/ccxml-createconference-design.md`
- Modify: `docs/specs/ccxml-dynamic-string-expression-design.md`
- Create: `docs/specs/ccxml-dynamic-conference-name-design.md`
- Create: `docs/superpowers/plans/2026-09-05-ccxml-dynamic-conference-name.md`

**Interfaces:**
- Consumes: the existing foreach record string descriptor, scoped expression compiler/evaluator, createconference provider ticket, and owned-string datamodel writeback.
- Produces: evidence that `confname='item.destination'` uses each staged iteration while returned conference IDs still commit through the root writable location.

- [x] **Step 1: Add the CMeta integration test**

  Extend the foreach test root with an owned conference-ID string and the
  provider probe with createconference support. Execute two record elements,
  assert provider names `support-a` then `support-b`, and assert the returned ID
  is present in the root after commit.

- [x] **Step 2: Run the CMeta integration test**

  Run `ctest --preset win-release-user -R ccxml_cmeta_test --output-on-failure`.
  Expected: the test passes after Tasks 1 and 2 expose conference names through
  the shared scoped pipeline.

- [x] **Step 3: Update documentation**

  Add dynamic `confname` to README and the general dynamic-string design,
  annotate the original createconference design with the additive extension,
  and keep all remaining conference options explicitly unsupported.

- [x] **Step 4: Run full verification**

  Run `cmake --build --preset win-release-user`,
  `ctest --preset win-release-user --output-on-failure`, and
  `git diff --check main...HEAD`. Expected: 15/15 tests pass and diff check has
  no output.

- [x] **Step 5: Commit**

  Commit tests and docs as `docs(ccxml): describe dynamic conference names`.
