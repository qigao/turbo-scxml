#include <scxml/chttp_event_io.h>

#include <cflow/executor.h>
#include <tinytest.h>
#include <salts/error_codes.h>
#include <salts/thread.h>

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Struct(ingress_data,
    (int, param1)
);

static bool ingress_data_copy(void *destination, const void *source) {
    if (destination == NULL || source == NULL) return false;
    memcpy(destination, source, sizeof(ingress_data));
    return true;
}

static void ingress_data_move(void *destination, void *source) {
    if (destination == NULL || source == NULL) return;
    memcpy(destination, source, sizeof(ingress_data));
    memset(source, 0, sizeof(ingress_data));
}

static void ingress_data_destroy(void *value) { (void)value; }

static const cmeta_type_identity INGRESS_DATA_IDENTITY =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.chttp.ingress.data");
static const cmeta_type_traits INGRESS_DATA_TRAITS = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = ingress_data_copy,
    .move_construct = ingress_data_move,
    .destroy = ingress_data_destroy};
static const cmeta_type_desc INGRESS_DATA_TYPE = {
    .name = "ingress_data", .size = sizeof(ingress_data),
    .align = _Alignof(ingress_data), .kind = CMETA_T_OBJECT,
    .traits = &INGRESS_DATA_TRAITS, .identity = &INGRESS_DATA_IDENTITY};
static const cmeta_data_field_desc INGRESS_DATA_FIELDS[] = {
    {"test.scxml.chttp.ingress.data.param1", "param1",
     offsetof(ingress_data, param1), &cmeta_data_int}};
static const cmeta_data_struct_shape INGRESS_DATA_SHAPE = {
    .layout = StructMeta(ingress_data), .fields = INGRESS_DATA_FIELDS,
    .field_count = 1u};
static const cmeta_data_desc INGRESS_DATA_DESC = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.chttp.ingress.data.schema",
    .display_name = "CHTTP ingress data", .kind = CMETA_DATA_STRUCT,
    .storage_type = &INGRESS_DATA_TYPE, .shape = &INGRESS_DATA_SHAPE};

typedef struct decode_probe {
    scxml_chttp_decode_status status;
    size_t calls;
    size_t entries;
    atomic_bool block;
    atomic_bool entered;
    atomic_bool release;
    bool return_cmeta;
    ingress_data data;
} decode_probe;

typedef struct ingress_fixture {
    scxml_chttp_processor processor;
    scxml_chttp_binding binding;
    scxml_program program;
    cflow_executor executor;
    bool executor_live;
    scxml_session session;
    scxml_ioprocessor_descriptor descriptor;
    chttp_client client;
    decode_probe decode;
    size_t downstream_closes;
    char connection_uri[128];
    char authority[96];
    char target[256];
} ingress_fixture;

typedef struct blocking_call {
    ingress_fixture *fixture;
    unsigned int status;
} blocking_call;

typedef struct executor_blocker {
    atomic_bool entered;
    atomic_bool release;
} executor_blocker;

static native_io_backend_kind test_backend(void) {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
    return NATIVE_IO_BACKEND_EPOLL;
#else
    return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

static cnet_client_config test_network(size_t capacity) {
    return (cnet_client_config){
        .backend = test_backend(), .connection_capacity = capacity,
        .command_capacity = 16u, .request_capacity = 8u,
        .completion_batch_capacity = 8u, .event_capacity = 16u,
        .max_send_bytes = 4096u, .receive_buffer_bytes = 256u,
        .connect_timeout_ms = 500u, .read_timeout_ms = 500u,
        .write_timeout_ms = 500u};
}

static chttp_server_config test_server_config(void) {
    return (chttp_server_config){
        .host = "127.0.0.1", .port = 0u, .backlog = 4u,
        .network = test_network(4u),
        .route_capacity = 4u, .middleware_capacity = 1u,
        .max_route_middleware_count = 1u,
        .max_route_param_count = 1u, .max_route_param_bytes = 64u,
        .max_target_bytes = 256u,
        .max_header_count = 8u, .max_header_bytes = 512u,
        .max_request_body_bytes = 256u,
        .max_response_header_count = 8u,
        .max_response_header_bytes = 512u,
        .max_response_body_bytes = 64u,
        .session_capacity = 1u, .session_entry_capacity = 1u,
        .max_session_key_bytes = 16u, .max_session_value_bytes = 16u,
        .session_idle_timeout_ms = 1000u,
        .session_cookie_name = "ingress_sid", .poll_slice_ms = 1u};
}

static chttp_client_config test_client_config(void) {
    return (chttp_client_config){
        .network = test_network(2u), .request_capacity = 1u,
        .max_start_line_bytes = 256u,
        .max_header_count = 8u, .max_header_bytes = 512u,
        .max_request_body_bytes = 256u, .max_response_body_bytes = 64u,
        .max_informational_responses = 1u};
}

static int unused_resolver(
    void *user, const char *uri, size_t uri_size,
    scxml_chttp_resolved_target *out_target) {
    (void)user; (void)uri; (void)uri_size; (void)out_target;
    return SALTS_EINVAL;
}

static scxml_adapter_status unused_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    (void)user; (void)request; (void)out_ticket;
    if (out_error != NULL) *out_error = "unused";
    return SCXML_ADAPTER_CLOSED;
}

static scxml_adapter_status unused_cancel(
    void *user, const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    (void)user; (void)request; (void)out_ticket;
    if (out_error != NULL) *out_error = "unused";
    return SCXML_ADAPTER_CLOSED;
}

static void downstream_close(void *user) { ++*(size_t *)user; }
static bool downstream_quiescent(void *user) {
    return *(size_t *)user != 0u;
}

static const scxml_event_io_adapter DOWNSTREAM = {
    .abi_version = SCXML_ADAPTER_ABI,
    .struct_size = sizeof(scxml_event_io_adapter),
    .capabilities = SCXML_EVENT_IO_CAP_SEND |
        SCXML_EVENT_IO_CAP_DELAYED_SEND | SCXML_EVENT_IO_CAP_CANCEL |
        SCXML_EVENT_IO_CAP_PAYLOAD | SCXML_EVENT_IO_CAP_CONTENT,
    .prepare_send = unused_send, .prepare_cancel = unused_cancel,
    .close = downstream_close, .is_quiescent = downstream_quiescent};

static scxml_chttp_decode_status test_decode(
    void *user, const scxml_chttp_ingress_view *ingress,
    scxml_content_view *out_data) {
    decode_probe *probe = (decode_probe *)user;
    size_t index;
    ++probe->calls;
    probe->entries = ingress->entry_count;
    if (atomic_load(&probe->block)) {
        atomic_store(&probe->entered, true);
        while (!atomic_load(&probe->release)) salts_thread_yield();
    }
    if (probe->status != SCXML_CHTTP_DECODE_OK) return probe->status;
    if (!probe->return_cmeta) {
        *out_data = (scxml_content_view){.kind = SCXML_CONTENT_INVALID};
        return SCXML_CHTTP_DECODE_OK;
    }
    probe->data.param1 = 0;
    for (index = 0u; index < ingress->entry_count; ++index) {
        if (ingress->entries[index].name_size == 6u &&
            memcmp(ingress->entries[index].name, "param1", 6u) == 0)
            probe->data.param1 = atoi(ingress->entries[index].value);
    }
    *out_data = (scxml_content_view){
        .kind = SCXML_CONTENT_CMETA,
        .schema = &INGRESS_DATA_DESC, .object = &probe->data};
    return SCXML_CHTTP_DECODE_OK;
}

static void block_executor(void *user) {
    executor_blocker *blocker = (executor_blocker *)user;
    atomic_store(&blocker->entered, true);
    while (!atomic_load(&blocker->release)) salts_thread_yield();
}

static bool split_access_uri(ingress_fixture *fixture) {
    const char *authority;
    const char *slash;
    size_t authority_size;
    if (fixture->descriptor.location_size < 8u ||
        memcmp(fixture->descriptor.location, "http://", 7u) != 0)
        return false;
    authority = fixture->descriptor.location + 7u;
    slash = strchr(authority, '/');
    if (slash == NULL) return false;
    authority_size = (size_t)(slash - authority);
    if (authority_size >= sizeof(fixture->authority) ||
        strlen(slash) >= sizeof(fixture->target))
        return false;
    memcpy(fixture->authority, authority, authority_size);
    fixture->authority[authority_size] = '\0';
    (void)snprintf(fixture->connection_uri,
                   sizeof(fixture->connection_uri),
                   "tcp://%s", fixture->authority);
    (void)snprintf(fixture->target, sizeof(fixture->target), "%s", slash);
    return true;
}

static bool fixture_init(
    ingress_fixture *fixture, bool activate, bool with_decoder,
    bool cmeta_profile) {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
        "<state id='waiting'><transition event='HTTP.POST' target='pass'/>"
        "</state><final id='pass'/></scxml>";
    static const char cmeta_source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "datamodel='cmeta'><state id='waiting'><transition "
        "event='HTTP.POST' cond='_event.data.param1 == 2 &amp;&amp; "
        "_event.origintype == &quot;"
        SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI
        "&quot;' target='pass'/>"
        "</state><final id='pass'/></scxml>";
    static const char raw_source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "datamodel='cmeta'><state id='waiting'><transition "
        "event='HTTP.POST' cond='_event.data == &quot;raw&quot;' target='pass'/>"
        "</state><final id='pass'/></scxml>";
    scxml_chttp_processor_config_v1 processor_config;
    scxml_chttp_binding_config_v1 binding_config;
    scxml_session_config session_config;
    scxml_diagnostic diagnostic = {0};
    ingress_data initial_data = {0};
    int status;
    memset(fixture, 0, sizeof(*fixture));
    processor_config = (scxml_chttp_processor_config_v1){
        .abi_version = SCXML_CHTTP_ABI_V1,
        .struct_size = sizeof(processor_config),
        .server = test_server_config(), .client = test_client_config(),
        .advertised_authority = "127.0.0.1", .base_path = "/scxml",
        .endpoint_capacity = 2u, .egress_capacity = 2u,
        .max_access_uri_bytes = 256u, .max_event_name_bytes = 64u,
        .max_form_entry_count = 8u, .max_form_name_bytes = 64u,
        .max_form_value_bytes = 128u, .max_encoded_body_bytes = 256u,
        .request_timeout_ms = 500u, .worker_poll_ms = 1u,
        .resolve = unused_resolver};
    if (scxml_chttp_processor_init(
            &fixture->processor, &processor_config) != SALTS_OK ||
        scxml_chttp_processor_start(&fixture->processor) != SALTS_OK)
        return false;
    binding_config = (scxml_chttp_binding_config_v1){
        .abi_version = SCXML_CHTTP_ABI_V1,
        .struct_size = sizeof(binding_config),
        .scxml_adapter = &DOWNSTREAM,
        .scxml_adapter_user = &fixture->downstream_closes,
        .decode = with_decoder ? test_decode : NULL,
        .decode_user = &fixture->decode};
    if (scxml_chttp_binding_init(
            &fixture->binding, &fixture->processor,
            &binding_config) != SALTS_OK ||
        !scxml_chttp_binding_ioprocessor(
            &fixture->binding, &fixture->descriptor) ||
        !split_access_uri(fixture) ||
        chttp_client_init(&fixture->client, &processor_config.client) !=
            SALTS_OK)
        return false;
    if (!activate) return true;
    if (cmeta_profile) {
        const scxml_cmeta_compile_options_v1 options =
            scxml_cmeta_default_compile_options(&INGRESS_DATA_DESC);
        const char *selected_source = with_decoder
            ? cmeta_source : raw_source;
        const size_t selected_size = with_decoder
            ? sizeof(cmeta_source) - 1u : sizeof(raw_source) - 1u;
        status = (int)scxml_compile_cmeta(
            &fixture->program, selected_source, selected_size,
            NULL, &options, &diagnostic);
        fixture->decode.return_cmeta = with_decoder;
    } else {
        status = (int)scxml_compile(
            &fixture->program, source, sizeof(source) - 1u,
            NULL, &diagnostic);
    }
    if (status != SCXML_OK ||
        !cflow_executor_serial_init(&fixture->executor))
        return false;
    fixture->executor_live = true;
    session_config = (scxml_session_config){
        .program = &fixture->program, .executor = &fixture->executor,
        .external_event_capacity = 1u, .internal_event_capacity = 1u,
        .completion_capacity = 1u, .microstep_limit = 8u,
        .max_storage_bytes = 4096u, .effect_capacity = 2u,
        .adapter_internal_event_capacity = 2u,
        .delayed_send_capacity = 2u,
        .event_io = scxml_chttp_binding_event_io_adapter(&fixture->binding),
        .adapter_user =
            scxml_chttp_binding_adapter_user(&fixture->binding),
        .ioprocessors = &fixture->descriptor, .ioprocessor_count = 1u};
    if (cmeta_profile) {
        const scxml_cmeta_session_options_v1 options = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(options), .initial_state = &initial_data};
        status = (int)scxml_session_init_cmeta(
            &fixture->session, &session_config, &options);
    } else {
        status = (int)scxml_session_init(&fixture->session, &session_config);
    }
    return status == CFLOW_STATECHART_INSTANCE_OK &&
        scxml_chttp_binding_activate(
            &fixture->binding, &fixture->session,
            &fixture->program) == SALTS_OK;
}

static unsigned int post_bytes(
    ingress_fixture *fixture, const char *target,
    const char *content_type, const void *body, size_t body_size) {
    chttp_header header = {"Content-Type", content_type};
    chttp_options options = {
        .connection_uri = fixture->connection_uri,
        .authority = fixture->authority, .target = target,
        .headers = &header, .header_count = 1u,
        .body = body, .body_size = body_size, .timeout_ms = 1000u};
    chttp_response response = {0};
    chttp_error error = {0};
    unsigned int status = 0u;
    if (chttp_post(&fixture->client, &options, &response, &error) == SALTS_OK)
        status = response.status_code;
    chttp_response_destroy(&response);
    return status;
}

static unsigned int post(
    ingress_fixture *fixture, const char *target,
    const char *content_type, const char *body) {
    return post_bytes(
        fixture, target, content_type, body, strlen(body));
}

static void blocking_http(void *user) {
    blocking_call *call = (blocking_call *)user;
    call->status = post(
        call->fixture, call->fixture->target,
        "application/x-www-form-urlencoded", "param1=2");
}

static void fixture_destroy(ingress_fixture *fixture) {
    if (fixture->client.impl != NULL)
        (void)chttp_client_destroy(&fixture->client, 1000u);
    if (fixture->session.impl != NULL)
        (void)scxml_session_destroy(&fixture->session);
    if (fixture->binding.impl != NULL)
        (void)scxml_chttp_binding_destroy(&fixture->binding);
    if (fixture->processor.impl != NULL) {
        (void)scxml_chttp_processor_stop(&fixture->processor, 1000u);
        (void)scxml_chttp_processor_destroy(&fixture->processor);
    }
    if (fixture->executor_live) cflow_executor_destroy(&fixture->executor);
    if (fixture->program.impl != NULL) scxml_program_destroy(&fixture->program);
}

spec("TurboSCXML CHTTP ingress") {
    it("returns 204 only after admission and 503 at mailbox capacity") {
        ingress_fixture fixture;
        executor_blocker blocker = {0};
        cflow_statechart_instance_stats stats = {0};
        scxml_chttp_processor_stats processor_stats = {0};
        check_true(fixture_init(&fixture, true, true, false));
        check_equal(cflow_executor_try_post(
                        &fixture.executor, block_executor, &blocker),
                    CFLOW_ADMISSION_ACCEPTED);
        while (!atomic_load(&blocker.entered)) salts_thread_yield();
        check_equal(post(&fixture, fixture.target,
                        "application/x-www-form-urlencoded", "param1=2"),
                    204u);
        check_equal(post(&fixture, fixture.target,
                        "application/x-www-form-urlencoded", "param1=2"),
                    503u);
        atomic_store(&blocker.release, true);
        check_true(cflow_executor_wait_idle(&fixture.executor));
        check_true(scxml_session_get_stats(&fixture.session, &stats));
        check_true(stats.done);
        check_equal(fixture.decode.calls, (size_t)2u);
        check_equal(fixture.decode.entries, (size_t)1u);
        check_true(scxml_chttp_processor_get_stats(
            &fixture.processor, &processor_stats));
        check_equal(processor_stats.ingress_requests, UINT64_C(2));
        check_equal(processor_stats.ingress_admitted, UINT64_C(1));
        check_equal(processor_stats.ingress_rejected, UINT64_C(1));
        fixture_destroy(&fixture);
    }

    it("copies decoder CMeta data and admits raw content") {
        ingress_fixture cmeta;
        ingress_fixture raw;
        executor_blocker cmeta_blocker = {0};
        executor_blocker raw_blocker = {0};
        char raw_body[] = "raw";
        cflow_statechart_instance_stats stats = {0};
        check_true(fixture_init(&cmeta, true, true, true));
        check_equal(cflow_executor_try_post(
                        &cmeta.executor, block_executor, &cmeta_blocker),
                    CFLOW_ADMISSION_ACCEPTED);
        while (!atomic_load(&cmeta_blocker.entered)) salts_thread_yield();
        check_equal(post(&cmeta, cmeta.target,
                        "application/x-www-form-urlencoded", "param1=2"),
                    204u);
        cmeta.decode.data.param1 = 99;
        atomic_store(&cmeta_blocker.release, true);
        check_true(cflow_executor_wait_idle(&cmeta.executor));
        check_true(scxml_session_get_stats(&cmeta.session, &stats));
        check_true(stats.done);
        fixture_destroy(&cmeta);

        check_true(fixture_init(&raw, true, false, true));
        check_equal(cflow_executor_try_post(
                        &raw.executor, block_executor, &raw_blocker),
                    CFLOW_ADMISSION_ACCEPTED);
        while (!atomic_load(&raw_blocker.entered)) salts_thread_yield();
        check_equal(post_bytes(&raw, raw.target,
                        "text/plain; charset=utf-8",
                        raw_body, sizeof(raw_body) - 1u), 204u);
        memcpy(raw_body, "bad", sizeof(raw_body) - 1u);
        check_equal(post_bytes(&raw, raw.target,
                        "text/plain; charset=utf-8",
                        raw_body, sizeof(raw_body) - 1u), 503u);
        atomic_store(&raw_blocker.release, true);
        check_true(cflow_executor_wait_idle(&raw.executor));
        check_true(scxml_session_get_stats(&raw.session, &stats));
        check_true(stats.done);
        fixture_destroy(&raw);
    }

    it("rejects malformed raw UTF-8 and embedded NUL as Event data") {
        static const unsigned char invalid_utf8[] = {0xc0u, 0xafu};
        static const unsigned char embedded_nul[] = {'o', 'k', 0u, 'x'};
        ingress_fixture fixture;
        check_true(fixture_init(&fixture, true, false, true));
        check_equal(post_bytes(
                        &fixture, fixture.target, "text/plain",
                        invalid_utf8, sizeof(invalid_utf8)), 422u);
        check_equal(post_bytes(
                        &fixture, fixture.target, "text/plain",
                        embedded_nul, sizeof(embedded_nul)), 422u);
        fixture_destroy(&fixture);
    }

    it("maps every deterministic ingress rejection status") {
        ingress_fixture active;
        ingress_fixture inactive;
        char missing[64];
        check_true(fixture_init(&active, true, true, false));
        check_equal(post(&active, active.target,
                        "application/x-www-form-urlencoded",
                        "_scxmleventname=%GG"), 400u);
        check_equal(post(&active, active.target,
                        "application/x-www-form-urlencoded",
                        "_scxmleventname=a&_scxmleventname=b"), 400u);
        check_equal(post(&active, active.target,
                        "application/x-www-form-urlencoded",
                        "_scxmleventname=bad%20name"), 400u);
        (void)snprintf(missing, sizeof(missing), "/scxml/missing");
        check_equal(post(&active, missing,
                        "application/x-www-form-urlencoded", "x=1"), 404u);
        active.decode.status = SCXML_CHTTP_DECODE_UNSUPPORTED_MEDIA;
        check_equal(post(&active, active.target,
                        "application/octet-stream", "raw"), 415u);
        active.decode.status = SCXML_CHTTP_DECODE_FAILED;
        check_equal(post(&active, active.target, "text/plain", "raw"), 422u);
        active.decode.status = (scxml_chttp_decode_status)99;
        check_equal(post(&active, active.target, "text/plain", "raw"), 500u);
        fixture_destroy(&active);

        check_true(fixture_init(&inactive, false, true, false));
        check_equal(post(&inactive, inactive.target,
                        "application/x-www-form-urlencoded", "x=1"), 410u);
        fixture_destroy(&inactive);
    }

    it("keeps session destruction behind an active decoder callback") {
        ingress_fixture fixture;
        blocking_call call = {0};
        salts_thread_t http_thread = NULL;
        check_true(fixture_init(&fixture, true, true, false));
        call.fixture = &fixture;
        atomic_store(&fixture.decode.block, true);
        check_equal(salts_thread_create(
                        &http_thread, blocking_http, &call), SALTS_OK);
        while (!atomic_load(&fixture.decode.entered)) salts_thread_yield();
        check_equal(scxml_session_destroy(&fixture.session),
                    CFLOW_STATECHART_INSTANCE_WOULD_BLOCK);
        check_not_null(fixture.session.impl);
        atomic_store(&fixture.decode.release, true);
        check_equal(salts_thread_join(&http_thread), SALTS_OK);
        salts_thread_destroy(&http_thread);
        check_equal(call.status, 410u);
        check_equal(scxml_session_destroy(&fixture.session),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_null(fixture.session.impl);
        fixture_destroy(&fixture);
    }
}
