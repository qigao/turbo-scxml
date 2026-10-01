#include <voicexml/subdialog_owner.h>

#include <type_traits>

static_assert(
    std::is_standard_layout<vxml_cmeta_subdialog_owner>::value,
    "subdialog owner handle must remain C-compatible");
static_assert(
    std::is_standard_layout<
        vxml_cmeta_subdialog_owner_config_v1>::value,
    "subdialog owner config must remain C-compatible");
static_assert(
    std::is_standard_layout<
        vxml_cmeta_subdialog_owner_stats>::value,
    "subdialog owner stats must remain C-compatible");

int main() {
    auto config =
        vxml_cmeta_subdialog_owner_default_config_v1();
    auto *adapter =
        vxml_cmeta_subdialog_owner_adapter();
    auto init_owner =
        &vxml_cmeta_subdialog_owner_init;
    auto root_user =
        &vxml_cmeta_subdialog_owner_root_user;
    auto run_ready =
        &vxml_cmeta_subdialog_owner_run_ready;
    return config.abi_version ==
               VXML_CMETA_SUBDIALOG_OWNER_CONFIG_ABI_V1 &&
           adapter != nullptr &&
           init_owner != nullptr &&
           root_user != nullptr &&
           run_ready != nullptr
        ? 0 : 1;
}
