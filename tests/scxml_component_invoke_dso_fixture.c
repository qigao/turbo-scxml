#include <scxml/provider.h>
#include "scxml_component_invoke_probe.h"
#include <salts/clock.h>
#include <salts/component_plugin_abi.h>
#include <salts/plugin_decl.h>

#include <limits.h>
#include <stdatomic.h>
#include <string.h>

/*
 * Real independently loaded DSO: executable CMeta Invoke dispatch and its
 * move-only CFlow ticket callbacks live in THIS Plugin generation. Test code
 * may inspect the published int marker only while retaining a ComponentScope.
 * No borrowed request pointer or Session internal state is persisted.
 */
cmeta_component(ScxmlInvokeDsoFixture,
    cmeta_provides(scxml_invoke_provider)
    cmeta_provides(scxml_test_cnet_probe));

typedef struct invoke_fixture_state {
    int marker;
    uint64_t active_token;
    uint64_t prepared_token;
    bool start_reserved;
    bool cancel_reserved;
    atomic_bool closed;
    bool native_bound;
    bool native_connected;
    bool native_terminal;
    cnet_connection native_connection;
    size_t native_sends;
    size_t native_send_bytes;
    size_t native_state_callbacks;
    size_t stale_callbacks;
    scxml_test_cnet_callback_gate *send_gate;
    scxml_test_cnet_callback_gate *terminal_gate;
    scxml_test_cnet_callback_gate *cancel_gate;
} invoke_fixture_state;

static invoke_fixture_state state = {
    .marker = SCXML_COMPONENT_DSO_MARKER
};

static void start_commit(void *user) {
    invoke_fixture_state *fixture = (invoke_fixture_state *)user;
    if (fixture == NULL || !fixture->start_reserved) return;
    fixture->marker = (int)fixture->prepared_token;
    fixture->active_token = fixture->prepared_token;
    fixture->start_reserved = false;
}

static void start_discard(void *user) {
    invoke_fixture_state *fixture = (invoke_fixture_state *)user;
    if (fixture == NULL || !fixture->start_reserved) return;
    fixture->start_reserved = false;
    fixture->prepared_token = 0u;
}

static void cancel_commit(void *user) {
    invoke_fixture_state *fixture = (invoke_fixture_state *)user;
    if (fixture == NULL || !fixture->cancel_reserved) return;
    fixture->marker = -(int)fixture->active_token;
    fixture->active_token = 0u;
    fixture->cancel_reserved = false;
}

static void cancel_discard(void *user) {
    invoke_fixture_state *fixture = (invoke_fixture_state *)user;
    if (fixture == NULL || !fixture->cancel_reserved) return;
    fixture->cancel_reserved = false;
}

static scxml_adapter_status prepare_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    invoke_fixture_state *fixture = (invoke_fixture_state *)user;
    if (out_ticket != NULL)
        *out_ticket = (cflow_statechart_effect_ticket){0};
    if (out_error != NULL) *out_error = NULL;
    if (fixture == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL ||
        atomic_load_explicit(&fixture->closed, memory_order_acquire) ||
        fixture->start_reserved || fixture->active_token != 0u ||
        request->token == 0u || request->token > (uint64_t)INT_MAX ||
        request->id == NULL || request->id_size != 6u ||
        memcmp(request->id, "worker", 6u) != 0 ||
        request->type == NULL || request->type_size == 0u ||
        request->payload.kind != SCXML_PAYLOAD_NONE)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    fixture->start_reserved = true;
    fixture->prepared_token = request->token;
    *out_ticket = (cflow_statechart_effect_ticket){
        start_commit, start_discard, fixture
    };
    return SCXML_ADAPTER_ACCEPTED;
}

static void callback_gate_wait(scxml_test_cnet_callback_gate **selected);

static scxml_adapter_status prepare_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    invoke_fixture_state *fixture = (invoke_fixture_state *)user;
    if (out_ticket != NULL)
        *out_ticket = (cflow_statechart_effect_ticket){0};
    if (out_error != NULL) *out_error = NULL;
    if (fixture == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL ||
        fixture->cancel_reserved || request->token == 0u ||
        request->token != fixture->active_token ||
        request->id == NULL || request->id_size != 6u ||
        memcmp(request->id, "worker", 6u) != 0)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    /* Closing rejects new cancel admission; it does not invalidate an
       already accepted move-only ticket or fabricate NativeIO terminal. */
    if (atomic_load_explicit(&fixture->closed, memory_order_acquire))
        return SCXML_ADAPTER_CLOSED;
    /* Pause the actual DSO ACT cancellation callback while CNet's real
       native terminal is dispatched independently on its owner lane. */
    callback_gate_wait(&fixture->cancel_gate);
    fixture->cancel_reserved = true;
    *out_ticket = (cflow_statechart_effect_ticket){
        cancel_commit, cancel_discard, fixture
    };
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status reject_forward(
    void *user, const scxml_invoke_forward_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    (void)user;
    (void)request;
    if (out_ticket != NULL)
        *out_ticket = (cflow_statechart_effect_ticket){0};
    if (out_error != NULL) *out_error = NULL;
    return SCXML_ADAPTER_INVALID_CONTRACT;
}

static void close_provider(void *user) {
    invoke_fixture_state *fixture = (invoke_fixture_state *)user;
    if (fixture != NULL)
        atomic_store_explicit(&fixture->closed, true, memory_order_release);
}

static bool is_quiescent(void *user) {
    const invoke_fixture_state *fixture = (const invoke_fixture_state *)user;
    return fixture != NULL &&
           atomic_load_explicit(&fixture->closed, memory_order_acquire) &&
           !fixture->start_reserved && !fixture->cancel_reserved;
}

CMETA_IMPLEMENTS(scxml_invoke_provider, invoke_impl,
    SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
    .prepare_start = prepare_start,
    .prepare_cancel = prepare_cancel,
    .prepare_forward = reject_forward,
    .close = close_provider,
    .is_quiescent = is_quiescent);

/* CNet stores these function pointers in its observer. Their *instructions*
 * reside in this very DSO; the existing ComponentPlugin Scope pins module
 * code until CNet's authoritative native CLOSED/FAILED terminal is observed.
 * The callback is invoked only by the single CNet client Owner lane.
 * This is test-only protocol projection, not a second NativeIO owner. */
static bool native_connection_matches(invoke_fixture_state *fixture,
                                      cnet_connection connection) {
    if (!fixture->native_bound) {
        fixture->native_connection = connection;
        fixture->native_bound = true;
        return true;
    }
    return fixture->native_connection.slot == connection.slot &&
        fixture->native_connection.generation == connection.generation;
}

/* The two-thread test pauses INSIDE actual DSO instructions invoked by
 * CNet's owner lane. Only C11 atomics are touched by the foreign unload
 * thread: all provider and native state remains owner-only. */
static void callback_gate_wait(scxml_test_cnet_callback_gate **selected) {
    scxml_test_cnet_callback_gate *gate;
    uint64_t deadline;
    if (selected == NULL || *selected == NULL) return;
    gate = *selected;
    deadline = cmeta_monotonic_ms() + UINT64_C(5000);
    atomic_store_explicit(&gate->entered, 1, memory_order_release);
    while (!atomic_load_explicit(&gate->release, memory_order_acquire) &&
           cmeta_monotonic_ms() < deadline) {
    }
    if (!atomic_load_explicit(&gate->release, memory_order_acquire))
        atomic_store_explicit(&gate->timed_out, 1, memory_order_release);
    *selected = NULL; /* Host may retire the gate only after callback exit. */
}

static void dso_native_state(
    void *user, cnet_connection connection,
    cnet_connection_state value, const cnet_error *error) {
    invoke_fixture_state *fixture = (invoke_fixture_state *)user;
    (void)error;
    if (fixture == NULL) return;
    if (!native_connection_matches(fixture, connection)) {
        ++fixture->stale_callbacks;
        return;
    }
    if (value == CNET_CONNECTION_CLOSED || value == CNET_CONNECTION_FAILED)
        callback_gate_wait(&fixture->terminal_gate);
    ++fixture->native_state_callbacks;
    if (value == CNET_CONNECTION_CONNECTED) {
        fixture->native_connected = true;
    } else if (value == CNET_CONNECTION_CLOSING) {
        fixture->native_connected = false;
    } else if (value == CNET_CONNECTION_CLOSED ||
               value == CNET_CONNECTION_FAILED) {
        fixture->native_connected = false;
        fixture->native_terminal = true;
    }
}

static void dso_native_send(
    void *user, cnet_connection connection, size_t bytes) {
    invoke_fixture_state *fixture = (invoke_fixture_state *)user;
    if (fixture == NULL) return;
    if (!native_connection_matches(fixture, connection) ||
        fixture->native_terminal) {
        ++fixture->stale_callbacks;
        return;
    }
    callback_gate_wait(&fixture->send_gate);
    ++fixture->native_sends;
    fixture->native_send_bytes += bytes;
}

static bool probe_get_observer(void *self, cnet_observer *out) {
    invoke_fixture_state *fixture = (invoke_fixture_state *)self;
    if (out != NULL) *out = (cnet_observer){0};
    if (fixture == NULL || out == NULL || fixture->native_bound)
        return false;
    *out = (cnet_observer){
        .on_state = dso_native_state,
        .on_send = dso_native_send,
        .user = fixture
    };
    return true;
}

static bool probe_snapshot(void *self,
                           scxml_test_cnet_observer_stats *out) {
    const invoke_fixture_state *fixture =
        (const invoke_fixture_state *)self;
    if (out != NULL) *out = (scxml_test_cnet_observer_stats){0};
    if (fixture == NULL || out == NULL) return false;
    *out = (scxml_test_cnet_observer_stats){
        .invoke_token = fixture->active_token,
        .marker = fixture->marker,
        .native_sends = fixture->native_sends,
        .native_send_bytes = fixture->native_send_bytes,
        .native_state_callbacks = fixture->native_state_callbacks,
        .stale_callbacks = fixture->stale_callbacks,
        .native_connected = fixture->native_connected,
        .native_terminal = fixture->native_terminal,
        .invoke_closed = atomic_load_explicit(
            &fixture->closed, memory_order_acquire)
    };
    return true;
}

static bool probe_arm_send_gate(
    void *self, scxml_test_cnet_callback_gate *gate) {
    invoke_fixture_state *fixture = (invoke_fixture_state *)self;
    if (fixture == NULL || gate == NULL || !fixture->native_connected ||
        fixture->native_terminal || fixture->send_gate != NULL)
        return false;
    fixture->send_gate = gate;
    return true;
}

static bool probe_arm_terminal_gate(
    void *self, scxml_test_cnet_callback_gate *gate) {
    invoke_fixture_state *fixture = (invoke_fixture_state *)self;
    if (fixture == NULL || gate == NULL || !fixture->native_connected ||
        fixture->native_terminal || fixture->terminal_gate != NULL)
        return false;
    fixture->terminal_gate = gate;
    return true;
}

static bool probe_arm_cancel_gate(
    void *self, scxml_test_cnet_callback_gate *gate) {
    invoke_fixture_state *fixture = (invoke_fixture_state *)self;
    if (fixture == NULL || gate == NULL ||
        atomic_load_explicit(&fixture->closed, memory_order_acquire) ||
        fixture->active_token == 0u || fixture->cancel_reserved ||
        fixture->cancel_gate != NULL)
        return false;
    fixture->cancel_gate = gate;
    return true;
}

CMETA_IMPLEMENTS(scxml_test_cnet_probe, probe_impl, 0u,
    .get_observer = probe_get_observer,
    .snapshot = probe_snapshot,
    .arm_send_gate = probe_arm_send_gate,
    .arm_terminal_gate = probe_arm_terminal_gate,
    .arm_cancel_gate = probe_arm_cancel_gate);

static cmeta_status project(
    void *context, const cmeta_object_ref *object,
    const cmeta_interface_desc *expected, cmeta_interface_projection *out) {
    (void)context;
    if (object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (cmeta_interface_desc_equal(
            expected, scxml_invoke_provider_interface())) {
        *out = (cmeta_interface_projection){
            sizeof(*out), scxml_invoke_provider_interface(),
            &state, &invoke_impl_vtable
        };
    } else if (cmeta_interface_desc_equal(
            expected, scxml_test_cnet_probe_interface())) {
        *out = (cmeta_interface_projection){
            sizeof(*out), scxml_test_cnet_probe_interface(),
            &state, &probe_impl_vtable
        };
    } else {
        return CMETA_TRAIT_MISSING;
    }
    return CMETA_OK;
}

static const cmeta_object_interface_provider interfaces = {
    sizeof(cmeta_object_interface_provider), NULL, project
};

static cmeta_status SALTS_COMPONENT_CALL create(
    void *context, const cmeta_data_desc *config_data, const void *config_value,
    const salts_component_dependency *dependencies, size_t dependency_count,
    cmeta_object_ref *out) {
    (void)dependencies;
    if (context == NULL || config_data != NULL || config_value != NULL ||
        dependency_count != 0u)
        return CMETA_INVALID_ARGUMENT;
    return cmeta_object_borrow(out, context, &cmeta_data_int, NULL);
}

static const salts_component_provider_binding binding = {
    sizeof(salts_component_provider_binding),
    SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION,
    cmeta_component_meta(ScxmlInvokeDsoFixture), &state.marker,
    &interfaces, create, NULL, NULL
};

static const salts_component_provider_binding *get_binding(void *self) {
    return (const salts_component_provider_binding *)self;
}

CMETA_IMPLEMENTS(salts_component_provider, provider_impl, 0u,
    .get_binding = get_binding);

static salts_component_provider provider = {
    (void *)&binding, &provider_impl_vtable
};

#define SCXML_INVOKE_DSO_EXPORTS(X) \
    X(interface, (salts_component_provider, &provider), "component-provider", \
      SALTS_COMPONENT_PROVIDER_CONTRACT_ID, \
      SALTS_COMPONENT_PROVIDER_CONTRACT_VERSION, 0)

CMETA_PLUGIN_DECLARE(scxml_invoke_component_fixture,
    SCXML_INVOKE_DSO_ID, (1,0,0), SCXML_INVOKE_DSO_EXPORTS,
    CMETA_PLUGIN_PASSIVE());
