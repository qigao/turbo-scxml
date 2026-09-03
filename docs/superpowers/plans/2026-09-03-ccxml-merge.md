# CCXML Merge Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded `<merge>` compilation and transactional provider
dispatch for two copied literal connection identifiers.

**Architecture:** Reuse the compact action row's two identifier views and
append a size-guarded merge command to the telephony adapter. The core owns
syntax, bounded storage, and effect transactions; the provider owns connection
validation, signaling, bridge/media teardown, and asynchronous outcomes.

**Tech Stack:** C11, Turbo XML parser, CFlow effect tickets, CMake, TinyTest

**Spec:** `docs/specs/ccxml-merge-design.md`

## Global Constraints

- Admit exactly `connectionid1` and `connectionid2`, once each, as nonempty
  quoted string literals.
- Reject `hints`, nested content, escapes, and arbitrary expressions.
- Copy both decoded identifiers into existing bounded program storage.
- Append `prepare_merge` after `prepare_unjoin` and never read beyond the
  caller-provided `struct_size`.
- Preserve document-order prepare/commit and reverse-order discard.
- Keep connection/bridge state, ownership checks, network signaling, media
  teardown, and result events in the provider.
- Run Windows builds inside the Visual Studio x64 developer environment.

---

### Task 1: Compile bounded merge identifiers

**Files:**
- Modify: `tests/ccxml_program_test.c`
- Modify: `src/ccxml_internal.h`
- Modify: `src/ccxml_program.c`

**Interfaces:**
- Consumes: `ccxml_compile`, `ccxml_limits.max_name_bytes`, compact actions.
- Produces: `CCXML_ACTION_MERGE`, copied `id1`/`id2` views, `uses_merge`.

- [x] **Step 1: Write compiler behavior tests**

Add a TinyTest `group("merge")`. Its valid source contains:

```c
"<merge connectionid1=\"'call-a'\" connectionid2=\"'call-b'\"/>"
```

Compile from a mutable source, overwrite it, and assert one retained action.
Add independent cases for missing `connectionid1`, missing `connectionid2`,
empty values, a nonliteral expression, an escaped literal, duplicate XML
attributes, `hints`, nested executable content, and `max_name_bytes = 32` for
an event plus identifiers that require 33 bytes including NUL terminators.
Duplicate XML attributes expect `CCXML_XML_ERROR`; missing/empty fields expect
`CCXML_INVALID_STRUCTURE`; unsupported forms expect
`CCXML_UNSUPPORTED_FEATURE`; the budget case expects `CCXML_LIMIT_EXCEEDED`.

- [x] **Step 2: Run compiler RED**

Build target `ccxml_program_test` using `win-release-user`, then run
`ctest --preset win-release-user -R ccxml_program --output-on-failure`.
The valid merge and structure-specific cases must fail because merge is still
reported as unsupported executable content.

- [x] **Step 3: Add strict two-literal validation**

Append `CCXML_ACTION_MERGE` and `bool uses_merge`. Generalize the private
two-literal action validator to accept the two required local attribute names,
so join/unjoin continue using `id1`/`id2` while merge uses `connectionid1` and
`connectionid2`. It must validate both attributes before charging storage,
reject non-ignorable children, and checked-add both decoded sizes plus NULs.

- [x] **Step 4: Copy the merge identifiers**

Admit `merge` in transition validation. In the compiler's second pass, select
the merge kind and feature bit, locate both named attributes, strip exactly one
pair of expression quotes, and copy them to the action row's existing `id1`
and `id2` views. Never alias XML input or current-event data.

- [x] **Step 5: Run compiler GREEN and commit**

Rebuild and rerun the focused compiler test with no failures or warnings, run
`git diff --check`, and commit only compiler files as:

```text
feat(ccxml): compile bounded merge connections
```

### Task 2: Dispatch transactional merge commands

**Files:**
- Modify: `tests/ccxml_session_test.c`
- Modify: `include/ccxml/ccxml.h`
- Modify: `src/ccxml_session.c`

**Interfaces:**
- Consumes: `CCXML_ACTION_MERGE`, copied IDs, `uses_merge`, effect tickets.
- Produces: `ccxml_merge_request` and
  `ccxml_telephony_adapter_v1.prepare_merge`.

- [x] **Step 1: Write runtime and ABI tests**

Extend the provider probe with two merge buffers and `PROVIDER_MERGE`. Add a
callback using the intended public shape before the declaration exists:

```c
static scxml_adapter_status prepare_merge(
    void *user, const ccxml_merge_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error);
```

Add independent tests for exact copied request bytes after source overwrite,
dispatch without an Event connection ID, session non-termination, join then
merge document-order commit, reverse rollback when the third merge prepare is
refused, unjoin-era prefix compatibility, and missing, NULL, and one-byte-
truncated merge callback tails. Adjust the unjoin truncation test to end one
byte before the complete `prepare_unjoin` field after the struct grows.

- [x] **Step 2: Build runtime RED**

Build target `ccxml_session_test`. Compilation must fail because
`ccxml_merge_request` and `prepare_merge` do not yet exist in the public API.

- [x] **Step 3: Append and validate the merge capability**

Declare the borrowed request exactly as:

```c
typedef struct ccxml_merge_request {
    const char *connection_id1;
    size_t connection_id1_size;
    const char *connection_id2;
    size_t connection_id2_size;
} ccxml_merge_request;
```

Append `prepare_merge` after `prepare_unjoin`. During session initialization,
require the complete field and a non-NULL callback only when `uses_merge` is
true. Preserve bounded adapter copying and every older prefix rule.

- [x] **Step 4: Dispatch through the existing transaction**

For `CCXML_ACTION_MERGE`, construct the borrowed request from program storage,
call `prepare_merge`, and retain the returned ticket. Do not inspect the Event
connection ID, mutate termination state, create resource registries, or emit
connection/conference events in the core.

- [x] **Step 5: Run runtime GREEN and commit**

Build both CCXML test targets and run
`ctest --preset win-release-user -R ccxml_ --output-on-failure`. Run
`git diff --check` and commit the runtime files as:

```text
feat(ccxml): dispatch transactional merge
```

### Task 3: Document, verify, and publish

**Files:**
- Modify: `README.md`
- Create: `docs/specs/ccxml-merge-design.md`
- Create: `docs/superpowers/plans/2026-09-03-ccxml-merge.md`

**Interfaces:**
- Consumes: completed compiler/runtime merge contract.
- Produces: documented installed API and updated PR #35.

- [x] **Step 1: Document the incubation boundary**

Add the exact XML form, provider ownership, asynchronous success/failure event
responsibility, append-only callback rule, and unsupported `hints`/expression
forms to README. Link this design from the CCXML section.

- [x] **Step 2: Run the complete verification matrix**

Fresh-configure, build, and test `win-release-user`, `win-dev-user`,
`win-release-chttp-user`, and `win-release-quickjs-user`. Expected CTest totals
are 13/13, 13/13, 18/18, and 13/13. Install with
`install-win-release-user`, then fresh-configure/build/run the installed core C,
CCXML C, and CCXML C++ consumers; all three must exit zero.

- [x] **Step 3: Review, publish, and finalize**

Run `git diff --check`; scan changed source/tests for `fit(`, `it_only(`,
`TODO`, `FIXME`, and `HACK`; and review every design requirement against the
diff. Commit documentation, push the branch, update PR #35, verify
`OPEN / CLEAN / MERGEABLE`, check this final step in a final docs commit, push,
and recheck the PR state.
