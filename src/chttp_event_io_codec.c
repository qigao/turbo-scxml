#include "chttp_event_io_internal.h"

#include <turbo_vstr.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char FORM_MEDIA_TYPE[] =
    "application/x-www-form-urlencoded";
static const char TEXT_MEDIA_TYPE[] = "text/plain; charset=utf-8";
static const char XML_MEDIA_TYPE[] = "application/xml; charset=utf-8";
static const char RESERVED_EVENT_NAME[] = "_scxmleventname";
static const char DEFAULT_EVENT_NAME[] = "HTTP.POST";

static bool checked_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) return false;
    *out = left + right;
    return true;
}

bool scxml_chttp_codec_utf8_valid(const void *data, size_t size) {
    if (size == 0u) return true;
    return data != NULL && memchr(data, '\0', size) == NULL &&
           vstr_utf8_valid(vstr_from_buf((const char *)data, size));
}

static bool form_unreserved(unsigned char value) {
    return (value >= (unsigned char)'A' && value <= (unsigned char)'Z') ||
           (value >= (unsigned char)'a' && value <= (unsigned char)'z') ||
           (value >= (unsigned char)'0' && value <= (unsigned char)'9') ||
           value == (unsigned char)'-' || value == (unsigned char)'.' ||
           value == (unsigned char)'_' || value == (unsigned char)'~';
}

static bool form_encoded_size(
    const char *data, size_t size, size_t limit, size_t *out) {
    size_t required = 0u;
    size_t index;
    if (size > limit || !scxml_chttp_codec_utf8_valid(data, size)) return false;
    for (index = 0u; index < size; ++index) {
        const unsigned char value = (unsigned char)data[index];
        const size_t width = form_unreserved(value) || value == ' ' ? 1u : 3u;
        if (!checked_add(required, width, &required)) return false;
    }
    *out = required;
    return true;
}

static char *form_encode(char *out, const char *data, size_t size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t index;
    for (index = 0u; index < size; ++index) {
        const unsigned char value = (unsigned char)data[index];
        if (form_unreserved(value)) {
            *out++ = (char)value;
        } else if (value == (unsigned char)' ') {
            *out++ = '+';
        } else {
            *out++ = '%';
            *out++ = hex[value >> 4u];
            *out++ = hex[value & 0x0fu];
        }
    }
    return out;
}

static bool canonicalize_float_text(char storage[64], int *in_out_written) {
    size_t read_index = 0u;
    size_t write_index = 0u;
    const size_t size = (size_t)*in_out_written;

    if (read_index < size &&
        (storage[read_index] == '+' || storage[read_index] == '-'))
        storage[write_index++] = storage[read_index++];
    if (read_index == size || storage[read_index] < '0' ||
        storage[read_index] > '9')
        return false;
    while (read_index < size && storage[read_index] >= '0' &&
           storage[read_index] <= '9')
        storage[write_index++] = storage[read_index++];
    if (read_index < size && storage[read_index] != 'e' &&
        storage[read_index] != 'E') {
        storage[write_index++] = '.';
        while (read_index < size &&
               (storage[read_index] < '0' || storage[read_index] > '9') &&
               storage[read_index] != 'e' && storage[read_index] != 'E')
            ++read_index;
        if (read_index == size || storage[read_index] < '0' ||
            storage[read_index] > '9')
            return false;
        while (read_index < size && storage[read_index] >= '0' &&
               storage[read_index] <= '9')
            storage[write_index++] = storage[read_index++];
    }
    if (read_index < size &&
        (storage[read_index] == 'e' || storage[read_index] == 'E')) {
        storage[write_index++] = 'e';
        ++read_index;
        if (read_index < size &&
            (storage[read_index] == '+' || storage[read_index] == '-'))
            storage[write_index++] = storage[read_index++];
        if (read_index == size || storage[read_index] < '0' ||
            storage[read_index] > '9')
            return false;
        while (read_index < size && storage[read_index] >= '0' &&
               storage[read_index] <= '9')
            storage[write_index++] = storage[read_index++];
    }
    if (read_index != size) return false;
    storage[write_index] = '\0';
    *in_out_written = (int)write_index;
    return true;
}

static bool scalar_text(
    const scxml_payload_value *value, char storage[64],
    const char **out_data, size_t *out_size) {
    int written;
    if (value == NULL || out_data == NULL || out_size == NULL) return false;
    if (value->kind == SCXML_PAYLOAD_VALUE_STRING) {
        *out_data = value->data.string.data;
        *out_size = value->data.string.size;
        return scxml_chttp_codec_utf8_valid(*out_data, *out_size);
    }
    if (value->kind == SCXML_PAYLOAD_VALUE_BOOL)
        written = snprintf(storage, 64u, "%s",
                           value->data.boolean ? "true" : "false");
    else if (value->kind == SCXML_PAYLOAD_VALUE_SINT)
        written = snprintf(storage, 64u, "%lld",
                           (long long)value->data.sint);
    else if (value->kind == SCXML_PAYLOAD_VALUE_UINT)
        written = snprintf(storage, 64u, "%llu",
                           (unsigned long long)value->data.uint);
    else if (value->kind == SCXML_PAYLOAD_VALUE_FLOAT &&
             isfinite(value->data.number))
        written = snprintf(storage, 64u, "%.17g", value->data.number);
    else
        return false;
    if (written < 0 || written >= 64 ||
        (value->kind == SCXML_PAYLOAD_VALUE_FLOAT &&
         !canonicalize_float_text(storage, &written)))
        return false;
    *out_data = storage;
    *out_size = (size_t)written;
    return true;
}

static bool add_form_field_size(
    size_t *total, bool has_prior,
    const char *name, size_t name_size, size_t name_limit,
    const char *value, size_t value_size, size_t value_limit) {
    size_t encoded_name;
    size_t encoded_value;
    size_t required = *total;
    if (name_size == 0u ||
        !form_encoded_size(name, name_size, name_limit, &encoded_name) ||
        !form_encoded_size(value, value_size, value_limit, &encoded_value) ||
        (has_prior && !checked_add(required, 1u, &required)) ||
        !checked_add(required, encoded_name, &required) ||
        !checked_add(required, 1u, &required) ||
        !checked_add(required, encoded_value, &required))
        return false;
    *total = required;
    return true;
}

static char *write_form_field(
    char *cursor, bool has_prior,
    const char *name, size_t name_size,
    const char *value, size_t value_size) {
    if (has_prior) *cursor++ = '&';
    cursor = form_encode(cursor, name, name_size);
    *cursor++ = '=';
    return form_encode(cursor, value, value_size);
}

scxml_adapter_status scxml_chttp_codec_encode(
    const scxml_send_request *request,
    const scxml_chttp_codec_limits *limits,
    char *body, size_t body_capacity,
    size_t *out_required_body_size,
    scxml_chttp_encoded_body *out) {
    size_t required = 0u;
    size_t field_count = 0u;
    size_t index;
    char *cursor;
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (out_required_body_size != NULL) *out_required_body_size = 0u;
    if (request == NULL || limits == NULL ||
        out_required_body_size == NULL || out == NULL ||
        (body_capacity != 0u && body == NULL))
        return SCXML_ADAPTER_INVALID_CONTRACT;

    if (request->payload.kind == SCXML_PAYLOAD_CONTENT) {
        const scxml_content_view *content = &request->payload.content;
        const char *media_type;
        size_t media_type_size;
        const char *data;
        size_t size;
        char scalar_storage[64];
        if (content->kind == SCXML_CONTENT_SCALAR) {
            if (!scalar_text(&content->scalar, scalar_storage, &data, &size))
                return SCXML_ADAPTER_ERROR_EXECUTION;
            media_type = TEXT_MEDIA_TYPE;
            media_type_size = sizeof(TEXT_MEDIA_TYPE) - 1u;
        } else if (content->kind == SCXML_CONTENT_TEXT_UTF8 ||
                   content->kind == SCXML_CONTENT_XML_UTF8) {
            data = content->bytes;
            size = content->byte_count;
            if (!scxml_chttp_codec_utf8_valid(data, size))
                return SCXML_ADAPTER_ERROR_EXECUTION;
            media_type = content->kind == SCXML_CONTENT_XML_UTF8
                ? XML_MEDIA_TYPE : TEXT_MEDIA_TYPE;
            media_type_size = content->kind == SCXML_CONTENT_XML_UTF8
                ? sizeof(XML_MEDIA_TYPE) - 1u
                : sizeof(TEXT_MEDIA_TYPE) - 1u;
        } else {
            return SCXML_ADAPTER_ERROR_EXECUTION;
        }
        *out_required_body_size = size;
        if (size > limits->max_encoded_body_bytes)
            return SCXML_ADAPTER_ERROR_EXECUTION;
        if (size > body_capacity || (size != 0u && body == NULL)) {
            memset(out, 0, sizeof(*out));
            return SCXML_ADAPTER_FULL;
        }
        if (size != 0u) memcpy(body, data, size);
        out->media_type = media_type;
        out->media_type_size = media_type_size;
        out->body_size = size;
        return SCXML_ADAPTER_ACCEPTED;
    }

    if (request->payload.kind != SCXML_PAYLOAD_NONE &&
        request->payload.kind != SCXML_PAYLOAD_NAMED)
        return SCXML_ADAPTER_ERROR_EXECUTION;
    if (request->event_size != 0u) {
        if (limits->max_form_entry_count == 0u ||
            !add_form_field_size(
                &required, false,
                RESERVED_EVENT_NAME, sizeof(RESERVED_EVENT_NAME) - 1u,
                sizeof(RESERVED_EVENT_NAME) - 1u,
                request->event, request->event_size,
                limits->max_event_name_bytes))
            return SCXML_ADAPTER_ERROR_EXECUTION;
        field_count = 1u;
    } else if (request->event != NULL) {
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    if (request->payload.kind == SCXML_PAYLOAD_NAMED) {
        if ((request->payload.entry_count != 0u &&
             request->payload.entries == NULL) ||
            request->payload.entry_count >
                limits->max_form_entry_count - field_count)
            return SCXML_ADAPTER_ERROR_EXECUTION;
        for (index = 0u; index < request->payload.entry_count; ++index) {
            const scxml_payload_entry *entry = &request->payload.entries[index];
            const char *value;
            size_t value_size;
            char scalar_storage[64];
            if ((entry->name_size == sizeof(RESERVED_EVENT_NAME) - 1u &&
                 entry->name != NULL &&
                 memcmp(entry->name, RESERVED_EVENT_NAME,
                        entry->name_size) == 0) ||
                entry->value.kind != SCXML_CONTENT_SCALAR ||
                !scalar_text(
                    &entry->value.scalar, scalar_storage, &value, &value_size) ||
                !add_form_field_size(
                    &required, field_count != 0u,
                    entry->name, entry->name_size,
                    limits->max_form_name_bytes,
                    value, value_size, limits->max_form_value_bytes))
                return SCXML_ADAPTER_ERROR_EXECUTION;
            ++field_count;
        }
    }
    *out_required_body_size = required;
    if (required > limits->max_encoded_body_bytes)
        return SCXML_ADAPTER_ERROR_EXECUTION;
    if (required > body_capacity || (required != 0u && body == NULL))
        return SCXML_ADAPTER_FULL;

    cursor = body;
    field_count = 0u;
    if (request->event_size != 0u) {
        cursor = write_form_field(
            cursor, false,
            RESERVED_EVENT_NAME, sizeof(RESERVED_EVENT_NAME) - 1u,
            request->event, request->event_size);
        field_count = 1u;
    }
    if (request->payload.kind == SCXML_PAYLOAD_NAMED) {
        for (index = 0u; index < request->payload.entry_count; ++index) {
            const scxml_payload_entry *entry = &request->payload.entries[index];
            const char *value;
            size_t value_size;
            char scalar_storage[64];
            if (!scalar_text(
                    &entry->value.scalar, scalar_storage, &value, &value_size))
                return SCXML_ADAPTER_INVALID_CONTRACT;
            cursor = write_form_field(
                cursor, field_count != 0u,
                entry->name, entry->name_size, value, value_size);
            ++field_count;
        }
    }
    out->media_type = FORM_MEDIA_TYPE;
    out->media_type_size = sizeof(FORM_MEDIA_TYPE) - 1u;
    out->body_size = required;
    return SCXML_ADAPTER_ACCEPTED;
}

static int hex_value(unsigned char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static bool decode_component(
    const char *source, size_t source_size,
    char *destination, size_t *out_size) {
    size_t source_index;
    size_t destination_index = 0u;
    for (source_index = 0u; source_index < source_size; ++source_index) {
        unsigned char value = (unsigned char)source[source_index];
        if (value == '+') {
            value = ' ';
        } else if (value == '%') {
            int high;
            int low;
            if (source_size - source_index < 3u ||
                (high = hex_value((unsigned char)source[source_index + 1u])) < 0 ||
                (low = hex_value((unsigned char)source[source_index + 2u])) < 0)
                return false;
            value = (unsigned char)((high << 4u) | low);
            source_index += 2u;
        }
        if (value == 0u) return false;
        destination[destination_index++] = (char)value;
    }
    if (!scxml_chttp_codec_utf8_valid(destination, destination_index))
        return false;
    *out_size = destination_index;
    return true;
}

scxml_chttp_decode_status scxml_chttp_codec_decode_form(
    const void *body, size_t body_size,
    const scxml_chttp_codec_limits *limits,
    scxml_chttp_form_entry_view *entry_storage,
    size_t entry_capacity,
    char *text_storage, size_t text_capacity,
    size_t *out_required_text_size,
    scxml_chttp_decoded_form *out) {
    const char *source = (const char *)body;
    size_t cursor = 0u;
    size_t text_used = 0u;
    size_t total_entries = 0u;
    size_t application_entries = 0u;
    const char *event = DEFAULT_EVENT_NAME;
    size_t event_size = sizeof(DEFAULT_EVENT_NAME) - 1u;
    bool has_reserved_event = false;
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (out_required_text_size != NULL) *out_required_text_size = body_size;
    if (limits == NULL || out_required_text_size == NULL ||
        out == NULL || (body_size != 0u && body == NULL) ||
        (entry_capacity != 0u && entry_storage == NULL) ||
        (text_capacity != 0u && text_storage == NULL))
        return SCXML_CHTTP_DECODE_FAILED;
    if (body_size > limits->max_encoded_body_bytes ||
        body_size > text_capacity ||
        (body_size != 0u && text_storage == NULL))
        return SCXML_CHTTP_DECODE_LIMIT_EXCEEDED;
    while (cursor < body_size) {
        size_t field_end = cursor;
        size_t equals;
        size_t name_size;
        size_t value_size;
        char *name;
        char *value;
        while (field_end < body_size && source[field_end] != '&') ++field_end;
        if (field_end == cursor) return SCXML_CHTTP_DECODE_BAD_REQUEST;
        if (++total_entries > limits->max_form_entry_count)
            return SCXML_CHTTP_DECODE_LIMIT_EXCEEDED;
        equals = cursor;
        while (equals < field_end && source[equals] != '=') ++equals;
        name = text_storage + text_used;
        if (!decode_component(source + cursor, equals - cursor,
                              name, &name_size) || name_size == 0u)
            return SCXML_CHTTP_DECODE_BAD_REQUEST;
        text_used += name_size;
        value = text_storage + text_used;
        if (!decode_component(
                equals < field_end ? source + equals + 1u : source + field_end,
                equals < field_end ? field_end - equals - 1u : 0u,
                value, &value_size))
            return SCXML_CHTTP_DECODE_BAD_REQUEST;
        text_used += value_size;
        if (name_size == sizeof(RESERVED_EVENT_NAME) - 1u &&
            memcmp(name, RESERVED_EVENT_NAME, name_size) == 0) {
            if (has_reserved_event || value_size == 0u)
                return SCXML_CHTTP_DECODE_BAD_REQUEST;
            if (value_size > limits->max_event_name_bytes)
                return SCXML_CHTTP_DECODE_LIMIT_EXCEEDED;
            has_reserved_event = true;
            event = value;
            event_size = value_size;
        } else {
            if (name_size > limits->max_form_name_bytes ||
                value_size > limits->max_form_value_bytes ||
                application_entries >= entry_capacity)
                return SCXML_CHTTP_DECODE_LIMIT_EXCEEDED;
            entry_storage[application_entries++] =
                (scxml_chttp_form_entry_view){
                    name, name_size, value, value_size};
        }
        cursor = field_end < body_size ? field_end + 1u : field_end;
        if (field_end < body_size && cursor == body_size)
            return SCXML_CHTTP_DECODE_BAD_REQUEST;
    }
    out->event = event;
    out->event_size = event_size;
    out->entries = entry_storage;
    out->entry_count = application_entries;
    return SCXML_CHTTP_DECODE_OK;
}
