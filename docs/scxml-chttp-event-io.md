# CHTTP BasicHTTP Event I/O Processor

`TurboSCXML::CHttpEventIO` is an optional C11 composite Event I/O adapter. It
implements the W3C BasicHTTP Event I/O Processor over Rocida CHTTP while
delegating the mandatory SCXML Event I/O Processor to an adapter supplied by
the host. The core `TurboSCXML::SCXML` target remains transport-free.

## Build and consume

Enable `TURBOSCXML_ENABLE_CHTTP_EVENT_IO=ON`, or use the checked-in
`win-dev-chttp-user` / `win-release-chttp-user` presets. The selected Rocida
installation must export `Rocida::CHTTP` from the same `ROCIDA_ROOT` as the
other dependencies.

An installed consumer requests and links the component explicitly:

```cmake
find_package(TurboSCXML CONFIG REQUIRED COMPONENTS CHttpEventIO
  PATHS "${TURBOSCXML_ROOT}" NO_DEFAULT_PATH)
target_link_libraries(app PRIVATE TurboSCXML::CHttpEventIO)
```

Include `<scxml/chttp_event_io.h>`. The installed C and C++ compile/link
examples are `tests/install_consumer/event_io_main.c` and
`tests/install_consumer/event_io_main.cpp`; together they exercise every
public lifecycle/query symbol and both-language header compatibility.

## Required lifecycle

The order is strict:

```text
processor_init -> processor_start -> binding_init
-> install descriptor + adapter in session config
-> session_init -> binding_activate
-> session_destroy -> binding_destroy
-> processor_stop -> processor_destroy
```

`processor_start` precedes `binding_init` because an ephemeral listener port
must be known before the binding publishes its access URI. The descriptor from
`scxml_chttp_binding_ioprocessor()` must be placed in
`scxml_session_config.ioprocessors`, and the composite adapter/user pair must
be installed before session initialization:

```c
scxml_chttp_processor processor = {0};
scxml_chttp_binding binding = {0};
scxml_ioprocessor_descriptor basichttp = {0};

scxml_chttp_processor_config_v1 processor_config = {
    .abi_version = SCXML_CHTTP_ABI_V1,
    .struct_size = sizeof(processor_config),
    .server = server_config,       /* every CHTTP capacity/deadline is finite */
    .client = client_config,
    .advertised_authority = "127.0.0.1",
    .base_path = "/scxml",
    .endpoint_capacity = 16,
    .egress_capacity = 32,
    .max_access_uri_bytes = 256,
    .max_event_name_bytes = 128,
    .max_form_entry_count = 16,
    .max_form_name_bytes = 64,
    .max_form_value_bytes = 256,
    .max_encoded_body_bytes = 4096,
    .request_timeout_ms = 2000,
    .worker_poll_ms = 2,
    .resolve = authorize_http_target,
    .resolve_user = &resolver_state
};
scxml_chttp_binding_config_v1 binding_config = {
    .abi_version = SCXML_CHTTP_ABI_V1,
    .struct_size = sizeof(binding_config),
    .scxml_adapter = host_scxml_adapter,
    .scxml_adapter_user = host_scxml_user,
    .decode = decode_application_fields,
    .decode_user = &decoder_state
};

if (scxml_chttp_processor_init(&processor, &processor_config) != TURBO_OK ||
    scxml_chttp_processor_start(&processor) != TURBO_OK ||
    scxml_chttp_binding_init(&binding, &processor, &binding_config) !=
        TURBO_OK ||
    !scxml_chttp_binding_ioprocessor(&binding, &basichttp)) {
    /* unwind whichever owning steps succeeded */
}

scxml_session_config session_config = {
    .program = &program,
    .executor = &executor,
    .external_event_capacity = 16,
    .internal_event_capacity = 16,
    .completion_capacity = 8,
    .microstep_limit = 64,
    .max_storage_bytes = 1024 * 1024,
    .effect_capacity = 16,
    .adapter_internal_event_capacity = 8,
    .delayed_send_capacity = 16,
    .event_io = scxml_chttp_event_io_adapter(),
    .adapter_user = scxml_chttp_binding_adapter_user(&binding),
    .ioprocessors = &basichttp,
    .ioprocessor_count = 1
};

if (scxml_session_init(&session, &session_config) ==
        CFLOW_STATECHART_INSTANCE_OK &&
    scxml_chttp_binding_activate(&binding, &session, &program) == TURBO_OK) {
    /* run the session; destruction may be retried while transport drains */
}
```

The example intentionally leaves `server_config`, `client_config`, the
downstream SCXML router, decoder, and resolver as host policy. The concrete
compiling loopback setup is exercised by `scxml_w3c_conformance_test` and the
four `scxml_chttp_*_test` executables.

## Resolver and decoder contracts

The resolver is mandatory and deny-by-default. It receives untrusted logical
target bytes and must return an authorized CHTTP `connection_uri`, HTTP
authority, and origin-form target. Returned strings are callback-scoped and
copied before the callback returns. Authorize a finite host/path allowlist;
never treat arbitrary SCXML target text as permission to connect.

V1 supports HTTP/1 over CHTTP `tcp://` connections. It rejects HTTPS rather
than downgrading it, does not follow redirects, and does not retry. If TLS is
required, provide another Event I/O adapter until the selected transport can
verify HTTPS end to end.

The decoder receives request/form views owned by CHTTP. It may return no data,
text/XML/scalar data, or a callback-scoped CMeta object. TurboSCXML copies the
returned content during mailbox admission; the decoder retains ownership and
must not release borrowed request memory early. A CMeta descriptor must match
the session's compiled root schema.

These are typed C callbacks, not CMeta lambdas. Adding transport-local
signatures to the build-wide CMeta callable list would change the callable ABI
for unrelated translation units.

## Wire behavior and backpressure

Outbound named payloads use
`application/x-www-form-urlencoded`. A nonempty send Event becomes the first
`_scxmleventname` field; `namelist` and `param` entries follow in document
order. A param-only send may supply `_scxmleventname` itself. Text/XML content
is copied as the complete body rather than form-wrapped.

Ingress accepts POST on the binding access URI. One reserved form field names
the Event; without it, the Event name is `HTTP.POST`. Other form entries and
raw content are offered to the decoder. A 204 response means the Event and its
metadata were copied into the external queue. A full external mailbox returns
503, so clients may apply their own policy without any silent overwrite.

Malformed forms return 400, unknown endpoints 404, closing/inactive bindings
410, unsupported decoder media 415, and decoder/data admission failures 422.
Outbound resolution, transport, timeout, and non-2xx failures become the
standard internal `error.communication` Event. Payload-expression or encoding
failures become `error.execution`.

All capacities and positive connect/read/write/request deadlines are host
configuration. Commit/discard callbacks only publish or release preallocated
rows; the processor worker owns network progress. During shutdown,
`scxml_session_destroy()` and `scxml_chttp_binding_destroy()` may report a
retryable busy/would-block result until active handlers and outbound requests
quiesce.
