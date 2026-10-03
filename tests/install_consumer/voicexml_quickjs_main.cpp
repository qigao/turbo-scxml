#include <voicexml/quickjs.h>

#include <type_traits>

static_assert(
    std::is_standard_layout<vxml_quickjs_compile_options_v1>::value,
    "VoiceXML QuickJS compile options must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_quickjs_session_options_v1>::value,
    "VoiceXML QuickJS session options must remain C-compatible");

int main() {
    auto compile = vxml_quickjs_default_compile_options();
    auto session = vxml_quickjs_default_session_options();
    auto compile_fn = &vxml_compile_quickjs_script_profile;
    auto init_fn = &vxml_session_init_quickjs;
    auto event_fn = &vxml_quickjs_session_last_event;

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
           compile_fn != nullptr &&
           init_fn != nullptr &&
           event_fn != nullptr
        ? 0 : 1;
}
