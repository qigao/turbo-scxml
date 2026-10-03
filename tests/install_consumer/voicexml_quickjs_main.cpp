#include <voicexml/quickjs.h>

#include <type_traits>

static_assert(
    std::is_standard_layout<vxml_quickjs_compile_options_v1>::value,
    "VoiceXML QuickJS compile options must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_quickjs_session_options_v1>::value,
    "VoiceXML QuickJS session options must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_quickjs_script_execution_v1>::value,
    "VoiceXML QuickJS execution request must remain C-compatible");

int main() {
    auto compile = vxml_quickjs_default_compile_options();
    auto session = vxml_quickjs_default_session_options();
    auto execution =
        vxml_quickjs_script_execution_v1
            VXML_QUICKJS_SCRIPT_EXECUTION_V1_INIT;
    auto compile_fn = &vxml_compile_quickjs_script_profile;
    auto init_fn = &vxml_session_init_quickjs;
    auto execute_fn = &vxml_quickjs_session_execute_script;
    auto state_fn = &vxml_quickjs_session_state;
    auto event_fn = &vxml_quickjs_session_last_event;

    return compile.abi_version ==
               VXML_QUICKJS_COMPILE_OPTIONS_ABI_V1 &&
           compile.struct_size == sizeof(compile) &&
           compile.max_expression_bytes != 0u &&
           compile.max_dynamic_script_uri_bytes != 0u &&
           compile.max_heap_bytes != 0u &&
           compile.max_stack_bytes != 0u &&
           compile.max_eval_milliseconds != 0u &&
           compile.root == nullptr &&
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
           session.initial_state == nullptr &&
           session.data_resources == nullptr &&
           session.max_data_bytes != 0u &&
           session.max_data_request_value_bytes != 0u &&
           session.data_fetch_audio == nullptr &&
           execution.abi_version ==
               VXML_QUICKJS_SCRIPT_EXECUTION_ABI_V1 &&
           execution.struct_size == sizeof(execution) &&
           compile_fn != nullptr &&
           init_fn != nullptr &&
           execute_fn != nullptr &&
           state_fn != nullptr &&
           event_fn != nullptr
        ? 0 : 1;
}
