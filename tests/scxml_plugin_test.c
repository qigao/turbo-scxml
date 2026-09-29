#include <scxml/plugin.h>

#include <cmeta/cmeta.h>
#include <tinytest.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static const char *plugin_fixture_path(void) {
    const char *path = getenv("SCXML_PLUGIN_FIXTURE_PATH");
    return path != NULL && path[0] != '\0' ? path : NULL;
}

Struct(scxml_plugin_test_state,
    (int, count)
);

static const cmeta_type_traits plugin_state_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY
};

static const cmeta_type_desc plugin_state_type = {
    .name = "scxml_plugin_test_state",
    .size = sizeof(scxml_plugin_test_state),
    .align = _Alignof(scxml_plugin_test_state),
    .kind = CMETA_T_OBJECT,
    .traits = &plugin_state_traits
};

static const cmeta_data_field_desc plugin_state_fields[] = {{
    "test.scxml.plugin.count",
    "count",
    offsetof(scxml_plugin_test_state, count),
    &cmeta_data_int
}};

static const cmeta_data_struct_shape plugin_state_shape = {
    .layout = StructMeta(scxml_plugin_test_state),
    .fields = plugin_state_fields,
    .field_count = 1u
};

static const cmeta_data_desc plugin_state_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.plugin.state",
    .display_name = "SCXML Plugin test state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &plugin_state_type,
    .shape = &plugin_state_shape
};

spec("TurboSCXML Plugin bridge") {
    it("holds the plugin lease through Program execution and releases it after Program destruction") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "xmlns:p='urn:test:plugin' version='1.0' "
            "datamodel='cmeta' initial='active'>"
            "<state id='active'><onentry>"
            "<p:check value='count + 1'/>"
            "</onentry><transition target='done'/></state>"
            "<final id='done'/></scxml>";
        salts_plugin_registry registry = {0};
        const salts_plugin_registry_config registry_config = {
            .capacity = 2u};
        salts_plugin_ref ref = {0};
        salts_plugin_lifecycle_info lifecycle = {0};
        salts_plugin_status plugin_status = SALTS_PLUGIN_OK;
        bool quiescent = false;
        scxml_plugin_program program = {0};
        const scxml_program *core;
        scxml_cmeta_compile_options_v4 cmeta =
            scxml_cmeta_default_compile_options_v4(&plugin_state_desc);
        const scxml_plugin_action_v1 action = {
            .struct_size = sizeof(scxml_plugin_action_v1),
            .plugin = {0u, 0u},
            .export_id = "test.scxml.action.check",
            .contract_id = "test.scxml.action",
            .contract_version = 1u,
            .required_capabilities = UINT64_C(1),
            .namespace_uri = "urn:test:plugin",
            .namespace_uri_size = sizeof("urn:test:plugin") - 1u,
            .local_name = "check",
            .local_name_size = sizeof("check") - 1u};
        scxml_plugin_action_v1 mapped_action = action;
        scxml_plugin_compile_options_v1 options =
            SCXML_PLUGIN_COMPILE_OPTIONS_V1_INIT;
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_statechart_instance_stats stats = {0};
        scxml_session_config session_config = {0};
        const scxml_plugin_test_state initial = {7};
        const scxml_cmeta_session_options_v1 data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(data),
            .initial_state = &initial};

        check_equal(
            salts_plugin_registry_init(&registry, &registry_config),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_load(
                &registry, plugin_fixture_path(), &ref),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_start(&registry, ref),
            SALTS_PLUGIN_OK);

        mapped_action.plugin = ref;
        options.registry = &registry;
        options.cmeta = &cmeta;
        options.plugin_actions = &mapped_action;
        options.plugin_action_count = 1u;

        check_equal(
            scxml_plugin_compile_cmeta_v1(
                &program, source, sizeof(source) - 1u,
                NULL, &options, &diagnostic, &plugin_status),
            SCXML_PLUGIN_OK);
        check_equal(plugin_status, SALTS_PLUGIN_OK);
        core = scxml_plugin_program_core(&program);
        check_not_null(core);

        check_equal(
            salts_plugin_registry_get_lifecycle(
                &registry, ref, &lifecycle),
            SALTS_PLUGIN_OK);
        check_equal(lifecycle.active_leases, (size_t)1u);

        check_true(cflow_executor_serial_init(&executor));
        session_config = (scxml_session_config){
            .program = core,
            .executor = &executor,
            .external_event_capacity = 1u,
            .internal_event_capacity = 2u,
            .completion_capacity = 1u,
            .microstep_limit = 16u};
        check_equal(
            scxml_session_init_cmeta(&session, &session_config, &data),
            CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(
            scxml_session_destroy(&session),
            CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);

        check_equal(
            salts_plugin_registry_request_stop(&registry, ref),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_poll_quiescent(
                &registry, ref, &quiescent),
            SALTS_PLUGIN_OK);
        check_false(quiescent);
        check_equal(
            salts_plugin_registry_unload(&registry, ref),
            SALTS_PLUGIN_BUSY);

        check_equal(
            scxml_plugin_program_destroy(&program, &plugin_status),
            SCXML_PLUGIN_OK);
        check_equal(plugin_status, SALTS_PLUGIN_OK);
        check_null(scxml_plugin_program_core(&program));

        check_equal(
            salts_plugin_registry_poll_quiescent(
                &registry, ref, &quiescent),
            SALTS_PLUGIN_OK);
        check_true(quiescent);
        check_equal(
            salts_plugin_registry_unload(&registry, ref),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_destroy(&registry),
            SALTS_PLUGIN_OK);
    }

    it("releases its lease when export contract admission fails") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "xmlns:p='urn:test:plugin' version='1.0' datamodel='cmeta'>"
            "<state id='active'><onentry><p:check value='count'/></onentry>"
            "</state></scxml>";
        salts_plugin_registry registry = {0};
        const salts_plugin_registry_config registry_config = {
            .capacity = 1u};
        salts_plugin_ref ref = {0};
        salts_plugin_lifecycle_info lifecycle = {0};
        salts_plugin_status plugin_status = SALTS_PLUGIN_OK;
        bool quiescent = false;
        scxml_plugin_program program = {0};
        scxml_cmeta_compile_options_v4 cmeta =
            scxml_cmeta_default_compile_options_v4(&plugin_state_desc);
        scxml_plugin_action_v1 action = {
            .struct_size = sizeof(scxml_plugin_action_v1),
            .export_id = "test.scxml.action.check",
            .contract_id = "wrong.contract",
            .contract_version = 1u,
            .required_capabilities = UINT64_C(1),
            .namespace_uri = "urn:test:plugin",
            .namespace_uri_size = sizeof("urn:test:plugin") - 1u,
            .local_name = "check",
            .local_name_size = sizeof("check") - 1u};
        scxml_plugin_compile_options_v1 options =
            SCXML_PLUGIN_COMPILE_OPTIONS_V1_INIT;
        scxml_diagnostic diagnostic = {0};

        check_equal(
            salts_plugin_registry_init(&registry, &registry_config),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_load(
                &registry, plugin_fixture_path(), &ref),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_start(&registry, ref),
            SALTS_PLUGIN_OK);
        action.plugin = ref;
        options.registry = &registry;
        options.cmeta = &cmeta;
        options.plugin_actions = &action;
        options.plugin_action_count = 1u;

        check_equal(
            scxml_plugin_compile_cmeta_v1(
                &program, source, sizeof(source) - 1u,
                NULL, &options, &diagnostic, &plugin_status),
            SCXML_PLUGIN_INCOMPATIBLE_EXPORT);
        check_equal(
            plugin_status, SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);
        check_null(scxml_plugin_program_core(&program));
        check_equal(
            salts_plugin_registry_get_lifecycle(
                &registry, ref, &lifecycle),
            SALTS_PLUGIN_OK);
        check_equal(lifecycle.active_leases, (size_t)0u);

        check_equal(
            salts_plugin_registry_request_stop(&registry, ref),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_poll_quiescent(
                &registry, ref, &quiescent),
            SALTS_PLUGIN_OK);
        check_true(quiescent);
        check_equal(
            salts_plugin_registry_unload(&registry, ref),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_destroy(&registry),
            SALTS_PLUGIN_OK);
    }

    it("fails closed when a Plugin ref became stale before compilation") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "xmlns:p='urn:test:plugin' version='1.0' datamodel='cmeta'>"
            "<state id='active'><onentry><p:check value='count'/></onentry>"
            "</state></scxml>";
        salts_plugin_registry registry = {0};
        const salts_plugin_registry_config registry_config = {
            .capacity = 1u};
        salts_plugin_ref stale_ref = {0};
        salts_plugin_status plugin_status = SALTS_PLUGIN_OK;
        bool quiescent = false;
        scxml_plugin_program program = {0};
        scxml_cmeta_compile_options_v4 cmeta =
            scxml_cmeta_default_compile_options_v4(&plugin_state_desc);
        scxml_plugin_action_v1 action = {
            .struct_size = sizeof(scxml_plugin_action_v1),
            .export_id = "test.scxml.action.check",
            .contract_id = "test.scxml.action",
            .contract_version = 1u,
            .required_capabilities = UINT64_C(1),
            .namespace_uri = "urn:test:plugin",
            .namespace_uri_size = sizeof("urn:test:plugin") - 1u,
            .local_name = "check",
            .local_name_size = sizeof("check") - 1u};
        scxml_plugin_compile_options_v1 options =
            SCXML_PLUGIN_COMPILE_OPTIONS_V1_INIT;

        check_not_null(plugin_fixture_path());
        check_equal(
            salts_plugin_registry_init(&registry, &registry_config),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_load(
                &registry, plugin_fixture_path(), &stale_ref),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_start(&registry, stale_ref),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_request_stop(&registry, stale_ref),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_poll_quiescent(
                &registry, stale_ref, &quiescent),
            SALTS_PLUGIN_OK);
        check_true(quiescent);
        check_equal(
            salts_plugin_registry_unload(&registry, stale_ref),
            SALTS_PLUGIN_OK);

        action.plugin = stale_ref;
        options.registry = &registry;
        options.cmeta = &cmeta;
        options.plugin_actions = &action;
        options.plugin_action_count = 1u;
        check_equal(
            scxml_plugin_compile_cmeta_v1(
                &program, source, sizeof(source) - 1u,
                NULL, &options, NULL, &plugin_status),
            SCXML_PLUGIN_PLUGIN_ERROR);
        check_equal(plugin_status, SALTS_PLUGIN_STALE);
        check_null(scxml_plugin_program_core(&program));
        check_equal(
            salts_plugin_registry_destroy(&registry),
            SALTS_PLUGIN_OK);
    }

    it("bridges a Plugin Event I/O Interface export with a lease-safe adapter") {
        salts_plugin_registry registry = {0};
        const salts_plugin_registry_config registry_config = {
            .capacity = 1u};
        salts_plugin_ref ref = {0};
        salts_plugin_lifecycle_info lifecycle = {0};
        salts_plugin_status plugin_status = SALTS_PLUGIN_OK;
        scxml_plugin_event_io_provider provider = {0};
        scxml_plugin_provider_v1 binding =
            SCXML_PLUGIN_PROVIDER_V1_INIT;
        const scxml_event_io_adapter *adapter;
        scxml_send_request request = {0};
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;
        bool quiescent = false;

        check_equal(
            salts_plugin_registry_init(&registry, &registry_config),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_load(
                &registry, plugin_fixture_path(), &ref),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_start(&registry, ref),
            SALTS_PLUGIN_OK);

        binding.plugin = ref;
        binding.export_id = "test.scxml.provider.event-io";
        binding.contract_id = "test.scxml.event-io";
        binding.contract_version = 1u;
        binding.required_capabilities = SCXML_EVENT_IO_CAP_SEND;

        check_equal(
            scxml_plugin_event_io_provider_open(
                &provider, &registry, &binding, &plugin_status),
            SCXML_PLUGIN_OK);
        check_equal(plugin_status, SALTS_PLUGIN_OK);
        adapter = scxml_plugin_event_io_provider_adapter(&provider);
        check_not_null(adapter);
        check_true(
            (adapter->capabilities & SCXML_EVENT_IO_CAP_SEND) != 0u);
        check_equal(
            adapter->prepare_send(
                scxml_plugin_event_io_provider_user(&provider),
                &request, &ticket, &error),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_not_null(error);

        check_equal(
            salts_plugin_registry_get_lifecycle(
                &registry, ref, &lifecycle),
            SALTS_PLUGIN_OK);
        check_equal(lifecycle.active_leases, (size_t)1u);
        check_equal(
            salts_plugin_registry_request_stop(&registry, ref),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_poll_quiescent(
                &registry, ref, &quiescent),
            SALTS_PLUGIN_OK);
        check_false(quiescent);
        check_equal(
            salts_plugin_registry_unload(&registry, ref),
            SALTS_PLUGIN_BUSY);

        check_equal(
            scxml_plugin_event_io_provider_destroy(
                &provider, &plugin_status),
            SCXML_PLUGIN_OK);
        check_equal(plugin_status, SALTS_PLUGIN_OK);
        check_null(scxml_plugin_event_io_provider_adapter(&provider));

        binding.export_id = "test.scxml.provider.invoke";
        binding.contract_id = "test.scxml.invoke";
        binding.required_capabilities = SCXML_INVOKE_CAP_START;
        check_equal(
            scxml_plugin_event_io_provider_open(
                &provider, &registry, &binding, &plugin_status),
            SCXML_PLUGIN_INCOMPATIBLE_EXPORT);
        check_equal(
            plugin_status, SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);
        check_equal(
            salts_plugin_registry_get_lifecycle(
                &registry, ref, &lifecycle),
            SALTS_PLUGIN_OK);
        check_equal(lifecycle.active_leases, (size_t)0u);

        check_equal(
            salts_plugin_registry_poll_quiescent(
                &registry, ref, &quiescent),
            SALTS_PLUGIN_OK);
        check_true(quiescent);
        check_equal(
            salts_plugin_registry_unload(&registry, ref),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_destroy(&registry),
            SALTS_PLUGIN_OK);
    }

    it("bridges a Plugin Invoke Interface export and rejects capability mismatch") {
        salts_plugin_registry registry = {0};
        const salts_plugin_registry_config registry_config = {
            .capacity = 1u};
        salts_plugin_ref ref = {0};
        salts_plugin_lifecycle_info lifecycle = {0};
        salts_plugin_status plugin_status = SALTS_PLUGIN_OK;
        scxml_plugin_invoke_provider provider = {0};
        scxml_plugin_provider_v1 binding =
            SCXML_PLUGIN_PROVIDER_V1_INIT;
        const scxml_invoke_adapter *adapter;
        scxml_invoke_start_request request = {0};
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;
        bool quiescent = false;

        check_equal(
            salts_plugin_registry_init(&registry, &registry_config),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_load(
                &registry, plugin_fixture_path(), &ref),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_start(&registry, ref),
            SALTS_PLUGIN_OK);

        binding.plugin = ref;
        binding.export_id = "test.scxml.provider.invoke";
        binding.contract_id = "test.scxml.invoke";
        binding.contract_version = 1u;
        binding.required_capabilities = SCXML_INVOKE_CAP_START;

        check_equal(
            scxml_plugin_invoke_provider_open(
                &provider, &registry, &binding, &plugin_status),
            SCXML_PLUGIN_OK);
        adapter = scxml_plugin_invoke_provider_adapter(&provider);
        check_not_null(adapter);
        check_true(
            (adapter->capabilities & SCXML_INVOKE_CAP_START) != 0u);
        check_equal(
            adapter->prepare_start(
                scxml_plugin_invoke_provider_user(&provider),
                &request, &ticket, &error),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(
            scxml_plugin_invoke_provider_destroy(
                &provider, &plugin_status),
            SCXML_PLUGIN_OK);

        binding.required_capabilities =
            SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_FORWARD;
        check_equal(
            scxml_plugin_invoke_provider_open(
                &provider, &registry, &binding, &plugin_status),
            SCXML_PLUGIN_INCOMPATIBLE_EXPORT);
        check_equal(
            plugin_status, SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);
        check_equal(
            salts_plugin_registry_get_lifecycle(
                &registry, ref, &lifecycle),
            SALTS_PLUGIN_OK);
        check_equal(lifecycle.active_leases, (size_t)0u);

        check_equal(
            salts_plugin_registry_request_stop(&registry, ref),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_poll_quiescent(
                &registry, ref, &quiescent),
            SALTS_PLUGIN_OK);
        check_true(quiescent);
        check_equal(
            salts_plugin_registry_unload(&registry, ref),
            SALTS_PLUGIN_OK);
        check_equal(
            salts_plugin_registry_destroy(&registry),
            SALTS_PLUGIN_OK);
    }

}

