# CCXML CMeta Transition Conditions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded `transition@cond` support whose boolean expressions are compiled and evaluated by the session's CMeta datamodel adapter.

**Architecture:** Retain condition source bytes in the immutable CCXML program, then compile one adapter-owned opaque condition handle per transition during session admission. CFlow guards combine event matching, optional `state` matching, and optional condition evaluation in document order; evaluation failures stop guard selection for that event. The built-in CMeta adapter reuses `scxml_expr` and owns each compiled expression until session destruction.

**Tech Stack:** C11, Turbo XML parser, CMeta data descriptors, TurboSCXML expression VM, CFlow Statechart, TinyTest, CMake presets.

**Spec:** `docs/specs/ccxml-core-mvp-design.md`

## Global Constraints

- Keep `ccxml_program` immutable and independently owned after XML input destruction.
- Preserve version-1 datamodel adapters by appending optional operations and checking `struct_size` only for programs that use conditions.
- Compile conditions once per session; do not parse or allocate in a dispatch guard.
- Condition evaluation is synchronous and side-effect free; no callback may retain event or source pointers.
- Preserve first-match document order and stop later guards after an adapter failure.
- Keep general ECMAScript outside this bounded profile.
- Do not commit; the user controls repository integration.

---

### Task 1: Program syntax and retained condition rows

**Files:**
- Modify: `CMakeLists.txt`
- Create: `src/scxml_xml_decode.h`
- Create: `src/scxml_xml_decode.c`
- Modify: `src/scxml_emit.c`
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_program.c`
- Test: `tests/ccxml_program_test.c`

**Interfaces:**
- Consumes: parsed `transition` attributes and `ccxml_limits.max_name_bytes`.
- Produces: `ccxml_transition_row.condition`, `condition_size`, and `ccxml_program_impl.uses_condition`.

- [x] **Step 1: Write the failing syntax test**

Add a TinyTest case compiling a transition with `cond='mode == &quot;active&quot;'`; assert `CCXML_OK`. Add an empty-condition case and assert `CCXML_INVALID_STRUCTURE`.

- [x] **Step 2: Run the focused program test and verify RED**

Run `cmake --build --preset win-dev-user --target ccxml_program_test && ctest --preset win-dev-user -R ccxml_program_test --output-on-failure`. The positive condition document must fail because `cond` is currently an unsupported transition attribute.

- [x] **Step 3: Implement bounded syntax retention**

Extract the existing SCXML XML-entity decoder into a private allocation-free helper, preserving SCXML behavior. Admit one unqualified `cond`, require a nonempty decoded value, charge `decoded_size + 1` against retained bytes, decode it into program storage, set `uses_condition`, and mark the lowered CFlow guard `CMETA_EFFECT_MAY_FAIL`.

- [x] **Step 4: Re-run the focused program test and verify GREEN**

Run the same build and CTest filter; both condition syntax cases must pass.

### Task 2: Adapter lifecycle and CMeta expression execution

**Files:**
- Modify: `include/ccxml/ccxml.h`
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_session.c`
- Modify: `src/ccxml_cmeta.c`
- Test: `tests/ccxml_cmeta_test.c`
- Test: `tests/ccxml_session_test.c`

**Interfaces:**
- Consumes: retained condition source, CMeta root descriptor/state, and the current `ccxml_event`.
- Produces: optional `compile_condition`, `evaluate_condition`, and `destroy_condition` adapter operations plus session-owned `ccxml_condition` handles.

- [x] **Step 1: Write the failing end-to-end condition test**

Use a real `ccxml_cmeta_datamodel`: the first dispatch must skip `mode == "active"`, select `mode == "idle"`, and assign `active`; the second dispatch must select the first transition and exit.

- [x] **Step 2: Run the focused CMeta test and verify RED**

Build and run `ccxml_cmeta_test`; it must fail because condition syntax/runtime support is absent.

- [x] **Step 3: Add the versioned adapter tail and session ownership**

Define opaque `ccxml_condition`, append compile/evaluate/destroy callbacks, validate the full tail only when `uses_condition`, allocate one zeroed handle per transition binding, compile before publishing the session, and destroy all initialized handles on every later failure and successful session destruction.

- [x] **Step 4: Implement the built-in CMeta callbacks**

Compile with `scxml_expr_compile`, the adapter's root schema/path/string bounds, and callbacks that reject SCXML `In()` state references. Evaluate with the borrowed root state and current event system values; map allocation pressure to `SCXML_ADAPTER_FULL`, contract misuse to `SCXML_ADAPTER_INVALID_CONTRACT`, and expression errors to `SCXML_ADAPTER_ERROR_EXECUTION`.

- [x] **Step 5: Combine condition evaluation with the CFlow guard**

Evaluate only after event and optional state match. False continues document-order selection; adapter rejection maps to `CCXML_ADAPTER_ERROR` and prevents subsequent guards.

- [x] **Step 6: Verify GREEN and admission cleanup**

Run `ccxml_cmeta_test` and `ccxml_session_test`. Add cases proving malformed CMeta syntax rejects session initialization without committing the root initializer, and a legacy/truncated adapter rejects condition programs without callbacks.

### Task 3: Documentation and verification

**Files:**
- Modify: `README.md`
- Modify: `docs/specs/ccxml-core-mvp-design.md`

**Interfaces:**
- Consumes: verified compiler, adapter, and runtime behavior.
- Produces: documented bounded condition grammar/lifecycle and an updated completion checklist.

- [x] **Step 1: Document the condition contract**

Describe deferred adapter compilation, CMeta boolean syntax, first-match behavior, runtime error mapping, and the fact that this is not general ECMAScript.

- [x] **Step 2: Run static and focused verification**

Run `git diff --check`, scan for focused/debug tests, build the three CCXML targets with `win-dev-user`, and run `ctest --preset win-dev-user -R ccxml_ --output-on-failure`.

- [x] **Step 3: Run the full Release suite**

Run `cmake --build --preset win-release-user && ctest --preset win-release-user --output-on-failure`; require zero failed tests.
