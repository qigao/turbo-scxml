#include "tinytest.h"
#include "scxml_component_interceptor_probe.h"
#include <scxml/component.h>
#include <salts/plugin.h>

#include <string.h>

enum { GENERATION_CAPACITY = 3, SESSION_CAPACITY = 2 };

typedef struct dso_generation {
    salts_component_plugin_generation generation;
    salts_component_deployment deployments[1];
    salts_component_instance instances[1];
    salts_component_dependency dependencies[1];
    size_t activation_order[1];
    salts_component_plugin_module modules[1];
} dso_generation;

typedef struct dso_test {
    cmeta_plugin_registry registry;
    bool registry_live;
    cmeta_plugin_ref plugins[SESSION_CAPACITY];
    salts_component_plugin_runtime runtime;
    dso_generation generations[GENERATION_CAPACITY];
    scxml_component_scope scopes[SESSION_CAPACITY];
    scxml_component_event_io_provider providers[SESSION_CAPACITY];
    scxml_program program;
    scxml_session sessions[SESSION_CAPACITY];
    cflow_executor executors[SESSION_CAPACITY];
    bool executor_live[SESSION_CAPACITY];
} dso_test;

static salts_component_plugin_status build_generation(
    dso_test *test, size_t slot, size_t plugin, uint64_t id) {
    dso_generation *g = &test->generations[slot];
    const salts_component_plugin_generation_storage storage = {
        g->deployments, 1u, g->instances, 1u, g->dependencies, 1u,
        g->activation_order, 1u, g->modules, 1u};
    const salts_component_plugin_source source = {
        test->plugins[plugin], "component-provider", NULL, NULL};
    return salts_component_plugin_generation_build(
        &g->generation, id, &test->registry, &storage,
        NULL, 0u, &source, 1u, NULL, 0u);
}

static bool cleanup(dso_test *test) {
    bool ok = true;
    size_t i;
    for (i = 0u; i < SESSION_CAPACITY; ++i) {
        if (test->sessions[i].impl != NULL) {
            scxml_session_cancel(&test->sessions[i]);
            ok = cflow_executor_wait_idle(&test->executors[i]) && ok;
            ok = scxml_session_destroy(&test->sessions[i]) ==
                CFLOW_STATECHART_INSTANCE_OK && ok;
        }
        if (test->executor_live[i])
            cflow_executor_destroy(&test->executors[i]);
        if (test->providers[i].live)
            ok = scxml_component_event_io_provider_destroy(
                &test->providers[i]) == SCXML_COMPONENT_OK && ok;
        if (test->scopes[i].live)
            ok = scxml_component_scope_release(
                &test->scopes[i]) == SCXML_COMPONENT_OK && ok;
    }
    scxml_program_destroy(&test->program);
    if (test->runtime.initialized && test->runtime.current != NULL) {
        salts_component_plugin_generation *previous = NULL;
        ok = salts_component_plugin_runtime_close(
            &test->runtime, &previous) == SALTS_COMPONENT_PLUGIN_OK && ok;
    }
    for (i = 0u; i < GENERATION_CAPACITY; ++i) {
        salts_component_plugin_generation *g = &test->generations[i].generation;
        if (g->state == SALTS_COMPONENT_PLUGIN_GENERATION_DRAINING)
            ok = salts_component_plugin_generation_drain(
                &test->runtime, g) == SALTS_COMPONENT_PLUGIN_OK && ok;
        else if (g->state == SALTS_COMPONENT_PLUGIN_GENERATION_BUILT)
            ok = salts_component_plugin_generation_discard(g) ==
                SALTS_COMPONENT_PLUGIN_OK && ok;
    }
    if (test->runtime.initialized)
        ok = salts_component_plugin_runtime_destroy(&test->runtime) ==
            SALTS_COMPONENT_PLUGIN_OK && ok;
    if (test->registry_live) {
        for (i = 0u; i < SESSION_CAPACITY; ++i) {
            bool quiescent = false;
            if (test->plugins[i].generation == 0u) continue;
            ok = cmeta_plugin_registry_request_stop(
                &test->registry, test->plugins[i]) == CMETA_PLUGIN_OK && ok;
            ok = cmeta_plugin_registry_poll_quiescent(
                &test->registry, test->plugins[i], &quiescent) ==
                CMETA_PLUGIN_OK && quiescent && ok;
            ok = cmeta_plugin_registry_unload(
                &test->registry, test->plugins[i]) == CMETA_PLUGIN_OK && ok;
        }
        ok = cmeta_plugin_registry_destroy(&test->registry) ==
            CMETA_PLUGIN_OK && ok;
    }
    return ok;
}

static int marker_value(const scxml_component_scope *scope) {
    salts_component_service service = {0};
    if (salts_component_plugin_scope_find_service_from(
            &scope->component_scope, "ScxmlDsoFixture",
            scxml_event_io_provider_interface(), &service) !=
            SALTS_COMPONENT_PLUGIN_OK || service.object == NULL ||
        !cmeta_data_desc_equal(service.object->data, &cmeta_data_int))
        return -1;
    return *(const int *)service.object->object;
}

static bool fire(dso_test *test, size_t slot, const char *event) {
    const scxml_event_metadata metadata = {
        .abi_version = SCXML_EVENT_METADATA_ABI,
        .struct_size = sizeof(scxml_event_metadata)};
    return scxml_session_try_send_named_with_metadata(
        &test->sessions[slot], event, strlen(event), &metadata) ==
        CFLOW_MAILBOX_OK && cflow_executor_wait_idle(&test->executors[slot]);
}

spec("Component DSO-backed SCXML sessions") {
    static dso_test test;

    before_each() { memset(&test, 0, sizeof(test)); }
    after_each() { check_true(cleanup(&test)); }

    it("retains real DSO FunctionAbi and ticket authority through scoped Interceptor calls") {
        const char *paths[] = {SCXML_COMPONENT_DSO_ONE, SCXML_COMPONENT_DSO_TWO};
        const cmeta_plugin_registry_config registry_config = {.capacity = 2u};
        const scxml_send_request probe_request = {
            .event = "probe", .event_size = 5u
        };
        const scxml_send_request wrong_request = {
            .event = "wrong", .event_size = 5u
        };
        const cmeta_function_abi_desc *expected =
            &scxml_intercept_target_contract__function_abi_meta;
        cmeta_function_abi_desc provided = *expected;
        cmeta_function_desc wrong_result = *expected->function;
        scxml_component_invoke_provider denied_invoke = {0};
        scxml_component_event_io_provider denied_event = {0};
        scxml_intercept_probe old = {0}, next = {0};
        salts_component_plugin_generation *previous = NULL;
        const scxml_event_io_adapter *adapter;
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;

        check_equal(cmeta_plugin_registry_init(
            &test.registry, &registry_config), CMETA_PLUGIN_OK);
        test.registry_live = true;
        for (size_t i = 0u; i < SESSION_CAPACITY; ++i) {
            check_equal(cmeta_plugin_registry_load(
                &test.registry, paths[i], &test.plugins[i]), CMETA_PLUGIN_OK);
            check_equal(cmeta_plugin_registry_start(
                &test.registry, test.plugins[i]), CMETA_PLUGIN_OK);
            check_equal(build_generation(&test, i, i, (uint64_t)i + 1u),
                        SALTS_COMPONENT_PLUGIN_OK);
        }
        check_equal(salts_component_plugin_runtime_init(&test.runtime),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_publish(
            &test.runtime, &test.generations[0].generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_null(previous);
        check_equal(scxml_component_scope_acquire(
            &test.scopes[0], &test.runtime), SCXML_COMPONENT_OK);

        /* Real Plugin DSO supplies only canonical Event I/O and SEND.
           Wrong Interface or capability fails BEFORE Session publication. */
        check_equal(scxml_component_invoke_provider_bind(
            &denied_invoke, &test.scopes[0], "ScxmlDsoFixture",
            SCXML_INVOKE_CAP_START), SCXML_COMPONENT_UNAVAILABLE);
        check_equal(scxml_component_event_io_provider_bind(
            &denied_event, &test.scopes[0], "ScxmlDsoFixture",
            SCXML_EVENT_IO_CAP_CANCEL),
            SCXML_COMPONENT_CAPABILITY_MISMATCH);
        check_equal(test.scopes[0].binding_count, (size_t)0u);
        check_false(denied_invoke.live);
        check_false(denied_event.live);

        check_equal(scxml_component_event_io_provider_bind(
            &test.providers[0], &test.scopes[0], "ScxmlDsoFixture",
            SCXML_EVENT_IO_CAP_SEND), SCXML_COMPONENT_OK);
        adapter = scxml_component_event_io_provider_adapter(&test.providers[0]);
        check_not_null(adapter);
        check_equal(scxml_intercept_probe_init(
            &old, adapter,
            scxml_component_event_io_provider_user(&test.providers[0])),
            CMETA_OK);
        check_true(cmeta_function_abi_desc_valid(expected));
        wrong_result.result_flags = CMETA_RESULT_OWNED;
        provided.function = &wrong_result;
        check_equal(scxml_test_send_intercept_admit(
            &old.chain, &old, scxml_intercept_target,
            old.hooks, 2u, expected, &provided), CMETA_TYPE_MISMATCH);
        check_true(old.chain.target == scxml_intercept_target);
        check_equal(marker_value(&test.scopes[0]), 100);

        /* Actual DSO prepare_send creates the ticket, but the Interceptor
           only TRANSFERS it on ACCEPTED; execution has no effect yet. */
        check_equal(scxml_intercept_prepare_send(
            &old, &probe_request, &ticket, &error), SCXML_ADAPTER_ACCEPTED);
        check_not_null(ticket.commit);
        check_not_null(ticket.discard);
        check_null(error);
        check_equal(marker_value(&test.scopes[0]), 100);
        check_equal(old.trace_count, (size_t)5u);
        check_equal(old.trace[0], 11u);
        check_equal(old.trace[1], 12u);
        check_equal(old.trace[2], 9u);
        check_equal(old.trace[3], 22u);
        check_equal(old.trace[4], 21u);
        ticket.commit(ticket.user);
        ticket = (cflow_statechart_effect_ticket){0};
        check_equal(marker_value(&test.scopes[0]), 101);

        old.reject_stage = 2u;
        scxml_intercept_probe_reset_trace(&old);
        check_equal(scxml_intercept_prepare_send(
            &old, &probe_request, &ticket, &error),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_null(ticket.commit);
        check_null(ticket.discard);
        check_equal(old.target_calls, 1u);
        check_equal(marker_value(&test.scopes[0]), 101);
        check_equal(old.trace_count, (size_t)4u);
        check_equal(old.trace[2], 32u);
        check_equal(old.trace[3], 31u);
        old.reject_stage = 0u;

        /* Real DSO rejects an unsupported event without issuing a ticket. */
        check_equal(scxml_intercept_prepare_send(
            &old, &wrong_request, &ticket, &error),
            SCXML_ADAPTER_INVALID_CONTRACT);
        check_null(ticket.commit);
        check_null(ticket.discard);
        check_equal(marker_value(&test.scopes[0]), 101);

        /* Deliberate post-provider failure must invoke the REAL DSO
           discard callback exactly once, never commit or transfer ticket. */
        old.fail_after_provider = true;
        check_equal(scxml_intercept_prepare_send(
            &old, &probe_request, &ticket, &error),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_null(ticket.commit);
        check_equal(old.discarded_on_failure, 1u);
        check_equal(marker_value(&test.scopes[0]), 1101);
        old.fail_after_provider = false;

        /* Publication makes gN DRAINING but does not transfer its borrow.
           Intercepted real DSO callback remains callable under gN Scope. */
        check_equal(salts_component_plugin_runtime_publish(
            &test.runtime, &test.generations[1].generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &test.generations[0].generation);
        check_equal(salts_component_plugin_generation_drain(
            &test.runtime, &test.generations[0].generation),
            SALTS_COMPONENT_PLUGIN_BUSY);
        check_equal(cmeta_plugin_registry_unload(
            &test.registry, test.plugins[0]), CMETA_PLUGIN_BUSY);
        check_equal(scxml_intercept_prepare_send(
            &old, &probe_request, &ticket, &error), SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        ticket = (cflow_statechart_effect_ticket){0};
        check_equal(marker_value(&test.scopes[0]), 1102);

        check_equal(scxml_component_scope_acquire(
            &test.scopes[1], &test.runtime), SCXML_COMPONENT_OK);
        check_equal(scxml_component_event_io_provider_bind(
            &test.providers[1], &test.scopes[1], "ScxmlDsoFixture",
            SCXML_EVENT_IO_CAP_SEND), SCXML_COMPONENT_OK);
        check_equal(scxml_intercept_probe_init(
            &next, scxml_component_event_io_provider_adapter(&test.providers[1]),
            scxml_component_event_io_provider_user(&test.providers[1])),
            CMETA_OK);
        check_equal(marker_value(&test.scopes[1]), 200);
        check_equal(scxml_intercept_prepare_send(
            &next, &probe_request, &ticket, &error), SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        ticket = (cflow_statechart_effect_ticket){0};
        check_equal(marker_value(&test.scopes[1]), 201);

        /* Only the external Component lease authorizes safe old DSO unload.
           An interceptor hook pointer is never used after its Scope retires. */
        adapter->close(scxml_component_event_io_provider_user(
            &test.providers[0]));
        check_equal(scxml_component_event_io_provider_destroy(
            &test.providers[0]), SCXML_COMPONENT_OK);
        check_equal(scxml_component_scope_release(
            &test.scopes[0]), SCXML_COMPONENT_OK);
        check_equal(salts_component_plugin_generation_drain(
            &test.runtime, &test.generations[0].generation),
            SALTS_COMPONENT_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_request_stop(
            &test.registry, test.plugins[0]), CMETA_PLUGIN_OK);
        {
            bool quiescent = false;
            check_equal(cmeta_plugin_registry_poll_quiescent(
                &test.registry, test.plugins[0], &quiescent), CMETA_PLUGIN_OK);
            check_true(quiescent);
        }
        check_equal(cmeta_plugin_registry_unload(
            &test.registry, test.plugins[0]), CMETA_PLUGIN_OK);
        test.plugins[0] = (cmeta_plugin_ref){0};

        /* gN+1 callback and its own independent ticket still execute
           after old DSO code/metadata are fully unloaded. */
        check_equal(scxml_intercept_prepare_send(
            &next, &probe_request, &ticket, &error), SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(marker_value(&test.scopes[1]), 202);
    }

    it("keeps old session effects in the old DSO until session and scope retirement") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' initial='running'>"
            "<state id='running'>"
            "<transition event='fire'><send event='probe' target='receiver'/></transition>"
            "<transition event='finish' target='done'/>"
            "</state><final id='done'/></scxml>";
        const char *paths[] = {SCXML_COMPONENT_DSO_ONE, SCXML_COMPONENT_DSO_TWO};
        const cmeta_plugin_registry_config registry_config = {.capacity = 2u};
        scxml_diagnostic diagnostic = {0};
        salts_component_plugin_generation *previous = NULL;
        cflow_statechart_instance_stats stats = {0};
        cmeta_plugin_lifecycle_info lifecycle = {0};
        scxml_component_scope rejected = {0};
        size_t i;

        check_equal(cmeta_plugin_registry_init(&test.registry, &registry_config),
                    CMETA_PLUGIN_OK);
        test.registry_live = true;
        for (i = 0u; i < SESSION_CAPACITY; ++i) {
            check_equal(cmeta_plugin_registry_load(
                            &test.registry, paths[i], &test.plugins[i]),
                        CMETA_PLUGIN_OK);
            check_equal(cmeta_plugin_registry_start(
                            &test.registry, test.plugins[i]), CMETA_PLUGIN_OK);
            check_equal(build_generation(&test, i, i, (uint64_t)i + 1u),
                        SALTS_COMPONENT_PLUGIN_OK);
        }
        check_equal(salts_component_plugin_runtime_init(&test.runtime),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(scxml_compile(&test.program, source, sizeof(source) - 1u,
                                  NULL, &diagnostic), SCXML_OK);

        for (i = 0u; i < SESSION_CAPACITY; ++i) {
            scxml_session_config config;
            check_equal(salts_component_plugin_runtime_publish(
                            &test.runtime, &test.generations[i].generation,
                            &previous), SALTS_COMPONENT_PLUGIN_OK);
            if (i == 0u) check_null(previous);
            else check_true(previous == &test.generations[0].generation);
            check_equal(scxml_component_scope_acquire(
                            &test.scopes[i], &test.runtime), SCXML_COMPONENT_OK);
            check_equal(scxml_component_event_io_provider_bind(
                            &test.providers[i], &test.scopes[i], "ScxmlDsoFixture",
                            SCXML_EVENT_IO_CAP_SEND), SCXML_COMPONENT_OK);
            check_true(cflow_executor_serial_init(&test.executors[i]));
            test.executor_live[i] = true;
            config = (scxml_session_config){
                .program = &test.program, .executor = &test.executors[i],
                .external_event_capacity = 2u, .internal_event_capacity = 2u,
                .completion_capacity = 2u, .microstep_limit = 16u,
                .effect_capacity = 2u, .adapter_internal_event_capacity = 2u,
                .event_io = scxml_component_event_io_provider_adapter(&test.providers[i]),
                .adapter_user = scxml_component_event_io_provider_user(&test.providers[i])};
            check_equal(scxml_session_init(&test.sessions[i], &config),
                        CFLOW_STATECHART_INSTANCE_OK);
            check_true(fire(&test, i, "fire"));
            check_equal(marker_value(&test.scopes[i]), i == 0u ? 101 : 201);
        }
        /* Runtime publication does not migrate a live session's adapters. */
        check_true(fire(&test, 0u, "fire"));
        check_equal(marker_value(&test.scopes[0]), 102);
        check_equal(marker_value(&test.scopes[1]), 201);
        check_equal(salts_component_plugin_generation_drain(
                        &test.runtime, &test.generations[0].generation),
                    SALTS_COMPONENT_PLUGIN_BUSY);
        check_equal(cmeta_plugin_registry_unload(&test.registry, test.plugins[0]),
                    CMETA_PLUGIN_BUSY);
        check_equal(scxml_component_scope_release(&test.scopes[0]),
                    SCXML_COMPONENT_BUSY);

        check_equal(build_generation(&test, 2u, 1u, UINT64_C(3)),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_publish(
                        &test.runtime, &test.generations[2].generation, &previous),
                    SALTS_COMPONENT_PLUGIN_BUSY);
        check_equal(salts_component_plugin_generation_discard(
                        &test.generations[2].generation), SALTS_COMPONENT_PLUGIN_OK);

        check_equal(salts_component_plugin_runtime_close(&test.runtime, &previous),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(scxml_component_scope_acquire(&rejected, &test.runtime),
                    SCXML_COMPONENT_ERROR);
        check_false(rejected.live);
        check_true(fire(&test, 0u, "finish"));
        check_true(scxml_session_get_stats(&test.sessions[0], &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(scxml_session_destroy(&test.sessions[0]),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(marker_value(&test.scopes[0]), 112);
        check_equal(scxml_component_event_io_provider_destroy(&test.providers[0]),
                    SCXML_COMPONENT_OK);
        check_equal(scxml_component_scope_release(&test.scopes[0]),
                    SCXML_COMPONENT_OK);
        check_equal(salts_component_plugin_generation_drain(
                        &test.runtime, &test.generations[0].generation),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_get_lifecycle(
                        &test.registry, test.plugins[0], &lifecycle), CMETA_PLUGIN_OK);
        check_equal(lifecycle.active_leases, (size_t)0u);
        /* The second session can finish after closing Component admission. */
        check_true(fire(&test, 1u, "finish"));
        check_equal(scxml_session_destroy(&test.sessions[1]),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(marker_value(&test.scopes[1]), 211);
    }
}
