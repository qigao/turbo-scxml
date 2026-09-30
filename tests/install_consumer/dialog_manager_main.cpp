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
static_assert(std::is_standard_layout<vxml_dialog_event_v1>::value,
              "dialog manager event must remain C-compatible");
static_assert(std::is_standard_layout<vxml_document_store>::value,
              "document store handle must remain C-compatible");
static_assert(std::is_standard_layout<vxml_document_fetch_policy_v1>::value,
              "document fetch policy must remain C-compatible");

int main() {
    const auto v1 = vxml_dialog_manager_default_config_v1();
    auto policy = vxml_document_fetch_policy_v1
        VXML_DOCUMENT_FETCH_POLICY_V1_INIT;
    const auto v2 = vxml_dialog_manager_default_config_v2();
    const auto v3 = vxml_dialog_manager_default_config_v3();
    return v1.abi_version == VXML_DIALOG_MANAGER_CONFIG_ABI_V1 &&
           v2.abi_version == VXML_DIALOG_MANAGER_CONFIG_ABI_V2 &&
           v3.abi_version == VXML_DIALOG_MANAGER_CONFIG_ABI_V3 &&
           v3.max_navigation_hops != 0u &&
           policy.abi_version == VXML_DOCUMENT_FETCH_POLICY_ABI_V1 &&
           policy.struct_size == sizeof(policy) &&
           policy.fetchaudio_uri == nullptr &&
           policy.fetchaudio_uri_size == 0u
        ? 0 : 1;
}
