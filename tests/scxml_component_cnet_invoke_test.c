#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
/* Windows RPC/COM headers reserve `interface` as a macro, while CMeta
   deliberately has an Interface descriptor field with that C identifier.
   Keep WinSock types available without letting this macro rewrite CMeta. */
#ifdef interface
#undef interface
#endif
#else
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <scxml/component.h>
#include <scxml/cnet_domain_fence.h>
#include "scxml_component_invoke_probe.h"

#include <cnet/cnet.h>
#include <salts/clock.h>
#include <salts/native_io_ace_token.h>
#include <salts/plugin.h>
#include <salts/thread.h>
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

/* Native ACT and DSO Invoke use one real NativeIO owner in the test below.
 * This helper only creates the peer's connected kernel sockets. */
#if defined(_WIN32)
typedef SOCKET scxml_act_socket;
#define SCXML_ACT_INVALID_SOCKET INVALID_SOCKET
#else
typedef int scxml_act_socket;
#define SCXML_ACT_INVALID_SOCKET (-1)
#endif

typedef struct scxml_invoke_native_context {
    scxml_component_scope *scope;
    uint64_t generation;
    uintptr_t invoke_token;
    unsigned char *borrowed_buffer;
    size_t settlements;
} scxml_invoke_native_context;

NATIVE_IO_ACE_TOKEN_TYPE(scxml_invoke_native_act, scxml_invoke_native_context);

static int scxml_act_make_socket_pair(scxml_act_socket pair[2]) {
#if defined(_WIN32)
    WSADATA wsa = {0};
    SOCKET listener = INVALID_SOCKET;
    struct sockaddr_in address = {0};
    int address_length = (int)sizeof(address);
    u_long nonblocking = 1u;
    pair[0] = INVALID_SOCKET;
    pair[1] = INVALID_SOCKET;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
    listener = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0u,
                          WSA_FLAG_OVERLAPPED);
    if (listener == INVALID_SOCKET) goto failed;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, (const struct sockaddr *)&address,
             (int)sizeof(address)) != 0 || listen(listener, 1) != 0 ||
        getsockname(listener, (struct sockaddr *)&address,
                    &address_length) != 0)
        goto failed;
    pair[1] = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0u,
                         WSA_FLAG_OVERLAPPED);
    if (pair[1] == INVALID_SOCKET ||
        connect(pair[1], (const struct sockaddr *)&address,
                (int)sizeof(address)) != 0)
        goto failed;
    pair[0] = accept(listener, NULL, NULL);
    if (pair[0] == INVALID_SOCKET ||
        ioctlsocket(pair[0], FIONBIO, &nonblocking) != 0 ||
        ioctlsocket(pair[1], FIONBIO, &nonblocking) != 0)
        goto failed;
    (void)closesocket(listener);
    return 0;
failed:
    if (listener != INVALID_SOCKET) (void)closesocket(listener);
    if (pair[0] != INVALID_SOCKET) (void)closesocket(pair[0]);
    if (pair[1] != INVALID_SOCKET) (void)closesocket(pair[1]);
    pair[0] = INVALID_SOCKET;
    pair[1] = INVALID_SOCKET;
    (void)WSACleanup();
    return -1;
#else
    int flags;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) return -1;
    for (size_t index = 0u; index < 2u; ++index) {
        flags = fcntl(pair[index], F_GETFL, 0);
        if (flags < 0 ||
            fcntl(pair[index], F_SETFL, flags | O_NONBLOCK) != 0) {
            (void)close(pair[0]);
            (void)close(pair[1]);
            pair[0] = -1;
            pair[1] = -1;
            return -1;
        }
    }
    return 0;
#endif
}

static int scxml_act_close_socket(scxml_act_socket value) {
#if defined(_WIN32)
    return closesocket(value);
#else
    return close(value);
#endif
}

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

static salts_component_plugin_status generation_build_export(
    joint_fixture *fixture, size_t slot, size_t plugin_index,
    uint64_t generation_id, const char *export_id) {
    invoke_dso_generation *g = &fixture->generations[slot];
    const salts_component_plugin_generation_storage storage = {
        g->deployments, 1u, g->instances, 1u, g->dependencies, 1u,
        g->activation_order, 1u, g->modules, 1u
    };
    const salts_component_plugin_source source = {
        fixture->plugins[plugin_index], export_id, NULL, NULL
    };
    return salts_component_plugin_generation_build(
        &g->generation, generation_id, &fixture->registry, &storage,
        NULL, 0u, &source, 1u, NULL, 0u);
}

static salts_component_plugin_status generation_build(
    joint_fixture *fixture, size_t slot, size_t plugin_index,
    uint64_t generation_id) {
    return generation_build_export(
        fixture, slot, plugin_index, generation_id, "component-provider");
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
    scxml_test_cnet_observer_stats *out) {
    return adapter != NULL && scxml_test_cnet_probe_valid(adapter) &&
        out != NULL && scxml_test_cnet_probe_snapshot(adapter, out);
}

static bool retire_native_dso_generation(void *user) {
    joint_dso_native_observer *probe = (joint_dso_native_observer *)user;
    scxml_test_cnet_observer_stats state = {0};
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

typedef struct dso_concurrent_unload {
    cmeta_plugin_registry *registry;
    cmeta_plugin_ref plugin;
    scxml_test_cnet_callback_gate *gate;
    cmeta_plugin_status status;
    bool reached_callback;
} dso_concurrent_unload;

static void callback_gate_init(scxml_test_cnet_callback_gate *gate) {
    atomic_init(&gate->entered, 0);
    atomic_init(&gate->release, 0);
    atomic_init(&gate->timed_out, 0);
}

/* Runs concurrently with *actual DSO instructions* inside the CNet owner's
 * poll callback. Never touches the owner-only CNet/Invoke/Scope objects;
 * the registry itself serializes unload/lease admission. */
static void foreign_unload_during_callback(void *user) {
    dso_concurrent_unload *race = (dso_concurrent_unload *)user;
    const uint64_t deadline = cmeta_monotonic_ms() + UINT64_C(10000);
    while (!atomic_load_explicit(&race->gate->entered, memory_order_acquire) &&
           cmeta_monotonic_ms() < deadline) {
    }
    race->reached_callback =
        atomic_load_explicit(&race->gate->entered, memory_order_acquire) != 0;
    if (race->reached_callback)
        race->status = cmeta_plugin_registry_unload(
            race->registry, race->plugin);
    atomic_store_explicit(&race->gate->release, 1, memory_order_release);
}

typedef struct invoke_native_overlap {
    scxml_test_cnet_callback_gate *cancel_gate;
    scxml_test_cnet_callback_gate *send_gate;
    bool both_entered;
} invoke_native_overlap;

/* CFlow's SerialExecutor and CNet's poll owner execute different actual DSO
 * callback instructions. They must BOTH enter before either is released. */
static void release_simultaneous_invoke_native(void *user) {
    invoke_native_overlap *race = (invoke_native_overlap *)user;
    const uint64_t deadline = cmeta_monotonic_ms() + UINT64_C(10000);
    while ((!atomic_load_explicit(&race->cancel_gate->entered,
                                   memory_order_acquire) ||
            !atomic_load_explicit(&race->send_gate->entered,
                                   memory_order_acquire)) &&
           cmeta_monotonic_ms() < deadline) {
    }
    race->both_entered =
        atomic_load_explicit(&race->cancel_gate->entered,
                             memory_order_acquire) != 0 &&
        atomic_load_explicit(&race->send_gate->entered,
                             memory_order_acquire) != 0;
    atomic_store_explicit(&race->send_gate->release, 1,
                          memory_order_release);
    atomic_store_explicit(&race->cancel_gate->release, 1,
                          memory_order_release);
}

typedef struct dso_domain_restart_attempt {
    scxml_cnet_domain_fence *fence;
    cmeta_plugin_registry *registry;
    cmeta_plugin_ref plugin;
    scxml_test_cnet_callback_gate *gate;
    int close_status;
    int destroy_status;
    cmeta_plugin_status unload_status;
    bool callback_entered;
} dso_domain_restart_attempt;

/* The foreign lane NEVER assumes ownership of CNet or this domain. It
 * deliberately attempts forbidden close/destroy while actual DSO on_send
 * instructions are executing, and asks the authoritative Plugin registry
 * to unload. All three must reject. */
static void foreign_domain_restart_during_dso_send(void *user) {
    dso_domain_restart_attempt *race =
        (dso_domain_restart_attempt *)user;
    const uint64_t deadline = cmeta_monotonic_ms() + UINT64_C(10000);
    while (!atomic_load_explicit(
               &race->gate->entered, memory_order_acquire) &&
           cmeta_monotonic_ms() < deadline) {
    }
    race->callback_entered = atomic_load_explicit(
        &race->gate->entered, memory_order_acquire) != 0;
    if (race->callback_entered) {
        race->close_status = scxml_cnet_domain_fence_close(race->fence);
        race->destroy_status = scxml_cnet_domain_fence_destroy(race->fence);
        race->unload_status =
            cmeta_plugin_registry_unload(race->registry, race->plugin);
    }
    atomic_store_explicit(&race->gate->release, 1, memory_order_release);
}

static bool restart_invoke_session(joint_fixture *f, size_t slot) {
    scxml_session_config conf = {0};
    if (f == NULL || slot >= SESSIONS ||
        scxml_component_invoke_provider_bind(
            &f->invoke[slot], &f->scopes[slot], "ScxmlInvokeDsoFixture",
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL) !=
            SCXML_COMPONENT_OK ||
        !cflow_executor_serial_init(&f->executors[slot]))
        return false;
    f->executor_live[slot] = true;
    conf = (scxml_session_config){
        .program = &f->program, .executor = &f->executors[slot],
        .external_event_capacity = 2u, .internal_event_capacity = 2u,
        .completion_capacity = 2u, .microstep_limit = 16u,
        .invocation_capacity = 1u, .effect_capacity = 2u,
        .adapter_internal_event_capacity = 2u,
        .invoke = scxml_component_invoke_provider_adapter(&f->invoke[slot]),
        .invoke_user = scxml_component_invoke_provider_user(&f->invoke[slot])
    };
    return scxml_session_init(&f->sessions[slot], &conf) ==
            CFLOW_STATECHART_INSTANCE_OK &&
        cflow_executor_wait_idle(&f->executors[slot]);
}

static bool session_send_signal(joint_fixture *fixture, size_t slot, const char *name) {
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
        check_true(session_send_signal(&f, 1u, "finish"));
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

    it("keeps real DSO CNet callback code loaded through on_send/terminal after N-to-N+1") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "version='1.0' initial='working'>"
            "<state id='working'>"
            "<invoke id='worker' type='urn:test:invoke' src='worker'/>"
            "<transition event='done.invoke.worker' target='done'/>"
            "<transition event='finish' target='done'/>"
            "</state><final id='done'/></scxml>";
        const char *paths[] = { SCXML_INVOKE_DSO_ONE, SCXML_INVOKE_DSO_TWO };
        const cmeta_plugin_registry_config registry_conf = { .capacity = 2u };
        const cnet_client_config net_conf = cnet_config();
        const cnet_listener_config listener_conf = {
            .backend = backend(), .host = "127.0.0.1",
            .port = 0u, .backlog = 2u
        };
        cnet_observer sender_observers[SESSIONS] = {{0}, {0}};
        cnet_observer recv_observers[SESSIONS] = {
            { .on_state = receiver_state, .on_receive = receiver_data,
              .user = &f.receiver_probe },
            { .on_state = receiver_state, .on_receive = receiver_data,
              .user = &f.receiver_probe_next }
        };
        scxml_test_cnet_probe probes[SESSIONS] = {{0}, {0}};
        scxml_test_cnet_observer_stats native[SESSIONS] = {{0}, {0}};
        joint_dso_native_observer draining[SESSIONS] = {
            { .fixture = &f, .generation_slot = 0u, .expected_sends = 1u },
            { .fixture = &f, .generation_slot = 1u, .expected_sends = 2u }
        };
        salts_component_plugin_generation *previous = NULL;
        scxml_diagnostic diagnostic = {0};
        cnet_connect_options options = {0};
        scxml_cnet_domain_fence_stats fence_stats = {0};
        cflow_statechart_instance_stats session_stats = {0};
        uint64_t deadline;
        uint64_t gen[SESSIONS] = {0u, 0u};
        uint16_t port = 0u;
        int token[SESSIONS] = {0, 0}, ready = 0;
        size_t events = 0u;
        char uri[64];
        const unsigned char a = 'A', b = 'B', c = 'C';
        fenced_cnet_write write_a, write_b, write_c;
        salts_component_plugin_generation *rejected_previous = NULL;
        scxml_component_scope unchanged_scope = {0};
        scxml_test_cnet_callback_gate send_gate, terminal_gate;
        scxml_test_cnet_callback_gate overlap_send_gate, cancel_gate;
        invoke_native_overlap overlap = {0};
        cmeta_thread_t overlap_thread = NULL;
        const scxml_event_metadata cancel_metadata = {
            .abi_version = SCXML_EVENT_METADATA_ABI,
            .struct_size = sizeof(scxml_event_metadata)
        };
        dso_concurrent_unload send_race = {0}, terminal_race = {0};
        cmeta_thread_t send_thread = NULL, terminal_thread = NULL;

        callback_gate_init(&send_gate);
        callback_gate_init(&terminal_gate);
        callback_gate_init(&overlap_send_gate);
        callback_gate_init(&cancel_gate);
        check_equal(cmeta_plugin_registry_init(&f.registry, &registry_conf),
                    CMETA_PLUGIN_OK);
        f.registry_live = true;
        for (size_t i = 0u; i < SESSIONS; ++i) {
            check_equal(cmeta_plugin_registry_load(
                &f.registry, paths[i], &f.plugins[i]), CMETA_PLUGIN_OK);
            check_equal(cmeta_plugin_registry_start(
                &f.registry, f.plugins[i]), CMETA_PLUGIN_OK);
            check_equal(generation_build(&f, i, i, UINT64_C(11) + (uint64_t)i),
                        SALTS_COMPONENT_PLUGIN_OK);
        }
        check_equal(salts_component_plugin_runtime_init(&f.runtime),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(scxml_compile(&f.program, source, sizeof(source)-1u,
                                  NULL, &diagnostic), SCXML_OK);
        check_equal(scxml_cnet_domain_fence_init(&f.fence), SALTS_OK);
        check_equal(cnet_client_init(&f.sender, &net_conf), SALTS_OK);
        check_equal(cnet_client_init(&f.receiver, &net_conf), SALTS_OK);
        check_equal(cnet_listener_init(&f.listener, &listener_conf), SALTS_OK);
        check_equal(cnet_listener_port(&f.listener, &port), SALTS_OK);
        check_true(port > 0u);
        check_true(snprintf(uri, sizeof(uri),
            "tcp://127.0.0.1:%u", (unsigned)port) > 0);
        options.uri = uri;

        /* Publish N; its Invoke and CNet observer callbacks are both code
           pointers into the first real ComponentPlugin-loaded DSO. */
        check_equal(salts_component_plugin_runtime_publish(
            &f.runtime, &f.generations[0].generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_null(previous);
        check_equal(scxml_component_scope_acquire(
            &f.scopes[0], &f.runtime), SCXML_COMPONENT_OK);
        gen[0] = scxml_component_scope_generation_id(&f.scopes[0]);
        check_equal(gen[0], UINT64_C(11));
        check_equal(scxml_component_invoke_provider_bind(
            &f.invoke[0], &f.scopes[0], "ScxmlInvokeDsoFixture",
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL),
            SCXML_COMPONENT_OK);
        check_true(dso_native_observer_from_scope(&f.scopes[0], &probes[0]));
        check_true(scxml_test_cnet_probe_get_observer(
            &probes[0], &sender_observers[0]));
        check_not_null(sender_observers[0].on_send);
        check_not_null(sender_observers[0].on_state);
        check_true(cflow_executor_serial_init(&f.executors[0]));
        f.executor_live[0] = true;
        {
            scxml_session_config conf = {
                .program = &f.program, .executor = &f.executors[0],
                .external_event_capacity = 2u, .internal_event_capacity = 2u,
                .completion_capacity = 2u, .microstep_limit = 16u,
                .invocation_capacity = 1u, .effect_capacity = 2u,
                .adapter_internal_event_capacity = 2u,
                .invoke = scxml_component_invoke_provider_adapter(&f.invoke[0]),
                .invoke_user = scxml_component_invoke_provider_user(&f.invoke[0])
            };
            check_equal(scxml_session_init(&f.sessions[0], &conf),
                        CFLOW_STATECHART_INSTANCE_OK);
        }
        check_true(cflow_executor_wait_idle(&f.executors[0]));
        token[0] = invoke_token_from_scope(&f.scopes[0]);
        check_true(token[0] > 0);
        check_equal(scxml_cnet_domain_fence_attach(&f.fence, gen[0]), SALTS_OK);

        options.observer = sender_observers[0];
        check_equal(cnet_connect(&f.sender, &options, &f.outbound), SALTS_OK);
        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        do {
            check_equal(cnet_client_poll(&f.sender, 1u, &events), SALTS_OK);
            check_true(native_dso_snapshot(&probes[0], &native[0]));
        } while (!native[0].native_connected &&
                 cmeta_monotonic_ms() < deadline);
        check_true(native[0].native_connected);
        check_equal(cnet_listener_wait(&f.listener, TIMEOUT_MS, &ready),
                    SALTS_OK);
        check_equal(ready, 1);
        check_equal(cnet_listener_accept(&f.listener, &f.receiver,
            &recv_observers[0], &f.inbound), SALTS_OK);
        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        while (!f.receiver_probe.connected && cmeta_monotonic_ms() < deadline)
            check_equal(cnet_client_poll(&f.receiver, 1u, &events), SALTS_OK);
        check_true(f.receiver_probe.connected);
        check_equal(cnet_receive(&f.receiver, f.inbound, 1u), SALTS_OK);
        /* Arm before native send admission: later owner polls may deliver
           on_send while building/connecting the next generation. */
        check_true(scxml_test_cnet_probe_arm_send_gate(
            &probes[0], &send_gate));
        write_a = (fenced_cnet_write){&f, &a, 1u, f.outbound};
        check_equal(scxml_cnet_domain_fence_try_submit(
            &f.fence, gen[0], send_one, &write_a), SALTS_OK);
        /* This native send has been accepted but its DSO on_send callback
           has NOT yet run. The old Component Scope must remain pinned. */
        check_true(native_dso_snapshot(&probes[0], &native[0]));
        check_equal(native[0].native_sends, (size_t)0u);

        check_equal(salts_component_plugin_runtime_publish(
            &f.runtime, &f.generations[1].generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &f.generations[0].generation);
        check_equal(scxml_cnet_domain_fence_attach(
            &f.fence, UINT64_C(12)), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_activate(
            &f.fence, UINT64_C(12)), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_try_submit(
            &f.fence, gen[0], send_one, &write_a), SALTS_EPERM);
        check_equal(cmeta_plugin_registry_unload(
            &f.registry, f.plugins[0]), CMETA_PLUGIN_BUSY);
        check_equal(salts_component_plugin_generation_drain(
            &f.runtime, &f.generations[0].generation),
            SALTS_COMPONENT_PLUGIN_BUSY);

        /* New generation has an independent real Invoke/Scope, its own
           DSO-resident CNet callback, a *different connection*, but the
           SAME listener and same native owner (no listener rebind). */
        check_equal(scxml_component_scope_acquire(
            &f.scopes[1], &f.runtime), SCXML_COMPONENT_OK);
        gen[1] = scxml_component_scope_generation_id(&f.scopes[1]);
        check_equal(gen[1], UINT64_C(12));
        check_equal(scxml_component_invoke_provider_bind(
            &f.invoke[1], &f.scopes[1], "ScxmlInvokeDsoFixture",
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL),
            SCXML_COMPONENT_OK);
        check_true(dso_native_observer_from_scope(&f.scopes[1], &probes[1]));
        check_true(scxml_test_cnet_probe_get_observer(
            &probes[1], &sender_observers[1]));
        check_true(cflow_executor_serial_init(&f.executors[1]));
        f.executor_live[1] = true;
        {
            scxml_session_config conf = {
                .program = &f.program, .executor = &f.executors[1],
                .external_event_capacity = 2u, .internal_event_capacity = 2u,
                .completion_capacity = 2u, .microstep_limit = 16u,
                .invocation_capacity = 1u, .effect_capacity = 2u,
                .adapter_internal_event_capacity = 2u,
                .invoke = scxml_component_invoke_provider_adapter(&f.invoke[1]),
                .invoke_user = scxml_component_invoke_provider_user(&f.invoke[1])
            };
            check_equal(scxml_session_init(&f.sessions[1], &conf),
                        CFLOW_STATECHART_INSTANCE_OK);
        }
        check_true(cflow_executor_wait_idle(&f.executors[1]));
        token[1] = invoke_token_from_scope(&f.scopes[1]);
        check_true(token[1] > 0);

        /* Race registry unload against live gN DSO on_send, while its Scope
           and old NativeIO request are still pinned through publication. */
        send_race.registry = &f.registry;
        send_race.plugin = f.plugins[0];
        send_race.gate = &send_gate;
        check_equal(cmeta_thread_create(
            &send_thread, foreign_unload_during_callback, &send_race),
            SALTS_OK);
        options.observer = sender_observers[1];
        check_equal(cnet_connect(&f.sender, &options, &f.outbound_next), SALTS_OK);
        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        do {
            check_equal(cnet_client_poll(&f.sender, 1u, &events), SALTS_OK);
            check_true(native_dso_snapshot(&probes[1], &native[1]));
        } while (!native[1].native_connected &&
                 cmeta_monotonic_ms() < deadline);
        check_true(native[1].native_connected);
        /* Connection callbacks and the old send may arrive in either order.
           Keep polling the authoritative owner until the DSO entry barrier
           actually fired; joining early would accidentally skip the race. */
        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        while (!atomic_load_explicit(
                   &send_gate.entered, memory_order_acquire) &&
               cmeta_monotonic_ms() < deadline)
            check_equal(cnet_client_poll(&f.sender, 1u, &events), SALTS_OK);
        check_equal(cmeta_thread_join(&send_thread), SALTS_OK);
        cmeta_thread_destroy(&send_thread);
        check_true(send_race.reached_callback);
        check_equal(send_race.status, CMETA_PLUGIN_BUSY);
        check_equal(atomic_load_explicit(
            &send_gate.timed_out, memory_order_acquire), 0);
        check_equal(cnet_listener_wait(&f.listener, TIMEOUT_MS, &ready),
                    SALTS_OK);
        check_equal(ready, 1);
        check_equal(cnet_listener_accept(&f.listener, &f.receiver,
            &recv_observers[1], &f.inbound_next), SALTS_OK);
        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        while (!f.receiver_probe_next.connected &&
               cmeta_monotonic_ms() < deadline)
            check_equal(cnet_client_poll(&f.receiver, 1u, &events), SALTS_OK);
        check_true(f.receiver_probe_next.connected);
        check_equal(cnet_receive(&f.receiver, f.inbound_next, 1u), SALTS_OK);
        write_b = (fenced_cnet_write){&f, &b, 1u, f.outbound_next};
        check_equal(scxml_cnet_domain_fence_try_submit(
            &f.fence, gen[1], send_one, &write_b), SALTS_OK);

        /* Exercise an actual provider-export activation failure, not just
           a domain-fence ENOENT. ComponentPlugin acquires the live module,
           discovers the missing export, then releases its candidate lease.
           Neither the failed candidate nor its rejected publish may replace
           gN+1 or touch the native send already accepted above. */
        check_equal(generation_build_export(
            &f, 2u, 1u, gen[1] + UINT64_C(1),
            "missing-invoke-provider-export"), SALTS_COMPONENT_PLUGIN_PLUGIN_ERROR);
        check_equal(f.generations[2].generation.state,
                    SALTS_COMPONENT_PLUGIN_GENERATION_FAILED);
        check_equal(f.generations[2].generation.module_count, (size_t)0u);
        check_equal(salts_component_plugin_runtime_publish(
            &f.runtime, &f.generations[2].generation, &rejected_previous),
            SALTS_COMPONENT_PLUGIN_INVALID_STATE);
        check_null(rejected_previous);
        check_equal(invoke_token_from_scope(&f.scopes[1]), token[1]);
        check_equal(cmeta_plugin_registry_unload(
            &f.registry, f.plugins[1]), CMETA_PLUGIN_BUSY);

        /* With both real ComponentPlugin generations attached, the old gN
           still owns a live Invoke and DSO callback, and the new gN+1 has
           accepted an actual CNet send. A *built* third-generation candidate
           must be rejected by the authoritative runtime publication gate,
           not merely by a test-local epoch check. Failure must leave gN+1
           current and must not settle or migrate either native operation. */
        check_equal(generation_build(
            &f, 2u, 1u, gen[1] + UINT64_C(1)), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_publish(
            &f.runtime, &f.generations[2].generation, &rejected_previous),
            SALTS_COMPONENT_PLUGIN_BUSY);
        check_null(rejected_previous);
        check_equal(f.generations[2].generation.state,
                    SALTS_COMPONENT_PLUGIN_GENERATION_BUILT);
        check_equal(scxml_component_scope_acquire(
            &unchanged_scope, &f.runtime), SCXML_COMPONENT_OK);
        check_equal(scxml_component_scope_generation_id(&unchanged_scope),
                    gen[1]);
        check_equal(scxml_component_scope_release(
            &unchanged_scope), SCXML_COMPONENT_OK);
        /* The real rejected candidate must discard its provider/module
           lease explicitly; it never obtains a CNet listener or replaces
           the live gN+1 Invoke. No automatic publish retry or fallback. */
        check_equal(salts_component_plugin_generation_discard(
            &f.generations[2].generation), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_unload(
            &f.registry, f.plugins[1]), CMETA_PLUGIN_BUSY);
        check_equal(invoke_token_from_scope(&f.scopes[1]), token[1]);

        /* A copied Scope is not an owning lease: its address-stable Salts
           owner identity still points at the original object. Neither the
           copy's release attempt nor a legitimate release while the Invoke
           adapter remains bound may decrement the real generation lease. */
        {
            scxml_component_scope copied_scope = f.scopes[1];
            const size_t generation_before =
                f.generations[1].generation.active_scopes;
            const size_t runtime_before = f.runtime.active_scopes;
            check_true(generation_before > 0u);
            check_true(runtime_before >= generation_before);
            check_equal(scxml_component_scope_generation_id(
                &copied_scope), UINT64_C(0));
            check_equal(scxml_component_scope_release(
                &copied_scope), SCXML_COMPONENT_INVALID_ARGUMENT);
            check_equal(scxml_component_scope_release(
                &f.scopes[1]), SCXML_COMPONENT_BUSY);
            check_equal(f.generations[1].generation.active_scopes,
                        generation_before);
            check_equal(f.runtime.active_scopes, runtime_before);
            check_equal(scxml_component_scope_generation_id(
                &f.scopes[1]), gen[1]);
            check_equal(cmeta_plugin_registry_unload(
                &f.registry, f.plugins[1]), CMETA_PLUGIN_BUSY);
        }

        /* Poll both CNet connections through the same owner. Each callback
           executes inside its own DSO, not in this executable. */
        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        while (cmeta_monotonic_ms() < deadline) {
            check_equal(cnet_client_poll(&f.sender, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&f.receiver, 1u, &events), SALTS_OK);
            check_true(native_dso_snapshot(&probes[0], &native[0]));
            check_true(native_dso_snapshot(&probes[1], &native[1]));
            if (native[0].native_sends >= 1u &&
                native[1].native_sends >= 1u &&
                f.receiver_probe.received >= 1u &&
                f.receiver_probe_next.received >= 1u) break;
        }
        check_equal(native[0].native_sends, (size_t)1u);
        check_equal(native[1].native_sends, (size_t)1u);
        check_equal(native[0].native_send_bytes, (size_t)1u);
        check_equal(native[1].native_send_bytes, (size_t)1u);
        check_equal(native[0].stale_callbacks, (size_t)0u);
        check_equal(native[1].stale_callbacks, (size_t)0u);
        check_equal(f.receiver_probe.received, (size_t)1u);
        check_equal(f.receiver_probe_next.received, (size_t)1u);
        check_equal(f.receiver_probe.bytes[0], (unsigned char)'A');
        check_equal(f.receiver_probe_next.bytes[0], (unsigned char)'B');

        /* Unloading old DSO after only on_send is still invalid: its own
           on_state(CLOSED) code has not executed yet. */
        draining[0].adapter = probes[0];
        draining[1].adapter = probes[1];
        check_equal(scxml_cnet_domain_fence_retire(
            &f.fence, gen[0], retire_native_dso_generation, &draining[0]),
            SALTS_EBUSY);
        check_false(draining[0].saw_native_terminal);
        check_equal(cmeta_plugin_registry_unload(
            &f.registry, f.plugins[0]), CMETA_PLUGIN_BUSY);
        check_equal(cnet_close(&f.sender, f.outbound), SALTS_OK);
        check_equal(cnet_close(&f.receiver, f.inbound), SALTS_OK);
        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        do {
            check_equal(cnet_client_poll(&f.sender, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&f.receiver, 1u, &events), SALTS_OK);
            check_true(native_dso_snapshot(&probes[0], &native[0]));
        } while (!native[0].native_terminal &&
                 cmeta_monotonic_ms() < deadline);
        check_true(native[0].native_terminal);
        check_equal(scxml_cnet_domain_fence_retire(
            &f.fence, gen[0], retire_native_dso_generation, &draining[0]),
            SALTS_EBUSY);
        check_true(draining[0].saw_native_terminal);
        check_true(draining[0].saw_scope_busy);
        check_equal(salts_component_plugin_generation_drain(
            &f.runtime, &f.generations[0].generation),
            SALTS_COMPONENT_PLUGIN_BUSY);

        check_equal(scxml_session_report_invoke_done(
            &f.sessions[0], (uint64_t)token[0]), CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&f.executors[0]));
        check_equal(scxml_session_report_invoke_done(
            &f.sessions[0], (uint64_t)token[0]),
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
        /* Native CLOSED and Plugin Scope release are both authoritative
           before the DSO callback instructions can be unloaded. */
        check_equal(scxml_cnet_domain_fence_retire(
            &f.fence, gen[0], retire_native_dso_generation, &draining[0]),
            SALTS_OK);
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
        f.outbound = (cnet_connection){0};
        f.inbound = (cnet_connection){0};

        /* Old DSO is GONE, while N+1 Invoke and DSO CNet observer are still
           live. A subsequent N+1 command must continue to work. */
        check_equal(scxml_cnet_domain_fence_get_stats(&f.fence, &fence_stats),
                    true);
        check_equal(fence_stats.current_generation, gen[1]);
        check_equal(fence_stats.draining_generation, UINT64_C(0));

        /* Build an actual ComponentPlugin candidate from the live DSO
           before attempting to activate its exclusive CNet capability.
           The candidate is never published after failed domain admission;
           it must be discarded without replacing or draining gN+1. */
        check_equal(generation_build(
            &f, 2u, 1u, gen[1] + UINT64_C(1)), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(invoke_token_from_scope(&f.scopes[1]), token[1]);

        /* A failed EXCLUSIVE-I/O candidate activation must never displace
           the still-live gN+1 Invoke/DSO and its real CNet connection.
           Roll back the staged domain admission, not the current provider. */
        check_equal(scxml_cnet_domain_fence_attach(
            &f.fence, gen[1] + UINT64_C(1)), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_activate(
            &f.fence, gen[1] + UINT64_C(2)), SALTS_ENOENT);
        check_equal(scxml_cnet_domain_fence_get_stats(
            &f.fence, &fence_stats), true);
        check_equal(fence_stats.current_generation, gen[1]);
        check_equal(fence_stats.staged_generation, gen[1] + UINT64_C(1));
        check_equal(fence_stats.attached, (size_t)2u);
        check_equal(scxml_cnet_domain_fence_try_submit(
            &f.fence, gen[1] + UINT64_C(1), send_one, &write_b), SALTS_EPERM);
        check_equal(scxml_cnet_domain_fence_retire(
            &f.fence, gen[1] + UINT64_C(1), NULL, NULL), SALTS_OK);
        check_equal(salts_component_plugin_generation_discard(
            &f.generations[2].generation), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(scxml_cnet_domain_fence_attach(
            &f.fence, gen[1] + UINT64_C(1)), SALTS_EALREADY);
        check_equal(scxml_cnet_domain_fence_get_stats(
            &f.fence, &fence_stats), true);
        check_equal(fence_stats.current_generation, gen[1]);
        check_equal(fence_stats.staged_generation, UINT64_C(0));
        check_equal(fence_stats.attached, (size_t)1u);
        check_equal(invoke_token_from_scope(&f.scopes[1]), token[1]);

        /* The existing listener and gN+1 still transmit after rollback. */
        check_equal(cnet_receive(&f.receiver, f.inbound_next, 1u), SALTS_OK);
        /* Both instructions are in the same live DSO but execute on
           independent CFlow SerialExecutor and CNet owner lanes. This
           overlaps real cancellation preparation with native completion. */
        check_true(scxml_test_cnet_probe_arm_send_gate(
            &probes[1], &overlap_send_gate));
        check_true(scxml_test_cnet_probe_arm_cancel_gate(
            &probes[1], &cancel_gate));
        write_c = (fenced_cnet_write){&f, &c, 1u, f.outbound_next};
        check_equal(scxml_cnet_domain_fence_try_submit(
            &f.fence, gen[1], send_one, &write_c), SALTS_OK);
        check_true(native_dso_snapshot(&probes[1], &native[1]));
        check_equal(native[1].native_sends, (size_t)1u);

        overlap.cancel_gate = &cancel_gate;
        overlap.send_gate = &overlap_send_gate;
        check_equal(cmeta_thread_create(
            &overlap_thread, release_simultaneous_invoke_native, &overlap),
            SALTS_OK);
        check_equal(scxml_session_try_send_named_with_metadata(
            &f.sessions[1], "finish", 6u, &cancel_metadata),
            CFLOW_MAILBOX_OK);
        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        while (!atomic_load_explicit(
                   &overlap_send_gate.entered, memory_order_acquire) &&
               cmeta_monotonic_ms() < deadline)
            check_equal(cnet_client_poll(&f.sender, 1u, &events), SALTS_OK);
        check_equal(cmeta_thread_join(&overlap_thread), SALTS_OK);
        cmeta_thread_destroy(&overlap_thread);
        check_true(overlap.both_entered);
        check_equal(atomic_load_explicit(
            &overlap_send_gate.timed_out, memory_order_acquire), 0);
        check_equal(atomic_load_explicit(
            &cancel_gate.timed_out, memory_order_acquire), 0);
        check_true(cflow_executor_wait_idle(&f.executors[1]));
        check_true(native_dso_snapshot(&probes[1], &native[1]));
        check_equal(native[1].native_sends, (size_t)2u);
        check_equal(invoke_token_from_scope(&f.scopes[1]), -token[1]);
        check_equal(scxml_session_report_invoke_done(
            &f.sessions[1], (uint64_t)token[1]),
            CFLOW_MAILBOX_INVALID_ARGUMENT);
        check_equal(scxml_session_destroy(&f.sessions[1]),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_component_invoke_provider_destroy(&f.invoke[1]),
                    SCXML_COMPONENT_OK);
        check_equal(salts_component_plugin_runtime_close(
            &f.runtime, &previous), SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &f.generations[1].generation);
        check_equal(scxml_cnet_domain_fence_close(&f.fence), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_retire(
            &f.fence, gen[1], retire_native_dso_generation, &draining[1]),
            SALTS_EBUSY);
        check_false(draining[1].saw_native_terminal);

        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        while (cmeta_monotonic_ms() < deadline) {
            check_equal(cnet_client_poll(&f.sender, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&f.receiver, 1u, &events), SALTS_OK);
            check_true(native_dso_snapshot(&probes[1], &native[1]));
            if (native[1].native_sends >= 2u &&
                f.receiver_probe_next.received >= 2u) break;
        }
        check_equal(native[1].native_sends, (size_t)2u);
        check_equal(f.receiver_probe_next.received, (size_t)2u);
        check_equal(f.receiver_probe_next.bytes[1], (unsigned char)'C');
        /* Repeat with an authentic terminal on_state callback *inside*
           the second DSO. Unload races the callback, not a copied snapshot. */
        check_true(scxml_test_cnet_probe_arm_terminal_gate(
            &probes[1], &terminal_gate));
        terminal_race.registry = &f.registry;
        terminal_race.plugin = f.plugins[1];
        terminal_race.gate = &terminal_gate;
        check_equal(cmeta_thread_create(
            &terminal_thread, foreign_unload_during_callback, &terminal_race),
            SALTS_OK);
        check_equal(cnet_close(&f.sender, f.outbound_next), SALTS_OK);
        check_equal(cnet_close(&f.receiver, f.inbound_next), SALTS_OK);
        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        do {
            check_equal(cnet_client_poll(&f.sender, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&f.receiver, 1u, &events), SALTS_OK);
            check_true(native_dso_snapshot(&probes[1], &native[1]));
        } while (!native[1].native_terminal &&
                 cmeta_monotonic_ms() < deadline);
        check_true(native[1].native_terminal);
        check_equal(cmeta_thread_join(&terminal_thread), SALTS_OK);
        cmeta_thread_destroy(&terminal_thread);
        check_true(terminal_race.reached_callback);
        check_equal(terminal_race.status, CMETA_PLUGIN_BUSY);
        check_equal(atomic_load_explicit(
            &terminal_gate.timed_out, memory_order_acquire), 0);
        check_equal(scxml_cnet_domain_fence_retire(
            &f.fence, gen[1], retire_native_dso_generation, &draining[1]),
            SALTS_EBUSY);
        check_true(draining[1].saw_native_terminal);
        check_true(draining[1].saw_scope_busy);
        check_equal(scxml_component_scope_release(&f.scopes[1]),
                    SCXML_COMPONENT_OK);
        check_equal(scxml_cnet_domain_fence_retire(
            &f.fence, gen[1], retire_native_dso_generation, &draining[1]),
            SALTS_OK);
        check_equal(scxml_cnet_domain_fence_destroy(&f.fence), SALTS_OK);
        f.outbound_next = (cnet_connection){0};
        f.inbound_next = (cnet_connection){0};
    }

    it("fails closed if published Component cannot activate exclusive CNet owner") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "version='1.0' initial='working'>"
            "<state id='working'>"
            "<invoke id='worker' type='urn:test:invoke' src='worker'/>"
            "<transition event='done.invoke.worker' target='done'/>"
            "</state><final id='done'/></scxml>";
        const char *paths[] = {SCXML_INVOKE_DSO_ONE, SCXML_INVOKE_DSO_TWO};
        const cmeta_plugin_registry_config registry_conf = {.capacity = 2u};
        const cnet_client_config net_conf = cnet_config();
        const cnet_listener_config listener_conf = {
            .backend = backend(), .host = "127.0.0.1",
            .port = 0u, .backlog = 2u
        };
        cnet_observer sender_observer = {0};
        const cnet_observer receiver_observer = {
            .on_state = receiver_state, .on_receive = receiver_data,
            .user = &f.receiver_probe
        };
        scxml_test_cnet_probe probe = {0};
        scxml_test_cnet_observer_stats native = {0};
        joint_dso_native_observer drain = {
            .fixture = &f, .generation_slot = 0u, .expected_sends = 1u
        };
        salts_component_plugin_generation *previous = NULL;
        scxml_cnet_domain_fence_stats fence_stats = {0};
        cnet_connect_options options = {0};
        scxml_diagnostic diagnostic = {0};
        uint16_t port = 0u;
        uint64_t deadline, old_generation;
        size_t events = 0u;
        int ready = 0, invoke_token;
        const unsigned char payload = 'P';
        char uri[64];
        fenced_cnet_write accepted_write;

        check_equal(cmeta_plugin_registry_init(&f.registry, &registry_conf),
                    CMETA_PLUGIN_OK);
        f.registry_live = true;
        for (size_t index = 0u; index < SESSIONS; ++index) {
            check_equal(cmeta_plugin_registry_load(
                &f.registry, paths[index], &f.plugins[index]), CMETA_PLUGIN_OK);
            check_equal(cmeta_plugin_registry_start(
                &f.registry, f.plugins[index]), CMETA_PLUGIN_OK);
            check_equal(generation_build(
                &f, index, index, UINT64_C(11) + (uint64_t)index),
                SALTS_COMPONENT_PLUGIN_OK);
        }
        check_equal(salts_component_plugin_runtime_init(&f.runtime),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(scxml_compile(&f.program, source, sizeof(source) - 1u,
                                  NULL, &diagnostic), SCXML_OK);
        check_equal(scxml_cnet_domain_fence_init(&f.fence), SALTS_OK);
        check_equal(cnet_client_init(&f.sender, &net_conf), SALTS_OK);
        check_equal(cnet_client_init(&f.receiver, &net_conf), SALTS_OK);
        check_equal(cnet_listener_init(&f.listener, &listener_conf), SALTS_OK);
        check_equal(cnet_listener_port(&f.listener, &port), SALTS_OK);
        check_true(port != 0u);
        check_true(snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u",
                            (unsigned)port) > 0);

        check_equal(salts_component_plugin_runtime_publish(
            &f.runtime, &f.generations[0].generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_null(previous);
        check_equal(scxml_component_scope_acquire(
            &f.scopes[0], &f.runtime), SCXML_COMPONENT_OK);
        old_generation = scxml_component_scope_generation_id(&f.scopes[0]);
        check_equal(old_generation, UINT64_C(11));
        check_equal(scxml_component_invoke_provider_bind(
            &f.invoke[0], &f.scopes[0], "ScxmlInvokeDsoFixture",
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL),
            SCXML_COMPONENT_OK);
        check_true(dso_native_observer_from_scope(&f.scopes[0], &probe));
        check_true(scxml_test_cnet_probe_get_observer(&probe, &sender_observer));
        check_not_null(sender_observer.on_send);
        check_not_null(sender_observer.on_state);
        check_true(cflow_executor_serial_init(&f.executors[0]));
        f.executor_live[0] = true;
        {
            scxml_session_config config = {
                .program = &f.program, .executor = &f.executors[0],
                .external_event_capacity = 2u, .internal_event_capacity = 2u,
                .completion_capacity = 2u, .microstep_limit = 16u,
                .invocation_capacity = 1u, .effect_capacity = 2u,
                .adapter_internal_event_capacity = 2u,
                .invoke = scxml_component_invoke_provider_adapter(&f.invoke[0]),
                .invoke_user =
                    scxml_component_invoke_provider_user(&f.invoke[0])
            };
            check_equal(scxml_session_init(&f.sessions[0], &config),
                        CFLOW_STATECHART_INSTANCE_OK);
        }
        check_true(cflow_executor_wait_idle(&f.executors[0]));
        invoke_token = invoke_token_from_scope(&f.scopes[0]);
        check_true(invoke_token > 0);
        check_equal(scxml_cnet_domain_fence_attach(
            &f.fence, old_generation), SALTS_OK);

        options.uri = uri;
        options.observer = sender_observer;
        check_equal(cnet_connect(&f.sender, &options, &f.outbound), SALTS_OK);
        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        do {
            check_equal(cnet_client_poll(&f.sender, 1u, &events), SALTS_OK);
            check_true(native_dso_snapshot(&probe, &native));
        } while (!native.native_connected && cmeta_monotonic_ms() < deadline);
        check_true(native.native_connected);
        check_equal(cnet_listener_wait(&f.listener, TIMEOUT_MS, &ready),
                    SALTS_OK);
        check_equal(ready, 1);
        check_equal(cnet_listener_accept(
            &f.listener, &f.receiver, &receiver_observer, &f.inbound),
            SALTS_OK);
        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        while (!f.receiver_probe.connected && cmeta_monotonic_ms() < deadline)
            check_equal(cnet_client_poll(&f.receiver, 1u, &events), SALTS_OK);
        check_true(f.receiver_probe.connected);
        check_equal(cnet_receive(&f.receiver, f.inbound, 1u), SALTS_OK);
        accepted_write = (fenced_cnet_write){
            &f, &payload, 1u, f.outbound
        };
        check_equal(scxml_cnet_domain_fence_try_submit(
            &f.fence, old_generation, send_one, &accepted_write), SALTS_OK);
        check_true(native_dso_snapshot(&probe, &native));
        check_equal(native.native_sends, (size_t)0u);

        /* Publication succeeds BEFORE the domain's exclusive-I/O switch.
           A domain-owner shutdown now rejects activation: the component
           runtime must fail closed, never secretly republish the old DSO. */
        check_equal(scxml_cnet_domain_fence_attach(
            &f.fence, UINT64_C(12)), SALTS_OK);
        check_equal(salts_component_plugin_runtime_publish(
            &f.runtime, &f.generations[1].generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &f.generations[0].generation);
        check_equal(scxml_cnet_domain_fence_close(&f.fence), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_activate(
            &f.fence, UINT64_C(12)), SALTS_ESHUTDOWN);
        check_equal(scxml_cnet_domain_fence_get_stats(
            &f.fence, &fence_stats), true);
        check_equal(fence_stats.current_generation, old_generation);
        check_equal(fence_stats.staged_generation, UINT64_C(12));
        check_equal(fence_stats.switches, UINT64_C(0));
        check_equal(fence_stats.admission_epoch, UINT64_C(1));
        check_equal(scxml_cnet_domain_fence_try_submit(
            &f.fence, old_generation, send_one, &accepted_write),
            SALTS_ESHUTDOWN);
        check_equal(scxml_cnet_domain_fence_try_submit(
            &f.fence, UINT64_C(12), send_one, &accepted_write),
            SALTS_ESHUTDOWN);
        check_equal(invoke_token_from_scope(&f.scopes[0]), invoke_token);
        check_equal(salts_component_plugin_generation_drain(
            &f.runtime, &f.generations[0].generation),
            SALTS_COMPONENT_PLUGIN_BUSY);
        check_equal(cmeta_plugin_registry_unload(
            &f.registry, f.plugins[0]), CMETA_PLUGIN_BUSY);

        /* Explicitly close the published, never-admitted replacement and
           discard its staged domain binding. Neither action cancels an
           already accepted old NativeIO write or fabricates a terminal. */
        check_equal(salts_component_plugin_runtime_close(
            &f.runtime, &previous), SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &f.generations[1].generation);
        check_null(f.runtime.current);
        check_equal(salts_component_plugin_generation_drain(
            &f.runtime, &f.generations[1].generation),
            SALTS_COMPONENT_PLUGIN_OK);
        check_equal(scxml_cnet_domain_fence_retire(
            &f.fence, UINT64_C(12), NULL, NULL), SALTS_OK);
        check_equal(scxml_cnet_domain_fence_destroy(&f.fence), SALTS_EBUSY);

        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        while (cmeta_monotonic_ms() < deadline) {
            check_equal(cnet_client_poll(&f.sender, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&f.receiver, 1u, &events), SALTS_OK);
            check_true(native_dso_snapshot(&probe, &native));
            if (native.native_sends >= 1u &&
                f.receiver_probe.received >= 1u) break;
        }
        check_equal(native.native_sends, (size_t)1u);
        check_equal(native.native_send_bytes, (size_t)1u);
        check_equal(f.receiver_probe.received, (size_t)1u);
        check_equal(f.receiver_probe.bytes[0], payload);
        check_false(f.receiver_probe.failed);
        check_equal(cnet_close(&f.sender, f.outbound), SALTS_OK);
        check_equal(cnet_close(&f.receiver, f.inbound), SALTS_OK);
        deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
        do {
            check_equal(cnet_client_poll(&f.sender, 1u, &events), SALTS_OK);
            check_equal(cnet_client_poll(&f.receiver, 1u, &events), SALTS_OK);
            check_true(native_dso_snapshot(&probe, &native));
        } while (!native.native_terminal && cmeta_monotonic_ms() < deadline);
        check_true(native.native_terminal);
        drain.adapter = probe;
        check_equal(scxml_cnet_domain_fence_retire(
            &f.fence, old_generation, retire_native_dso_generation, &drain),
            SALTS_EBUSY);
        check_true(drain.saw_native_terminal);
        check_true(drain.saw_scope_busy);

        check_equal(scxml_session_report_invoke_done(
            &f.sessions[0], (uint64_t)invoke_token), CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&f.executors[0]));
        check_equal(scxml_session_report_invoke_done(
            &f.sessions[0], (uint64_t)invoke_token),
            CFLOW_MAILBOX_INVALID_ARGUMENT);
        check_equal(scxml_session_destroy(&f.sessions[0]),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_component_invoke_provider_destroy(
            &f.invoke[0]), SCXML_COMPONENT_OK);
        check_equal(scxml_component_scope_release(
            &f.scopes[0]), SCXML_COMPONENT_OK);
        /* The module is now safe to drain. Never dereference the borrowed
           CMeta probe or its DSO callback after this point. */
        check_equal(scxml_cnet_domain_fence_retire(
            &f.fence, old_generation, retire_native_dso_generation, &drain),
            SALTS_OK);
        check_equal(scxml_cnet_domain_fence_destroy(&f.fence), SALTS_OK);
        check_equal(f.runtime.attached_generations, (size_t)0u);
        check_equal(f.runtime.active_scopes, (size_t)0u);
    }

    it("binds real DSO Invoke identities to one NativeIO ACT owner across generations") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "version='1.0' initial='working'>"
            "<state id='working'>"
            "<invoke id='worker' type='urn:test:invoke' src='worker'/>"
            "<transition event='finish' target='done'/>"
            "<transition event='done.invoke.worker' target='done'/>"
            "</state><final id='done'/></scxml>";
        const char *paths[] = {SCXML_INVOKE_DSO_ONE, SCXML_INVOKE_DSO_TWO};
        const cmeta_plugin_registry_config registry_conf = {.capacity = 2u};
        const native_io_backend_config native_conf = {
            .kind = backend(), .endpoint_capacity = 1u,
            .request_capacity = 1u, .completion_batch_capacity = 1u
        };
        const scxml_event_metadata finish_metadata = {
            .abi_version = SCXML_EVENT_METADATA_ABI,
            .struct_size = sizeof(scxml_event_metadata)
        };
        native_io_backend native = {0};
        native_io_endpoint endpoint = {0};
        native_io_operation operation = {0};
        native_io_request old_request = {0}, next_request = {0};
        native_io_completion old_terminal = {0}, next_terminal = {0};
        native_io_backend_stats stats = {0};
        scxml_act_socket sockets[2] = {
            SCXML_ACT_INVALID_SOCKET, SCXML_ACT_INVALID_SOCKET
        };
        scxml_invoke_native_context old_ctx = {0}, next_ctx = {0};
        scxml_invoke_native_context *settled = NULL;
        scxml_invoke_native_act old_act = {0}, next_act = {0};
        salts_component_plugin_generation *previous = NULL;
        scxml_diagnostic diagnostic = {0};
        unsigned char old_byte = 0u, next_byte = 0u;
        const unsigned char payload = 'N';
        size_t count = 0u;
        int old_token, next_token, cancel_status;

        check_equal(cmeta_plugin_registry_init(&f.registry, &registry_conf),
                    CMETA_PLUGIN_OK);
        f.registry_live = true;
        for (size_t index = 0u; index < SESSIONS; ++index) {
            check_equal(cmeta_plugin_registry_load(
                &f.registry, paths[index], &f.plugins[index]), CMETA_PLUGIN_OK);
            check_equal(cmeta_plugin_registry_start(
                &f.registry, f.plugins[index]), CMETA_PLUGIN_OK);
            check_equal(generation_build(
                &f, index, index, UINT64_C(11) + (uint64_t)index),
                SALTS_COMPONENT_PLUGIN_OK);
        }
        check_equal(salts_component_plugin_runtime_init(&f.runtime),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(scxml_compile(&f.program, source, sizeof(source) - 1u,
                                  NULL, &diagnostic), SCXML_OK);
        check_equal(salts_component_plugin_runtime_publish(
            &f.runtime, &f.generations[0].generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_null(previous);
        check_equal(scxml_component_scope_acquire(
            &f.scopes[0], &f.runtime), SCXML_COMPONENT_OK);
        check_equal(scxml_component_invoke_provider_bind(
            &f.invoke[0], &f.scopes[0], "ScxmlInvokeDsoFixture",
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL),
            SCXML_COMPONENT_OK);
        check_true(cflow_executor_serial_init(&f.executors[0]));
        f.executor_live[0] = true;
        {
            scxml_session_config conf = {
                .program = &f.program, .executor = &f.executors[0],
                .external_event_capacity = 2u, .internal_event_capacity = 2u,
                .completion_capacity = 2u, .microstep_limit = 16u,
                .invocation_capacity = 1u, .effect_capacity = 2u,
                .adapter_internal_event_capacity = 2u,
                .invoke = scxml_component_invoke_provider_adapter(&f.invoke[0]),
                .invoke_user = scxml_component_invoke_provider_user(&f.invoke[0])
            };
            check_equal(scxml_session_init(&f.sessions[0], &conf),
                        CFLOW_STATECHART_INSTANCE_OK);
        }
        check_true(cflow_executor_wait_idle(&f.executors[0]));
        old_token = invoke_token_from_scope(&f.scopes[0]);
        check_true(old_token > 0);

        /* Only one NativeIO backend and one owner: the DSO/Invoke lease is
           owned by ComponentPlugin, while the ACT owns no transport or DSO.
           NativeIO remains the sole source of I/O terminal truth. */
        check_equal(scxml_act_make_socket_pair(sockets), 0);
        check_equal(native_io_backend_init(&native, &native_conf), SALTS_OK);
        check_equal(native_io_backend_attach_socket(
            &native, (uintptr_t)sockets[0], &endpoint), SALTS_OK);
        operation = (native_io_operation){
            .kind = NATIVE_IO_OPERATION_STREAM_RECV,
            .endpoint = endpoint, .buffer = &old_byte,
            .length = 1u, .user_data = (uintptr_t)old_token
        };
        check_equal(native_io_backend_submit(
            &native, &operation, &old_request), SALTS_OK);
        old_ctx = (scxml_invoke_native_context){
            &f.scopes[0], UINT64_C(11), (uintptr_t)old_token,
            &old_byte, 0u
        };
        check_equal(scxml_invoke_native_act_bind(
            &old_act, old_request, endpoint, operation.user_data, &old_ctx),
            SALTS_OK);

        /* The original DSO Invoke stays gN, while a real replacement DSO
           becomes the current generation. Neither native request identity
           nor its borrowed payload may migrate to the replacement. */
        check_equal(salts_component_plugin_runtime_publish(
            &f.runtime, &f.generations[1].generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &f.generations[0].generation);
        check_equal(scxml_component_scope_acquire(
            &f.scopes[1], &f.runtime), SCXML_COMPONENT_OK);
        check_equal(scxml_component_invoke_provider_bind(
            &f.invoke[1], &f.scopes[1], "ScxmlInvokeDsoFixture",
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL),
            SCXML_COMPONENT_OK);
        check_true(cflow_executor_serial_init(&f.executors[1]));
        f.executor_live[1] = true;
        {
            scxml_session_config conf = {
                .program = &f.program, .executor = &f.executors[1],
                .external_event_capacity = 2u, .internal_event_capacity = 2u,
                .completion_capacity = 2u, .microstep_limit = 16u,
                .invocation_capacity = 1u, .effect_capacity = 2u,
                .adapter_internal_event_capacity = 2u,
                .invoke = scxml_component_invoke_provider_adapter(&f.invoke[1]),
                .invoke_user = scxml_component_invoke_provider_user(&f.invoke[1])
            };
            check_equal(scxml_session_init(&f.sessions[1], &conf),
                        CFLOW_STATECHART_INSTANCE_OK);
        }
        check_true(cflow_executor_wait_idle(&f.executors[1]));
        next_token = invoke_token_from_scope(&f.scopes[1]);
        check_true(next_token > 0);
        check_equal(invoke_token_from_scope(&f.scopes[0]), old_token);
        check_equal(scxml_component_scope_generation_id(&f.scopes[0]),
                    old_ctx.generation);
        check_equal(salts_component_plugin_generation_drain(
            &f.runtime, &f.generations[0].generation),
            SALTS_COMPONENT_PLUGIN_BUSY);
        check_equal(cmeta_plugin_registry_unload(
            &f.registry, f.plugins[0]), CMETA_PLUGIN_BUSY);

        /* An SCXML cancellation settles its Statechart effect journal, NOT
           the accepted kernel request or the typed NativeIO ACT. */
        check_equal(scxml_session_try_send_named_with_metadata(
            &f.sessions[0], "finish", 6u, &finish_metadata),
            CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&f.executors[0]));
        check_equal(invoke_token_from_scope(&f.scopes[0]), -old_token);
        check_true(old_act.active);
        check_equal(old_ctx.settlements, (size_t)0u);
        check_equal(native_io_backend_release_socket(
            &native, endpoint), SALTS_EBUSY);
        cancel_status = native_io_backend_cancel(&native, old_request);
        check_true(cancel_status == SALTS_OK ||
                   cancel_status == SALTS_EALREADY);
        check_true(old_act.active);
        check_equal(native_io_backend_observe(
            &native, &old_terminal, 1u, TIMEOUT_MS, &count), SALTS_OK);
        check_equal(count, (size_t)1u);
        check_equal(old_terminal.request.slot, old_request.slot);
        check_equal(old_terminal.request.generation, old_request.generation);
        check_equal(old_terminal.kind, NATIVE_IO_COMPLETION_CANCELLED);
        check_equal(scxml_invoke_native_act_settle(
            &old_act, &old_terminal, &settled), SALTS_OK);
        check_true(settled == &old_ctx);
        check_true(settled->scope == &f.scopes[0]);
        check_equal(settled->generation, UINT64_C(11));
        check_equal(settled->invoke_token, (uintptr_t)old_token);
        ++settled->settlements;
        check_equal(scxml_invoke_native_act_settle(
            &old_act, &old_terminal, &settled), SALTS_EALREADY);
        check_null(settled);
        check_equal(old_ctx.settlements, (size_t)1u);
        check_equal(scxml_session_report_invoke_done(
            &f.sessions[0], (uint64_t)old_token),
            CFLOW_MAILBOX_INVALID_ARGUMENT);

        /* Capacity one forces an actual slot reuse. The old authentic
           terminal cannot consume the new generation's ACT, even before
           the old Component scope/module is finally unloaded. */
        operation.buffer = &next_byte;
        operation.user_data = (uintptr_t)next_token;
        check_equal(native_io_backend_submit(
            &native, &operation, &next_request), SALTS_OK);
        check_equal(next_request.slot, old_request.slot);
        check_not_equal(next_request.generation, old_request.generation);
        next_ctx = (scxml_invoke_native_context){
            &f.scopes[1], UINT64_C(12), (uintptr_t)next_token,
            &next_byte, 0u
        };
        check_equal(scxml_invoke_native_act_bind(
            &next_act, next_request, endpoint, operation.user_data, &next_ctx),
            SALTS_OK);
        check_equal(scxml_invoke_native_act_settle(
            &next_act, &old_terminal, &settled), SALTS_ENOENT);
        check_null(settled);
        check_true(next_act.active);
        check_equal(next_ctx.settlements, (size_t)0u);
        check_equal(native_io_backend_cancel(
            &native, old_request), SALTS_ENOENT);
        check_true(native_io_backend_get_stats(&native, &stats));
        check_equal(stats.active_requests, (size_t)1u);

        check_equal(scxml_session_destroy(&f.sessions[0]),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_component_invoke_provider_destroy(&f.invoke[0]),
                    SCXML_COMPONENT_OK);
        check_equal(scxml_component_scope_release(&f.scopes[0]),
                    SCXML_COMPONENT_OK);
        check_equal(salts_component_plugin_generation_drain(
            &f.runtime, &f.generations[0].generation),
            SALTS_COMPONENT_PLUGIN_OK);
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
        check_equal(invoke_token_from_scope(&f.scopes[1]), next_token);
        check_true(next_act.active);

        check_equal(send(sockets[1], (const char *)&payload, 1, 0), 1);
        count = 0u;
        check_equal(native_io_backend_observe(
            &native, &next_terminal, 1u, TIMEOUT_MS, &count), SALTS_OK);
        check_equal(count, (size_t)1u);
        check_equal(next_terminal.request.slot, next_request.slot);
        check_equal(next_terminal.request.generation, next_request.generation);
        check_equal(next_terminal.kind, NATIVE_IO_COMPLETION_OK);
        check_equal(next_terminal.bytes, (size_t)1u);
        check_equal(next_byte, payload);
        check_equal(scxml_invoke_native_act_settle(
            &next_act, &next_terminal, &settled), SALTS_OK);
        check_true(settled == &next_ctx);
        check_true(settled->scope == &f.scopes[1]);
        check_equal(settled->generation, UINT64_C(12));
        check_equal(settled->invoke_token, (uintptr_t)next_token);
        ++settled->settlements;
        check_equal(scxml_invoke_native_act_settle(
            &next_act, &next_terminal, &settled), SALTS_EALREADY);
        check_null(settled);
        check_equal(next_ctx.settlements, (size_t)1u);
        check_equal(native_io_backend_cancel(
            &native, next_request), SALTS_ENOENT);
        check_true(native_io_backend_get_stats(&native, &stats));
        check_equal(stats.active_requests, (size_t)0u);

        check_equal(scxml_session_report_invoke_done(
            &f.sessions[1], (uint64_t)next_token), CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&f.executors[1]));
        check_equal(scxml_session_report_invoke_done(
            &f.sessions[1], (uint64_t)next_token),
            CFLOW_MAILBOX_INVALID_ARGUMENT);
        check_equal(scxml_session_destroy(&f.sessions[1]),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_component_invoke_provider_destroy(&f.invoke[1]),
                    SCXML_COMPONENT_OK);
        check_equal(scxml_component_scope_release(&f.scopes[1]),
                    SCXML_COMPONENT_OK);
        check_equal(salts_component_plugin_runtime_close(
            &f.runtime, &previous), SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &f.generations[1].generation);
        check_equal(salts_component_plugin_generation_drain(
            &f.runtime, &f.generations[1].generation),
            SALTS_COMPONENT_PLUGIN_OK);
        check_equal(f.runtime.attached_generations, (size_t)0u);
        check_equal(f.runtime.active_scopes, (size_t)0u);

        check_equal(scxml_act_close_socket(sockets[0]), 0);
        check_equal(scxml_act_close_socket(sockets[1]), 0);
        check_equal(native_io_backend_release_socket(
            &native, endpoint), SALTS_OK);
        check_equal(native_io_backend_close(&native), SALTS_OK);
        check_equal(native_io_backend_destroy(&native), SALTS_OK);
#if defined(_WIN32)
        check_equal(WSACleanup(), 0);
#endif
    }

    it("recreates real DSO and CNet owner only after concurrent unload is rejected and native quiesces") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "version='1.0' initial='working'>"
            "<state id='working'>"
            "<invoke id='worker' type='urn:test:invoke' src='worker'/>"
            "<transition event='done.invoke.worker' target='done'/>"
            "</state><final id='done'/></scxml>";
        const char *paths[] = {SCXML_INVOKE_DSO_ONE, SCXML_INVOKE_DSO_TWO};
        const cmeta_plugin_registry_config registry_conf = {.capacity = 2u};
        const cnet_client_config net_conf = cnet_config();
        const cnet_listener_config listener_conf = {
            .backend = backend(), .host = "127.0.0.1",
            .port = 0u, .backlog = 2u
        };
        const uint64_t generation_ids[SESSIONS] = {
            UINT64_C(11), UINT64_C(22)
        };
        scxml_diagnostic diagnostic = {0};
        scxml_test_cnet_callback_gate send_gate = {0};
        dso_domain_restart_attempt race = {0};
        cmeta_thread_t foreign_thread = NULL;
        salts_component_plugin_generation *previous = NULL;
        cmeta_plugin_ref retired_plugin = {0};

        callback_gate_init(&send_gate);
        check_equal(cmeta_plugin_registry_init(&f.registry, &registry_conf),
                    CMETA_PLUGIN_OK);
        f.registry_live = true;
        check_equal(salts_component_plugin_runtime_init(&f.runtime),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(scxml_compile(
            &f.program, source, sizeof(source) - 1u, NULL, &diagnostic),
            SCXML_OK);

        for (size_t round = 0u; round < SESSIONS; ++round) {
            const uint64_t generation = generation_ids[round];
            scxml_test_cnet_probe probe = {0};
            scxml_test_cnet_observer_stats native = {0};
            joint_dso_native_observer drain = {
                .fixture = &f, .generation_slot = round,
                .expected_sends = 1u
            };
            scxml_cnet_domain_fence_stats domain_stats = {0};
            cnet_observer sender_observer = {0};
            net_receiver_probe *receiver = round == 0u
                ? &f.receiver_probe : &f.receiver_probe_next;
            const cnet_observer receiver_observer = {
                .on_state = receiver_state,
                .on_receive = receiver_data,
                .user = receiver
            };
            cnet_connect_options options = {0};
            cnet_connection *outbound = round == 0u
                ? &f.outbound : &f.outbound_next;
            cnet_connection *inbound = round == 0u
                ? &f.inbound : &f.inbound_next;
            unsigned char byte = (unsigned char)('M' + (int)round);
            fenced_cnet_write write = {&f, &byte, 1u, {0}};
            uint16_t port = 0u;
            uint64_t deadline;
            size_t events = 0u;
            char uri[64];
            int ready = 0, token;

            check_equal(cmeta_plugin_registry_load(
                &f.registry, paths[round], &f.plugins[round]),
                CMETA_PLUGIN_OK);
            check_equal(cmeta_plugin_registry_start(
                &f.registry, f.plugins[round]), CMETA_PLUGIN_OK);
            check_equal(generation_build(
                &f, round, round, generation), SALTS_COMPONENT_PLUGIN_OK);
            check_equal(salts_component_plugin_runtime_publish(
                &f.runtime, &f.generations[round].generation, &previous),
                SALTS_COMPONENT_PLUGIN_OK);
            check_null(previous);
            check_equal(scxml_component_scope_acquire(
                &f.scopes[round], &f.runtime), SCXML_COMPONENT_OK);
            check_equal(scxml_component_scope_generation_id(
                &f.scopes[round]), generation);
            check_true(restart_invoke_session(&f, round));
            token = invoke_token_from_scope(&f.scopes[round]);
            check_true(token > 0);
            check_true(dso_native_observer_from_scope(
                &f.scopes[round], &probe));
            check_true(scxml_test_cnet_probe_get_observer(
                &probe, &sender_observer));

            /* Domain storage is the SAME address reused on this owner,
               but every successful destroy must erase its runtime history. */
            check_equal(scxml_cnet_domain_fence_init(&f.fence), SALTS_OK);
            check_equal(scxml_cnet_domain_fence_attach(
                &f.fence, generation), SALTS_OK);
            check_true(scxml_cnet_domain_fence_get_stats(
                &f.fence, &domain_stats));
            check_equal(domain_stats.current_generation, generation);
            check_equal(domain_stats.admission_epoch, UINT64_C(1));
            check_equal(domain_stats.accepted, UINT64_C(0));
            check_equal(domain_stats.retired, UINT64_C(0));
            if (round != 0u) {
                check_equal(scxml_cnet_domain_fence_try_submit(
                    &f.fence, generation_ids[0], send_one, &write),
                    SALTS_EPERM);
                check_equal(cmeta_plugin_registry_unload(
                    &f.registry, retired_plugin), CMETA_PLUGIN_STALE);
            }

            check_equal(cnet_client_init(&f.sender, &net_conf), SALTS_OK);
            check_equal(cnet_client_init(&f.receiver, &net_conf), SALTS_OK);
            check_equal(cnet_listener_init(
                &f.listener, &listener_conf), SALTS_OK);
            check_equal(cnet_listener_port(&f.listener, &port), SALTS_OK);
            check_true(port != 0u);
            check_true(snprintf(uri, sizeof(uri),
                "tcp://127.0.0.1:%u", (unsigned)port) > 0);
            options.uri = uri;
            options.observer = sender_observer;
            check_equal(cnet_connect(
                &f.sender, &options, outbound), SALTS_OK);
            deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
            do {
                check_equal(cnet_client_poll(
                    &f.sender, 1u, &events), SALTS_OK);
                check_true(native_dso_snapshot(&probe, &native));
            } while (!native.native_connected &&
                     cmeta_monotonic_ms() < deadline);
            check_true(native.native_connected);
            check_equal(cnet_listener_wait(
                &f.listener, TIMEOUT_MS, &ready), SALTS_OK);
            check_equal(ready, 1);
            check_equal(cnet_listener_accept(
                &f.listener, &f.receiver, &receiver_observer, inbound),
                SALTS_OK);
            deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
            while (!receiver->connected && cmeta_monotonic_ms() < deadline)
                check_equal(cnet_client_poll(
                    &f.receiver, 1u, &events), SALTS_OK);
            check_true(receiver->connected);
            check_equal(cnet_receive(
                &f.receiver, *inbound, 1u), SALTS_OK);
            write.connection = *outbound;

            if (round == 0u) {
                check_true(scxml_test_cnet_probe_arm_send_gate(
                    &probe, &send_gate));
                race = (dso_domain_restart_attempt){
                    .fence = &f.fence, .registry = &f.registry,
                    .plugin = f.plugins[round], .gate = &send_gate
                };
                check_equal(cmeta_thread_create(
                    &foreign_thread, foreign_domain_restart_during_dso_send,
                    &race), SALTS_OK);
            }
            check_equal(scxml_cnet_domain_fence_try_submit(
                &f.fence, generation, send_one, &write), SALTS_OK);
            /* Only CNet's owner can poll; the foreign lane is blocked in
               the actual DSO send gate and never touches native state. */
            deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
            while (native.native_sends != 1u &&
                   cmeta_monotonic_ms() < deadline) {
                check_equal(cnet_client_poll(
                    &f.sender, 1u, &events), SALTS_OK);
                check_true(native_dso_snapshot(&probe, &native));
            }
            check_equal(native.native_sends, (size_t)1u);
            if (round == 0u) {
                check_equal(cmeta_thread_join(&foreign_thread), SALTS_OK);
                cmeta_thread_destroy(&foreign_thread);
                check_true(race.callback_entered);
                check_equal(race.close_status, SALTS_EINVAL);
                check_equal(race.destroy_status, SALTS_EINVAL);
                check_equal(race.unload_status, CMETA_PLUGIN_BUSY);
                check_equal(atomic_load_explicit(
                    &send_gate.timed_out, memory_order_acquire), 0);
            }
            check_equal(scxml_cnet_domain_fence_close(
                &f.fence), SALTS_OK);
            check_equal(scxml_cnet_domain_fence_try_submit(
                &f.fence, generation, send_one, &write),
                SALTS_ESHUTDOWN);
            drain.adapter = probe;
            check_equal(scxml_cnet_domain_fence_retire(
                &f.fence, generation, retire_native_dso_generation, &drain),
                SALTS_EBUSY);
            check_equal(scxml_cnet_domain_fence_destroy(
                &f.fence), SALTS_EBUSY);

            deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
            while (receiver->received < 1u &&
                   cmeta_monotonic_ms() < deadline)
                check_equal(cnet_client_poll(
                    &f.receiver, 1u, &events), SALTS_OK);
            check_equal(receiver->received, (size_t)1u);
            check_equal(receiver->bytes[0], byte);
            check_false(receiver->failed);
            check_equal(cnet_close(
                &f.sender, *outbound), SALTS_OK);
            check_equal(cnet_close(
                &f.receiver, *inbound), SALTS_OK);
            deadline = cmeta_monotonic_ms() + TIMEOUT_MS;
            do {
                check_equal(cnet_client_poll(
                    &f.sender, 1u, &events), SALTS_OK);
                check_equal(cnet_client_poll(
                    &f.receiver, 1u, &events), SALTS_OK);
                check_true(native_dso_snapshot(&probe, &native));
            } while ((!native.native_terminal || !receiver->terminal) &&
                     cmeta_monotonic_ms() < deadline);
            check_true(native.native_terminal);
            check_true(receiver->terminal);

            /* Actual native terminal alone cannot retire the DSO while its
               still-running SCXML Invoke holds the original Component Scope. */
            check_equal(salts_component_plugin_runtime_close(
                &f.runtime, &previous), SALTS_COMPONENT_PLUGIN_OK);
            check_true(previous == &f.generations[round].generation);
            check_equal(scxml_cnet_domain_fence_retire(
                &f.fence, generation, retire_native_dso_generation, &drain),
                SALTS_EBUSY);
            check_true(drain.saw_native_terminal);
            check_true(drain.saw_scope_busy);
            check_equal(cmeta_plugin_registry_unload(
                &f.registry, f.plugins[round]), CMETA_PLUGIN_BUSY);
            check_equal(scxml_session_report_invoke_done(
                &f.sessions[round], (uint64_t)token), CFLOW_MAILBOX_OK);
            check_true(cflow_executor_wait_idle(&f.executors[round]));
            check_equal(scxml_session_report_invoke_done(
                &f.sessions[round], (uint64_t)token),
                CFLOW_MAILBOX_INVALID_ARGUMENT);
            check_equal(scxml_session_destroy(&f.sessions[round]),
                        CFLOW_STATECHART_INSTANCE_OK);
            check_equal(scxml_component_invoke_provider_destroy(
                &f.invoke[round]), SCXML_COMPONENT_OK);
            check_equal(scxml_component_scope_release(
                &f.scopes[round]), SCXML_COMPONENT_OK);
            check_equal(scxml_cnet_domain_fence_retire(
                &f.fence, generation, retire_native_dso_generation, &drain),
                SALTS_OK);
            check_equal(scxml_cnet_domain_fence_destroy(
                &f.fence), SALTS_OK);
            check_null(f.fence.impl);
            check_equal(f.runtime.active_scopes, (size_t)0u);
            check_equal(f.runtime.attached_generations, (size_t)0u);

            /* No CNet owner, connection or listener from this incarnation
               survives when the next domain and DSO are initialized. */
            check_equal(cnet_client_stop(
                &f.sender, 1000u), SALTS_OK);
            check_equal(cnet_client_stop(
                &f.receiver, 1000u), SALTS_OK);
            check_equal(cnet_client_destroy(
                &f.sender), SALTS_OK);
            check_equal(cnet_client_destroy(
                &f.receiver), SALTS_OK);
            check_equal(cnet_listener_close(
                &f.listener), SALTS_OK);
            check_equal(cnet_listener_destroy(
                &f.listener), SALTS_OK);
            *outbound = (cnet_connection){0};
            *inbound = (cnet_connection){0};
            check_equal(cmeta_plugin_registry_request_stop(
                &f.registry, f.plugins[round]), CMETA_PLUGIN_OK);
            {
                bool quiescent = false;
                check_equal(cmeta_plugin_registry_poll_quiescent(
                    &f.registry, f.plugins[round], &quiescent),
                    CMETA_PLUGIN_OK);
                check_true(quiescent);
            }
            check_equal(cmeta_plugin_registry_unload(
                &f.registry, f.plugins[round]), CMETA_PLUGIN_OK);
            retired_plugin = f.plugins[round];
            f.plugins[round] = (cmeta_plugin_ref){0};
        }
    }
}
