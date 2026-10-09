#include <scxml/host_event_io.h>

#include <cflow/executor.h>
#include <tinytest.h>

#include <string.h>

static scxml_session_config make_config(
    scxml_program *program, cflow_executor *executor,
    scxml_host_event_io_binding *binding) {
    scxml_session_config config = {0};
    config.program = program;
    config.executor = executor;
    config.external_event_capacity = 2u;
    config.internal_event_capacity = 2u;
    config.completion_capacity = 2u;
    config.microstep_limit = 16u;
    config.effect_capacity = 2u;
    config.adapter_internal_event_capacity = 2u;
    config.event_io = scxml_host_event_io_binding_adapter();
    config.adapter_user = scxml_host_event_io_binding_user(binding);
    return config;
}

spec("Staged Host Session and canonical SCXML SEND composition") {
    it("holds the initial child #_parent Event until its Session is active") {
        static const char parent_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "version='1.0' initial='wait'>"
            "<state id='wait'>"
            "<transition event='reply' target='done'/></state>"
            "<final id='done'/></scxml>";
        static const char child_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "version='1.0' initial='child'>"
            "<state id='child'><onentry>"
            "<send event='reply' target='#_parent' id='reply-1'/>"
            "</onentry></state></scxml>";
        scxml_program programs[2] = {{0}, {0}};
        scxml_diagnostic diagnostics[2] = {{0}, {0}};
        cflow_executor executors[2] = {{0}, {0}};
        scxml_session sessions[2] = {{0}, {0}};
        scxml_host_event_io_binding bindings[2] = {{0}, {0}};
        scxml_host_session_ref refs[2] = {{0}, {0}};
        scxml_host_router router = {0};
        scxml_host_router_stats stats = {0};
        cflow_statechart_instance_stats machine = {0};
        scxml_session_config config;
        const scxml_event_io_adapter *adapter =
            scxml_host_event_io_binding_adapter();
        size_t delivered = 0u;

        check_not_null(adapter);
        check_equal(adapter->capabilities, SCXML_EVENT_IO_CAP_SEND);
        check_null(adapter->prepare_cancel);
        check_equal(scxml_compile(&programs[0], parent_source,
                    sizeof(parent_source) - 1u, NULL, &diagnostics[0]),
                    SCXML_OK);
        check_equal(scxml_compile(&programs[1], child_source,
                    sizeof(child_source) - 1u, NULL, &diagnostics[1]),
                    SCXML_OK);
        check_equal(scxml_host_router_init(
            &router, &(scxml_host_router_config){
                .endpoint_capacity = 2u, .event_capacity = 2u,
                .max_text_bytes = 32u, .invoke_capacity = 1u
            }), SALTS_OK);

        for (size_t i = 0u; i < 2u; ++i) {
            check_equal(scxml_host_router_reserve(&router, &refs[i]), SALTS_OK);
            check_equal(scxml_host_event_io_binding_init(
                &bindings[i], &router, refs[i]), SALTS_OK);
            check_true(cflow_executor_serial_init(&executors[i]));
        }
        check_equal(scxml_host_router_set_parent(
            &router, refs[1], refs[0]), SALTS_OK);
        check_equal(scxml_host_router_bind_invoke(
            &router, refs[0], "child", sizeof("child") - 1u, refs[1]),
            SALTS_OK);

        config = make_config(&programs[0], &executors[0], &bindings[0]);
        check_equal(scxml_session_init(&sessions[0], &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_host_router_activate(
            &router, refs[0], &sessions[0]), SALTS_OK);

        config = make_config(&programs[1], &executors[1], &bindings[1]);
        check_equal(scxml_session_init(&sessions[1], &config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executors[1]));
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.committed, UINT64_C(1));
        check_equal(stats.pending, (size_t)1u);
        /* Despite a committed effect ticket, an initial onentry Event cannot
           escape a Session before successful activation/publication. */
        check_equal(scxml_host_router_drain(&router, 1u, &delivered),
                    CFLOW_MAILBOX_FULL);
        check_equal(delivered, (size_t)0u);

        check_equal(scxml_host_router_activate(
            &router, refs[1], &sessions[1]), SALTS_OK);
        check_equal(scxml_host_router_drain(&router, 1u, &delivered),
                    CFLOW_MAILBOX_OK);
        check_equal(delivered, (size_t)1u);
        check_true(cflow_executor_wait_idle(&executors[0]));
        check_true(scxml_session_get_stats(&sessions[0], &machine));
        check_true(machine.done);
        check_false(machine.errored);
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.delivered, UINT64_C(1));
        check_equal(stats.pending, (size_t)0u);

        scxml_session_cancel(&sessions[1]);
        check_true(cflow_executor_wait_idle(&executors[1]));
        check_equal(scxml_session_destroy(&sessions[1]),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_session_destroy(&sessions[0]),
                    CFLOW_STATECHART_INSTANCE_OK);
        for (size_t i = 0u; i < 2u; ++i) {
            check_equal(scxml_host_event_io_binding_destroy(&bindings[i]),
                        SALTS_OK);
        }
        check_equal(scxml_host_router_unbind_invoke(
            &router, refs[0], "child", sizeof("child") - 1u), SALTS_OK);
        check_equal(scxml_host_router_set_parent(
            &router, refs[1], (scxml_host_session_ref){0}), SALTS_OK);
        check_equal(scxml_host_router_detach(&router, refs[1]), SALTS_OK);
        check_equal(scxml_host_router_detach(&router, refs[0]), SALTS_OK);
        check_equal(scxml_host_router_close(&router), SALTS_OK);
        check_equal(scxml_host_router_destroy(&router), SALTS_OK);
        for (size_t i = 0u; i < 2u; ++i) {
            cflow_executor_destroy(&executors[i]);
            scxml_program_destroy(&programs[i]);
        }
    }

    it("rejects abort until live effect tickets are settled and unlinked") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "version='1.0' initial='idle'>"
            "<state id='idle'/></scxml>";
        scxml_host_router router = {0};
        scxml_host_event_io_binding binding = {0};
        scxml_host_session_ref pending = {0};
        cflow_statechart_effect_ticket ticket = {0};
        const scxml_event_io_adapter *adapter =
            scxml_host_event_io_binding_adapter();
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_session live = {0};
        scxml_host_router_stats stats = {0};
        scxml_host_session_ref live_ref = {0};

        check_equal(scxml_compile(&program, source, sizeof(source) - 1u,
                                  NULL, &diagnostic), SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        {
            scxml_session_config config = {
                .program = &program, .executor = &executor,
                .external_event_capacity = 2u,
                .internal_event_capacity = 2u,
                .completion_capacity = 2u,
                .microstep_limit = 16u
            };
            check_equal(scxml_session_init(&live, &config),
                        CFLOW_STATECHART_INSTANCE_OK);
        }
        check_equal(scxml_host_router_init(
            &router, &(scxml_host_router_config){
                .endpoint_capacity = 2u, .event_capacity = 2u,
                .max_text_bytes = 8u
            }), SALTS_OK);
        check_equal(scxml_host_router_attach(&router, &live, &live_ref),
                    SALTS_OK);
        check_equal(scxml_host_router_reserve(&router, &pending), SALTS_OK);
        check_equal(scxml_host_event_io_binding_init(
            &binding, &router, pending), SALTS_OK);
        check_equal(scxml_host_router_prepare_target(
            &router, pending, NULL, 0u,
            "go", 2u, NULL, 0u, NULL, 0u, &ticket), SALTS_OK);
        check_equal(scxml_host_router_abort(&router, pending), SALTS_EBUSY);
        ticket.discard(ticket.user);
        check_true(scxml_host_router_source_is_quiescent(&router, pending));
        check_equal(scxml_host_router_prepare_target(
            &router, pending, NULL, 0u,
            "go", 2u, NULL, 0u, NULL, 0u, &ticket), SALTS_OK);
        ticket.commit(ticket.user);
        check_equal(scxml_host_event_io_binding_destroy(&binding), SALTS_EBUSY);
        adapter->close(scxml_host_event_io_binding_user(&binding));
        check_true(adapter->is_quiescent(
            scxml_host_event_io_binding_user(&binding)));
        check_equal(scxml_host_event_io_binding_destroy(&binding), SALTS_OK);
        check_equal(scxml_host_router_abort(&router, pending), SALTS_OK);
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.cancelled, UINT64_C(1));
        check_equal(scxml_host_router_detach(&router, pending), SALTS_ENOENT);
        check_equal(scxml_host_router_detach(&router, live_ref), SALTS_OK);
        check_equal(scxml_host_router_close(&router), SALTS_OK);
        check_equal(scxml_host_router_destroy(&router), SALTS_OK);
        check_equal(scxml_session_destroy(&live),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }
}
