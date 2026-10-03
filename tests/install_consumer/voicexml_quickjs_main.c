#include <voicexml/quickjs.h>

int main(void) {
    vxml_quickjs_compile_options_v1 compile =
        vxml_quickjs_default_compile_options();
    vxml_quickjs_session_options_v1 session = {
        .abi_version = VXML_QUICKJS_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(vxml_quickjs_session_options_v1)};
    vxml_status (*compile_fn)(
        const void *, size_t, const vxml_limits *,
        const vxml_quickjs_compile_options_v1 *,
        vxml_program *, vxml_diagnostic *) =
        vxml_compile_quickjs_script_profile;
    vxml_status (*init_fn)(
        vxml_session *, const vxml_program *,
        const vxml_quickjs_session_options_v1 *) =
        vxml_session_init_quickjs;
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
           session.abi_version ==
               VXML_QUICKJS_SESSION_OPTIONS_ABI_V1 &&
           session.struct_size == sizeof(session) &&
           compile_fn != NULL &&
           init_fn != NULL &&
           event_fn != NULL
        ? 0 : 1;
}
