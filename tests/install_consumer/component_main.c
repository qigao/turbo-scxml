#include <scxml/component.h>

int main(void) {
    salts_component_plugin_runtime runtime = SALTS_COMPONENT_PLUGIN_RUNTIME_INIT;
    scxml_component_scope scope = {0};
    if (salts_component_plugin_runtime_init(&runtime) != SALTS_COMPONENT_PLUGIN_OK)
        return 1;
    if (scxml_component_scope_acquire(&scope, &runtime) != SCXML_COMPONENT_ERROR ||
        scope.live || scxml_component_scope_generation_id(&scope) != 0u) {
        salts_component_plugin_runtime_destroy(&runtime);
        return 2;
    }
    return salts_component_plugin_runtime_destroy(&runtime) == SALTS_COMPONENT_PLUGIN_OK
        ? 0 : 3;
}
