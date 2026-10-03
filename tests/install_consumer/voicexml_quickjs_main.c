#include <voicexml/quickjs.h>

int main(void) {
    vxml_quickjs_compile_options_v1 compile =
        vxml_quickjs_default_compile_options();
    vxml_quickjs_session_options_v1 session =
        vxml_quickjs_default_session_options();
    vxml_quickjs_script_execution_v1 execution =
        VXML_QUICKJS_SCRIPT_EXECUTION_V1_INIT;
    vxml_status (*compile_fn)(
        const void *, size_t, const vxml_limits *,
        const vxml_quickjs_compile_options_v1 *,
        vxml_program *, vxml_diagnostic *) =
        vxml_compile_quickjs_script_profile;
    vxml_status (*init_fn)(
        vxml_session *, const vxml_program *,
        const vxml_quickjs_session_options_v1 *) =
        vxml_session_init_quickjs;
    vxml_status (*execute_fn)(
        vxml_session *,
        const vxml_quickjs_script_execution_v1 *) =
        vxml_quickjs_session_execute_script;
    vxml_status (*state_fn)(
        const vxml_session *, const void **) =
        vxml_quickjs_session_state;
    vxml_status (*event_fn)(
        const vxml_session *, const char **, size_t *) =
        vxml_quickjs_session_last_event;

    return compile.abi_version ==
               VXML_QUICKJS_COMPILE_OPTIONS_ABI_V1 &&
           compile.struct_size == sizeof(compile) &&
           compile.max_expression_bytes != 0u &&
           compile.max_dynamic_script_uri_bytes != 0u &&
           compile.max_heap_bytes != 0u &&
           compile.max_stack_bytes != 0u &&
           compile.max_eval_milliseconds != 0u &&
           compile.root == 0 &&
           compile.max_conversion_depth != 0u &&
           compile.max_properties != 0u &&
           compile.max_array_items != 0u &&
           compile.max_snapshot_bytes != 0u &&
           compile.max_state_string_bytes != 0u &&
           compile.max_resolved_script_uri_bytes != 0u &&
           compile.max_script_source_bytes != 0u &&
           compile.max_data_rows != 0u &&
           compile.max_data_uri_bytes != 0u &&
           compile.max_data_namelist_fields != 0u &&
           session.abi_version ==
               VXML_QUICKJS_SESSION_OPTIONS_ABI_V1 &&
           session.struct_size == sizeof(session) &&
           session.initial_state == 0 &&
           session.data_resources == 0 &&
           session.max_data_bytes != 0u &&
           session.max_data_request_value_bytes != 0u &&
           session.data_fetch_audio == 0 &&
           execution.abi_version ==
               VXML_QUICKJS_SCRIPT_EXECUTION_ABI_V1 &&
           execution.struct_size == sizeof(execution) &&
           compile_fn != 0 && init_fn != 0 &&
           execute_fn != 0 && state_fn != 0 && event_fn != 0
        ? 0 : 1;
}
