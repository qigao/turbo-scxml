#include <scxml/cnet_frame_ingress.h>

#include <cflow/executor.h>
#include <salts/clock.h>
#include <tinytest.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { FRAMED_TEST_TIMEOUT_MS = 5000 };

typedef struct framed_sender {
    bool connected;
    bool terminal;
} framed_sender;

static native_io_backend_kind test_backend(void) {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
    return NATIVE_IO_BACKEND_EPOLL;
#else
    return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

static cnet_client_config test_client_config(void) {
    cnet_client_config config = {0};
    config.backend = test_backend();
    config.connection_capacity = 2u;
    config.command_capacity = 8u;
    config.request_capacity = 8u;
    config.completion_batch_capacity = 8u;
    config.event_capacity = 8u;
    config.max_send_bytes = 128u;
    config.receive_buffer_bytes = 128u;
    return config;
}

static void sender_state(void *user, cnet_connection connection,
                         cnet_connection_state state, const cnet_error *error) {
    framed_sender *sender = (framed_sender *)user;
    (void)connection;
    (void)error;
    if (state == CNET_CONNECTION_CONNECTED) sender->connected = true;
    if (state == CNET_CONNECTION_CLOSED ||
        state == CNET_CONNECTION_FAILED) sender->terminal = true;
}

static int send_bytes(cnet_client *client, cnet_connection connection,
                      const unsigned char *bytes, size_t count) {
    mem_buffer_t *buffer;
    int status;
    if (client == NULL || bytes == NULL || count == 0u) return SALTS_EINVAL;
    buffer = mem_get_buffer(mem_global(), count);
    if (buffer == NULL) return SALTS_ENOMEM;
    memcpy(mem_buffer_data(buffer), bytes, count);
    mem_set_used(buffer, count);
    status = cnet_send_buffer(client, connection, buffer);
    mem_buffer_release(buffer);
    return status;
}

spec("Bounded CNet frame reassembly into generation-safe Host Mailbox") {
    it("reassembles split/coalesced frames, holds FULL and rejects oversize") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' initial='run'>"
            "<state id='run'><transition event='wire.frame'/></state></scxml>";
        static const unsigned char first_header[] = {0x00u};
        static const unsigned char second_chunk[] = {
            0x02u, 'A', 'B', 0x00u, 0x02u, 'C', 'D'
        };
        static const unsigned char oversize_header[] = {0x00u, 0x09u};
        const cnet_client_config net_config = test_client_config();
        const cnet_listener_config listen_config = {
            .backend = test_backend(),
            .host = "127.0.0.1", .port = 0u, .backlog = 2u
        };
        cnet_client sender = {0};
        cnet_client receiver = {0};
        cnet_listener listener = {0};
        cnet_connection outbound = {0};
        cnet_connection inbound = {0};
        framed_sender sender_probe = {0};
        cnet_connect_options options = {0};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_session session = {0};
        scxml_host_router router = {0};
        scxml_host_session_ref ref = {0};
        scxml_host_router_stats router_stats = {0};
        scxml_cnet_frame_ingress ingress = {0};
        scxml_cnet_frame_ingress_stats stats = {0};
        cnet_observer ingress_observer;
        cflow_statechart_instance_stats machine = {0};
        uint16_t port = 0u;
        uint64_t deadline;
        char uri[64];
        size_t events = 0u, count = 0u, delivered = 0u;
        int ready = 0;

        check_equal(scxml_compile(&program, source, sizeof(source) - 1u,
                                  NULL, &diagnostic), SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        {
            scxml_session_config config = {
                .program = &program,
                .executor = &executor,
                .external_event_capacity = 2u,
                .internal_event_capacity = 2u,
                .completion_capacity = 2u,
                .microstep_limit = 16u
            };
            check_equal(scxml_session_init(&session, &config),
                        CFLOW_STATECHART_INSTANCE_OK);
        }
        check_equal(scxml_host_router_init(
            &router, &(scxml_host_router_config){
                .endpoint_capacity = 1u,
                .event_capacity = 1u,
                .max_text_bytes = 16u
            }), SALTS_OK);
        check_equal(scxml_host_router_attach(&router, &session, &ref), SALTS_OK);
        check_equal(cnet_client_init(&sender, &net_config), SALTS_OK);
        check_equal(cnet_client_init(&receiver, &net_config), SALTS_OK);
        check_equal(cnet_listener_init(&listener, &listen_config), SALTS_OK);
        check_equal(cnet_listener_port(&listener, &port), SALTS_OK);
        check_true(port != 0u);
        check_equal(scxml_cnet_frame_ingress_init(
            &ingress, &(scxml_cnet_frame_ingress_config){
                .client = &receiver, .router = &router, .target = ref,
                .event_name = "wire.frame",
                .event_name_size = sizeof("wire.frame") - 1u,
                .max_frame_bytes = 8u, .max_receive_bytes = 64u
            }), SALTS_OK);

        check_true(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u",
                            (unsigned int)port) > 0);
        options.uri = uri;
        options.observer = (cnet_observer){
            .on_state = sender_state, .user = &sender_probe
        };
        check_equal(cnet_connect(&sender, &options, &outbound), SALTS_OK);
        deadline = cmeta_monotonic_ms() + FRAMED_TEST_TIMEOUT_MS;
        while (!sender_probe.connected && cmeta_monotonic_ms() < deadline)
            check_equal(cnet_client_poll(&sender, 1u, &events), SALTS_OK);
        check_true(sender_probe.connected);

        check_equal(cnet_listener_wait(
            &listener, FRAMED_TEST_TIMEOUT_MS, &ready), SALTS_OK);
        check_equal(ready, 1);
        ingress_observer = scxml_cnet_frame_ingress_observer(&ingress);
        check_equal(cnet_listener_accept(
            &listener, &receiver, &ingress_observer, &inbound), SALTS_OK);
        check_equal(scxml_cnet_frame_ingress_bind(&ingress, inbound), SALTS_OK);

        deadline = cmeta_monotonic_ms() + FRAMED_TEST_TIMEOUT_MS;
        do {
            check_equal(cnet_client_poll(&receiver, 1u, &events), SALTS_OK);
            check_true(scxml_cnet_frame_ingress_get_stats(&ingress, &stats));
        } while (!stats.connected && cmeta_monotonic_ms() < deadline);
        check_true(stats.connected);
        {
            cnet_connection stale = inbound;
            const cnet_receive_view forged = {"X", 1u, CNET_MESSAGE_BYTES};
            ++stale.generation;
            ingress_observer.on_receive(ingress_observer.user, stale, &forged);
            check_true(scxml_cnet_frame_ingress_get_stats(&ingress, &stats));
            check_equal(stats.stale_callbacks, UINT64_C(1));
            check_equal(stats.received_chunks, UINT64_C(0));
        }

        /* Split the two-byte length header across separate real CNet reads. */
        check_equal(scxml_cnet_frame_ingress_arm(&ingress), SALTS_OK);
        check_equal(send_bytes(&sender, outbound, first_header,
                               sizeof(first_header)), SALTS_OK);
        deadline = cmeta_monotonic_ms() + FRAMED_TEST_TIMEOUT_MS;
        while (stats.received_chunks < UINT64_C(1) &&
               cmeta_monotonic_ms() < deadline) {
            check_equal(cnet_client_poll(&sender, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&receiver, 1u, &events), SALTS_OK);
            check_true(scxml_cnet_frame_ingress_get_stats(&ingress, &stats));
        }
        check_equal(stats.received_chunks, UINT64_C(1));
        check_equal(scxml_cnet_frame_ingress_process(&ingress, 4u, &count),
                    SALTS_OK);
        check_equal(count, (size_t)0u);
        check_equal(scxml_cnet_frame_ingress_arm(&ingress), SALTS_OK);

        /* Coalesced remainder of frame A and full frame B. The Host has one
           slot: a complete B remains retained until the Host drains A. */
        check_equal(send_bytes(&sender, outbound, second_chunk,
                               sizeof(second_chunk)), SALTS_OK);
        deadline = cmeta_monotonic_ms() + FRAMED_TEST_TIMEOUT_MS;
        while (stats.host_full == 0u &&
               cmeta_monotonic_ms() < deadline) {
            int process_status;
            check_equal(cnet_client_poll(&sender, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&receiver, 1u, &events), SALTS_OK);
            process_status = scxml_cnet_frame_ingress_process(
                &ingress, 4u, &count);
            check_true(process_status == SALTS_OK ||
                       process_status == SALTS_ENOBUFS);
            check_true(scxml_cnet_frame_ingress_get_stats(&ingress, &stats));
            if (process_status == SALTS_OK && !stats.armed &&
                stats.buffered_chunk_bytes == 0u && stats.host_full == 0u)
                check_equal(scxml_cnet_frame_ingress_arm(&ingress), SALTS_OK);
        }
        check_equal(stats.accepted_frames, UINT64_C(1));
        check_true(stats.host_full != 0u);
        check_equal(scxml_cnet_frame_ingress_arm(&ingress), SALTS_ENOBUFS);
        check_true(scxml_host_router_get_stats(&router, &router_stats));
        check_equal(router_stats.pending, (size_t)1u);
        check_equal(scxml_host_router_drain(&router, 1u, &delivered),
                    CFLOW_MAILBOX_OK);
        check_equal(delivered, (size_t)1u);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(scxml_cnet_frame_ingress_process(&ingress, 4u, &count),
                    SALTS_OK);
        check_equal(count, (size_t)1u);
        check_equal(scxml_host_router_drain(&router, 1u, &delivered),
                    CFLOW_MAILBOX_OK);
        check_equal(delivered, (size_t)1u);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &machine));
        check_false(machine.errored);
        check_true(scxml_host_router_get_stats(&router, &router_stats));
        check_equal(router_stats.delivered, UINT64_C(2));
        check_equal(router_stats.pending, (size_t)0u);
        check_true(scxml_cnet_frame_ingress_get_stats(&ingress, &stats));
        check_equal(stats.accepted_frames, UINT64_C(2));

        /* An invalid (over-limit) frame is not truncated into an Event. */
        check_equal(scxml_cnet_frame_ingress_arm(&ingress), SALTS_OK);
        check_equal(send_bytes(&sender, outbound, oversize_header,
                               sizeof(oversize_header)), SALTS_OK);
        deadline = cmeta_monotonic_ms() + FRAMED_TEST_TIMEOUT_MS;
        while (stats.received_chunks < UINT64_C(3) &&
               cmeta_monotonic_ms() < deadline) {
            check_equal(cnet_client_poll(&sender, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&receiver, 1u, &events), SALTS_OK);
            check_true(scxml_cnet_frame_ingress_get_stats(&ingress, &stats));
        }
        check_true(stats.received_chunks >= UINT64_C(3));
        check_equal(scxml_cnet_frame_ingress_process(&ingress, 2u, &count),
                    SALTS_EMSGSIZE);
        check_true(scxml_cnet_frame_ingress_get_stats(&ingress, &stats));
        check_true(stats.failed);
        check_equal(stats.rejected_frames, UINT64_C(1));
        check_equal(stats.accepted_frames, UINT64_C(2));

        check_equal(scxml_cnet_frame_ingress_close(&ingress), SALTS_OK);
        check_equal(scxml_cnet_frame_ingress_destroy(&ingress), SALTS_EBUSY);
        check_equal(cnet_close(&receiver, inbound), SALTS_OK);
        check_equal(cnet_close(&sender, outbound), SALTS_OK);
        deadline = cmeta_monotonic_ms() + FRAMED_TEST_TIMEOUT_MS;
        while (!scxml_cnet_frame_ingress_is_quiescent(&ingress) &&
               cmeta_monotonic_ms() < deadline) {
            check_equal(cnet_client_poll(&receiver, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&sender, 1u, &events), SALTS_OK);
        }
        check_true(scxml_cnet_frame_ingress_is_quiescent(&ingress));
        check_equal(scxml_cnet_frame_ingress_destroy(&ingress), SALTS_OK);
        check_equal(scxml_host_router_detach(&router, ref), SALTS_OK);
        check_equal(scxml_host_router_close(&router), SALTS_OK);
        check_equal(scxml_host_router_destroy(&router), SALTS_OK);
        check_equal(cnet_client_stop(&receiver, 1000u), SALTS_OK);
        check_equal(cnet_client_destroy(&receiver), SALTS_OK);
        check_equal(cnet_client_stop(&sender, 1000u), SALTS_OK);
        check_equal(cnet_client_destroy(&sender), SALTS_OK);
        check_equal(cnet_listener_close(&listener), SALTS_OK);
        check_equal(cnet_listener_destroy(&listener), SALTS_OK);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }
}
