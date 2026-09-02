#include <scxml/scxml.h>

#if defined(TURBOSCXML_INSTALL_CONSUMER_EXPECT_QUICKJS) && \
    (!defined(TURBOSCXML_HAS_QUICKJS) || !TURBOSCXML_HAS_QUICKJS)
#error "Installed TurboSCXML target does not advertise QuickJS support"
#endif

int main(void) {
    const scxml_limits limits = scxml_default_limits();
    const scxml_ioprocessor_descriptor descriptor = {0};
    const scxml_cmeta_compile_options_v2 cmeta_v2 =
        scxml_cmeta_default_compile_options_v2(NULL);
    int valid = limits.max_states > 0u && limits.max_events > 0u &&
                limits.max_transitions > 0u && limits.max_name_bytes > 0u &&
                cmeta_v2.abi_version == SCXML_CMETA_COMPILE_OPTIONS_ABI_V2 &&
                cmeta_v2.struct_size == sizeof(cmeta_v2) &&
                cmeta_v2.actions == NULL && cmeta_v2.action_count == 0u &&
                descriptor.name == NULL &&
                scxml_session_copy_ioprocessor_location(
                    NULL, NULL, 0u, NULL, 0u, NULL) ==
                    SCXML_LOCATION_INVALID_ARGUMENT &&
                scxml_session_try_send_named_with_metadata(
                    NULL, NULL, 0u, NULL) ==
                    CFLOW_MAILBOX_INVALID_ARGUMENT;
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
