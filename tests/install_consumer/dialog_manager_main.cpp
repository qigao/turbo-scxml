#include <voicexml/dialog_manager.h>
#include <voicexml/document_store.h>

#include <type_traits>

static_assert(std::is_standard_layout<vxml_dialog_manager>::value,
              "dialog manager handle must remain C-compatible");
static_assert(std::is_standard_layout<vxml_dialog_manager_config_v1>::value,
              "dialog manager v1 config must remain C-compatible");
static_assert(std::is_standard_layout<vxml_dialog_manager_config_v2>::value,
              "dialog manager v2 config must remain C-compatible");
static_assert(std::is_standard_layout<vxml_dialog_manager_config_v3>::value,
              "dialog manager v3 config must remain C-compatible");
static_assert(std::is_standard_layout<vxml_dialog_manager_config_v4>::value,
              "dialog manager v4 config must remain C-compatible");
static_assert(std::is_standard_layout<vxml_dialog_event_v1>::value,
              "dialog manager event must remain C-compatible");
static_assert(std::is_standard_layout<vxml_session_factory_v1>::value,
              "session factory must remain C-compatible");
static_assert(std::is_standard_layout<vxml_document_store>::value,
              "document store handle must remain C-compatible");
static_assert(std::is_standard_layout<vxml_document_fetch_policy_v1>::value,
              "document fetch policy must remain C-compatible");
static_assert(std::is_standard_layout<vxml_document_store_config_v1>::value,
              "document store config must remain C-compatible");
static_assert(std::is_standard_layout<vxml_fetch_audio_request_v1>::value,
              "fetch audio request must remain C-compatible");
static_assert(std::is_standard_layout<vxml_fetch_audio_ticket_v1>::value,
              "fetch audio ticket must remain C-compatible");
static_assert(std::is_standard_layout<vxml_fetch_audio_adapter_v1>::value,
              "fetch audio adapter must remain C-compatible");

int main() {
    const auto v1 = vxml_dialog_manager_default_config_v1();
    auto policy = vxml_document_fetch_policy_v1
        VXML_DOCUMENT_FETCH_POLICY_V1_INIT;
    vxml_fetch_audio_request_v1 fetch_audio_request{};
    vxml_fetch_audio_ticket_v1 fetch_audio_ticket{};
    vxml_fetch_audio_adapter_v1 fetch_audio_adapter{};
    vxml_document_store_config_v1 store_config{};
    vxml_session_factory_v1 session_factory{};
    const auto v2 = vxml_dialog_manager_default_config_v2();
    const auto v3 = vxml_dialog_manager_default_config_v3();
    const auto v4 = vxml_dialog_manager_default_config_v4();
    session_factory.abi_version =
        VXML_SESSION_FACTORY_ABI_V1;
    session_factory.struct_size =
        sizeof(session_factory);
    fetch_audio_request.abi_version = VXML_FETCH_AUDIO_REQUEST_ABI_V1;
    fetch_audio_request.struct_size = sizeof(fetch_audio_request);
    fetch_audio_adapter.abi_version = VXML_FETCH_AUDIO_ADAPTER_ABI_V1;
    fetch_audio_adapter.struct_size = sizeof(fetch_audio_adapter);
    return v1.abi_version == VXML_DIALOG_MANAGER_CONFIG_ABI_V1 &&
           v2.abi_version == VXML_DIALOG_MANAGER_CONFIG_ABI_V2 &&
           v3.abi_version == VXML_DIALOG_MANAGER_CONFIG_ABI_V3 &&
           v4.abi_version == VXML_DIALOG_MANAGER_CONFIG_ABI_V4 &&
           v3.max_navigation_hops != 0u &&
           v4.max_navigation_hops != 0u &&
           v4.max_submit_response_bytes != 0u &&
           v4.voice_limits.max_forms != 0u &&
           v4.document_store == nullptr &&
           v4.submit == nullptr &&
           v2.session_factory == nullptr &&
           v3.session_factory == nullptr &&
           v4.session_factory == nullptr &&
           session_factory.abi_version ==
               VXML_SESSION_FACTORY_ABI_V1 &&
           policy.abi_version == VXML_DOCUMENT_FETCH_POLICY_ABI_V1 &&
           policy.struct_size == sizeof(policy) &&
           fetch_audio_request.abi_version == VXML_FETCH_AUDIO_REQUEST_ABI_V1 &&
           fetch_audio_adapter.abi_version == VXML_FETCH_AUDIO_ADAPTER_ABI_V1 &&
           fetch_audio_ticket.finish == nullptr &&
           store_config.fetch_audio == nullptr &&
           VXML_FETCH_AUDIO_STARTED != VXML_FETCH_AUDIO_SKIPPED
        ? 0 : 1;
}
