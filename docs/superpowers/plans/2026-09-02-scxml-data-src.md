# SCXML Data Source Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Load `<data src>` through a bounded host adapter at the exact SCXML binding time and decode it transactionally with CSerde/CBind.

**Architecture:** The immutable program owns URI and destination descriptors. CMeta session options V3 inject a host resource adapter; runtime opens one token reader, decodes one exact value into scratch, moves it into staged state, and closes the resource exactly once.

**Tech Stack:** C11, CMeta, CSerde, CBind, optional CHTTP, CFlow Statechart,
TinyTest, CMake Presets.

**Spec:** `docs/specs/scxml-remaining-conformance-design.md`

## Global Constraints

- No core filesystem, HTTP, URI resolution, media-type guessing, or fallback.
- Existing V1/V2 compile and session APIs remain source and ABI compatible.
- Every allocation and token/container/string/depth count has a positive hard limit.
- Provider/decode failure raises `error.execution` without publishing partial data.
- CHTTP remains an optional adapter target; the SCXML core never parses network
  URLs or links `Rocida::CHTTP`.

---

### Task 1: Resource adapter contract

**Files:**
- Modify: `include/scxml/scxml.h`
- Modify: `CMakeLists.txt`
- Test: `tests/scxml_cmeta_test.c`

**Interfaces:**
- Produces: `scxml_data_resource_adapter_v1`, `scxml_cmeta_session_options_v3`, `scxml_session_init_cmeta_v3()`.
- Consumes: `cserde_reader`, existing CMeta initial state and environment override rows.

- [x] **Step 1: Write the failing contract test**

  Compile a `<data id='count' src='mem:count'/>` program, initialize through V3 with an in-memory one-token reader, and assert terminal state requires `count == 7`.

- [x] **Step 2: Verify RED**

  Run `scxml_cmeta_test --filter "loads data src"`; expect compile failure with the current external-loader unsupported diagnostic.

- [x] **Step 3: Add only the versioned declarations and dependency checks**

  Add ABI/size fields, exact open/close callback signatures, V3 session options containing V2 fields plus adapter/user and positive decode limits, and CMake target checks for `Rocida::CSerde`/`Rocida::CBind`.

- [x] **Step 4: Build the focused target**

  Run `cmake --build --preset win-dev-user --target scxml_cmeta_test`; expect the test to build and remain behaviorally red.

### Task 2: Compile immutable data-source descriptors

**Files:**
- Modify: `src/scxml_impl.h`
- Modify: `src/scxml_analyze.c`
- Modify: `src/scxml_emit.c`
- Modify: `src/scxml_program.c`
- Modify: `src/scxml_assign.h`
- Modify: `src/scxml_assign.c`

**Interfaces:**
- Produces: destination-only assignment programs and program-owned source URI bytes.
- Consumes: existing initializer/binding indices and retained program string storage.

- [x] **Step 1: Add admission failure cases**

  Assert empty `src`, `src` with `expr`, and `src` with inline content fail deterministically; valid `src` reaches session admission.

- [x] **Step 2: Verify RED**

  Run the focused cases and confirm valid `src` still fails as unsupported while malformed combinations fail for their intended structural reason.

- [x] **Step 3: Add destination-only assignment compilation**

  Implement `scxml_assign_compile_external()` using the existing strict destination compiler and retain its exact destination descriptor/range without an expression.

- [x] **Step 4: Retain source descriptors transactionally**

  Extend each initializer row with source kind and URI view, count URI bytes with checked arithmetic, copy them into program-owned storage, and destroy all partial rows on failure.

- [x] **Step 5: Verify compile behavior**

  Run the focused tests; expect valid `src` to compile and session init without V3 adapter to fail admission.

### Task 3: Runtime decode and lifecycle

**Files:**
- Modify: `src/scxml_session.c`
- Modify: `src/scxml_session.h`
- Modify: `src/scxml_runtime.c`
- Modify: `src/scxml_assign.c`
- Test: `tests/scxml_cmeta_test.c`

**Interfaces:**
- Consumes: V3 adapter, destination-only assignment, program URI descriptor.
- Produces: one-value CBind decode into staged state and exactly-once adapter close.

- [x] **Step 1: Add timing, close, override, and error tests**

  Cover early init, first late entry, no reload on re-entry, environment override without open, open failure, malformed token, trailing token, limit failure, and close after every successful open.

- [x] **Step 2: Verify RED**

  Run the resource group and confirm each case fails at the missing runtime path rather than test setup.

- [x] **Step 3: Allocate bounded session scratch**

  Validate V3 ABI and positive limits, copy adapter ops, allocate aligned destination plus CBind scratch with checked arithmetic, and release them after the native instance is quiescent.

- [x] **Step 4: Decode and replace staged destination**

  Open the URI, decode exactly one CSerde value with the destination descriptor, require reader completion, close the resource, and move the complete scratch value into the staged location. Restore scratch and raise `error.execution` on every failure.

- [x] **Step 5: Verify GREEN and regressions**

  Run the resource group, full `scxml_cmeta_test`, then CTest Debug.

### Task 4: W3C 552 promotion

**Files:**
- Create: `tests/w3c/test552.scxml`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`

**Interfaces:**
- Consumes: the real V3 in-memory resource adapter.
- Produces: an automated terminal-pass rewrite of W3C test 552.

- [x] **Step 1: Add the failing corpus fixture and runner path**

  Rewrite the conformance placeholders to CMeta `count`, load `mem:test552` as integer 7, and transition to pass only when the bound value is observed.

- [x] **Step 2: Verify RED before manifest promotion**

  Run strict W3C mode and confirm test552 is not counted as PASS until the fixture test succeeds.

- [x] **Step 3: Promote manifest and README**

  Change only row 552 to PASS/TERMINAL_PASS with its exact source and local rewrite rationale.

- [x] **Step 4: Verify**

  Run Debug and Release focused/full CTest, strict W3C execution, and `git diff --check`.

### Task 5: Optional CHTTP resource adapter

**Files:**
- Create: `include/scxml/chttp_resource.h`
- Create: `src/scxml_chttp_resource.c`
- Modify: `CMakeLists.txt`
- Modify: `cmake/TurboSCXMLConfig.cmake.in`
- Create: `tests/scxml_chttp_resource_test.c`

**Interfaces:**
- Consumes: a borrowed single-owner `chttp_client`, a host URI resolver, an
  exact media-type decoder, and positive response/deadline limits.
- Produces: `scxml_data_resource_adapter_v1` and the compile-time text resource
  provider without exposing CHTTP through `TurboSCXML::SCXML`.

- [x] **Step 1: Write failing boundary tests**

  Cover denied URI before I/O, authorized endpoint mapping, non-2xx failure,
  redirect rejection, timeout, oversized body, unsupported media type,
  exactly-once response/reader release, and `https` rejection.

- [x] **Step 2: Verify RED**

  Configure with the optional adapter enabled and confirm the new contract test
  fails because no adapter target/API exists.

- [x] **Step 3: Add the isolated target**

  Build and export `TurboSCXML::CHttpResource` only when explicitly enabled and
  `Rocida::CHTTP` is present. Link it to the generic SCXML resource API;
  leave the core dependency contract unchanged.

- [x] **Step 4: Implement bounded acquisition**

  Call the resolver before `chttp_get`, require a final 2xx response, never
  redirect/retry, pass an exact configured content type to the decoder, and
  retain the owning response until the resource lease closes.

- [x] **Step 5: Verify optional packaging**

  Run enabled/disabled Debug and Release tests plus installed C and C++
  consumers. Assert the core package still links without CHTTP.
