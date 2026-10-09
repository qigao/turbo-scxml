#include <cnet/cnet.h>
#include <salts/clock.h>
#include <salts/native_io.h>
#include <tinytest.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Test-only external-progress host. It owns ONE NativeIO backend. CNet
 * borrows it, and all observation, routing and admission use the same owner.
 * No second request registry or completion engine is introduced. */
enum { REQUEST_TEST_TIMEOUT_MS = 5000, REQUEST_TEST_BATCH = 4,
       REQUEST_TEST_REUSE_ATTEMPTS = 32 };

typedef struct request_probe {
    bool connected;
    bool terminal;
    bool failed;
    size_t sends;
    size_t bytes;
} request_probe;

static native_io_backend_kind test_backend(void) {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
    return NATIVE_IO_BACKEND_KQUEUE;
#else
    return NATIVE_IO_BACKEND_EPOLL;
#endif
}

static void on_state(void *user, cnet_connection connection,
                     cnet_connection_state state, const cnet_error *error) {
    request_probe *probe = (request_probe *)user;
    (void)connection;
    if (state == CNET_CONNECTION_CONNECTED) probe->connected = true;
    if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED) {
        probe->terminal = true;
        if (state == CNET_CONNECTION_FAILED || error != NULL)
            probe->failed = true;
    }
}

static void on_send(void *user, cnet_connection connection, size_t bytes) {
    request_probe *probe = (request_probe *)user;
    (void)connection;
    ++probe->sends;
    probe->bytes += bytes;
}

static bool same_request(native_io_request a, native_io_request b) {
    return a.slot == b.slot && a.generation == b.generation;
}

static int external_progress(cnet_client *client, native_io_backend *backend,
                             uint32_t timeout_ms, native_io_request watched,
                             native_io_completion *watched_completion,
                             bool *found) {
    native_io_completion batch[REQUEST_TEST_BATCH] = {{0}};
    size_t count = 0u, events = 0u;
    int status = cnet_client_advance_external(client, &events);
    if (status != SALTS_OK) return status;
    status = native_io_backend_observe(
        backend, batch, REQUEST_TEST_BATCH, timeout_ms, &count);
    if (status == SALTS_ETIMEDOUT)
        return cnet_client_advance_external(client, &events);
    if (status != SALTS_OK) return status;

    for (size_t i = 0u; i < count; ++i) {
        bool consumed = false;
        size_t routed_events = 0u;
        if (found != NULL && !*found &&
            same_request(batch[i].request, watched)) {
            *found = true;
            if (watched_completion != NULL)
                *watched_completion = batch[i]; /* authentic observed packet */
        }
        status = cnet_client_route_external_completion(
            client, &batch[i], &consumed, &routed_events);
        if (status != SALTS_OK) return status;
        if (!consumed) return SALTS_EPROTO;
    }
    return SALTS_OK;
}

static int submit_one(cnet_client *client, cnet_connection connection,
                      unsigned char value, native_io_request *request) {
    cnet_external_request_snapshot snapshots[REQUEST_TEST_BATCH] = {{0}};
    mem_buffer_t *buffer;
    size_t count = 0u, events = 0u;
    int status;

    *request = (native_io_request){0};
    buffer = mem_get_buffer(mem_global(), 1u);
    if (buffer == NULL) return SALTS_ENOMEM;
    *(unsigned char *)mem_buffer_data(buffer) = value;
    mem_set_used(buffer, 1u);
    status = cnet_send_buffer(client, connection, buffer);
    mem_buffer_release(buffer);
    if (status != SALTS_OK) return status;
    status = cnet_client_advance_external(client, &events);
    if (status != SALTS_OK) return status;
    status = cnet_client_external_request_snapshots(
        client, connection, snapshots, REQUEST_TEST_BATCH, &count);
    if (status != SALTS_OK) return status;
    for (size_t i = 0u; i < count; ++i) {
        if (snapshots[i].operation_kind == NATIVE_IO_OPERATION_STREAM_SEND) {
            if (native_io_request_valid(*request)) return SALTS_EPROTO;
            *request = snapshots[i].request;
        }
    }
    return native_io_request_valid(*request) ? SALTS_OK : SALTS_ENOENT;
}

spec("CNet external NativeIO slot/generation and stale terminal ownership") {
    it("reuses a real native request slot without accepting the old terminal") {
        const native_io_backend_config io_conf = {
            .kind = test_backend(), .endpoint_capacity = 2u,
            .request_capacity = REQUEST_TEST_BATCH,
            .completion_batch_capacity = REQUEST_TEST_BATCH
        };
        const cnet_client_config cnet_conf = {
            .backend = test_backend(), .connection_capacity = 1u,
            .command_capacity = 8u, .request_capacity = REQUEST_TEST_BATCH,
            .completion_batch_capacity = REQUEST_TEST_BATCH,
            .event_capacity = 8u, .max_send_bytes = 8u,
            .receive_buffer_bytes = 8u,
            .connect_timeout_ms = 1000u, .write_timeout_ms = 1000u
        };
        const cnet_observer observer = {
            .on_state = on_state, .on_send = on_send
        };
        native_io_backend io = {0};
        cnet_client client = {0};
        cnet_listener listener = {0}, outbound = {0};
        cnet_stream_endpoint bind = CNET_STREAM_ENDPOINT_INIT;
        cnet_stream_endpoint remote = CNET_STREAM_ENDPOINT_INIT;
        cnet_connection connection = {0};
        request_probe probe = {0};
        cnet_observer bound_observer = observer;
        native_io_request first = {0}, next = {0};
        native_io_completion old_packet = {0};
        native_io_backend_stats stats = {0};
        uint64_t deadline;
        size_t events = 0u;
        bool captured = false, reused = false, consumed = true;
        unsigned char value = 'A';

        bound_observer.user = &probe;
        check_equal(native_io_backend_init(&io, &io_conf), SALTS_OK);
        check_equal(cnet_client_init_external(&client, &cnet_conf, &io),
                    SALTS_OK);
        bind.family = CNET_DATAGRAM_ADDRESS_IPV4;
        bind.address[0] = 127u; bind.address[3] = 1u;
        check_equal(cnet_listener_open(
            &listener, test_backend(), CNET_DATAGRAM_ADDRESS_IPV4), SALTS_OK);
        check_equal(cnet_listener_bind_open_endpoint(&listener, &bind), SALTS_OK);
        check_equal(cnet_listener_local_endpoint(&listener, &remote), SALTS_OK);
        check_true(remote.port != 0u);
        check_equal(cnet_listener_listen(&listener, 8u), SALTS_OK);
        check_equal(cnet_listener_open(
            &outbound, test_backend(), CNET_DATAGRAM_ADDRESS_IPV4), SALTS_OK);
        check_equal(cnet_listener_connect_endpoint(
            &outbound, &client, &remote, &bound_observer, &connection), SALTS_OK);
        check_null(outbound.impl);
        deadline = cmeta_monotonic_ms() + REQUEST_TEST_TIMEOUT_MS;
        while (!probe.connected && !probe.failed &&
               cmeta_monotonic_ms() < deadline) {
            check_equal(external_progress(
                &client, &io, 1u, (native_io_request){0}, NULL, NULL), SALTS_OK);
        }
        check_true(probe.connected);
        check_false(probe.failed);

        check_equal(submit_one(&client, connection, value++, &first), SALTS_OK);
        deadline = cmeta_monotonic_ms() + REQUEST_TEST_TIMEOUT_MS;
        while (probe.sends < 1u && cmeta_monotonic_ms() < deadline)
            check_equal(external_progress(
                &client, &io, 1u, first, &old_packet, &captured), SALTS_OK);
        check_true(captured);
        check_equal(probe.sends, (size_t)1u);
        check_true(same_request(first, old_packet.request));
        check_equal(old_packet.kind, NATIVE_IO_COMPLETION_OK);
        check_equal(native_io_backend_cancel(&io, first), SALTS_ENOENT);

        for (size_t round = 0u; round < REQUEST_TEST_REUSE_ATTEMPTS; ++round) {
            size_t old_sends = probe.sends, routed_events = 99u;
            check_equal(submit_one(&client, connection, value++, &next),
                        SALTS_OK);
            if (next.slot == first.slot) {
                /* Same real NativeIO slot, but with a new generation.
                   A stale cancel must not cancel the new NativeIO request. */
                check_not_equal(next.generation, first.generation);
                check_equal(native_io_backend_cancel(&io, first), SALTS_ENOENT);
                check_true(native_io_backend_get_stats(&io, &stats));
                check_equal(stats.active_requests, (size_t)1u);

                /* Duplicate a real already-routed completion while the
                   replacement request is live; not a synthetic packet. */
                consumed = true;
                check_equal(cnet_client_route_external_completion(
                    &client, &old_packet, &consumed, &routed_events), SALTS_OK);
                check_false(consumed);
                check_equal(routed_events, (size_t)0u);
                check_equal(probe.sends, old_sends);
                check_true(native_io_backend_get_stats(&io, &stats));
                check_equal(stats.active_requests, (size_t)1u);
                reused = true;
            }
            deadline = cmeta_monotonic_ms() + REQUEST_TEST_TIMEOUT_MS;
            while (probe.sends == old_sends &&
                   !probe.terminal && cmeta_monotonic_ms() < deadline)
                check_equal(external_progress(
                    &client, &io, 1u, next, NULL, NULL), SALTS_OK);
            check_equal(probe.sends, old_sends + 1u);
            check_false(probe.terminal);
            if (reused) {
                /* Even after the genuine replacement terminal is routed,
                   a replay of that packet cannot settle anything twice. */
                break;
            }
        }
        check_true(reused);
        check_equal(probe.bytes, probe.sends);
        check_equal(cnet_close(&client, connection), SALTS_OK);
        deadline = cmeta_monotonic_ms() + REQUEST_TEST_TIMEOUT_MS;
        while (!probe.terminal && cmeta_monotonic_ms() < deadline)
            check_equal(external_progress(
                &client, &io, 1u, (native_io_request){0}, NULL, NULL), SALTS_OK);
        check_true(probe.terminal);
        check_false(probe.failed);

        check_equal(cnet_client_stop_external(&client), SALTS_OK);
        check_equal(cnet_client_destroy(&client), SALTS_OK);
        check_equal(cnet_listener_close(&listener), SALTS_OK);
        check_equal(cnet_listener_destroy(&listener), SALTS_OK);
        check_equal(native_io_backend_close(&io), SALTS_OK);
        check_equal(native_io_backend_destroy(&io), SALTS_OK);
    }
}
