# SCXML Invoke Completion Conformance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. This task explicitly forbids subagents.

**Goal:** Promote W3C mandatory tests 228, 232, 235, 236, and 247 with strict public-path evidence for returned Event provenance, FIFO ordering, exact completion identity, terminal token lifecycle, and host-owned child completion.

**Architecture:** TurboSCXML keeps one session-owned fixed invocation row as the token/ID fact source and CFlow keeps the bounded external FIFO. A strict test host copies committed start identity, reports Events through `scxml_session_report_invoke_event()`/`scxml_session_report_invoke_done()`, and for 247 owns a second real TurboSCXML session whose top-level final is the causal completion witness.

**Tech Stack:** C11, TurboSCXML public API, installed TurboUtils CFlow/CMeta/TinyTest, CMake user presets, MSVC Release and Debug/ASan.

**Spec:** `docs/specs/scxml-invoke-completion-design.md`

## Global Constraints

- Work only on `feat/w3c-invoke-completion` in the designated isolated worktree; do not push or merge.
- Keep `TurboSCXML -> installed TurboUtils`, one session owner, bounded copied Event admission, deterministic FIFO, explicit tokens, and host-owned child interpreters.
- Do not add global lookup, transport, threads, filesystem/network I/O, unbounded storage, fallback interpreters, or public ABI/API changes.
- Windows configure/build/test commands run through `VsDevCmd.bat` and public `win-release-user`/`win-dev-user` presets.
- Manifest remains exactly 202 documents: 168 mandatory and 34 optional; promotion happens only after executable GREEN evidence.

---

### Task 1: Add the strict completion harness and capture RED

**Files:**
- Modify: `tests/scxml_w3c_conformance_test.c`
- Create later, not before RED: `tests/w3c/test228.scxml`, `tests/w3c/test232.scxml`, `tests/w3c/test235.scxml`, `tests/w3c/test236.scxml`, `tests/w3c/test247.scxml`, `tests/w3c/test247-child.scxml`

**Interfaces:**
- Consumes: `scxml_session_report_invoke_event()`, `scxml_session_report_invoke_done()`, `scxml_session_get_invoke_stats()`, `scxml_session_get_stats()`.
- Produces: `run_w3c_invoke_completion_fixture(const char *, w3c_invoke_completion_case)` and five named TinyTest cases.

- [x] Add a dedicated probe with separate start prepare/commit/discard and cancel counts, copied exact ID/token, and terminal result effect counts.
- [x] Add an enum for the five fixed scenarios so the helper has only two behavior parameters.
- [x] In the helper, require one nonzero committed start and no discard, exact report statuses, scenario-specific order, exact result Event, exact invoke counters, `active == 0`, and successful cleanup.
- [x] For 247 only, compile and run `test247-child.scxml` in a second serial executor/session, require `child_stats.done && !child_stats.errored`, destroy it cleanly, and only then report exactly one parent completion.
- [x] Register named tests 228/232/235/236/247 while every candidate fixture is absent.
- [x] Run `cmake --build --preset win-release-user --target scxml_w3c_conformance_test`, then run the executable with the five focused filters; preserve failures caused by missing fixture files as RED.

### Task 2: Add the minimum faithful fixtures and reach focused GREEN

**Files:**
- Create: `tests/w3c/test228.scxml`
- Create: `tests/w3c/test232.scxml`
- Create: `tests/w3c/test235.scxml`
- Create: `tests/w3c/test236.scxml`
- Create: `tests/w3c/test247.scxml`
- Create: `tests/w3c/test247-child.scxml`
- Modify only if strict RED proves a production defect: `src/scxml_session.c`, `src/scxml_runtime.c`

**Interfaces:**
- Consumes: the strict helper from Task 1.
- Produces: deterministic terminal witnesses for all five normative assertions.

- [x] Test 228: explicit invoke ID `invoke228`; one completion Event returned by `scxml_session_report_invoke_done()`; CMeta guard requires `_event.invokeid == \"invoke228\"` and external type.
- [x] Test 232: states accept `childToParent1`, then `childToParent2`, then `done.invoke`; any out-of-order Event reaches fail.
- [x] Test 235: `done.invoke.foo` reaches pass only when `_event.name` is exactly `done.invoke.foo`; a same-event fallback reaches fail.
- [x] Test 236: accept normal child Event, then done, then only host `confirm`; any late returned Event reaches fail. The helper waits for completion, requires a later same-token report to be `INVALID_ARGUMENT`, then admits `confirm`.
- [x] Test 247: parent accepts done from its invoke; separate child fixture is a real top-level final document with no embedded interpreter in core.
- [x] Run each focused filter and then the full W3C executable. If a public-path semantic assertion fails, make only the smallest owning runtime correction and rerun the exact RED test before expanding.

### Task 3: Promote corpus facts and perform full verification

**Files:**
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `docs/superpowers/plans/2026-08-31-scxml-invoke-completion-conformance.md`
- Create: `.superpowers/sdd/2026-08-31-uscxml-reference-guided-development/task-4b-report.md`

**Interfaces:**
- Consumes: GREEN strict fixtures and test output.
- Produces: exact corpus accounting, review evidence, report, and one reviewable commit.

- [x] Change only rows 228/232/235/236/247 to `PASS` with the exact expected terminal witness and truthful transformation/rationale.
- [x] Recompute TSV counts independently and update README from 125 PASS / 43 UNSUPPORTED / 34 N/A to 130 PASS / 38 UNSUPPORTED / 34 N/A while retaining 202/168/34.
- [x] Run focused Release filters, the complete W3C executable, fresh full Release build, and `ctest --preset win-release-user`; require 8/8.
- [x] If `win-dev-user` configures, build the W3C target and run focused filters for 228/232/235/236/247 under Debug/ASan; otherwise record the exact configuration failure.
- [x] Run manifest calculation, `git diff --check`, focus-marker/placeholder scan, and self-review of public compatibility, ownership, ordering, errors, cleanup, and fixture provenance.
- [x] Write the report with status, files, commit, exact RED/GREEN results, manifest calculation, self-review, and concerns; commit once without push/merge.
