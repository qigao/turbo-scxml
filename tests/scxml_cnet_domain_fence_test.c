#include <scxml/cnet_domain_fence.h>

#include <cnet/cnet.h>
#include <salts/clock.h>
#include <salts/thread.h>
#include <tinytest.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { FENCE_TEST_TIMEOUT_MS = 5000 };

typedef struct fence_sender {
    bool connected;
    bool terminal;
    size_t completed;
    size_t accepted_bytes;
} fence_sender;

typedef struct fence_receiver {
    bool connected;
    bool terminal;
    bool failed;
    unsigned char received[16];
    size_t used;
} fence_receiver;

typedef struct fence_submit {
    scxml_cnet_domain_fence *fence;
    cnet_client *client;
    cnet_connection connection;
    const unsigned char *bytes;
    size_t size;
    bool attempt_reentry;
    int reenter_result;
} fence_submit;

typedef struct fence_quiescence {
    bool native_done;
    bool component_scope_released;
} fence_quiescence;

static native_io_backend_kind test_backend(void) {
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
    config.backend = test_backend();
    config.connection_capacity = 2u;
    config.command_capacity = 8u;
    config.request_capacity = 8u;
    config.completion_batch_capacity = 8u;
    config.event_capacity = 8u;
    config.max_send_bytes = 64u;
    config.receive_buffer_bytes = 64u;
    return config;
}

static void outgoing_state(
    void *user, cnet_connection handle, cnet_connection_state state,
    const cnet_error *error) {
    fence_sender *probe = (fence_sender *)user;
    (void)handle;
    (void)error;
    if (state == CNET_CONNECTION_CONNECTED) probe->connected = true;
    if (state == CNET_CONNECTION_CLOSED ||
        state == CNET_CONNECTION_FAILED) probe->terminal = true;
}

static void outgoing_send(
    void *user, cnet_connection handle, size_t bytes) {
    fence_sender *probe = (fence_sender *)user;
    (void)handle;
    ++probe->completed;
    probe->accepted_bytes += bytes;
}

static void incoming_state(
    void *user, cnet_connection handle, cnet_connection_state state,
    const cnet_error *error) {
    fence_receiver *probe = (fence_receiver *)user;
    (void)handle;
    (void)error;
    if (state == CNET_CONNECTION_CONNECTED) probe->connected = true;
    if (state == CNET_CONNECTION_CLOSED ||
        state == CNET_CONNECTION_FAILED) probe->terminal = true;
}

static void incoming_receive(
    void *user, cnet_connection handle, const cnet_receive_view *view) {
    fence_receiver *probe = (fence_receiver *)user;
    (void)handle;
    if (view == NULL || view->kind != CNET_MESSAGE_BYTES ||
        (view->size != 0u && view->data == NULL) ||
        view->size > sizeof(probe->received) - probe->used) {
        probe->failed = true;
        return;
    }
    memcpy(probe->received + probe->used, view->data, view->size);
    probe->used += view->size;
}

static int submit_bytes(void *user) {
    fence_submit *submit = (fence_submit *)user;
    mem_buffer_t *buffer;
    int status;
    if (submit == NULL || submit->client == NULL ||
        submit->bytes == NULL || submit->size == 0u) return SALTS_EINVAL;
    if (submit->attempt_reentry) {
        /* A publication on the same CNet owner must not interleave between
           generation verification and real CNet command admission. */
        submit->reenter_result = scxml_cnet_domain_fence_activate(
            submit->fence, UINT64_C(12));
    }
    buffer = mem_get_buffer(mem_global(), submit->size);
    if (buffer == NULL) return SALTS_ENOMEM;
    memcpy(mem_buffer_data(buffer), submit->bytes, submit->size);
    mem_set_used(buffer, submit->size);
    status = cnet_send_buffer(submit->client, submit->connection, buffer);
    mem_buffer_release(buffer);
    return status;
}

static int accept_noop(void *unused) {
    (void)unused;
    return SALTS_OK;
}

static bool check_quiescence(void *user) {
    const fence_quiescence *q = (const fence_quiescence *)user;
    return q != NULL && q->native_done && q->component_scope_released;
}

typedef struct foreign_owner_attempt {
    scxml_cnet_domain_fence *fence;
    int status;
} foreign_owner_attempt;

static void foreign_thread(void *arg) {
    foreign_owner_attempt *ctx = (foreign_owner_attempt *)arg;
    ctx->status = scxml_cnet_domain_fence_try_submit(
        ctx->fence, UINT64_C(11), accept_noop, NULL);
}

spec("ACE CNet domain exclusive owner and Component generation fencing") {
    it("keeps one real TCP listener across N-to-N+1 and fences every new write") {
        const cnet_client_config net_config = client_config();
        const cnet_listener_config listener_config = {
            .backend = test_backend(), .host = "127.0.0.1",
            .port = 0u, .backlog = 2u
        };
        scxml_cnet_domain_fence fence = {0};
        scxml_cnet_domain_fence_stats stats = {0};
        cnet_client sender = {0}, receiver = {0};
        cnet_listener listener = {0};
        cnet_connection outgoing = {0}, incoming = {0};
        fence_sender send_probe = {0};
        fence_receiver recv_probe = {0};
        fence_quiescence g1 = {0}, g2 = {0};
        cnet_observer sender_observer = {
            .on_state = outgoing_state, .on_send = outgoing_send,
            .user = &send_probe
        };
        cnet_observer receiver_observer = {
            .on_state = incoming_state, .on_receive = incoming_receive,
            .user = &recv_probe
        };
        fence_submit send1 = {0}, send2 = {0};
        cnet_connect_options options = {0};
        uint16_t port = 0u;
        char uri[64];
        uint64_t deadline;
        size_t events = 0u;
        int ready = 0;
        const unsigned char a[] = {'A'}, b[] = {'B'};
        const uint64_t gen1 = UINT64_C(11), gen2 = UINT64_C(12),
                       gen3 = UINT64_C(13), gen4 = UINT64_C(14);

        check_equal(scxml_cnet_domain_fence_init(&fence), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_attach(&fence, gen1), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_attach(&fence, gen1), SALTS_EALREADY);
        check_true(scxml_cnet_domain_fence_get_stats(&fence, &stats));
        check_equal(stats.current_generation, gen1);
        check_equal(stats.admission_epoch, UINT64_C(1));

        check_equal(cnet_client_init(&sender, &net_config), SALTS_OK);
        check_equal(cnet_client_init(&receiver, &net_config), SALTS_OK);
        check_equal(cnet_listener_init(&listener, &listener_config), SALTS_OK);
        check_equal(cnet_listener_port(&listener, &port), SALTS_OK);
        check_true(port != 0u);
        check_true(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u",
                            (unsigned int)port) > 0);
        options.uri = uri;
        options.observer = sender_observer;
        check_equal(cnet_connect(&sender, &options, &outgoing), SALTS_OK);
        deadline = cmeta_monotonic_ms() + FENCE_TEST_TIMEOUT_MS;
        while (!send_probe.connected && cmeta_monotonic_ms() < deadline)
            check_equal(cnet_client_poll(&sender, 1u, &events), SALTS_OK);
        check_true(send_probe.connected);
        check_equal(cnet_listener_wait(
            &listener, FENCE_TEST_TIMEOUT_MS, &ready), SALTS_OK);
        check_equal(ready, 1);
        check_equal(cnet_listener_accept(
            &listener, &receiver, &receiver_observer, &incoming), SALTS_OK);
        deadline = cmeta_monotonic_ms() + FENCE_TEST_TIMEOUT_MS;
        while (!recv_probe.connected && cmeta_monotonic_ms() < deadline)
            check_equal(cnet_client_poll(&receiver, 1u, &events), SALTS_OK);
        check_true(recv_probe.connected);
        check_equal(cnet_receive(&receiver, incoming, 2u), SALTS_OK);

        send1 = (fence_submit){
            .fence = &fence, .client = &sender, .connection = outgoing,
            .bytes = a, .size = sizeof(a), .attempt_reentry = true
        };
        check_equal(scxml_cnet_domain_fence_try_submit(
            &fence, gen1, submit_bytes, &send1), SALTS_OK);
        check_equal(send1.reenter_result, SALTS_EBUSY);
        /* g1's first native write may still be in-flight when g2 publishes.
           The actual CNet terminal is NOT a Component switch callback. */
        check_equal(scxml_cnet_domain_fence_attach(&fence, gen2), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_attach(&fence, gen3), SALTS_EBUSY);
        check_equal(scxml_cnet_domain_fence_activate(&fence, gen2), SALTS_OK);
        check_true(scxml_cnet_domain_fence_get_stats(&fence, &stats));
        check_equal(stats.current_generation, gen2);
        check_equal(stats.draining_generation, gen1);
        check_equal(stats.attached, (size_t)2u);
        check_equal(stats.admission_epoch, UINT64_C(2));
        check_equal(scxml_cnet_domain_fence_try_submit(
            &fence, gen1, submit_bytes, &send1), SALTS_EPERM);
        check_equal(scxml_cnet_domain_fence_attach(&fence, gen3), SALTS_EBUSY);
        check_equal(scxml_cnet_domain_fence_retire(
            &fence, gen1, check_quiescence, &g1), SALTS_EBUSY);

        send2 = (fence_submit){
            .fence = &fence, .client = &sender, .connection = outgoing,
            .bytes = b, .size = sizeof(b)
        };
        check_equal(scxml_cnet_domain_fence_try_submit(
            &fence, gen2, submit_bytes, &send2), SALTS_OK);

        /* Old completion remains authoritative under the original CNet
           connection/request identity, even after its generation was fenced. */
        deadline = cmeta_monotonic_ms() + FENCE_TEST_TIMEOUT_MS;
        while ((send_probe.completed < 2u || recv_probe.used < 2u) &&
               cmeta_monotonic_ms() < deadline) {
            check_equal(cnet_client_poll(&sender, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&receiver, 1u, &events), SALTS_OK);
        }
        check_equal(send_probe.completed, (size_t)2u);
        check_equal(send_probe.accepted_bytes, (size_t)2u);
        check_equal(recv_probe.used, (size_t)2u);
        check_false(recv_probe.failed);
        check_true(memcmp(recv_probe.received, "AB", 2u) == 0);
        /* Real CNet completion is necessary, but not sufficient: the original
           Component scope / deferred provider borrowers must also quiesce. */
        g1.native_done = true;
        check_equal(scxml_cnet_domain_fence_retire(
            &fence, gen1, check_quiescence, &g1), SALTS_EBUSY);
        g1.component_scope_released = true;
        check_equal(scxml_cnet_domain_fence_retire(
            &fence, gen1, check_quiescence, &g1), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_attach(&fence, gen3), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_retire(
            &fence, gen3, NULL, NULL), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_attach(&fence, gen3), SALTS_EALREADY);
        check_equal(scxml_cnet_domain_fence_attach(&fence, gen4), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_activate(&fence, gen4), SALTS_OK);
        check_true(scxml_cnet_domain_fence_get_stats(&fence, &stats));
        check_equal(stats.draining_generation, gen2);
        check_equal(stats.current_generation, gen4);
        check_equal(stats.accepted, UINT64_C(2));
        check_equal(stats.rejected, UINT64_C(1));
        check_equal(stats.switches, UINT64_C(2));

        /* Exactly one listener remains bound. Closing the gate does not
           request-close the connection or terminate native callbacks. */
        check_equal(scxml_cnet_domain_fence_close(&fence), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_destroy(&fence), SALTS_EBUSY);
        check_equal(scxml_cnet_domain_fence_try_submit(
            &fence, gen4, submit_bytes, &send2), SALTS_ESHUTDOWN);
        g2.native_done = true;
        g2.component_scope_released = true;
        check_equal(scxml_cnet_domain_fence_retire(
            &fence, gen2, check_quiescence, &g2), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_retire(
            &fence, gen4, check_quiescence, &g2), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_destroy(&fence), SALTS_OK);

        check_equal(cnet_close(&sender, outgoing), SALTS_OK);
        check_equal(cnet_close(&receiver, incoming), SALTS_OK);
        deadline = cmeta_monotonic_ms() + FENCE_TEST_TIMEOUT_MS;
        while ((!send_probe.terminal || !recv_probe.terminal) &&
               cmeta_monotonic_ms() < deadline) {
            check_equal(cnet_client_poll(&sender, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&receiver, 1u, &events), SALTS_OK);
        }
        check_true(send_probe.terminal);
        check_equal(cnet_client_stop(&receiver, 1000u), SALTS_OK);
        check_equal(cnet_client_destroy(&receiver), SALTS_OK);
        check_equal(cnet_client_stop(&sender, 1000u), SALTS_OK);
        check_equal(cnet_client_destroy(&sender), SALTS_OK);
        check_equal(cnet_listener_close(&listener), SALTS_OK);
        check_equal(cnet_listener_destroy(&listener), SALTS_OK);
    }

    it("rejects cross-owner submit without borrowing the Component generation") {
        scxml_cnet_domain_fence fence = {0};
        scxml_cnet_domain_fence_stats stats = {0};
        foreign_owner_attempt ctx = {0};
        cmeta_thread_t thread = NULL;
        check_equal(scxml_cnet_domain_fence_init(&fence), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_attach(
            &fence, UINT64_C(11)), SALTS_OK);
        ctx.fence = &fence;
        check_equal(cmeta_thread_create(&thread, foreign_thread, &ctx), SALTS_OK);
        check_equal(cmeta_thread_join(&thread), SALTS_OK);
        cmeta_thread_destroy(&thread);
        check_equal(ctx.status, SALTS_EINVAL);
        check_true(scxml_cnet_domain_fence_get_stats(&fence, &stats));
        check_equal(stats.accepted, UINT64_C(0));
        check_equal(scxml_cnet_domain_fence_close(&fence), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_retire(
            &fence, UINT64_C(11), NULL, NULL), SALTS_EINVAL);
        {
            fence_quiescence ready = {true, true};
            check_equal(scxml_cnet_domain_fence_retire(
                &fence, UINT64_C(11), check_quiescence, &ready), SALTS_OK);
        }
        check_equal(scxml_cnet_domain_fence_destroy(&fence), SALTS_OK);
    }
}
