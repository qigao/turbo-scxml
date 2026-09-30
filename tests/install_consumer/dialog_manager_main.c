#include <voicexml/dialog_manager.h>
#include <voicexml/document_store.h>

int main(void) {
    vxml_dialog_manager manager = {0};
    vxml_dialog_manager_config_v1 v1 =
        vxml_dialog_manager_default_config_v1();
    vxml_dialog_manager_config_v2 v2 =
        vxml_dialog_manager_default_config_v2();
    vxml_dialog_manager_config_v3 v3 =
        vxml_dialog_manager_default_config_v3();
    vxml_document_store store = {0};
    vxml_document_fetch_policy_v1 fetch_policy =
        VXML_DOCUMENT_FETCH_POLICY_V1_INIT;
    vxml_document_ref ref = {0};

    if (manager.impl != NULL || store.impl != NULL)
        return 1;
    if (v1.abi_version != VXML_DIALOG_MANAGER_CONFIG_ABI_V1)
        return 2;
    if (v2.abi_version != VXML_DIALOG_MANAGER_CONFIG_ABI_V2)
        return 3;
    if (v3.abi_version != VXML_DIALOG_MANAGER_CONFIG_ABI_V3)
        return 4;
    if (v1.capacity == 0u || v1.max_document_bytes == 0u ||
        v2.capacity == 0u || v2.max_source_bytes == 0u ||
        v3.capacity == 0u || v3.max_source_bytes == 0u ||
        v3.max_navigation_hops == 0u)
        return 5;
    if (v2.document_store != NULL || v3.document_store != NULL)
        return 6;
    if (fetch_policy.abi_version != VXML_DOCUMENT_FETCH_POLICY_ABI_V1 ||
        fetch_policy.struct_size != sizeof(fetch_policy) ||
        fetch_policy.fetchaudio_uri != NULL ||
        fetch_policy.fetchaudio_uri_size != 0u)
        return 7;
    if (vxml_document_store_acquire_with_policy(
            &store, "x", 1u, &fetch_policy, &ref, NULL) !=
        VXML_DOCUMENT_STORE_INVALID_ARGUMENT)
        return 8;
    return vxml_dialog_manager_ccxml_adapter() != NULL ? 0 : 9;
}
