# TurboSCXML CHTTP BasicHTTP Event I/O Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an optional, bounded `TurboSCXML::CHttpEventIO` composite adapter that implements the W3C BasicHTTP Event I/O Processor with real CHTTP ingress/egress while preserving the mandatory host-owned SCXML Event I/O Processor.

**Architecture:** The transport-free core gains a session-owned supported-processor table and named external Event admission. A separate opaque CHTTP processor owns the listener, async client, worker, fixed endpoint rows, and fixed egress rows; per-session bindings decorate a required downstream SCXML adapter and participate in the existing close/quiescence protocol.

**Tech Stack:** C11, TurboSCXML, Rocida CFlow/CMeta/Core/TinyTest, Rocida CHTTP/CNet, CMake Presets, W3C SCXML 1.0.

**Spec:** `docs/specs/scxml-chttp-event-io-design.md`

## Global Constraints

- Start execution from an up-to-date branch containing the current `origin/main` CMeta session-options work; do not implement against the two-commit-behind checkout recorded when this plan was written.
- Use `superpowers:using-git-worktrees` before implementation and preserve the existing untracked `docs/superpowers/plans/2026-08-31-uscxml-reference-guided-development.md` in the original checkout.
- Follow strict red-green-refactor: add one focused TinyTest behavior, run it and observe the expected failure, then add only the production code needed for green.
- Keep `TurboSCXML::SCXML` transport-free. No CHTTP type or header may appear in `include/scxml/scxml.h`, and its existing link interface must remain unchanged.
- `TURBOSCXML_ENABLE_CHTTP_EVENT_IO` defaults to `OFF`. When `ON`, resolve `Rocida::CHTTP` only from the already selected `ROCIDA_ROOT`; never add a second package root or fallback search.
- Feature-ON execution requires a CHTTP-enabled Rocida SDK installed at `$env{PKG_ROOT}/rocida/{debug,release}` before configuring TurboSCXML.
- Keep decoder and resolver extension points as typed C callbacks. Do not add adapter-local types to `CMETA_CALLABLE_TYPE_LIST`: that list changes the build-wide `cmeta_sig`/`cmeta_callable` ABI. CMeta is used for decoded Event data and its schema validation, not for transport callback dispatch.
- Accept only `http://www.w3.org/TR/scxml/#BasicHTTPEventProcessor`; do not accept aliases, follow redirects, retry, downgrade HTTPS, or infer authorization from untrusted target text.
- Every count, stride, byte total, endpoint, egress row, form entry, request body, and copied metadata field has a positive configured hard bound and overflow-checked aggregate allocation.
- Ticket `commit` and `discard` callbacks are nonblocking and infallible. No DNS, socket, HTTP, allocation, external callback, or wait occurs from either callback.
- Preserve per-source commit order. Full outbound capacity returns `SCXML_ADAPTER_FULL`; full inbound session capacity returns HTTP 503; no row is overwritten or silently dropped.
- Run Windows CMake commands through `VsDevCmd.bat` and the checked-in user presets. Use the smallest target/filter first, then Release and Debug/ASan complete gates.

---

### Task 1: Admit arbitrary named external Events through the core

**Files:**
- Modify: `include/scxml/scxml.h`
- Modify: `src/scxml_impl.h`
- Modify: `src/scxml_analyze.c`
- Modify: `src/scxml_emit.c`
- Modify: `src/scxml_program.c`
- Modify: `src/scxml_runtime.c`
- Modify: `src/scxml_session.c`
- Test: `tests/scxml_test.c`
- Test: `tests/scxml_event_io_contract_test.c`

**Interfaces:**
- Consumes: existing exact `scxml_program_event()`, metadata admission, hierarchical descriptor matching, and external metadata rows.
- Produces: `scxml_session_try_send_named_with_metadata(scxml_session *, const char *, size_t, const scxml_event_metadata *)`; one private unmatched Event ID per program; copied actual Event names through the complete external Event lifecycle.

- [x] **Step 1: Write failing exact/prefix/wildcard named-admission tests**

Add three TinyTest cases using real compiled programs and real sessions. The key assertions must have literal expected names and transition outcomes:

```c
it("routes an unseen hierarchical Event through its longest descriptor prefix") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='wait' datamodel='cmeta'><state id='wait'>"
        "<transition event='alarm.system' "
        "cond='_event.name == &quot;alarm.system.disk.full&quot;' target='pass'/>"
        "</state><final id='pass'/></scxml>";
    /* Compile, initialize, call scxml_session_try_send_named_with_metadata()
       with literal name alarm.system.disk.full, drain, and require pass. */
}

it("routes an otherwise unseen Event only through a star descriptor") {
    /* Compile one '*' transition guarded by _event.name == "vendor.new";
       admit vendor.new through the named API and require pass. */
}

it("queues an unmatched named Event without fabricating a transition") {
    /* Admit vendor.ignored into a program with only event='go', verify one
       external dequeue/macrostep occurs, and verify the state stays wait. */
}
```

Also extend the Event I/O contract test so the existing host router uses named
admission and proves that an `alarm.system.disk.full` envelope preserves that
full name through `_event.name` rather than exposing `alarm.system`.

- [x] **Step 2: Run the focused tests and verify RED**

Run:

```powershell
cmd /c "call \"\"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat\"\" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target scxml_test scxml_event_io_contract_test && build\Msvc-Release\tests\scxml_test.exe --filter \"unseen\" --no-color"
```

Expected: compilation fails because `scxml_session_try_send_named_with_metadata` is undeclared. After adding only the declaration to expose the intended API, the tests must fail because no private unmatched ID/name-copy path exists.

- [x] **Step 3: Add the private representative Event and named admission**

Add a private `unmatched_external_event` ID to `scxml_program_impl`. Increase
the native event definition count by one without inserting a public name row.
During transition emission, bind only `*` descriptors to this ID. Add an
internal longest-prefix resolver:

```c
static cflow_event_id scxml_program_route_external_name(
    const scxml_program_impl *program, const char *name, size_t name_size) {
    const scxml_program_name *best = NULL;
    size_t index;
    for (index = 0u; index < program->event_name_count; ++index) {
        const scxml_program_name *candidate = &program->event_names[index];
        const bool prefix = candidate->size <= name_size &&
            memcmp(candidate->name, name, candidate->size) == 0 &&
            (candidate->size == name_size || name[candidate->size] == '.');
        if (prefix && (best == NULL || candidate->size > best->size))
            best = candidate;
    }
    return best != NULL ? (cflow_event_id)best->id
                        : program->unmatched_external_event;
}
```

Extend `scxml_external_event_metadata_row` with a bounded actual-name buffer,
size, and presence bit. Factor the existing metadata reservation into one
private function that optionally copies a name. The new public API validates a
nonempty name, routes the representative ID, reserves/copies metadata and the
actual name atomically, then calls the existing tagged mailbox admission. On
mailbox failure it releases both the metadata row and copied name.

In external preprocessing, use the copied actual name when present; otherwise
retain the existing ID-to-name behavior. Ensure finalize and autoforward build
their envelope from `session->system_values.event_name`, which now holds the
actual name.

- [x] **Step 4: Run named-admission tests and all adjacent Event tests GREEN**

Run both executables without filters after the focused cases pass:

```powershell
cmd /c "call \"\"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat\"\" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target scxml_test scxml_event_io_contract_test && build\Msvc-Release\tests\scxml_test.exe --no-color && build\Msvc-Release\tests\scxml_event_io_contract_test.exe --no-color"
```

Expected: both exit 0 with no framework errors.

- [x] **Step 5: Commit the core named Event boundary**

```powershell
git add include/scxml/scxml.h src/scxml_impl.h src/scxml_analyze.c src/scxml_emit.c src/scxml_program.c src/scxml_runtime.c src/scxml_session.c tests/scxml_test.c tests/scxml_event_io_contract_test.c
git commit -m "feat(scxml): admit arbitrary external event names"
```

---

### Task 2: Generalize `_ioprocessors` to a bounded session descriptor table

**Files:**
- Modify: `include/scxml/scxml.h`
- Modify: `src/scxml_expr.h`
- Modify: `src/scxml_expr.c`
- Modify: `src/scxml_impl.h`
- Modify: `src/scxml_session.c`
- Test: `tests/scxml_expr_test.c`
- Test: `tests/scxml_cmeta_test.c`
- Test: `tests/scxml_test.c`

**Interfaces:**
- Consumes: `scxml_session_config`, generated SCXML location, CMeta system-value evaluation, and read-only system-location classification.
- Produces: `scxml_ioprocessor_descriptor`; `scxml_session_config.ioprocessors`; `scxml_session_config.ioprocessor_count`; `scxml_session_copy_ioprocessor_location()`; generic `_ioprocessors.<NCName>.location` evaluation.

- [x] **Step 1: Add failing descriptor validation and expression tests**

Add literal fixtures for one BasicHTTP row:

```c
static const scxml_ioprocessor_descriptor basic_http = {
    .name = "basichttp",
    .name_size = sizeof("basichttp") - 1u,
    .type = "http://www.w3.org/TR/scxml/#BasicHTTPEventProcessor",
    .type_size = sizeof("http://www.w3.org/TR/scxml/#BasicHTTPEventProcessor") - 1u,
    .location = "http://127.0.0.1:43123/scxml/session-a",
    .location_size = sizeof("http://127.0.0.1:43123/scxml/session-a") - 1u};
```

Tests must prove:

- `_ioprocessors.basichttp.location` evaluates to the exact literal;
- an absent `_ioprocessors.vendor.location` produces unknown-location at runtime;
- `scxml` plus `basichttp` are both bound and immutable;
- duplicate name, duplicate type, invalid NCName, partial NULL fields,
  replacement of `scxml`, aggregate overflow, and oversized configured bytes
  reject initialization without publishing a session;
- `scxml_session_copy_ioprocessor_location()` reports required capacity without
  partial output and finds both canonical type URIs.

- [x] **Step 2: Run the focused tests and verify RED**

```powershell
cmd /c "call \"\"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat\"\" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target scxml_expr_test scxml_cmeta_test scxml_test && build\Msvc-Release\tests\scxml_cmeta_test.exe --filter \"ioprocessors\" --no-color"
```

Expected: compile failure for the missing descriptor/config/copy API, followed
by an expression failure while the parser still requires `.scxml.location`.

- [x] **Step 3: Implement the copied table and generic expression operand**

Add the public descriptor and append the two fields to `scxml_session_config`:

```c
const scxml_ioprocessor_descriptor *ioprocessors;
size_t ioprocessor_count;
```

Session initialization must synthesize row zero as `{name="scxml",
type=canonical SCXML URI, location=generated #_scxml UUID}`, validate configured
rows, calculate `row_bytes + all string bytes + terminators` with existing
checked helpers, allocate one block, and rewrite every stored pointer into that
block. Free it only after adapter quiescence during session teardown.

Replace `EXPR_OPERAND_SYSTEM_SCXML_LOCATION` with
`EXPR_OPERAND_SYSTEM_IOPROCESSOR_LOCATION` carrying the property-name bytes.
Parsing must accept exactly:

```text
_ioprocessors . IDENT . location
```

Evaluation performs a bounded linear lookup over the usually tiny immutable
session table. It returns the copied location or unknown-location without
fallback. `isBound(_ioprocessors)` remains true because the core always owns
the SCXML row. Keep the entire root read-only through the existing location
classifier.

Add:

```c
scxml_location_status scxml_session_copy_ioprocessor_location(
    const scxml_session *session,
    const char *type,
    size_t type_size,
    char *out_location,
    size_t location_capacity,
    size_t *out_required_capacity);
```

Have `scxml_session_copy_location()` call this function with the canonical
SCXML type so the original API has one fact source.

- [x] **Step 4: Run expression, CMeta, and public-session suites GREEN**

```powershell
cmd /c "call \"\"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat\"\" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target scxml_expr_test scxml_cmeta_test scxml_test && build\Msvc-Release\tests\scxml_expr_test.exe --no-color && build\Msvc-Release\tests\scxml_cmeta_test.exe --no-color && build\Msvc-Release\tests\scxml_test.exe --no-color"
```

Expected: all three exit 0.

- [x] **Step 5: Commit the supported-processor table**

```powershell
git add include/scxml/scxml.h src/scxml_expr.h src/scxml_expr.c src/scxml_impl.h src/scxml_session.c tests/scxml_expr_test.c tests/scxml_cmeta_test.c tests/scxml_test.c
git commit -m "feat(scxml): bind configured event processors"
```

---

### Task 3: Add the optional target and pure BasicHTTP codec

**Files:**
- Create: `include/scxml/chttp_event_io.h`
- Create: `src/chttp_event_io_internal.h`
- Create: `src/chttp_event_io_codec.c`
- Create: `tests/scxml_chttp_codec_test.c`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `cmake/TurboSCXMLConfig.cmake.in`
- Modify: `CMakeUserPresets.json`

**Interfaces:**
- Consumes: public CHTTP request/server views, `scxml_send_request`, and `scxml_content_view`.
- Produces: feature option and exported `TurboSCXML::CHttpEventIO`; public constants/types/config declarations from the spec; private encode/decode functions returning explicit status and required byte counts.

- [x] **Step 1: Add failing literal codec tests**

Create a TinyTest executable with no socket dependency. Use literal output,
not a mirrored helper. Include cases equivalent to:

```c
it("encodes the event and named payload in stable form order") {
    /* request: event="order ready", entries customer="A&B" and qty=2 */
    static const char expected[] =
        "_scxmleventname=order+ready&customer=A%26B&qty=2";
    check_equal(actual_size, sizeof(expected) - 1u);
    check_equal(actual, expected, sizeof(expected) - 1u);
}

it("decodes one reserved Event name and retains duplicate application fields") {
    static const char body[] =
        "_scxmleventname=test&tag=one&tag=two";
    /* Require event name test and two ordered tag entries. */
}

it("rejects malformed percent escapes without partial output") {
    static const char body[] = "_scxmleventname=test&bad=%G0";
    /* Require SCXML_CHTTP_DECODE_BAD_REQUEST and zero published entries. */
}
```

Cover bool/signed/unsigned/finite-float/string lexical forms, `+`, `%00`,
invalid UTF-8, duplicate `_scxmleventname`, empty names, exact-capacity and
capacity+1, raw `<content>`, XML/text content types, and unsupported CMeta
content. Every test names the production branch it catches.

- [x] **Step 2: Configure feature ON and verify RED**

Install the CHTTP-enabled Rocida SDK into the ordinary Release root, then
set `TURBOSCXML_ENABLE_CHTTP_EVENT_IO=ON` in the public Release preset for the
feature branch and run:

```powershell
cmd /c "call \"\"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat\"\" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user --target scxml_chttp_codec_test"
```

Expected: the test target fails to compile/link because codec functions are
not implemented. Separately configure once with the option OFF and verify the
core still configures when `Rocida::CHTTP` is absent.

- [x] **Step 3: Implement exact bounded form encoding and decoding**

Implement two-pass encoding: the first pass calculates required bytes with
checked additions and validates value kinds; the second writes only after the
destination bound is known to fit. Encode unreserved `[A-Za-z0-9-._~]`
literally, space as `+`, and every other UTF-8 byte as `%HH` with uppercase
digits. Use `snprintf` only into a fixed local scalar buffer and reject
non-finite floats.

Decode by scanning the CHTTP-owned body once into preallocated processor
scratch. Split on `&` and the first `=`, validate percent triplets before
writing, turn `+` into space, validate decoded UTF-8, and enforce entry/name/
value bounds before publishing the output count. Remove the single reserved
name from application entries; reject duplicates. Use `HTTP.POST` when it is
absent.

For content payloads, copy text/XML bytes unchanged and select literal content
types `text/plain; charset=utf-8` and `application/xml; charset=utf-8`.
Scalar content uses the same lexical converter. Return
`SCXML_ADAPTER_ERROR_EXECUTION` for `SCXML_CONTENT_CMETA` in V1 because no
format was selected by the document or host.

- [x] **Step 4: Define optional CMake/package behavior and run codec GREEN**

Add the option and target only inside its `if()`:

```cmake
option(TURBOSCXML_ENABLE_CHTTP_EVENT_IO
  "Build the optional CHTTP BasicHTTP Event I/O processor" OFF)

if(TURBOSCXML_ENABLE_CHTTP_EVENT_IO)
  if(NOT TARGET Rocida::CHTTP)
    message(FATAL_ERROR
      "TURBOSCXML_ENABLE_CHTTP_EVENT_IO requires Rocida::CHTTP from ROCIDA_ROOT")
  endif()
  add_library(turbo_scxml_chttp_event_io
    src/chttp_event_io_codec.c)
  add_library(TurboSCXML::CHttpEventIO ALIAS turbo_scxml_chttp_event_io)
  target_link_libraries(turbo_scxml_chttp_event_io
    PUBLIC TurboSCXML::SCXML Rocida::CHTTP
    PRIVATE Rocida::Core)
  install(TARGETS turbo_scxml_chttp_event_io EXPORT TurboSCXMLTargets)
  install(FILES include/scxml/chttp_event_io.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/scxml)
endif()
```

The package config continues to find only the one selected Rocida package;
the exported optional target carries its CHTTP dependency. Register the codec
test only when the feature is ON. Run the complete codec executable and verify
every boundary case passes.

- [x] **Step 5: Commit the optional target and codec**

```powershell
git add include/scxml/chttp_event_io.h src/chttp_event_io_internal.h src/chttp_event_io_codec.c tests/scxml_chttp_codec_test.c CMakeLists.txt tests/CMakeLists.txt cmake/TurboSCXMLConfig.cmake.in CMakeUserPresets.json
git commit -m "feat(scxml): add bounded BasicHTTP codec"
```

---

### Task 4: Implement processor and binding lifecycle without transport work

**Files:**
- Create: `src/chttp_event_io.c`
- Modify: `src/chttp_event_io_internal.h`
- Modify: `CMakeLists.txt`
- Create: `tests/scxml_chttp_lifecycle_test.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: public versioned configs, Rocida mutex/condition/thread/UUID APIs, downstream `scxml_event_io_adapter`.
- Produces: processor init/start/stop/destroy; binding init/accessors/activate/destroy; fixed endpoint rows; composite adapter close/quiescence/delegation skeleton.

- [x] **Step 1: Add failing lifecycle and invalid-config tests**

Test zero/partial configs, non-power-of-two CHTTP command capacities, zero
endpoint/egress/string bounds, checked aggregate overflow, missing resolver,
missing/incomplete downstream adapter, duplicate init/start/activate, destroy
before stop, processor stop with a live binding, binding destroy before
quiescence, and a complete happy lifecycle.

The happy path must follow the actual user protocol:

```c
check_equal(scxml_chttp_processor_init(&processor, &processor_config), TURBO_OK);
check_equal(scxml_chttp_processor_start(&processor), TURBO_OK);
check_equal(scxml_chttp_binding_init(&binding, &processor, &binding_config), TURBO_OK);
check_true(scxml_chttp_binding_ioprocessor(&binding, &descriptor));
session_config.ioprocessors = &descriptor;
session_config.ioprocessor_count = 1u;
session_config.event_io =
    scxml_chttp_binding_event_io_adapter(&binding);
session_config.adapter_user = scxml_chttp_binding_adapter_user(&binding);
check_equal(scxml_session_init_cmeta(&session, &session_config, &data),
            CFLOW_STATECHART_INSTANCE_OK);
check_equal(scxml_chttp_binding_activate(&binding, &session, &program), TURBO_OK);
check_equal(scxml_session_destroy(&session), CFLOW_STATECHART_INSTANCE_OK);
check_equal(scxml_chttp_binding_destroy(&binding), TURBO_OK);
check_equal(scxml_chttp_processor_stop(&processor, 1000u), TURBO_OK);
check_equal(scxml_chttp_processor_destroy(&processor), TURBO_OK);
```

- [x] **Step 2: Run lifecycle tests and verify RED**

Build and run `scxml_chttp_lifecycle_test --no-color`. Expected: missing
symbols first; after API shells exist, invalid lifecycle assertions remain red.

- [x] **Step 3: Implement fixed storage, endpoint registry, and state gates**

Allocate one processor implementation and one checked aggregate block holding
endpoint rows, egress rows, and their fixed string/body strides. Generate each
endpoint token with `turbo_uuid_v4_generate()` and format it into:

```text
http://<advertised_authority><base_path>/<uuid>
```

Require `base_path` to start with `/`, contain no query/fragment, and fit the
configured URI bound. Store processor states `INITIALIZED`, `RUNNING`,
`STOPPING`, and `STOPPED`; binding states `RESERVED`, `ACTIVE`, `CLOSING`, and
`QUIESCENT`. Protect all transitions and counters with the processor mutex.

The initial composite `prepare_send` delegates non-BasicHTTP requests directly
and rejects BasicHTTP with `SCXML_ADAPTER_CLOSED` until Task 5 installs egress.
`close` atomically changes the binding to closing and calls the downstream
adapter's close exactly once. `is_quiescent` checks downstream quiescence plus
zero handler/outbound references.

- [x] **Step 4: Run lifecycle and existing adapter suites GREEN**

Run the new lifecycle executable, then `scxml_event_io_contract_test`. Expected:
both exit 0 and the downstream close counter is exactly one for initialization
failure and normal destruction.

- [x] **Step 5: Commit lifecycle ownership**

```powershell
git add src/chttp_event_io.c src/chttp_event_io_internal.h CMakeLists.txt tests/scxml_chttp_lifecycle_test.c tests/CMakeLists.txt
git commit -m "feat(scxml): own CHTTP event processor lifecycle"
```

---

### Task 5: Implement transactional outbound HTTP, delay, cancel, and delegation

**Files:**
- Create: `src/chttp_event_io_egress.c`
- Modify: `src/chttp_event_io.c`
- Modify: `src/chttp_event_io_internal.h`
- Modify: `CMakeLists.txt`
- Create: `tests/scxml_chttp_egress_test.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: pure codec, mandatory resolver, async CHTTP client, binding/session references, downstream adapter tickets.
- Produces: fixed egress state machine; nonblocking prepare/commit/discard; single CHTTP owner thread; completion error reporting; delayed BasicHTTP and send-id cancellation.

- [x] **Step 1: Add failing transaction tests around a real loopback CHTTP server**

Use a real local CHTTP server as the remote peer and literal wire expectations.
Cover:

- prepare reserves but sends nothing before commit;
- discard sends nothing and immediately frees capacity;
- commit sends one POST with exact target, Host, content type, and body;
- capacity one rejects the second reservation with `SCXML_ADAPTER_FULL` and
  accepts again after completion;
- two source bindings preserve order independently;
- a future delay does not submit early;
- committed cancel before due time sends nothing;
- cancel racing an in-flight request produces one terminal row release;
- transport failure and HTTP 404 report one asynchronous
  `error.communication` to the source session;
- HTTP 204 completes without an error Event;
- non-BasicHTTP send/cancel returns the exact downstream ticket behavior.

- [x] **Step 2: Run egress tests and verify RED**

Build and run `scxml_chttp_egress_test --filter "before commit" --no-color`.
Expected: BasicHTTP prepare is closed/rejected and no outbound state exists.

- [x] **Step 3: Implement prepare-time resolution, copying, and ticket states**

Call the resolver during `prepare_send()` with the sized SCXML target. Require
three nonempty NUL-terminated outputs; reject connection schemes other than
`tcp://` or `pipe://`; require an origin-form target beginning `/`; copy all
three strings before the resolver returns. Empty BasicHTTP target maps to
`SCXML_ADAPTER_ERROR_COMMUNICATION`.

Encode the body into the row's fixed stride, copy send id and source binding,
record a monotonically increasing per-binding commit sequence, and return:

```c
*out_ticket = (cflow_statechart_effect_ticket){
    .commit = scxml_chttp_egress_commit,
    .discard = scxml_chttp_egress_discard,
    .user = row};
```

Commit changes only `RESERVED -> READY`, records the due time using the
session's already-evaluated `delay_ms`, and signals the worker. Discard changes
only `RESERVED -> FREE` and drops its binding reference. Impossible generation
or state changes are ignored by the infallible callback but increment an
invariant-failure statistic.

- [x] **Step 4: Implement the single-owner CHTTP worker and completion path**

The worker alone calls `chttp_async_client_submit/poll/stop/destroy`. Under the
mutex it selects the lowest committed sequence that is due for each binding,
marks it `SUBMITTING`, copies only the stable row handle, unlocks, and submits.
Immediate `TURBO_ENOBUFS` returns the row to READY; other immediate failures
enter COMPLETING from SUBMITTING and finish as communication failures. The
CHTTP callback changes `SUBMITTED -> COMPLETING`, records status/metrics, then
reports adapter completion or communication failure outside the processor
mutex while retaining the binding/session reference. Only after reporting does
it change `COMPLETING -> FREE` and release that reference.

Implement cancel as a transaction over `{slot,generation}`. Prepare claims a
matching BasicHTTP send id in RESERVED, READY, SUBMITTING, SUBMITTED, or
COMPLETING so same-transaction and notification-window races do not delegate
to the downstream adapter. Commit generation-checks the row, removes READY, or
marks SUBMITTING/SUBMITTED for worker-owned `chttp_async_request_cancel()`;
RESERVED/COMPLETING and changed-generation tickets complete as no-ops. If no
BasicHTTP row matches, delegate to the downstream cancel callback. Advertise
delayed/cancel only when both downstream capabilities are present.

- [x] **Step 5: Run egress, codec, lifecycle, and core adapter suites GREEN**

Run all four executables unfiltered. Repeat the cancellation-race filter 100
times from PowerShell and require zero failures:

```powershell
1..100 | ForEach-Object {
  & build\Msvc-Release\tests\scxml_chttp_egress_test.exe --filter "cancel racing" --no-color
  if ($LASTEXITCODE -ne 0) { throw "iteration $_ failed" }
}
```

- [x] **Step 6: Commit outbound transport**

```powershell
git add src/chttp_event_io_egress.c src/chttp_event_io.c src/chttp_event_io_internal.h CMakeLists.txt tests/scxml_chttp_egress_test.c tests/CMakeLists.txt
git commit -m "feat(scxml): dispatch BasicHTTP event output"
```

---

### Task 6: Implement inbound POST admission and HTTP response semantics

**Files:**
- Create: `src/chttp_event_io_ingress.c`
- Modify: `src/chttp_event_io.c`
- Modify: `src/chttp_event_io_internal.h`
- Modify: `CMakeLists.txt`
- Create: `tests/scxml_chttp_ingress_test.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: CHTTP route handler, form decoder, binding decode callback, named Event admission, session/program pointers protected by binding callback references.
- Produces: `POST <base_path>/:endpoint` ingress; exact 204/400/404/410/415/422/503/500 response mapping; handler participation in quiescence.

- [x] **Step 1: Add failing real-server ingress tests**

Start the actual optional processor on loopback port zero, activate a real
session, and call it with the blocking CHTTP client. Test each table row from
the spec. The 204 test must prove response causality:

```c
check_equal(chttp_post(&client, &options, &response, &error), TURBO_OK);
check_equal(response.status_code, 204u);
check_equal(scxml_session_drain(&session, 8u, &processed),
            CFLOW_STATECHART_INSTANCE_OK);
check_equal(processed, (size_t)1u);
check_true(session_reached_pass(&session, &program));
```

Use a capacity-one external mailbox to force 503. Use a decoder returning a
real CMeta object compatible with the program and prove `_event.data.param1`
equals literal `2`. Add a blocking decoder probe so session destruction waits
until the active handler releases its borrowed session pointer.

- [x] **Step 2: Run ingress tests and verify RED**

Build and run `scxml_chttp_ingress_test --filter "204" --no-color`. Expected:
404 because no endpoint route/handler has been registered.

- [x] **Step 3: Register the route and implement reference-safe handler flow**

Register one CHTTP route at `<base_path>/:endpoint` before server start. In the
handler:

1. Require POST and locate the endpoint token.
2. Under the processor mutex require ACTIVE, increment `active_callbacks`, and
   copy the session/program/decoder pointers.
3. Release the mutex, decode form/raw content, call the decoder if configured,
   and create metadata with `origin_type` equal to the canonical BasicHTTP URI.
4. Call `scxml_session_try_send_named_with_metadata()`; do not drain or execute
   the session from the CHTTP owner thread.
5. Map the result to the exact HTTP status table and call
   `chttp_server_reply()` once.
6. Under the mutex decrement `active_callbacks`, signal close waiters, unlock,
   and return the CHTTP result.

The route must never retain `request`, parsed entry, decoded content, or
response pointers after return. If no decoder is configured, expose raw body
as `SCXML_CONTENT_TEXT_UTF8`; an empty body uses invalid/empty data.

- [x] **Step 4: Run ingress/lifecycle stress and adjacent suites GREEN**

Run ingress unfiltered, the blocking-destroy filter 100 times, then lifecycle,
egress, Event I/O contract, and CMeta suites. Expected: every executable exits
0; processor stats show admitted/rejected counts matching the literal request
count.

- [x] **Step 5: Commit inbound transport**

```powershell
git add src/chttp_event_io_ingress.c src/chttp_event_io.c src/chttp_event_io_internal.h CMakeLists.txt tests/scxml_chttp_ingress_test.c tests/CMakeLists.txt
git commit -m "feat(scxml): admit BasicHTTP event input"
```

---

### Task 7: Promote the BasicHTTP W3C corpus with equivalent strict probes

**Files:**
- Create: `tests/w3c/test201.scxml`
- Create: `tests/w3c/test509.scxml`
- Create: `tests/w3c/test510.scxml`
- Create: `tests/w3c/test513.scxml`
- Create: `tests/w3c/test518.scxml`
- Create: `tests/w3c/test519.scxml`
- Create: `tests/w3c/test520.scxml`
- Create: `tests/w3c/test522.scxml`
- Create: `tests/w3c/test531.scxml`
- Create: `tests/w3c/test532.scxml`
- Create: `tests/w3c/test534.scxml`
- Create: `tests/w3c/test567.scxml`
- Create: `tests/w3c/test577.scxml`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `tests/w3c/manifest.tsv`
- Modify: `tests/w3c/README.md`

**Interfaces:**
- Consumes: real loopback CHTTP processor/binding, strict W3C result probe, local CMeta rewrite conventions, transport request observations.
- Produces: executable optional conformance rows 201, 509, 510, 513, 518, 519, 520, 522, 531, 532, 534, 567, and 577.

- [x] **Step 1: Add local CMeta rewrites and mark rows as expected-but-failing**

Translate `conf:basicHTTPAccessURITarget` to
`_ioprocessors.basichttp.location`. Replace the non-standard `_event.raw`
predicates with strict host probe expectations in the harness. Preserve each
assertion independently:

| Row | Literal witness |
|---:|---|
| 201 | exact BasicHTTP type accepted and one self POST returns `event1` |
| 509 | observed method is POST and Event reaches the session |
| 510 | raised internal Event is processed before HTTP external Event |
| 513 | externally submitted well-formed request receives 204 after admission |
| 518 | namelist produces the expected form name/value |
| 519 | `<param name="param1">` produces `param1=1` |
| 520 | content body is exactly `this is some content` |
| 522 | `_ioprocessors.basichttp.location` is nonempty and externally usable |
| 531 | `_scxmleventname=test` raises Event `test` |
| 532 | absent reserved name raises Event `HTTP.POST` |
| 534 | send `event="test"` emits `_scxmleventname=test` |
| 567 | non-reserved `param1=2` becomes `_event.data.param1 == 2` |
| 577 | BasicHTTP send without target raises `error.communication` internally |

Keep `OPTIONAL` in the requirement column because BasicHTTP itself remains an
optional SCXML profile; change execution status and evidence fields only after
the strict runner passes.

- [x] **Step 2: Run all 13 filters and verify RED for the expected missing harness mode**

Build `scxml_w3c_conformance_test` with the feature ON and run each numeric
filter. Expected: rows fail because the harness has not yet created a CHTTP
processor/binding or supplied its descriptor.

- [x] **Step 3: Add one strict CHTTP W3C runner**

Add `run_w3c_chttp_fixture()` beside the existing strict Event I/O runners. It
must:

- create a loopback processor with a resolver that authorizes only the exact
  generated access URI;
- create the downstream bounded SCXML router and the composite binding;
- install the binding's descriptor before session initialization and activate
  only after successful init;
- record method, target, content type, and body from the real server request;
- provide the row-567 decoder with a complete CMeta schema/object;
- drive the serial executor and HTTP worker until exactly one `result.pass`,
  one `result.fail`, timeout, or a fixed iteration bound;
- destroy in session, binding, processor-stop, processor-destroy order and
  require all queues/callbacks to be quiescent.

No fixture may pass from a log string, mock callback, or source-text check.

- [x] **Step 4: Run focused rows GREEN and update manifest evidence**

Run all thirteen filters in Release. Only after each exits 0, change its
manifest execution columns to `PASS`/`TERMINAL_PASS` (or the row-513 manual
integration witness) and document the local rewrite/equivalent transport
probe in `tests/w3c/README.md`.

- [x] **Step 5: Commit the optional conformance profile**

```powershell
git add tests/w3c/test201.scxml tests/w3c/test509.scxml tests/w3c/test510.scxml tests/w3c/test513.scxml tests/w3c/test518.scxml tests/w3c/test519.scxml tests/w3c/test520.scxml tests/w3c/test522.scxml tests/w3c/test531.scxml tests/w3c/test532.scxml tests/w3c/test534.scxml tests/w3c/test567.scxml tests/w3c/test577.scxml tests/scxml_w3c_conformance_test.c tests/w3c/manifest.tsv tests/w3c/README.md
git commit -m "test(scxml): promote BasicHTTP event processor corpus"
```

---

### Task 8: Document, install, and run completion gates

**Files:**
- Modify: `README.md`
- Create: `docs/scxml-chttp-event-io.md`
- Modify: `tests/install_consumer/CMakeLists.txt`
- Create: `tests/install_consumer/event_io_main.c`
- Create: `tests/install_consumer/event_io_main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `docs/specs/scxml-chttp-event-io-design.md`
- Modify: `docs/superpowers/plans/2026-09-02-scxml-chttp-event-io.md`

**Interfaces:**
- Consumes: completed core and optional APIs, exported CMake targets, user presets.
- Produces: user lifecycle guide, feature/dependency documentation, installed C/C++ ABI consumers, final verification evidence.

- [x] **Step 1: Add failing installed-consumer checks**

The C consumer must include both headers, initialize zero opaque handles, take
addresses of every public lifecycle/query function, and link
`TurboSCXML::CHttpEventIO`. The C++ consumer must include both headers inside a
normal C++ translation unit and value-initialize both handles. A separate
feature-OFF consumer links only `TurboSCXML::SCXML` and proves CHTTP is not a
transitive target requirement.

Run the install-consumer verification before updating export/install rules and
observe failure to find the optional target/header.

- [x] **Step 2: Complete public documentation and examples**

Document the exact order:

```text
processor_init -> processor_start -> binding_init
-> install descriptor + adapter in session config
-> session_init -> binding_activate
-> session_destroy -> binding_destroy
-> processor_stop -> processor_destroy
```

Include one compiling example with a deny-by-default resolver, positive CHTTP
deadlines, finite capacities, decoder ownership, 503 backpressure, error Event
semantics, HTTP-only limitation, and no HTTPS downgrade. State that the host
still supplies the canonical SCXML router and remains responsible for target
authorization.

Mark the design status `Implemented` only after all gates below pass. Check off
plan steps only when their commands have fresh success output.

- [x] **Step 3: Run Release feature-ON focused and complete verification**

```powershell
cmd /c "call \"\"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat\"\" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-chttp-user && cmake --build --preset win-release-chttp-user && ctest --preset win-release-chttp-user --output-on-failure && cmake --build --preset install-win-release-chttp-user"
```

Require zero failed CTest cases, then configure/build/run both installed
consumers against the Release install prefix.

- [x] **Step 4: Run Debug/ASan feature-ON verification**

Install the matching CHTTP-enabled Debug SDK, then run:

```powershell
cmd /c "call \"\"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat\"\" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-dev-chttp-user && cmake --build --preset win-dev-chttp-user && ctest --preset win-dev-chttp-user --output-on-failure && cmake --build --preset install-win-dev-chttp-user"
```

Require zero test failures and no ASan diagnostics.

- [x] **Step 5: Reconfigure feature OFF and prove dependency isolation**

Set the option OFF in both public profiles, reconfigure Release with the normal
non-CHTTP Rocida SDK, build, run complete CTest, install, and run the core
installed consumer. Inspect `INTERFACE_LINK_LIBRARIES` and require the existing
`TurboSCXML::SCXML` dependency contract to remain exact.

- [x] **Step 6: Run static repository gates and review the complete diff**

```powershell
rg -n "fit\(|it_only\(|TODO|FIXME|HACK" include src tests CMakeLists.txt cmake README.md docs/scxml-chttp-event-io.md
git diff --check
git status --short
git diff --stat
```

Expected: no focused-test markers or unowned placeholders, `git diff --check`
exit 0, and status contains only scoped changes plus the pre-existing untracked
plan file called out in Global Constraints. Re-read every requirement and risk
in the spec and map it to a passing test or documented host precondition.

- [x] **Step 7: Commit documentation and verification artifacts**

```powershell
git add README.md docs/scxml-chttp-event-io.md tests/install_consumer CMakeLists.txt docs/specs/scxml-chttp-event-io-design.md docs/superpowers/plans/2026-09-02-scxml-chttp-event-io.md
git commit -m "docs(scxml): publish CHTTP event processor contract"
```
