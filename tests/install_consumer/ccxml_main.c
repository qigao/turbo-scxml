#include <ccxml/ccxml.h>

int main(void) {
    ccxml_program program = {0};
    ccxml_cmeta_datamodel datamodel = {0};
    ccxml_telephony_adapter_v1 telephony = {0};
    scxml_event_io_adapter event_io = {0};
    ccxml_session_config session_config = {
        .event_io = &event_io, .event_io_user = &event_io};
    ccxml_prepared_dialog_start_request prepared_start = {0};
    const ccxml_limits limits = ccxml_default_limits();
    const ccxml_datamodel_adapter_v1 *adapter =
        ccxml_cmeta_datamodel_adapter();
    const int valid =
        limits.max_transitions > 0u && limits.max_actions > 0u &&
        limits.max_name_bytes > 0u &&
        CCXML_TELEPHONY_ADAPTER_ABI_V1 == 1u &&
        CCXML_DATAMODEL_ADAPTER_ABI_V1 == 1u && adapter != NULL &&
        adapter->prepare_assign_string != NULL &&
        session_config.event_io == &event_io &&
        session_config.event_io_user == &event_io &&
        telephony.prepare_prepared_dialog_start == NULL &&
        prepared_start.dialog_id == NULL &&
        prepared_start.connection_id == NULL;
    ccxml_cmeta_datamodel_destroy(&datamodel);
    ccxml_program_destroy(&program);
    return valid ? 0 : 1;
}
