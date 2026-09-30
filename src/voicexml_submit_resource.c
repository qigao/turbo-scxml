#include <voicexml/submit_resource.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const char submit_urlencoded_type[] =
    "application/x-www-form-urlencoded";
static const char submit_vxml_type[] =
    "application/voicexml+xml";

static bool bytes_valid(
    const char *data, size_t size, bool allow_empty) {
    return (allow_empty || size != 0u) &&
        (size == 0u || data != NULL) &&
        (size == 0u || memchr(data, '\0', size) == NULL);
}

static bool view_equal_literal(
    const char *data, size_t size, const char *literal) {
    const size_t literal_size = literal != NULL ? strlen(literal) : 0u;
    return data != NULL && size == literal_size &&
        memcmp(data, literal, size) == 0;
}

static bool adapter_valid(
    const vxml_submit_resource_adapter_v1 *adapter) {
    const size_t prefix =
        offsetof(vxml_submit_resource_adapter_v1, close) +
        sizeof(adapter->close);
    return adapter != NULL &&
        adapter->abi_version == VXML_SUBMIT_RESOURCE_ADAPTER_ABI_V1 &&
        adapter->struct_size >= prefix &&
        adapter->execute != NULL &&
        adapter->close != NULL;
}

static void close_if_live(
    const vxml_submit_resource_adapter_v1 *adapter,
    void *user,
    vxml_submit_response *response) {
    if (adapter != NULL && adapter->close != NULL &&
        response != NULL && response->lease != NULL)
        adapter->close(user, response);
    if (response != NULL)
        *response = (vxml_submit_response){0};
}

static bool form_safe(unsigned char byte) {
    return (byte >= 'a' && byte <= 'z') ||
        (byte >= 'A' && byte <= 'Z') ||
        (byte >= '0' && byte <= '9') ||
        byte == '-' || byte == '_' || byte == '.' || byte == '~';
}

static bool encoded_component_size(
    const char *data, size_t size, size_t *out) {
    size_t total = 0u;
    size_t index;
    if (out == NULL || (size != 0u && data == NULL))
        return false;
    for (index = 0u; index < size; ++index) {
        const unsigned char byte = (unsigned char)data[index];
        const size_t add = byte == ' ' || form_safe(byte) ? 1u : 3u;
        if (add > SIZE_MAX - total) return false;
        total += add;
    }
    *out = total;
    return true;
}

static char hex_digit(unsigned value) {
    static const char digits[] = "0123456789ABCDEF";
    return digits[value & 0x0fu];
}

static void encode_component(
    char *out, size_t *cursor,
    const char *data, size_t size) {
    size_t index;
    for (index = 0u; index < size; ++index) {
        const unsigned char byte = (unsigned char)data[index];
        if (byte == ' ') {
            out[(*cursor)++] = '+';
        } else if (form_safe(byte)) {
            out[(*cursor)++] = (char)byte;
        } else {
            out[(*cursor)++] = '%';
            out[(*cursor)++] = hex_digit(byte >> 4u);
            out[(*cursor)++] = hex_digit(byte);
        }
    }
}

static vxml_submit_resource_status validate_fields(
    const vxml_submit_request_v1 *request,
    size_t *out_encoded_size) {
    size_t total = 0u;
    size_t index;
    if (out_encoded_size == NULL)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    *out_encoded_size = 0u;
    if (request->field_count != 0u && request->fields == NULL)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    for (index = 0u; index < request->field_count; ++index) {
        const vxml_submit_field_v1 *field = &request->fields[index];
        size_t name_encoded = 0u;
        size_t value_encoded = 0u;
        size_t prior;
        if (!bytes_valid(field->name, field->name_size, false) ||
            !bytes_valid(field->value, field->value_size, true))
            return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
        for (prior = 0u; prior < index; ++prior) {
            const vxml_submit_field_v1 *previous =
                &request->fields[prior];
            if (field->name_size == previous->name_size &&
                memcmp(
                    field->name, previous->name,
                    field->name_size) == 0)
                return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
        }
        if (!encoded_component_size(
                field->name, field->name_size, &name_encoded) ||
            !encoded_component_size(
                field->value, field->value_size, &value_encoded))
            return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
        if (index != 0u) {
            if (total == SIZE_MAX)
                return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
            ++total;
        }
        if (name_encoded > SIZE_MAX - total)
            return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
        total += name_encoded;
        if (total == SIZE_MAX)
            return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
        ++total; /* '=' */
        if (value_encoded > SIZE_MAX - total)
            return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
        total += value_encoded;
    }
    if (total > request->max_body_bytes)
        return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
    *out_encoded_size = total;
    return VXML_SUBMIT_RESOURCE_OK;
}

static void encode_fields(
    const vxml_submit_request_v1 *request,
    char *out) {
    size_t cursor = 0u;
    size_t index;
    for (index = 0u; index < request->field_count; ++index) {
        const vxml_submit_field_v1 *field = &request->fields[index];
        if (index != 0u) out[cursor++] = '&';
        encode_component(
            out, &cursor, field->name, field->name_size);
        out[cursor++] = '=';
        encode_component(
            out, &cursor, field->value, field->value_size);
    }
    out[cursor] = '\0';
}

const char *vxml_submit_resource_status_string(
    vxml_submit_resource_status status) {
    switch (status) {
    case VXML_SUBMIT_RESOURCE_OK:
        return "ok";
    case VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT:
        return "invalid_argument";
    case VXML_SUBMIT_RESOURCE_ALLOCATION_FAILED:
        return "allocation_failed";
    case VXML_SUBMIT_RESOURCE_INVALID_URI:
        return "invalid_uri";
    case VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED:
        return "limit_exceeded";
    case VXML_SUBMIT_RESOURCE_UNSUPPORTED_ENCODING:
        return "unsupported_encoding";
    case VXML_SUBMIT_RESOURCE_PROVIDER_ERROR:
        return "provider_error";
    case VXML_SUBMIT_RESOURCE_POSSIBLY_PROCESSED:
        return "possibly_processed";
    case VXML_SUBMIT_RESOURCE_INVALID_RESPONSE:
        return "invalid_response";
    default:
        return "unknown";
    }
}

vxml_submit_resource_status vxml_submit_resource_execute(
    const vxml_document_store *resolver,
    const vxml_submit_resource_adapter_v1 *adapter,
    void *adapter_user,
    const vxml_submit_request_v1 *request,
    vxml_submit_response *out_response) {
    char *resolved_uri = NULL;
    char *fragment = NULL;
    char *encoded = NULL;
    char *wire_uri = NULL;
    size_t encoded_size = 0u;
    size_t wire_uri_size = 0u;
    vxml_resolved_uri_v1 resolved = VXML_RESOLVED_URI_V1_INIT;
    vxml_submit_wire_request_v1 wire = {0};
    vxml_submit_response response = {0};
    vxml_submit_resource_status status;
    vxml_document_store_status resolve_status;

    if (out_response != NULL)
        *out_response = (vxml_submit_response){0};
    if (resolver == NULL || resolver->impl == NULL ||
        !adapter_valid(adapter) ||
        request == NULL || out_response == NULL ||
        request->abi_version != VXML_SUBMIT_REQUEST_ABI_V1 ||
        request->struct_size < sizeof(*request) ||
        !bytes_valid(request->target, request->target_size, false) ||
        (request->base_document_uri_size != 0u &&
         !bytes_valid(
             request->base_document_uri,
             request->base_document_uri_size, false)) ||
        !bytes_valid(
            request->enctype, request->enctype_size, true) ||
        (request->method != VXML_SUBMIT_METHOD_GET &&
         request->method != VXML_SUBMIT_METHOD_POST) ||
        request->max_uri_bytes == 0u ||
        request->max_uri_bytes == SIZE_MAX ||
        request->max_body_bytes == 0u ||
        request->max_body_bytes == SIZE_MAX ||
        request->max_response_bytes == 0u)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;

    if (request->method == VXML_SUBMIT_METHOD_POST &&
        request->enctype_size != 0u &&
        !view_equal_literal(
            request->enctype, request->enctype_size,
            submit_urlencoded_type))
        return VXML_SUBMIT_RESOURCE_UNSUPPORTED_ENCODING;

    status = validate_fields(request, &encoded_size);
    if (status != VXML_SUBMIT_RESOURCE_OK)
        return status;

    resolved_uri = (char *)malloc(request->max_uri_bytes + 1u);
    fragment = (char *)malloc(request->max_uri_bytes + 1u);
    encoded = (char *)malloc(encoded_size + 1u);
    if (resolved_uri == NULL || fragment == NULL || encoded == NULL) {
        free(encoded);
        free(fragment);
        free(resolved_uri);
        return VXML_SUBMIT_RESOURCE_ALLOCATION_FAILED;
    }
    encoded[0] = '\0';
    encode_fields(request, encoded);

    resolved.document_uri = resolved_uri;
    resolved.document_uri_capacity = request->max_uri_bytes + 1u;
    resolved.fragment = fragment;
    resolved.fragment_capacity = request->max_uri_bytes + 1u;
    resolve_status = vxml_document_store_resolve(
        resolver,
        request->base_document_uri,
        request->base_document_uri_size,
        request->target,
        request->target_size,
        &resolved);
    if (resolve_status != VXML_DOCUMENT_STORE_OK ||
        resolved.document_uri_size == 0u) {
        free(encoded);
        free(fragment);
        free(resolved_uri);
        return resolve_status == VXML_DOCUMENT_STORE_LIMIT_EXCEEDED
            ? VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED
            : VXML_SUBMIT_RESOURCE_INVALID_URI;
    }

    wire_uri = resolved_uri;
    wire_uri_size = resolved.document_uri_size;
    if (request->method == VXML_SUBMIT_METHOD_GET &&
        encoded_size != 0u) {
        const bool has_query =
            memchr(resolved_uri, '?', wire_uri_size) != NULL;
        if (wire_uri_size > request->max_uri_bytes - 1u ||
            encoded_size >
                request->max_uri_bytes - wire_uri_size - 1u) {
            free(encoded);
            free(fragment);
            free(resolved_uri);
            return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
        }
        wire_uri[wire_uri_size++] = has_query ? '&' : '?';
        memcpy(wire_uri + wire_uri_size, encoded, encoded_size);
        wire_uri_size += encoded_size;
        wire_uri[wire_uri_size] = '\0';
    }

    wire = (vxml_submit_wire_request_v1){
        .abi_version = VXML_SUBMIT_WIRE_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_submit_wire_request_v1),
        .uri = wire_uri,
        .uri_size = wire_uri_size,
        .fragment = resolved.fragment_size != 0u ? fragment : NULL,
        .fragment_size = resolved.fragment_size,
        .method = request->method,
        .content_type =
            request->method == VXML_SUBMIT_METHOD_POST
                ? submit_urlencoded_type : NULL,
        .content_type_size =
            request->method == VXML_SUBMIT_METHOD_POST
                ? sizeof(submit_urlencoded_type) - 1u : 0u,
        .body =
            request->method == VXML_SUBMIT_METHOD_POST &&
                    encoded_size != 0u
                ? encoded : NULL,
        .body_size =
            request->method == VXML_SUBMIT_METHOD_POST
                ? encoded_size : 0u};

    /*
     * Exactly one provider attempt. There is intentionally no loop here:
     * POSSIBLY_PROCESSED is returned unchanged to the caller.
     */
    status = adapter->execute(
        adapter_user, &wire, &response);

    free(encoded);
    free(fragment);
    free(resolved_uri);

    if (status != VXML_SUBMIT_RESOURCE_OK) {
        close_if_live(adapter, adapter_user, &response);
        return status;
    }
    if (response.lease == NULL ||
        (response.size != 0u && response.data == NULL) ||
        response.size > request->max_response_bytes ||
        !bytes_valid(
            response.media_type,
            response.media_type_size, false) ||
        !view_equal_literal(
            response.media_type,
            response.media_type_size,
            submit_vxml_type) ||
        (response.effective_uri_size != 0u &&
         !bytes_valid(
             response.effective_uri,
             response.effective_uri_size, false))) {
        const vxml_submit_resource_status failure =
            response.size > request->max_response_bytes
                ? VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED
                : VXML_SUBMIT_RESOURCE_INVALID_RESPONSE;
        close_if_live(adapter, adapter_user, &response);
        return failure;
    }

    *out_response = response;
    return VXML_SUBMIT_RESOURCE_OK;
}

vxml_submit_resource_status vxml_submit_resource_close(
    const vxml_submit_resource_adapter_v1 *adapter,
    void *adapter_user,
    vxml_submit_response *response) {
    if (!adapter_valid(adapter) || response == NULL)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    if (response->lease == NULL) {
        if (response->data != NULL || response->size != 0u ||
            response->media_type != NULL ||
            response->media_type_size != 0u ||
            response->effective_uri != NULL ||
            response->effective_uri_size != 0u)
            return VXML_SUBMIT_RESOURCE_INVALID_RESPONSE;
        return VXML_SUBMIT_RESOURCE_OK;
    }
    adapter->close(adapter_user, response);
    *response = (vxml_submit_response){0};
    return VXML_SUBMIT_RESOURCE_OK;
}
