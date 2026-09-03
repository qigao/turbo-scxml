# CCXML Create Call Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded `<createcall>` action that submits a copied literal destination through the transactional telephony adapter.

**Architecture:** Extend the compact CCXML action row with an owned destination view and append one optional callback to the size-versioned v1 telephony adapter. The compiler admits only a quoted nonempty string literal; the session reuses the existing prepare/commit/reverse-discard transaction and leaves asynchronous outcome events to the provider.

**Tech Stack:** C11, Turbo XML parser, CFlow effect tickets, CMake, TinyTest

**Spec:** `docs/specs/ccxml-createcall-design.md`

## Global Constraints

- Keep `TurboSCXML::CCXML` always built and preserve the one-way dependency on `TurboSCXML::SCXML`.
- Do not add an ECMAScript engine or URI/network backend.
- Copy all admitted XML bytes into bounded program-owned storage.
- Keep the original v1 adapter prefix binary-readable through `struct_size`.
- Preserve document-order prepare/commit and reverse-order discard.
- Run all Windows configure, build, and test commands from an initialized
  Visual Studio x64 developer environment so MSVC headers and the ASan runtime
  are on `INCLUDE` and `PATH`.

---

### Task 1: Compile a bounded create-call action

**Files:**
- Modify: `tests/ccxml_program_test.c`
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_program.c`

**Interfaces:**
- Consumes: `ccxml_compile`, `ccxml_limits.max_name_bytes`, Turbo XML node/attribute views.
- Produces: `CCXML_ACTION_CREATE_CALL` rows with `destination` and `destination_size`, plus `ccxml_program_impl.uses_create_call`.

- [x] **Step 1: Write the failing compiler tests**

Add TinyTest cases where `<createcall dest="'tel:+12025550123'"/>` returns
`CCXML_OK`, while missing `dest`, `dest="''"`, `dest="number"`, a backslash
escape, an extra `callerid`, and nested content return the exact statuses from
the spec. The production change each case catches is incorrect admission or
missing rejection at the CCXML syntax boundary.

- [x] **Step 2: Run the focused test and verify RED**

Run:

```powershell
cmake --build --preset win-release-user --target ccxml_program_test
ctest --preset win-release-user -R ccxml_program --output-on-failure
```

Expected: the valid form fails as `CCXML_UNSUPPORTED_FEATURE` before compiler
support exists.

- [x] **Step 3: Implement literal validation, measurement, and copying**

Add `CCXML_ACTION_CREATE_CALL` and destination fields to the private action
row. Parse exactly one `dest` attribute, validate the quote-only literal
profile, charge the decoded bytes plus NUL to `max_name_bytes`, and copy the
unquoted bytes during the existing second pass. Mark programs that use the
new action.

- [x] **Step 4: Run the focused compiler test and verify GREEN**

Run the commands from Step 2 and then:

```powershell
ctest --preset win-release-user -R ccxml_program
```

Expected: all compiler cases pass.

- [x] **Step 5: Commit the compiler slice**

```powershell
git add tests/ccxml_program_test.c src/ccxml_internal.h src/ccxml_program.c
git commit -m "feat(ccxml): compile bounded createcall destinations"
```

### Task 2: Dispatch create-call transactions through the adapter

**Files:**
- Modify: `tests/ccxml_session_test.c`
- Modify: `include/ccxml/ccxml.h`
- Modify: `src/ccxml_session.c`

**Interfaces:**
- Consumes: `CCXML_ACTION_CREATE_CALL`, `ccxml_program_impl.uses_create_call`, `cflow_statechart_effect_ticket`.
- Produces: `ccxml_create_call_request` and append-only `ccxml_telephony_adapter_v1.prepare_create_call`.

- [x] **Step 1: Write failing dispatch and compatibility tests**

Add a provider callback that records the exact destination and returns a real
ticket. Add cases proving source overwrite does not affect the request, mixed
accept/create-call effects commit in document order, a rejected create-call
discards the earlier accept ticket, the legacy prefix initializes an
accept-only program, and a create-call program rejects a legacy prefix.

- [x] **Step 2: Run the focused test and verify RED**

Run:

```powershell
cmake --build --preset win-release-user --target ccxml_session_test
```

Expected: compilation fails because the request type and adapter callback do
not exist.

- [x] **Step 3: Implement safe tail-copy and create-call dispatch**

Append `prepare_create_call` after `is_quiescent`. Validate the legacy prefix
with `offsetof`, zero the session copy, and `memcpy` only
`min(struct_size, sizeof(current_table))`. Reject a create-call program during
session initialization when the copied tail callback is absent. Dispatch the
new action through the same ticket-validation helper and transaction storage
used by accept.

- [x] **Step 4: Run focused and full CCXML tests**

```powershell
cmake --build --preset win-release-user --target ccxml_session_test
ctest --preset win-release-user -R ccxml_
```

Expected: all cases pass with no warnings.

- [x] **Step 5: Commit the runtime slice**

```powershell
git add tests/ccxml_session_test.c include/ccxml/ccxml.h src/ccxml_session.c
git commit -m "feat(ccxml): dispatch transactional createcall"
```

### Task 3: Document, verify, and update the pull request

**Files:**
- Modify: `README.md`
- Create: `docs/specs/ccxml-createcall-design.md`
- Create: `docs/superpowers/plans/2026-09-03-ccxml-createcall.md`

**Interfaces:**
- Consumes: the completed compiler/runtime contract.
- Produces: a documented installed API and verified PR revision.

- [x] **Step 1: Update public documentation**

Document the literal destination profile, adapter callback, asynchronous
provider-event responsibility, unsupported optional attributes, and the fact
that this remains an incubation subset.

- [x] **Step 2: Run the full verification matrix**

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user
cmake --fresh --preset win-dev-user
cmake --build --preset win-dev-user
ctest --preset win-dev-user
cmake --fresh --preset win-release-chttp-user
cmake --build --preset win-release-chttp-user
ctest --preset win-release-chttp-user
cmake --fresh --preset win-release-quickjs-user
cmake --build --preset win-release-quickjs-user
ctest --preset win-release-quickjs-user
cmake --build --preset install-win-release-user
git diff --check
rg -n "fit\\(|it_only\\(|TODO|FIXME|HACK" include src tests README.md docs/specs/ccxml-createcall-design.md
```

Expected: Release 13/13, Debug/ASan 13/13, CHTTP 18/18, QuickJS 13/13,
clean diff check, and no focused tests or placeholder markers in changed code.
Then configure `tests/install_consumer` with `TURBOSCXML_ROOT` naming the
Release install and `SALTS_ROOT` naming the matching Salts profile; build and
run `turboscxml_install_consumer`, `turboscxml_ccxml_install_consumer`, and
`turboscxml_ccxml_install_consumer_cpp`. All three must exit zero.

- [x] **Step 3: Commit documentation and push**

```powershell
git add README.md docs/specs/ccxml-createcall-design.md docs/superpowers/plans/2026-09-03-ccxml-createcall.md
git commit -m "docs(ccxml): define createcall incubation profile"
git push
```

- [x] **Step 4: Verify the PR state**

```powershell
gh pr view 35 --json url,state,mergeable,mergeStateStatus,statusCheckRollup
```

Expected: PR #35 remains open and mergeable with all configured checks green.
