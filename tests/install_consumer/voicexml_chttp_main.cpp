#include <voicexml/chttp_resource.h>

#include <type_traits>

static_assert(std::is_standard_layout<vxml_chttp_resource>::value,
              "VoiceXML CHTTP resource handle must remain C-compatible");
static_assert(std::is_standard_layout<vxml_chttp_resource_config_v1>::value,
              "VoiceXML CHTTP resource config must remain C-compatible");

int main() {
    const vxml_chttp_resource_config_v1 config =
        VXML_CHTTP_RESOURCE_CONFIG_V1_INIT;
    return config.abi_version == VXML_CHTTP_RESOURCE_CONFIG_ABI_V1 ? 0 : 1;
}
