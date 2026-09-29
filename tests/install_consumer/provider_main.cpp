#include <scxml/provider.h>

#include <type_traits>

static_assert(std::is_standard_layout<scxml_event_io_provider>::value,
              "Event I/O provider must remain C-compatible");
static_assert(std::is_standard_layout<scxml_invoke_provider>::value,
              "Invoke provider must remain C-compatible");
static_assert(std::is_standard_layout<scxml_data_resource_provider>::value,
              "Data resource provider must remain C-compatible");
static_assert(std::is_standard_layout<scxml_text_resource_provider>::value,
              "Text resource provider must remain C-compatible");

int main() {
    return cmeta_interface_desc_valid(scxml_event_io_provider_interface()) &&
           cmeta_interface_desc_valid(scxml_invoke_provider_interface())
        ? 0 : 1;
}
