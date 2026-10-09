#include <scxml/host_event_io.h>

#include <cflow/executor.h>
#include <tinytest.h>

#include <string.h>

/* Test-only, trivially-copyable root for inspecting the *actual* receiving
   CMeta _event data, sendid and origin rather than testing ticket counts only. */
Struct(host_content_test_root,
    (int, marker)
);

static const cmeta_type_traits host_content_root_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY
};
static const cmeta_type_desc host_content_root_type = {
    .name = "host_content_test_root",
    .size = sizeof(host_content_test_root),
    .align = _Alignof(host_content_test_root),
    .kind = CMETA_T_OBJECT,
    .traits = &host_content_root_traits
};
static const cmeta_data_field_desc host_content_root_fields[] = {{
    "test.host.content.marker", "marker",
    offsetof(host_content_test_root, marker), &cmeta_data_int
}};
static const cmeta_data_struct_shape host_content_root_shape = {
    .layout = StructMeta(host_content_test_root),
    .fields = host_content_root_fields,
    .field_count = 1u
};
static const cmeta_data_desc host_content_root_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.host.content.schema",
    .display_name = "Test SCXML Host Content",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &host_content_root_type,
    .shape = &host_content_root_shape
};

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
        check_equal(adapter->capabilities,
                    SCXML_EVENT_IO_CAP_SEND | SCXML_EVENT_IO_CAP_CONTENT);
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
            "go", 2u, NULL, 0u, NULL, &ticket), SALTS_OK);
        check_equal(scxml_host_router_abort(&router, pending), SALTS_EBUSY);
        ticket.discard(ticket.user);
        check_true(scxml_host_router_source_is_quiescent(&router, pending));
        check_equal(scxml_host_router_prepare_target(
            &router, pending, NULL, 0u,
            "go", 2u, NULL, 0u, NULL, &ticket), SALTS_OK);
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
    it("copies W3C TEXT_UTF8 <content> with sendid and SCXML origin") {
        static const char receiver_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "version='1.0' datamodel='cmeta' initial='waiting'>"
            "<state id='waiting'>"
            "<transition event='note' cond='_event.data == &quot;owned&quot; "
            "&amp;&amp; _event.sendid == &quot;note-1&quot; "
            "&amp;&amp; _event.origin != &quot;&quot; "
            "&amp;&amp; _event.origintype == &quot;scxml&quot;' target='done'/>"
            "<transition event='note' target='failed'/></state>"
            "<state id='failed'/><final id='done'/></scxml>";
        static const char sender_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "version='1.0' datamodel='cmeta' initial='sending'>"
            "<state id='sending'><onentry>"
            "<send event='note' target='#_parent' id='note-1'>"
            "<content>owned</content></send>"
            "</onentry></state></scxml>";
        scxml_program programs[2] = {{0}, {0}};
        scxml_diagnostic diagnostics[2] = {{0}, {0}};
        cflow_executor executors[2] = {{0}, {0}};
        scxml_session sessions[2] = {{0}, {0}};
        scxml_host_event_io_binding bindings[2] = {{0}, {0}};
        scxml_host_router router = {0};
        scxml_host_session_ref refs[2] = {{0}, {0}};
        cflow_statechart_instance_stats machine = {0};
        const host_content_test_root initial = {0};
        const scxml_cmeta_session_options_v1 session_options = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(session_options),
            .initial_state = &initial
        };
        const scxml_cmeta_compile_options_v1 compile_options =
            scxml_cmeta_default_compile_options(&host_content_root_desc);
        size_t delivered = 0u;

        check_equal(scxml_compile_cmeta(
            &programs[0], receiver_source, sizeof(receiver_source) - 1u,
            NULL, &compile_options, &diagnostics[0]), SCXML_OK);
        check_equal(scxml_compile_cmeta(
            &programs[1], sender_source, sizeof(sender_source) - 1u,
            NULL, &compile_options, &diagnostics[1]), SCXML_OK);
        check_equal(scxml_host_router_init(
            &router, &(scxml_host_router_config){
                .endpoint_capacity = 2u, .event_capacity = 2u,
                .max_text_bytes = 32u
            }), SALTS_OK);

        for (size_t i = 0u; i < 2u; ++i) {
            check_equal(scxml_host_router_reserve(&router, &refs[i]), SALTS_OK);
            check_equal(scxml_host_event_io_binding_init(
                &bindings[i], &router, refs[i]), SALTS_OK);
            check_true(cflow_executor_serial_init(&executors[i]));
        }
        check_equal(scxml_host_router_set_parent(
            &router, refs[1], refs[0]), SALTS_OK);
        {
            scxml_session_config config =
                make_config(&programs[0], &executors[0], &bindings[0]);
            check_equal(scxml_session_init_cmeta(
                &sessions[0], &config, &session_options),
                CFLOW_STATECHART_INSTANCE_OK);
        }
        check_equal(scxml_host_router_activate(
            &router, refs[0], &sessions[0]), SALTS_OK);
        {
            scxml_session_config config =
                make_config(&programs[1], &executors[1], &bindings[1]);
            check_equal(scxml_session_init_cmeta(
                &sessions[1], &config, &session_options),
                CFLOW_STATECHART_INSTANCE_OK);
        }
        check_true(cflow_executor_wait_idle(&executors[1]));
        check_equal(scxml_host_router_activate(
            &router, refs[1], &sessions[1]), SALTS_OK);
        check_equal(scxml_host_router_drain(&router, 1u, &delivered),
                    CFLOW_MAILBOX_OK);
        check_equal(delivered, (size_t)1u);
        check_true(cflow_executor_wait_idle(&executors[0]));
        check_true(scxml_session_get_stats(&sessions[0], &machine));
        check_true(machine.done);
        check_false(machine.errored);

        scxml_session_cancel(&sessions[1]);
        check_true(cflow_executor_wait_idle(&executors[1]));
        for (size_t i = 0u; i < 2u; ++i) {
            check_equal(scxml_session_destroy(&sessions[i]),
                        CFLOW_STATECHART_INSTANCE_OK);
            check_equal(scxml_host_event_io_binding_destroy(&bindings[i]),
                        SALTS_OK);
        }
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

    it("rejects scalar/named payload and oversized content without a ticket") {
        static const char state_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "version='1.0' initial='waiting'><state id='waiting'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        scxml_session session = {0};
        scxml_host_router router = {0};
        scxml_host_event_io_binding binding = {0};
        scxml_host_session_ref ref = {0};
        scxml_host_router_stats stats = {0};
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;
        scxml_send_request request = {0};
        char changed[5] = "data";
        const scxml_event_io_adapter *adapter =
            scxml_host_event_io_binding_adapter();
        check_equal(scxml_compile(
            &program, state_source, sizeof(state_source) - 1u,
            NULL, &diagnostic), SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        {
            scxml_session_config config = {
                .program = &program, .executor = &executor,
                .external_event_capacity = 2u,
                .internal_event_capacity = 2u,
                .completion_capacity = 2u, .microstep_limit = 16u
            };
            check_equal(scxml_session_init(&session, &config),
                        CFLOW_STATECHART_INSTANCE_OK);
        }
        check_equal(scxml_host_router_init(
            &router, &(scxml_host_router_config){
                .endpoint_capacity = 1u, .event_capacity = 1u,
                .max_text_bytes = 4u
            }), SALTS_OK);
        check_equal(scxml_host_router_attach(&router, &session, &ref),
                    SALTS_OK);
        check_equal(scxml_host_event_io_binding_init(
            &binding, &router, ref), SALTS_OK);
        request.event = "note";
        request.event_size = 4u;
        request.payload.kind = SCXML_PAYLOAD_CONTENT;
        request.payload.content.kind = SCXML_CONTENT_SCALAR;
        request.payload.content.scalar.kind = SCXML_PAYLOAD_VALUE_STRING;
        request.payload.content.scalar.data.string.data = "data";
        request.payload.content.scalar.data.string.size = 4u;
        check_equal(adapter->prepare_send(
            scxml_host_event_io_binding_user(&binding),
            &request, &ticket, &error), SCXML_ADAPTER_ERROR_EXECUTION);
        check_null(ticket.commit);
        request.payload.kind = SCXML_PAYLOAD_NAMED;
        check_equal(adapter->prepare_send(
            scxml_host_event_io_binding_user(&binding),
            &request, &ticket, &error), SCXML_ADAPTER_ERROR_EXECUTION);
        check_null(ticket.commit);

        request.payload.kind = SCXML_PAYLOAD_CONTENT;
        request.payload.content.kind = SCXML_CONTENT_XML_UTF8;
        request.payload.content.bytes = "oversize";
        request.payload.content.byte_count = 8u;
        check_equal(adapter->prepare_send(
            scxml_host_event_io_binding_user(&binding),
            &request, &ticket, &error), SCXML_ADAPTER_ERROR_EXECUTION);
        check_null(ticket.commit);
        check_not_null(error);

        request.payload.content.bytes = changed;
        request.payload.content.byte_count = 4u;
        check_equal(adapter->prepare_send(
            scxml_host_event_io_binding_user(&binding),
            &request, &ticket, &error), SCXML_ADAPTER_ACCEPTED);
        changed[0] = 'X';
        check_equal(scxml_host_router_detach(&router, ref), SALTS_EBUSY);
        ticket.discard(ticket.user);
        check_true(scxml_host_router_get_stats(&router, &stats));
        check_equal(stats.pending, (size_t)0u);
        check_equal(stats.discarded, UINT64_C(1));

        adapter->close(scxml_host_event_io_binding_user(&binding));
        check_true(adapter->is_quiescent(
            scxml_host_event_io_binding_user(&binding)));
        check_equal(scxml_host_event_io_binding_destroy(&binding), SALTS_OK);
        check_equal(scxml_host_router_detach(&router, ref), SALTS_OK);
        check_equal(scxml_host_router_close(&router), SALTS_OK);
        check_equal(scxml_host_router_destroy(&router), SALTS_OK);
        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
    }

}
