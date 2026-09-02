#include <scxml/chttp_event_io.h>

#include <cflow/executor.h>
#include <tinytest.h>
#include <turbo/error_codes.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct downstream_probe {
    size_t sends;
    size_t cancels;
    size_t closes;
    bool quiescent;
} downstream_probe;

static native_io_backend_kind test_backend(void) {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
    return NATIVE_IO_BACKEND_EPOLL;
#else
    return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

static cnet_client_config test_network(size_t connections) {
    return (cnet_client_config){
        .backend = test_backend(),
        .connection_capacity = connections,
        .command_capacity = 16u,
        .request_capacity = 8u,
        .completion_batch_capacity = 8u,
        .event_capacity = 16u,
        .max_send_bytes = 4096u,
        .receive_buffer_bytes = 256u,
        .connect_timeout_ms = 1000u,
        .read_timeout_ms = 1000u,
        .write_timeout_ms = 1000u};
}

static chttp_server_config test_server_config(void) {
    return (chttp_server_config){
        .host = "127.0.0.1",
        .port = 0u,
        .backlog = 4u,
        .network = test_network(4u),
        .route_capacity = 4u,
        .middleware_capacity = 1u,
        .max_route_middleware_count = 1u,
        .max_route_param_count = 1u,
        .max_route_param_bytes = 64u,
        .max_target_bytes = 256u,
        .max_header_count = 8u,
        .max_header_bytes = 512u,
        .max_request_body_bytes = 256u,
        .max_response_header_count = 8u,
        .max_response_header_bytes = 512u,
        .max_response_body_bytes = 64u,
        .session_capacity = 1u,
        .session_entry_capacity = 1u,
        .max_session_key_bytes = 16u,
        .max_session_value_bytes = 16u,
        .session_idle_timeout_ms = 1000u,
        .session_cookie_name = "scxml_sid",
        .poll_slice_ms = 2u};
}

static chttp_client_config test_client_config(void) {
    return (chttp_client_config){
        .network = test_network(2u),
        .request_capacity = 2u,
        .max_start_line_bytes = 256u,
        .max_header_count = 8u,
        .max_header_bytes = 512u,
        .max_request_body_bytes = 256u,
        .max_response_body_bytes = 64u,
        .max_informational_responses = 1u};
}

static int resolve_target(
    void *user, const char *uri, size_t uri_size,
    scxml_chttp_resolved_target *out_target) {
    (void)user;
    (void)uri;
    (void)uri_size;
    if (out_target == NULL) return 0;
    *out_target = (scxml_chttp_resolved_target){
        "tcp://127.0.0.1:9", "127.0.0.1:9", "/event"};
    return 1;
}

static scxml_chttp_processor_config_v1 test_processor_config(void) {
    return (scxml_chttp_processor_config_v1){
        .abi_version = SCXML_CHTTP_ABI_V1,
        .struct_size = sizeof(scxml_chttp_processor_config_v1),
        .server = test_server_config(),
        .client = test_client_config(),
        .advertised_authority = "127.0.0.1",
        .base_path = "/scxml",
        .endpoint_capacity = 2u,
        .egress_capacity = 2u,
        .max_access_uri_bytes = 256u,
        .max_event_name_bytes = 64u,
        .max_form_entry_count = 8u,
        .max_form_name_bytes = 64u,
        .max_form_value_bytes = 128u,
        .max_encoded_body_bytes = 256u,
        .request_timeout_ms = 1000u,
        .worker_poll_ms = 2u,
        .resolve = resolve_target};
}

static scxml_adapter_status downstream_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    downstream_probe *probe = (downstream_probe *)user;
    (void)request;
    (void)out_ticket;
    if (probe != NULL) ++probe->sends;
    if (out_error != NULL) *out_error = "downstream closed";
    return SCXML_ADAPTER_CLOSED;
}

static scxml_adapter_status downstream_cancel(
    void *user, const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    downstream_probe *probe = (downstream_probe *)user;
    (void)request;
    (void)out_ticket;
    if (probe != NULL) ++probe->cancels;
    if (out_error != NULL) *out_error = "downstream closed";
    return SCXML_ADAPTER_CLOSED;
}

static void downstream_close(void *user) {
    downstream_probe *probe = (downstream_probe *)user;
    if (probe != NULL) ++probe->closes;
}

static bool downstream_is_quiescent(void *user) {
    downstream_probe *probe = (downstream_probe *)user;
    return probe != NULL && probe->quiescent;
}

static const scxml_event_io_adapter DOWNSTREAM_ADAPTER = {
    .abi_version = SCXML_ADAPTER_ABI,
    .struct_size = sizeof(scxml_event_io_adapter),
    .capabilities = SCXML_EVENT_IO_CAP_SEND |
        SCXML_EVENT_IO_CAP_DELAYED_SEND |
        SCXML_EVENT_IO_CAP_CANCEL |
        SCXML_EVENT_IO_CAP_PAYLOAD |
        SCXML_EVENT_IO_CAP_CONTENT,
    .prepare_send = downstream_send,
    .prepare_cancel = downstream_cancel,
    .close = downstream_close,
    .is_quiescent = downstream_is_quiescent};

static scxml_chttp_decode_status decode_ingress(
    void *user, const scxml_chttp_ingress_view *ingress,
    scxml_content_view *out_data) {
    (void)user;
    (void)ingress;
    if (out_data == NULL) return SCXML_CHTTP_DECODE_FAILED;
    *out_data = (scxml_content_view){.kind = SCXML_CONTENT_INVALID};
    return SCXML_CHTTP_DECODE_OK;
}

static scxml_chttp_binding_config_v1 test_binding_config(
    downstream_probe *probe) {
    return (scxml_chttp_binding_config_v1){
        .abi_version = SCXML_CHTTP_ABI_V1,
        .struct_size = sizeof(scxml_chttp_binding_config_v1),
        .scxml_adapter = &DOWNSTREAM_ADAPTER,
        .scxml_adapter_user = probe,
        .decode = decode_ingress};
}

static void expect_processor_rejected(
    const scxml_chttp_processor_config_v1 *config, int expected) {
    scxml_chttp_processor processor = {0};
    check_equal(scxml_chttp_processor_init(&processor, config), expected);
    check_null(processor.impl);
}

static void close_and_destroy_binding(
    scxml_chttp_binding *binding, downstream_probe *probe) {
    const scxml_event_io_adapter *adapter = scxml_chttp_event_io_adapter();
    adapter->close(scxml_chttp_binding_adapter_user(binding));
    probe->quiescent = true;
    check_true(adapter->is_quiescent(
        scxml_chttp_binding_adapter_user(binding)));
    check_equal(scxml_chttp_binding_destroy(binding), TURBO_OK);
}

spec("TurboSCXML CHTTP processor lifecycle") {
    it("rejects partial invalid and overflowing processor contracts") {
        scxml_chttp_processor_config_v1 config = test_processor_config();

        expect_processor_rejected(NULL, TURBO_EINVAL);
        config.abi_version = 0u;
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.struct_size = offsetof(
            scxml_chttp_processor_config_v1, resolve_user);
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.server.network.command_capacity = 3u;
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.client.network.command_capacity = 3u;
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.endpoint_capacity = 0u;
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.egress_capacity = 0u;
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.max_access_uri_bytes = 0u;
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.max_event_name_bytes = 0u;
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.max_form_entry_count = 0u;
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.max_form_name_bytes = 0u;
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.max_form_value_bytes = 0u;
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.max_encoded_body_bytes = 0u;
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.max_encoded_body_bytes =
            config.client.max_request_body_bytes + 1u;
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.worker_poll_ms = 0u;
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.resolve = NULL;
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.advertised_authority = NULL;
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.base_path = "relative";
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.base_path = "/events?bad";
        expect_processor_rejected(&config, TURBO_EINVAL);
        config = test_processor_config();
        config.endpoint_capacity = SIZE_MAX;
        expect_processor_rejected(&config, TURBO_ERANGE);
    }

    it("enforces processor and reserved binding state gates") {
        scxml_chttp_processor_config_v1 processor_config =
            test_processor_config();
        scxml_chttp_processor processor = {0};
        scxml_chttp_binding binding = {0};
        downstream_probe probe = {0};
        scxml_chttp_binding_config_v1 binding_config =
            test_binding_config(&probe);
        const scxml_event_io_adapter *composite =
            scxml_chttp_event_io_adapter();
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;
        scxml_send_request send_request = {
            .event = "go", .event_size = 2u,
            .target = "http://target", .target_size = 13u,
            .type = SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI,
            .type_size = sizeof(SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI) - 1u};
        scxml_cancel_request cancel_request = {
            .send_id = "id", .send_id_size = 2u};

        check_equal(scxml_chttp_processor_init(
                        &processor, &processor_config), TURBO_OK);
        check_equal(scxml_chttp_processor_init(
                        &processor, &processor_config), TURBO_EALREADY);
        check_equal(scxml_chttp_processor_destroy(&processor), TURBO_EBUSY);
        check_equal(scxml_chttp_processor_start(&processor), TURBO_OK);
        check_equal(scxml_chttp_processor_start(&processor), TURBO_EALREADY);
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &binding_config), TURBO_OK);
        check_equal(composite->prepare_send(
                        scxml_chttp_binding_adapter_user(&binding),
                        &send_request, &ticket, &error),
                    SCXML_ADAPTER_ACCEPTED);
        check_equal(probe.sends, (size_t)0u);
        ticket.discard(ticket.user);
        send_request.type = NULL;
        send_request.type_size = 0u;
        check_equal(composite->prepare_send(
                        scxml_chttp_binding_adapter_user(&binding),
                        &send_request, &ticket, &error),
                    SCXML_ADAPTER_CLOSED);
        check_equal(probe.sends, (size_t)1u);
        check_equal(composite->prepare_cancel(
                        scxml_chttp_binding_adapter_user(&binding),
                        &cancel_request, &ticket, &error),
                    SCXML_ADAPTER_CLOSED);
        check_equal(probe.cancels, (size_t)1u);
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &binding_config), TURBO_EALREADY);
        check_equal(scxml_chttp_processor_stop(&processor, 1000u), TURBO_EBUSY);
        check_equal(scxml_chttp_binding_destroy(&binding), TURBO_EBUSY);
        scxml_chttp_event_io_adapter()->close(
            scxml_chttp_binding_adapter_user(&binding));
        check_equal(probe.closes, (size_t)1u);
        check_equal(scxml_chttp_binding_destroy(&binding), TURBO_EBUSY);
        probe.quiescent = true;
        check_true(scxml_chttp_event_io_adapter()->is_quiescent(
            scxml_chttp_binding_adapter_user(&binding)));
        check_equal(scxml_chttp_binding_destroy(&binding), TURBO_OK);
        check_equal(scxml_chttp_processor_stop(&processor, 1000u), TURBO_OK);
        check_equal(scxml_chttp_processor_stop(&processor, 1000u),
                    TURBO_EALREADY);
        check_equal(scxml_chttp_processor_destroy(&processor), TURBO_OK);
        check_null(processor.impl);
    }

    it("rejects incomplete binding contracts without consuming endpoints") {
        scxml_chttp_processor_config_v1 processor_config =
            test_processor_config();
        scxml_chttp_processor processor = {0};
        scxml_chttp_binding binding = {0};
        downstream_probe probe = {.quiescent = true};
        scxml_chttp_binding_config_v1 config = test_binding_config(&probe);
        scxml_event_io_adapter incomplete = DOWNSTREAM_ADAPTER;

        check_equal(scxml_chttp_processor_init(
                        &processor, &processor_config), TURBO_OK);
        check_equal(scxml_chttp_processor_start(&processor), TURBO_OK);
        check_equal(scxml_chttp_binding_init(NULL, &processor, &config),
                    TURBO_EINVAL);
        check_equal(scxml_chttp_binding_init(&binding, &processor, NULL),
                    TURBO_EINVAL);
        config.abi_version = 0u;
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &config), TURBO_EINVAL);
        config = test_binding_config(&probe);
        config.struct_size = offsetof(
            scxml_chttp_binding_config_v1, decode_user);
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &config), TURBO_EINVAL);
        config = test_binding_config(&probe);
        config.scxml_adapter = NULL;
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &config), TURBO_EINVAL);
        config = test_binding_config(&probe);
        config.decode = NULL;
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &config), TURBO_EINVAL);
        incomplete.close = NULL;
        config = test_binding_config(&probe);
        config.scxml_adapter = &incomplete;
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &config), TURBO_EINVAL);
        incomplete = DOWNSTREAM_ADAPTER;
        incomplete.capabilities = SCXML_EVENT_IO_CAP_SEND;
        incomplete.prepare_cancel = NULL;
        config = test_binding_config(&probe);
        config.scxml_adapter = &incomplete;
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &config), TURBO_EINVAL);
        config = test_binding_config(&probe);
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &config), TURBO_OK);
        close_and_destroy_binding(&binding, &probe);
        check_equal(scxml_chttp_processor_stop(&processor, 1000u), TURBO_OK);
        check_equal(scxml_chttp_processor_destroy(&processor), TURBO_OK);
    }

    it("closes the downstream adapter exactly once on session init failure") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='waiting'/></scxml>";
        scxml_chttp_processor_config_v1 processor_config =
            test_processor_config();
        scxml_chttp_processor processor = {0};
        scxml_chttp_binding binding = {0};
        downstream_probe probe = {.quiescent = true};
        scxml_chttp_binding_config_v1 binding_config =
            test_binding_config(&probe);
        scxml_ioprocessor_descriptor descriptor = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_session session = {0};
        scxml_session_config session_config = {0};

        check_equal(scxml_compile(
                        &program, source, sizeof(source) - 1u,
                        NULL, &diagnostic), SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_chttp_processor_init(
                        &processor, &processor_config), TURBO_OK);
        check_equal(scxml_chttp_processor_start(&processor), TURBO_OK);
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &binding_config), TURBO_OK);
        check_true(scxml_chttp_binding_ioprocessor(&binding, &descriptor));
        session_config = (scxml_session_config){
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 0u,
            .internal_event_capacity = 1u,
            .completion_capacity = 1u,
            .microstep_limit = 8u,
            .max_storage_bytes = 4096u,
            .event_io = scxml_chttp_event_io_adapter(),
            .adapter_user = scxml_chttp_binding_adapter_user(&binding),
            .ioprocessors = &descriptor,
            .ioprocessor_count = 1u};
        check_not_equal(scxml_session_init(&session, &session_config),
                        CFLOW_STATECHART_INSTANCE_OK);
        check_null(session.impl);
        check_equal(probe.closes, (size_t)1u);
        scxml_chttp_event_io_adapter()->close(
            scxml_chttp_binding_adapter_user(&binding));
        check_equal(probe.closes, (size_t)1u);
        check_equal(scxml_chttp_binding_destroy(&binding), TURBO_OK);
        check_equal(scxml_chttp_processor_stop(&processor, 1000u), TURBO_OK);
        check_equal(scxml_chttp_processor_destroy(&processor), TURBO_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

    it("supports the complete session-owned binding protocol") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='waiting'><transition event='go' target='waiting'/>"
            "</state></scxml>";
        static const char basic_name[] = "basichttp";
        scxml_chttp_processor_config_v1 processor_config =
            test_processor_config();
        scxml_chttp_processor processor = {0};
        scxml_chttp_binding binding = {0};
        downstream_probe probe = {.quiescent = true};
        scxml_chttp_binding_config_v1 binding_config =
            test_binding_config(&probe);
        scxml_ioprocessor_descriptor descriptor = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_session session = {0};
        scxml_session_config session_config = {0};
        scxml_chttp_processor_stats stats = {0};

        check_equal(scxml_compile(
                        &program, source, sizeof(source) - 1u,
                        NULL, &diagnostic), SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_chttp_processor_init(
                        &processor, &processor_config), TURBO_OK);
        check_equal(scxml_chttp_processor_start(&processor), TURBO_OK);
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &binding_config), TURBO_OK);
        check_true(scxml_chttp_binding_ioprocessor(&binding, &descriptor));
        check_equal(descriptor.name_size, sizeof(basic_name) - 1u);
        check_equal(memcmp(
                        descriptor.name, basic_name,
                        sizeof(basic_name) - 1u), 0);
        check_equal(descriptor.type_size,
                    sizeof(SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI) - 1u);
        check_true(descriptor.location_size > 36u);
        check_equal(memcmp(descriptor.location, "http://", 7u), 0);
        session_config = (scxml_session_config){
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 1u,
            .microstep_limit = 8u,
            .max_storage_bytes = 4096u,
            .effect_capacity = 2u,
            .adapter_internal_event_capacity = 2u,
            .delayed_send_capacity = 2u,
            .event_io = scxml_chttp_event_io_adapter(),
            .adapter_user = scxml_chttp_binding_adapter_user(&binding),
            .ioprocessors = &descriptor,
            .ioprocessor_count = 1u};
        check_equal(scxml_session_init(&session, &session_config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_chttp_binding_activate(
                        &binding, &session, &program), TURBO_OK);
        check_equal(scxml_chttp_binding_activate(
                        &binding, &session, &program), TURBO_EALREADY);
        check_true(scxml_chttp_processor_get_stats(&processor, &stats));
        check_equal(stats.active_bindings, (size_t)1u);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(probe.closes, (size_t)1u);
        check_equal(scxml_chttp_binding_destroy(&binding), TURBO_OK);
        check_equal(scxml_chttp_processor_stop(&processor, 1000u), TURBO_OK);
        check_equal(scxml_chttp_processor_destroy(&processor), TURBO_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }
}
