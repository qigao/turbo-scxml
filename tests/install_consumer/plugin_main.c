#include <scxml/plugin.h>

#include <string.h>

int main(void) {
    scxml_plugin_program program = {0};
    scxml_plugin_compile_options_v1 options =
        SCXML_PLUGIN_COMPILE_OPTIONS_V1_INIT;
    scxml_plugin_action_v1 action =
        SCXML_PLUGIN_ACTION_V1_INIT;

    if (options.abi_version != SCXML_PLUGIN_COMPILE_OPTIONS_ABI_V1)
        return 1;
    if (action.struct_size != sizeof(scxml_plugin_action_v1))
        return 2;
    if (scxml_plugin_program_core(&program) != NULL)
        return 3;
    return strcmp(scxml_plugin_status_string(SCXML_PLUGIN_OK), "ok") == 0
        ? 0 : 4;
}
