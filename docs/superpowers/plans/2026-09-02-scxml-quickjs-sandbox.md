# TurboSCXML QuickJS Sandbox Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add optional bounded synchronous script execution and script-variable locations without exposing host I/O or QuickJS ABI.

**Architecture:** A private QuickJS engine is selected only by an additive compile/session API for `quickjs-sandbox`. Program-owned source is evaluated in a per-session runtime and per-macrostep restricted context; CMeta state plus supplemental scope are imported/exported transactionally.

**Tech Stack:** C11, pinned QuickJS source, CMeta, CFlow SerialExecutor, TinyTest, CMake Presets.

**Spec:** `docs/specs/scxml-remaining-conformance-design.md`

## Global Constraints

- `TURBOSCXML_ENABLE_QUICKJS` defaults OFF and disabled packages do not link the engine.
- No `quickjs-libc`, module loader, native module, filesystem, network, process, environment, or credential surface.
- Heap/stack/time/source/conversion limits are mandatory and positive.
- Existing null/CMeta behavior and package links remain unchanged when disabled.
- Optional HTTP source acquisition uses `TurboSCXML::CHttpResource` during
  admission only; QuickJS never receives a CHTTP client or network API.

---

### Task 1: Optional dependency and sandbox kernel

**Files:** `CMakeLists.txt`, `vcpkg.json` or `vendor/quickjs/*`, `src/scxml_quickjs.c`, `src/scxml_quickjs.h`, `tests/scxml_quickjs_test.c`, `tests/CMakeLists.txt`

- [x] Add a feature-OFF test proving `quickjs-sandbox` is rejected without an engine dependency.
- [x] Pin the approved QuickJS source/revision and license; build only engine sources behind the OFF-by-default option.
- [x] Create a runtime/context with allocator, stack, interrupt deadline, and cancellation limits; install only enumerated intrinsics.
- [x] Add API-absence and escape tests for `fetch`, `std`, `os`, `process`, `require`, module loading, dynamic code, prototype attacks, and credentials.
- [x] Verify enabled/disabled configure, build, focused tests, and installed consumers.

### Task 2: Public profile and transactional bridge

**Files:** `include/scxml/scxml.h`, `src/scxml_program.c`, `src/scxml_session.c`, `src/scxml_impl.h`, `src/scxml_quickjs.c`, `tests/scxml_quickjs_test.c`

- [x] Add failing compile/session tests for exact profile spelling, invalid limits, syntax failure, initial import, scalar/struct/sequence round trip, and conversion rollback.
- [x] Add versioned QuickJS compile/session options containing no QuickJS types.
- [x] Compile immutable expression/script UTF-8 source and create one isolated runtime per session.
- [x] Import read-only system values and schema/supplemental data; export into fresh scratch and publish only after full validation.
- [x] Test timeout, OOM, exception-after-write, fresh-context disposal, cross-session isolation, and quiescent destroy.

### Task 3: Script syntax and timing

**Files:** `src/scxml_ast.h`, `src/scxml_ast.c`, `src/scxml_analyze.c`, `src/scxml_emit.c`, `src/scxml_runtime.c`, `tests/scxml_quickjs_test.c`

- [x] Add failing tests for inline/root script, nested executable script, exclusive `src` versus content, and script variable use as an assignment location.
- [x] Add `SCXML_ELEMENT_SCRIPT`, immutable script descriptors, root load actions, and ordinary executable steps.
- [x] Resolve `<script src>` only through the compile-time text provider and reject timeout/missing/non-2xx/oversized source before publication; the optional CHTTP provider follows no redirect or retry.
- [x] Execute root scripts after data initialization and before initial configuration; execute nested scripts in document order inside their block.
- [x] Persist admitted script variables through the bounded supplemental scope and enforce atomic rollback.

### Task 4: W3C 301-304 promotion

**Files:** `tests/w3c/test301.scxml`, `tests/w3c/test302.scxml`, `tests/w3c/test303.scxml`, `tests/w3c/test304.scxml`, `tests/scxml_w3c_conformance_test.c`, `tests/w3c/manifest.tsv`, `tests/w3c/README.md`

- [x] Add automated terminal fixtures for bad source rejection, load-time root script, executable nested script, and script-variable location assignment.
- [x] Run each fixture in the enabled QuickJS test profile and prove it fails before its implementation step.
- [x] Promote rows 301-304 only in the enabled profile and keep feature-OFF accounting explicit.
- [x] Run enabled/disabled Debug and Release CTest, strict W3C, sanitizer/security cases, install consumers, and `git diff --check`.
