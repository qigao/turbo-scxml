#include <voicexml/dialog_manager.h>

int main(void) {
    vxml_dialog_manager manager = {0};
    vxml_dialog_manager_config_v1 config =
        vxml_dialog_manager_default_config_v1();
    vxml_dialog_manager_config_v2 config_v2 =
        vxml_dialog_manager_default_config_v2();
    if (manager.impl != NULL)
        return 1;
    if (config.abi_version != VXML_DIALOG_MANAGER_CONFIG_ABI_V1)
        return 2;
    if (config.capacity == 0u || config.max_document_bytes == 0u)
        return 3;
    if (config_v2.abi_version != VXML_DIALOG_MANAGER_CONFIG_ABI_V2)
        return 4;
    if (config_v2.capacity == 0u || config_v2.max_fragment_bytes == 0u)
        return 5;
    return vxml_dialog_manager_ccxml_adapter() != NULL ? 0 : 6;
}
