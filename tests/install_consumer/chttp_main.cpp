#include <scxml/chttp_resource.h>

int main() {
    scxml_chttp_resource resource{};
    return scxml_chttp_resource_data_adapter(&resource) == nullptr &&
                   scxml_chttp_resource_text_adapter(&resource) == nullptr &&
                   scxml_chttp_resource_destroy(&resource) == SCXML_OK
               ? 0
               : 1;
}
