# CMeta Environment Overrides Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement W3C-compatible CMeta environment overrides and promote W3C test 276 to executable PASS coverage without changing V1 behavior.

**Architecture:** A V2 session boundary validates borrowed override locations once, maps them to the exact compiled root data-assignment span, and stores only owned immutable assignment indices. Early and late initialization share the same skip predicate while the native StateChart instance remains the sole owner of mutable state.

**Tech Stack:** C11, Turbo::CMeta, Turbo::CFlow StateChart, TinyTest, CMake Presets.

**Spec:** `docs/specs/scxml-cmeta-environment-overrides-design.md`

## Global Constraints

- Preserve `scxml_session_init_cmeta` V1 source and runtime behavior.
- Use test-first RED/GREEN cycles; do not add placeholder implementations.
- Validate all borrowed views before CFlow instance attachment.
- Keep override storage bounded by the compiled root data declaration count.
- Run the smallest affected test target before broader Debug/Release checks.

---

## Task 1: Add the V2 public contract and early-binding semantics

- [x] Add a focused TinyTest that references the desired V2 API and proves V1
  still applies a document default while V2 preserves an explicit root value.
- [x] Build the focused test target and record the expected RED failure.
- [x] Add V2 public types and documentation to `include/scxml/scxml.h`.
- [x] Track root data initializer count through analysis and program emission.
- [x] Add private assignment destination matching and session override
  validation/storage.
- [x] Skip validated assignments during early initialization.
- [x] Rebuild and run the focused test to GREEN.

## Task 2: Reject invalid environment contracts

- [x] Add RED tests for NULL/empty, unknown, nested/non-root, duplicate, and
  excessive override rows.
- [x] Implement fail-fast validation and atomic cleanup using existing instance
  status results.
- [x] Run the focused contract tests to GREEN.

## Task 3: Preserve overrides with late binding

- [x] Add a RED test using `binding="late"` and a root data default.
- [x] Make the late initializer transaction use the shared override predicate.
- [x] Run early, late, and V1 regression tests to GREEN.

## Task 4: Promote W3C test 276

- [x] Add the local executable fixture and a V2-aware W3C harness option that
  models the parent-supplied child environment.
- [x] Run test 276 before changing the manifest and record RED/unsupported state.
- [x] Mark test 276 PASS and update conformance counts/provenance in tests and
  README documentation.
- [x] Run the W3C conformance target to GREEN.

## Task 5: Verify and commit

- [x] Run focused CMeta and W3C tests.
- [x] Configure/build/test Debug and Release presets.
- [x] Review the diff, public documentation, generated/local artifacts, and git
  status; ensure `.codegraph/` is not committed.
- [x] Commit the verified implementation on
  `feat/w3c-data-environment-overrides`.
