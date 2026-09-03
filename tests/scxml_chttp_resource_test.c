#include <scxml/chttp_resource.h>

#include "scxml_chttp_resource_internal.h"

#include <tinytest.h>
#include <salts/error_codes.h>
#include <salts/clock.h>
#include <salts/thread.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct resource_probe {
    scxml_resource_status resolve_status;
    scxml_chttp_resolution_v1 resolution;
    const scxml_chttp_data_decoder_v1 *decoder;
    void *decoder_user;
    size_t resolve_calls;
    size_t get_calls;
    size_t response_destroy_calls;
    size_t decoder_open_calls;
    size_t decoder_close_calls;
    int transport_status;
    unsigned int response_status;
    const char *response_media_type;
    chttp_header response_headers[2];
    size_t response_header_count;
    const void *response_body;
    size_t response_body_size;
    char connection_uri[64];
    char authority[64];
    char target[64];
    uint32_t timeout_ms;
    bool replace_decoder_after_resolve;
    scxml_resource_status decoder_open_status;
    bool publish_invalid_reader;
} resource_probe;

static cserde_status int_reader_next(void *context, cserde_token *out) {
    bool *done = (bool *)context;
    if (done == NULL || out == NULL) return CSERDE_INVALID_ARGUMENT;
    if (*done) return CSERDE_DONE;
    *done = true;
    *out = (cserde_token){.kind = CSERDE_SINT, .value.sint = 7};
    return CSERDE_OK;
}

static const cserde_reader_ops int_reader_ops = {
    .struct_size = sizeof(cserde_reader_ops),
    .abi_version = CSERDE_READER_OPS_ABI_VERSION,
    .next = int_reader_next};

static scxml_resource_status probe_decoder_open(
    void *user, const void *body, size_t body_size,
    const cmeta_data_desc *expected, scxml_data_resource *out) {
    resource_probe *probe = (resource_probe *)user;
    bool *done;
    if (probe == NULL || expected == NULL || out == NULL ||
        body != probe->response_body || body_size != probe->response_body_size)
        return SCXML_RESOURCE_FAILED;
    ++probe->decoder_open_calls;
    if (probe->decoder_open_status != SCXML_RESOURCE_OK)
        return probe->decoder_open_status;
    if (probe->publish_invalid_reader) return SCXML_RESOURCE_OK;
    done = (bool *)calloc(1u, sizeof(*done));
    if (done == NULL) return SCXML_RESOURCE_FAILED;
    if (cserde_reader_init(&out->reader, &int_reader_ops, done) != CSERDE_OK) {
        free(done);
        return SCXML_RESOURCE_FAILED;
    }
    out->lease = done;
    return SCXML_RESOURCE_OK;
}

static void probe_decoder_close(
    void *user, scxml_data_resource *resource) {
    resource_probe *probe = (resource_probe *)user;
    if (probe != NULL) ++probe->decoder_close_calls;
    if (resource != NULL) {
        free(resource->lease);
        memset(resource, 0, sizeof(*resource));
    }
}

static scxml_resource_status replaced_decoder_open(
    void *user, const void *body, size_t body_size,
    const cmeta_data_desc *expected, scxml_data_resource *out) {
    (void)user;
    (void)body;
    (void)body_size;
    (void)expected;
    (void)out;
    return SCXML_RESOURCE_FAILED;
}

static const scxml_chttp_data_decoder_v1 probe_decoder = {
    .abi_version = SCXML_CHTTP_DATA_DECODER_ABI_V1,
    .struct_size = sizeof(scxml_chttp_data_decoder_v1),
    .open = probe_decoder_open,
    .close = probe_decoder_close};

static scxml_resource_status probe_resolve(
    void *user, const char *uri, size_t uri_size,
    scxml_chttp_resource_kind kind,
    scxml_chttp_resolution_v1 *out_resolution,
    const scxml_chttp_data_decoder_v1 **out_decoder,
    void **out_decoder_user) {
    resource_probe *probe = (resource_probe *)user;
    (void)uri;
    (void)uri_size;
    if (probe == NULL || out_resolution == NULL || out_decoder == NULL ||
        out_decoder_user == NULL ||
        (kind != SCXML_CHTTP_RESOURCE_DATA &&
         kind != SCXML_CHTTP_RESOURCE_TEXT))
        return SCXML_RESOURCE_FAILED;
    ++probe->resolve_calls;
    if (probe->resolve_status != SCXML_RESOURCE_OK)
        return probe->resolve_status;
    *out_resolution = probe->resolution;
    *out_decoder = probe->decoder;
    *out_decoder_user = probe->decoder_user;
    return SCXML_RESOURCE_OK;
}

static int probe_get(
    void *user, chttp_client *client, const chttp_options *options,
    chttp_response *out_response, chttp_error *out_error) {
    resource_probe *probe = (resource_probe *)user;
    (void)client;
    if (probe == NULL || options == NULL || out_response == NULL ||
        out_error == NULL)
        return SALTS_EINVAL;
    ++probe->get_calls;
    (void)strncpy(probe->connection_uri, options->connection_uri,
                  sizeof(probe->connection_uri) - 1u);
    (void)strncpy(probe->authority, options->authority,
                  sizeof(probe->authority) - 1u);
    (void)strncpy(probe->target, options->target,
                  sizeof(probe->target) - 1u);
    probe->timeout_ms = options->timeout_ms;
    if (probe->replace_decoder_after_resolve && probe->decoder != NULL) {
        scxml_chttp_data_decoder_v1 *decoder =
            (scxml_chttp_data_decoder_v1 *)probe->decoder;
        decoder->open = replaced_decoder_open;
    }
    if (probe->transport_status != SALTS_OK) {
        out_error->status = probe->transport_status;
        out_error->stage = "test";
        return probe->transport_status;
    }
    if (probe->response_header_count != 0u)
        probe->response_headers[0].value = probe->response_media_type;
    *out_response = (chttp_response){
        .status_code = probe->response_status,
        .headers = probe->response_headers,
        .header_count = probe->response_header_count,
        .body = (void *)probe->response_body,
        .body_size = probe->response_body_size};
    return SALTS_OK;
}

static void probe_response_destroy(
    void *user, chttp_response *response) {
    resource_probe *probe = (resource_probe *)user;
    if (probe != NULL) ++probe->response_destroy_calls;
    if (response != NULL) memset(response, 0, sizeof(*response));
}

static const scxml_chttp_transport_v1 probe_transport = {
    .get = probe_get,
    .response_destroy = probe_response_destroy};

static resource_probe default_probe(void) {
    static const char body[] = "7";
    resource_probe probe = {
        .resolve_status = SCXML_RESOURCE_OK,
        .decoder = &probe_decoder,
        .transport_status = SALTS_OK,
        .response_status = 200u,
        .response_media_type = "application/json",
        .response_body = body,
        .response_body_size = sizeof(body) - 1u};
    probe.resolution = (scxml_chttp_resolution_v1){
        .abi_version = SCXML_CHTTP_RESOLUTION_ABI_V1,
        .struct_size = sizeof(scxml_chttp_resolution_v1),
        .connection_uri = "tcp://127.0.0.1:8080",
        .connection_uri_size = sizeof("tcp://127.0.0.1:8080") - 1u,
        .authority = "workflow.internal",
        .authority_size = sizeof("workflow.internal") - 1u,
        .target = "/workflow/script.js",
        .target_size = sizeof("/workflow/script.js") - 1u,
        .media_type = "application/json",
        .media_type_size = sizeof("application/json") - 1u};
    probe.response_headers[0] = (chttp_header){
        .name = "Content-Type", .value = "application/json"};
    probe.response_header_count = 1u;
    return probe;
}

static scxml_chttp_resource_config_v1 default_config(resource_probe *probe) {
    static chttp_client borrowed_client;
    borrowed_client.impl = &borrowed_client;
    return (scxml_chttp_resource_config_v1){
        .abi_version = SCXML_CHTTP_RESOURCE_CONFIG_ABI_V1,
        .struct_size = sizeof(scxml_chttp_resource_config_v1),
        .client = &borrowed_client,
        .resolve = probe_resolve,
        .resolver_user = probe,
        .timeout_ms = 250u,
        .max_connection_uri_bytes = 64u,
        .max_authority_bytes = 64u,
        .max_target_bytes = 128u,
        .max_media_type_bytes = 64u,
        .max_response_body_bytes = 32u};
}

static void init_with_probe(
    scxml_chttp_resource *resource, resource_probe *probe) {
    const scxml_chttp_resource_config_v1 config = default_config(probe);
    check_equal(scxml_chttp_resource_init(resource, &config), SCXML_OK);
    check_equal(scxml_chttp_resource_set_transport_for_test(
                    resource, &probe_transport, probe),
                SCXML_OK);
}

static native_io_backend_kind loopback_backend(void) {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
    return NATIVE_IO_BACKEND_EPOLL;
#else
    return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

static cnet_client_config loopback_network(size_t connection_capacity) {
    return (cnet_client_config){
        .backend = loopback_backend(),
        .connection_capacity = connection_capacity,
        .command_capacity = 16u,
        .request_capacity = 8u,
        .completion_batch_capacity = 8u,
        .event_capacity = 16u,
        .max_send_bytes = 4096u,
        .receive_buffer_bytes = 256u,
        .connect_timeout_ms = 1000u,
        .read_timeout_ms = 1000u,
        .write_timeout_ms = 1000u};
}

static chttp_server_config loopback_server_config(void) {
    return (chttp_server_config){
        .host = "127.0.0.1",
        .port = 0u,
        .backlog = 4u,
        .network = loopback_network(4u),
        .route_capacity = 4u,
        .middleware_capacity = 1u,
        .max_route_middleware_count = 1u,
        .max_route_param_count = 1u,
        .max_route_param_bytes = 32u,
        .max_target_bytes = 128u,
        .max_header_count = 8u,
        .max_header_bytes = 512u,
        .max_request_body_bytes = 64u,
        .max_response_header_count = 8u,
        .max_response_header_bytes = 512u,
        .max_response_body_bytes = 128u,
        .session_capacity = 1u,
        .session_entry_capacity = 1u,
        .max_session_key_bytes = 16u,
        .max_session_value_bytes = 16u,
        .session_idle_timeout_ms = 1000u,
        .session_cookie_name = "scxml_sid",
        .poll_slice_ms = 2u};
}

static chttp_client_config loopback_client_config(void) {
    return (chttp_client_config){
        .network = loopback_network(2u),
        .request_capacity = 1u,
        .max_start_line_bytes = 128u,
        .max_header_count = 8u,
        .max_header_bytes = 512u,
        .max_request_body_bytes = 64u,
        .max_response_body_bytes = 8u,
        .max_informational_responses = 1u};
}

static int loopback_ok(
    void *user, const chttp_server_request_view *request,
    chttp_server_response *response) {
    (void)user;
    (void)request;
    return chttp_server_reply(
        response, 200u, "application/json", "7", 1u);
}

static int loopback_large(
    void *user, const chttp_server_request_view *request,
    chttp_server_response *response) {
    static const char body[] = "0123456789abcdef";
    (void)user;
    (void)request;
    return chttp_server_reply(
        response, 200u, "application/json", body, sizeof(body) - 1u);
}

static int loopback_slow(
    void *user, const chttp_server_request_view *request,
    chttp_server_response *response) {
    const uint64_t deadline = salts_monotonic_ms() + 50u;
    (void)user;
    (void)request;
    while (salts_monotonic_ms() < deadline) salts_thread_yield();
    return chttp_server_reply(
        response, 200u, "application/json", "7", 1u);
}

spec("TurboSCXML optional CHTTP resource adapter") {
    it("rejects an uninitialized borrowed CHTTP client at init") {
        resource_probe probe = default_probe();
        chttp_client client = {0};
        scxml_chttp_resource_config_v1 config = default_config(&probe);
        scxml_chttp_resource resource = {0};
        config.client = &client;

        check_equal(scxml_chttp_resource_init(&resource, &config),
                    SCXML_INVALID_ARGUMENT);
        check_null(resource.impl);
    }

    it("authorizes and maps a bounded request before network admission") {
        resource_probe probe = default_probe();
        scxml_chttp_resource resource = {0};
        scxml_text_resource text = {0};
        const scxml_text_resource_adapter_v1 *adapter;
        init_with_probe(&resource, &probe);
        adapter = scxml_chttp_resource_text_adapter(&resource);
        check_not_null(adapter);

        probe.resolve_status = SCXML_RESOURCE_DENIED;
        check_equal(adapter->open(
                        &resource, "tenant:denied", sizeof("tenant:denied") - 1u,
                        32u, &text),
                    SCXML_RESOURCE_DENIED);
        check_equal(probe.get_calls, 0u);

        probe.resolve_status = SCXML_RESOURCE_OK;
        probe.response_media_type = "application/json";
        check_equal(adapter->open(
                        &resource, "tenant:script", sizeof("tenant:script") - 1u,
                        32u, &text),
                    SCXML_RESOURCE_OK);
        check_equal(probe.get_calls, 1u);
        check_equal(probe.connection_uri, "tcp://127.0.0.1:8080");
        check_equal(probe.authority, "workflow.internal");
        check_equal(probe.target, "/workflow/script.js");
        check_equal(probe.timeout_ms, 250u);
        check_equal(text.data, probe.response_body);
        check_equal(text.size, probe.response_body_size);
        adapter->close(&resource, &text);
        check_equal(probe.response_destroy_calls, 1u);
        check_equal(scxml_chttp_resource_destroy(&resource), SCXML_OK);
    }

    it("rejects redirects transport failures media mismatch and excess data") {
        resource_probe probe = default_probe();
        scxml_chttp_resource resource = {0};
        scxml_text_resource text = {0};
        const scxml_text_resource_adapter_v1 *adapter;
        init_with_probe(&resource, &probe);
        adapter = scxml_chttp_resource_text_adapter(&resource);

        probe.response_status = 302u;
        check_equal(adapter->open(
                        &resource, "tenant:redirect", sizeof("tenant:redirect") - 1u,
                        32u, &text),
                    SCXML_RESOURCE_DENIED);
        check_equal(probe.response_destroy_calls, 1u);

        probe.response_status = 404u;
        check_equal(adapter->open(
                        &resource, "tenant:missing", sizeof("tenant:missing") - 1u,
                        32u, &text),
                    SCXML_RESOURCE_NOT_FOUND);
        check_equal(probe.response_destroy_calls, 2u);

        probe.response_status = 500u;
        check_equal(adapter->open(
                        &resource, "tenant:failed", sizeof("tenant:failed") - 1u,
                        32u, &text),
                    SCXML_RESOURCE_FAILED);
        check_equal(probe.response_destroy_calls, 3u);

        probe.response_status = 200u;
        probe.transport_status = SALTS_ETIMEDOUT;
        check_equal(adapter->open(
                        &resource, "tenant:slow", sizeof("tenant:slow") - 1u,
                        32u, &text),
                    SCXML_RESOURCE_TIMEOUT);

        probe.transport_status = SALTS_EMSGSIZE;
        check_equal(adapter->open(
                        &resource, "tenant:large", sizeof("tenant:large") - 1u,
                        32u, &text),
                    SCXML_RESOURCE_LIMIT_EXCEEDED);

        probe.transport_status = SALTS_OK;
        probe.response_media_type = "text/plain";
        check_equal(adapter->open(
                        &resource, "tenant:type", sizeof("tenant:type") - 1u,
                        32u, &text),
                    SCXML_RESOURCE_INVALID_DATA);
        check_equal(probe.response_destroy_calls, 4u);

        probe.response_media_type = "application/json";
        probe.response_body_size = 33u;
        check_equal(adapter->open(
                        &resource, "tenant:large", sizeof("tenant:large") - 1u,
                        32u, &text),
                    SCXML_RESOURCE_LIMIT_EXCEEDED);
        check_equal(probe.response_destroy_calls, 5u);

        {
            static const unsigned char invalid_utf8[] = {0xc0u, 0x80u};
            probe.response_body = invalid_utf8;
            probe.response_body_size = sizeof(invalid_utf8);
            check_equal(adapter->open(
                            &resource, "tenant:utf8", sizeof("tenant:utf8") - 1u,
                            32u, &text),
                        SCXML_RESOURCE_INVALID_DATA);
            check_equal(probe.response_destroy_calls, 6u);
        }

        check_equal(adapter->open(
                        &resource, "https://workflow.invalid/script",
                        sizeof("https://workflow.invalid/script") - 1u,
                        32u, &text),
                    SCXML_RESOURCE_DENIED);
        check_equal(probe.resolve_calls, 8u);
        check_equal(probe.get_calls, 8u);
        check_equal(scxml_chttp_resource_destroy(&resource), SCXML_OK);
    }

    it("rejects malformed resolver output before calling CHTTP") {
        resource_probe probe = default_probe();
        scxml_chttp_resource resource = {0};
        scxml_text_resource text = {0};
        const scxml_text_resource_adapter_v1 *adapter;
        init_with_probe(&resource, &probe);
        adapter = scxml_chttp_resource_text_adapter(&resource);

        probe.resolution.target = "relative";
        probe.resolution.target_size = sizeof("relative") - 1u;
        check_equal(adapter->open(
                        &resource, "tenant:bad", sizeof("tenant:bad") - 1u,
                        32u, &text),
                    SCXML_RESOURCE_DENIED);
        check_equal(probe.get_calls, 0u);
        check_equal(scxml_chttp_resource_destroy(&resource), SCXML_OK);
    }

    it("enforces every endpoint bound before transport admission") {
        resource_probe probe = default_probe();
        scxml_chttp_resource resource = {0};
        scxml_text_resource text = {0};
        const scxml_text_resource_adapter_v1 *adapter;
        const scxml_chttp_resolution_v1 valid = probe.resolution;
        init_with_probe(&resource, &probe);
        adapter = scxml_chttp_resource_text_adapter(&resource);

        probe.resolution.connection_uri_size = 65u;
        check_equal(adapter->open(
                        &resource, "tenant:bad", sizeof("tenant:bad") - 1u,
                        32u, &text),
                    SCXML_RESOURCE_DENIED);
        probe.resolution = valid;
        probe.resolution.authority_size = 65u;
        check_equal(adapter->open(
                        &resource, "tenant:bad", sizeof("tenant:bad") - 1u,
                        32u, &text),
                    SCXML_RESOURCE_DENIED);
        probe.resolution = valid;
        probe.resolution.target_size = 129u;
        check_equal(adapter->open(
                        &resource, "tenant:bad", sizeof("tenant:bad") - 1u,
                        32u, &text),
                    SCXML_RESOURCE_DENIED);
        probe.resolution = valid;
        probe.resolution.media_type_size = 65u;
        check_equal(adapter->open(
                        &resource, "tenant:bad", sizeof("tenant:bad") - 1u,
                        32u, &text),
                    SCXML_RESOURCE_DENIED);
        check_equal(probe.get_calls, 0u);
        check_equal(scxml_chttp_resource_destroy(&resource), SCXML_OK);
    }

    it("rejects duplicate Content-Type response headers") {
        resource_probe probe = default_probe();
        scxml_chttp_resource resource = {0};
        scxml_text_resource text = {0};
        const scxml_text_resource_adapter_v1 *adapter;
        probe.response_headers[1] = (chttp_header){
            .name = "content-type", .value = "text/plain"};
        probe.response_header_count = 2u;
        init_with_probe(&resource, &probe);
        adapter = scxml_chttp_resource_text_adapter(&resource);

        check_equal(adapter->open(
                        &resource, "tenant:ambiguous",
                        sizeof("tenant:ambiguous") - 1u, 32u, &text),
                    SCXML_RESOURCE_INVALID_DATA);
        check_equal(probe.response_destroy_calls, 1u);

        probe.response_headers[1].value = "application/json";
        check_equal(adapter->open(
                        &resource, "tenant:duplicate",
                        sizeof("tenant:duplicate") - 1u, 32u, &text),
                    SCXML_RESOURCE_INVALID_DATA);
        check_equal(probe.response_destroy_calls, 2u);
        check_equal(scxml_chttp_resource_destroy(&resource), SCXML_OK);
    }

    it("retains the response and decoder until one data-resource close") {
        resource_probe probe = default_probe();
        scxml_chttp_resource resource = {0};
        scxml_data_resource data = {0};
        scxml_data_resource second = {0};
        cserde_token token = {0};
        const scxml_data_resource_adapter_v1 *adapter;
        probe.decoder_user = &probe;
        init_with_probe(&resource, &probe);
        adapter = scxml_chttp_resource_data_adapter(&resource);
        check_not_null(adapter);

        check_equal(adapter->open(
                        &resource, "tenant:data", sizeof("tenant:data") - 1u,
                        &cmeta_data_int, &data),
                    SCXML_RESOURCE_OK);
        check_equal(probe.decoder_open_calls, 1u);
        check_equal(probe.response_destroy_calls, 0u);
        check_equal(adapter->open(
                        &resource, "tenant:second",
                        sizeof("tenant:second") - 1u,
                        &cmeta_data_int, &second),
                    SCXML_RESOURCE_FAILED);
        check_equal(probe.get_calls, 1u);
        check_equal(cserde_reader_next(&data.reader, &token), CSERDE_OK);
        check_equal(token.kind, CSERDE_SINT);
        check_equal(token.value.sint, 7);
        check_equal(scxml_chttp_resource_destroy(&resource),
                    SCXML_INVALID_ARGUMENT);

        adapter->close(&resource, &data);
        check_equal(probe.decoder_close_calls, 1u);
        check_equal(probe.response_destroy_calls, 1u);
        adapter->close(&resource, &data);
        check_equal(probe.decoder_close_calls, 1u);
        check_equal(probe.response_destroy_calls, 1u);
        check_equal(scxml_chttp_resource_destroy(&resource), SCXML_OK);
    }

    it("cleans the response when a decoder fails or publishes no reader") {
        resource_probe probe = default_probe();
        scxml_chttp_resource resource = {0};
        scxml_data_resource data = {0};
        const scxml_data_resource_adapter_v1 *adapter;
        probe.decoder_user = &probe;
        init_with_probe(&resource, &probe);
        adapter = scxml_chttp_resource_data_adapter(&resource);

        probe.decoder_open_status = SCXML_RESOURCE_INVALID_DATA;
        check_equal(adapter->open(
                        &resource, "tenant:invalid",
                        sizeof("tenant:invalid") - 1u,
                        &cmeta_data_int, &data),
                    SCXML_RESOURCE_INVALID_DATA);
        check_equal(probe.decoder_close_calls, 0u);
        check_equal(probe.response_destroy_calls, 1u);

        probe.decoder_open_status = SCXML_RESOURCE_OK;
        probe.publish_invalid_reader = true;
        check_equal(adapter->open(
                        &resource, "tenant:invalid",
                        sizeof("tenant:invalid") - 1u,
                        &cmeta_data_int, &data),
                    SCXML_RESOURCE_INVALID_DATA);
        check_equal(probe.decoder_close_calls, 1u);
        check_equal(probe.response_destroy_calls, 2u);
        check_equal(scxml_chttp_resource_destroy(&resource), SCXML_OK);
    }

    it("copies decoder operations before blocking transport progress") {
        resource_probe probe = default_probe();
        scxml_chttp_data_decoder_v1 transient_decoder = probe_decoder;
        scxml_chttp_resource resource = {0};
        scxml_data_resource data = {0};
        const scxml_data_resource_adapter_v1 *adapter;
        probe.decoder = &transient_decoder;
        probe.decoder_user = &probe;
        probe.replace_decoder_after_resolve = true;
        init_with_probe(&resource, &probe);
        adapter = scxml_chttp_resource_data_adapter(&resource);

        check_equal(adapter->open(
                        &resource, "tenant:data", sizeof("tenant:data") - 1u,
                        &cmeta_data_int, &data),
                    SCXML_RESOURCE_OK);
        check_equal(probe.decoder_open_calls, 1u);
        adapter->close(&resource, &data);
        check_equal(probe.decoder_close_calls, 1u);
        check_equal(scxml_chttp_resource_destroy(&resource), SCXML_OK);
    }

    it("uses a real loopback CHTTP client and enforces its response bound") {
        chttp_server server = {0};
        chttp_client client = {0};
        chttp_server_config server_config = loopback_server_config();
        chttp_client_config client_config = loopback_client_config();
        resource_probe probe = default_probe();
        scxml_chttp_resource_config_v1 config;
        scxml_chttp_resource resource = {0};
        scxml_text_resource text = {0};
        const scxml_text_resource_adapter_v1 *adapter;
        char connection_uri[64];
        uint16_t port = 0u;
        int uri_size;

        check_equal(chttp_server_init(&server, &server_config), SALTS_OK);
        check_equal(chttp_server_get(
                        &server, "/ok", loopback_ok, NULL),
                    SALTS_OK);
        check_equal(chttp_server_get(
                        &server, "/large", loopback_large, NULL),
                    SALTS_OK);
        check_equal(chttp_server_get(
                        &server, "/slow", loopback_slow, NULL),
                    SALTS_OK);
        check_equal(chttp_server_start(&server), SALTS_OK);
        check_equal(chttp_server_port(&server, &port), SALTS_OK);
        uri_size = snprintf(
            connection_uri, sizeof(connection_uri),
            "tcp://127.0.0.1:%u", (unsigned int)port);
        check_true(uri_size > 0 && (size_t)uri_size < sizeof(connection_uri));
        check_equal(chttp_client_init(&client, &client_config), SALTS_OK);

        probe.resolution.connection_uri = connection_uri;
        probe.resolution.connection_uri_size = (size_t)uri_size;
        probe.resolution.authority = "127.0.0.1";
        probe.resolution.authority_size = sizeof("127.0.0.1") - 1u;
        probe.resolution.target = "/ok";
        probe.resolution.target_size = sizeof("/ok") - 1u;
        config = default_config(&probe);
        config.client = &client;
        config.max_response_body_bytes = 8u;
        check_equal(scxml_chttp_resource_init(&resource, &config), SCXML_OK);
        adapter = scxml_chttp_resource_text_adapter(&resource);

        check_equal(adapter->open(
                        &resource, "tenant:ok", sizeof("tenant:ok") - 1u,
                        8u, &text),
                    SCXML_RESOURCE_OK);
        check_equal(text.size, 1u);
        check_equal(text.data[0], '7');
        adapter->close(&resource, &text);

        probe.resolution.target = "/large";
        probe.resolution.target_size = sizeof("/large") - 1u;
        check_equal(adapter->open(
                        &resource, "tenant:large",
                        sizeof("tenant:large") - 1u, 8u, &text),
                    SCXML_RESOURCE_LIMIT_EXCEEDED);

        check_equal(scxml_chttp_resource_destroy(&resource), SCXML_OK);

        probe.resolution.target = "/slow";
        probe.resolution.target_size = sizeof("/slow") - 1u;
        config.timeout_ms = 5u;
        check_equal(scxml_chttp_resource_init(&resource, &config), SCXML_OK);
        adapter = scxml_chttp_resource_text_adapter(&resource);
        check_equal(adapter->open(
                        &resource, "tenant:slow",
                        sizeof("tenant:slow") - 1u, 8u, &text),
                    SCXML_RESOURCE_TIMEOUT);
        check_equal(scxml_chttp_resource_destroy(&resource), SCXML_OK);

        check_equal(chttp_client_destroy(&client, 1000u), SALTS_OK);
        check_equal(chttp_server_stop(&server, 1000u), SALTS_OK);
        check_equal(chttp_server_destroy(&server), SALTS_OK);
    }
}
