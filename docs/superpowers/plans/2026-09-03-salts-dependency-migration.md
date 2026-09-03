# Salts Dependency Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the retired dependency package identity with the installed Salts 1.1 package across TurboSCXML build, test, install, and documentation surfaces.

**Architecture:** TurboSCXML continues to consume one explicitly rooted first-party SDK, but the root becomes `SALTS_ROOT`, package discovery becomes `find_package(Salts 1.1 CONFIG REQUIRED)`, and every imported target uses the `Salts::` namespace. Dependency-facing source uses the current Salts XML, threading, clock, error-code, UUID, logging, and CMeta-data API names, while typed container tests use the current `<cstl/typed.h>` public header and `Salts::CSTL` target. TurboSCXML's own exported target names and runtime APIs remain unchanged.

**Tech Stack:** C11/C++17, CMake 3.20+, CMake Presets, Salts 1.1, CSTL, TinyTest, MSVC/Ninja.

**Spec:** `docs/specs/scxml-core-design.md`

## Global Constraints

- Resolve Salts only from `$ENV{SALTS_ROOT}` with `NO_DEFAULT_PATH`; do not retain a legacy-package fallback.
- Keep `CMAKE_PREFIX_PATH` limited to the matching vcpkg profile.
- Preserve Debug/ASan, Release, QuickJS, and CHTTP profile isolation.
- Use BoringSSL in the local vcpkg runtime profile so Salts CHTTP's `ssl.dll`/`crypto.dll` dependency closure is present.
- Preserve all `TurboSCXML::*` exported target names and component behavior.
- Use `<cstl/typed.h>` and `Salts::CSTL` for typed container tests.
- Update historical documentation terminology without changing recorded commit SHAs or feature semantics.
- Verify installed C and C++ consumers against each applicable installed profile.

---

### Task 1: Migrate active build and package contracts

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `CMakeUserPresets.json`
- Modify: `cmake/TurboSCXMLConfig.cmake.in`
- Modify: `include/ccxml/ccxml.h`
- Modify: `include/scxml/scxml.h`
- Modify: `src/*.c`
- Modify: `src/*.h`
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/scxml_foreach_test.c`
- Modify: `tests/scxml_managed_foreach_test.c`
- Modify: `tests/scxml_quickjs_test.c`
- Modify: `tests/scxml_sequence_test.c`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `vcpkg.json`

**Interfaces:**
- Consumes: installed `$PROJECT_ROOT/external/pkgs/salts/{debug,release}` and Salts 1.1 imported targets.
- Produces: source-tree and installed TurboSCXML packages with no active legacy dependency.

- [x] **Step 1: Replace package roots and imported targets**

Change the legacy root and path variables to `SALTS_ROOT`/`SALTS_ROOT_PATH`, package discovery to `find_package(Salts 1.1)`, and every legacy imported target to its `Salts::*` equivalent. Replace the old STL test dependency with `Salts::CSTL`.

- [x] **Step 2: Replace retired typed-container includes**

Change each retired namespaced typed-container include to `<cstl/typed.h>` without changing typed container behavior.

- [x] **Step 3: Migrate Salts public API prefixes and headers**

Replace XmlParser types, functions, and constants with their current `salts_xml_*` and `SALTS_XML_*` names throughout public headers, implementation, tests, and documentation. Migrate dependency-owned thread, clock, error-code, UUID, logging, and CMeta-data headers and symbols to their `salts_*`/`SALTS_*` names. Preserve TurboSCXML's own API names and behavior.

- [x] **Step 4: Reconfigure and run focused core tests**

Run `cmake --fresh --preset win-dev-user`, build `scxml_foreach_test`, `scxml_managed_foreach_test`, `scxml_sequence_test`, `scxml_quickjs_test`, and `scxml_w3c_conformance_test`, then run those CTest names. Require configuration to report the Salts package and every test to pass.

### Task 2: Update repository documentation terminology

**Files:**
- Modify: `README.md`
- Modify: `docs/**/*.md`

**Interfaces:**
- Consumes: the active `SALTS_ROOT`, `Salts::*`, and `<cstl/typed.h>` contract from Task 1.
- Produces: documentation and historical plans whose current package terminology matches the repository.

- [x] **Step 1: Replace package terminology**

Replace every legacy package name, root variable, imported-target namespace, and lowercase package path with `Salts`, `SALTS_ROOT`, `Salts::*`, and `salts` respectively. Preserve unrelated prose and all recorded Git commit identifiers.

- [x] **Step 2: Scan for stale references**

Run a case-insensitive repository scan for the retired package name while excluding build and vcpkg output. Require no matches.

### Task 3: Verify every supported profile and installed consumer

**Files:**
- Verify: `CMakeUserPresets.json`
- Verify: `tests/install_consumer/CMakeLists.txt`

**Interfaces:**
- Consumes: migrated source and package configuration.
- Produces: build/test/install evidence for core, QuickJS, and CHTTP profiles.

- [x] **Step 1: Run full source-tree matrices**

Fresh-configure, build, and test `win-dev-user`, `win-release-user`, `win-release-quickjs-user`, and `win-release-chttp-user` from `VsDevCmd.bat`. Require zero failed tests in every profile.

- [x] **Step 2: Install and consume packages**

Install `install-win-release-user`, `install-win-release-quickjs-user`, and `install-win-release-chttp-user`. Configure `tests/install_consumer` against each installed TurboSCXML root with matching `SALTS_ROOT`, build the consumers, and run the core C, CCXML C/C++, QuickJS, CHTTP resource, and CHTTP Event I/O executables applicable to each profile. Require exit code zero.

- [x] **Step 3: Run final static checks**

Run `git diff --check`, the retired-package scan, and the focused-test/debug-residue scan. Require a clean worktree except for the planned tracked changes.

### Task 4: Review, publish, and merge PR #35

**Files:**
- Review: the complete migration diff

**Interfaces:**
- Consumes: verified commits on `feat/ccxml-core-mvp`.
- Produces: updated and merged GitHub PR #35 targeting `main`.

- [ ] **Step 1: Commit and request read-only review**

Commit the migration as `build: migrate dependencies to Salts`, then request a read-only review covering package isolation, imported targets, installed config behavior, headers, and docs.

- [ ] **Step 2: Push and update PR #35**

Push the feature branch, append the Salts migration and verification evidence to PR #35, and verify its head SHA matches the pushed commit.

- [ ] **Step 3: Merge and verify remote main**

Use the repository's established PR merge method, verify PR #35 is `MERGED`, fetch `origin/main`, and confirm the reported merge commit is reachable from `origin/main`. Preserve the externally managed worktree.
