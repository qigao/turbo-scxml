# SCXML Invoke Autoforward Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose the complete borrowed SCXML Event envelope to invocation adapters and pass W3C mandatory autoforward tests 229 and 230.

**Architecture:** Keep `scxml_session_impl.system_values` as the only current-Event fact source and build a stack-local immutable envelope after event observation and finalize. Append the view to the existing forward request without changing the adapter table ABI, then exercise delivery through the existing transactional prepare/commit/discard boundary.

**Tech Stack:** C11, TurboSCXML typed AST/runtime, TurboUtils CMeta/CFlow, TinyTest, CMake Presets.

**Spec:** `docs/specs/scxml-invoke-autoforward-design.md`

## Global Constraints

- Preserve finalize-before-autoforward and descriptor-order forwarding.
- Keep `scxml_session_impl.system_values` as the sole current-Event fact source.
- Every envelope pointer is borrowed only for one `prepare_forward` callback.
- Add no allocation, queue, retry, fallback, dependency, or capacity.
- Preserve the existing accepted-ticket exactly-one commit-or-discard contract.
- Keep `SCXML_ADAPTER_ABI` at 1 because the adapter table and callback signature do not change.

---

### Task 1: Public Event-envelope contract

**Files:**
- Modify: `include/scxml/scxml.h`
- Test: `tests/scxml_test.c`

**Interfaces:**
- Consumes: existing `scxml_content_view`, `cflow_event_view`, and `scxml_invoke_forward_request`.
- Produces: `SCXML_EVENT_ENVELOPE_ABI`, `scxml_event_envelope_view`, and `scxml_invoke_forward_request.envelope`.

- [x] **Step 1: Write the failing callback contract test**

Extend `scxml_invoke_probe` with copied name/type/metadata/data buffers and make
`scxml_invoke_prepare_forward()` require:

```c
request->envelope != NULL &&
request->envelope->abi_version == SCXML_EVENT_ENVELOPE_ABI &&
request->envelope->struct_size == sizeof(scxml_event_envelope_view)
```

The existing test translation unit must fail to compile because the macro,
type, and request field do not yet exist.

- [x] **Step 2: Run the focused build to verify RED**

Run from a Visual Studio developer shell:

```powershell
cmake --build --preset win-release-user --target scxml_test
```

Expected: compile failure naming `SCXML_EVENT_ENVELOPE_ABI`,
`scxml_event_envelope_view`, or `scxml_invoke_forward_request.envelope`.

- [x] **Step 3: Add the minimal public view**

Add the exact ABI-1 structure from the design spec after
`scxml_event_metadata`; append:

```c
/** Complete borrowed SCXML Event copy, valid only for this callback. */
const scxml_event_envelope_view *envelope;
```

to `scxml_invoke_forward_request`. Update the adapter ownership comment so
retained envelope fields must be copied before return.

- [x] **Step 4: Rebuild to isolate the remaining runtime failure**

Run:

```powershell
cmake --build --preset win-release-user --target scxml_test
```

Expected: build succeeds; the direct runtime test added in Task 2 will provide
the behavioral RED boundary.

### Task 2: Runtime envelope projection and direct regression

**Files:**
- Modify: `src/scxml_runtime.c`
- Modify: `tests/scxml_test.c`

**Interfaces:**
- Consumes: `scxml_session_impl.system_values` after
  `scxml_runtime_observe_event()` and the ABI-1 public view from Task 1.
- Produces: a non-NULL `request.envelope` for every `prepare_forward` call.

- [x] **Step 1: Add a seven-field runtime assertion**

In the existing autoforward test, admit `tick` with
`scxml_session_try_send_with_metadata()` and these distinct values:

```c
send_id = "send-230"
origin = "scxml://parent/session"
origin_type = "http://www.w3.org/TR/scxml/#SCXMLEventProcessor"
invoke_id = "source-invoke-230"
data = "payload-230"
```

Require the probe to observe `name="tick"`, `type="external"`, and exact
copies of all five remaining values. Copy inside the callback; compare only
after executor idle to prove the test does not rely on borrowed lifetime.

- [x] **Step 2: Run the direct test to verify behavioral RED**

Run:

```powershell
ctest --preset win-release-user --output-on-failure -R '^scxml_test$'
```

Expected: FAIL because `request.envelope` is NULL.

- [x] **Step 3: Project system values into one stack-local envelope**

Add one private helper in `src/scxml_runtime.c` that maps the current
`scxml_expr_system_values` into:

```c
scxml_event_envelope_view envelope = {
    .abi_version = SCXML_EVENT_ENVELOPE_ABI,
    .struct_size = sizeof(scxml_event_envelope_view),
    /* six string views plus structured-or-text data */
};
```

Construct it once in `forward_external_to_invocations()` before the descriptor
loop and set `.envelope = &envelope` in every request. Use
`SCXML_CONTENT_CMETA` only when both schema and object are bound; otherwise use
`SCXML_CONTENT_TEXT_UTF8`, including an empty present view.

- [x] **Step 4: Run the direct regression to verify GREEN**

Run:

```powershell
cmake --build --preset win-release-user --target scxml_test
ctest --preset win-release-user --output-on-failure -R '^scxml_test$'
```

Expected: build succeeds and `scxml_test` passes.

### Task 3: W3C-derived tests 229 and 230

**Files:**
- Create: `tests/w3c/test229.scxml`
- Create: `tests/w3c/test230.scxml`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`

**Interfaces:**
- Consumes: live invocation tokens, `scxml_session_report_invoke_event()`,
  `scxml_session_try_send_with_metadata()`, and the complete envelope view.
- Produces: deterministic bounded host witnesses for W3C assertions 229/230.

- [x] **Step 1: Add fixtures and registrations before the host runner**

Create `test229.scxml` with one `autoforward="true"` invoke and a transition
from `eventReceived` to final `pass`. Create `test230.scxml` with the same
invoke and a transition from `fieldsEqual` to final `pass`. Register both in
the invoke group through a new `run_w3c_invoke_autoforward_fixture()` helper.

- [x] **Step 2: Run the W3C executable to verify RED**

Run:

```powershell
cmake --build --preset win-release-user --target scxml_w3c_conformance_test
ctest --preset win-release-user --output-on-failure -R '^scxml_w3c_conformance_test$'
```

Expected: FAIL because the unimplemented host runner cannot complete either
fixture in `pass`.

- [x] **Step 3: Implement the bounded transactional host probe**

Add a probe with one live token, copied envelope buffers, prepared/committed/
discarded counters, and an enum selecting test 229 or 230. Start tickets record
the token. Forward prepare copies the envelope; commit only marks the copied
reservation deliverable. After executor idle, the runner pumps exactly one
child Event: `eventReceived` for 229, or `fieldsEqual` only when all seven 230
fields match. Return Events use `scxml_session_report_invoke_event()`; no
callback admits an Event, calls `wait_idle`, or recursively advances the
session.

- [x] **Step 4: Mark corpus rows and document the transformation**

Change manifest rows 229 and 230 to `PASS` and `TERMINAL_PASS`, with a non-`NONE`
transformation naming the round-trip and seven-field witnesses. Add both rows
to the README table and explain that the invoked child is replaced by a bounded
host probe while the original assertions remain unchanged.

- [x] **Step 5: Run focused W3C and manifest verification**

Run:

```powershell
cmake --build --preset win-release-user --target scxml_w3c_conformance_test
ctest --preset win-release-user --output-on-failure -R '^scxml_w3c_(manifest|conformance)_test$'
```

Expected: both W3C tests pass; manifest totals are 202 rows, 168 mandatory,
34 optional, 134 PASS, 34 UNSUPPORTED, and 34 N/A.

### Task 4: Full verification and delivery commit

**Files:**
- Modify: `docs/specs/scxml-invoke-autoforward-design.md` only if verification reveals a contract mismatch.
- Modify: `docs/superpowers/plans/2026-09-01-scxml-invoke-autoforward.md` to check completed steps.

**Interfaces:**
- Consumes: all Task 1-3 changes.
- Produces: one reviewed, reproducible feature commit on `feat/w3c-invoke-autoforward`.

- [x] **Step 1: Run a fresh Release configure and full suite**

Run from the Visual Studio developer shell:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
```

Expected: configure and build succeed; all CTest entries pass.

- [x] **Step 2: Verify source hygiene and review the diff**

Run:

```powershell
git diff --check
git status --short
git diff --stat
git diff -- include/scxml/scxml.h src/scxml_runtime.c tests/scxml_test.c tests/scxml_w3c_conformance_test.c tests/w3c docs/specs/scxml-invoke-autoforward-design.md
```

Expected: no whitespace errors, no `.codegraph` path staged, and only the
planned feature files differ.

- [x] **Step 3: Commit the verified feature**

Run:

```powershell
git add include/scxml/scxml.h src/scxml_runtime.c tests/scxml_test.c tests/w3c/test229.scxml tests/w3c/test230.scxml tests/scxml_w3c_conformance_test.c tests/w3c/manifest.tsv tests/w3c/README.md docs/specs/scxml-invoke-autoforward-design.md docs/superpowers/plans/2026-09-01-scxml-invoke-autoforward.md
git commit -m "feat(scxml): preserve autoforward event envelopes"
```
