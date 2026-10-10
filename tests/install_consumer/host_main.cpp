#include <scxml/host_router.h>
#include <scxml/host_event_io.h>
#include <salts/error_codes.h>

int main() {
    scxml_host_router router = {};
    scxml_host_event_io_binding binding = {};
    const scxml_event_io_adapter *adapter =
        scxml_host_event_io_binding_adapter();
    if (adapter == nullptr || adapter->prepare_send == nullptr ||
        adapter->prepare_cancel != nullptr ||
        adapter->capabilities !=
            (SCXML_EVENT_IO_CAP_SEND | SCXML_EVENT_IO_CAP_CONTENT)) return 1;
    if (scxml_host_event_io_binding_destroy(&binding) != SALTS_OK)
        return 2;
    return scxml_host_router_destroy(&router) == SALTS_OK ? 0 : 3;
}
