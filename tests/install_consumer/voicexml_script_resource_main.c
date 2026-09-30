#include <voicexml/script_resource.h>

int main(void) {
    vxml_script_request_v1 request = VXML_SCRIPT_REQUEST_V1_INIT;
    vxml_script_source source = {0};
    vxml_script_resource_status (*acquire_fn)(
        const vxml_document_store *,
        const vxml_script_resource_adapter_v1 *,
        void *,
        const vxml_script_request_v1 *,
        vxml_script_source *) = vxml_script_resource_acquire;
    vxml_script_resource_status (*close_fn)(
        const vxml_script_resource_adapter_v1 *,
        void *,
        vxml_script_source *) = vxml_script_resource_close;

    if (request.abi_version != VXML_SCRIPT_REQUEST_ABI_V1 ||
        request.struct_size != sizeof(request) ||
        source.data != NULL || source.size != 0u || source.lease != NULL ||
        acquire_fn == NULL || close_fn == NULL ||
        VXML_SCRIPT_RESOURCE_ADAPTER_ABI_V1 == 0u)
        return 1;
    return vxml_script_resource_status_string(
               VXML_SCRIPT_RESOURCE_OK) != NULL
        ? 0 : 2;
}
