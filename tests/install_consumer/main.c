#include <scxml/scxml.h>

#if defined(TURBOSCXML_INSTALL_CONSUMER_EXPECT_QUICKJS) && \
    (!defined(TURBOSCXML_HAS_QUICKJS) || !TURBOSCXML_HAS_QUICKJS)
#error "Installed TurboSCXML target does not advertise QuickJS support"
#endif

int main(void) {
    const scxml_limits limits = scxml_default_limits();
    int valid = limits.max_states > 0u && limits.max_events > 0u &&
                limits.max_transitions > 0u && limits.max_name_bytes > 0u;
#if defined(TURBOSCXML_INSTALL_CONSUMER_EXPECT_QUICKJS)
    const scxml_quickjs_compile_options_v1 quickjs =
        scxml_quickjs_default_compile_options(NULL);
    valid = valid &&
        quickjs.abi_version == SCXML_QUICKJS_COMPILE_OPTIONS_ABI_V1 &&
        quickjs.struct_size >= sizeof(quickjs) &&
        quickjs.max_heap_bytes > 0u && quickjs.max_eval_milliseconds > 0u;
#endif
    return valid ? 0 : 1;
}
