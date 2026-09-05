# CCXML CMeta Descriptor Registry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Allow a CCXML CMeta datamodel to supply semantic descriptors for
trivial structured sequence elements so foreach conditions can read
`item.member` without adding an artificial root field.

**Architecture:** Extend the size-versioned CMeta config with an optional tail
containing borrowed semantic descriptor pointers. The datamodel owner copies
the pointer table, resolves entries by `cmeta_type_equal`, and keeps descriptor
objects borrowed. Foreach retains the existing hard storage bound and rejects
managed element types.

**Tech Stack:** C11, Salts CMeta/CSTL, CCXML/SCXML expression runtime,
TinyTest, CMake presets.

**Spec:** `docs/specs/ccxml-core-mvp-design.md`

## Global Constraints

- Keep `CCXML_DATAMODEL_ADAPTER_ABI_V1` and its callback table unchanged.
- Accept the original `ccxml_cmeta_datamodel_config_v1` prefix by
  `struct_size`; only read the registry tail when the full tail is present.
- Compare registered storage types with `cmeta_type_equal`, never descriptor
  address identity.
- Copy the pointer table into the datamodel owner; descriptor objects remain
  borrowed through every attached session.
- Continue rejecting managed-copy foreach elements because lifecycle hooks can
  allocate outside `max_foreach_storage_bytes`.
- Do not commit, merge, or push until the user explicitly requests it.

---

### Task 1: Size-versioned registry contract

**Files:**
- Modify: `include/ccxml/ccxml.h`
- Modify: `src/ccxml_cmeta.c`
- Test: `tests/ccxml_cmeta_test.c`

**Interfaces:**
- Consumes: the existing v1 config prefix ending at `max_string_bytes`.
- Produces: optional `semantic_data` and `semantic_data_count` tail fields;
  owner-held `const cmeta_data_desc **` pointer storage.

- [x] **Step 1: Write the failing compatibility and validation tests**

```c
config.struct_size = offsetof(
    ccxml_cmeta_datamodel_config_v1, semantic_data);
check_equal(ccxml_cmeta_datamodel_init(&datamodel, &config), CCXML_OK);
```

Add invalid cases for count-without-array, array-without-count, invalid
descriptors, and duplicate semantic storage types.

- [x] **Step 2: Run the focused test and verify RED**

Run `cmake --build --preset win-release-user --target ccxml_cmeta_test` through
`VsDevCmd.bat`, then `ctest --preset win-release-user -R ccxml_cmeta_test
--output-on-failure`. Expected: compile failure because the tail fields do not
exist.

- [x] **Step 3: Implement the minimal size-gated config tail**

```c
const cmeta_data_desc *const *semantic_data;
size_t semantic_data_count;
```

Treat `offsetof(config, semantic_data)` as the accepted legacy prefix size.
Validate the full tail before reading it, checked-allocate the owner pointer
table, and reject semantically duplicate storage types.

- [x] **Step 4: Run the focused test and verify GREEN**

Run the same build and CTest filter; expected `ccxml_cmeta_test` passes.

### Task 2: Structured foreach condition resolution

**Files:**
- Modify: `src/ccxml_cmeta.c`
- Test: `tests/ccxml_cmeta_test.c`

**Interfaces:**
- Consumes: the copied registry from Task 1 and `scxml_scope_find_data_for_type`.
- Produces: registry-first semantic resolution by `cmeta_type_equal`.

- [x] **Step 1: Write the failing real-session test**

```c
static const cmeta_data_desc *const registry[] = {&record_data};
/* Vec<Record>{11,22,33}; only record.code == 22 prepares an accept. */
config.semantic_data = registry;
config.semantic_data_count = 1u;
check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
check_equal(provider.prepare_count, (size_t)1u);
```

Also assert the committed supplemental record contains `code == 33` and the
staged view is unbound after commit.

- [x] **Step 2: Run the focused test and verify RED**

Expected: session admission returns invalid argument because the root-only
resolver cannot find the record semantic descriptor.

- [x] **Step 3: Implement registry-first type resolution**

```c
for (i = 0; i < impl->semantic_data_count; ++i)
    if (cmeta_type_equal(impl->semantic_data[i]->storage_type, type))
        return impl->semantic_data[i];
return scxml_scope_find_data_for_type(impl->root, type,
                                      impl->max_path_depth);
```

Use the result when registering the foreach item scope. Do not weaken the
trivial lifecycle or root-alias checks.

- [x] **Step 4: Run focused CCXML tests and verify GREEN**

Run `ccxml_cmeta_test`, `ccxml_program_test`, and `ccxml_session_test`; expected
all pass.

### Task 3: Documentation, review, and full verification

**Files:**
- Modify: `README.md`
- Modify: `docs/specs/ccxml-core-mvp-design.md`
- Modify: `docs/superpowers/plans/2026-09-04-ccxml-foreach.md`

**Interfaces:**
- Consumes: the tested registry ownership and compatibility behavior.
- Produces: documented initialization example, lifetime contract, and current
  managed-element restriction.

- [x] **Step 1: Document the optional registry tail and borrowed lifetime**

Show a `const cmeta_data_desc *const[]` registry in the README and state that
the owner copies the pointer array while descriptor objects outlive sessions.

- [x] **Step 2: Request read-only code review and fix Critical/Important issues**

Review config prefix compatibility, overflow, duplicate detection, multi-TU
type equality, descriptor lifetime, and structured staged-condition behavior.

- [x] **Step 3: Run fresh Release verification**

Run `git diff --check`, then through `VsDevCmd.bat` run `cmake --fresh --preset
win-release-user`, `cmake --build --preset win-release-user`, and `ctest
--preset win-release-user --output-on-failure`. Expected: 15/15 tests pass.
