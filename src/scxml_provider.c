#include <scxml/provider.h>

#include <string.h>

static bool event_io_capabilities_valid(uint64_t capabilities) {
    const uint64_t known =
        SCXML_EVENT_IO_CAP_SEND |
        SCXML_EVENT_IO_CAP_DELAYED_SEND |
        SCXML_EVENT_IO_CAP_CANCEL |
        SCXML_EVENT_IO_CAP_PAYLOAD |
        SCXML_EVENT_IO_CAP_CONTENT;
    if ((capabilities & ~known) != 0u)
        return false;
    if ((capabilities &
         (SCXML_EVENT_IO_CAP_PAYLOAD |
          SCXML_EVENT_IO_CAP_CONTENT |
          SCXML_EVENT_IO_CAP_DELAYED_SEND)) != 0u &&
        (capabilities & SCXML_EVENT_IO_CAP_SEND) == 0u)
        return false;
    if ((capabilities & SCXML_EVENT_IO_CAP_CANCEL) != 0u &&
        (capabilities & SCXML_EVENT_IO_CAP_DELAYED_SEND) == 0u)
        return false;
    return true;
}

static bool invoke_capabilities_valid(uint64_t capabilities) {
    const uint64_t known =
        SCXML_INVOKE_CAP_START |
        SCXML_INVOKE_CAP_CANCEL |
        SCXML_INVOKE_CAP_FORWARD |
        SCXML_INVOKE_CAP_PAYLOAD |
        SCXML_INVOKE_CAP_CONTENT;
    if ((capabilities & ~known) != 0u)
        return false;
    if ((capabilities &
         (SCXML_INVOKE_CAP_PAYLOAD |
          SCXML_INVOKE_CAP_CONTENT)) != 0u &&
        (capabilities & SCXML_INVOKE_CAP_START) == 0u)
        return false;
    return true;
}

static bool event_io_adapter_valid_for_bridge(
    const scxml_event_io_adapter *adapter) {
    if (adapter == NULL ||
        adapter->abi_version != SCXML_ADAPTER_ABI ||
        adapter->struct_size != sizeof(*adapter) ||
        !event_io_capabilities_valid(adapter->capabilities) ||
        adapter->close == NULL ||
        adapter->is_quiescent == NULL)
        return false;
    if ((adapter->capabilities & SCXML_EVENT_IO_CAP_SEND) != 0u &&
        adapter->prepare_send == NULL)
        return false;
    if ((adapter->capabilities & SCXML_EVENT_IO_CAP_CANCEL) != 0u &&
        adapter->prepare_cancel == NULL)
        return false;
    return true;
}

static bool invoke_adapter_valid_for_bridge(
    const scxml_invoke_adapter *adapter) {
    if (adapter == NULL ||
        adapter->abi_version != SCXML_ADAPTER_ABI ||
        adapter->struct_size != sizeof(*adapter) ||
        !invoke_capabilities_valid(adapter->capabilities) ||
        adapter->close == NULL ||
        adapter->is_quiescent == NULL)
        return false;
    if ((adapter->capabilities & SCXML_INVOKE_CAP_START) != 0u &&
        adapter->prepare_start == NULL)
        return false;
    if ((adapter->capabilities & SCXML_INVOKE_CAP_CANCEL) != 0u &&
        adapter->prepare_cancel == NULL)
        return false;
    if ((adapter->capabilities & SCXML_INVOKE_CAP_FORWARD) != 0u &&
        adapter->prepare_forward == NULL)
        return false;
    return true;
}

static scxml_adapter_status event_io_provider_prepare_send(
    void *self,
    const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    scxml_event_io_provider_bridge *bridge =
        (scxml_event_io_provider_bridge *)self;
    if (bridge == NULL || bridge->adapter.prepare_send == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    return bridge->adapter.prepare_send(
        bridge->adapter_user, request, out_ticket, out_error);
}

static scxml_adapter_status event_io_provider_prepare_cancel(
    void *self,
    const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    scxml_event_io_provider_bridge *bridge =
        (scxml_event_io_provider_bridge *)self;
    if (bridge == NULL || bridge->adapter.prepare_cancel == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    return bridge->adapter.prepare_cancel(
        bridge->adapter_user, request, out_ticket, out_error);
}

static void event_io_provider_close(void *self) {
    scxml_event_io_provider_bridge *bridge =
        (scxml_event_io_provider_bridge *)self;
    if (bridge != NULL && bridge->adapter.close != NULL)
        bridge->adapter.close(bridge->adapter_user);
}

static bool event_io_provider_is_quiescent(void *self) {
    scxml_event_io_provider_bridge *bridge =
        (scxml_event_io_provider_bridge *)self;
    return bridge != NULL &&
           bridge->adapter.is_quiescent != NULL &&
           bridge->adapter.is_quiescent(bridge->adapter_user);
}

bool scxml_event_io_provider_bridge_init(
    scxml_event_io_provider_bridge *bridge,
    const scxml_event_io_adapter *adapter,
    void *adapter_user) {
    if (bridge == NULL ||
        !event_io_adapter_valid_for_bridge(adapter))
        return false;
    memset(bridge, 0, sizeof(*bridge));
    bridge->adapter = *adapter;
    bridge->adapter_user = adapter_user;
    bridge->vtable = (scxml_event_io_provider_vtable){
        .implementation = "scxml_event_io_adapter",
        .capabilities = adapter->capabilities,
        .prepare_send = event_io_provider_prepare_send,
        .prepare_cancel = event_io_provider_prepare_cancel,
        .close = event_io_provider_close,
        .is_quiescent = event_io_provider_is_quiescent};
    bridge->provider =
        scxml_event_io_provider_bind(bridge, &bridge->vtable);
    return scxml_event_io_provider_valid(&bridge->provider);
}

scxml_event_io_provider *scxml_event_io_provider_bridge_get(
    scxml_event_io_provider_bridge *bridge) {
    return bridge != NULL &&
           scxml_event_io_provider_valid(&bridge->provider)
        ? &bridge->provider : NULL;
}

static scxml_adapter_status invoke_provider_prepare_start(
    void *self,
    const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    scxml_invoke_provider_bridge *bridge =
        (scxml_invoke_provider_bridge *)self;
    if (bridge == NULL || bridge->adapter.prepare_start == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    return bridge->adapter.prepare_start(
        bridge->adapter_user, request, out_ticket, out_error);
}

static scxml_adapter_status invoke_provider_prepare_cancel(
    void *self,
    const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    scxml_invoke_provider_bridge *bridge =
        (scxml_invoke_provider_bridge *)self;
    if (bridge == NULL || bridge->adapter.prepare_cancel == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    return bridge->adapter.prepare_cancel(
        bridge->adapter_user, request, out_ticket, out_error);
}

static scxml_adapter_status invoke_provider_prepare_forward(
    void *self,
    const scxml_invoke_forward_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    scxml_invoke_provider_bridge *bridge =
        (scxml_invoke_provider_bridge *)self;
    if (bridge == NULL || bridge->adapter.prepare_forward == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    return bridge->adapter.prepare_forward(
        bridge->adapter_user, request, out_ticket, out_error);
}

static void invoke_provider_close(void *self) {
    scxml_invoke_provider_bridge *bridge =
        (scxml_invoke_provider_bridge *)self;
    if (bridge != NULL && bridge->adapter.close != NULL)
        bridge->adapter.close(bridge->adapter_user);
}

static bool invoke_provider_is_quiescent(void *self) {
    scxml_invoke_provider_bridge *bridge =
        (scxml_invoke_provider_bridge *)self;
    return bridge != NULL &&
           bridge->adapter.is_quiescent != NULL &&
           bridge->adapter.is_quiescent(bridge->adapter_user);
}

bool scxml_invoke_provider_bridge_init(
    scxml_invoke_provider_bridge *bridge,
    const scxml_invoke_adapter *adapter,
    void *adapter_user) {
    if (bridge == NULL ||
        !invoke_adapter_valid_for_bridge(adapter))
        return false;
    memset(bridge, 0, sizeof(*bridge));
    bridge->adapter = *adapter;
    bridge->adapter_user = adapter_user;
    bridge->vtable = (scxml_invoke_provider_vtable){
        .implementation = "scxml_invoke_adapter",
        .capabilities = adapter->capabilities,
        .prepare_start = invoke_provider_prepare_start,
        .prepare_cancel = invoke_provider_prepare_cancel,
        .prepare_forward = invoke_provider_prepare_forward,
        .close = invoke_provider_close,
        .is_quiescent = invoke_provider_is_quiescent};
    bridge->provider =
        scxml_invoke_provider_bind(bridge, &bridge->vtable);
    return scxml_invoke_provider_valid(&bridge->provider);
}

scxml_invoke_provider *scxml_invoke_provider_bridge_get(
    scxml_invoke_provider_bridge *bridge) {
    return bridge != NULL &&
           scxml_invoke_provider_valid(&bridge->provider)
        ? &bridge->provider : NULL;
}

static scxml_adapter_status event_io_adapter_prepare_send(
    void *user,
    const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    scxml_event_io_adapter_bridge *bridge =
        (scxml_event_io_adapter_bridge *)user;
    if (bridge == NULL ||
        !scxml_event_io_provider_valid(&bridge->provider))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    return scxml_event_io_provider_prepare_send(
        &bridge->provider, request, out_ticket, out_error);
}

static scxml_adapter_status event_io_adapter_prepare_cancel(
    void *user,
    const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    scxml_event_io_adapter_bridge *bridge =
        (scxml_event_io_adapter_bridge *)user;
    if (bridge == NULL ||
        !scxml_event_io_provider_valid(&bridge->provider))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    return scxml_event_io_provider_prepare_cancel(
        &bridge->provider, request, out_ticket, out_error);
}

static void event_io_adapter_close(void *user) {
    scxml_event_io_adapter_bridge *bridge =
        (scxml_event_io_adapter_bridge *)user;
    if (bridge != NULL &&
        scxml_event_io_provider_valid(&bridge->provider))
        scxml_event_io_provider_close(&bridge->provider);
}

static bool event_io_adapter_is_quiescent(void *user) {
    scxml_event_io_adapter_bridge *bridge =
        (scxml_event_io_adapter_bridge *)user;
    return bridge != NULL &&
           scxml_event_io_provider_valid(&bridge->provider) &&
           scxml_event_io_provider_is_quiescent(&bridge->provider);
}

bool scxml_event_io_adapter_bridge_init(
    scxml_event_io_adapter_bridge *bridge,
    const scxml_event_io_provider *provider) {
    uint64_t capabilities;
    if (bridge == NULL ||
        !scxml_event_io_provider_valid(provider))
        return false;
    capabilities = scxml_event_io_provider_capabilities(provider);
    if (!event_io_capabilities_valid(capabilities))
        return false;
    memset(bridge, 0, sizeof(*bridge));
    bridge->provider = *provider;
    bridge->adapter = (scxml_event_io_adapter){
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(scxml_event_io_adapter),
        .capabilities = capabilities,
        .prepare_send = event_io_adapter_prepare_send,
        .prepare_cancel = event_io_adapter_prepare_cancel,
        .close = event_io_adapter_close,
        .is_quiescent = event_io_adapter_is_quiescent};
    return true;
}

const scxml_event_io_adapter *scxml_event_io_adapter_bridge_get(
    const scxml_event_io_adapter_bridge *bridge) {
    return bridge != NULL ? &bridge->adapter : NULL;
}

void *scxml_event_io_adapter_bridge_user(
    scxml_event_io_adapter_bridge *bridge) {
    return bridge;
}

static scxml_adapter_status invoke_adapter_prepare_start(
    void *user,
    const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    scxml_invoke_adapter_bridge *bridge =
        (scxml_invoke_adapter_bridge *)user;
    if (bridge == NULL ||
        !scxml_invoke_provider_valid(&bridge->provider))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    return scxml_invoke_provider_prepare_start(
        &bridge->provider, request, out_ticket, out_error);
}

static scxml_adapter_status invoke_adapter_prepare_cancel(
    void *user,
    const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    scxml_invoke_adapter_bridge *bridge =
        (scxml_invoke_adapter_bridge *)user;
    if (bridge == NULL ||
        !scxml_invoke_provider_valid(&bridge->provider))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    return scxml_invoke_provider_prepare_cancel(
        &bridge->provider, request, out_ticket, out_error);
}

static scxml_adapter_status invoke_adapter_prepare_forward(
    void *user,
    const scxml_invoke_forward_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    scxml_invoke_adapter_bridge *bridge =
        (scxml_invoke_adapter_bridge *)user;
    if (bridge == NULL ||
        !scxml_invoke_provider_valid(&bridge->provider))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    return scxml_invoke_provider_prepare_forward(
        &bridge->provider, request, out_ticket, out_error);
}

static void invoke_adapter_close(void *user) {
    scxml_invoke_adapter_bridge *bridge =
        (scxml_invoke_adapter_bridge *)user;
    if (bridge != NULL &&
        scxml_invoke_provider_valid(&bridge->provider))
        scxml_invoke_provider_close(&bridge->provider);
}

static bool invoke_adapter_is_quiescent(void *user) {
    scxml_invoke_adapter_bridge *bridge =
        (scxml_invoke_adapter_bridge *)user;
    return bridge != NULL &&
           scxml_invoke_provider_valid(&bridge->provider) &&
           scxml_invoke_provider_is_quiescent(&bridge->provider);
}

bool scxml_invoke_adapter_bridge_init(
    scxml_invoke_adapter_bridge *bridge,
    const scxml_invoke_provider *provider) {
    uint64_t capabilities;
    if (bridge == NULL ||
        !scxml_invoke_provider_valid(provider))
        return false;
    capabilities = scxml_invoke_provider_capabilities(provider);
    if (!invoke_capabilities_valid(capabilities))
        return false;
    memset(bridge, 0, sizeof(*bridge));
    bridge->provider = *provider;
    bridge->adapter = (scxml_invoke_adapter){
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(scxml_invoke_adapter),
        .capabilities = capabilities,
        .prepare_start = invoke_adapter_prepare_start,
        .prepare_cancel = invoke_adapter_prepare_cancel,
        .prepare_forward = invoke_adapter_prepare_forward,
        .close = invoke_adapter_close,
        .is_quiescent = invoke_adapter_is_quiescent};
    return true;
}

const scxml_invoke_adapter *scxml_invoke_adapter_bridge_get(
    const scxml_invoke_adapter_bridge *bridge) {
    return bridge != NULL ? &bridge->adapter : NULL;
}

void *scxml_invoke_adapter_bridge_user(
    scxml_invoke_adapter_bridge *bridge) {
    return bridge;
}
