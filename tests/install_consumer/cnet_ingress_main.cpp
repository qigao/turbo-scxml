#include <scxml/cnet_ingress.h>
#include <scxml/cnet_egress.h>
#include <scxml/host_router.h>
#include <scxml/host_event_io.h>
#include <scxml/cnet_frame_ingress.h>
#include <salts/error_codes.h>

int main() {
    scxml_host_event_io_binding host = {0};
    const scxml_event_io_adapter *host_ops = scxml_host_event_io_binding_adapter();
    if (host_ops == NULL || host_ops->prepare_send == NULL ||
        host_ops->prepare_cancel != NULL ||
        host_ops->capabilities != SCXML_EVENT_IO_CAP_SEND ||
        scxml_host_event_io_binding_destroy(&host) != SALTS_OK) return 6;
    scxml_host_router router = {0};
    scxml_cnet_frame_ingress frame = {0};
    if (scxml_host_router_destroy(&router) != SALTS_OK ||
        scxml_cnet_frame_ingress_destroy(&frame) != SALTS_OK) return 5;
    scxml_cnet_egress outbound = {0};
    const scxml_event_io_adapter *transport = scxml_cnet_egress_adapter();
    if (transport == NULL || transport->prepare_send == NULL ||
        transport->prepare_cancel != NULL ||
        transport->capabilities != (SCXML_EVENT_IO_CAP_SEND | SCXML_EVENT_IO_CAP_CONTENT) ||
        scxml_cnet_egress_destroy(&outbound) != SALTS_OK) return 4;
    scxml_cnet_ingress ingress = {};
    scxml_cnet_ingress_config config = {};
    if (scxml_cnet_ingress_init(&ingress, &config) != SALTS_EINVAL)
        return 1;
    return scxml_cnet_ingress_destroy(&ingress) == SALTS_OK ? 0 : 2;
}
