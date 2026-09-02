# SCXML Foreach Auto-Bindings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make undeclared foreach item/index names finite session variables while preserving existing declared CMeta locations.

**Architecture:** Analysis builds immutable supplemental slot descriptors before expression emission. Each session owns bounded aligned slot storage and bound bits; a microstep effect ticket stages commit/discard so supplemental values follow Statechart publication.

**Tech Stack:** C11, CMeta lifecycle traits, CFlow effect tickets, TinyTest.

**Spec:** `docs/specs/scxml-remaining-conformance-design.md`

## Global Constraints

- Runtime cannot insert names or grow a map.
- Declared CMeta locations remain the preferred destination.
- Slot type conflicts fail compilation; managed values use copy/move/destroy traits.
- Snapshot iteration behavior and iteration limits remain unchanged.

---

### Task 1: Program supplemental-slot table

**Files:** `src/scxml_analyze.c`, `src/scxml_impl.h`, `src/scxml_program.c`, `tests/scxml_foreach_test.c`

- [x] Add a failing compile test where an undeclared item is accepted and a conflicting reuse is rejected.
- [x] Verify RED with `scxml_foreach_test --filter "auto-declares"`.
- [x] Pre-scan foreach descriptors, assign stable slot ordinals and checked aligned offsets, and retain names/types in the program.
- [x] Verify valid admission and conflict rejection.

### Task 2: Expression/location resolution and storage

**Files:** `src/scxml_expr.c`, `src/scxml_expr.h`, `src/scxml_location.c`, `src/scxml_foreach.c`, `src/scxml_session.c`, `src/scxml_runtime.c`

- [x] Add failing tests for item persistence, index persistence, declared-location reuse, managed element destruction, and adapter rollback.
- [x] Extend compiled operands/locations with a supplemental slot ordinal while retaining CMeta offsets for declared paths.
- [x] Allocate/initialize session slot storage and bind bits; route foreach writes and subsequent reads through the resolved destination kind.
- [x] Stage one supplemental-scope snapshot ticket per microstep and implement exactly-once commit/discard.
- [x] Run focused foreach, managed foreach, expression, and CMeta session tests.

### Task 3: W3C 150/151 promotion

**Files:** `tests/w3c/test150.scxml`, `tests/w3c/test151.scxml`, `tests/scxml_w3c_conformance_test.c`, `tests/w3c/manifest.tsv`, `tests/w3c/README.md`

- [x] Add terminal-pass fixtures that first reuse declared fields, then auto-declare item/index and observe each after the foreach block.
- [x] Verify both fixtures fail before runner support and pass afterward.
- [x] Promote only rows 150 and 151 to PASS/TERMINAL_PASS.
- [x] Run strict W3C and complete Debug/Release CTest.
