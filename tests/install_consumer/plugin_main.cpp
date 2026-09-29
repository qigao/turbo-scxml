#include <scxml/plugin.h>

#include <type_traits>

static_assert(std::is_standard_layout<scxml_plugin_program>::value,
              "Plugin program handle must remain C-compatible");
static_assert(std::is_standard_layout<scxml_plugin_action_v1>::value,
              "Plugin action mapping must remain C-compatible");
static_assert(std::is_standard_layout<scxml_plugin_compile_options_v1>::value,
              "Plugin compile options must remain C-compatible");

int main() {
    scxml_plugin_program program{};
    return scxml_plugin_program_core(&program) == nullptr ? 0 : 1;
}
