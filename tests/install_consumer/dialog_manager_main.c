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
    vxml_dialog_manager_config_v4 v4 =
        vxml_dialog_manager_default_config_v4();
    vxml_document_store store = {0};
    vxml_document_fetch_policy_v1 fetch_policy =
        VXML_DOCUMENT_FETCH_POLICY_V1_INIT;
    vxml_fetch_audio_request_v1 fetch_audio_request = {
        .abi_version = VXML_FETCH_AUDIO_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_fetch_audio_request_v1),
        .uri = "https://example.invalid/wait.wav",
        .uri_size = sizeof("https://example.invalid/wait.wav") - 1u,
        .has_delay = true,
        .delay_us = UINT64_C(0),
        .has_minimum = true,
        .minimum_us = UINT64_C(0)};
    vxml_fetch_audio_ticket_v1 fetch_audio_ticket = {0};
    vxml_fetch_audio_adapter_v1 fetch_audio_adapter = {
        .abi_version = VXML_FETCH_AUDIO_ADAPTER_ABI_V1,
        .struct_size = sizeof(vxml_fetch_audio_adapter_v1)};
    vxml_document_store_config_v1 store_config = {0};
    vxml_document_ref ref = {0};
    vxml_session_factory_v1 session_factory = {
        .abi_version = VXML_SESSION_FACTORY_ABI_V1,
        .struct_size = sizeof(vxml_session_factory_v1)};

    if (manager.impl != NULL || store.impl != NULL)
        return 1;
    if (v1.abi_version != VXML_DIALOG_MANAGER_CONFIG_ABI_V1)
        return 2;
    if (v2.abi_version != VXML_DIALOG_MANAGER_CONFIG_ABI_V2)
        return 3;
    if (v3.abi_version != VXML_DIALOG_MANAGER_CONFIG_ABI_V3)
        return 4;
    if (v4.abi_version != VXML_DIALOG_MANAGER_CONFIG_ABI_V4)
        return 40;
    if (v1.capacity == 0u || v1.max_document_bytes == 0u ||
        v2.capacity == 0u || v2.max_source_bytes == 0u ||
        v3.capacity == 0u || v3.max_source_bytes == 0u ||
        v3.max_navigation_hops == 0u ||
        v4.capacity == 0u || v4.max_source_bytes == 0u ||
        v4.max_navigation_hops == 0u ||
        v4.max_submit_response_bytes == 0u ||
        v4.voice_limits.max_forms == 0u)
        return 5;
    if (v2.document_store != NULL || v3.document_store != NULL ||
        v4.document_store != NULL || v4.submit != NULL ||
        v2.session_factory != NULL ||
        v3.session_factory != NULL ||
        v4.session_factory != NULL ||
        session_factory.abi_version !=
            VXML_SESSION_FACTORY_ABI_V1)
        return 6;
    if (fetch_policy.abi_version != VXML_DOCUMENT_FETCH_POLICY_ABI_V1 ||
        fetch_policy.struct_size != sizeof(fetch_policy) ||
        fetch_audio_request.abi_version != VXML_FETCH_AUDIO_REQUEST_ABI_V1 ||
        !fetch_audio_request.has_delay ||
        fetch_audio_request.delay_us != UINT64_C(0) ||
        !fetch_audio_request.has_minimum ||
        fetch_audio_request.minimum_us != UINT64_C(0) ||
        fetch_audio_ticket.finish != NULL ||
        fetch_audio_adapter.abi_version != VXML_FETCH_AUDIO_ADAPTER_ABI_V1 ||
        store_config.fetch_audio != NULL ||
        VXML_FETCH_AUDIO_SKIPPED == VXML_FETCH_AUDIO_STARTED)
        return 7;
    if (vxml_document_store_acquire_with_policy(
            &store, "x", 1u, &fetch_policy, &ref, NULL) !=
        VXML_DOCUMENT_STORE_INVALID_ARGUMENT)
        return 8;
    return vxml_dialog_manager_ccxml_adapter() != NULL ? 0 : 9;
}
