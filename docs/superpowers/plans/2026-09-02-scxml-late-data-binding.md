# SCXML CMeta Late Data Binding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make CMeta locations declared under `binding="late"` unbound until their owning state begins first entry, then promote W3C test 280 without weakening test 307.

**Architecture:** Compile immutable declaration-to-storage descriptors, derive bound state from the existing late-initializer phase, and enforce it through one private expression/assignment lookup callback. Keep CFlow staged state as the only mutable fact source.

**Tech Stack:** C11, Turbo::CMeta, Turbo::CFlow StateChart, TinyTest, CMake Presets.

**Spec:** `docs/specs/scxml-late-data-binding-design.md`

## Global Constraints

- Preserve public API/ABI and behavior for undeclared host fields and early binding.
- Keep `late_initializers[]` as the only mutable binding fact source.
- Bound all descriptor counts and range arithmetic at compile time.
- Reuse existing evaluation errors and `error.execution` propagation.
- Develop test-first and verify both Windows Debug and Release presets.

### Task 1: Specify and test late-unbound behavior

**Files:**
- Modify: `tests/scxml_cmeta_test.c`

- [x] Add a TinyTest case that reads a late declaration before state entry and requires `error.execution`.
- [x] Require the same declaration to be readable with its initialized value from the declaring state's first `onentry`.
- [x] Add coverage proving an environment override binds the declaration immediately.
- [x] Run the focused test and record the current incorrect pre-entry read as RED.

### Task 2: Compile immutable binding descriptors

**Files:**
- Modify: `src/scxml_assign.h`
- Modify: `src/scxml_assign.c`
- Modify: `src/scxml_impl.h`
- Modify: `src/scxml_emit.c`
- Modify: `src/scxml_program.c`

- [x] Expose a private validated assignment-destination byte range query.
- [x] Allocate exactly one binding descriptor per emitted `<data>` assignment.
- [x] Associate late descriptors with their existing late-initializer group; mark early descriptors without a group.
- [x] Validate descriptor counts, destination bounds, and checked range arithmetic.

### Task 3: Enforce one runtime binding boundary

**Files:**
- Modify: `src/scxml_expr.h`
- Modify: `src/scxml_expr.c`
- Modify: `src/scxml_assign.h`
- Modify: `src/scxml_assign.c`
- Modify: `src/scxml_session.c`
- Modify: `src/scxml_runtime.c`

- [x] Add a private data-bound callback to expression runtime context.
- [x] Derive binding from descriptor overlap, environment overrides, and `late_initializers[]` phase.
- [x] Reject unbound expression reads before accessing host storage.
- [x] Reject ordinary assignment to an unbound destination while preserving initializer application.
- [x] Verify entry rollback returns a pending declaration to unbound through existing phase settlement.
- [x] Run focused and adjacent CMeta tests to GREEN.

### Task 4: Promote W3C test 280 and preserve the test 307 boundary

**Files:**
- Create: `tests/w3c/test280.scxml`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`

- [x] Translate upstream test 280 to the bounded CMeta schema while preserving both pre-entry failure and onentry availability.
- [x] Register the fixture and verify it independently before changing its manifest status.
- [x] Keep test 307 unsupported with the exact static-schema limitation documented.
- [x] Promote test 280 to `PASS/TERMINAL_PASS` and update corpus counts from 159/9 to 160/8.
- [x] Run the focused fixture and the full strict corpus executable.

### Task 5: Verify and review

- [x] Run `codegraph affected` for every modified production file and inspect affected callers.
- [x] Run fresh Debug configure, build, and CTest.
- [x] Run fresh Release configure, build, and CTest.
- [x] Run `git diff --check`, inspect status, and review public API/ABI and state-source invariants.

### Task 6: Close review findings

- [x] Keep donedata projection destinations independent from session binding offsets.
- [x] Route foreach, send/invoke `idlocation`, and structured CMeta content through one private binding check.
- [x] Reject invalid mutable data descriptor ranges during emission while explicitly preserving read-only system locations.
- [x] Add regression coverage for every affected runtime path.
