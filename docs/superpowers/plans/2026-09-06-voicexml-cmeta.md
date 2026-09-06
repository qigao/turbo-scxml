# VoiceXML Typed/Staged CMeta Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development to implement this plan task-by-task.

**Goal:** Add an installable `datamodel="cmeta"` VoiceXML profile with typed
lexical variables, transactional executable content, non-media FIA block
selection, and durable exit data.

**Architecture:** Keep `TurboSCXML::VoiceXML` XmlParser-only. Add
`TurboSCXML::VoiceXMLCMeta`, reuse neutral CMeta scope/location primitives,
compile a restricted VoiceXML scalar expression language, and extend the
existing opaque program/session owners through private profile vtables. Each
block runs against staged root and lexical scopes and commits atomically.

**Tech Stack:** C11, Salts CMeta, Salts QueryVM, Salts XmlParser, TinyTest,
CMake presets.

**Spec:** `docs/specs/voicexml-cmeta-design.md`

**Tracking:** [GitHub issue #44](https://github.com/qigao/turbo-scxml/issues/44),
under [VoiceXML roadmap #41](https://github.com/qigao/turbo-scxml/issues/41).

## Global constraints

- Do not add `value`, prompt/audio/SSML, ASR/SRGS, grammar/recognition/collect,
  fields/filled, record, transfer, media callback, ticket, token, wait state,
  QuickJS, DOM, script, data/resource loading, network, executor, or CCXML API.
- Do not call the CMeta expression language ECMAScript. Plain `vxml_compile`
  continues to reject expressions and `datamodel="cmeta"`.
- Preserve `TurboSCXML::VoiceXML`'s exact `Salts::XmlParser` dependency
  contract and its XmlParser-only package isolation test.
- Do not link VoiceXMLCMeta to `TurboSCXML::SCXML` or expose SCXML private
  headers through the new target.
- Use semantic CMeta equality, checked arithmetic, bounded storage, explicit
  undefined bits, measured ownership, and failure-atomic output handles.
- Run Windows configure/build/test commands through the discovered
  `VsDevCmd.bat` and checked-in user presets.

---

### Task 1: Publish the optional CMeta component and versioned ABI

**Files:**
- Create: `include/voicexml/cmeta.h`
- Create: `src/voicexml_cmeta_internal.h`
- Create: `src/voicexml_cmeta_program.c`
- Create: `tests/voicexml/test_voicexml_cmeta_api.c`
- Modify: `include/voicexml/voicexml.h`
- Modify: `src/voicexml_internal.h`
- Modify: `src/voicexml_program.c`
- Modify: `src/voicexml_session.c`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Boundary:** Produces the `TurboSCXML::VoiceXMLCMeta` target, size-versioned
compile/session options, value/exit query types, and private profile lifecycle
hooks without changing the base VoiceXML dependency closure.

- [ ] Add API tests first for C/C++ header inclusion, default/zero handles,
  appended status values, correct ABI prefixes, wrong versions, undersized
  prefixes, ignored future tails, and cross-profile initializer rejection.
  Configure/build the focused target and record RED.
- [ ] Add the exact public declarations from the design. Validate that the root
  is a CMeta struct with supported copy/move/destroy semantics and that all hard
  limits are positive. Copy the semantic-descriptor pointer array while
  borrowing every descriptor.
- [ ] Add private program/session profile kind and destructor/start hooks. Core
  owners invoke generic hooks only; no base source includes CMeta headers.
- [ ] Add CMake dependency-contract assertions proving base VoiceXML remains
  exactly XmlParser-only and VoiceXMLCMeta publishes only VoiceXML+CMeta with a
  link-only QueryVM closure.
- [ ] Run focused API tests and the Release suite. Commit.

### Task 2: Extract neutral typed scope and location storage

**Files:**
- Create: `src/cmeta_scope.h`
- Create: `src/cmeta_scope.c`
- Create: `src/cmeta_location.h`
- Create: `src/cmeta_location.c`
- Create: `tests/voicexml/test_voicexml_cmeta_scope.c`
- Modify: `src/scxml_scope.h`
- Modify: `src/scxml_scope.c`
- Modify: `src/scxml_location.h`
- Modify: `src/scxml_location.c`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Boundary:** Shares only CMeta-generic storage and dotted-path resolution.
SCXML-specific statuses, system locations, and policy remain in SCXML wrappers.

- [ ] Add failing tests for aligned trivial/managed slots, bound versus zero,
  clear/destruction, copy/move rollback, duplicate semantic-equal types,
  same-name type conflicts, nested dotted paths, overflow, and hard limits.
- [ ] Extract bounded generic implementations with allocator operations so
  VoiceXML allocation-failure tests cover schema, storage, and managed copies.
  Keep one schema per lexical frame; shadowing is resolved above this layer.
- [ ] Convert SCXML scope/location entry points to thin compatibility wrappers
  and run `scxml_expr_test`, `scxml_cmeta_test`, `scxml_foreach_test`, and the
  full Release suite before continuing.
- [ ] Commit only after the SCXML behavior-preservation gate is green.

### Task 3: Compile the restricted VoiceXML CMeta expression profile

**Files:**
- Create: `src/voicexml_cmeta_expr.h`
- Create: `src/voicexml_cmeta_expr.c`
- Create: `tests/voicexml/test_voicexml_cmeta_expr.c`
- Modify: `src/voicexml_cmeta_internal.h`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Boundary:** Produces immutable Boolean/value expression programs whose
location operands are resolved against a compile-time lexical-scope chain.

- [ ] Add RED tests for supported scalar literals/operators, typed dotted
  operands, Boolean-only conditions, nearest-scope shadowing, declared
  undefined propagation, undeclared-read errors, numeric overflow, owned
  strings, semantic-equal multi-TU descriptors, exact limits, and
  destroy-after-partial-compile.
- [ ] Add negative tests proving `_event`, `_name`, `_sessionid`,
  `_ioprocessors`, `In()`, `is_bound()`, runtime-missing paths, and external
  evaluation are absent.
- [ ] Implement a separately named restricted front end backed by QueryVM.
  Reuse parsing/conversion ideas but do not include or link SCXML. Each name
  operand stores an ordered typed candidate chain of root or
  `{scope_id, slot, field_offset}` handles and resolves the innermost currently
  declared candidate at runtime.
- [ ] Require scalar terminal descriptors; use semantic type comparisons and
  bounded CMeta buffer reads for strings. Retain all expression-owned bytes in
  immutable program storage.
- [ ] Run focused expression tests and the full Release suite. Commit.

### Task 4: Compile variables, conditionals, clear, guards, and exit data

**Files:**
- Modify: `src/voicexml_cmeta_program.c`
- Modify: `src/voicexml_cmeta_internal.h`
- Create: `tests/voicexml/test_voicexml_cmeta_program.c`
- Modify: `tests/CMakeLists.txt`

**Boundary:** Lowers admitted XML into immutable forms, block form-item rows,
lexical schemas, structured action/branch rows, typed locations, and expression
programs.

- [ ] Add table-driven RED tests for root/form/block `var`, executable-only
  repeated `var`, assign, clear with/without namelist, nested if/elseif/else,
  block name/expr/cond, and exit empty/expr/namelist.
- [ ] Add placement/order and exclusivity failures, duplicate root/form vars,
  the common dialog namespace collision between form vars and form items,
  unknown/incompatible paths, non-Boolean conditions, invalid NCNames,
  conditional-depth/scope-storage bounds, and source diagnostics. Assert
  `exit` expr+namelist maps to `VXML_INVALID_STRUCTURE`/`error.badfetch`.
- [ ] Add explicit rejection tests for `value`, `log`, non-whitespace implicit
  prompt PCDATA, and every deferred media/input element. They must return
  `VXML_UNSUPPORTED_FEATURE`, never compile to no-op.
- [ ] Resolve each source variable's type from the matching top-level root
  field, build independent document/dialog/anonymous schemas, and compile
  nearest-scope typed handles.
- [ ] Measure all row/string/expression/scope storage with checked arithmetic;
  ensure every failed pass destroys compiled expressions and schemas once and
  leaves `vxml_program` empty.
- [ ] Run focused compiler tests and Release. Commit.

### Task 5: Execute non-media FIA with whole-turn staging

**Files:**
- Create: `src/voicexml_cmeta_session.c`
- Modify: `src/voicexml_cmeta_internal.h`
- Create: `tests/voicexml/test_voicexml_cmeta_session.c`
- Modify: `tests/CMakeLists.txt`

**Boundary:** Initializes application/document/dialog/form-item scopes, selects
eligible blocks in FIA order, and commits or rolls back one complete block turn.

- [ ] Add RED tests for declaration initialization order, all four lookup
  layers, shadowing, executable redeclarations, undeclared/undefined/defined
  states, forward references, declarations in untaken branches, undefined
  distinct from false/zero/empty string, first-match conditionals, and staged
  writes visible to later siblings.
- [ ] Add block-FIA tests for document order, `block@expr` initialization,
  mark-before-body, false guards, named clear revisit with persistent anonymous
  state, empty clear resetting all block items, exhaustion exit, and
  `max_execution_steps` stopping a self-clearing loop.
- [ ] Allocate aligned committed/staged application-field and lexical storage
  during init. Independently validate/copy each constructed `initial_root`
  field, apply the initial undefined bitmap, and reserve core turn scratch
  within `max_transaction_bytes` before session publication.
- [ ] Execute every block against staged application slots, bound maps, and
  frames. Managed CMeta copies/assignments may allocate while staging; on any
  failure destroy staged values and retain all committed bytes/bits. Commit by
  infallible move/swap only after the complete turn succeeds.
- [ ] Sweep allocator and managed-copy failures at session init and each turn;
  assert one terminal `FAILED` status, stable error, no partial commit, and
  idempotent close/destroy.
- [ ] Run focused session tests and Release. Commit.

### Task 6: Own exit results and public reads

**Files:**
- Modify: `src/voicexml_cmeta_session.c`
- Modify: `src/voicexml_cmeta_internal.h`
- Modify: `tests/voicexml/test_voicexml_cmeta_session.c`

**Boundary:** Publishes stable scalar read views and session-owned terminal
snapshots for empty, expression, and namelist exits.

- [ ] Add RED tests for wrong state/index/kind, application/document/dialog
  reads, undefined reads, one unnamed expression value, ordered named values,
  undefined namelist members, and string lifetime after staged storage cleanup.
- [ ] Reserve exit names and value/string capacity before mutation. Snapshot
  the result into session-owned storage before commit; allocation failure rolls
  back the whole turn.
- [ ] Keep exit-result strings valid until close/destroy. Copy ordinary CMeta
  read strings into bounded session scratch valid until the next read or
  mutating call. Clear output arguments on every error.
- [ ] Run focused session tests and Release. Commit.

### Task 7: Export, package, document, and harden

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `cmake/TurboSCXMLConfig.cmake.in`
- Modify: `tests/install_consumer/CMakeLists.txt`
- Modify: `tests/install_consumer/main.c`
- Modify: `tests/install_consumer/main.cpp`
- Modify: `tests/package/test_voicexml_package_isolation.ps1`
- Modify: `README.md`
- Modify: `docs/specs/voicexml-architecture-design.md`
- Modify: `tests/CMakeLists.txt`

**Boundary:** Installs an honest optional component and proves base-only and
CMeta-enabled package dependency closures independently.

- [ ] Add external C/C++ consumers for `find_package(TurboSCXML COMPONENTS
  VoiceXMLCMeta)` and a multi-TU semantic-descriptor fixture. Record RED before
  installing the target/header/component metadata.
- [ ] Keep the existing XmlParser-only VoiceXML fixture green. Add a CMeta
  fixture containing XmlParser, CMeta, and QueryVM but intentionally lacking
  SCXML/CFlow/CSerde/CBind; prove the CMeta component neither requires nor
  discovers the absent targets.
- [ ] Document accepted syntax, explicit language selection, ownership,
  the deliberate whole-block rollback deviation, unsupported qualified scope
  objects/application aliasing, undefined/staging semantics, hard limits, and
  the deferred media surface.
  Update issue #44 with exact delivered commits and verification only after
  the implementation is complete.
- [ ] Run fresh Release and Debug/ASan configure/build/full CTest, matching
  install presets, and all external consumers from `VsDevCmd.bat`.
- [ ] Run `git diff --check`; scan public VoiceXML headers for prompt/audio/
  recognition/media callbacks, effect tickets, tokens, waits, CFlow, SCXML,
  QuickJS, XPath, and networking. Require zero unwanted matches.
- [ ] Obtain independent spec-conformance and code-quality reviews, fix every
  material finding with focused tests, and repeat the whole-branch gates.

### Task 8: Integration checkpoint

- [ ] Confirm the feature branch is clean and every commit is based on the
  recorded #42 core head.
- [ ] Post the final local branch/head, test counts, package results, deferred
  items, and residual risks to #44.
- [ ] Stop before merge or push unless the user explicitly authorizes those
  operations.
