# CCXML Redirect Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded `<redirect>` compilation and transactional provider
dispatch using a copied literal destination and the current event connection.

**Architecture:** Reuse the compact action destination storage introduced for
create-call and append a size-guarded redirect callback to the telephony
adapter. The core validates borrowed inputs and effect tickets; the provider
owns connection/bridge state and asynchronous outcomes.

**Tech Stack:** C11, Turbo XML parser, CFlow effect tickets, CMake, TinyTest

**Spec:** `docs/specs/ccxml-redirect-design.md`

## Global Constraints

- Require exactly one nonempty quoted-string `dest` and no other attributes.
- Default exclusively to the current event `connection_id`.
- Do not add an expression engine, URI backend, connection registry, bridge
  registry, or synthesized result events.
- Append `prepare_redirect` after `prepare_reject` and preserve safe reads of
  every older size-versioned adapter prefix.
- Copy destination bytes into the existing bounded program storage.
- Preserve document-order prepare/commit and reverse-order discard.
- Run Windows presets inside the Visual Studio x64 developer environment.

---

### Task 1: Compile bounded redirect destinations

**Files:**
- Modify: `tests/ccxml_program_test.c`
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_program.c`

**Interfaces:**
- Consumes: `ccxml_compile`, `ccxml_limits.max_name_bytes`, destination-bearing
  compact action rows.
- Produces: `CCXML_ACTION_REDIRECT`, copied `destination` bytes, and
  `ccxml_program_impl.uses_redirect`.

- [x] **Step 1: Write failing compiler tests**

Add a valid literal `tel:` destination case and independent missing, empty,
nonliteral, escaped, explicit `connectionid`, and nested-content cases. The
valid case must also overwrite the source after compilation so later runtime
coverage can depend only on program-owned bytes.

- [x] **Step 2: Run focused compiler RED**

Build `ccxml_program_test` and run `ctest -R ccxml_program` through
`win-release-user`. The valid redirect must fail as
`CCXML_UNSUPPORTED_FEATURE` before implementation.

- [x] **Step 3: Implement destination validation and copying**

Generalize the existing create-call literal validator only as far as needed to
admit redirect with the same quote/escape/storage rules. Add the action kind,
copy the unquoted bytes in the second pass, and set `uses_redirect`; preserve
create-call behavior and diagnostics.

- [x] **Step 4: Run focused compiler GREEN and commit**

Rebuild and run the compiler tests without warnings, then commit the compiler
and tests as `feat(ccxml): compile bounded redirect destinations`.

### Task 2: Dispatch redirect transactions

**Files:**
- Modify: `tests/ccxml_session_test.c`
- Modify: `include/ccxml/ccxml.h`
- Modify: `src/ccxml_session.c`

**Interfaces:**
- Consumes: `CCXML_ACTION_REDIRECT`, copied destination, current connection ID,
  `uses_redirect`, and `cflow_statechart_effect_ticket`.
- Produces: `ccxml_redirect_request` and append-only
  `ccxml_telephony_adapter_v1.prepare_redirect`.

- [x] **Step 1: Write failing runtime and ABI tests**

Record both borrowed request fields and add independent cases for exact copied
bytes, missing/embedded-NUL connection IDs, rollback after a prior create-call,
document-order mixed commit, provider refusal, reject-era adapter-prefix
compatibility, and absent/NULL/truncated redirect callback tails.

- [x] **Step 2: Build runtime RED**

Build `ccxml_session_test`. Compilation must fail because the public redirect
request and callback field do not exist.

- [x] **Step 3: Implement the append-only redirect command**

Declare the two-field borrowed request, append `prepare_redirect`, validate its
complete field only for redirect programs, and dispatch through shared
connection validation and ticket retention. Do not mutate termination state or
synthesize outcome events.

- [x] **Step 4: Run focused CCXML GREEN and commit**

Build both CCXML test targets and run `ctest -R ccxml_`; all cases must pass
without warnings. Commit public API, runtime, and tests as
`feat(ccxml): dispatch transactional redirect`.

### Task 3: Document, verify, and publish

**Files:**
- Modify: `README.md`
- Create: `docs/specs/ccxml-redirect-design.md`
- Create: `docs/superpowers/plans/2026-09-03-ccxml-redirect.md`

**Interfaces:**
- Consumes: completed compiler/runtime contract.
- Produces: installed API documentation and updated PR #35.

- [x] **Step 1: Document the redirect incubation boundary**

Document literal destination syntax, default connection targeting,
provider-owned state/bridge handling, asynchronous outcomes, appended callback,
and unsupported optional attributes.

- [x] **Step 2: Run the full verification matrix**

Fresh-configure, build, and test Release, Debug/ASan, CHTTP, and QuickJS user
presets. Install Release through `install-win-release-user`; configure the
install consumer with the matching Rocida root and vcpkg prefix, then build and
run core C, CCXML C, and CCXML C++ consumers with matching runtime paths.
Expected counts are 13/13, 13/13, 18/18, and 13/13; consumers exit zero.

- [x] **Step 3: Review, publish, and finalize**

Run `git diff --check`, scan source/tests for focused tests and placeholder
markers, and review every change against the spec. Commit documentation, push,
update PR #35, verify `OPEN / CLEAN / MERGEABLE`, then mark this plan complete
in a final documentation commit and push it.
