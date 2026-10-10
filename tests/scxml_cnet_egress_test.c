#include <scxml/cnet_egress.h>

#include <cflow/executor.h>
#include <salts/clock.h>
#include <tinytest.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { EGRESS_TEST_TIMEOUT_MS = 5000 };

typedef struct receiver_probe {
    bool connected;
    bool terminal;
    bool received;
    bool failed;
    size_t size;
    unsigned char data[64];
} receiver_probe;

static native_io_backend_kind backend(void) {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
    return NATIVE_IO_BACKEND_EPOLL;
#else
    return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

static cnet_client_config client_config(void) {
    cnet_client_config config = {0};
    config.backend = backend();
    config.connection_capacity = 2u;
    config.command_capacity = 8u;
    config.request_capacity = 8u;
    config.completion_batch_capacity = 8u;
    config.event_capacity = 8u;
    config.max_send_bytes = 64u;
    config.receive_buffer_bytes = 64u;
    return config;
}

static void receiver_state(void *user, cnet_connection connection,
                           cnet_connection_state state,
                           const cnet_error *error) {
    receiver_probe *probe = (receiver_probe *)user;
    (void)connection;
    if (state == CNET_CONNECTION_CONNECTED) probe->connected = true;
    if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED) {
        probe->terminal = true;
        if (state == CNET_CONNECTION_FAILED || error != NULL)
            probe->failed = true;
    }
}

static void receiver_data(void *user, cnet_connection connection,
                          const cnet_receive_view *view) {
    receiver_probe *probe = (receiver_probe *)user;
    (void)connection;
    if (view == NULL || view->kind != CNET_MESSAGE_BYTES ||
        view->size == 0u || view->data == NULL || view->size > sizeof(probe->data)) {
        probe->failed = true;
        return;
    }
    memcpy(probe->data, view->data, view->size);
    probe->size = view->size;
    probe->received = true;
}

static scxml_send_request no_payload_request(const char *event) {
    scxml_send_request request = {0};
    request.event = event;
    request.event_size = strlen(event);
    request.target = SCXML_CNET_BOUND_TARGET;
    request.target_size = sizeof(SCXML_CNET_BOUND_TARGET) - 1u;
    request.type = SCXML_CNET_RAW_PROCESSOR_URI;
    request.type_size = sizeof(SCXML_CNET_RAW_PROCESSOR_URI) - 1u;
    request.payload.kind = SCXML_PAYLOAD_NONE;
    return request;
}

spec("CNet transactional outbound raw-byte profile") {
    it("reserves exact bytes, rejects unsupported semantics, and discards without I/O") {
        cnet_client client = {0};
        scxml_cnet_egress egress = {0};
        scxml_cnet_egress_config config = {0};
        scxml_cnet_egress_stats stats = {0};
        scxml_send_request request;
        cflow_statechart_effect_ticket first = {0};
        cflow_statechart_effect_ticket second = {0};
        const scxml_event_io_adapter *adapter;
        const char *error = NULL;
        size_t submitted = 99u;

        config.client = &client;
        config.capacity = 1u;
        config.max_payload_bytes = 16u;
        check_equal(scxml_cnet_egress_init(&egress, &config), SALTS_OK);
        adapter = scxml_cnet_egress_adapter();
        check_not_null(adapter);
        check_equal(adapter->capabilities,
                    SCXML_EVENT_IO_CAP_SEND | SCXML_EVENT_IO_CAP_CONTENT);
        check_null(adapter->prepare_cancel);
        request = no_payload_request("ping");

        check_equal(adapter->prepare_send(
            scxml_cnet_egress_adapter_user(&egress),
            &request, &first, &error), SCXML_ADAPTER_ACCEPTED);
        check_not_null(first.commit);
        check_not_null(first.discard);
        check_equal(adapter->prepare_send(
            scxml_cnet_egress_adapter_user(&egress),
            &request, &second, &error), SCXML_ADAPTER_FULL);
        check_null(second.commit);
        check_true(scxml_cnet_egress_get_stats(&egress, &stats));
        check_equal(stats.pending, (size_t)1u);
        check_equal(stats.rejected_full, UINT64_C(1));
        check_equal(scxml_cnet_egress_pump(&egress, 1u, &submitted),
                    SALTS_ENOTCONN);
        check_equal(submitted, (size_t)0u);
        first.discard(first.user);
        check_true(scxml_cnet_egress_get_stats(&egress, &stats));
        check_equal(stats.pending, (size_t)0u);
        check_equal(stats.discarded, UINT64_C(1));

        request.type = NULL;
        request.type_size = 0u;
        check_equal(adapter->prepare_send(
            scxml_cnet_egress_adapter_user(&egress),
            &request, &first, &error), SCXML_ADAPTER_ERROR_EXECUTION);
        check_not_null(error);
        request = no_payload_request("ping");
        request.target = "cnet://wrong";
        request.target_size = sizeof("cnet://wrong") - 1u;
        check_equal(adapter->prepare_send(
            scxml_cnet_egress_adapter_user(&egress),
            &request, &first, &error), SCXML_ADAPTER_ERROR_COMMUNICATION);
        request = no_payload_request("ping");
        request.delay_ms = 42u;
        check_equal(adapter->prepare_send(
            scxml_cnet_egress_adapter_user(&egress),
            &request, &first, &error), SCXML_ADAPTER_ERROR_EXECUTION);

        request = no_payload_request("ping");
        check_equal(adapter->prepare_send(
            scxml_cnet_egress_adapter_user(&egress),
            &request, &first, &error), SCXML_ADAPTER_ACCEPTED);
        check_equal(scxml_cnet_egress_destroy(&egress), SALTS_EBUSY);
        check_equal(scxml_cnet_egress_close(&egress), SALTS_OK);
        check_equal(scxml_cnet_egress_destroy(&egress), SALTS_EBUSY);
        first.commit(first.user);
        check_true(scxml_cnet_egress_is_quiescent(&egress));
        check_equal(scxml_cnet_egress_destroy(&egress), SALTS_OK);
        check_null(egress.impl);
    }

    it("commits a real SCXML microstep before CNet owner sends and settles TCP") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' initial='active'>"
            "<state id='active'><transition event='go'>"
            "<send event='PING' type='urn:turboscxml:cnet-raw:1' target='cnet://bound'/>"
            "</transition></state></scxml>";
        cnet_client sender = {0};
        cnet_client receiver = {0};
        cnet_listener listener = {0};
        cnet_connection outgoing = {0};
        cnet_connection incoming = {0};
        scxml_cnet_egress egress = {0};
        scxml_cnet_egress_stats stats = {0};
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        cflow_statechart_instance_stats machine = {0};
        scxml_session_config session_config = {0};
        cnet_client_config net_config = client_config();
        cnet_listener_config listen_config = {
            .backend = backend(), .host = "127.0.0.1",
            .port = 0u, .backlog = 2u
        };
        scxml_event_metadata metadata = {
            .abi_version = SCXML_EVENT_METADATA_ABI,
            .struct_size = sizeof(scxml_event_metadata)
        };
        receiver_probe probe = {0};
        cnet_observer receiver_observer = {
            .on_state = receiver_state,
            .on_receive = receiver_data,
            .user = &probe
        };
        cnet_connect_options options = {0};
        uint16_t port = 0u;
        uint64_t deadline = 0u;
        char uri[64];
        size_t events = 0u, submitted = 0u;
        int ready = 0;

        check_equal(scxml_compile(&program, source,
            sizeof(source) - 1u, NULL, &diagnostic), SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        check_equal(cnet_client_init(&sender, &net_config), SALTS_OK);
        check_equal(cnet_client_init(&receiver, &net_config), SALTS_OK);
        check_equal(cnet_listener_init(&listener, &listen_config), SALTS_OK);
        check_equal(cnet_listener_port(&listener, &port), SALTS_OK);
        check_true(port != 0u);

        check_equal(scxml_cnet_egress_init(
            &egress, &(scxml_cnet_egress_config){
                .client = &sender, .capacity = 2u,
                .max_payload_bytes = 16u
            }), SALTS_OK);
        session_config = (scxml_session_config){
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .effect_capacity = 2u,
            .adapter_internal_event_capacity = 2u,
            .microstep_limit = 16u,
            .event_io = scxml_cnet_egress_adapter(),
            .adapter_user = scxml_cnet_egress_adapter_user(&egress)
        };
        check_equal(scxml_session_init(&session, &session_config),
                    CFLOW_STATECHART_INSTANCE_OK);

        check_true(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u",
                            (unsigned int)port) > 0);
        options.uri = uri;
        options.observer = scxml_cnet_egress_observer(&egress);
        check_equal(cnet_connect(&sender, &options, &outgoing), SALTS_OK);
        check_equal(scxml_cnet_egress_bind(&egress, outgoing), SALTS_OK);
        deadline = cmeta_monotonic_ms() + EGRESS_TEST_TIMEOUT_MS;
        while (cmeta_monotonic_ms() < deadline) {
            check_equal(cnet_client_poll(&sender, 1u, &events), SALTS_OK);
            check_true(scxml_cnet_egress_get_stats(&egress, &stats));
            if (stats.connected) break;
        }
        check_true(stats.connected);
        check_equal(cnet_listener_wait(
            &listener, EGRESS_TEST_TIMEOUT_MS, &ready), SALTS_OK);
        check_equal(ready, 1);
        check_equal(cnet_listener_accept(
            &listener, &receiver, &receiver_observer, &incoming), SALTS_OK);
        deadline = cmeta_monotonic_ms() + EGRESS_TEST_TIMEOUT_MS;
        while (cmeta_monotonic_ms() < deadline) {
            check_equal(cnet_client_poll(&receiver, 1u, &events), SALTS_OK);
            if (probe.connected) break;
        }
        check_true(probe.connected);
        check_equal(cnet_receive(&receiver, incoming, 1u), SALTS_OK);

        check_equal(scxml_session_try_send_named_with_metadata(
            &session, "go", sizeof("go") - 1u, &metadata), CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &machine));
        check_false(machine.errored);
        check_true(scxml_cnet_egress_get_stats(&egress, &stats));
        check_equal(stats.committed, UINT64_C(1));
        check_equal(stats.submitted, UINT64_C(0));
        check_equal(stats.pending, (size_t)1u);

        check_equal(scxml_cnet_egress_pump(&egress, 1u, &submitted),
                    SALTS_OK);
        check_equal(submitted, (size_t)1u);
        deadline = cmeta_monotonic_ms() + EGRESS_TEST_TIMEOUT_MS;
        while (cmeta_monotonic_ms() < deadline) {
            check_equal(cnet_client_poll(&sender, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&receiver, 1u, &events), SALTS_OK);
            check_true(scxml_cnet_egress_get_stats(&egress, &stats));
            if (probe.received && stats.completed == UINT64_C(1)) break;
        }
        check_true(probe.received);
        check_equal(probe.size, (size_t)4u);
        check_true(memcmp(probe.data, "PING", 4u) == 0);
        check_true(scxml_cnet_egress_get_stats(&egress, &stats));
        check_equal(stats.completed, UINT64_C(1));
        check_equal(stats.pending, (size_t)0u);
        check_false(stats.failed);

        /* The domain host drains its real CNet owners before the Session
           attempts close/quiescence. Requesting exit is not I/O terminal. */
        check_equal(cnet_close(&sender, outgoing), SALTS_OK);
        check_equal(cnet_close(&receiver, incoming), SALTS_OK);
        deadline = cmeta_monotonic_ms() + EGRESS_TEST_TIMEOUT_MS;
        while (cmeta_monotonic_ms() < deadline) {
            check_equal(cnet_client_poll(&sender, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&receiver, 1u, &events), SALTS_OK);
            check_true(scxml_cnet_egress_get_stats(&egress, &stats));
            /* Terminal belongs to CNet; full quiescence additionally
               requires the SCXML Session to close its adapter below. */
            if (stats.terminal) break;
        }
        check_true(stats.terminal);
        check_equal(stats.pending, (size_t)0u);
        scxml_session_cancel(&session);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_cnet_egress_is_quiescent(&egress));
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_cnet_egress_destroy(&egress), SALTS_OK);
        check_equal(cnet_client_stop(&receiver, 1000u), SALTS_OK);
        /* CNet stop is the definitive terminal for the receiving owner. */
        check_true(probe.terminal);
        check_equal(cnet_client_destroy(&receiver), SALTS_OK);
        check_equal(cnet_client_stop(&sender, 1000u), SALTS_OK);
        check_equal(cnet_client_destroy(&sender), SALTS_OK);
        check_equal(cnet_listener_close(&listener), SALTS_OK);
        check_equal(cnet_listener_destroy(&listener), SALTS_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }
}
