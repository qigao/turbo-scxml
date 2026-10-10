#include "tinytest.h"
#include "scxml_component_interceptor_probe.h"

#include <scxml/component.h>

#include <string.h>

#define SCXML_COMPONENT_FIXTURE_ID "ScxmlComponentFixture"

cmeta_component(ScxmlComponentFixture,
    cmeta_provides(scxml_event_io_provider)
    cmeta_provides(scxml_invoke_provider));

typedef struct fixture_state {
    size_t event_close_count;
    size_t invoke_close_count;
    int anchor;
    cmeta_object_interface_provider interfaces;
    salts_component_provider_binding binding;
    salts_component_deployment deployment;
} fixture_state;

typedef struct generation_fixture {
    salts_component_plugin_generation generation;
    salts_component_deployment deployments[1];
    salts_component_instance instances[1];
    salts_component_dependency dependencies[1];
    size_t activation_order[1];
    salts_component_plugin_module modules[1];
} generation_fixture;

static scxml_adapter_status event_send(
    void *self,
    const scxml_send_request *request,
    cflow_statechart_effect_ticket *ticket,
    const char **error) {
    (void)self;
    (void)request;
    (void)ticket;
    if (error != NULL) *error = "component-event";
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static scxml_adapter_status event_cancel(
    void *self,
    const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *ticket,
    const char **error) {
    (void)self;
    (void)request;
    (void)ticket;
    if (error != NULL) *error = "component-event";
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static void event_close(void *self) {
    fixture_state *state = (fixture_state *)self;
    if (state != NULL) ++state->event_close_count;
}

static bool event_quiescent(void *self) {
    return self != NULL;
}

static scxml_adapter_status invoke_start(
    void *self,
    const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *ticket,
    const char **error) {
    (void)self;
    (void)request;
    (void)ticket;
    if (error != NULL) *error = "component-invoke";
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static scxml_adapter_status invoke_cancel(
    void *self,
    const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *ticket,
    const char **error) {
    (void)self;
    (void)request;
    (void)ticket;
    if (error != NULL) *error = "component-invoke";
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static scxml_adapter_status invoke_forward(
    void *self,
    const scxml_invoke_forward_request *request,
    cflow_statechart_effect_ticket *ticket,
    const char **error) {
    (void)self;
    (void)request;
    (void)ticket;
    if (error != NULL) *error = "component-invoke";
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static void invoke_close(void *self) {
    fixture_state *state = (fixture_state *)self;
    if (state != NULL) ++state->invoke_close_count;
}

static bool invoke_quiescent(void *self) {
    return self != NULL;
}

static const scxml_event_io_provider_vtable event_vtable = {
    .implementation = "component_event",
    .capabilities = SCXML_EVENT_IO_CAP_SEND |
                    SCXML_EVENT_IO_CAP_DELAYED_SEND |
                    SCXML_EVENT_IO_CAP_CANCEL,
    .prepare_send = event_send,
    .prepare_cancel = event_cancel,
    .close = event_close,
    .is_quiescent = event_quiescent
};

static const scxml_invoke_provider_vtable invoke_vtable = {
    .implementation = "component_invoke",
    .capabilities = SCXML_INVOKE_CAP_START |
                    SCXML_INVOKE_CAP_CANCEL |
                    SCXML_INVOKE_CAP_FORWARD,
    .prepare_start = invoke_start,
    .prepare_cancel = invoke_cancel,
    .prepare_forward = invoke_forward,
    .close = invoke_close,
    .is_quiescent = invoke_quiescent
};

static cmeta_status fixture_project(
    void *context,
    const cmeta_object_ref *object,
    const cmeta_interface_desc *expected,
    cmeta_interface_projection *out) {
    fixture_state *state = (fixture_state *)context;
    if (state == NULL || object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;

    if (cmeta_interface_desc_equal(
            expected, scxml_event_io_provider_interface())) {
        out->size = sizeof(*out);
        out->interface = scxml_event_io_provider_interface();
        out->self = state;
        out->dispatch = &event_vtable;
        return CMETA_OK;
    }

    if (cmeta_interface_desc_equal(
            expected, scxml_invoke_provider_interface())) {
        out->size = sizeof(*out);
        out->interface = scxml_invoke_provider_interface();
        out->self = state;
        out->dispatch = &invoke_vtable;
        return CMETA_OK;
    }

    return CMETA_TRAIT_MISSING;
}

static cmeta_status SALTS_COMPONENT_CALL fixture_create(
    void *provider_context,
    const cmeta_data_desc *config_data,
    const void *config_value,
    const salts_component_dependency *dependencies,
    size_t dependency_count,
    cmeta_object_ref *out_instance) {
    fixture_state *state = (fixture_state *)provider_context;
    (void)dependencies;
    if (state == NULL || config_data != NULL || config_value != NULL ||
        dependency_count != 0u || out_instance == NULL)
        return CMETA_INVALID_ARGUMENT;
    return cmeta_object_borrow(
        out_instance, &state->anchor, &cmeta_data_int, NULL);
}

static void fixture_init(fixture_state *state, int anchor) {
    memset(state, 0, sizeof(*state));
    state->anchor = anchor;
    state->interfaces =
        (cmeta_object_interface_provider){
            sizeof(cmeta_object_interface_provider),
            state,
            fixture_project};
    state->binding =
        (salts_component_provider_binding){
            sizeof(salts_component_provider_binding),
            SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION,
            cmeta_component_meta(ScxmlComponentFixture),
            state,
            &state->interfaces,
            fixture_create,
            NULL,
            NULL};
    state->deployment =
        (salts_component_deployment){
            &state->binding,
            NULL,
            NULL};
}

static int generation_build(
    generation_fixture *fixture,
    fixture_state *state,
    uint64_t generation_id) {
    const salts_component_plugin_generation_storage storage = {
        fixture->deployments, 1u,
        fixture->instances, 1u,
        fixture->dependencies, 1u,
        fixture->activation_order, 1u,
        fixture->modules, 1u};

    memset(fixture, 0, sizeof(*fixture));
    fixture->deployments[0] = state->deployment;

    return salts_component_plugin_generation_build(
        &fixture->generation,
        generation_id,
        NULL,
        &storage,
        fixture->deployments, 1u,
        NULL, 0u,
        NULL, 0u);
}

spec("TurboSCXML Component provider scope") {
    it("pins Event I/O and Invoke providers to one Component generation") {
        fixture_state state1;
        fixture_state state2;
        generation_fixture g1 = {0};
        generation_fixture g2 = {0};
        salts_component_plugin_runtime runtime = {0};
        salts_component_plugin_generation *previous = NULL;
        scxml_component_scope scope1 = {0};
        scxml_component_scope scope2 = {0};
        scxml_component_event_io_provider event1 = {0};
        scxml_component_invoke_provider invoke1 = {0};
        scxml_component_event_io_provider event2 = {0};
        const scxml_event_io_adapter *event_adapter;
        const scxml_invoke_adapter *invoke_adapter;
        scxml_send_request send = {0};
        scxml_invoke_start_request start = {0};
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;

        fixture_init(&state1, 1);
        fixture_init(&state2, 2);

        check_equal(generation_build(&g1, &state1, UINT64_C(11)),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(generation_build(&g2, &state2, UINT64_C(12)),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_init(&runtime),
                    SALTS_COMPONENT_PLUGIN_OK);

        check_equal(salts_component_plugin_runtime_publish(
                        &runtime, &g1.generation, &previous),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_null(previous);

        check_equal(scxml_component_scope_acquire(&scope1, &runtime),
                    SCXML_COMPONENT_OK);
        check_equal(scxml_component_scope_generation_id(&scope1),
                    UINT64_C(11));

        check_equal(scxml_component_event_io_provider_bind(
                        &event1, &scope1, SCXML_COMPONENT_FIXTURE_ID,
                        SCXML_EVENT_IO_CAP_SEND),
                    SCXML_COMPONENT_OK);
        check_equal(scxml_component_invoke_provider_bind(
                        &invoke1, &scope1, SCXML_COMPONENT_FIXTURE_ID,
                        SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL),
                    SCXML_COMPONENT_OK);

        check_equal(scxml_component_scope_release(&scope1),
                    SCXML_COMPONENT_BUSY);

        event_adapter =
            scxml_component_event_io_provider_adapter(&event1);
        invoke_adapter =
            scxml_component_invoke_provider_adapter(&invoke1);
        check_not_null(event_adapter);
        check_not_null(invoke_adapter);
        check_equal(event_adapter->prepare_send(
                        scxml_component_event_io_provider_user(&event1),
                        &send, &ticket, &error),
                    SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(invoke_adapter->prepare_start(
                        scxml_component_invoke_provider_user(&invoke1),
                        &start, &ticket, &error),
                    SCXML_ADAPTER_ERROR_EXECUTION);

        check_equal(salts_component_plugin_runtime_publish(
                        &runtime, &g2.generation, &previous),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &g1.generation);

        check_equal(scxml_component_scope_acquire(&scope2, &runtime),
                    SCXML_COMPONENT_OK);
        check_equal(scxml_component_scope_generation_id(&scope2),
                    UINT64_C(12));
        check_equal(scxml_component_event_io_provider_bind(
                        &event2, &scope2, SCXML_COMPONENT_FIXTURE_ID,
                        SCXML_EVENT_IO_CAP_SEND),
                    SCXML_COMPONENT_OK);

        /* Existing provider/session-facing handles remain pinned to g1. */
        check_equal(scxml_component_scope_generation_id(&scope1),
                    UINT64_C(11));
        check_equal(salts_component_plugin_generation_drain(
                        &runtime, &g1.generation),
                    SALTS_COMPONENT_PLUGIN_BUSY);

        event_adapter->close(
            scxml_component_event_io_provider_user(&event1));
        invoke_adapter->close(
            scxml_component_invoke_provider_user(&invoke1));
        check_equal(state1.event_close_count, (size_t)1u);
        check_equal(state1.invoke_close_count, (size_t)1u);

        check_equal(scxml_component_invoke_provider_destroy(&invoke1),
                    SCXML_COMPONENT_OK);
        check_equal(scxml_component_event_io_provider_destroy(&event1),
                    SCXML_COMPONENT_OK);
        check_equal(scxml_component_scope_release(&scope1),
                    SCXML_COMPONENT_OK);
        check_equal(salts_component_plugin_generation_drain(
                        &runtime, &g1.generation),
                    SALTS_COMPONENT_PLUGIN_OK);

        event_adapter =
            scxml_component_event_io_provider_adapter(&event2);
        check_not_null(event_adapter);
        event_adapter->close(
            scxml_component_event_io_provider_user(&event2));
        check_equal(scxml_component_event_io_provider_destroy(&event2),
                    SCXML_COMPONENT_OK);
        check_equal(scxml_component_scope_release(&scope2),
                    SCXML_COMPONENT_OK);

        check_equal(salts_component_plugin_runtime_close(
                        &runtime, &previous),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &g2.generation);
        check_equal(salts_component_plugin_generation_drain(
                        &runtime, &g2.generation),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_destroy(&runtime),
                    SALTS_COMPONENT_PLUGIN_OK);
    }

    it("admits a static Component through exact native FunctionAbi before invoking a provider") {
        fixture_state state;
        generation_fixture generation = {0};
        salts_component_plugin_runtime runtime = {0};
        salts_component_plugin_generation *previous = NULL;
        scxml_component_scope scope = {0};
        scxml_component_event_io_provider provider = {0}, denied = {0};
        const scxml_event_io_adapter *adapter;
        scxml_intercept_probe intercept = {0};
        scxml_send_request request = {.event = "probe", .event_size = 5u};
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;
        const cmeta_function_abi_desc *abi =
            &scxml_intercept_target_contract__function_abi_meta;
        cmeta_function_abi_desc wrong = *abi;
        cmeta_abi_carrier carriers[3] = {
            CMETA_ABI_OBJECT_POINTER, CMETA_ABI_OBJECT_POINTER,
            CMETA_ABI_UNSPECIFIED
        };

        fixture_init(&state, 10);
        check_equal(generation_build(&generation, &state, UINT64_C(51)),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_init(&runtime),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_publish(
            &runtime, &generation.generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_equal(scxml_component_scope_acquire(
            &scope, &runtime), SCXML_COMPONENT_OK);

        /* Wrong interface ID and impossible capabilities are rejected before
           binding or Session publication; no hidden Plugin lease is taken. */
        check_equal(scxml_component_event_io_provider_bind(
            &denied, &scope, "NoSuchComponent", SCXML_EVENT_IO_CAP_SEND),
            SCXML_COMPONENT_UNAVAILABLE);
        check_equal(scxml_component_event_io_provider_bind(
            &denied, &scope, SCXML_COMPONENT_FIXTURE_ID, UINT64_C(1) << 60),
            SCXML_COMPONENT_CAPABILITY_MISMATCH);
        check_equal(scope.binding_count, (size_t)0u);
        check_false(denied.live);

        check_equal(scxml_component_event_io_provider_bind(
            &provider, &scope, SCXML_COMPONENT_FIXTURE_ID,
            SCXML_EVENT_IO_CAP_SEND), SCXML_COMPONENT_OK);
        adapter = scxml_component_event_io_provider_adapter(&provider);
        check_not_null(adapter);
        check_equal(scxml_intercept_probe_init(
            &intercept, adapter,
            scxml_component_event_io_provider_user(&provider)), CMETA_OK);
        check_true(cmeta_function_abi_desc_valid(abi));
        wrong.param_carriers = carriers;
        check_equal(scxml_test_send_intercept_admit(
            &intercept.chain, &intercept, scxml_intercept_target,
            intercept.hooks, 2u, abi, &wrong), CMETA_TYPE_MISMATCH);
        check_true(intercept.chain.target == scxml_intercept_target);

        /* Before -> before -> target error -> reverse on_error;
           this static provider deliberately returns ERROR_EXECUTION. */
        check_equal(scxml_intercept_prepare_send(
            &intercept, &request, &ticket, &error),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(error, "component-event");
        check_null(ticket.commit);
        check_null(ticket.discard);
        check_equal(intercept.target_calls, 1u);
        check_equal(intercept.trace_count, (size_t)5u);
        check_equal(intercept.trace[0], 11u);
        check_equal(intercept.trace[1], 12u);
        check_equal(intercept.trace[2], 9u);
        check_equal(intercept.trace[3], 32u);
        check_equal(intercept.trace[4], 31u);

        /* Short circuit and failing before cannot call provider or invent
           ACCEPTED/ticket; Interceptor hooks have only borrowed contexts. */
        scxml_intercept_probe_reset_trace(&intercept);
        intercept.reject_stage = 2u;
        check_equal(scxml_intercept_prepare_send(
            &intercept, &request, &ticket, &error),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_null(ticket.commit);
        check_equal(intercept.target_calls, 1u);
        check_equal(intercept.trace_count, (size_t)4u);
        check_equal(intercept.trace[2], 32u);
        check_equal(intercept.trace[3], 31u);
        intercept.reject_stage = 0u;
        intercept.error_stage = 1u;
        scxml_intercept_probe_reset_trace(&intercept);
        check_equal(scxml_intercept_prepare_send(
            &intercept, &request, &ticket, &error),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_null(ticket.commit);
        check_equal(intercept.target_calls, 1u);
        check_equal(intercept.trace_count, (size_t)2u);
        check_equal(intercept.trace[0], 11u);
        check_equal(intercept.trace[1], 31u);
        check_equal(state.anchor, 10);

        adapter->close(scxml_component_event_io_provider_user(&provider));
        check_equal(scxml_component_event_io_provider_destroy(&provider),
                    SCXML_COMPONENT_OK);
        check_equal(scxml_component_scope_release(&scope),
                    SCXML_COMPONENT_OK);
        check_equal(salts_component_plugin_runtime_close(
            &runtime, &previous), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_generation_drain(
            &runtime, &generation.generation), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_destroy(&runtime),
                    SALTS_COMPONENT_PLUGIN_OK);
    }

    it("rejects provider capability mismatch without retaining a binding") {
        fixture_state state;
        generation_fixture generation = {0};
        salts_component_plugin_runtime runtime = {0};
        salts_component_plugin_generation *previous = NULL;
        scxml_component_scope scope = {0};
        scxml_component_event_io_provider event = {0};

        fixture_init(&state, 3);
        check_equal(generation_build(
                        &generation, &state, UINT64_C(21)),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_init(&runtime),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_publish(
                        &runtime, &generation.generation, &previous),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(scxml_component_scope_acquire(&scope, &runtime),
                    SCXML_COMPONENT_OK);

        check_equal(scxml_component_event_io_provider_bind(
                        &event, &scope, SCXML_COMPONENT_FIXTURE_ID,
                        UINT64_C(1) << 60),
                    SCXML_COMPONENT_CAPABILITY_MISMATCH);
        check_null(scxml_component_event_io_provider_adapter(&event));
        check_equal(scope.binding_count, (size_t)0u);

        check_equal(scxml_component_scope_release(&scope),
                    SCXML_COMPONENT_OK);
        check_equal(salts_component_plugin_runtime_close(
                        &runtime, &previous),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_generation_drain(
                        &runtime, &generation.generation),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_destroy(&runtime),
                    SALTS_COMPONENT_PLUGIN_OK);
    }
}
