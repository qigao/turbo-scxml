#include <salts/plugin.h>
#include <scxml/provider.h>

#include <string.h>

FunctionDecl(value, int, scxml_plugin_fixture_check,
    (int, value, CMETA_PARAM_IN));

int scxml_plugin_fixture_check(int value) {
    return value * 2;
}

static bool SALTS_PLUGIN_CALL fixture_invoke(
    void *context,
    void *return_storage,
    void *const *params,
    size_t param_count) {
    int input;
    int result;
    if (context != NULL || return_storage == NULL ||
        params == NULL || param_count != 1u ||
        params[0] == NULL)
        return false;
    memcpy(&input, params[0], sizeof(input));
    if (input != 8)
        return false;
    result = scxml_plugin_fixture_check(input);
    memcpy(return_storage, &result, sizeof(result));
    return true;
}

typedef struct fixture_provider_state {
    size_t close_count;
} fixture_provider_state;

static scxml_adapter_status fixture_event_send(
    void *self,
    const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    (void)self;
    (void)request;
    (void)out_ticket;
    if (out_error != NULL) *out_error = "fixture";
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static scxml_adapter_status fixture_event_cancel(
    void *self,
    const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    (void)self;
    (void)request;
    (void)out_ticket;
    if (out_error != NULL) *out_error = "fixture";
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static scxml_adapter_status fixture_invoke_start(
    void *self,
    const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    (void)self;
    (void)request;
    (void)out_ticket;
    if (out_error != NULL) *out_error = "fixture";
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static scxml_adapter_status fixture_invoke_cancel(
    void *self,
    const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    (void)self;
    (void)request;
    (void)out_ticket;
    if (out_error != NULL) *out_error = "fixture";
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static scxml_adapter_status fixture_invoke_forward(
    void *self,
    const scxml_invoke_forward_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    (void)self;
    (void)request;
    (void)out_ticket;
    if (out_error != NULL) *out_error = "fixture";
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static void fixture_provider_close(void *self) {
    fixture_provider_state *state = (fixture_provider_state *)self;
    if (state != NULL) ++state->close_count;
}

static bool fixture_provider_quiescent(void *self) {
    return self != NULL;
}

static fixture_provider_state event_state;
static fixture_provider_state invoke_state;

static const scxml_event_io_provider_vtable event_vtable = {
    .implementation = "fixture_event_io",
    .capabilities = SCXML_EVENT_IO_CAP_SEND,
    .prepare_send = fixture_event_send,
    .prepare_cancel = fixture_event_cancel,
    .close = fixture_provider_close,
    .is_quiescent = fixture_provider_quiescent};

static const scxml_invoke_provider_vtable invoke_vtable = {
    .implementation = "fixture_invoke",
    .capabilities = SCXML_INVOKE_CAP_START,
    .prepare_start = fixture_invoke_start,
    .prepare_cancel = fixture_invoke_cancel,
    .prepare_forward = fixture_invoke_forward,
    .close = fixture_provider_close,
    .is_quiescent = fixture_provider_quiescent};

static scxml_event_io_provider event_provider;
static scxml_invoke_provider invoke_provider;
static salts_plugin_export fixture_exports[3];

static const salts_plugin_manifest fixture_manifest = {
    .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,
    .abi_version = SALTS_PLUGIN_ABI_VERSION,
    .plugin_id = "test.scxml.plugin",
    .version = {1u, 0u, 0u},
    .exports = fixture_exports,
    .export_count = 3u};

SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
    if (host_abi != SALTS_PLUGIN_ABI_VERSION)
        return NULL;

    event_provider =
        scxml_event_io_provider_bind(&event_state, &event_vtable);
    invoke_provider =
        scxml_invoke_provider_bind(&invoke_state, &invoke_vtable);

    fixture_exports[0] = (salts_plugin_export){
        .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
        .kind = SALTS_PLUGIN_EXPORT_FUNCTION,
        .contract_version = 1u,
        .capabilities = UINT64_C(1),
        .export_id = "test.scxml.action.check",
        .contract_id = "test.scxml.action",
        .value.function = {
            .desc = FunctionMeta(scxml_plugin_fixture_check),
            .abi = FunctionAbi(scxml_plugin_fixture_check),
            .context = NULL,
            .invoke = fixture_invoke}};

    fixture_exports[1] = (salts_plugin_export){
        .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
        .kind = SALTS_PLUGIN_EXPORT_INTERFACE,
        .contract_version = 1u,
        .capabilities = SCXML_EVENT_IO_CAP_SEND,
        .export_id = "test.scxml.provider.event-io",
        .contract_id = "test.scxml.event-io",
        .value.interface = {
            .desc = scxml_event_io_provider_interface(),
            .value = &event_provider}};

    fixture_exports[2] = (salts_plugin_export){
        .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
        .kind = SALTS_PLUGIN_EXPORT_INTERFACE,
        .contract_version = 1u,
        .capabilities = SCXML_INVOKE_CAP_START,
        .export_id = "test.scxml.provider.invoke",
        .contract_id = "test.scxml.invoke",
        .value.interface = {
            .desc = scxml_invoke_provider_interface(),
            .value = &invoke_provider}};

    return &fixture_manifest;
}
