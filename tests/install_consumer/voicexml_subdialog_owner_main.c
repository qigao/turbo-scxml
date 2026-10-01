#include <voicexml/subdialog_owner.h>

int main(void) {
    vxml_cmeta_subdialog_owner_config_v1 config =
        vxml_cmeta_subdialog_owner_default_config_v1();
    vxml_cmeta_subdialog_owner owner = {0};
    vxml_cmeta_subdialog_owner_stats stats = {0};
    const vxml_cmeta_subdialog_adapter_v1 *adapter =
        vxml_cmeta_subdialog_owner_adapter();
    vxml_status (*init_owner)(
        vxml_cmeta_subdialog_owner *,
        const vxml_cmeta_subdialog_owner_config_v1 *) =
        vxml_cmeta_subdialog_owner_init;
    vxml_status (*bind_root)(
        vxml_cmeta_subdialog_owner *, vxml_session *,
        const char *, size_t) =
        vxml_cmeta_subdialog_owner_bind_root;
    vxml_status (*run_ready)(
        vxml_cmeta_subdialog_owner *, size_t, size_t *) =
        vxml_cmeta_subdialog_owner_run_ready;

    if (config.abi_version !=
            VXML_CMETA_SUBDIALOG_OWNER_CONFIG_ABI_V1 ||
        config.capacity == 0u ||
        config.max_navigation_hops == 0u ||
        config.max_nesting_depth == 0u ||
        adapter == NULL ||
        adapter->abi_version !=
            VXML_CMETA_SUBDIALOG_ADAPTER_ABI_V1 ||
        init_owner == NULL ||
        bind_root == NULL ||
        run_ready == NULL ||
        owner.impl != NULL ||
        stats.capacity != 0u)
        return 1;
    return 0;
}
