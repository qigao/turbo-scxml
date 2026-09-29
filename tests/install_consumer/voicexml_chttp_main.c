#include <voicexml/chttp_resource.h>

int main(void) {
    vxml_chttp_resource resource = {0};
    vxml_chttp_resource_config_v1 config =
        VXML_CHTTP_RESOURCE_CONFIG_V1_INIT;
    scxml_resource_status status = SCXML_RESOURCE_FAILED;

    if (resource.impl != NULL)
        return 1;
    if (config.abi_version != VXML_CHTTP_RESOURCE_CONFIG_ABI_V1)
        return 2;
    if (vxml_chttp_resource_document_adapter(&resource) != NULL)
        return 3;
    if (vxml_chttp_resource_last_status(&resource, &status))
        return 4;
    return 0;
}
