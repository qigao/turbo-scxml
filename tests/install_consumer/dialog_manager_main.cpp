#include <voicexml/dialog_manager.h>

#include <type_traits>

static_assert(std::is_standard_layout<vxml_dialog_manager>::value,
              "dialog manager handle must remain C-compatible");
static_assert(std::is_standard_layout<vxml_dialog_manager_config_v1>::value,
              "dialog manager config must remain C-compatible");
static_assert(std::is_standard_layout<vxml_dialog_event_v1>::value,
              "dialog manager event must remain C-compatible");

int main() {
    const auto config = vxml_dialog_manager_default_config_v1();
    return config.abi_version == VXML_DIALOG_MANAGER_CONFIG_ABI_V1 ? 0 : 1;
}
