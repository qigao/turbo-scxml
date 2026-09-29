#include <scxml/provider.h>

int main(void) {
    const cmeta_interface_desc *event_meta =
        scxml_event_io_provider_interface();
    const cmeta_interface_desc *invoke_meta =
        scxml_invoke_provider_interface();
    const cmeta_interface_desc *data_meta =
        scxml_data_resource_provider_interface();
    const cmeta_interface_desc *text_meta =
        scxml_text_resource_provider_interface();

    return cmeta_interface_desc_valid(event_meta) &&
           cmeta_interface_desc_valid(invoke_meta) &&
           cmeta_interface_desc_valid(data_meta) &&
           cmeta_interface_desc_valid(text_meta)
        ? 0 : 1;
}
