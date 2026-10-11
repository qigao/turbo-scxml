#include <ccxml/ccxml.h>
#ifdef TURBOSCXML_INSTALL_CONSUMER_EXPECT_QUICKJS
#include <ccxml/quickjs.h>
#include "ccxml_quickjs_cases.h"
#endif

int main() {
#ifdef TURBOSCXML_INSTALL_CONSUMER_EXPECT_QUICKJS
    ccxml_quickjs_datamodel js_owner{};
    const auto js_config = ccxml_quickjs_default_datamodel_config();
    ccxml_status (*init_js)(ccxml_quickjs_datamodel *,
        const ccxml_quickjs_datamodel_config_v1 *) = ccxml_quickjs_datamodel_init;
    if (js_config.abi_version != CCXML_QUICKJS_DATAMODEL_CONFIG_ABI_V1 ||
        js_config.struct_size != sizeof(js_config) ||
        js_config.max_expressions == 0u ||
        ccxml_quickjs_datamodel_adapter()->evaluate_string_expression == nullptr ||
        init_js(&js_owner, &js_config) != CCXML_INVALID_ARGUMENT ||
        js_owner.impl != nullptr) return 2;
    ccxml_quickjs_datamodel_destroy(&js_owner);
    const int js_result = installed_ccxml_quickjs_cases();
    if (js_result != 0) return js_result;
#endif
    ccxml_session session{};
    ccxml_cmeta_datamodel datamodel{};
    ccxml_telephony_adapter_v1 telephony{};
    scxml_event_io_adapter event_io{};
    ccxml_session_config session_config{};
    ccxml_prepared_dialog_start_request prepared_start{};
    const ccxml_limits limits = ccxml_default_limits();
    const ccxml_datamodel_adapter_v1 *adapter =
        ccxml_cmeta_datamodel_adapter();
    session_config.event_io = &event_io;
    session_config.event_io_user = &event_io;
    const bool valid = limits.max_transitions > 0u &&
                       limits.max_actions > 0u &&
                       limits.max_name_bytes > 0u && session.impl == nullptr &&
                       adapter != nullptr &&
                       adapter->validate_string_location != nullptr &&
                       session_config.event_io == &event_io &&
                       session_config.event_io_user == &event_io &&
                       telephony.prepare_prepared_dialog_start == nullptr &&
                       prepared_start.dialog_id == nullptr &&
                       prepared_start.connection_id == nullptr;
    ccxml_cmeta_datamodel_destroy(&datamodel);
    return valid ? 0 : 1;
}
