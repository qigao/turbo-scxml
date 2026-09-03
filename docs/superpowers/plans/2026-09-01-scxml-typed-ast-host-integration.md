# TurboSCXML Typed AST and Host Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make TurboSCXML compile through an owning typed immutable SCXML tree and use the single CFlow V4 host transaction boundary for finalize and invocation lifecycle behavior.

**Architecture:** TurboXML is syntax-only and dies after typed AST construction. Semantic analysis lowers the typed tree into the existing immutable program IR. Runtime SCXML behavior stays in the session host callback; CFlow remains format-neutral.

**Tech Stack:** C11, TurboParser XML, TurboUtils CFlow/CMeta, TinyTest, CMake Presets.

**Spec:** `docs/specs/scxml-typed-ast-host-integration.md`

### Task 1: Add a bounded owning typed AST with RED tests

**Files:**
- Create: `src/scxml_ast.h`
- Create: `src/scxml_ast.c`
- Create: `tests/scxml_ast_test.c`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [x] Add private tests for typed element/attribute recognition, document order,
  parent/child/sibling IDs, copied text, source locations, and validity after
  `salts_xml_document_destroy()`.
- [x] Add limit and malformed namespace/vocabulary tests with deterministic
  diagnostics and no partially published AST.
- [x] Record RED, then implement two-pass count/storage construction using
  checked arithmetic and one cleanup path.

### Task 2: Move root and topology analysis from DOM to AST

**Files:**
- Modify: `src/scxml_program.c`
- Modify: `src/scxml_analyze.c`
- Modify: `src/scxml_impl.h`
- Test: `tests/scxml_test.c`

- [x] Characterize current root/state/initial/history diagnostics and emitted
  state IDs before changing production code.
- [x] Parse XML, build AST, destroy DOM, then validate root attributes and state
  topology exclusively through node IDs and typed attribute accessors.
- [x] Replace `scxml_node_ref.node` DOM identity with stable AST node IDs.
- [x] Run compiler tests after each migrated behavior family.

### Task 3: Move transitions and executable content to AST

**Files:**
- Modify: `src/scxml_analyze.c`
- Modify: `src/scxml_emit.c`
- Modify: `src/scxml_program.c`
- Test: `tests/scxml_test.c`
- Test: `tests/scxml_w3c_conformance_test.c`

- [x] Migrate transitions, guards, event descriptors, and target resolution.
- [x] Migrate onentry/onexit/raise/if/log/assign/foreach/send/cancel in document
  order while preserving existing IR counts and ownership.
- [x] Migrate invoke/finalize descriptors last, then remove all TurboXML node
  parameters from semantic analysis and emission.
- [x] Prove no runtime/program structure retains a TurboXML handle with `rg.exe`
  and private AST lifetime tests.

### Task 4: Migrate the session to CFlow host ABI V4

**Files:**
- Modify: `src/scxml_session.c`
- Modify: `src/scxml_runtime.c`
- Modify: `src/scxml_impl.h`
- Modify: `tests/scxml_w3c_conformance_test.c`

- [x] Replace V2/V3 table selection with one V4 callback and phase dispatch.
- [x] Reuse one executable-range engine for normal blocks and finalize blocks,
  obtaining mutable state only when an assignment executes.
- [x] Remove the compile-time `finalize cannot mutate CMeta state` rejection.
- [x] Preserve matching-token resolution, finalize-before-selection,
  completion, autoforward, and quiescent invocation reconciliation order.
- [x] Make W3C 233/234 pass without changing their strict host witnesses.

### Task 5: Verify compatibility and ownership

**Files:**
- Modify: `docs/specs/scxml-invoke-finalize-design.md`
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`

- [x] Update the focused finalize design to reference the V4 transaction and
  promote corpus rows only after both executable witnesses pass.
- [x] Run `scxml_ast_test`, compile tests, focused W3C 233/234, full W3C corpus,
  full Release CTest, and focused Debug/ASan ownership tests.
- [x] Run CodeGraph affected analysis and `git diff --check`; confirm build and
  `.codegraph/` products are absent from Git status.
