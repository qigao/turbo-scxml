#include <scxml/chttp_resource.h>

#include "scxml_chttp_resource_internal.h"

#include <salts/error_codes.h>
#include <salts_vstr.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum scxml_chttp_active_kind {
    SCXML_CHTTP_ACTIVE_NONE = 0,
    SCXML_CHTTP_ACTIVE_DATA,
    SCXML_CHTTP_ACTIVE_TEXT
} scxml_chttp_active_kind;

typedef struct scxml_chttp_resource_impl {
    scxml_chttp_resource_config_v1 config;
    scxml_chttp_transport_v1 transport;
    void *transport_user;
    scxml_chttp_active_kind active;
    chttp_response response;
    scxml_chttp_data_decoder_v1 decoder;
    void *decoder_user;
    scxml_data_resource decoded;
} scxml_chttp_resource_impl;

typedef struct scxml_chttp_request_storage {
    char *allocation;
    char *connection_uri;
    char *authority;
    char *target;
    char *media_type;
} scxml_chttp_request_storage;

static int production_get(
    void *user, chttp_client *client, const chttp_options *options,
    chttp_response *out_response, chttp_error *out_error) {
    (void)user;
    return chttp_get(client, options, out_response, out_error);
}

static void production_response_destroy(
    void *user, chttp_response *response) {
    (void)user;
    chttp_response_destroy(response);
}

static const scxml_chttp_transport_v1 production_transport = {
    .get = production_get,
    .response_destroy = production_response_destroy};

static bool checked_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) return false;
    *out = left + right;
    return true;
}

static bool view_valid(
    const char *data, size_t size, size_t limit) {
    return data != NULL && size != 0u && size <= limit &&
        memchr(data, '\0', size) == NULL;
}

static bool has_https_scheme(const char *uri, size_t uri_size) {
    static const char scheme[] = "https:";
    size_t index;
    if (uri == NULL || uri_size < sizeof(scheme) - 1u) return false;
    for (index = 0u; index < sizeof(scheme) - 1u; ++index) {
        unsigned char value = (unsigned char)uri[index];
        if (value >= (unsigned char)'A' && value <= (unsigned char)'Z')
            value = (unsigned char)(value - (unsigned char)'A' +
                                    (unsigned char)'a');
        if (value != (unsigned char)scheme[index]) return false;
    }
    return true;
}

static bool supported_connection_uri(const char *uri, size_t uri_size) {
    static const char tcp_scheme[] = "tcp://";
    static const char pipe_scheme[] = "pipe://";
    return (uri_size >= sizeof(tcp_scheme) - 1u &&
            memcmp(uri, tcp_scheme, sizeof(tcp_scheme) - 1u) == 0) ||
        (uri_size >= sizeof(pipe_scheme) - 1u &&
         memcmp(uri, pipe_scheme, sizeof(pipe_scheme) - 1u) == 0);
}

static bool visible_ascii_view(
    const char *data, size_t size, bool allow_space) {
    size_t index;
    if (data == NULL) return false;
    for (index = 0u; index < size; ++index) {
        const unsigned char value = (unsigned char)data[index];
        if (value > 0x7eu || value < (allow_space ? 0x20u : 0x21u))
            return false;
    }
    return true;
}

static bool origin_form_target(
    const char *target, size_t target_size) {
    return target != NULL && target_size != 0u && target[0] == '/' &&
        visible_ascii_view(target, target_size, false) &&
        memchr(target, '#', target_size) == NULL;
}

static bool resolution_valid(
    const scxml_chttp_resource_impl *impl,
    const scxml_chttp_resolution_v1 *resolution) {
    return impl != NULL && resolution != NULL &&
        resolution->abi_version == SCXML_CHTTP_RESOLUTION_ABI_V1 &&
        resolution->struct_size >= sizeof(*resolution) &&
        view_valid(resolution->connection_uri,
                   resolution->connection_uri_size,
                   impl->config.max_connection_uri_bytes) &&
        view_valid(resolution->authority, resolution->authority_size,
                   impl->config.max_authority_bytes) &&
        view_valid(resolution->target, resolution->target_size,
                   impl->config.max_target_bytes) &&
        view_valid(resolution->media_type, resolution->media_type_size,
                   impl->config.max_media_type_bytes) &&
        visible_ascii_view(resolution->connection_uri,
                           resolution->connection_uri_size, false) &&
        visible_ascii_view(resolution->authority,
                           resolution->authority_size, false) &&
        visible_ascii_view(resolution->media_type,
                           resolution->media_type_size, true) &&
        supported_connection_uri(
            resolution->connection_uri, resolution->connection_uri_size) &&
        origin_form_target(resolution->target, resolution->target_size);
}

static bool decoder_valid(
    const scxml_chttp_data_decoder_v1 *decoder) {
    return decoder != NULL &&
        decoder->abi_version == SCXML_CHTTP_DATA_DECODER_ABI_V1 &&
        decoder->struct_size >= sizeof(*decoder) &&
        decoder->open != NULL && decoder->close != NULL;
}

static bool transport_valid(
    const scxml_chttp_transport_v1 *transport) {
    return transport != NULL && transport->get != NULL &&
        transport->response_destroy != NULL;
}

static bool ascii_name_equal(const char *left, const char *right) {
    size_t index = 0u;
    if (left == NULL || right == NULL) return false;
    while (left[index] != '\0' && right[index] != '\0') {
        unsigned char left_value = (unsigned char)left[index];
        unsigned char right_value = (unsigned char)right[index];
        if (left_value >= (unsigned char)'A' &&
            left_value <= (unsigned char)'Z')
            left_value = (unsigned char)(left_value - (unsigned char)'A' +
                                         (unsigned char)'a');
        if (right_value >= (unsigned char)'A' &&
            right_value <= (unsigned char)'Z')
            right_value = (unsigned char)(right_value - (unsigned char)'A' +
                                          (unsigned char)'a');
        if (left_value != right_value) return false;
        ++index;
    }
    return left[index] == right[index];
}

static const char *single_content_type(const chttp_response *response) {
    const char *value = NULL;
    size_t index;
    if (response == NULL ||
        (response->header_count != 0u && response->headers == NULL))
        return NULL;
    for (index = 0u; index < response->header_count; ++index) {
        if (!ascii_name_equal(response->headers[index].name, "Content-Type"))
            continue;
        if (value != NULL || response->headers[index].value == NULL)
            return NULL;
        value = response->headers[index].value;
    }
    return value;
}

static bool config_valid(
    const scxml_chttp_resource_config_v1 *config) {
    return config != NULL &&
        config->abi_version == SCXML_CHTTP_RESOURCE_CONFIG_ABI_V1 &&
        config->struct_size >= sizeof(*config) && config->client != NULL &&
        config->client->impl != NULL && config->resolve != NULL &&
        config->timeout_ms != 0u &&
        config->max_connection_uri_bytes != 0u &&
        config->max_authority_bytes != 0u &&
        config->max_target_bytes != 0u &&
        config->max_media_type_bytes != 0u &&
        config->max_response_body_bytes != 0u;
}

static bool allocate_request_storage(
    const scxml_chttp_resolution_v1 *resolution,
    scxml_chttp_request_storage *out) {
    const size_t sizes[] = {
        resolution->connection_uri_size,
        resolution->authority_size,
        resolution->target_size,
        resolution->media_type_size};
    char **fields[] = {
        &out->connection_uri, &out->authority,
        &out->target, &out->media_type};
    const char *sources[] = {
        resolution->connection_uri, resolution->authority,
        resolution->target, resolution->media_type};
    size_t total = 0u;
    size_t index;
    char *cursor;
    if (resolution == NULL || out == NULL) return false;
    memset(out, 0, sizeof(*out));
    for (index = 0u; index < sizeof(sizes) / sizeof(sizes[0]); ++index) {
        size_t terminated;
        if (!checked_add(sizes[index], 1u, &terminated) ||
            !checked_add(total, terminated, &total))
            return false;
    }
    out->allocation = (char *)malloc(total);
    if (out->allocation == NULL) return false;
    cursor = out->allocation;
    for (index = 0u; index < sizeof(sizes) / sizeof(sizes[0]); ++index) {
        *fields[index] = cursor;
        memcpy(cursor, sources[index], sizes[index]);
        cursor[sizes[index]] = '\0';
        cursor += sizes[index] + 1u;
    }
    return true;
}

static scxml_resource_status map_transport_status(int status) {
    if (status == SALTS_ETIMEDOUT) return SCXML_RESOURCE_TIMEOUT;
    if (status == SALTS_EMSGSIZE || status == SALTS_ENOBUFS)
        return SCXML_RESOURCE_LIMIT_EXCEEDED;
    if (status == SALTS_EPERM) return SCXML_RESOURCE_DENIED;
    return SCXML_RESOURCE_FAILED;
}

static scxml_resource_status map_http_status(unsigned int status) {
    if (status >= 200u && status <= 299u) return SCXML_RESOURCE_OK;
    if (status >= 300u && status <= 399u) return SCXML_RESOURCE_DENIED;
    if (status == 404u) return SCXML_RESOURCE_NOT_FOUND;
    return SCXML_RESOURCE_FAILED;
}

static void release_active(scxml_chttp_resource_impl *impl) {
    if (impl == NULL || impl->active == SCXML_CHTTP_ACTIVE_NONE) return;
    if (impl->active == SCXML_CHTTP_ACTIVE_DATA &&
        impl->decoder.close != NULL)
        impl->decoder.close(impl->decoder_user, &impl->decoded);
    impl->transport.response_destroy(
        impl->transport_user, &impl->response);
    impl->active = SCXML_CHTTP_ACTIVE_NONE;
    impl->decoder = (scxml_chttp_data_decoder_v1){0};
    impl->decoder_user = NULL;
    impl->decoded = (scxml_data_resource){0};
}

static scxml_resource_status acquire_response(
    scxml_chttp_resource_impl *impl, const char *uri, size_t uri_size,
    scxml_chttp_resource_kind kind, size_t caller_max_bytes,
    const cmeta_data_desc *expected,
    scxml_text_resource *out_text, scxml_data_resource *out_data) {
    scxml_chttp_resolution_v1 resolution = {0};
    const scxml_chttp_data_decoder_v1 *decoder = NULL;
    scxml_chttp_data_decoder_v1 decoder_copy = {0};
    void *decoder_user = NULL;
    scxml_chttp_request_storage storage = {0};
    chttp_response response = {0};
    chttp_error error = {0};
    chttp_options options;
    scxml_resource_status status;
    const char *content_type;
    size_t body_limit;
    int transport_status;
    if (impl == NULL || uri == NULL || uri_size == 0u ||
        impl->active != SCXML_CHTTP_ACTIVE_NONE ||
        (kind == SCXML_CHTTP_RESOURCE_TEXT &&
         (out_text == NULL || caller_max_bytes == 0u)) ||
        (kind == SCXML_CHTTP_RESOURCE_DATA &&
         (out_data == NULL || expected == NULL)))
        return SCXML_RESOURCE_FAILED;
    if (memchr(uri, '\0', uri_size) != NULL)
        return SCXML_RESOURCE_INVALID_DATA;
    if (has_https_scheme(uri, uri_size)) return SCXML_RESOURCE_DENIED;
    status = impl->config.resolve(
        impl->config.resolver_user, uri, uri_size, kind,
        &resolution, &decoder, &decoder_user);
    if (status != SCXML_RESOURCE_OK) return status;
    if (!resolution_valid(impl, &resolution) ||
        (kind == SCXML_CHTTP_RESOURCE_DATA && !decoder_valid(decoder)))
        return SCXML_RESOURCE_DENIED;
    if (kind == SCXML_CHTTP_RESOURCE_DATA) decoder_copy = *decoder;
    if (!allocate_request_storage(&resolution, &storage))
        return SCXML_RESOURCE_FAILED;
    options = (chttp_options){
        .connection_uri = storage.connection_uri,
        .authority = storage.authority,
        .target = storage.target,
        .timeout_ms = impl->config.timeout_ms};
    transport_status = impl->transport.get(
        impl->transport_user, impl->config.client, &options,
        &response, &error);
    if (transport_status != SALTS_OK) {
        free(storage.allocation);
        return map_transport_status(transport_status);
    }
    status = map_http_status(response.status_code);
    if (status != SCXML_RESOURCE_OK) goto fail_response;
    body_limit = impl->config.max_response_body_bytes;
    if (kind == SCXML_CHTTP_RESOURCE_TEXT && caller_max_bytes < body_limit)
        body_limit = caller_max_bytes;
    if (response.body_size > body_limit) {
        status = SCXML_RESOURCE_LIMIT_EXCEEDED;
        goto fail_response;
    }
    if (response.body_size != 0u && response.body == NULL) {
        status = SCXML_RESOURCE_INVALID_DATA;
        goto fail_response;
    }
    content_type = single_content_type(&response);
    if (content_type == NULL || strcmp(content_type, storage.media_type) != 0) {
        status = SCXML_RESOURCE_INVALID_DATA;
        goto fail_response;
    }
    if (kind == SCXML_CHTTP_RESOURCE_TEXT) {
        if (!vstr_utf8_valid(vstr_from_buf(
                (const char *)response.body, response.body_size))) {
            status = SCXML_RESOURCE_INVALID_DATA;
            goto fail_response;
        }
        impl->response = response;
        impl->active = SCXML_CHTTP_ACTIVE_TEXT;
        *out_text = (scxml_text_resource){
            .data = (const char *)impl->response.body,
            .size = impl->response.body_size,
            .lease = impl};
        free(storage.allocation);
        return SCXML_RESOURCE_OK;
    }

    impl->decoder = decoder_copy;
    impl->decoder_user = decoder_user;
    status = impl->decoder.open(
        impl->decoder_user, response.body, response.body_size,
        expected, &impl->decoded);
    if (status != SCXML_RESOURCE_OK) {
        impl->decoder = (scxml_chttp_data_decoder_v1){0};
        impl->decoder_user = NULL;
        goto fail_response;
    }
    if (impl->decoded.reader.state != CSERDE_READER_READY ||
        impl->decoded.reader.ops == NULL) {
        impl->decoder.close(impl->decoder_user, &impl->decoded);
        impl->decoder = (scxml_chttp_data_decoder_v1){0};
        impl->decoder_user = NULL;
        impl->decoded = (scxml_data_resource){0};
        status = SCXML_RESOURCE_INVALID_DATA;
        goto fail_response;
    }
    impl->response = response;
    impl->active = SCXML_CHTTP_ACTIVE_DATA;
    *out_data = (scxml_data_resource){
        .reader = impl->decoded.reader,
        .lease = impl};
    free(storage.allocation);
    return SCXML_RESOURCE_OK;

fail_response:
    impl->transport.response_destroy(impl->transport_user, &response);
    free(storage.allocation);
    return status;
}

static scxml_resource_status data_open(
    void *user, const char *uri, size_t uri_size,
    const cmeta_data_desc *expected, scxml_data_resource *out) {
    scxml_chttp_resource *resource = (scxml_chttp_resource *)user;
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (resource == NULL || resource->impl == NULL || out == NULL)
        return SCXML_RESOURCE_FAILED;
    return acquire_response(
        (scxml_chttp_resource_impl *)resource->impl,
        uri, uri_size, SCXML_CHTTP_RESOURCE_DATA, 0u,
        expected, NULL, out);
}

static void data_close(void *user, scxml_data_resource *resource_value) {
    scxml_chttp_resource *resource = (scxml_chttp_resource *)user;
    scxml_chttp_resource_impl *impl = resource != NULL
        ? (scxml_chttp_resource_impl *)resource->impl : NULL;
    if (impl == NULL || resource_value == NULL ||
        impl->active != SCXML_CHTTP_ACTIVE_DATA ||
        resource_value->lease != impl)
        return;
    impl->decoded.reader = resource_value->reader;
    release_active(impl);
    memset(resource_value, 0, sizeof(*resource_value));
}

static scxml_resource_status text_open(
    void *user, const char *uri, size_t uri_size,
    size_t max_bytes, scxml_text_resource *out) {
    scxml_chttp_resource *resource = (scxml_chttp_resource *)user;
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (resource == NULL || resource->impl == NULL || out == NULL)
        return SCXML_RESOURCE_FAILED;
    return acquire_response(
        (scxml_chttp_resource_impl *)resource->impl,
        uri, uri_size, SCXML_CHTTP_RESOURCE_TEXT, max_bytes,
        NULL, out, NULL);
}

static void text_close(void *user, scxml_text_resource *resource_value) {
    scxml_chttp_resource *resource = (scxml_chttp_resource *)user;
    scxml_chttp_resource_impl *impl = resource != NULL
        ? (scxml_chttp_resource_impl *)resource->impl : NULL;
    if (impl == NULL || resource_value == NULL ||
        impl->active != SCXML_CHTTP_ACTIVE_TEXT ||
        resource_value->lease != impl)
        return;
    release_active(impl);
    memset(resource_value, 0, sizeof(*resource_value));
}

static const scxml_data_resource_adapter_v1 data_adapter = {
    .abi_version = SCXML_DATA_RESOURCE_ADAPTER_ABI_V1,
    .struct_size = sizeof(scxml_data_resource_adapter_v1),
    .open = data_open,
    .close = data_close};

static const scxml_text_resource_adapter_v1 text_adapter = {
    .abi_version = SCXML_TEXT_RESOURCE_ADAPTER_ABI_V1,
    .struct_size = sizeof(scxml_text_resource_adapter_v1),
    .open = text_open,
    .close = text_close};

scxml_status scxml_chttp_resource_init(
    scxml_chttp_resource *resource,
    const scxml_chttp_resource_config_v1 *config) {
    scxml_chttp_resource_impl *impl;
    if (resource == NULL || resource->impl != NULL || !config_valid(config))
        return SCXML_INVALID_ARGUMENT;
    impl = (scxml_chttp_resource_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return SCXML_ALLOCATION_FAILED;
    impl->config = *config;
    impl->transport = production_transport;
    resource->impl = impl;
    return SCXML_OK;
}

const scxml_data_resource_adapter_v1 *
scxml_chttp_resource_data_adapter(const scxml_chttp_resource *resource) {
    return resource != NULL && resource->impl != NULL ? &data_adapter : NULL;
}

const scxml_text_resource_adapter_v1 *
scxml_chttp_resource_text_adapter(const scxml_chttp_resource *resource) {
    return resource != NULL && resource->impl != NULL ? &text_adapter : NULL;
}

scxml_status scxml_chttp_resource_destroy(scxml_chttp_resource *resource) {
    scxml_chttp_resource_impl *impl;
    if (resource == NULL) return SCXML_INVALID_ARGUMENT;
    impl = (scxml_chttp_resource_impl *)resource->impl;
    if (impl == NULL) return SCXML_OK;
    if (impl->active != SCXML_CHTTP_ACTIVE_NONE)
        return SCXML_INVALID_ARGUMENT;
    free(impl);
    resource->impl = NULL;
    return SCXML_OK;
}

scxml_status scxml_chttp_resource_set_transport_for_test(
    scxml_chttp_resource *resource,
    const scxml_chttp_transport_v1 *transport,
    void *transport_user) {
    scxml_chttp_resource_impl *impl = resource != NULL
        ? (scxml_chttp_resource_impl *)resource->impl : NULL;
    if (impl == NULL || impl->active != SCXML_CHTTP_ACTIVE_NONE ||
        !transport_valid(transport))
        return SCXML_INVALID_ARGUMENT;
    impl->transport = *transport;
    impl->transport_user = transport_user;
    return SCXML_OK;
}
