#include <ccxml/ccxml.h>

int main() {
    ccxml_session session{};
    ccxml_cmeta_datamodel datamodel{};
    const ccxml_limits limits = ccxml_default_limits();
    const ccxml_datamodel_adapter_v1 *adapter =
        ccxml_cmeta_datamodel_adapter();
    const bool valid = limits.max_transitions > 0u &&
                       limits.max_actions > 0u &&
                       limits.max_name_bytes > 0u && session.impl == nullptr &&
                       adapter != nullptr &&
                       adapter->validate_string_location != nullptr;
    ccxml_cmeta_datamodel_destroy(&datamodel);
    return valid ? 0 : 1;
}
