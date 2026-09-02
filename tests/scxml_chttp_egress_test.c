#include <scxml/chttp_event_io.h>

#include "scxml_impl.h"

#include <cflow/executor.h>
#include <tinytest.h>
#include <turbo/clock.h>
#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <stdio.h>
#include <string.h>

typedef struct wire_probe {
    turbo_mutex_t mutex;
    size_t requests;
    unsigned int response_status;
    uint32_t delay_ms;
    chttp_method method;
    char target[128];
    char host[128];
    char content_type[128];
    char body[256];
    size_t body_size;
    char bodies[8][256];
} wire_probe;

typedef struct resolver_probe {
    char connection_uri[96];
    char authority[64];
    char target[64];
    size_t calls;
    char requested_uri[128];
    size_t requested_uri_size;
} resolver_probe;

typedef struct downstream_probe {
    size_t sends;
    size_t cancels;
    size_t commits;
    size_t discards;
    size_t closes;
} downstream_probe;

typedef struct egress_fixture {
    wire_probe wire;
    resolver_probe resolver;
    downstream_probe downstream;
    chttp_server peer;
    scxml_chttp_processor processor;
    scxml_chttp_binding binding;
    scxml_program program;
    cflow_executor executor;
    bool executor_initialized;
    scxml_session session;
    cflow_machine_state_id waiting_id;
    cflow_machine_state_id failed_id;
} egress_fixture;

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
        .connect_timeout_ms = 500u,
        .read_timeout_ms = 500u,
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
        .session_cookie_name = "test_sid", .poll_slice_ms = 1u};
}

static chttp_client_config test_client_config(size_t capacity) {
    return (chttp_client_config){
        .network = test_network(capacity),
        .request_capacity = capacity,
        .max_start_line_bytes = 256u,
        .max_header_count = 8u, .max_header_bytes = 512u,
        .max_request_body_bytes = 256u,
        .max_response_body_bytes = 64u,
        .max_informational_responses = 1u};
}

static int peer_handler(
    void *user, const chttp_server_request_view *request,
    chttp_server_response *response) {
    wire_probe *probe = (wire_probe *)user;
    const char *host = chttp_server_request_header(request, "Host");
    const char *content_type =
        chttp_server_request_header(request, "Content-Type");
    unsigned int response_status;
    uint32_t delay_ms;
    turbo_mutex_lock(&probe->mutex);
    ++probe->requests;
    probe->method = request->method;
    snprintf(probe->target, sizeof(probe->target), "%s", request->target);
    snprintf(probe->host, sizeof(probe->host), "%s", host != NULL ? host : "");
    snprintf(probe->content_type, sizeof(probe->content_type), "%s",
             content_type != NULL ? content_type : "");
    probe->body_size = request->body_size < sizeof(probe->body) - 1u
        ? request->body_size : sizeof(probe->body) - 1u;
    memcpy(probe->body, request->body, probe->body_size);
    probe->body[probe->body_size] = '\0';
    if (probe->requests <= 8u)
        memcpy(probe->bodies[probe->requests - 1u], probe->body,
               probe->body_size + 1u);
    response_status = probe->response_status;
    delay_ms = probe->delay_ms;
    turbo_mutex_unlock(&probe->mutex);
    if (delay_ms != 0u) turbo_sleep_ms(delay_ms);
    return chttp_server_reply(response, response_status, NULL, NULL, 0u);
}

static int resolve_target(
    void *user, const char *uri, size_t uri_size,
    scxml_chttp_resolved_target *out_target) {
    resolver_probe *probe = (resolver_probe *)user;
    if (probe == NULL || out_target == NULL ||
        uri == NULL || uri_size >= sizeof(probe->requested_uri))
        return 0;
    ++probe->calls;
    memcpy(probe->requested_uri, uri, uri_size);
    probe->requested_uri[uri_size] = '\0';
    probe->requested_uri_size = uri_size;
    *out_target = (scxml_chttp_resolved_target){
        probe->connection_uri, probe->authority, probe->target};
    return 1;
}

static void downstream_commit(void *user) {
    downstream_probe *probe = (downstream_probe *)user;
    ++probe->commits;
}

static void downstream_discard(void *user) {
    downstream_probe *probe = (downstream_probe *)user;
    ++probe->discards;
}

static scxml_adapter_status downstream_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    downstream_probe *probe = (downstream_probe *)user;
    (void)request;
    if (out_error != NULL) *out_error = NULL;
    ++probe->sends;
    *out_ticket = (cflow_statechart_effect_ticket){
        downstream_commit, downstream_discard, probe};
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status downstream_cancel(
    void *user, const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    downstream_probe *probe = (downstream_probe *)user;
    (void)request;
    if (out_error != NULL) *out_error = NULL;
    ++probe->cancels;
    *out_ticket = (cflow_statechart_effect_ticket){
        downstream_commit, downstream_discard, probe};
    return SCXML_ADAPTER_ACCEPTED;
}

static void downstream_close(void *user) {
    ++((downstream_probe *)user)->closes;
}

static bool downstream_quiescent(void *user) {
    return ((downstream_probe *)user)->closes != 0u;
}

static const scxml_event_io_adapter DOWNSTREAM = {
    .abi_version = SCXML_ADAPTER_ABI,
    .struct_size = sizeof(scxml_event_io_adapter),
    .capabilities = SCXML_EVENT_IO_CAP_SEND |
        SCXML_EVENT_IO_CAP_DELAYED_SEND | SCXML_EVENT_IO_CAP_CANCEL |
        SCXML_EVENT_IO_CAP_PAYLOAD | SCXML_EVENT_IO_CAP_CONTENT,
    .prepare_send = downstream_send,
    .prepare_cancel = downstream_cancel,
    .close = downstream_close,
    .is_quiescent = downstream_quiescent};

static scxml_chttp_decode_status decode_ingress(
    void *user, const scxml_chttp_ingress_view *ingress,
    scxml_content_view *out_data) {
    (void)user;
    (void)ingress;
    if (out_data == NULL) return SCXML_CHTTP_DECODE_FAILED;
    *out_data = (scxml_content_view){.kind = SCXML_CONTENT_INVALID};
    return SCXML_CHTTP_DECODE_OK;
}

static bool fixture_init(egress_fixture *fixture, size_t egress_capacity) {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
        "<state id='waiting'><transition event='error.communication' "
        "target='failed'/></state><final id='failed'/></scxml>";
    chttp_server_config peer_config = test_server_config();
    scxml_chttp_processor_config_v1 processor_config = {0};
    scxml_chttp_binding_config_v1 binding_config = {0};
    scxml_ioprocessor_descriptor descriptor = {0};
    scxml_session_config session_config = {0};
    scxml_diagnostic diagnostic = {0};
    uint16_t port = 0u;
    memset(fixture, 0, sizeof(*fixture));
    turbo_mutex_init(&fixture->wire.mutex);
    if (fixture->wire.mutex == NULL) return false;
    fixture->wire.response_status = 204u;
    if (chttp_server_init(&fixture->peer, &peer_config) != TURBO_OK ||
        chttp_server_post(
            &fixture->peer, "/sink", peer_handler, &fixture->wire) != TURBO_OK ||
        chttp_server_start(&fixture->peer) != TURBO_OK ||
        chttp_server_port(&fixture->peer, &port) != TURBO_OK)
        return false;
    snprintf(fixture->resolver.connection_uri,
             sizeof(fixture->resolver.connection_uri),
             "tcp://127.0.0.1:%u", (unsigned int)port);
    snprintf(fixture->resolver.authority,
             sizeof(fixture->resolver.authority),
             "127.0.0.1:%u", (unsigned int)port);
    snprintf(fixture->resolver.target,
             sizeof(fixture->resolver.target), "/sink");
    processor_config = (scxml_chttp_processor_config_v1){
        .abi_version = SCXML_CHTTP_ABI_V1,
        .struct_size = sizeof(processor_config),
        .server = test_server_config(),
        .client = test_client_config(egress_capacity),
        .advertised_authority = "127.0.0.1",
        .base_path = "/scxml",
        .endpoint_capacity = 2u,
        .egress_capacity = egress_capacity,
        .max_access_uri_bytes = 256u,
        .max_event_name_bytes = 64u,
        .max_form_entry_count = 8u,
        .max_form_name_bytes = 64u,
        .max_form_value_bytes = 128u,
        .max_encoded_body_bytes = 256u,
        .request_timeout_ms = 500u,
        .worker_poll_ms = 1u,
        .resolve = resolve_target,
        .resolve_user = &fixture->resolver};
    if (scxml_chttp_processor_init(
            &fixture->processor, &processor_config) != TURBO_OK ||
        scxml_chttp_processor_start(&fixture->processor) != TURBO_OK)
        return false;
    binding_config = (scxml_chttp_binding_config_v1){
        .abi_version = SCXML_CHTTP_ABI_V1,
        .struct_size = sizeof(binding_config),
        .scxml_adapter = &DOWNSTREAM,
        .scxml_adapter_user = &fixture->downstream,
        .decode = decode_ingress};
    if (scxml_chttp_binding_init(
            &fixture->binding, &fixture->processor,
            &binding_config) != TURBO_OK ||
        !scxml_chttp_binding_ioprocessor(&fixture->binding, &descriptor) ||
        scxml_compile(&fixture->program, source, sizeof(source) - 1u,
                      NULL, &diagnostic) != SCXML_OK ||
        !scxml_program_state_id(
            &fixture->program, "waiting", 7u, &fixture->waiting_id) ||
        !scxml_program_state_id(
            &fixture->program, "failed", 6u, &fixture->failed_id) ||
        !cflow_executor_serial_init(&fixture->executor))
        return false;
    fixture->executor_initialized = true;
    session_config = (scxml_session_config){
        .program = &fixture->program,
        .executor = &fixture->executor,
        .external_event_capacity = 2u,
        .internal_event_capacity = 2u,
        .completion_capacity = 1u,
        .microstep_limit = 8u,
        .max_storage_bytes = 4096u,
        .effect_capacity = 2u,
        .adapter_internal_event_capacity = 2u,
        .delayed_send_capacity = 2u,
        .event_io = scxml_chttp_event_io_adapter(),
        .adapter_user = scxml_chttp_binding_adapter_user(&fixture->binding),
        .ioprocessors = &descriptor,
        .ioprocessor_count = 1u};
    return scxml_session_init(&fixture->session, &session_config) ==
               CFLOW_STATECHART_INSTANCE_OK &&
           scxml_chttp_binding_activate(
               &fixture->binding, &fixture->session,
               &fixture->program) == TURBO_OK;
}

static void fixture_destroy(egress_fixture *fixture) {
    if (fixture->session.impl != NULL)
        (void)scxml_session_destroy(&fixture->session);
    if (fixture->binding.impl != NULL)
        (void)scxml_chttp_binding_destroy(&fixture->binding);
    if (fixture->processor.impl != NULL) {
        (void)scxml_chttp_processor_stop(&fixture->processor, 1000u);
        (void)scxml_chttp_processor_destroy(&fixture->processor);
    }
    if (fixture->peer.impl != NULL) {
        (void)chttp_server_stop(&fixture->peer, 1000u);
        (void)chttp_server_destroy(&fixture->peer);
    }
    if (fixture->executor_initialized)
        cflow_executor_destroy(&fixture->executor);
    if (fixture->program.impl != NULL)
        scxml_program_destroy(&fixture->program);
    if (fixture->wire.mutex != NULL)
        turbo_mutex_destroy(&fixture->wire.mutex);
}

static size_t wire_requests(egress_fixture *fixture) {
    size_t requests;
    turbo_mutex_lock(&fixture->wire.mutex);
    requests = fixture->wire.requests;
    turbo_mutex_unlock(&fixture->wire.mutex);
    return requests;
}

static bool wait_for_terminal(
    egress_fixture *fixture, uint64_t terminals, uint32_t timeout_ms) {
    const uint64_t deadline = turbo_monotonic_ms() + timeout_ms;
    do {
        scxml_chttp_processor_stats stats = {0};
        if (scxml_chttp_processor_get_stats(&fixture->processor, &stats) &&
            stats.egress_completed + stats.egress_failed +
                stats.egress_cancelled >= terminals)
            return true;
        turbo_sleep_ms(1u);
    } while (turbo_monotonic_ms() < deadline);
    return false;
}

static cflow_machine_state_id current_state(egress_fixture *fixture) {
    scxml_session_impl *impl = (scxml_session_impl *)fixture->session.impl;
    return cflow_statechart_instance_current_state(&impl->instance);
}

static scxml_send_request basic_request(uint64_t delay_ms, const char *id) {
    static const scxml_payload_entry entries[] = {
        {.name = "customer", .name_size = 8u,
         .value = {.kind = SCXML_CONTENT_SCALAR,
                   .scalar = {.kind = SCXML_PAYLOAD_VALUE_STRING,
                              .data.string = {"A&B", 3u}}}},
        {.name = "qty", .name_size = 3u,
         .value = {.kind = SCXML_CONTENT_SCALAR,
                   .scalar = {.kind = SCXML_PAYLOAD_VALUE_UINT,
                              .data.uint = 2u}}}};
    return (scxml_send_request){
        .event = "order ready", .event_size = 11u,
        .target = "http://allowed/event", .target_size = 20u,
        .type = SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI,
        .type_size = sizeof(SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI) - 1u,
        .id = id, .id_size = id != NULL ? strlen(id) : 0u,
        .delay_ms = delay_ms,
        .payload = {.kind = SCXML_PAYLOAD_NAMED,
                    .entries = entries, .entry_count = 2u}};
}

spec("TurboSCXML CHTTP transactional egress") {
    it("reserves before commit and discard frees capacity without wire I/O") {
        egress_fixture fixture;
        const scxml_event_io_adapter *adapter =
            scxml_chttp_event_io_adapter();
        scxml_send_request request = basic_request(0u, "one");
        cflow_statechart_effect_ticket first = {0};
        cflow_statechart_effect_ticket second = {0};
        const char *error = NULL;

        check_true(fixture_init(&fixture, 1u));
        check_equal(adapter->prepare_send(
                        scxml_chttp_binding_adapter_user(&fixture.binding),
                        &request, &first, &error), SCXML_ADAPTER_ACCEPTED);
        turbo_sleep_ms(20u);
        check_equal(wire_requests(&fixture), (size_t)0u);
        check_equal(adapter->prepare_send(
                        scxml_chttp_binding_adapter_user(&fixture.binding),
                        &request, &second, &error), SCXML_ADAPTER_FULL);
        first.discard(first.user);
        check_equal(adapter->prepare_send(
                        scxml_chttp_binding_adapter_user(&fixture.binding),
                        &request, &second, &error), SCXML_ADAPTER_ACCEPTED);
        second.discard(second.user);
        check_equal(wire_requests(&fixture), (size_t)0u);
        fixture_destroy(&fixture);
    }

    it("commits one literal POST and releases capacity after HTTP 204") {
        egress_fixture fixture;
        const scxml_event_io_adapter *adapter =
            scxml_chttp_event_io_adapter();
        scxml_send_request request = basic_request(0u, "one");
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;
        scxml_chttp_processor_stats stats = {0};
        char original_connection[sizeof(fixture.resolver.connection_uri)];
        char original_authority[sizeof(fixture.resolver.authority)];
        char original_target[sizeof(fixture.resolver.target)];

        check_true(fixture_init(&fixture, 1u));
        memcpy(original_connection, fixture.resolver.connection_uri,
               sizeof(original_connection));
        memcpy(original_authority, fixture.resolver.authority,
               sizeof(original_authority));
        memcpy(original_target, fixture.resolver.target,
               sizeof(original_target));
        check_equal(adapter->prepare_send(
                        scxml_chttp_binding_adapter_user(&fixture.binding),
                        &request, &ticket, &error), SCXML_ADAPTER_ACCEPTED);
        snprintf(fixture.resolver.connection_uri,
                 sizeof(fixture.resolver.connection_uri),
                 "tcp://127.0.0.1:1");
        snprintf(fixture.resolver.authority,
                 sizeof(fixture.resolver.authority), "mutated.invalid");
        snprintf(fixture.resolver.target,
                 sizeof(fixture.resolver.target), "/mutated");
        ticket.commit(ticket.user);
        check_true(wait_for_terminal(&fixture, 1u, 1000u));
        check_equal(wire_requests(&fixture), (size_t)1u);
        turbo_mutex_lock(&fixture.wire.mutex);
        check_equal(fixture.wire.method, CHTTP_METHOD_POST);
        check_equal(strcmp(fixture.wire.target, "/sink"), 0);
        check_equal(strcmp(
                        fixture.wire.host,
                        original_authority), 0);
        check_equal(strcmp(
                        fixture.wire.content_type,
                        "application/x-www-form-urlencoded"), 0);
        check_equal(strcmp(
                        fixture.wire.body,
                        "_scxmleventname=order+ready&customer=A%26B&qty=2"), 0);
        turbo_mutex_unlock(&fixture.wire.mutex);
        check_true(scxml_chttp_processor_get_stats(
            &fixture.processor, &stats));
        check_equal(stats.egress_completed, UINT64_C(1));
        check_equal(stats.egress_failed, UINT64_C(0));
        check_equal(stats.queued_egress, (size_t)0u);
        check_equal(stats.in_flight_egress, (size_t)0u);
        check_equal(current_state(&fixture), fixture.waiting_id);
        memcpy(fixture.resolver.connection_uri, original_connection,
               sizeof(original_connection));
        memcpy(fixture.resolver.authority, original_authority,
               sizeof(original_authority));
        memcpy(fixture.resolver.target, original_target,
               sizeof(original_target));
        check_equal(adapter->prepare_send(
                        scxml_chttp_binding_adapter_user(&fixture.binding),
                        &request, &ticket, &error), SCXML_ADAPTER_ACCEPTED);
        ticket.discard(ticket.user);
        fixture_destroy(&fixture);
    }

    it("preserves commit order per binding without blocking another binding") {
        egress_fixture fixture;
        downstream_probe second_downstream = {0};
        scxml_chttp_binding second_binding = {0};
        scxml_chttp_binding_config_v1 binding_config = {
            .abi_version = SCXML_CHTTP_ABI_V1,
            .struct_size = sizeof(binding_config),
            .scxml_adapter = &DOWNSTREAM,
            .scxml_adapter_user = &second_downstream,
            .decode = decode_ingress};
        const scxml_event_io_adapter *adapter =
            scxml_chttp_event_io_adapter();
        scxml_send_request first = basic_request(80u, "first");
        scxml_send_request second = basic_request(0u, "second");
        scxml_send_request other = basic_request(0u, "other");
        cflow_statechart_effect_ticket first_ticket = {0};
        cflow_statechart_effect_ticket second_ticket = {0};
        cflow_statechart_effect_ticket other_ticket = {0};
        const char *error = NULL;

        first.event = "first";
        first.event_size = 5u;
        second.event = "second";
        second.event_size = 6u;
        other.event = "other";
        other.event_size = 5u;
        check_true(fixture_init(&fixture, 4u));
        check_equal(scxml_chttp_binding_init(
                        &second_binding, &fixture.processor,
                        &binding_config), TURBO_OK);
        check_equal(scxml_chttp_binding_activate(
                        &second_binding, &fixture.session,
                        &fixture.program), TURBO_OK);
        check_equal(adapter->prepare_send(
                        scxml_chttp_binding_adapter_user(&fixture.binding),
                        &first, &first_ticket, &error), SCXML_ADAPTER_ACCEPTED);
        check_equal(adapter->prepare_send(
                        scxml_chttp_binding_adapter_user(&fixture.binding),
                        &second, &second_ticket, &error), SCXML_ADAPTER_ACCEPTED);
        check_equal(adapter->prepare_send(
                        scxml_chttp_binding_adapter_user(&second_binding),
                        &other, &other_ticket, &error), SCXML_ADAPTER_ACCEPTED);
        first_ticket.commit(first_ticket.user);
        second_ticket.commit(second_ticket.user);
        other_ticket.commit(other_ticket.user);
        check_true(wait_for_terminal(&fixture, 3u, 1500u));
        turbo_mutex_lock(&fixture.wire.mutex);
        check_equal(strncmp(
                        fixture.wire.bodies[0],
                        "_scxmleventname=other&", 22u), 0);
        check_equal(strncmp(
                        fixture.wire.bodies[1],
                        "_scxmleventname=first&", 22u), 0);
        check_equal(strncmp(
                        fixture.wire.bodies[2],
                        "_scxmleventname=second&", 23u), 0);
        turbo_mutex_unlock(&fixture.wire.mutex);
        adapter->close(scxml_chttp_binding_adapter_user(&second_binding));
        check_true(adapter->is_quiescent(
            scxml_chttp_binding_adapter_user(&second_binding)));
        check_equal(scxml_chttp_binding_destroy(&second_binding), TURBO_OK);
        fixture_destroy(&fixture);
    }

    it("honors delay and committed cancellation before submission") {
        egress_fixture fixture;
        const scxml_event_io_adapter *adapter =
            scxml_chttp_event_io_adapter();
        scxml_send_request request = basic_request(100u, "later");
        scxml_cancel_request cancel = {.send_id = "later", .send_id_size = 5u};
        cflow_statechart_effect_ticket send_ticket = {0};
        cflow_statechart_effect_ticket cancel_ticket = {0};
        const char *error = NULL;
        scxml_chttp_processor_stats stats = {0};

        check_true(fixture_init(&fixture, 1u));
        check_equal(adapter->prepare_send(
                        scxml_chttp_binding_adapter_user(&fixture.binding),
                        &request, &send_ticket, &error), SCXML_ADAPTER_ACCEPTED);
        send_ticket.commit(send_ticket.user);
        turbo_sleep_ms(20u);
        check_equal(wire_requests(&fixture), (size_t)0u);
        check_equal(adapter->prepare_cancel(
                        scxml_chttp_binding_adapter_user(&fixture.binding),
                        &cancel, &cancel_ticket, &error),
                    SCXML_ADAPTER_ACCEPTED);
        cancel_ticket.commit(cancel_ticket.user);
        check_true(wait_for_terminal(&fixture, 1u, 200u));
        turbo_sleep_ms(100u);
        check_equal(wire_requests(&fixture), (size_t)0u);
        check_true(scxml_chttp_processor_get_stats(
            &fixture.processor, &stats));
        check_equal(stats.egress_cancelled, UINT64_C(1));
        fixture_destroy(&fixture);
    }

    it("cancel racing with an in-flight request has one terminal release") {
        egress_fixture fixture;
        const scxml_event_io_adapter *adapter =
            scxml_chttp_event_io_adapter();
        scxml_send_request request = basic_request(0u, "racing");
        scxml_cancel_request cancel = {
            .send_id = "racing", .send_id_size = 6u};
        cflow_statechart_effect_ticket send_ticket = {0};
        cflow_statechart_effect_ticket cancel_ticket = {0};
        const char *error = NULL;
        scxml_chttp_processor_stats stats = {0};
        const uint64_t deadline = turbo_monotonic_ms() + 1000u;

        check_true(fixture_init(&fixture, 1u));
        turbo_mutex_lock(&fixture.wire.mutex);
        fixture.wire.delay_ms = 100u;
        turbo_mutex_unlock(&fixture.wire.mutex);
        check_equal(adapter->prepare_send(
                        scxml_chttp_binding_adapter_user(&fixture.binding),
                        &request, &send_ticket, &error), SCXML_ADAPTER_ACCEPTED);
        send_ticket.commit(send_ticket.user);
        while (wire_requests(&fixture) == 0u &&
               turbo_monotonic_ms() < deadline)
            turbo_sleep_ms(1u);
        check_equal(wire_requests(&fixture), (size_t)1u);
        check_equal(adapter->prepare_cancel(
                        scxml_chttp_binding_adapter_user(&fixture.binding),
                        &cancel, &cancel_ticket, &error),
                    SCXML_ADAPTER_ACCEPTED);
        cancel_ticket.commit(cancel_ticket.user);
        check_true(wait_for_terminal(&fixture, 1u, 1000u));
        check_true(scxml_chttp_processor_get_stats(
            &fixture.processor, &stats));
        check_equal(stats.egress_completed + stats.egress_failed +
                        stats.egress_cancelled,
                    UINT64_C(1));
        check_equal(stats.egress_failed, UINT64_C(0));
        check_equal(stats.queued_egress, (size_t)0u);
        check_equal(stats.in_flight_egress, (size_t)0u);
        fixture_destroy(&fixture);
    }

    it("rejects denied resolver outputs and reports transport failure") {
        egress_fixture fixture;
        const scxml_event_io_adapter *adapter =
            scxml_chttp_event_io_adapter();
        scxml_send_request request = basic_request(0u, "transport");
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;
        scxml_chttp_processor_stats stats = {0};

        check_true(fixture_init(&fixture, 1u));
        snprintf(fixture.resolver.connection_uri,
                 sizeof(fixture.resolver.connection_uri), "https://denied");
        check_equal(adapter->prepare_send(
                        scxml_chttp_binding_adapter_user(&fixture.binding),
                        &request, &ticket, &error),
                    SCXML_ADAPTER_ERROR_COMMUNICATION);
        check_not_null(error);
        snprintf(fixture.resolver.connection_uri,
                 sizeof(fixture.resolver.connection_uri),
                 "tcp://127.0.0.1:1");
        check_equal(adapter->prepare_send(
                        scxml_chttp_binding_adapter_user(&fixture.binding),
                        &request, &ticket, &error), SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_true(wait_for_terminal(&fixture, 1u, 1000u));
        check_true(scxml_chttp_processor_get_stats(
            &fixture.processor, &stats));
        check_equal(stats.egress_failed, UINT64_C(1));
        check_equal(current_state(&fixture), fixture.failed_id);
        fixture_destroy(&fixture);
    }

    it("reports HTTP failure while preserving exact downstream tickets") {
        egress_fixture fixture;
        const scxml_event_io_adapter *adapter =
            scxml_chttp_event_io_adapter();
        scxml_send_request basic = basic_request(0u, "bad");
        scxml_send_request delegated = basic;
        scxml_cancel_request missing = {
            .send_id = "downstream", .send_id_size = 10u};
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;

        check_true(fixture_init(&fixture, 1u));
        turbo_mutex_lock(&fixture.wire.mutex);
        fixture.wire.response_status = 404u;
        turbo_mutex_unlock(&fixture.wire.mutex);
        check_equal(adapter->prepare_send(
                        scxml_chttp_binding_adapter_user(&fixture.binding),
                        &basic, &ticket, &error), SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_true(wait_for_terminal(&fixture, 1u, 1000u));
        check_equal(current_state(&fixture), fixture.failed_id);
        delegated.type = NULL;
        delegated.type_size = 0u;
        check_equal(adapter->prepare_send(
                        scxml_chttp_binding_adapter_user(&fixture.binding),
                        &delegated, &ticket, &error), SCXML_ADAPTER_ACCEPTED);
        check_equal(fixture.downstream.sends, (size_t)1u);
        ticket.discard(ticket.user);
        check_equal(fixture.downstream.discards, (size_t)1u);
        check_equal(adapter->prepare_cancel(
                        scxml_chttp_binding_adapter_user(&fixture.binding),
                        &missing, &ticket, &error), SCXML_ADAPTER_ACCEPTED);
        check_equal(fixture.downstream.cancels, (size_t)1u);
        ticket.commit(ticket.user);
        check_equal(fixture.downstream.commits, (size_t)1u);
        fixture_destroy(&fixture);
    }
}
