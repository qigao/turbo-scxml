#include <scxml/provider.h>
#include <tinytest.h>

#include <string.h>

typedef struct provider_probe {
    size_t event_send_calls;
    size_t event_cancel_calls;
    size_t invoke_start_calls;
    size_t invoke_cancel_calls;
    size_t invoke_forward_calls;
    size_t close_calls;
    size_t data_open_calls;
    size_t data_close_calls;
    size_t text_open_calls;
    size_t text_close_calls;
    bool quiescent;
} provider_probe;

static scxml_adapter_status probe_event_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    (void)request;
    (void)out_ticket;
    if (out_error != NULL) *out_error = "probe";
    if (probe != NULL) ++probe->event_send_calls;
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static scxml_adapter_status probe_event_cancel(
    void *user, const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    (void)request;
    (void)out_ticket;
    if (out_error != NULL) *out_error = "probe";
    if (probe != NULL) ++probe->event_cancel_calls;
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static scxml_adapter_status probe_invoke_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    (void)request;
    (void)out_ticket;
    if (out_error != NULL) *out_error = "probe";
    if (probe != NULL) ++probe->invoke_start_calls;
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static scxml_adapter_status probe_invoke_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    (void)request;
    (void)out_ticket;
    if (out_error != NULL) *out_error = "probe";
    if (probe != NULL) ++probe->invoke_cancel_calls;
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static scxml_adapter_status probe_invoke_forward(
    void *user, const scxml_invoke_forward_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    (void)request;
    (void)out_ticket;
    if (out_error != NULL) *out_error = "probe";
    if (probe != NULL) ++probe->invoke_forward_calls;
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static void probe_close(void *user) {
    provider_probe *probe = (provider_probe *)user;
    if (probe != NULL) ++probe->close_calls;
}

static bool probe_quiescent(void *user) {
    provider_probe *probe = (provider_probe *)user;
    return probe != NULL && probe->quiescent;
}

static scxml_resource_status probe_data_open(
    void *user, const char *uri, size_t uri_size,
    size_t max_bytes, scxml_data_resource_v2 *out) {
    provider_probe *probe = (provider_probe *)user;
    static const char data[] = "7";
    (void)uri;
    (void)uri_size;
    if (probe == NULL || out == NULL || max_bytes < sizeof(data) - 1u)
        return SCXML_RESOURCE_FAILED;
    ++probe->data_open_calls;
    *out = (scxml_data_resource_v2){
        .data = data,
        .size = sizeof(data) - 1u,
        .format = DATA_BIND_FORMAT_JSON,
        .lease = probe};
    return SCXML_RESOURCE_OK;
}

static void probe_data_close(
    void *user, scxml_data_resource_v2 *resource) {
    provider_probe *probe = (provider_probe *)user;
    if (probe != NULL && resource != NULL && resource->lease == probe)
        ++probe->data_close_calls;
    if (resource != NULL) memset(resource, 0, sizeof(*resource));
}

static scxml_resource_status probe_text_open(
    void *user, const char *uri, size_t uri_size,
    size_t max_bytes, scxml_text_resource *out) {
    provider_probe *probe = (provider_probe *)user;
    static const char text[] = "script";
    (void)uri;
    (void)uri_size;
    if (probe == NULL || out == NULL || max_bytes < sizeof(text) - 1u)
        return SCXML_RESOURCE_FAILED;
    ++probe->text_open_calls;
    *out = (scxml_text_resource){
        .data = text,
        .size = sizeof(text) - 1u,
        .lease = probe};
    return SCXML_RESOURCE_OK;
}

static void probe_text_close(
    void *user, scxml_text_resource *resource) {
    provider_probe *probe = (provider_probe *)user;
    if (probe != NULL && resource != NULL && resource->lease == probe)
        ++probe->text_close_calls;
    if (resource != NULL) memset(resource, 0, sizeof(*resource));
}

spec("TurboSCXML CMeta provider interfaces") {
    it("publishes stable Event I/O and Invoke interface descriptors") {
        const cmeta_interface_desc *event_meta =
            scxml_event_io_provider_interface();
        const cmeta_interface_desc *invoke_meta =
            scxml_invoke_provider_interface();

        check_true(cmeta_interface_desc_valid(event_meta));
        check_equal(event_meta->name, "scxml_event_io_provider");
        check_equal(event_meta->method_count, (size_t)4u);
        check_equal(event_meta->methods[0].name, "prepare_send");
        check_equal(cmeta_interface_method_arity(&event_meta->methods[0]),
                    (size_t)3u);
        check_equal(event_meta->methods[1].name, "prepare_cancel");
        check_equal(event_meta->methods[2].name, "close");
        check_equal(event_meta->methods[3].name, "is_quiescent");
        check_null(event_meta->methods[0].function);
        check_null(event_meta->methods[0].abi);

        check_true(cmeta_interface_desc_valid(invoke_meta));
        check_equal(invoke_meta->name, "scxml_invoke_provider");
        check_equal(invoke_meta->method_count, (size_t)5u);
        check_equal(invoke_meta->methods[0].name, "prepare_start");
        check_equal(invoke_meta->methods[1].name, "prepare_cancel");
        check_equal(invoke_meta->methods[2].name, "prepare_forward");
        check_equal(invoke_meta->methods[3].name, "close");
        check_equal(invoke_meta->methods[4].name, "is_quiescent");

        {
            const cmeta_interface_desc *data_meta =
                scxml_data_resource_provider_interface();
            const cmeta_interface_desc *text_meta =
                scxml_text_resource_provider_interface();
            check_true(cmeta_interface_desc_valid(data_meta));
            check_equal(data_meta->method_count, (size_t)2u);
            check_equal(data_meta->methods[0].name, "open");
            check_equal(cmeta_interface_method_arity(&data_meta->methods[0]),
                        (size_t)4u);
            check_equal(data_meta->methods[1].name, "close");
            check_true(cmeta_interface_desc_valid(text_meta));
            check_equal(text_meta->method_count, (size_t)2u);
            check_equal(cmeta_interface_method_arity(&text_meta->methods[0]),
                        (size_t)4u);
        }
    }

    it("projects a static Event I/O adapter through CMeta Interface and back") {
        provider_probe probe = {0};
        const scxml_event_io_adapter adapter = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_event_io_adapter),
            .capabilities =
                SCXML_EVENT_IO_CAP_SEND |
                SCXML_EVENT_IO_CAP_DELAYED_SEND |
                SCXML_EVENT_IO_CAP_CANCEL,
            .prepare_send = probe_event_send,
            .prepare_cancel = probe_event_cancel,
            .close = probe_close,
            .is_quiescent = probe_quiescent};
        scxml_event_io_provider_bridge provider_bridge = {0};
        scxml_event_io_adapter_bridge adapter_bridge = {0};
        scxml_event_io_provider *provider;
        const scxml_event_io_adapter *round_trip;
        scxml_send_request send = {0};
        scxml_cancel_request cancel = {0};
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;

        check_true(scxml_event_io_provider_bridge_init(
            &provider_bridge, &adapter, &probe));
        provider = scxml_event_io_provider_bridge_get(&provider_bridge);
        check_not_null(provider);
        check_true(scxml_event_io_provider_has(
            provider, SCXML_EVENT_IO_CAP_SEND));
        check_equal(scxml_event_io_provider_prepare_send(
                        provider, &send, &ticket, &error),
                    SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(probe.event_send_calls, (size_t)1u);

        check_true(scxml_event_io_adapter_bridge_init(
            &adapter_bridge, provider));
        round_trip = scxml_event_io_adapter_bridge_get(&adapter_bridge);
        check_not_null(round_trip);
        check_equal(round_trip->capabilities, adapter.capabilities);
        check_equal(round_trip->prepare_cancel(
                        scxml_event_io_adapter_bridge_user(&adapter_bridge),
                        &cancel, &ticket, &error),
                    SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(probe.event_cancel_calls, (size_t)1u);

        probe.quiescent = true;
        round_trip->close(
            scxml_event_io_adapter_bridge_user(&adapter_bridge));
        check_equal(probe.close_calls, (size_t)1u);
        check_true(round_trip->is_quiescent(
            scxml_event_io_adapter_bridge_user(&adapter_bridge)));
    }

    it("projects a static Invoke adapter through CMeta Interface and back") {
        provider_probe probe = {0};
        const scxml_invoke_adapter adapter = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_invoke_adapter),
            .capabilities =
                SCXML_INVOKE_CAP_START |
                SCXML_INVOKE_CAP_CANCEL |
                SCXML_INVOKE_CAP_FORWARD,
            .prepare_start = probe_invoke_start,
            .prepare_cancel = probe_invoke_cancel,
            .prepare_forward = probe_invoke_forward,
            .close = probe_close,
            .is_quiescent = probe_quiescent};
        scxml_invoke_provider_bridge provider_bridge = {0};
        scxml_invoke_adapter_bridge adapter_bridge = {0};
        scxml_invoke_provider *provider;
        const scxml_invoke_adapter *round_trip;
        scxml_invoke_start_request start = {0};
        scxml_invoke_forward_request forward = {0};
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;

        check_true(scxml_invoke_provider_bridge_init(
            &provider_bridge, &adapter, &probe));
        provider = scxml_invoke_provider_bridge_get(&provider_bridge);
        check_not_null(provider);
        check_true(scxml_invoke_provider_has(
            provider, SCXML_INVOKE_CAP_FORWARD));
        check_equal(scxml_invoke_provider_prepare_start(
                        provider, &start, &ticket, &error),
                    SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(probe.invoke_start_calls, (size_t)1u);

        check_true(scxml_invoke_adapter_bridge_init(
            &adapter_bridge, provider));
        round_trip = scxml_invoke_adapter_bridge_get(&adapter_bridge);
        check_not_null(round_trip);
        check_equal(round_trip->prepare_forward(
                        scxml_invoke_adapter_bridge_user(&adapter_bridge),
                        &forward, &ticket, &error),
                    SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(probe.invoke_forward_calls, (size_t)1u);

        probe.quiescent = true;
        round_trip->close(
            scxml_invoke_adapter_bridge_user(&adapter_bridge));
        check_equal(probe.close_calls, (size_t)1u);
        check_true(round_trip->is_quiescent(
            scxml_invoke_adapter_bridge_user(&adapter_bridge)));
    }

    it("rejects capability contradictions before bridge publication") {
        provider_probe probe = {0};
        const scxml_event_io_adapter invalid_event = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_event_io_adapter),
            .capabilities = SCXML_EVENT_IO_CAP_CONTENT,
            .close = probe_close,
            .is_quiescent = probe_quiescent};
        const scxml_invoke_adapter invalid_invoke = {
            .abi_version = SCXML_ADAPTER_ABI,
            .struct_size = sizeof(scxml_invoke_adapter),
            .capabilities = SCXML_INVOKE_CAP_PAYLOAD,
            .close = probe_close,
            .is_quiescent = probe_quiescent};
        scxml_event_io_provider_bridge event_bridge = {0};
        scxml_invoke_provider_bridge invoke_bridge = {0};

        check_false(scxml_event_io_provider_bridge_init(
            &event_bridge, &invalid_event, &probe));
        check_false(scxml_invoke_provider_bridge_init(
            &invoke_bridge, &invalid_invoke, &probe));
    }

    it("projects canonical raw data resources through CMeta Interface and back") {
        provider_probe probe = {0};
        const scxml_data_resource_adapter_v2 adapter = {
            .abi_version = SCXML_DATA_RESOURCE_ADAPTER_ABI_V2,
            .struct_size = sizeof(scxml_data_resource_adapter_v2),
            .open = probe_data_open,
            .close = probe_data_close};
        scxml_data_resource_provider_bridge provider_bridge = {0};
        scxml_data_resource_adapter_bridge adapter_bridge = {0};
        scxml_data_resource_provider *provider;
        const scxml_data_resource_adapter_v2 *round_trip;
        scxml_data_resource_v2 resource = {0};

        check_true(scxml_data_resource_provider_bridge_init(
            &provider_bridge, &adapter, &probe));
        provider = scxml_data_resource_provider_bridge_get(&provider_bridge);
        check_not_null(provider);
        check_equal(scxml_data_resource_provider_open(
                        provider, "mem:x", 5u, 16u, &resource),
                    SCXML_RESOURCE_OK);
        check_equal(probe.data_open_calls, (size_t)1u);
        scxml_data_resource_provider_close(provider, &resource);
        check_equal(probe.data_close_calls, (size_t)1u);

        check_true(scxml_data_resource_adapter_bridge_init(
            &adapter_bridge, provider));
        round_trip = scxml_data_resource_adapter_bridge_get(&adapter_bridge);
        check_not_null(round_trip);
        check_equal(round_trip->open(
                        scxml_data_resource_adapter_bridge_user(&adapter_bridge),
                        "mem:x", 5u, 16u, &resource),
                    SCXML_RESOURCE_OK);
        check_equal(probe.data_open_calls, (size_t)2u);
        round_trip->close(
            scxml_data_resource_adapter_bridge_user(&adapter_bridge),
            &resource);
        check_equal(probe.data_close_calls, (size_t)2u);
    }

    it("projects compile-time text resources through the same CMeta model") {
        provider_probe probe = {0};
        const scxml_text_resource_adapter_v1 adapter = {
            .abi_version = SCXML_TEXT_RESOURCE_ADAPTER_ABI_V1,
            .struct_size = sizeof(scxml_text_resource_adapter_v1),
            .open = probe_text_open,
            .close = probe_text_close};
        scxml_text_resource_provider_bridge provider_bridge = {0};
        scxml_text_resource_adapter_bridge adapter_bridge = {0};
        scxml_text_resource_provider *provider;
        const scxml_text_resource_adapter_v1 *round_trip;
        scxml_text_resource resource = {0};

        check_true(scxml_text_resource_provider_bridge_init(
            &provider_bridge, &adapter, &probe));
        provider = scxml_text_resource_provider_bridge_get(&provider_bridge);
        check_not_null(provider);
        check_equal(scxml_text_resource_provider_open(
                        provider, "mem:s", 5u, 16u, &resource),
                    SCXML_RESOURCE_OK);
        check_equal(probe.text_open_calls, (size_t)1u);
        scxml_text_resource_provider_close(provider, &resource);
        check_equal(probe.text_close_calls, (size_t)1u);

        check_true(scxml_text_resource_adapter_bridge_init(
            &adapter_bridge, provider));
        round_trip = scxml_text_resource_adapter_bridge_get(&adapter_bridge);
        check_equal(round_trip->open(
                        scxml_text_resource_adapter_bridge_user(&adapter_bridge),
                        "mem:s", 5u, 16u, &resource),
                    SCXML_RESOURCE_OK);
        check_equal(probe.text_open_calls, (size_t)2u);
        round_trip->close(
            scxml_text_resource_adapter_bridge_user(&adapter_bridge),
            &resource);
        check_equal(probe.text_close_calls, (size_t)2u);
    }

}
