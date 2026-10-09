#include <scxml/component.h>
#include <scxml/cnet_domain_fence.h>
#include "scxml_component_invoke_probe.h"

#include <cnet/cnet.h>
#include <salts/clock.h>
#include <salts/plugin.h>
#include <tinytest.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { GENERATIONS = 3, SESSIONS = 2, TIMEOUT_MS = 5000 };

typedef struct invoke_dso_generation {
    salts_component_plugin_generation generation;
    salts_component_deployment deployments[1];
    salts_component_instance instances[1];
    salts_component_dependency dependencies[1];
    size_t activation_order[1];
    salts_component_plugin_module modules[1];
} invoke_dso_generation;

typedef struct net_sender_probe {
    bool connected;
    bool terminal;
    size_t sends_done;
    size_t bytes_completed;
} net_sender_probe;

typedef struct net_receiver_probe {
    bool connected;
    bool terminal;
    bool failed;
    unsigned char bytes[16];
    size_t received;
} net_receiver_probe;

typedef struct joint_fixture {
    cmeta_plugin_registry registry;
    bool registry_live;
    cmeta_plugin_ref plugins[SESSIONS];
    salts_component_plugin_runtime runtime;
    invoke_dso_generation generations[GENERATIONS];
    scxml_component_scope scopes[SESSIONS];
    scxml_component_invoke_provider invoke[SESSIONS];
    scxml_program program;
    scxml_session sessions[SESSIONS];
    cflow_executor executors[SESSIONS];
    bool executor_live[SESSIONS];
    scxml_cnet_domain_fence fence;
    cnet_client sender;
    cnet_client receiver;
    cnet_listener listener;
    cnet_connection outbound;
    cnet_connection inbound;
    cnet_connection outbound_next;
    cnet_connection inbound_next;
    net_sender_probe sender_probe;
    net_receiver_probe receiver_probe;
    net_receiver_probe receiver_probe_next;
} joint_fixture;

typedef struct native_scope_probe {
    joint_fixture *fixture;
    size_t accepted_terminal_count;
    size_t generation_slot;
    bool saw_native_terminal;
    bool saw_scope_busy;
} native_scope_probe;

typedef struct fenced_cnet_write {
    joint_fixture *fixture;
    const unsigned char *data;
    size_t size;
    cnet_connection connection;
} fenced_cnet_write;

static native_io_backend_kind backend(void) {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
    return NATIVE_IO_BACKEND_EPOLL;
#else
    return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

static cnet_client_config cnet_config(void) {
    cnet_client_config conf = {0};
    conf.backend = backend();
    conf.connection_capacity = 2u;
    conf.command_capacity = 8u;
    conf.request_capacity = 8u;
    conf.completion_batch_capacity = 8u;
    conf.event_capacity = 8u;
    conf.max_send_bytes = 64u;
    conf.receive_buffer_bytes = 64u;
    return conf;
}

static void sender_state(void *user, cnet_connection handle,
                         cnet_connection_state state, const cnet_error *error) {
    net_sender_probe *probe = (net_sender_probe *)user;
    (void)handle;
    (void)error;
    if (state == CNET_CONNECTION_CONNECTED) probe->connected = true;
    if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED)
        probe->terminal = true;
}

static void sender_send(void *user, cnet_connection handle, size_t bytes) {
    net_sender_probe *probe = (net_sender_probe *)user;
    (void)handle;
    ++probe->sends_done;
    probe->bytes_completed += bytes;
}

static void receiver_state(void *user, cnet_connection handle,
                           cnet_connection_state state, const cnet_error *error) {
    net_receiver_probe *probe = (net_receiver_probe *)user;
    (void)handle;
    (void)error;
    if (state == CNET_CONNECTION_CONNECTED) probe->connected = true;
    if (state == CNET_CONNECTION_CLOSED || state == CNET_CONNECTION_FAILED)
        probe->terminal = true;
}

static void receiver_data(void *user, cnet_connection handle,
                          const cnet_receive_view *view) {
    net_receiver_probe *probe = (net_receiver_probe *)user;
    (void)handle;
    if (view == NULL || view->kind != CNET_MESSAGE_BYTES ||
        (view->size != 0u && view->data == NULL) ||
        view->size > sizeof(probe->bytes) - probe->received) {
        probe->failed = true;
        return;
    }
    memcpy(probe->bytes + probe->received, view->data, view->size);
    probe->received += view->size;
}

static salts_component_plugin_status generation_build(
    joint_fixture *fixture, size_t slot, size_t plugin_index,
    uint64_t generation_id) {
    invoke_dso_generation *g = &fixture->generations[slot];
    const salts_component_plugin_generation_storage storage = {
        g->deployments, 1u, g->instances, 1u, g->dependencies, 1u,
        g->activation_order, 1u, g->modules, 1u
    };
    const salts_component_plugin_source source = {
        fixture->plugins[plugin_index], "component-provider", NULL, NULL
    };
    return salts_component_plugin_generation_build(
        &g->generation, generation_id, &fixture->registry, &storage,
        NULL, 0u, &source, 1u, NULL, 0u);
}

/* Get a DSO-owned Invoke token through its *real* borrowed Component Scope.
   This is not a test-managed imitation of the Session invoke registry. */
static int invoke_token_from_scope(const scxml_component_scope *scope) {
    salts_component_service service = {0};
    if (scope == NULL || !scope->live ||
        salts_component_plugin_scope_find_service_from(
            &scope->component_scope, "ScxmlInvokeDsoFixture",
            scxml_invoke_provider_interface(), &service) !=
            SALTS_COMPONENT_PLUGIN_OK ||
        service.object == NULL ||
        !cmeta_data_desc_equal(service.object->data, &cmeta_data_int) ||
        service.object->object == NULL)
        return 0;
    return *(const int *)service.object->object;
}

static int send_one(void *user) {
    fenced_cnet_write *request = (fenced_cnet_write *)user;
    mem_buffer_t *buffer;
    int status;
    if (request == NULL || request->fixture == NULL ||
        request->data == NULL || request->size == 0u)
        return SALTS_EINVAL;
    buffer = mem_get_buffer(mem_global(), request->size);
    if (buffer == NULL) return SALTS_ENOMEM;
    memcpy(mem_buffer_data(buffer), request->data, request->size);
    mem_set_used(buffer, request->size);
    status = cnet_send_buffer(
        &request->fixture->sender, request->connection, buffer);
    mem_buffer_release(buffer);
    return status;
}

/* This proof reads the *actual* CNet completion callback count and asks
   Salts ComponentPlugin itself whether the DSO generation may retire.
   There are no test-local "scope released" flags or shadow leases. */
static bool retire_real_generation(void *user) {
    native_scope_probe *probe = (native_scope_probe *)user;
    joint_fixture *fixture;
    salts_component_plugin_status status;
    if (probe == NULL || probe->fixture == NULL) return false;
    fixture = probe->fixture;
    if (fixture->sender_probe.sends_done <
        probe->accepted_terminal_count) return false;
    probe->saw_native_terminal = true;
    status = salts_component_plugin_generation_drain(
        &fixture->runtime, &fixture->generations[probe->generation_slot].generation);
    if (status == SALTS_COMPONENT_PLUGIN_BUSY)
        probe->saw_scope_busy = true;
    return status == SALTS_COMPONENT_PLUGIN_OK;
}

/* This test uses CMeta's *typed* ObjectRef projection as the only path to
 * DSO-resident CNet callback pointers and terminal snapshots. A borrowed
 * pointer is NEVER called after the authoritative Component Scope is released.
 */
typedef struct joint_dso_native_observer {
    joint_fixture *fixture;
    scxml_test_cnet_probe adapter;
    size_t generation_slot;
    size_t expected_sends;
    bool saw_native_terminal;
    bool saw_scope_busy;
} joint_dso_native_observer;

static bool dso_native_observer_from_scope(
    const scxml_component_scope *scope, scxml_test_cnet_probe *out) {
    salts_component_service service = {0};
    if (out != NULL) *out = scxml_test_cnet_probe_bind(NULL, NULL);
    if (scope == NULL || !scope->live || out == NULL ||
        salts_component_plugin_scope_find_service_from(
            &scope->component_scope, "ScxmlInvokeDsoFixture",
            scxml_test_cnet_probe_interface(), &service) !=
                SALTS_COMPONENT_PLUGIN_OK ||
        service.object == NULL || service.interfaces == NULL)
        return false;
    return scxml_test_cnet_probe_borrow_from_object(
        service.object, service.interfaces, out) == CMETA_OK &&
        scxml_test_cnet_probe_valid(out);
}

static bool native_dso_snapshot(
    scxml_test_cnet_probe *adapter,
    scxml_test_cnet_probe_snapshot *out) {
    return adapter != NULL && scxml_test_cnet_probe_valid(adapter) &&
        out != NULL && scxml_test_cnet_probe_snapshot(adapter, out);
}

static bool retire_native_dso_generation(void *user) {
    joint_dso_native_observer *probe = (joint_dso_native_observer *)user;
    scxml_test_cnet_probe_snapshot state = {0};
    salts_component_plugin_status status;
    if (probe == NULL || probe->fixture == NULL ||
        probe->generation_slot >= SESSIONS)
        return false;
    if (probe->fixture->scopes[probe->generation_slot].live) {
        if (!native_dso_snapshot(&probe->adapter, &state) ||
            state.native_sends < probe->expected_sends ||
            !state.native_terminal)
            return false;
        /* Immutable evidence copied from the *real* DSO on_state callback,
           while that callback's module is still Scope-pinned. The adapter
           function pointer is NOT dereferenced again after Scope release. */
        probe->saw_native_terminal = true;
    }
    if (!probe->saw_native_terminal) return false;
    status = salts_component_plugin_generation_drain(
        &probe->fixture->runtime,
        &probe->fixture->generations[probe->generation_slot].generation);
    if (status == SALTS_COMPONENT_PLUGIN_BUSY)
        probe->saw_scope_busy = true;
    return status == SALTS_COMPONENT_PLUGIN_OK;
}

static bool signal(joint_fixture *fixture, size_t slot, const char *name) {
    const scxml_event_metadata metadata = {
        .abi_version = SCXML_EVENT_METADATA_ABI,
        .struct_size = sizeof(scxml_event_metadata)
    };
    return fixture != NULL && name != NULL &&
        scxml_session_try_send_named_with_metadata(
            &fixture->sessions[slot], name, strlen(name), &metadata) ==
            CFLOW_MAILBOX_OK &&
        cflow_executor_wait_idle(&fixture->executors[slot]);
}

static bool fixture_cleanup(joint_fixture *f) {
    bool ok = true;
    size_t i;
    if (f == NULL) return false;
    /* A failed assertion still attempts to quiesce the real native owners
       before touching Session/Plugin callback code. */
    if (f->outbound.generation != 0u)
        (void)cnet_close(&f->sender, f->outbound);
    if (f->outbound_next.generation != 0u)
        (void)cnet_close(&f->sender, f->outbound_next);
    if (f->inbound.generation != 0u)
        (void)cnet_close(&f->receiver, f->inbound);
    if (f->inbound_next.generation != 0u)
        (void)cnet_close(&f->receiver, f->inbound_next);
    if (f->sender.impl != NULL)
        ok = cnet_client_stop(&f->sender, 1000u) == SALTS_OK && ok;
    if (f->receiver.impl != NULL)
        ok = cnet_client_stop(&f->receiver, 1000u) == SALTS_OK && ok;
    if (f->sender.impl != NULL)
        ok = cnet_client_destroy(&f->sender) == SALTS_OK && ok;
    if (f->receiver.impl != NULL)
        ok = cnet_client_destroy(&f->receiver) == SALTS_OK && ok;
    if (f->listener.impl != NULL) {
        ok = cnet_listener_close(&f->listener) == SALTS_OK && ok;
        ok = cnet_listener_destroy(&f->listener) == SALTS_OK && ok;
    }
    for (i = 0u; i < SESSIONS; ++i) {
        if (f->sessions[i].impl != NULL) {
            scxml_session_cancel(&f->sessions[i]);
            ok = cflow_executor_wait_idle(&f->executors[i]) && ok;
            ok = scxml_session_destroy(&f->sessions[i]) ==
                CFLOW_STATECHART_INSTANCE_OK && ok;
        }
        if (f->executor_live[i])
            cflow_executor_destroy(&f->executors[i]);
        if (f->invoke[i].live)
            ok = scxml_component_invoke_provider_destroy(&f->invoke[i]) ==
                SCXML_COMPONENT_OK && ok;
        if (f->scopes[i].live)
            ok = scxml_component_scope_release(&f->scopes[i]) ==
                SCXML_COMPONENT_OK && ok;
    }
    scxml_program_destroy(&f->program);
    if (f->runtime.initialized && f->runtime.current != NULL) {
        salts_component_plugin_generation *previous = NULL;
        ok = salts_component_plugin_runtime_close(
            &f->runtime, &previous) == SALTS_COMPONENT_PLUGIN_OK && ok;
    }
    for (i = 0u; i < GENERATIONS; ++i) {
        salts_component_plugin_generation *g = &f->generations[i].generation;
        if (g->state == SALTS_COMPONENT_PLUGIN_GENERATION_DRAINING)
            ok = salts_component_plugin_generation_drain(
                &f->runtime, g) == SALTS_COMPONENT_PLUGIN_OK && ok;
        else if (g->state == SALTS_COMPONENT_PLUGIN_GENERATION_BUILT)
            ok = salts_component_plugin_generation_discard(g) ==
                SALTS_COMPONENT_PLUGIN_OK && ok;
    }
    if (f->fence.impl != NULL) {
        scxml_cnet_domain_fence_stats stats = {0};
        if (scxml_cnet_domain_fence_get_stats(&f->fence, &stats)) {
            native_scope_probe ready = {f, 0u, 0u, false, false};
            size_t slot;
            (void)scxml_cnet_domain_fence_close(&f->fence);
            /* The test's regular path must retire each attached generation.
               On failure we still best-effort release domain heap storage. */
            for (slot = 0u; slot < GENERATIONS; ++slot) {
                const uint64_t gid = (uint64_t)slot + 11u;
                if (gid != stats.current_generation &&
                    gid != stats.staged_generation &&
                    gid != stats.draining_generation) continue;
                if (gid == stats.staged_generation)
                    (void)scxml_cnet_domain_fence_retire(
                        &f->fence, gid, NULL, NULL);
                else {
                    ready.generation_slot = slot;
                    (void)scxml_cnet_domain_fence_retire(
                        &f->fence, gid, retire_real_generation, &ready);
                }
            }
        }
        ok = scxml_cnet_domain_fence_destroy(&f->fence) == SALTS_OK && ok;
    }
    if (f->runtime.initialized)
        ok = salts_component_plugin_runtime_destroy(&f->runtime) ==
            SALTS_COMPONENT_PLUGIN_OK && ok;
    if (f->registry_live) {
        for (i = 0u; i < SESSIONS; ++i) {
            bool quiescent = false;
            if (f->plugins[i].generation == 0u) continue;
            ok = cmeta_plugin_registry_request_stop(
                &f->registry, f->plugins[i]) == CMETA_PLUGIN_OK && ok;
            ok = cmeta_plugin_registry_poll_quiescent(
                &f->registry, f->plugins[i], &quiescent) ==
                CMETA_PLUGIN_OK && quiescent && ok;
            ok = cmeta_plugin_registry_unload(
                &f->registry, f->plugins[i]) == CMETA_PLUGIN_OK && ok;
        }
        ok = cmeta_plugin_registry_destroy(&f->registry) == CMETA_PLUGIN_OK && ok;
    }
    return ok;
}

spec("DSO-backed Invoke plus real CNet terminal across ACE generation switch") {
    static joint_fixture f;

    before_each() { memset(&f, 0, sizeof(f)); }
    after_each() { check_true(fixture_cleanup(&f)); }

    it("pins old Invoke/Plugin until native completion and rejects stale tokens") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "version='1.0' initial='working'>"
            "<state id='working'>"
            "<invoke id='worker' type='urn:test:invoke' src='worker'/>"
            "<transition event='done.invoke.worker' target='done'/>"
            "<transition event='finish' target='done'/>"
            "</state><final id='done'/></scxml>";
        const char *paths[] = {
            SCXML_INVOKE_DSO_ONE, SCXML_INVOKE_DSO_TWO
        };
        const cmeta_plugin_registry_config registry_config = {.capacity = 2u};
        const cnet_client_config net_config = cnet_config();
        const cnet_listener_config listener_config = {
            .backend = backend(), .host = "127.0.0.1",
            .port = 0u, .backlog = 2u
        };
        cnet_observer send_observer = {
            .on_state = sender_state, .on_send = sender_send,
            .user = &f.sender_probe
        };
        cnet_observer recv_observer = {
            .on_state = receiver_state, .on_receive = receiver_data,
            .user = &f.receiver_probe
        };
        cnet_connect_options options = {0};
        scxml_diagnostic diagnostic = {0};
        salts_component_plugin_generation *previous = NULL;
        scxml_cnet_domain_fence_stats fence_stats = {0};
        scxml_invoke_stats invoke_stats = {0};
        cflow_statechart_instance_stats session_stats = {0};
        native_scope_probe old_probe = {&f, 1u, 0u, false, false};
        native_scope_probe next_probe = {&f, 3u, 1u, false, false};
        fenced_cnet_write write_a, write_b, write_c;
        uint16_t port = 0u;
        uint64_t deadline, gen1, gen2;
        char uri[64];
        int token1, token2, ready = 0;
        size_t events = 0u;
        const unsigned char a = 'A', b = 'B', c = 'C';

        check_equal(cmeta_plugin_registry_init(&f.registry, &registry_config),
                    CMETA_PLUGIN_OK);
        f.registry_live = true;
        for (size_t i = 0u; i < SESSIONS; ++i) {
            check_equal(cmeta_plugin_registry_load(
                &f.registry, paths[i], &f.plugins[i]), CMETA_PLUGIN_OK);
            check_equal(cmeta_plugin_registry_start(
                &f.registry, f.plugins[i]), CMETA_PLUGIN_OK);
            check_equal(generation_build(
                &f, i, i, UINT64_C(11) + (uint64_t)i),
                SALTS_COMPONENT_PLUGIN_OK);
        }
        check_equal(salts_component_plugin_runtime_init(&f.runtime),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(scxml_compile(
            &f.program, source, sizeof(source)-1u, NULL, &diagnostic),
            SCXML_OK);

        /* gN publishes; its real Invoke callbacks run from DSO one. */
        check_equal(salts_component_plugin_runtime_publish(
            &f.runtime, &f.generations[0].generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_null(previous);
        check_equal(scxml_component_scope_acquire(
            &f.scopes[0], &f.runtime), SCXML_COMPONENT_OK);
        gen1 = scxml_component_scope_generation_id(&f.scopes[0]);
        check_equal(gen1, UINT64_C(11));
        check_equal(scxml_component_invoke_provider_bind(
            &f.invoke[0], &f.scopes[0], "ScxmlInvokeDsoFixture",
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL),
            SCXML_COMPONENT_OK);
        check_true(cflow_executor_serial_init(&f.executors[0]));
        f.executor_live[0] = true;
        {
            scxml_session_config conf = {
                .program = &f.program, .executor = &f.executors[0],
                .external_event_capacity = 2u,
                .internal_event_capacity = 2u,
                .completion_capacity = 2u,
                .microstep_limit = 16u,
                .invocation_capacity = 1u,
                .effect_capacity = 2u,
                .adapter_internal_event_capacity = 2u,
                .invoke = scxml_component_invoke_provider_adapter(&f.invoke[0]),
                .invoke_user =
                    scxml_component_invoke_provider_user(&f.invoke[0])
            };
            check_equal(scxml_session_init(&f.sessions[0], &conf),
                        CFLOW_STATECHART_INSTANCE_OK);
        }
        check_true(cflow_executor_wait_idle(&f.executors[0]));
        token1 = invoke_token_from_scope(&f.scopes[0]);
        check_true(token1 > 0);
        check_true(scxml_session_get_invoke_stats(&f.sessions[0], &invoke_stats));
        check_equal(invoke_stats.active, (size_t)1u);
        check_equal(invoke_stats.started, UINT64_C(1));

        /* One real listener and exact owner serialization for both generations. */
        check_equal(scxml_cnet_domain_fence_init(&f.fence), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_attach(&f.fence, gen1), SALTS_OK);
        check_equal(cnet_client_init(&f.sender, &net_config), SALTS_OK);
        check_equal(cnet_client_init(&f.receiver, &net_config), SALTS_OK);
        check_equal(cnet_listener_init(&f.listener, &listener_config), SALTS_OK);
        check_equal(cnet_listener_port(&f.listener, &port), SALTS_OK);
        check_true(port != 0u);
        check_true(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u",
                            (unsigned)port) > 0);
        options.uri = uri;
        options.observer = send_observer;
        check_equal(cnet_connect(&f.sender, &options, &f.outbound), SALTS_OK);
        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        while (!f.sender_probe.connected && cmeta_monotonic_ms() < deadline)
            check_equal(cnet_client_poll(&f.sender, 1u, &events), SALTS_OK);
        check_true(f.sender_probe.connected);
        check_equal(cnet_listener_wait(
            &f.listener, TIMEOUT_MS, &ready), SALTS_OK);
        check_equal(ready, 1);
        check_equal(cnet_listener_accept(
            &f.listener, &f.receiver, &recv_observer, &f.inbound), SALTS_OK);
        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        while (!f.receiver_probe.connected && cmeta_monotonic_ms() < deadline)
            check_equal(cnet_client_poll(&f.receiver, 1u, &events), SALTS_OK);
        check_true(f.receiver_probe.connected);
        check_equal(cnet_receive(&f.receiver, f.inbound, 2u), SALTS_OK);

        write_a = (fenced_cnet_write){&f, &a, 1u, f.outbound};
        check_equal(scxml_cnet_domain_fence_try_submit(
            &f.fence, gen1, send_one, &write_a), SALTS_OK);
        /* gN still holds a live Invoke even if its native send completes. */
        check_equal(scxml_cnet_domain_fence_retire(
            &f.fence, gen1, retire_real_generation, &old_probe), SALTS_EBUSY);

        /* Publish gN+1 while the original DSO-backed Invoke is active.
           That Invoke remains bound to its own gN Scope; no migration. */
        check_equal(salts_component_plugin_runtime_publish(
            &f.runtime, &f.generations[1].generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &f.generations[0].generation);
        check_equal(scxml_cnet_domain_fence_attach(
            &f.fence, UINT64_C(12)), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_activate(
            &f.fence, UINT64_C(12)), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_try_submit(
            &f.fence, gen1, send_one, &write_a), SALTS_EPERM);
        check_equal(scxml_component_scope_acquire(
            &f.scopes[1], &f.runtime), SCXML_COMPONENT_OK);
        gen2 = scxml_component_scope_generation_id(&f.scopes[1]);
        check_equal(gen2, UINT64_C(12));
        check_equal(scxml_component_invoke_provider_bind(
            &f.invoke[1], &f.scopes[1], "ScxmlInvokeDsoFixture",
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL),
            SCXML_COMPONENT_OK);
        check_true(cflow_executor_serial_init(&f.executors[1]));
        f.executor_live[1] = true;
        {
            scxml_session_config conf = {
                .program = &f.program, .executor = &f.executors[1],
                .external_event_capacity = 2u,
                .internal_event_capacity = 2u,
                .completion_capacity = 2u,
                .microstep_limit = 16u,
                .invocation_capacity = 1u,
                .effect_capacity = 2u,
                .adapter_internal_event_capacity = 2u,
                .invoke = scxml_component_invoke_provider_adapter(&f.invoke[1]),
                .invoke_user =
                    scxml_component_invoke_provider_user(&f.invoke[1])
            };
            check_equal(scxml_session_init(&f.sessions[1], &conf),
                        CFLOW_STATECHART_INSTANCE_OK);
        }
        check_true(cflow_executor_wait_idle(&f.executors[1]));
        token2 = invoke_token_from_scope(&f.scopes[1]);
        check_true(token2 > 0);
        check_true(scxml_session_get_invoke_stats(&f.sessions[1], &invoke_stats));
        check_equal(invoke_stats.active, (size_t)1u);
        check_equal(invoke_token_from_scope(&f.scopes[0]), token1);
        check_equal(salts_component_plugin_generation_drain(
            &f.runtime, &f.generations[0].generation),
            SALTS_COMPONENT_PLUGIN_BUSY);
        check_equal(cmeta_plugin_registry_unload(
            &f.registry, f.plugins[0]), CMETA_PLUGIN_BUSY);

        write_b = (fenced_cnet_write){&f, &b, 1u, f.outbound};
        check_equal(scxml_cnet_domain_fence_try_submit(
            &f.fence, gen2, send_one, &write_b), SALTS_OK);
        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        while ((f.sender_probe.sends_done < 2u ||
                f.receiver_probe.received < 2u) &&
               cmeta_monotonic_ms() < deadline) {
            check_equal(cnet_client_poll(&f.sender, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&f.receiver, 1u, &events), SALTS_OK);
        }
        check_equal(f.sender_probe.sends_done, (size_t)2u);
        check_equal(f.sender_probe.bytes_completed, (size_t)2u);
        check_equal(f.receiver_probe.received, (size_t)2u);
        check_false(f.receiver_probe.failed);
        check_true(memcmp(f.receiver_probe.bytes, "AB", 2u) == 0);
        /* CNet is terminal for both sends, but gN cannot retire while its
           DSO-backed Invoke/ComponentScope still borrows the old Plugin. */
        check_equal(scxml_cnet_domain_fence_retire(
            &f.fence, gen1, retire_real_generation, &old_probe), SALTS_EBUSY);
        check_true(old_probe.saw_native_terminal);
        check_true(old_probe.saw_scope_busy);
        check_equal(scxml_cnet_domain_fence_attach(
            &f.fence, UINT64_C(13)), SALTS_EBUSY);

        check_equal(scxml_session_report_invoke_done(
            &f.sessions[0], (uint64_t)token1), CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&f.executors[0]));
        check_equal(scxml_session_report_invoke_done(
            &f.sessions[0], (uint64_t)token1),
            CFLOW_MAILBOX_INVALID_ARGUMENT);
        check_equal(scxml_session_report_invoke_done(
            &f.sessions[0], (uint64_t)token1 + UINT64_C(10000)),
            CFLOW_MAILBOX_INVALID_ARGUMENT);
        check_true(scxml_session_get_stats(&f.sessions[0], &session_stats));
        check_true(session_stats.done);
        check_false(session_stats.errored);
        check_equal(scxml_session_destroy(&f.sessions[0]),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_component_invoke_provider_destroy(&f.invoke[0]),
                    SCXML_COMPONENT_OK);
        check_equal(scxml_component_scope_release(&f.scopes[0]),
                    SCXML_COMPONENT_OK);
        check_equal(scxml_cnet_domain_fence_retire(
            &f.fence, gen1, retire_real_generation, &old_probe), SALTS_OK);
        check_true(scxml_cnet_domain_fence_get_stats(&f.fence, &fence_stats));
        check_equal(fence_stats.draining_generation, UINT64_C(0));
        check_equal(fence_stats.current_generation, gen2);
        check_equal(fence_stats.attached, (size_t)1u);
        check_equal(cmeta_plugin_registry_request_stop(
            &f.registry, f.plugins[0]), CMETA_PLUGIN_OK);
        {
            bool quiescent = false;
            check_equal(cmeta_plugin_registry_poll_quiescent(
                &f.registry, f.plugins[0], &quiescent), CMETA_PLUGIN_OK);
            check_true(quiescent);
        }
        check_equal(cmeta_plugin_registry_unload(
            &f.registry, f.plugins[0]), CMETA_PLUGIN_OK);
        f.plugins[0] = (cmeta_plugin_ref){0};

        /* The reverse ACT race: admit another real CNet operation under gN+1,
           then cancel its Invoke before polling the native completion. The
           original Component Scope must remain retained until that callback. */
        check_equal(cnet_receive(&f.receiver, f.inbound, 1u), SALTS_OK);
        write_c = (fenced_cnet_write){&f, &c, 1u, f.outbound};
        check_equal(scxml_cnet_domain_fence_try_submit(
            &f.fence, gen2, send_one, &write_c), SALTS_OK);
        check_equal(f.sender_probe.sends_done, (size_t)2u);
        check_true(signal(&f, 1u, "finish"));
        check_true(scxml_session_get_stats(&f.sessions[1], &session_stats));
        check_true(session_stats.done);
        check_false(session_stats.errored);
        check_equal(scxml_session_report_invoke_done(
            &f.sessions[1], (uint64_t)token2),
            CFLOW_MAILBOX_INVALID_ARGUMENT);
        /* Onexit Invoke cancellation is a DSO callback, not a fabricated
           "done.invoke" or a CNet native terminal. */
        check_equal(invoke_token_from_scope(&f.scopes[1]), -token2);
        check_equal(scxml_session_destroy(&f.sessions[1]),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_component_invoke_provider_destroy(&f.invoke[1]),
                    SCXML_COMPONENT_OK);
        check_equal(salts_component_plugin_runtime_close(
            &f.runtime, &previous), SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &f.generations[1].generation);
        check_equal(scxml_cnet_domain_fence_close(&f.fence), SALTS_OK);
        check_equal(salts_component_plugin_generation_drain(
            &f.runtime, &f.generations[1].generation),
            SALTS_COMPONENT_PLUGIN_BUSY);
        check_equal(scxml_cnet_domain_fence_retire(
            &f.fence, gen2, retire_real_generation, &next_probe),
            SALTS_EBUSY);
        check_false(next_probe.saw_native_terminal);
        check_equal(cmeta_plugin_registry_unload(
            &f.registry, f.plugins[1]), CMETA_PLUGIN_BUSY);

        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        while ((f.sender_probe.sends_done < 3u ||
                f.receiver_probe.received < 3u) &&
               cmeta_monotonic_ms() < deadline) {
            check_equal(cnet_client_poll(&f.sender, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&f.receiver, 1u, &events), SALTS_OK);
        }
        check_equal(f.sender_probe.sends_done, (size_t)3u);
        check_equal(f.receiver_probe.received, (size_t)3u);
        check_true(memcmp(f.receiver_probe.bytes, "ABC", 3u) == 0);
        check_false(f.receiver_probe.failed);
        /* Nativeio delivery is now terminal, but the DSO Scope is still live:
           the generation MUST remain BUSY until that authoritative lease
           retires, independent from Session Invocation completion. */
        check_equal(scxml_cnet_domain_fence_retire(
            &f.fence, gen2, retire_real_generation, &next_probe),
            SALTS_EBUSY);
        check_true(next_probe.saw_native_terminal);
        check_true(next_probe.saw_scope_busy);
        check_equal(scxml_component_scope_release(&f.scopes[1]),
                    SCXML_COMPONENT_OK);
        check_equal(scxml_cnet_domain_fence_retire(
            &f.fence, gen2, retire_real_generation, &next_probe),
            SALTS_OK);
        check_equal(scxml_cnet_domain_fence_destroy(&f.fence), SALTS_OK);
    }
}
