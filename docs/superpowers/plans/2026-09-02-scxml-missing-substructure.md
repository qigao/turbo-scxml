# SCXML Missing Substructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give unbound late data and absent loaded substructure the same execution-time expression behavior.

**Architecture:** A runtime path policy retains only syntactically valid unresolved data operands. Evaluation maps both missing cases to `SCXML_EXPR_UNKNOWN_LOCATION`, which existing executable boundaries convert to `error.execution`.

**Tech Stack:** C11, QueryVM, CMeta descriptors, TinyTest.

**Spec:** `docs/specs/scxml-remaining-conformance-design.md`

## Global Constraints

- Invalid descriptor graphs and malformed paths still fail compilation.
- No undefined/null/default value is fabricated.
- Guard failure remains false plus one internal `error.execution`.

---

### Task 1: Runtime unresolved operands

**Files:** `src/scxml_expr.h`, `src/scxml_expr.c`, `src/scxml_emit.c`, `tests/scxml_expr_test.c`, `tests/scxml_cmeta_test.c`

- [x] Add paired failing tests that evaluate a not-yet-bound late path and an absent child path through executable content and observe identical error transitions.
- [x] Verify RED: the absent child path currently fails compile while the late path reaches runtime.
- [x] Add an explicit compile policy and unresolved operand carrying the validated source path; keep known root/supplemental operands on their typed lookup paths and report the retained missing operand at evaluation.
- [x] Assert both return `SCXML_EXPR_UNKNOWN_LOCATION` and enqueue exactly one `error.execution`.
- [x] Run expression, assignment, foreach, CMeta, and strict W3C regressions.

### Task 2: W3C 307 promotion

**Files:** `tests/w3c/test307.scxml`, `tests/scxml_w3c_conformance_test.c`, `tests/w3c/manifest.tsv`, `tests/w3c/README.md`

- [x] Rewrite the manual log comparison into two branches that record whether each access raises `error.execution` and pass only when outcomes match.
- [x] Verify the fixture is terminal and deterministic.
- [x] Promote row 307 to PASS/TERMINAL_PASS with the rewrite rationale.
- [x] Run strict W3C plus Debug/Release CTest.
