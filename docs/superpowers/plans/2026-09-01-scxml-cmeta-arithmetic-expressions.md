# CMeta Arithmetic Expressions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add checked arithmetic expressions to the TurboSCXML CMeta datamodel without changing CMeta, CFlow, or public APIs.

**Architecture:** Extend the existing recursive-descent grammar in `scxml_expr.c`, lower operators to QueryVM, and implement strict typed arithmetic in the existing QueryVM execution callbacks.

**Tech Stack:** C11, CMeta scalar descriptors, QueryVM, TinyTest, CMake Presets.

**Spec:** `docs/specs/scxml-cmeta-arithmetic-expression-design.md`

## Global Constraints

- Preserve all existing expression behavior and public interfaces.
- Keep numeric result kinds statically determined.
- Reject incompatible kinds at compile time and data-dependent numeric faults at evaluation time.
- Leave output values unchanged on evaluation failure.
- Use the existing instruction, operand, depth, and register limits.

---

## Task 1: Add failing arithmetic tests

- [x] Add TinyTest cases in `tests/scxml_expr_test.c` for precedence, parentheses, unary operators, typed results, and Boolean conditions containing arithmetic.
- [x] Add failure cases for type mismatches, overflow/underflow, division/remainder by zero, non-finite floating results, and unchanged output values.
- [x] Build `scxml_expr_test` and run `ctest --preset win-release-user -R '^scxml_expr_test$' --output-on-failure`; confirm the new tests fail for the expected missing syntax.

## Task 2: Extend lexer and parser

- [x] Add explicit arithmetic tokens and stop folding a leading minus into the numeric token.
- [x] Preserve exact `INT64_MIN` literal admission through unary-minus handling.
- [x] Add unary, multiplicative, and additive parser levels with static result-kind checks and QueryVM opcode emission.
- [x] Build and run the focused expression test.

## Task 3: Implement checked QueryVM arithmetic

- [x] Add integral checked helpers and explicit numeric conversion helpers.
- [x] Extend the QueryVM binary callback for `ADD`, `SUB`, `MUL`, `DIV`, and `MOD`.
- [x] Add the QueryVM unary callback for `NEG` and wire it into execution.
- [x] Return backend failure for numeric faults so the existing evaluator reports `SCXML_EXPR_EVALUATION_ERROR` without publishing an output.
- [x] Run the focused expression test and confirm it passes.

## Task 4: Verify adjacent consumers and documentation

- [x] Update the user-facing expression capability documentation with the operator and numeric-error contract.
- [x] Run `cmake --build --preset win-release-user`.
- [x] Run `ctest --preset win-release-user --output-on-failure` and record the exact result.
- [x] Inspect `git diff --check` and the final worktree status.
