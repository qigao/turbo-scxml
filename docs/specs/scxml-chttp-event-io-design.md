# TurboSCXML CHTTP BasicHTTP Event I/O Design

**Date:** 2026-09-02

**Status:** Implemented and verified

**Standards:** [SCXML 1.0, Appendix C.2](https://www.w3.org/TR/scxml/#BasicHTTPEventProcessor), [SCXML 1.0 Implementation Report](https://www.w3.org/Voice/2013/scxml-irp/)

## Decision

Add `TurboSCXML::CHttpEventIO` as an optional C11 library layered over
`TurboSCXML::SCXML` and `TurboUtils::CHTTP`. The core remains transport-free.
The optional library is a composite Event I/O adapter: it owns BasicHTTP
ingress and egress and delegates the mandatory SCXML Event I/O Processor to a
host-supplied adapter.

The implementation has three explicit owners:

- `scxml_chttp_processor` owns one CHTTP server, one asynchronous CHTTP client,
  one I/O worker, the endpoint registry, and fixed outbound slots.
- `scxml_chttp_binding` owns one session endpoint, its copied access URI,
  decoder callback, downstream SCXML adapter binding, and close/quiescence
  counters.
- `scxml_session` continues to own Statechart execution, external and internal
  queues, Event metadata, and run-to-completion ordering.

No CHTTP type appears in `<scxml/scxml.h>`. Applications that do not enable or
link `TurboSCXML::CHttpEventIO` retain the current dependency graph and
behavior.

## Standards profile

The adapter recognizes only the canonical BasicHTTP type URI:

```text
http://www.w3.org/TR/scxml/#BasicHTTPEventProcessor
```

Outbound BasicHTTP messages use HTTP `POST`. Named payloads are encoded as
`application/x-www-form-urlencoded`; a nonempty SCXML event name becomes the
first `_scxmleventname` parameter, followed by `namelist` and `<param>` entries
in SCXML document order. Names and scalar values are UTF-8 percent-encoded,
spaces use `+`, duplicates are retained, and percent hex digits are uppercase.
A `<content>` payload becomes the request body and is not form-wrapped.

Inbound messages are accepted only on a reserved binding URI and only through
`POST`. A strictly decoded, single `_scxmleventname` form field supplies the
Event name. Without that field the Event name is `HTTP.POST`, matching the W3C
IRP fixture convention. Remaining form fields or raw content are passed to the
binding's data decoder. HTTP 204 is returned only after the Event and metadata
have been copied into the target session's external queue. Malformed input,
an unknown endpoint, decoder failure, an unrouteable Event, or a full/closed
mailbox returns a non-2xx response.

The W3C IRP's non-standard `_event.raw` testing extension is not added to the
core. The conformance harness inspects the real CHTTP request at the transport
boundary, which the Implementation Report explicitly permits as an
equivalent validation method.

## Core additions

### Supported Event I/O locations

The current CMeta implementation hard-codes
`_ioprocessors.scxml.location`. Replace that special case with a bounded,
session-owned descriptor table:

```c
typedef struct scxml_ioprocessor_descriptor {
    const char *name;
    size_t name_size;
    const char *type;
    size_t type_size;
    const char *location;
    size_t location_size;
} scxml_ioprocessor_descriptor;
```

`scxml_session_config` gains borrowed `ioprocessors` and
`ioprocessor_count` fields. Initialization always creates the required
`scxml` descriptor from the generated session UUID, then validates and copies
the configured descriptors into one session-owned allocation. Names must be
unique NCNames, types must be unique nonempty strings, and no supplied row may
replace the required `scxml` name or canonical SCXML type. All count and byte
calculations are overflow-checked before allocation. Zero configured rows is
the existing behavior.

CMeta compilation admits `_ioprocessors.<NCName>.location`. The compiled
operand owns or references stable program bytes for `<NCName>`; evaluation
looks up that name in the session table and returns
`SCXML_EXPR_UNKNOWN_LOCATION` when it is absent. The read-only-location rule
continues to protect the complete `_ioprocessors` subtree. Add a bounded copy
API keyed by canonical type URI so optional adapters and hosts can verify the
installed value without exposing internal storage.

### Arbitrary external Event names

BasicHTTP ingress cannot assume that every external name appeared literally
in the document. Add:

```c
cflow_mailbox_status scxml_session_try_send_named_with_metadata(
    scxml_session *session,
    const char *name,
    size_t name_size,
    const scxml_event_metadata *metadata);
```

Compilation creates one private unmatched-external Event ID. Runtime routing
selects the longest compiled hierarchical prefix whose transition behavior
matches the incoming name; when none matches, it uses the private ID, which is
connected only to `*` transitions. The external metadata row copies the actual
name, and `_event.name`, finalize/autoforward envelopes, and diagnostics use
that copied name rather than the representative ID. The existing exact
`scxml_program_event()` and `scxml_session_try_send_with_metadata()` APIs stay
unchanged.

This closes a pre-existing host-boundary gap as well as supporting HTTP: a
valid external Event can be admitted even when no transition consumes it.

## Public optional API

The installed optional header is `<scxml/chttp_event_io.h>`:

```c
#define SCXML_CHTTP_ABI_V1 1u
#define SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI \
    "http://www.w3.org/TR/scxml/#BasicHTTPEventProcessor"

typedef struct scxml_chttp_processor { void *impl; } scxml_chttp_processor;
typedef struct scxml_chttp_binding { void *impl; } scxml_chttp_binding;

typedef enum scxml_chttp_decode_status {
    SCXML_CHTTP_DECODE_OK = 0,
    SCXML_CHTTP_DECODE_BAD_REQUEST,
    SCXML_CHTTP_DECODE_UNSUPPORTED_MEDIA,
    SCXML_CHTTP_DECODE_LIMIT_EXCEEDED,
    SCXML_CHTTP_DECODE_FAILED
} scxml_chttp_decode_status;

typedef struct scxml_chttp_form_entry_view {
    const char *name;
    size_t name_size;
    const char *value;
    size_t value_size;
} scxml_chttp_form_entry_view;

typedef struct scxml_chttp_ingress_view {
    const chttp_server_request_view *request;
    const scxml_chttp_form_entry_view *entries;
    size_t entry_count;
    const void *content;
    size_t content_size;
} scxml_chttp_ingress_view;

typedef scxml_chttp_decode_status (*scxml_chttp_decode_fn)(
    void *user,
    const scxml_chttp_ingress_view *ingress,
    scxml_content_view *out_data);

typedef struct scxml_chttp_resolved_target {
    const char *connection_uri;
    const char *authority;
    const char *target;
} scxml_chttp_resolved_target;

typedef int (*scxml_chttp_resolve_fn)(
    void *user,
    const char *uri,
    size_t uri_size,
    scxml_chttp_resolved_target *out_target);

typedef struct scxml_chttp_processor_config_v1 {
    uint32_t abi_version;
    size_t struct_size;
    chttp_server_config server;
    chttp_client_config client;
    const char *advertised_authority;
    const char *base_path;
    size_t endpoint_capacity;
    size_t egress_capacity;
    size_t max_access_uri_bytes;
    size_t max_event_name_bytes;
    size_t max_form_entry_count;
    size_t max_form_name_bytes;
    size_t max_form_value_bytes;
    size_t max_encoded_body_bytes;
    uint32_t request_timeout_ms;
    uint32_t worker_poll_ms;
    scxml_chttp_resolve_fn resolve;
    void *resolve_user;
} scxml_chttp_processor_config_v1;

typedef struct scxml_chttp_binding_config_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const scxml_event_io_adapter *scxml_adapter;
    void *scxml_adapter_user;
    scxml_chttp_decode_fn decode;
    void *decode_user;
} scxml_chttp_binding_config_v1;
```

The decoder and target resolver are ordinary typed C callbacks, not CMeta
callables. Their ingress, result, and policy types are local to this optional
adapter; adding those types to `CMETA_CALLABLE_TYPE_LIST` would change the
finite `cmeta_sig`/`cmeta_callable` ABI for every translation unit. Erasing the
same contract to `void *` would forfeit the type checking that could justify a
CMeta callable. CMeta remains the representation and validation mechanism for
decoded `SCXML_CONTENT_CMETA` values, while the callback remains the narrow
transport extension point.

Lifecycle and query functions are:

```c
int scxml_chttp_processor_init(
    scxml_chttp_processor *processor,
    const scxml_chttp_processor_config_v1 *config);
int scxml_chttp_processor_start(scxml_chttp_processor *processor);
int scxml_chttp_processor_stop(
    scxml_chttp_processor *processor, uint32_t timeout_ms);
int scxml_chttp_processor_destroy(scxml_chttp_processor *processor);

int scxml_chttp_binding_init(
    scxml_chttp_binding *binding,
    scxml_chttp_processor *processor,
    const scxml_chttp_binding_config_v1 *config);
const scxml_event_io_adapter *scxml_chttp_event_io_adapter(void);
void *scxml_chttp_binding_adapter_user(scxml_chttp_binding *binding);
bool scxml_chttp_binding_ioprocessor(
    const scxml_chttp_binding *binding,
    scxml_ioprocessor_descriptor *out_descriptor);
int scxml_chttp_binding_activate(
    scxml_chttp_binding *binding,
    scxml_session *session,
    const scxml_program *program);
int scxml_chttp_binding_destroy(scxml_chttp_binding *binding);

bool scxml_chttp_processor_get_stats(
    const scxml_chttp_processor *processor,
    scxml_chttp_processor_stats *out_stats);
```

`processor_start()` starts the CHTTP listener and I/O worker before a binding
is reserved, so an ephemeral port can be included in the access URI.
`binding_init()` generates a CSPRNG-backed UUID endpoint and copies its URI.
The caller installs the returned descriptor and composite adapter in
`scxml_session_config`, initializes the session, and then calls
`binding_activate()`. An inactive binding does not admit ingress and does not
dispatch its committed egress rows.

The composite adapter's `close` callback closes the downstream SCXML adapter,
stops new BasicHTTP reservations, cancels or drains that binding's published
rows, and prevents new ingress callbacks. Its `is_quiescent` returns true only
when the downstream adapter is quiescent, no handler holds the session, and no
reserved, queued, delayed, or in-flight HTTP row refers to the binding.
`binding_destroy()` therefore succeeds only after session destruction has
completed. `processor_stop()` refuses new work, stops the listener, drains or
cancels the client within the supplied deadline, joins the worker, and is
retryable after timeout. `processor_destroy()` requires stopped state and zero
live bindings.

## Outbound transaction and queue protocol

The egress path is a preallocated fixed-slot MPSC-to-single-owner queue guarded
by one processor mutex and condition variable. The session SerialExecutors are
producers; the processor I/O worker is the only CHTTP client owner and
consumer. A simple locked table is preferred to a lock-free ring because
delayed-send lookup, send-id cancellation, and per-binding close already
require indexed mutable state.

Each slot owns fixed strides for copied connection URI, authority, target,
headers, and encoded body. No pointer from `scxml_send_request`, the resolver,
or the session is retained without an associated live binding reference.
Initialization validates:

```text
slot_bytes = metadata_bytes
           + connection_uri_stride
           + authority_stride
           + target_stride
           + encoded_body_stride
total_bytes = endpoint_capacity * endpoint_bytes
            + egress_capacity * slot_bytes
```

Every addition and multiplication is checked. A zero capacity, zero string
bound, body bound larger than CHTTP's configured request bound, or aggregate
overflow fails initialization.

Slot states are:

```text
FREE -> RESERVED -> READY -> SUBMITTED -> FREE
          |            |          |
          +--discard---+          +--completion/error--+
                       +--cancel-----------------------+

READY may also carry a future due time. CANCEL_RESERVED is a transient ticket
state that changes a matching READY/SUBMITTED row only on commit.
```

`prepare_send()` performs type selection, target resolution, form/content
encoding, and one slot reservation without publishing. Full capacity returns
`SCXML_ADAPTER_FULL`; invalid payload encoding returns
`SCXML_ADAPTER_ERROR_EXECUTION`; an empty, denied, or unresolved BasicHTTP
target returns `SCXML_ADAPTER_ERROR_COMMUNICATION`. Ticket commit is
nonblocking and infallible: it changes `RESERVED` to `READY` and signals the
worker. Ticket discard returns the slot to `FREE`.

The worker preserves commit order per source binding. It waits until the due
time, submits to `chttp_async_client`, polls completions, and retains the slot
until the callback. A transport error or final non-2xx response releases the
slot and calls `scxml_session_report_adapter_error(...COMMUNICATION)` while the
binding reference still protects the session. There are no redirects, retries,
method changes, or HTTPS downgrade. URI interpretation and authorization live
solely in the resolver.

Delayed-send and cancel capabilities are advertised only when the downstream
SCXML adapter advertises them. The CHTTP layer implements the same capability
for its own rows; non-BasicHTTP requests and unmatched cancel IDs are delegated.
The downstream adapter must support every non-delay capability exposed by the
composite adapter, so capability validation cannot promise payload semantics
that delegation would reject.

Backpressure is always explicit: outbound capacity returns
`SCXML_ADAPTER_FULL`, ingress mailbox pressure returns HTTP 503, and CHTTP
client pressure keeps the committed row queued until capacity becomes
available or shutdown cancels it. Rows are never overwritten or silently
dropped.

## Inbound ownership and response mapping

CHTTP invokes the route handler serially on its server owner thread. The
handler resolves the endpoint under the processor mutex, increments the
binding's active-callback count, then releases the mutex before decoding or
calling TurboSCXML. The session and program pointers remain borrowed only
while that count is nonzero; session destruction cannot complete until the
adapter reports quiescence.

All request and parsed-form views expire when the CHTTP handler returns. The
decoder may return a callback-scoped `scxml_content_view`; the session copies
it during `scxml_session_try_send_named_with_metadata()`. The module neither
stores the decoder's view nor frees its object. The decoder must provide a
CMeta schema compatible with the target program when returning
`SCXML_CONTENT_CMETA`; existing session admission performs the final schema,
size, copy-trait, and capacity validation.

HTTP results are deterministic:

| Condition | Status |
|---|---:|
| Event admitted to external queue | 204 |
| Malformed form, duplicate reserved name, invalid event name | 400 |
| Unsupported content type selected by decoder | 415 |
| Decoder or Event-data validation failure | 422 |
| Endpoint absent | 404 |
| Binding inactive or closing | 410 |
| External mailbox full | 503 |
| Internal invariant or unexpected dependency failure | 500 |

## Capability and compatibility rules

- `TurboSCXML::SCXML` does not link CHTTP and retains its existing public
  target dependencies.
- `TurboSCXML::CHttpEventIO` exists only when
  `TURBOSCXML_ENABLE_CHTTP_EVENT_IO=ON`; configuration then requires the
  installed `TurboUtils::CHTTP` target from the same `TURBOUTILS_ROOT`.
- The implementation targets CHTTP HTTP/1 over `tcp://`. The resolver rejects
  `https:` until the selected CHTTP SDK exposes and verifies HTTPS. It must not
  translate HTTPS to plaintext.
- Existing session configurations with zero custom Event I/O descriptors and
  all existing exact Event admission calls retain their behavior.
- The public opaque processor/binding structs and versioned configs permit
  internal layout changes without ABI exposure.

## Risks and mitigations

- **HIGH — fact:** a session currently accepts one adapter and hard-codes one
  `_ioprocessors` location. A standalone HTTP adapter would replace mandatory
  SCXML routing. The composite adapter and generic descriptor table preserve
  both processors.
- **HIGH — fact:** CHTTP server handlers and the SCXML session executor run on
  different threads. Destroying a session while a handler or completion owns
  its pointer would cause use-after-free. Binding callback/outbound reference
  counts are included in the adapter's existing quiescence gate.
- **HIGH — fact:** Event I/O ticket commit must be nonblocking and infallible.
  HTTP cannot run in commit. Prepare copies into a reserved bounded row; one
  worker owns all transport progress after commit.
- **MED — inference:** a lock-free ring would complicate delay ordering and
  send-id cancellation without proven throughput benefit. A preallocated
  mutex-protected table is the initial implementation; performance changes
  require profiling and saturation benchmarks.
- **MED — fact:** CHTTP separates connection URI, authority, and origin-form
  target. The resolver is mandatory, preventing accidental URL parsing,
  authorization bypass, and SSRF-prone pass-through.
- **MED — fact:** the present CHTTP SDK is not installed in the ordinary
  `turboutils` profile used by this checkout. Feature-ON verification requires
  installing the CHTTP-enabled SDK into the normal profile root first; CMake
  must fail rather than search a fallback root.

## Verification

Implementation uses failing-first TinyTest cycles and requires:

- core tests for descriptor validation/copying, generic CMeta lookup,
  protected writes, arbitrary-name routing, wildcard/prefix equivalence,
  metadata name preservation, and capacity failures;
- pure codec tests with literal wire bodies for percent encoding/decoding,
  duplicate fields, malformed escapes, scalar formatting, raw content, and
  every configured bound;
- adapter transaction tests for reserve/commit/discard, full behavior,
  delayed order, cancellation races, delegated tickets, close, and quiescence;
- real loopback CHTTP tests for POST ingress, 204-after-admission, non-2xx
  mapping, self-send, mailbox pressure, stop timeout/retry, and concurrent
  session destruction;
- promotion of W3C rows 201, 509, 510, 513, 518, 519, 520, 522, 531, 532,
  534, 567, and 577 using local CMeta fixtures and strict transport probes;
- Release and Debug/ASan focused tests, complete CTest, strict W3C corpus,
  installed feature-OFF and feature-ON C/C++ consumers, and `git diff --check`.


