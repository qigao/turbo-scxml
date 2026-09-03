#include <scxml/scxml.h>
#include <scxml/chttp_event_io.h>

#include <stdbool.h>
#include <string.h>

typedef struct allowed_target {
    const char *logical_uri;
    size_t logical_uri_size;
    const char *connection_uri;
    const char *authority;
    const char *target;
} allowed_target;

static int resolve_allowed(
    void *user, const char *uri, size_t uri_size,
    scxml_chttp_resolved_target *out_target) {
    const allowed_target *allowed = (const allowed_target *)user;
    if (allowed == NULL || uri == NULL || out_target == NULL ||
        uri_size != allowed->logical_uri_size ||
        memcmp(uri, allowed->logical_uri, uri_size) != 0)
        return SALTS_EPERM;
    *out_target = (scxml_chttp_resolved_target){
        allowed->connection_uri, allowed->authority, allowed->target};
    return SALTS_OK;
}

static scxml_chttp_decode_status decode_ingress(
    void *user, const scxml_chttp_ingress_view *ingress,
    scxml_content_view *out_data) {
    (void)user;
    if (ingress == NULL || out_data == NULL)
        return SCXML_CHTTP_DECODE_FAILED;
    *out_data = (scxml_content_view){0};
    return SCXML_CHTTP_DECODE_OK;
}

static native_io_backend_kind native_backend(void) {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
    return NATIVE_IO_BACKEND_EPOLL;
#else
    return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

static cnet_client_config network_config(size_t connection_capacity) {
    return (cnet_client_config){
        .backend = native_backend(),
        .connection_capacity = connection_capacity,
        .command_capacity = 16u,
        .request_capacity = 8u,
        .completion_batch_capacity = 8u,
        .event_capacity = 16u,
        .max_send_bytes = 16384u,
        .receive_buffer_bytes = 4096u,
        .connect_timeout_ms = 1000u,
        .read_timeout_ms = 1000u,
        .write_timeout_ms = 1000u};
}

int main(void) {
    scxml_chttp_processor processor = {0};
    scxml_chttp_binding binding = {0};
    const allowed_target allowed = {
        "http://peer/events", sizeof("http://peer/events") - 1u,
        "tcp://127.0.0.1:8080", "127.0.0.1:8080", "/events"};
    const chttp_server_config server = {
        .host = "127.0.0.1", .port = 0u, .backlog = 16u,
        .network = network_config(16u),
        .route_capacity = 32u, .middleware_capacity = 1u,
        .max_route_middleware_count = 1u,
        .max_route_param_count = 1u, .max_route_param_bytes = 64u,
        .max_target_bytes = 512u,
        .max_header_count = 32u, .max_header_bytes = 4096u,
        .max_request_body_bytes = 16384u,
        .max_response_header_count = 8u,
        .max_response_header_bytes = 1024u,
        .max_response_body_bytes = 256u,
        .session_capacity = 1u, .session_entry_capacity = 1u,
        .max_session_key_bytes = 32u, .max_session_value_bytes = 32u,
        .session_idle_timeout_ms = 1000u,
        .session_cookie_name = "scxml_sid", .poll_slice_ms = 1u};
    const chttp_client_config client = {
        .network = network_config(16u),
        .request_capacity = 16u,
        .max_start_line_bytes = 512u,
        .max_header_count = 32u, .max_header_bytes = 4096u,
        .max_request_body_bytes = 16384u,
        .max_response_body_bytes = 256u,
        .max_informational_responses = 2u};
    const scxml_chttp_processor_config_v1 processor_config = {
        .abi_version = SCXML_CHTTP_ABI_V1,
        .struct_size = sizeof(processor_config),
        .server = server, .client = client,
        .advertised_authority = "127.0.0.1",
        .base_path = "/scxml",
        .endpoint_capacity = 16u, .egress_capacity = 64u,
        .max_access_uri_bytes = 256u,
        .max_event_name_bytes = 128u,
        .max_form_entry_count = 32u,
        .max_form_name_bytes = 128u,
        .max_form_value_bytes = 1024u,
        .max_encoded_body_bytes = 16384u,
        .request_timeout_ms = 2000u, .worker_poll_ms = 5u,
        .resolve = resolve_allowed, .resolve_user = (void *)&allowed};
    const scxml_chttp_binding_config_v1 binding_config = {
        .abi_version = SCXML_CHTTP_ABI_V1,
        .struct_size = sizeof(binding_config),
        .decode = decode_ingress};
    scxml_chttp_resolved_target denied = {0};
    int (*volatile processor_init)(
        scxml_chttp_processor *,
        const scxml_chttp_processor_config_v1 *) =
        &scxml_chttp_processor_init;
    int (*volatile processor_start)(scxml_chttp_processor *) =
        &scxml_chttp_processor_start;
    int (*volatile processor_stop)(scxml_chttp_processor *, uint32_t) =
        &scxml_chttp_processor_stop;
    int (*volatile processor_destroy)(scxml_chttp_processor *) =
        &scxml_chttp_processor_destroy;
    int (*volatile binding_init)(
        scxml_chttp_binding *, scxml_chttp_processor *,
        const scxml_chttp_binding_config_v1 *) =
        &scxml_chttp_binding_init;
    const scxml_event_io_adapter *(*volatile binding_adapter)(
        const scxml_chttp_binding *) =
        &scxml_chttp_binding_event_io_adapter;
    void *(*volatile binding_user)(scxml_chttp_binding *) =
        &scxml_chttp_binding_adapter_user;
    bool (*volatile binding_ioprocessor)(
        const scxml_chttp_binding *, scxml_ioprocessor_descriptor *) =
        &scxml_chttp_binding_ioprocessor;
    int (*volatile binding_activate)(
        scxml_chttp_binding *, scxml_session *, const scxml_program *) =
        &scxml_chttp_binding_activate;
    int (*volatile binding_destroy)(scxml_chttp_binding *) =
        &scxml_chttp_binding_destroy;
    bool (*volatile processor_stats)(
        const scxml_chttp_processor *, scxml_chttp_processor_stats *) =
        &scxml_chttp_processor_get_stats;

    /* Runtime code uses init/start/bind/session-init/activate, then destroys
       the session and binding before stop/destroy of the shared processor. */
    return processor.impl == NULL && binding.impl == NULL &&
                   processor_config.server.network.connect_timeout_ms > 0u &&
                   processor_config.server.network.read_timeout_ms > 0u &&
                   processor_config.server.network.write_timeout_ms > 0u &&
                   processor_config.endpoint_capacity > 0u &&
                   processor_config.egress_capacity > 0u &&
                   binding_config.decode == decode_ingress &&
                   resolve_allowed(
                       (void *)&allowed, "https://denied", 14u,
                       &denied) == SALTS_EPERM &&
                   processor_init != NULL && processor_start != NULL &&
                   processor_stop != NULL && processor_destroy != NULL &&
                   binding_init != NULL && binding_adapter != NULL &&
                   binding_user != NULL && binding_ioprocessor != NULL &&
                   binding_activate != NULL && binding_destroy != NULL &&
                   processor_stats != NULL
               ? 0
               : 1;
}
