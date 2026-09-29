#include <salts/plugin.h>
#include <salts/thread.h>

#include <string.h>

FunctionDecl(value, int, scxml_plugin_fixture_check,
    (int, value, CMETA_PARAM_IN));

int scxml_plugin_fixture_check(int value) {
    return value * 2;
}

static bool SALTS_PLUGIN_CALL fixture_invoke(
    void *context,
    void *return_storage,
    void *const *params,
    size_t param_count) {
    int input;
    int result;
    if (context != NULL || return_storage == NULL ||
        params == NULL || param_count != 1u ||
        params[0] == NULL)
        return false;
    memcpy(&input, params[0], sizeof(input));
    if (input != 8)
        return false;
    result = scxml_plugin_fixture_check(input);
    memcpy(return_storage, &result, sizeof(result));
    return true;
}

static salts_plugin_export fixture_export;
static salts_once_t fixture_once = SALTS_ONCE_INIT;

static void fixture_init(void) {
    fixture_export = (salts_plugin_export){
        .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
        .kind = SALTS_PLUGIN_EXPORT_FUNCTION,
        .contract_version = 1u,
        .capabilities = UINT64_C(1),
        .export_id = "test.scxml.action.check",
        .contract_id = "test.scxml.action",
        .value.function = {
            .desc = FunctionMeta(scxml_plugin_fixture_check),
            .abi = FunctionAbi(scxml_plugin_fixture_check),
            .context = NULL,
            .invoke = fixture_invoke}};
}

static const salts_plugin_manifest fixture_manifest = {
    .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,
    .abi_version = SALTS_PLUGIN_ABI_VERSION,
    .plugin_id = "test.scxml.plugin",
    .version = {1u, 0u, 0u},
    .exports = &fixture_export,
    .export_count = 1u};

SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
    if (host_abi != SALTS_PLUGIN_ABI_VERSION)
        return NULL;
    salts_once(&fixture_once, fixture_init);
    return &fixture_manifest;
}
