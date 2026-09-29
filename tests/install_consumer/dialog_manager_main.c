#include <voicexml/dialog_manager.h>
#include <voicexml/document_store.h>

int main(void) {
    vxml_dialog_manager manager = {0};
    vxml_dialog_manager_config_v1 v1 =
        vxml_dialog_manager_default_config_v1();
    vxml_dialog_manager_config_v2 v2 =
        vxml_dialog_manager_default_config_v2();
    vxml_document_store store = {0};

    if (manager.impl != NULL || store.impl != NULL)
        return 1;
    if (v1.abi_version != VXML_DIALOG_MANAGER_CONFIG_ABI_V1)
        return 2;
    if (v2.abi_version != VXML_DIALOG_MANAGER_CONFIG_ABI_V2)
        return 3;
    if (v1.capacity == 0u || v1.max_document_bytes == 0u ||
        v2.capacity == 0u || v2.max_source_bytes == 0u)
        return 4;
    if (v2.document_store != NULL)
        return 5;
    return vxml_dialog_manager_ccxml_adapter() != NULL ? 0 : 6;
}
