#include <scxml/cnet_ingress.h>
#include <scxml/cnet_egress.h>
#include <salts/error_codes.h>

int main() {
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
