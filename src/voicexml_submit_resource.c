#include <voicexml/submit_resource.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const char submit_urlencoded_type[] =
    "application/x-www-form-urlencoded";
static const char submit_vxml_type[] =
    "application/voicexml+xml";
static const char submit_multipart_type[] =
    "multipart/form-data";
static const char submit_boundary_prefix[] =
    "----TurboSCXMLVoiceXML";
static const char submit_default_filename[] =
    "recording";

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

static bool adapter_close_valid(
    const vxml_submit_resource_adapter_v1 *adapter) {
    const size_t prefix =
        offsetof(vxml_submit_resource_adapter_v1, close) +
        sizeof(adapter->close);
    return adapter != NULL &&
        adapter->abi_version == VXML_SUBMIT_RESOURCE_ADAPTER_ABI_V1 &&
        adapter->struct_size >= prefix &&
        adapter->close != NULL;
}

static bool adapter_valid(
    const vxml_submit_resource_adapter_v1 *adapter) {
    return adapter_close_valid(adapter) &&
        adapter->execute != NULL;
}

static bool adapter_v2_valid(
    const vxml_submit_resource_adapter_v1 *adapter) {
    const size_t tail =
        offsetof(vxml_submit_resource_adapter_v1, execute_v2) +
        sizeof(adapter->execute_v2);
    return adapter_close_valid(adapter) &&
        adapter->struct_size >= tail &&
        adapter->execute_v2 != NULL;
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

static bool checked_add_size(
    size_t *value, size_t amount) {
    if (value == NULL || amount > SIZE_MAX - *value)
        return false;
    *value += amount;
    return true;
}

static bool multipart_header_bytes_valid(
    const char *data, size_t size, bool allow_empty) {
    size_t index;
    if (!bytes_valid(data, size, allow_empty))
        return false;
    for (index = 0u; index < size; ++index)
        if (data[index] == '\r' || data[index] == '\n')
            return false;
    return true;
}

static bool multipart_quoted_size(
    const char *data, size_t size, size_t *out) {
    size_t total = 0u;
    size_t index;
    if (out == NULL ||
        !multipart_header_bytes_valid(data, size, false))
        return false;
    for (index = 0u; index < size; ++index) {
        const size_t add =
            data[index] == '"' || data[index] == '\\' ? 2u : 1u;
        if (!checked_add_size(&total, add))
            return false;
    }
    *out = total;
    return true;
}

static void multipart_write_quoted(
    char *out, size_t *cursor,
    const char *data, size_t size) {
    size_t index;
    for (index = 0u; index < size; ++index) {
        if (data[index] == '"' || data[index] == '\\')
            out[(*cursor)++] = '\\';
        out[(*cursor)++] = data[index];
    }
}

static uint64_t multipart_hash_bytes(
    uint64_t hash, const void *data, size_t size) {
    const unsigned char *bytes =
        (const unsigned char *)data;
    size_t index;
    for (index = 0u; index < size; ++index) {
        hash ^= (uint64_t)bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t multipart_hash_size(
    uint64_t hash, size_t value) {
    size_t index;
    for (index = 0u; index < sizeof(value); ++index) {
        const unsigned char byte =
            (unsigned char)((value >> (index * 8u)) & 0xffu);
        hash ^= (uint64_t)byte;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static size_t multipart_make_boundary(
    char *out, uint64_t hash, unsigned attempt) {
    static const char hex[] = "0123456789ABCDEF";
    const size_t prefix_size =
        sizeof(submit_boundary_prefix) - 1u;
    size_t cursor = 0u;
    unsigned index;
    memcpy(out, submit_boundary_prefix, prefix_size);
    cursor += prefix_size;
    out[cursor++] = '-';
    for (index = 0u; index < 16u; ++index) {
        const unsigned shift = (15u - index) * 4u;
        out[cursor++] = hex[
            (unsigned)((hash >> shift) & UINT64_C(0x0f))];
    }
    out[cursor++] = '-';
    out[cursor++] = hex[(attempt >> 4u) & 0x0fu];
    out[cursor++] = hex[attempt & 0x0fu];
    out[cursor] = '\0';
    return cursor;
}

static bool multipart_contains(
    const void *data, size_t size,
    const char *needle, size_t needle_size) {
    const unsigned char *bytes =
        (const unsigned char *)data;
    size_t index;
    if (needle_size == 0u || size < needle_size ||
        data == NULL || needle == NULL)
        return false;
    for (index = 0u; index <= size - needle_size; ++index)
        if (memcmp(bytes + index, needle, needle_size) == 0)
            return true;
    return false;
}

static bool multipart_boundary_conflicts(
    const vxml_submit_multipart_request_v1 *request,
    const char *boundary, size_t boundary_size) {
    size_t index;
    for (index = 0u; index < request->field_count; ++index)
        if (multipart_contains(
                request->fields[index].value,
                request->fields[index].value_size,
                boundary, boundary_size))
            return true;
    for (index = 0u; index < request->recording_count; ++index)
        if (multipart_contains(
                request->recordings[index].data,
                request->recordings[index].size,
                boundary, boundary_size))
            return true;
    return false;
}

static bool multipart_name_duplicate(
    const vxml_submit_multipart_request_v1 *request,
    const char *name, size_t name_size,
    size_t text_before, size_t recording_before) {
    size_t index;
    for (index = 0u; index < text_before; ++index) {
        const vxml_submit_field_v1 *field =
            &request->fields[index];
        if (field->name_size == name_size &&
            memcmp(field->name, name, name_size) == 0)
            return true;
    }
    for (index = 0u; index < recording_before; ++index) {
        const vxml_submit_recording_field_v1 *field =
            &request->recordings[index];
        if (field->name_size == name_size &&
            memcmp(field->name, name, name_size) == 0)
            return true;
    }
    return false;
}


static bool multipart_order_tail_present(
    const vxml_submit_multipart_request_v1 *request) {
    const size_t tail =
        offsetof(
            vxml_submit_multipart_request_v1,
            part_count) +
        sizeof(request->part_count);
    return request != NULL &&
        request->struct_size >= tail;
}

static bool multipart_order_enabled(
    const vxml_submit_multipart_request_v1 *request) {
    return multipart_order_tail_present(request) &&
        request->part_count != 0u;
}

static bool multipart_part_name(
    const vxml_submit_multipart_request_v1 *request,
    const vxml_submit_multipart_part_ref_v1 *part,
    const char **out_name, size_t *out_name_size) {
    if (out_name != NULL) *out_name = NULL;
    if (out_name_size != NULL) *out_name_size = 0u;
    if (request == NULL || part == NULL ||
        out_name == NULL || out_name_size == NULL)
        return false;
    if (part->kind == VXML_SUBMIT_MULTIPART_PART_TEXT) {
        if (part->index >= request->field_count ||
            request->fields == NULL)
            return false;
        *out_name = request->fields[part->index].name;
        *out_name_size =
            request->fields[part->index].name_size;
        return true;
    }
    if (part->kind ==
            VXML_SUBMIT_MULTIPART_PART_RECORDING) {
        if (part->index >= request->recording_count ||
            request->recordings == NULL)
            return false;
        *out_name =
            request->recordings[part->index].name;
        *out_name_size =
            request->recordings[part->index].name_size;
        return true;
    }
    return false;
}

static bool multipart_order_valid(
    const vxml_submit_multipart_request_v1 *request,
    size_t part_count) {
    size_t index;
    if (!multipart_order_tail_present(request))
        return true;
    if ((request->parts == NULL) !=
        (request->part_count == 0u))
        return false;
    if (request->part_count == 0u)
        return true;
    if (request->part_count != part_count)
        return false;
    for (index = 0u;
         index < request->part_count;
         ++index) {
        const vxml_submit_multipart_part_ref_v1 *part =
            &request->parts[index];
        const char *name = NULL;
        size_t name_size = 0u;
        size_t prior;
        if (!multipart_part_name(
                request, part, &name, &name_size))
            return false;
        for (prior = 0u; prior < index; ++prior) {
            const vxml_submit_multipart_part_ref_v1 *previous =
                &request->parts[prior];
            const char *previous_name = NULL;
            size_t previous_name_size = 0u;
            if (previous->kind == part->kind &&
                previous->index == part->index)
                return false;
            if (!multipart_part_name(
                    request, previous,
                    &previous_name,
                    &previous_name_size))
                return false;
            if (name_size == previous_name_size &&
                name_size != 0u &&
                name != NULL &&
                previous_name != NULL &&
                memcmp(
                    name, previous_name,
                    name_size) == 0)
                return false;
        }
    }
    return true;
}

static vxml_submit_resource_status multipart_measure_text_part(
    const vxml_submit_multipart_request_v1 *request,
    const vxml_submit_field_v1 *field,
    bool has_previous,
    uint64_t *hash,
    size_t *metadata,
    size_t *body,
    size_t *segments) {
    static const size_t text_fixed =
        sizeof(
            "--\r\nContent-Disposition: form-data; name=\"\"\r\n\r\n") -
        1u;
    size_t quoted_name = 0u;
    size_t header_size = text_fixed;
    if (request == NULL || field == NULL || hash == NULL ||
        metadata == NULL || body == NULL || segments == NULL ||
        !multipart_quoted_size(
            field->name, field->name_size,
            &quoted_name) ||
        !bytes_valid(
            field->value, field->value_size, true) ||
        field->value_size > request->max_body_bytes)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    if (has_previous &&
        !checked_add_size(&header_size, 2u))
        return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
    if (!checked_add_size(
            &header_size, quoted_name) ||
        !checked_add_size(metadata, header_size) ||
        !checked_add_size(body, header_size) ||
        !checked_add_size(body, field->value_size) ||
        !checked_add_size(
            segments,
            field->value_size != 0u ? 2u : 1u))
        return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
    *hash = multipart_hash_bytes(
        *hash, field->name, field->name_size);
    *hash = multipart_hash_bytes(
        *hash, field->value, field->value_size);
    return VXML_SUBMIT_RESOURCE_OK;
}

static vxml_submit_resource_status multipart_measure_recording_part(
    const vxml_submit_multipart_request_v1 *request,
    const vxml_submit_recording_field_v1 *field,
    bool has_previous,
    uint64_t *hash,
    size_t *metadata,
    size_t *body,
    size_t *segments) {
    static const size_t recording_fixed =
        sizeof(
            "--\r\nContent-Disposition: form-data; name=\"\"; filename=\"\"\r\nContent-Type: \r\n\r\n") -
        1u;
    const char *filename;
    size_t filename_size;
    size_t quoted_name = 0u;
    size_t quoted_filename = 0u;
    size_t header_size = recording_fixed;
    if (request == NULL || field == NULL || hash == NULL ||
        metadata == NULL || body == NULL || segments == NULL)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    filename = field->filename_size != 0u
        ? field->filename : submit_default_filename;
    filename_size = field->filename_size != 0u
        ? field->filename_size
        : sizeof(submit_default_filename) - 1u;
    if (!multipart_quoted_size(
            field->name, field->name_size,
            &quoted_name) ||
        !multipart_quoted_size(
            filename, filename_size,
            &quoted_filename) ||
        !multipart_header_bytes_valid(
            field->media_type,
            field->media_type_size, false) ||
        field->data == NULL ||
        field->size == 0u ||
        field->size > request->max_body_bytes)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    if (has_previous &&
        !checked_add_size(&header_size, 2u))
        return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
    if (!checked_add_size(
            &header_size, quoted_name) ||
        !checked_add_size(
            &header_size, quoted_filename) ||
        !checked_add_size(
            &header_size, field->media_type_size) ||
        !checked_add_size(metadata, header_size) ||
        !checked_add_size(body, header_size) ||
        !checked_add_size(body, field->size) ||
        !checked_add_size(segments, 2u))
        return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
    *hash = multipart_hash_bytes(
        *hash, field->name, field->name_size);
    *hash = multipart_hash_bytes(
        *hash, filename, filename_size);
    *hash = multipart_hash_bytes(
        *hash, field->media_type,
        field->media_type_size);
    *hash = multipart_hash_bytes(
        *hash, field->data, field->size);
    return VXML_SUBMIT_RESOURCE_OK;
}

static vxml_submit_resource_status multipart_validate(
    const vxml_submit_multipart_request_v1 *request,
    char *boundary, size_t *out_boundary_size,
    size_t *out_metadata_size,
    size_t *out_segment_capacity,
    size_t *out_body_size) {
    static const size_t trailer_fixed =
        sizeof("\r\n----\r\n") - 1u;
    const size_t historical_prefix =
        offsetof(
            vxml_submit_multipart_request_v1,
            max_segments) +
        sizeof(request->max_segments);
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t parts;
    size_t metadata = 0u;
    size_t body = 0u;
    size_t segments = 1u;
    size_t part_index = 0u;
    size_t index;
    unsigned attempt;
    size_t boundary_size = 0u;
    vxml_submit_resource_status status;

    if (request == NULL || boundary == NULL ||
        out_boundary_size == NULL ||
        out_metadata_size == NULL ||
        out_segment_capacity == NULL ||
        out_body_size == NULL ||
        request->abi_version !=
            VXML_SUBMIT_MULTIPART_REQUEST_ABI_V1 ||
        request->struct_size < historical_prefix ||
        !bytes_valid(
            request->target,
            request->target_size, false) ||
        (request->base_document_uri_size != 0u &&
         !bytes_valid(
             request->base_document_uri,
             request->base_document_uri_size, false)) ||
        request->recording_count == 0u ||
        request->recordings == NULL ||
        (request->field_count != 0u &&
         request->fields == NULL) ||
        request->max_uri_bytes == 0u ||
        request->max_uri_bytes == SIZE_MAX ||
        request->max_body_bytes == 0u ||
        request->max_body_bytes == SIZE_MAX ||
        request->max_response_bytes == 0u ||
        request->max_parts == 0u ||
        request->max_boundary_bytes == 0u ||
        request->max_header_bytes == 0u ||
        request->max_segments == 0u)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;

    if (request->field_count >
        SIZE_MAX - request->recording_count)
        return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
    parts =
        request->field_count + request->recording_count;
    if (parts > request->max_parts)
        return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
    if (!multipart_order_valid(request, parts))
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;

    hash = multipart_hash_bytes(
        hash, request->target, request->target_size);
    hash = multipart_hash_size(
        hash, request->field_count);
    hash = multipart_hash_size(
        hash, request->recording_count);

    if (multipart_order_enabled(request)) {
        hash = multipart_hash_size(
            hash, request->part_count);
        for (index = 0u;
             index < request->part_count;
             ++index) {
            const vxml_submit_multipart_part_ref_v1 *part =
                &request->parts[index];
            hash = multipart_hash_size(
                hash, (size_t)part->kind);
            hash = multipart_hash_size(
                hash, part->index);
            if (part->kind ==
                    VXML_SUBMIT_MULTIPART_PART_TEXT) {
                status = multipart_measure_text_part(
                    request,
                    &request->fields[part->index],
                    part_index != 0u,
                    &hash, &metadata,
                    &body, &segments);
            } else {
                status =
                    multipart_measure_recording_part(
                        request,
                        &request->recordings[
                            part->index],
                        part_index != 0u,
                        &hash, &metadata,
                        &body, &segments);
            }
            if (status != VXML_SUBMIT_RESOURCE_OK)
                return status;
            ++part_index;
        }
    } else {
        for (index = 0u;
             index < request->field_count;
             ++index) {
            const vxml_submit_field_v1 *field =
                &request->fields[index];
            if (multipart_name_duplicate(
                    request,
                    field->name,
                    field->name_size,
                    index, 0u))
                return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
            status = multipart_measure_text_part(
                request, field,
                part_index != 0u,
                &hash, &metadata,
                &body, &segments);
            if (status != VXML_SUBMIT_RESOURCE_OK)
                return status;
            ++part_index;
        }
        for (index = 0u;
             index < request->recording_count;
             ++index) {
            const vxml_submit_recording_field_v1 *field =
                &request->recordings[index];
            if (multipart_name_duplicate(
                    request,
                    field->name,
                    field->name_size,
                    request->field_count, index))
                return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
            status =
                multipart_measure_recording_part(
                    request, field,
                    part_index != 0u,
                    &hash, &metadata,
                    &body, &segments);
            if (status != VXML_SUBMIT_RESOURCE_OK)
                return status;
            ++part_index;
        }
    }

    for (attempt = 0u; attempt < 16u; ++attempt) {
        const uint64_t candidate_hash =
            hash ^ ((uint64_t)(attempt + 1u) *
                    UINT64_C(
                        0x9E3779B97F4A7C15));
        boundary_size = multipart_make_boundary(
            boundary, candidate_hash, attempt);
        if (boundary_size >
            request->max_boundary_bytes)
            return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
        if (!multipart_boundary_conflicts(
                request, boundary, boundary_size))
            break;
    }
    if (attempt == 16u)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;

    if (parts != 0u &&
        boundary_size > SIZE_MAX / parts)
        return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
    if (!checked_add_size(
            &metadata, parts * boundary_size) ||
        !checked_add_size(
            &body, parts * boundary_size))
        return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;

    {
        size_t trailer = trailer_fixed;
        if (!checked_add_size(
                &trailer, boundary_size) ||
            !checked_add_size(
                &metadata, trailer) ||
            !checked_add_size(&body, trailer))
            return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
    }

    {
        const size_t content_type_size =
            sizeof(submit_multipart_type) - 1u +
            sizeof("; boundary=") - 1u +
            boundary_size;
        size_t header_budget = metadata;
        if (!checked_add_size(
                &header_budget,
                content_type_size) ||
            header_budget >
                request->max_header_bytes ||
            body > request->max_body_bytes ||
            segments > request->max_segments)
            return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
    }

    *out_boundary_size = boundary_size;
    *out_metadata_size = metadata;
    *out_segment_capacity = segments;
    *out_body_size = body;
    return VXML_SUBMIT_RESOURCE_OK;
}

static void multipart_copy(
    char *out, size_t *cursor,
    const void *data, size_t size) {
    if (size != 0u) {
        memcpy(out + *cursor, data, size);
        *cursor += size;
    }
}

static void multipart_add_segment(
    vxml_submit_body_segment_v1 *segments,
    size_t *segment_count,
    const void *data, size_t size) {
    if (size == 0u) return;
    segments[*segment_count] =
        (vxml_submit_body_segment_v1){data, size};
    ++*segment_count;
}


static vxml_submit_resource_status multipart_build_text_part(
    const vxml_submit_field_v1 *field,
    bool has_previous,
    const char *boundary, size_t boundary_size,
    char *metadata, size_t metadata_size,
    size_t *cursor,
    vxml_submit_body_segment_v1 *segments,
    size_t segment_capacity,
    size_t *segment_count) {
    static const char disposition[] =
        "Content-Disposition: form-data; name=\"";
    static const char text_header_end[] =
        "\"\r\n\r\n";
    const size_t start =
        cursor != NULL ? *cursor : 0u;
    if (field == NULL || boundary == NULL ||
        metadata == NULL || cursor == NULL ||
        segments == NULL || segment_count == NULL)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    if (has_previous)
        multipart_copy(
            metadata, cursor, "\r\n", 2u);
    multipart_copy(
        metadata, cursor, "--", 2u);
    multipart_copy(
        metadata, cursor,
        boundary, boundary_size);
    multipart_copy(
        metadata, cursor, "\r\n", 2u);
    multipart_copy(
        metadata, cursor,
        disposition, sizeof(disposition) - 1u);
    multipart_write_quoted(
        metadata, cursor,
        field->name, field->name_size);
    multipart_copy(
        metadata, cursor,
        text_header_end,
        sizeof(text_header_end) - 1u);
    if (*cursor > metadata_size ||
        *segment_count >= segment_capacity)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    multipart_add_segment(
        segments, segment_count,
        metadata + start, *cursor - start);
    multipart_add_segment(
        segments, segment_count,
        field->value, field->value_size);
    return *segment_count <= segment_capacity
        ? VXML_SUBMIT_RESOURCE_OK
        : VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
}

static vxml_submit_resource_status multipart_build_recording_part(
    const vxml_submit_recording_field_v1 *field,
    bool has_previous,
    const char *boundary, size_t boundary_size,
    char *metadata, size_t metadata_size,
    size_t *cursor,
    vxml_submit_body_segment_v1 *segments,
    size_t segment_capacity,
    size_t *segment_count) {
    static const char disposition[] =
        "Content-Disposition: form-data; name=\"";
    static const char filename_marker[] =
        "\"; filename=\"";
    static const char content_type_marker[] =
        "\"\r\nContent-Type: ";
    static const char recording_header_end[] =
        "\r\n\r\n";
    const char *filename;
    size_t filename_size;
    const size_t start =
        cursor != NULL ? *cursor : 0u;
    if (field == NULL || boundary == NULL ||
        metadata == NULL || cursor == NULL ||
        segments == NULL || segment_count == NULL)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    filename = field->filename_size != 0u
        ? field->filename : submit_default_filename;
    filename_size = field->filename_size != 0u
        ? field->filename_size
        : sizeof(submit_default_filename) - 1u;
    if (has_previous)
        multipart_copy(
            metadata, cursor, "\r\n", 2u);
    multipart_copy(
        metadata, cursor, "--", 2u);
    multipart_copy(
        metadata, cursor,
        boundary, boundary_size);
    multipart_copy(
        metadata, cursor, "\r\n", 2u);
    multipart_copy(
        metadata, cursor,
        disposition, sizeof(disposition) - 1u);
    multipart_write_quoted(
        metadata, cursor,
        field->name, field->name_size);
    multipart_copy(
        metadata, cursor,
        filename_marker,
        sizeof(filename_marker) - 1u);
    multipart_write_quoted(
        metadata, cursor,
        filename, filename_size);
    multipart_copy(
        metadata, cursor,
        content_type_marker,
        sizeof(content_type_marker) - 1u);
    multipart_copy(
        metadata, cursor,
        field->media_type,
        field->media_type_size);
    multipart_copy(
        metadata, cursor,
        recording_header_end,
        sizeof(recording_header_end) - 1u);
    if (*cursor > metadata_size ||
        *segment_count >= segment_capacity)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    multipart_add_segment(
        segments, segment_count,
        metadata + start, *cursor - start);
    multipart_add_segment(
        segments, segment_count,
        field->data, field->size);
    return *segment_count <= segment_capacity
        ? VXML_SUBMIT_RESOURCE_OK
        : VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
}

static vxml_submit_resource_status multipart_build(
    const vxml_submit_multipart_request_v1 *request,
    const char *boundary, size_t boundary_size,
    char *metadata, size_t metadata_size,
    vxml_submit_body_segment_v1 *segments,
    size_t segment_capacity,
    size_t *out_segment_count) {
    size_t cursor = 0u;
    size_t segment_count = 0u;
    size_t part_index = 0u;
    size_t index;
    vxml_submit_resource_status status;

    if (out_segment_count == NULL)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    *out_segment_count = 0u;

    if (multipart_order_enabled(request)) {
        for (index = 0u;
             index < request->part_count;
             ++index) {
            const vxml_submit_multipart_part_ref_v1 *part =
                &request->parts[index];
            if (part->kind ==
                    VXML_SUBMIT_MULTIPART_PART_TEXT) {
                status = multipart_build_text_part(
                    &request->fields[part->index],
                    part_index != 0u,
                    boundary, boundary_size,
                    metadata, metadata_size,
                    &cursor,
                    segments, segment_capacity,
                    &segment_count);
            } else if (part->kind ==
                           VXML_SUBMIT_MULTIPART_PART_RECORDING) {
                status =
                    multipart_build_recording_part(
                        &request->recordings[
                            part->index],
                        part_index != 0u,
                        boundary, boundary_size,
                        metadata, metadata_size,
                        &cursor,
                        segments, segment_capacity,
                        &segment_count);
            } else {
                return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
            }
            if (status != VXML_SUBMIT_RESOURCE_OK)
                return status;
            ++part_index;
        }
    } else {
        for (index = 0u;
             index < request->field_count;
             ++index) {
            status = multipart_build_text_part(
                &request->fields[index],
                part_index != 0u,
                boundary, boundary_size,
                metadata, metadata_size,
                &cursor,
                segments, segment_capacity,
                &segment_count);
            if (status != VXML_SUBMIT_RESOURCE_OK)
                return status;
            ++part_index;
        }
        for (index = 0u;
             index < request->recording_count;
             ++index) {
            status =
                multipart_build_recording_part(
                    &request->recordings[index],
                    part_index != 0u,
                    boundary, boundary_size,
                    metadata, metadata_size,
                    &cursor,
                    segments, segment_capacity,
                    &segment_count);
            if (status != VXML_SUBMIT_RESOURCE_OK)
                return status;
            ++part_index;
        }
    }

    {
        const size_t start = cursor;
        multipart_copy(
            metadata, &cursor, "\r\n--", 4u);
        multipart_copy(
            metadata, &cursor,
            boundary, boundary_size);
        multipart_copy(
            metadata, &cursor, "--\r\n", 4u);
        if (cursor != metadata_size ||
            segment_count >= segment_capacity)
            return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
        multipart_add_segment(
            segments, &segment_count,
            metadata + start, cursor - start);
    }

    if (segment_count > segment_capacity)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    *out_segment_count = segment_count;
    return VXML_SUBMIT_RESOURCE_OK;
}

vxml_submit_resource_status vxml_submit_resource_execute_multipart(
    const vxml_document_store *resolver,
    const vxml_submit_resource_adapter_v1 *adapter,
    void *adapter_user,
    const vxml_submit_multipart_request_v1 *request,
    vxml_submit_response *out_response) {
    char boundary[64];
    char content_type[128];
    char *resolved_uri = NULL;
    char *fragment = NULL;
    char *metadata = NULL;
    vxml_submit_body_segment_v1 *segments = NULL;
    size_t boundary_size = 0u;
    size_t metadata_size = 0u;
    size_t segment_capacity = 0u;
    size_t segment_count = 0u;
    size_t body_size = 0u;
    size_t content_type_size = 0u;
    vxml_resolved_uri_v1 resolved = VXML_RESOLVED_URI_V1_INIT;
    vxml_submit_wire_request_v2 wire = {0};
    vxml_submit_response response = {0};
    vxml_submit_resource_status status;
    vxml_document_store_status resolve_status;

    if (out_response != NULL)
        *out_response = (vxml_submit_response){0};
    if (resolver == NULL || resolver->impl == NULL ||
        !adapter_close_valid(adapter) ||
        request == NULL || out_response == NULL)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    if (!adapter_v2_valid(adapter))
        return VXML_SUBMIT_RESOURCE_UNSUPPORTED_ENCODING;

    status = multipart_validate(
        request, boundary, &boundary_size,
        &metadata_size, &segment_capacity, &body_size);
    if (status != VXML_SUBMIT_RESOURCE_OK)
        return status;

    content_type_size =
        sizeof(submit_multipart_type) - 1u +
        sizeof("; boundary=") - 1u +
        boundary_size;
    if (content_type_size >= sizeof(content_type))
        return VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED;
    memcpy(
        content_type,
        submit_multipart_type,
        sizeof(submit_multipart_type) - 1u);
    memcpy(
        content_type + sizeof(submit_multipart_type) - 1u,
        "; boundary=", sizeof("; boundary=") - 1u);
    memcpy(
        content_type +
            sizeof(submit_multipart_type) - 1u +
            sizeof("; boundary=") - 1u,
        boundary, boundary_size);
    content_type[content_type_size] = '\0';

    resolved_uri = (char *)malloc(request->max_uri_bytes + 1u);
    fragment = (char *)malloc(request->max_uri_bytes + 1u);
    metadata = (char *)malloc(metadata_size);
    segments = (vxml_submit_body_segment_v1 *)calloc(
        segment_capacity, sizeof(*segments));
    if (resolved_uri == NULL || fragment == NULL ||
        metadata == NULL || segments == NULL) {
        free(segments);
        free(metadata);
        free(fragment);
        free(resolved_uri);
        return VXML_SUBMIT_RESOURCE_ALLOCATION_FAILED;
    }

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
        free(segments);
        free(metadata);
        free(fragment);
        free(resolved_uri);
        return resolve_status == VXML_DOCUMENT_STORE_LIMIT_EXCEEDED
            ? VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED
            : VXML_SUBMIT_RESOURCE_INVALID_URI;
    }

    status = multipart_build(
        request, boundary, boundary_size,
        metadata, metadata_size,
        segments, segment_capacity, &segment_count);
    if (status != VXML_SUBMIT_RESOURCE_OK) {
        free(segments);
        free(metadata);
        free(fragment);
        free(resolved_uri);
        return status;
    }

    wire = (vxml_submit_wire_request_v2){
        .abi_version = VXML_SUBMIT_WIRE_REQUEST_ABI_V2,
        .struct_size = sizeof(vxml_submit_wire_request_v2),
        .uri = resolved_uri,
        .uri_size = resolved.document_uri_size,
        .fragment =
            resolved.fragment_size != 0u ? fragment : NULL,
        .fragment_size = resolved.fragment_size,
        .method = VXML_SUBMIT_METHOD_POST,
        .content_type = content_type,
        .content_type_size = content_type_size,
        .segments = segments,
        .segment_count = segment_count,
        .body_size = body_size};

    /*
     * Exactly one provider attempt. There is no V1 buffering fallback and no
     * retry after POSSIBLY_PROCESSED.
     */
    status = adapter->execute_v2(
        adapter_user, &wire, &response);

    free(segments);
    free(metadata);
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
    if (!adapter_close_valid(adapter) || response == NULL)
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
