# SCXML Invoke Materialization Conformance Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to execute this plan task-by-task in the existing isolated worktree.

**Goal:** Promote W3C mandatory tests 215, 216, 220, 225, 226, 530, and 554 with deterministic public-path witnesses for runtime invoke argument materialization and no-start-on-error behavior.

**Architecture:** CFlow owns active configuration, staged CMeta state, queues, and macrostep publication. TurboSCXML owns compiled invocation descriptors, session rows, token/ID generation, expression evaluation, payload/content materialization, and versioned adapter dispatch. The host owns canonical SCXML child interpreters and external services; it receives only fully materialized bounded requests and reports child events through existing public APIs.

**Characterization gate:** Production code already appears to evaluate dynamic strings and payload/content at the stable invocation boundary and to stage `error.execution` without calling the adapter on evaluation failure. Add strict direct and W3C tests first. If they pass, keep production unchanged. If a direct regression fails, change only the owning analyzer/runtime layer exposed by that failure.

**State and failure contract:** Every actual invoke execution consumes a unique nonzero session token. Generated IDs derive from owner state plus token and are written only to staged state. All arguments are evaluated after `onentry` against that staged state and before `prepare_start`. Any argument error yields no start callback, one failed row, and one internal `error.execution`; transaction failure discards every prepared external effect.

**Compatibility:** No public API/ABI, dependency, CMake target, data format, or deployment change is expected. The platform-level SCXML processor remains a host adapter responsibility.

**Spec:** `docs/specs/scxml-invoke-materialization-design.md`

## Global Constraints

- Work in `feat/w3c-invoke-materialization`; preserve the untracked roadmap in the main checkout.
- Keep `TurboSCXML -> installed TurboUtils` as the only dependency direction and never add uSCXML to build/runtime paths.
- Keep one CFlow configuration fact source and one TurboSCXML invocation-row fact source.
- Keep all queues, payloads, content views, IDs, and effect storage bounded.
- Invoke adapter request memory is borrowed only for the prepare call; callbacks run outside session locks.
- `tests/w3c/manifest.tsv` remains the single corpus fact source with 202 rows: 168 mandatory and 34 optional.
- A row becomes `PASS` only after its local fixture terminates through the public TurboSCXML path.

---

### Task 1: Freeze the materialization contract

**Files:**
- Create: `docs/specs/scxml-invoke-materialization-design.md`
- Test: `tests/scxml_cmeta_test.c`

- [x] Commit the focused design and this executable plan before changing tests.
- [x] Add or strengthen one strict adapter characterization that observes runtime type/src, canonical type, unique IDs, named payload/content, commit/discard counts, and zero starts after an invocation argument error.
- [x] Run only `scxml_cmeta_test`; if RED, preserve the exact output before changing production.
- [x] If and only if RED proves a semantic gap, make the smallest change in `src/scxml_analyze.c` or `src/scxml_runtime.c`, then rerun the direct test.

### Task 2: Register the seven conformance cases and capture RED

**Files:**
- Modify: `tests/scxml_w3c_conformance_test.c`

- [x] Add named TinyTest registrations for 215, 216, 220, 225, 226, 530, and 554 before creating fixtures.
- [x] Reuse or add bounded strict invoke adapter helpers; do not simulate success before the prepared start ticket commits.
- [x] Build and run the W3C executable, recording that missing fixture files cause the expected RED.

### Task 3: Add faithful bounded W3C transformations

**Files:**
- Create: `tests/w3c/test215.scxml`
- Create: `tests/w3c/test216.scxml`
- Create: `tests/w3c/test220.scxml`
- Create: `tests/w3c/test225.scxml`
- Create: `tests/w3c/test226.scxml`
- Create: `tests/w3c/test530.scxml`
- Create: `tests/w3c/test554.scxml`
- Test: `tests/scxml_w3c_conformance_test.c`

- [x] Preserve each normative assertion while removing only generator metadata, timeouts, and `conf:pass`/`conf:fail` extensions.
- [x] For 215/216, change the CMeta value in `onentry` and require the adapter to see only the runtime type/src.
- [x] For 220/226, require canonical type and have the committed host adapter report the child result; for 226 also require exact src and named param data.
- [x] For 225, require two starts with distinct generated IDs/tokens in document order.
- [x] For 530, mutate the value in `onentry` and require the materialized content to contain the new value.
- [x] For 554, use a compile-valid expression that fails only at execution, require zero start callbacks, and terminate by consuming `error.execution`.
- [x] Run focused W3C filters and the complete W3C executable until GREEN.

### Task 4: Promote corpus facts and verify

**Files:**
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `docs/superpowers/plans/2026-08-31-scxml-invoke-materialization-conformance.md`

- [x] Promote only the seven proven rows to `PASS/TERMINAL_PASS` and update prose counts from 118/50 to 125/43 without changing 202 total or 168/34 classification.
- [x] Mark completed plan checkboxes only after their evidence exists.
- [x] Run fresh Release configure/build, all 8 CTest targets, manifest accounting, CodeGraph sync/affected, and `git diff --check`.
- [x] Review public-boundary compatibility, error behavior, fixture provenance, and remaining risks before committing the implementation batch.
