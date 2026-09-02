#include <scxml/chttp_resource.h>

int main(void) {
    scxml_chttp_resource resource = {0};
    return scxml_chttp_resource_data_adapter(&resource) == NULL &&
                   scxml_chttp_resource_text_adapter(&resource) == NULL &&
                   scxml_chttp_resource_destroy(&resource) == SCXML_OK
               ? 0
               : 1;
}
