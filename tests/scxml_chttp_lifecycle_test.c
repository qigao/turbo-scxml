#include <scxml/chttp_event_io.h>

#include "chttp_event_io_internal.h"

#include <cflow/executor.h>
#include <tinytest.h>
#include <turbo/error_codes.h>
#include <turbo/platform.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct downstream_probe {
    size_t prepare_send_calls;
    size_t prepare_cancel_calls;
    size_t close_calls;
    bool closed;
    bool quiescent;
} downstream_probe;

static native_io_backend_kind test_backend(void) {
#ifdef _WIN32
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
    return NATIVE_IO_BACKEND_EPOLL;
#else
    return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

static cnet_client_config test_network(size_t connection_capacity) {
    return (cnet_client_config){
        .backend = test_backend(),
        .connection_capacity = connection_capacity,
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

static int allow_resolve(
    void *user, const char *uri, size_t uri_size,
    scxml_chttp_resolved_target *out_target) {
    (void)user;
    (void)uri;
    (void)uri_size;
    if (out_target == NULL) return TURBO_EINVAL;
    *out_target = (scxml_chttp_resolved_target){
        "tcp://127.0.0.1:9", "127.0.0.1", "/event"};
    return TURBO_OK;
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
        .max_form_value_bytes = 64u,
        .max_encoded_body_bytes = 256u,
        .request_timeout_ms = 1000u,
        .worker_poll_ms = 2u,
        .resolve = allow_resolve};
}

static scxml_adapter_status downstream_prepare_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    downstream_probe *probe = (downstream_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->prepare_send_calls;
    *out_error = "downstream witness";
    memset(out_ticket, 0, sizeof(*out_ticket));
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static scxml_adapter_status downstream_prepare_cancel(
    void *user, const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    downstream_probe *probe = (downstream_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->prepare_cancel_calls;
    *out_error = "cancel witness";
    return SCXML_ADAPTER_ERROR_COMMUNICATION;
}

static void downstream_close(void *user) {
    downstream_probe *probe = (downstream_probe *)user;
    if (probe == NULL) return;
    ++probe->close_calls;
    probe->closed = true;
}

static bool downstream_is_quiescent(void *user) {
    const downstream_probe *probe = (const downstream_probe *)user;
    return probe != NULL && probe->closed && probe->quiescent;
}

static const scxml_event_io_adapter DOWNSTREAM_ADAPTER = {
    .abi_version = SCXML_ADAPTER_ABI,
    .struct_size = sizeof(scxml_event_io_adapter),
    .capabilities = SCXML_EVENT_IO_CAP_SEND |
                    SCXML_EVENT_IO_CAP_DELAYED_SEND |
                    SCXML_EVENT_IO_CAP_CANCEL |
                    SCXML_EVENT_IO_CAP_PAYLOAD |
                    SCXML_EVENT_IO_CAP_CONTENT,
    .prepare_send = downstream_prepare_send,
    .prepare_cancel = downstream_prepare_cancel,
    .close = downstream_close,
    .is_quiescent = downstream_is_quiescent};

static const scxml_event_io_adapter BASE_DOWNSTREAM_ADAPTER = {
    .abi_version = SCXML_ADAPTER_ABI,
    .struct_size = sizeof(scxml_event_io_adapter),
    .capabilities = SCXML_EVENT_IO_CAP_SEND |
                    SCXML_EVENT_IO_CAP_PAYLOAD |
                    SCXML_EVENT_IO_CAP_CONTENT,
    .prepare_send = downstream_prepare_send,
    .close = downstream_close,
    .is_quiescent = downstream_is_quiescent};

static scxml_chttp_binding_config_v1 test_binding_config(
    downstream_probe *probe) {
    return (scxml_chttp_binding_config_v1){
        .abi_version = SCXML_CHTTP_ABI_V1,
        .struct_size = sizeof(scxml_chttp_binding_config_v1),
        .scxml_adapter = &DOWNSTREAM_ADAPTER,
        .scxml_adapter_user = probe};
}

spec("TurboSCXML CHTTP processor and binding lifecycle") {
    it("rejects partial zero and overflowing processor configurations") {
        scxml_chttp_processor processor = {0};
        scxml_chttp_processor_config_v1 config = test_processor_config();

        check_equal(scxml_chttp_processor_init(NULL, &config), TURBO_EINVAL);
        check_equal(scxml_chttp_processor_init(&processor, NULL), TURBO_EINVAL);
        config.abi_version = 0u;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        check_null(processor.impl);
        config = test_processor_config();
        config.struct_size -= 1u;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        config = test_processor_config();
        config.endpoint_capacity = 0u;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        config = test_processor_config();
        config.egress_capacity = 0u;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        config = test_processor_config();
        config.max_access_uri_bytes = 0u;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        config = test_processor_config();
        config.max_form_entry_count = 0u;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        config = test_processor_config();
        config.max_event_name_bytes = 0u;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        config = test_processor_config();
        config.max_form_name_bytes = 0u;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        config = test_processor_config();
        config.max_form_value_bytes = 0u;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        config = test_processor_config();
        config.max_encoded_body_bytes = 0u;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        config = test_processor_config();
        config.request_timeout_ms = 0u;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        config = test_processor_config();
        config.worker_poll_ms = 0u;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        config = test_processor_config();
        config.advertised_authority = "";
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        config = test_processor_config();
        config.resolve = NULL;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        config = test_processor_config();
        config.base_path = "scxml";
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        config = test_processor_config();
        config.base_path = "/scxml?bad";
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        config = test_processor_config();
        config.client.network.command_capacity = 3u;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        config = test_processor_config();
        config.server.network.command_capacity = 3u;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_EINVAL);
        config = test_processor_config();
        config.endpoint_capacity = SIZE_MAX;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_ERANGE);
        config = test_processor_config();
        config.egress_capacity = SIZE_MAX;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_ERANGE);
        check_null(processor.impl);
    }

    it("enforces processor state and fixed endpoint capacity") {
        scxml_chttp_processor_config_v1 config = test_processor_config();
        scxml_chttp_processor processor = {0};
        scxml_chttp_binding first = {0};
        scxml_chttp_binding second = {0};
        downstream_probe first_probe = {.quiescent = true};
        downstream_probe second_probe = {.quiescent = true};
        scxml_chttp_binding_config_v1 first_config =
            test_binding_config(&first_probe);
        scxml_chttp_binding_config_v1 second_config =
            test_binding_config(&second_probe);

        config.endpoint_capacity = 1u;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_OK);
        check_equal(scxml_chttp_processor_init(&processor, &config),
                    TURBO_EALREADY);
        check_equal(scxml_chttp_processor_destroy(&processor), TURBO_EBUSY);
        check_equal(scxml_chttp_binding_init(
                        &first, &processor, &first_config),
                    TURBO_EBUSY);
        check_equal(scxml_chttp_processor_start(&processor), TURBO_OK);
        check_equal(scxml_chttp_processor_start(&processor), TURBO_EALREADY);
        check_equal(scxml_chttp_binding_init(
                        &first, &processor, &first_config),
                    TURBO_OK);
        check_equal(scxml_chttp_binding_init(
                        &first, &processor, &first_config),
                    TURBO_EALREADY);
        check_equal(scxml_chttp_binding_init(
                        &second, &processor, &second_config),
                    TURBO_ENOBUFS);
        check_equal(scxml_chttp_processor_stop(&processor, 1000u), TURBO_EBUSY);
        scxml_chttp_binding_event_io_adapter(&first)->close(
            scxml_chttp_binding_adapter_user(&first));
        check_equal(scxml_chttp_binding_destroy(&first), TURBO_OK);
        check_equal(first_probe.close_calls, (size_t)1u);
        check_equal(scxml_chttp_processor_stop(&processor, 1000u), TURBO_OK);
        check_equal(scxml_chttp_processor_destroy(&processor), TURBO_OK);
        check_null(processor.impl);
    }

    it("validates downstream adapters and delegates non-BasicHTTP requests") {
        scxml_chttp_processor_config_v1 config = test_processor_config();
        scxml_chttp_processor processor = {0};
        scxml_chttp_binding binding = {0};
        downstream_probe probe = {0};
        scxml_chttp_binding_config_v1 binding_config =
            test_binding_config(&probe);
        scxml_event_io_adapter incomplete = DOWNSTREAM_ADAPTER;
        scxml_send_request request = {0};
        cflow_statechart_effect_ticket ticket = {0};
        const scxml_event_io_adapter *adapter;
        const char *error = NULL;

        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_OK);
        check_equal(scxml_chttp_processor_start(&processor), TURBO_OK);
        binding_config.abi_version = 0u;
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &binding_config),
                    TURBO_EINVAL);
        binding_config = test_binding_config(&probe);
        binding_config.struct_size -= 1u;
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &binding_config),
                    TURBO_EINVAL);
        binding_config = test_binding_config(&probe);
        binding_config.scxml_adapter = NULL;
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &binding_config),
                    TURBO_EINVAL);
        incomplete.close = NULL;
        binding_config.scxml_adapter = &incomplete;
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &binding_config),
                    TURBO_EINVAL);
        incomplete = DOWNSTREAM_ADAPTER;
        incomplete.capabilities &= ~SCXML_EVENT_IO_CAP_DELAYED_SEND;
        binding_config.scxml_adapter = &incomplete;
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &binding_config),
                    TURBO_EINVAL);
        incomplete = BASE_DOWNSTREAM_ADAPTER;
        incomplete.capabilities |= SCXML_EVENT_IO_CAP_DELAYED_SEND;
        binding_config.scxml_adapter = &incomplete;
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &binding_config),
                    TURBO_EINVAL);
        incomplete = DOWNSTREAM_ADAPTER;
        incomplete.capabilities |= UINT64_C(1) << 63u;
        binding_config.scxml_adapter = &incomplete;
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &binding_config),
                    TURBO_EINVAL);
        binding_config = test_binding_config(&probe);
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &binding_config),
                    TURBO_OK);
        adapter = scxml_chttp_binding_event_io_adapter(&binding);
        check_not_null(adapter);
        check_equal(adapter->capabilities, DOWNSTREAM_ADAPTER.capabilities);

        request.type = "vendor:custom";
        request.type_size = sizeof("vendor:custom") - 1u;
        check_equal(adapter->prepare_send(
                        scxml_chttp_binding_adapter_user(&binding),
                        &request, &ticket, &error),
                    SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(probe.prepare_send_calls, (size_t)1u);
        check_equal(error, "downstream witness");

        {
            const scxml_cancel_request cancel = {"id", 2u};
            error = NULL;
            check_equal(adapter->prepare_cancel(
                            scxml_chttp_binding_adapter_user(&binding),
                            &cancel, &ticket, &error),
                        SCXML_ADAPTER_ERROR_COMMUNICATION);
            check_equal(probe.prepare_cancel_calls, (size_t)1u);
            check_equal(error, "cancel witness");
        }

        request.type = SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI;
        request.type_size = sizeof(SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI) - 1u;
        error = NULL;
        check_equal(adapter->prepare_send(
                        scxml_chttp_binding_adapter_user(&binding),
                        &request, &ticket, &error),
                    SCXML_ADAPTER_CLOSED);
        check_equal(probe.prepare_send_calls, (size_t)1u);

        adapter->close(scxml_chttp_binding_adapter_user(&binding));
        adapter->close(scxml_chttp_binding_adapter_user(&binding));
        check_equal(probe.close_calls, (size_t)1u);
        check_false(adapter->is_quiescent(
            scxml_chttp_binding_adapter_user(&binding)));
        check_equal(scxml_chttp_binding_destroy(&binding), TURBO_EBUSY);
        probe.quiescent = true;
        check_true(adapter->is_quiescent(
            scxml_chttp_binding_adapter_user(&binding)));
        check_equal(scxml_chttp_binding_destroy(&binding), TURBO_OK);
        check_equal(scxml_chttp_processor_stop(&processor, 1000u), TURBO_OK);
        check_equal(scxml_chttp_processor_destroy(&processor), TURBO_OK);
    }

    it("derives optional delayed-send capabilities from the downstream") {
        scxml_chttp_processor_config_v1 config = test_processor_config();
        scxml_chttp_processor processor = {0};
        scxml_chttp_binding binding = {0};
        downstream_probe probe = {.quiescent = true};
        scxml_chttp_binding_config_v1 binding_config =
            test_binding_config(&probe);
        const scxml_event_io_adapter *adapter;

        binding_config.scxml_adapter = &BASE_DOWNSTREAM_ADAPTER;
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_OK);
        check_equal(scxml_chttp_processor_start(&processor), TURBO_OK);
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &binding_config), TURBO_OK);
        adapter = scxml_chttp_binding_event_io_adapter(&binding);
        check_not_null(adapter);
        check_equal(adapter->capabilities,
                    BASE_DOWNSTREAM_ADAPTER.capabilities);
        adapter->close(scxml_chttp_binding_adapter_user(&binding));
        check_equal(scxml_chttp_binding_destroy(&binding), TURBO_OK);
        check_equal(scxml_chttp_processor_stop(&processor, 1000u), TURBO_OK);
        check_equal(scxml_chttp_processor_destroy(&processor), TURBO_OK);
    }

    it("recovers from worker creation failure and supports stop retry") {
        scxml_chttp_processor_config_v1 config = test_processor_config();
        scxml_chttp_processor processor = {0};
        uint64_t started_ms;

        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_OK);
        scxml_chttp_test_fail_next_worker_create();
        check_equal(scxml_chttp_processor_start(&processor), TURBO_ENOMEM);
        check_equal(scxml_chttp_processor_destroy(&processor), TURBO_OK);

        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_OK);
        check_equal(scxml_chttp_processor_start(&processor), TURBO_OK);
        scxml_chttp_test_delay_next_worker_exit(250u);
        started_ms = turbo_monotonic_ms();
        check_equal(scxml_chttp_processor_stop(&processor, 1u),
                    TURBO_ETIMEDOUT);
        check(turbo_monotonic_ms() - started_ms < 200u);
        check_equal(scxml_chttp_processor_stop(&processor, 1000u), TURBO_OK);
        check_equal(scxml_chttp_processor_destroy(&processor), TURBO_OK);
    }

    it("finishes cleanup after a terminal server error") {
        scxml_chttp_processor_config_v1 config = test_processor_config();
        scxml_chttp_processor processor = {0};

        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_OK);
        check_equal(scxml_chttp_processor_start(&processor), TURBO_OK);
        scxml_chttp_test_force_next_server_terminal_error();
        scxml_chttp_test_delay_next_worker_exit(500u);
        check_equal(scxml_chttp_processor_stop(&processor, 250u),
                    TURBO_ETIMEDOUT);
        check_equal(scxml_chttp_processor_stop(&processor, 1000u), TURBO_EIO);
        check_equal(scxml_chttp_processor_destroy(&processor), TURBO_OK);
    }

    it("closes the downstream adapter once when session initialization fails") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='done'><final id='done'/></scxml>";
        scxml_chttp_processor_config_v1 config = test_processor_config();
        scxml_chttp_processor processor = {0};
        scxml_chttp_binding binding = {0};
        downstream_probe probe = {.quiescent = true};
        scxml_chttp_binding_config_v1 binding_config =
            test_binding_config(&probe);
        scxml_ioprocessor_descriptor descriptor = {0};
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session_config session_config = {0};

        check_equal(scxml_compile(
                        &program, source, sizeof(source) - 1u,
                        NULL, &diagnostic), SCXML_OK);
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_OK);
        check_equal(scxml_chttp_processor_start(&processor), TURBO_OK);
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &binding_config), TURBO_OK);
        check_true(scxml_chttp_binding_ioprocessor(&binding, &descriptor));
        session_config = (scxml_session_config){
            .program = &program,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .event_io = scxml_chttp_binding_event_io_adapter(&binding),
            .adapter_user = scxml_chttp_binding_adapter_user(&binding),
            .ioprocessors = &descriptor,
            .ioprocessor_count = 1u};
        check_true(scxml_session_init(&session, &session_config) !=
                   CFLOW_STATECHART_INSTANCE_OK);
        check_null(session.impl);
        check_equal(probe.close_calls, (size_t)1u);
        check_equal(scxml_chttp_binding_destroy(&binding), TURBO_OK);
        check_equal(probe.close_calls, (size_t)1u);
        check_equal(scxml_chttp_processor_stop(&processor, 1000u), TURBO_OK);
        check_equal(scxml_chttp_processor_destroy(&processor), TURBO_OK);
        scxml_program_destroy(&program);
    }

    it("binds a real session through the complete ownership protocol") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='done'><final id='done'/></scxml>";
        scxml_chttp_processor_config_v1 config = test_processor_config();
        scxml_chttp_processor processor = {0};
        scxml_chttp_binding binding = {0};
        scxml_chttp_binding unrelated_binding = {0};
        downstream_probe probe = {.quiescent = true};
        downstream_probe unrelated_probe = {.quiescent = true};
        scxml_chttp_binding_config_v1 binding_config =
            test_binding_config(&probe);
        scxml_chttp_binding_config_v1 unrelated_binding_config =
            test_binding_config(&unrelated_probe);
        scxml_ioprocessor_descriptor descriptor = {0};
        scxml_program program = {0};
        scxml_program unrelated_program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_session_config session_config = {0};
        scxml_chttp_processor_stats stats = {0};

        check_equal(scxml_compile(
                        &program, source, sizeof(source) - 1u,
                        NULL, &diagnostic), SCXML_OK);
        check_equal(scxml_compile(
                        &unrelated_program, source, sizeof(source) - 1u,
                        NULL, &diagnostic), SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(scxml_chttp_processor_init(&processor, &config), TURBO_OK);
        check_equal(scxml_chttp_processor_start(&processor), TURBO_OK);
        check_equal(scxml_chttp_binding_init(
                        &binding, &processor, &binding_config), TURBO_OK);
        check_equal(scxml_chttp_binding_init(
                        &unrelated_binding, &processor,
                        &unrelated_binding_config), TURBO_OK);
        check_true(scxml_chttp_processor_get_stats(&processor, &stats));
        check_true(stats.running);
        check_equal(stats.live_bindings, (size_t)2u);
        check_true(scxml_chttp_binding_ioprocessor(&binding, &descriptor));
        check_equal(descriptor.name, "basichttp");
        check_equal(descriptor.type, SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI);
        check_not_null(strstr(descriptor.location, "http://127.0.0.1:"));
        check_not_null(strstr(descriptor.location, "/scxml/"));

        session_config = (scxml_session_config){
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .event_io = scxml_chttp_binding_event_io_adapter(&binding),
            .adapter_user = scxml_chttp_binding_adapter_user(&binding),
            .ioprocessors = &descriptor,
            .ioprocessor_count = 1u};
        check_equal(scxml_session_init(&session, &session_config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_chttp_binding_activate(
                        &binding, &session, &unrelated_program), TURBO_EINVAL);
        check_equal(scxml_chttp_binding_activate(
                        &unrelated_binding, &session, &program), TURBO_EINVAL);
        check_equal(scxml_chttp_binding_activate(
                        &binding, &session, &program), TURBO_OK);
        check_equal(scxml_chttp_binding_activate(
                        &binding, &session, &program), TURBO_EALREADY);
        check_equal(scxml_chttp_binding_destroy(&binding), TURBO_EBUSY);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(probe.close_calls, (size_t)1u);
        check_equal(scxml_chttp_binding_destroy(&binding), TURBO_OK);
        scxml_chttp_binding_event_io_adapter(&unrelated_binding)->close(
            scxml_chttp_binding_adapter_user(&unrelated_binding));
        check_equal(scxml_chttp_binding_destroy(&unrelated_binding), TURBO_OK);
        check_equal(scxml_chttp_processor_stop(&processor, 1000u), TURBO_OK);
        check_equal(scxml_chttp_processor_destroy(&processor), TURBO_OK);

        cflow_executor_destroy(&executor);
        scxml_program_destroy(&unrelated_program);
        scxml_program_destroy(&program);
    }
}
