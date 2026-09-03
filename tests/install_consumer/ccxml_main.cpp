#include <ccxml/ccxml.h>

int main() {
    ccxml_session session{};
    ccxml_cmeta_datamodel datamodel{};
    ccxml_telephony_adapter_v1 telephony{};
    ccxml_prepared_dialog_start_request prepared_start{};
    const ccxml_limits limits = ccxml_default_limits();
    const ccxml_datamodel_adapter_v1 *adapter =
        ccxml_cmeta_datamodel_adapter();
    const bool valid = limits.max_transitions > 0u &&
                       limits.max_actions > 0u &&
                       limits.max_name_bytes > 0u && session.impl == nullptr &&
                       adapter != nullptr &&
                       adapter->validate_string_location != nullptr &&
                       telephony.prepare_prepared_dialog_start == nullptr &&
                       prepared_start.dialog_id == nullptr &&
                       prepared_start.connection_id == nullptr;
    ccxml_cmeta_datamodel_destroy(&datamodel);
    return valid ? 0 : 1;
}
