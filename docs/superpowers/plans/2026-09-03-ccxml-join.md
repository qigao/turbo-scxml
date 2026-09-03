# CCXML Join Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded full-duplex `<join>` compilation and transactional
provider dispatch for two copied literal resource identifiers.

**Architecture:** Extend compact CCXML actions with two program-owned resource
views and append a size-guarded join command to the telephony adapter. The core
owns syntax, bounded storage, and effect transactions; the provider owns
resource validation, bridge/media state, and asynchronous outcomes.

**Tech Stack:** C11, Turbo XML parser, CFlow effect tickets, CMake, TinyTest

**Spec:** `docs/specs/ccxml-join-design.md`

## Global Constraints

- Admit exactly `id1` and `id2`, each once and each a nonempty quoted literal.
- Implement only the omitted-`duplex` full-duplex default.
- Reject optional join attributes, nested content, escapes, and expressions.
- Copy both decoded identifiers into the existing bounded program storage.
- Append `prepare_join` after `prepare_redirect`; never read beyond the
  caller-provided `struct_size`.
- Preserve document-order prepare/commit and reverse-order discard.
- Keep registries, ownership checks, bridge/media state, and result events in
  the provider.
- Run Windows builds inside the Visual Studio x64 developer environment.

---

### Task 1: Compile bounded join identifiers

**Files:**
- Modify: `tests/ccxml_program_test.c`
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_program.c`

**Interfaces:**
- Consumes: `ccxml_compile`, `ccxml_limits.max_name_bytes`, compact action rows.
- Produces: `CCXML_ACTION_JOIN`, two copied resource views, and `uses_join`.

- [x] **Step 1: Write compiler behavior tests**

Add a TinyTest `group("join")` whose valid case compiles this literal and then
overwrites its source buffer:

```c
char source[] =
    "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
    "<eventprocessor><transition event='conference.request'>"
    "<join id1=\"'call-a'\" id2=\"'call-b'\"/>"
    "</transition></eventprocessor></ccxml>";
```

Add independent cases for missing `id1`, missing `id2`, empty `id1`, empty
`id2`, nonliteral and escaped values, duplicate/extra attributes, nested
content, and a `max_name_bytes` value one byte below the storage required by
the event plus both decoded IDs and their NUL terminators. Each case asserts a
literal status value rather than an implementation detail; the duplicate
unqualified XML attribute expects `CCXML_XML_ERROR` from XML parsing.

- [x] **Step 2: Run compiler RED**

Build `ccxml_program_test` with `win-release-user` and run
`ctest --preset win-release-user -R ccxml_program --output-on-failure`.
The valid join must return `CCXML_UNSUPPORTED_FEATURE` before implementation.

- [x] **Step 3: Implement strict two-literal validation**

Add these private fields and enum member:

```c
CCXML_ACTION_JOIN
const char *id1;
size_t id1_size;
const char *id2;
size_t id2_size;
bool uses_join;
```

Introduce a small literal validator shared by destination and join attributes.
Add a join-specific attribute scan that accepts exactly one unqualified `id1`
and `id2`, validates both before charging storage, rejects non-ignorable
children, and uses checked additions for two `size + 1` retained ranges.

- [x] **Step 4: Copy both decoded identifiers**

In the compiler's second pass, locate both validated attributes, set
`CCXML_ACTION_JOIN` and `uses_join`, strip their outer quotes, and copy each
range plus trailing NUL into the program storage cursor. Do not alias XML input
or reuse the event connection identifier.

- [x] **Step 5: Run compiler GREEN and commit**

Rebuild and rerun the focused compiler test with zero failures and no compiler
warnings. Commit the compiler slice as:

```text
feat(ccxml): compile bounded join endpoints
```

### Task 2: Dispatch transactional join commands

**Files:**
- Modify: `tests/ccxml_session_test.c`
- Modify: `include/ccxml/ccxml.h`
- Modify: `src/ccxml_session.c`

**Interfaces:**
- Consumes: `CCXML_ACTION_JOIN`, copied IDs, `uses_join`, effect tickets.
- Produces: `ccxml_join_request` and
  `ccxml_telephony_adapter_v1.prepare_join`.

- [x] **Step 1: Write runtime and ABI tests**

Extend the provider probe with ordered join ID buffers and a
`PROVIDER_JOIN` kind. Add a callback with the intended signature before the
public declaration exists:

```c
static scxml_adapter_status prepare_join(
    void *user, const ccxml_join_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error);
```

Add independent tests for exact copied request bytes, source overwrite,
session non-termination, document-order mixed commit, reverse rollback after a
join refusal, redirect-era prefix compatibility, and missing/NULL/truncated
join callback tails.

- [x] **Step 2: Build runtime RED**

Build `ccxml_session_test`. Compilation must fail because
`ccxml_join_request` and `prepare_join` do not yet exist in the public API.

- [x] **Step 3: Append and validate the join capability**

Declare the public borrowed request and append the callback exactly as:

```c
typedef struct ccxml_join_request {
    const char *id1;
    size_t id1_size;
    const char *id2;
    size_t id2_size;
} ccxml_join_request;
```

During session initialization, require the complete appended field and a
non-NULL callback only when `uses_join` is true. Keep bounded adapter copying
and every older prefix rule unchanged.

- [x] **Step 4: Dispatch through the existing transaction**

For `CCXML_ACTION_JOIN`, construct the borrowed request from program storage,
call `prepare_join`, and pass its result to `retain_ticket`. Do not inspect the
current Event connection, mutate termination state, create a registry, or
synthesize conference events.

- [x] **Step 5: Run runtime GREEN and commit**

Build both CCXML test targets and run
`ctest --preset win-release-user -R ccxml_ --output-on-failure`. Commit as:

```text
feat(ccxml): dispatch transactional join
```

### Task 3: Document, verify, and publish

**Files:**
- Modify: `README.md`
- Create: `docs/specs/ccxml-join-design.md`
- Create: `docs/superpowers/plans/2026-09-03-ccxml-join.md`

**Interfaces:**
- Consumes: completed compiler/runtime contract.
- Produces: installed API documentation and an updated PR #35.

- [x] **Step 1: Document the incubation boundary**

Add the exact XML form, full-duplex default, provider ownership and event
responsibility, append-only callback rule, and unsupported optional attributes
to README. Link this design from the CCXML section.

- [x] **Step 2: Run the complete verification matrix**

Fresh-configure, build, and test `win-release-user`, `win-dev-user`,
`win-release-chttp-user`, and `win-release-quickjs-user`. Expected CTest totals
are 13/13, 13/13, 18/18, and 13/13. Install with
`install-win-release-user`, then configure, build, and run the installed core
C, CCXML C, and CCXML C++ consumers with the matching Rocida and vcpkg runtime
paths; all three must exit zero.

- [x] **Step 3: Review, publish, and finalize**

Run `git diff --check`, scan changed source and tests for `fit(`, `it_only(`,
`TODO`, `FIXME`, and `HACK`, and review the diff against every design
requirement. Commit documentation, push the branch, update PR #35, verify
`OPEN / CLEAN / MERGEABLE`, then check this final step in one last docs commit
and push it.
