#include <voicexml/submit_resource.h>

int main(void) {
    vxml_submit_request_v1 request = VXML_SUBMIT_REQUEST_V1_INIT;
    vxml_submit_response response = {0};
    vxml_submit_resource_status (*execute_fn)(
        const vxml_document_store *,
        const vxml_submit_resource_adapter_v1 *,
        void *,
        const vxml_submit_request_v1 *,
        vxml_submit_response *) = vxml_submit_resource_execute;
    vxml_submit_resource_status (*close_fn)(
        const vxml_submit_resource_adapter_v1 *,
        void *,
        vxml_submit_response *) = vxml_submit_resource_close;

    if (request.abi_version != VXML_SUBMIT_REQUEST_ABI_V1 ||
        request.struct_size != sizeof(request) ||
        request.method != VXML_SUBMIT_METHOD_GET ||
        response.data != NULL || response.size != 0u ||
        response.lease != NULL ||
        execute_fn == NULL || close_fn == NULL ||
        VXML_SUBMIT_RESOURCE_ADAPTER_ABI_V1 == 0u ||
        VXML_SUBMIT_WIRE_REQUEST_ABI_V1 == 0u)
        return 1;
    return vxml_submit_resource_status_string(
               VXML_SUBMIT_RESOURCE_OK) != NULL
        ? 0 : 2;
}
