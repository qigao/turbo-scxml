#include <scxml/cnet_ingress.h>

#include <cflow/executor.h>
#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/thread.h>
#include <tinytest.h>

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { CNET_SCXML_TIMEOUT_MS = 5000 };

static native_io_backend_kind native_test_backend(void) {
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
    config.backend = native_test_backend();
    config.connection_capacity = 2u;
    config.command_capacity = 8u;
    config.request_capacity = 8u;
    config.completion_batch_capacity = 8u;
    config.event_capacity = 8u;
    config.max_send_bytes = 64u;
    config.receive_buffer_bytes = 64u;
    return config;
}

typedef struct sender_probe {
    bool connected;
    bool terminal;
} sender_probe;

static void sender_state(void *user, cnet_connection connection,
                         cnet_connection_state state, const cnet_error *error) {
    sender_probe *probe = (sender_probe *)user;
    (void)connection;
    (void)error;
    if (state == CNET_CONNECTION_CONNECTED) probe->connected = true;
    if (state == CNET_CONNECTION_CLOSED ||
        state == CNET_CONNECTION_FAILED) probe->terminal = true;
}

/* Test uses the existing CFlow SerialExecutor as its single Session
 * execution owner. The kernel CNet callback continues on the separate
 * explicit CNet poll-owner lane; there is no second network progress loop. */
typedef struct ingress_fifo_gate {
    atomic_bool entered;
    atomic_bool release;
} ingress_fifo_gate;

static void block_session_fifo_executor(void *user) {
    ingress_fifo_gate *gate = (ingress_fifo_gate *)user;
    atomic_store_explicit(&gate->entered, true, memory_order_release);
    while (!atomic_load_explicit(&gate->release, memory_order_acquire))
        cmeta_thread_yield();
}

spec("Owner-driven CNet to SCXML Event ingress") {
    it("rejects missing Session and invalid bounded configuration") {
        scxml_cnet_ingress ingress = {0};
        scxml_cnet_ingress_config config = {0};
        check_equal(scxml_cnet_ingress_init(&ingress, &config), SALTS_EINVAL);
        check_null(ingress.impl);
        check_equal(scxml_cnet_ingress_destroy(&ingress), SALTS_OK);
    }

    it("retains a real TCP chunk across Host capacity and Session FIFO FULL without loss") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' initial='active'>"
            "<state id='active'><transition event='wire.chunk' target='done'/></state>"
            "<final id='done'/></scxml>";
        const cnet_client_config net_config = client_config();
        const cnet_listener_config listener_config = {
            .backend = native_test_backend(),
            .host = "127.0.0.1", .port = 0u, .backlog = 2u};
        scxml_program program = {0};
        scxml_session session = {0};
        scxml_diagnostic diagnostic = {0};
        const scxml_event_metadata ignore_metadata = {
            .abi_version = SCXML_EVENT_METADATA_ABI,
            .struct_size = sizeof(scxml_event_metadata)
        };
        ingress_fifo_gate gate = {0};
        cflow_executor executor = {0};
        cnet_client sender = {0};
        cnet_client receiver = {0};
        cnet_listener listener = {0};
        cnet_connection outgoing = {0};
        cnet_connection incoming = {0};
        scxml_cnet_ingress ingress = {0};
        scxml_cnet_ingress_stats stats = {0};
        cflow_statechart_instance_stats state = {0};
        sender_probe probe = {0};
        scxml_session_config session_config = {0};
        scxml_cnet_ingress_config ingress_config = {0};
        cnet_connect_options options = {0};
        cnet_observer incoming_observer;
        mem_buffer_t *buffer;
        uint64_t deadline;
        uint16_t port = 0u;
        size_t delivered = 0u;
        size_t events = 0u;
        char uri[64];
        int ready = 0;

        atomic_init(&gate.entered, false);
        atomic_init(&gate.release, false);
        check_equal(scxml_compile(
            &program, source, sizeof(source) - 1u, NULL, &diagnostic),
            SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        session_config = (scxml_session_config){
            .program = &program, .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u
        };
        check_equal(scxml_session_init(&session, &session_config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));

        check_equal(cnet_client_init(&sender, &net_config), SALTS_OK);
        check_equal(cnet_client_init(&receiver, &net_config), SALTS_OK);
        check_equal(cnet_listener_init(&listener, &listener_config), SALTS_OK);
        check_equal(cnet_listener_port(&listener, &port), SALTS_OK);
        check_true(port != 0u);

        ingress_config = (scxml_cnet_ingress_config){
            .client = &receiver,
            .session = &session,
            .event_name = "wire.chunk",
            .event_name_size = sizeof("wire.chunk") - 1u,
            .capacity = 1u,
            .max_chunk_bytes = 8u
        };
        check_equal(scxml_cnet_ingress_init(&ingress, &ingress_config),
                    SALTS_OK);
        incoming_observer = scxml_cnet_ingress_observer(&ingress);
        check_not_null(incoming_observer.on_state);
        check_not_null(incoming_observer.on_receive);

        check_true(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u",
                            (unsigned int)port) > 0);
        options.uri = uri;
        options.observer = (cnet_observer){
            .on_state = sender_state, .user = &probe
        };
        check_equal(cnet_connect(&sender, &options, &outgoing), SALTS_OK);
        deadline = cmeta_monotonic_ms() + CNET_SCXML_TIMEOUT_MS;
        while (!probe.connected && cmeta_monotonic_ms() < deadline) {
            check_equal(cnet_client_poll(&sender, 1u, &events), SALTS_OK);
        }
        check_true(probe.connected);

        check_equal(cnet_listener_wait(&listener, CNET_SCXML_TIMEOUT_MS, &ready),
                    SALTS_OK);
        check_equal(ready, 1);
        check_equal(cnet_listener_accept(
            &listener, &receiver, &incoming_observer, &incoming), SALTS_OK);
        check_equal(scxml_cnet_ingress_bind(&ingress, incoming), SALTS_OK);
        deadline = cmeta_monotonic_ms() + CNET_SCXML_TIMEOUT_MS;
        do {
            check_equal(cnet_client_poll(&receiver, 1u, &events), SALTS_OK);
            check_true(scxml_cnet_ingress_get_stats(&ingress, &stats));
        } while (!stats.connected && cmeta_monotonic_ms() < deadline);
        check_true(stats.connected);

        check_equal(scxml_cnet_ingress_arm(&ingress), SALTS_OK);

        /* Stale CNet slot/generation callbacks cannot borrow this Session. */
        {
            cnet_connection stale = incoming;
            const cnet_receive_view forged = {
                "z", 1u, CNET_MESSAGE_BYTES
            };
            ++stale.generation;
            incoming_observer.on_receive(incoming_observer.user, stale, &forged);
            check_true(scxml_cnet_ingress_get_stats(&ingress, &stats));
            check_equal(stats.stale_callbacks, UINT64_C(1));
            check_true(stats.receive_armed);
            check_equal(stats.pending, (size_t)0u);
        }

        /* Fill the ACTUAL Session external FIFO while the existing CFlow
           SerialExecutor is suspended by a test-only atomic gate. Network
           polling remains on its one CNet owner; the Host ingress staging
           row is independent of these two Session FIFO slots. */
        check_equal(cflow_executor_try_post(
            &executor, block_session_fifo_executor, &gate),
            CFLOW_ADMISSION_ACCEPTED);
        deadline = cmeta_monotonic_ms() + CNET_SCXML_TIMEOUT_MS;
        while (!atomic_load_explicit(&gate.entered, memory_order_acquire) &&
               cmeta_monotonic_ms() < deadline)
            cmeta_thread_yield();
        check_true(atomic_load_explicit(
            &gate.entered, memory_order_acquire));
        check_equal(scxml_session_try_send_named_with_metadata(
            &session, "ignore", 6u, &ignore_metadata), CFLOW_MAILBOX_OK);
        check_equal(scxml_session_try_send_named_with_metadata(
            &session, "ignore", 6u, &ignore_metadata), CFLOW_MAILBOX_OK);
        check_equal(scxml_session_try_send_named_with_metadata(
            &session, "ignore", 6u, &ignore_metadata), CFLOW_MAILBOX_FULL);

        buffer = mem_get_buffer(mem_global(), 1u);
        check_not_null(buffer);
        ((unsigned char *)mem_buffer_data(buffer))[0] = (unsigned char)'X';
        mem_set_used(buffer, 1u);
        check_equal(cnet_send_buffer(&sender, outgoing, buffer), SALTS_OK);
        mem_buffer_release(buffer);

        deadline = cmeta_monotonic_ms() + CNET_SCXML_TIMEOUT_MS;
        while (stats.pending == 0u && cmeta_monotonic_ms() < deadline) {
            check_equal(cnet_client_poll(&sender, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&receiver, 1u, &events), SALTS_OK);
            check_true(scxml_cnet_ingress_get_stats(&ingress, &stats));
        }
        check_equal(stats.pending, (size_t)1u);
        check_equal(stats.received, UINT64_C(1));
        check_equal(scxml_cnet_ingress_arm(&ingress), SALTS_ENOBUFS);
        /* Host capacity FULL rejects further receive credits. A separate
           Session FIFO FULL refuses delivery of this already accepted
           chunk but does not release, overwrite or replay the Host row. */
        check_equal(scxml_cnet_ingress_drain(&ingress, 1u, &delivered),
                    CFLOW_MAILBOX_FULL);
        check_equal(delivered, (size_t)0u);
        check_true(scxml_cnet_ingress_get_stats(&ingress, &stats));
        check_equal(stats.pending, (size_t)1u);
        check_equal(stats.received, UINT64_C(1));
        check_equal(stats.delivered, UINT64_C(0));
        check_false(stats.failed);

        /* Only explicit progress on the original CFlow SerialExecutor
           restores Session mailbox credit. There is no automatic retry or
           duplicate NativeIO completion. */
        atomic_store_explicit(
            &gate.release, true, memory_order_release);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(scxml_cnet_ingress_drain(&ingress, 1u, &delivered),
                    CFLOW_MAILBOX_OK);
        check_equal(delivered, (size_t)1u);
        check_equal(scxml_cnet_ingress_drain(&ingress, 1u, &delivered),
                    CFLOW_MAILBOX_EMPTY);
        check_equal(delivered, (size_t)0u);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &state));
        check_true(state.done);
        check_false(state.errored);
        check_true(scxml_cnet_ingress_get_stats(&ingress, &stats));
        check_equal(stats.delivered, UINT64_C(1));
        check_equal(stats.pending, (size_t)0u);
        check_equal(stats.rejected_full, UINT64_C(1));
        check_false(stats.failed);

        check_equal(scxml_cnet_ingress_close(&ingress), SALTS_OK);
        check_equal(scxml_cnet_ingress_destroy(&ingress), SALTS_EBUSY);
        check_equal(cnet_close(&receiver, incoming), SALTS_OK);
        check_equal(cnet_close(&sender, outgoing), SALTS_OK);
        deadline = cmeta_monotonic_ms() + CNET_SCXML_TIMEOUT_MS;
        while (!scxml_cnet_ingress_is_quiescent(&ingress) &&
               cmeta_monotonic_ms() < deadline) {
            check_equal(cnet_client_poll(&receiver, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&sender, 1u, &events), SALTS_OK);
        }
        check_true(scxml_cnet_ingress_is_quiescent(&ingress));
        check_equal(scxml_cnet_ingress_destroy(&ingress), SALTS_OK);
        check_null(ingress.impl);

        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
        check_equal(cnet_client_stop(&receiver, 1000u), SALTS_OK);
        check_equal(cnet_client_destroy(&receiver), SALTS_OK);
        check_equal(cnet_client_stop(&sender, 1000u), SALTS_OK);
        check_equal(cnet_client_destroy(&sender), SALTS_OK);
        check_equal(cnet_listener_close(&listener), SALTS_OK);
        check_equal(cnet_listener_destroy(&listener), SALTS_OK);
    }
}
