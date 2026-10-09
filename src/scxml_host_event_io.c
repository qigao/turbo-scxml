#include <scxml/host_event_io.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct scxml_host_binding_impl {
    scxml_host_router *router;
    scxml_host_session_ref source;
    atomic_bool closed;
} scxml_host_binding_impl;

static scxml_adapter_status host_prepare_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    scxml_host_binding_impl *impl = (scxml_host_binding_impl *)user;
    int status;
    const size_t canonical_size =
        sizeof(SCXML_HOST_EVENT_PROCESSOR_URI) - 1u;
    static const char invalid_type[] =
        "Host only accepts the canonical SCXML Event Processor URI";
    static const char invalid_payload[] =
        "Host Event I/O only supports bounded TEXT_UTF8/XML_UTF8 content";
    static const char invalid_delay[] =
        "Host Event I/O does not advertise delayed sends";
    static const char invalid_target[] =
        "Missing or inaccessible SCXML Session target";

    if (out_ticket != NULL)
        *out_ticket = (cflow_statechart_effect_ticket){0};
    if (out_error != NULL) *out_error = NULL;
    if (impl == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL ||
        request->event == NULL || request->event_size == 0u ||
        request->event_size > SCXML_EVENT_METADATA_CAPACITY ||
        (request->target_size != 0u && request->target == NULL) ||
        (request->type_size != 0u && request->type == NULL) ||
        (request->id_size != 0u && request->id == NULL))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    if (atomic_load_explicit(&impl->closed, memory_order_acquire))
        return SCXML_ADAPTER_CLOSED;
    if (request->type_size != 0u &&
        (request->type_size != canonical_size ||
         memcmp(request->type, SCXML_HOST_EVENT_PROCESSOR_URI,
                canonical_size) != 0)) {
        *out_error = invalid_type;
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    if (request->delay_ms != 0u) {
        *out_error = invalid_delay;
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    {
        const scxml_content_view *content = NULL;
        if (request->payload.kind == SCXML_PAYLOAD_CONTENT) {
            content = &request->payload.content;
            if (content->kind != SCXML_CONTENT_TEXT_UTF8 &&
                content->kind != SCXML_CONTENT_XML_UTF8) {
                *out_error = invalid_payload;
                return SCXML_ADAPTER_ERROR_EXECUTION;
            }
        } else if (request->payload.kind != SCXML_PAYLOAD_NONE) {
            *out_error = invalid_payload;
            return SCXML_ADAPTER_ERROR_EXECUTION;
        }

        /* Atomically resolve and copy call-scoped content. No target Session
           admission until the source microstep commits its effect ticket. */
        status = scxml_host_router_prepare_target(
            impl->router, impl->source, request->target, request->target_size,
            request->event, request->event_size,
            request->id, request->id_size, content, out_ticket);
    }
    switch (status) {
        case SALTS_OK:
            return SCXML_ADAPTER_ACCEPTED;
        case SALTS_ENOBUFS:
            return SCXML_ADAPTER_FULL;
        case SALTS_ESHUTDOWN:
            return SCXML_ADAPTER_CLOSED;
        case SALTS_ENOENT:
            *out_error = invalid_target;
            return SCXML_ADAPTER_ERROR_COMMUNICATION;
        case SALTS_EMSGSIZE:
            *out_error = "Host content exceeds configured storage bound";
            return SCXML_ADAPTER_ERROR_EXECUTION;
        case SALTS_ENOTSUP:
            *out_error = invalid_payload;
            return SCXML_ADAPTER_ERROR_EXECUTION;
        case SALTS_EINVAL:
            return SCXML_ADAPTER_INVALID_CONTRACT;
        default:
            *out_error = "Host routing admission failed";
            return SCXML_ADAPTER_ERROR_EXECUTION;
    }
}

static void host_close(void *user) {
    scxml_host_binding_impl *impl = (scxml_host_binding_impl *)user;
    size_t cancelled = 0u;
    if (impl != NULL && !atomic_exchange_explicit(
            &impl->closed, true, memory_order_acq_rel)) {
        /* Source-local READY rows may be cancelled, but accepted tickets and
           in-flight Session delivery retain their own authority. */
        (void)scxml_host_router_cancel_source(
            impl->router, impl->source, &cancelled);
    }
}

static bool host_quiescent(void *user) {
    scxml_host_binding_impl *impl = (scxml_host_binding_impl *)user;
    return impl != NULL &&
        atomic_load_explicit(&impl->closed, memory_order_acquire) &&
        scxml_host_router_source_is_quiescent(
            impl->router, impl->source);
}

static const scxml_event_io_adapter HOST_ADAPTER = {
    .abi_version = SCXML_ADAPTER_ABI,
    .struct_size = sizeof(scxml_event_io_adapter),
    .capabilities = SCXML_EVENT_IO_CAP_SEND | SCXML_EVENT_IO_CAP_CONTENT,
    .prepare_send = host_prepare_send,
    .prepare_cancel = NULL,
    .close = host_close,
    .is_quiescent = host_quiescent
};

int scxml_host_event_io_binding_init(
    scxml_host_event_io_binding *binding, scxml_host_router *router,
    scxml_host_session_ref source) {
    scxml_host_binding_impl *impl;
    scxml_host_session_ref resolved = {0};
    int status;
    if (binding == NULL || binding->impl != NULL || router == NULL)
        return SALTS_EINVAL;
    status = scxml_host_router_resolve(router, source, NULL, 0u, &resolved);
    if (status != SALTS_OK) return status;
    impl = (scxml_host_binding_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return SALTS_ENOMEM;
    impl->router = router;
    impl->source = resolved;
    atomic_init(&impl->closed, false);
    binding->impl = impl;
    return SALTS_OK;
}

const scxml_event_io_adapter *scxml_host_event_io_binding_adapter(void) {
    return &HOST_ADAPTER;
}

void *scxml_host_event_io_binding_user(
    scxml_host_event_io_binding *binding) {
    return binding != NULL ? binding->impl : NULL;
}

int scxml_host_event_io_binding_destroy(
    scxml_host_event_io_binding *binding) {
    scxml_host_binding_impl *impl;
    if (binding == NULL) return SALTS_EINVAL;
    impl = (scxml_host_binding_impl *)binding->impl;
    if (impl == NULL) return SALTS_OK;
    if (!host_quiescent(impl)) return SALTS_EBUSY;
    free(impl);
    binding->impl = NULL;
    return SALTS_OK;
}
