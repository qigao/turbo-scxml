# SCXML Conformance Continuation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote the first three mandatory W3C SCXML IRP documents whose semantics are already implemented from `UNSUPPORTED` to executable, provenance-preserving local `PASS` fixtures.

**Architecture:** Keep `tests/w3c/manifest.tsv` as the corpus fact source and `tests/scxml_w3c_conformance_test.c` as the strict executor. Each local fixture removes only generator metadata, timeout safety nets, and `conf:pass`/`conf:fail`; it preserves the semantic witness using ordinary SCXML states, events, `In(id)`, and terminal `pass`/`fail` states. No production API or runtime behavior changes are planned; if a fixture exposes a runtime defect, its failing fixture becomes the regression test before the minimal runtime fix.

**Tech Stack:** C11, TurboSCXML, TurboUtils CFlow/CMeta, TinyTest, CMake/CTest presets.

**Spec:** `docs/specs/scxml-core-design.md`; normative corpus source is the W3C SCXML Implementation Report suite linked by each manifest row; tracking issue is `qigao/turbo-utils#122`.

## Global Constraints

- Preserve `tests/w3c/manifest.tsv` as the single machine-readable corpus fact source with exactly 202 rows: 168 mandatory and 34 optional.
- A `PASS` row must name an existing nonempty fixture, use `TERMINAL_PASS`, describe a non-`NONE` transformation, and have synchronized README provenance.
- Preserve the named upstream assertion; do not weaken it merely to make the local harness pass.
- Keep the library fail-fast and format-neutral; do not add HTTP, QuickJS, persistence, or server behavior.
- Do not change production code unless an added fixture first fails because of a demonstrated runtime defect.
- Run the focused W3C executable first, then all eight CTest targets.
- Work directly on `main` as explicitly requested; do not push or merge externally as part of this plan.

---

### Task 1: Default and root initial configuration evidence

**Files:**
- Create: `tests/w3c/test364.scxml`
- Create: `tests/w3c/test413.scxml`
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `tests/scxml_w3c_conformance_test.c`

**Interfaces:**
- Consumes: `check_w3c_fixture(const char *fixture_name)` and the existing null-datamodel SCXML compiler/session path.
- Produces: two strict terminal fixtures and manifest totals of 38 `PASS`, 130 `UNSUPPORTED`, and 34 `N/A`.

- [x] **Step 1: Promote the two manifest rows before adding fixtures**

  Set rows 364 and 413 to `PASS`, `TERMINAL_PASS`, and a non-`NONE` local rewrite description. Add their exact fixture/source links and assertion summaries to the README. Change the inventory assertions to:

  ```c
  check_equal(stats.passed, (size_t)38u);
  check_equal(stats.unsupported, (size_t)130u);
  check_equal(stats.not_applicable, (size_t)34u);
  ```

- [x] **Step 2: Run the focused test and observe the expected RED state**

  Run:

  ```powershell
  cmake --build --preset win-release-user --target scxml_w3c_conformance_test
  ctest --preset win-release-user -R "^scxml_w3c_conformance_test$" --output-on-failure
  ```

  Expected: the strict inventory test fails because `test364.scxml` and `test413.scxml` do not yet exist.

- [x] **Step 3: Add `test364.scxml` preserving all three default-entry cases**

  The local document must execute this exact chain:

  1. Root `initial="s1"` enters compound `s1` whose `initial` IDREFS select `s11p112` and `s11p122` under one parallel descendant; entry of `s11p112` raises `entered.s11p112`, and the active sibling handles it by targeting `s2`.
  2. `s2` uses an explicit `<initial><transition target="s21p112 s21p122"/></initial>`; entry of `s21p112` raises `entered.s21p112`, and the active sibling targets `s3`.
  3. `s3`, `s31`, and `s311` omit initial declarations; document-order first-child descent reaches `s3111`, whose eventless transition targets terminal `pass`. Every alternate atomic child has an eventless transition to terminal `fail`.

  Use `datamodel="null"`; omit upstream timeout sends and generator-only `conf:*` nodes.

- [x] **Step 4: Add `test413.scxml` preserving root multi-target initial selection**

  Root `initial="s2p112 s2p122"` must enter both non-default leaves beneath separate regions of `s2p1`. Entry of `s2p112` raises `entered.s2p112`; only active sibling `s2p122` handles that event and targets terminal `pass`. Default leaves and unrelated root child `s1` have eventless transitions to terminal `fail`.

- [x] **Step 5: Register the two executable cases and verify GREEN**

  Add exactly:

  ```c
  it("test 364 enters every declared or document-order default") {
      check_w3c_fixture("test364.scxml");
  }
  it("test 413 starts in the root initial configuration") {
      check_w3c_fixture("test413.scxml");
  }
  ```

  Rebuild and run the focused CTest. Expected: pass with 38/130/34 inventory totals.

- [x] **Step 6: Commit the independently verified task**

  ```powershell
  git add tests/w3c/test364.scxml tests/w3c/test413.scxml tests/w3c/manifest.tsv tests/w3c/README.md tests/scxml_w3c_conformance_test.c
  git commit -m "test: expand SCXML initial configuration corpus"
  ```

### Task 2: Stored shallow and deep history evidence

**Files:**
- Create: `tests/w3c/test388.scxml`
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`
- Modify: `tests/scxml_w3c_conformance_test.c`

**Interfaces:**
- Consumes: the native active configuration as the `In(id)` fact source and `check_w3c_fixture(const char *fixture_name)`.
- Produces: one strict stored-history fixture and manifest totals of 39 `PASS`, 129 `UNSUPPORTED`, and 34 `N/A`.

- [ ] **Step 1: Promote row 388 before adding its fixture**

  Set row 388 to `PASS`/`TERMINAL_PASS`, document the local event-and-`In(id)` rewrite, add the exact fixture/source link to README, and change inventory totals to 39/129/34.

- [ ] **Step 2: Run the focused test and observe RED**

  Expected: strict inventory validation fails because `test388.scxml` is absent.

- [ ] **Step 3: Add the stored-history fixture**

  Use root `initial="s012"` to visit deep leaf `s012` under `s0/s01`, then leave `s0`. An eventless transition from `s1` targets `s0HistDeep` while raising `restore.deep`; `s0` accepts that event only when `In(s012)` and otherwise targets `fail`. It then exits to `s2`, whose eventless transition targets `s0HistShallow` while raising `restore.shallow`; `s0` accepts that event only when `In(s011)` and targets terminal `pass`, otherwise `fail`. Declare default history transitions to different `s02` descendants so accidentally using an unset/default history path cannot pass.

- [ ] **Step 4: Register and run the fixture**

  Add exactly:

  ```c
  it("test 388 restores stored shallow and deep configurations") {
      check_w3c_fixture("test388.scxml");
  }
  ```

  Run the focused target and CTest. If the fixture exposes a production defect, retain the failing fixture, diagnose the violated history invariant, and make only the minimal runtime correction.

- [ ] **Step 5: Run adjacent and full regression tests**

  ```powershell
  ctest --preset win-release-user -R "^scxml_(w3c_conformance_)?test$" --output-on-failure
  ctest --preset win-release-user --output-on-failure
  ```

  Expected: 8/8 pass and inventory totals are 39/129/34.

- [ ] **Step 6: Commit the independently verified task**

  ```powershell
  git add tests/w3c/test388.scxml tests/w3c/manifest.tsv tests/w3c/README.md tests/scxml_w3c_conformance_test.c
  git commit -m "test: cover stored SCXML history restoration"
  ```

### Task 3: Reconcile tracking and select the next batch

**Files:**
- Modify: `docs/superpowers/plans/2026-08-31-scxml-conformance-continuation.md`
- External tracker: `https://github.com/qigao/turbo-utils/issues/122`

**Interfaces:**
- Consumes: verified 39/129/34 manifest counts and the complete 202-row inventory.
- Produces: checked plan steps, an evidence-backed #122 progress comment, and a next-batch feature/count table without changing #122's full-conformance checkbox.

- [ ] **Step 1: Recompute counts from the manifest**

  Parse the TSV and record total, mandatory, optional, PASS, UNSUPPORTED, and N/A counts. Do not derive the claim from README prose.

- [ ] **Step 2: Update #122 with exact evidence**

  Add a concise comment naming tests 364, 388, and 413, the local CTest result, and the new 39/129/34 counts. Keep “Run an SCXML conformance corpus” unchecked because 129 mandatory documents remain unsupported.

- [ ] **Step 3: Select the next implementation batch**

  Group remaining mandatory `UNSUPPORTED` rows by feature and document the smallest next batch whose semantics are already present or whose missing host/runtime boundary is singular. Prefer completion/final semantics (372, 570, 415) before mixed transport/invoke groups.
