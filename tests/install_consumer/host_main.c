#include <scxml/host_router.h>
#include <scxml/host_event_io.h>
#include <salts/error_codes.h>

int main(void) {
    scxml_host_router router = {0};
    scxml_host_event_io_binding binding = {0};
    const scxml_event_io_adapter *adapter =
        scxml_host_event_io_binding_adapter();
    if (adapter == NULL || adapter->prepare_send == NULL ||
        adapter->prepare_cancel != NULL ||
        adapter->capabilities !=
            (SCXML_EVENT_IO_CAP_SEND | SCXML_EVENT_IO_CAP_CONTENT)) return 1;
    if (scxml_host_event_io_binding_destroy(&binding) != SALTS_OK)
        return 2;
    return scxml_host_router_destroy(&router) == SALTS_OK ? 0 : 3;
}
