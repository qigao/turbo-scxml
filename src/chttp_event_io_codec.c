#include "chttp_event_io_internal.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char FORM_CONTENT_TYPE[] =
    "application/x-www-form-urlencoded";
static const char TEXT_CONTENT_TYPE[] = "text/plain; charset=utf-8";
static const char XML_CONTENT_TYPE[] = "application/xml; charset=utf-8";
static const char RESERVED_EVENT_NAME[] = "_scxmleventname";
static const char DEFAULT_EVENT_NAME[] = "HTTP.POST";
static const char HEX_DIGITS[] = "0123456789ABCDEF";

typedef struct codec_writer {
    unsigned char *data;
    size_t capacity;
    size_t size;
} codec_writer;

static bool checked_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) return false;
    *out = left + right;
    return true;
}

static bool utf8_valid(const char *data, size_t size) {
    size_t index = 0u;
    if (data == NULL && size != 0u) return false;
    while (index < size) {
        const unsigned char lead = (unsigned char)data[index++];
        uint32_t codepoint;
        size_t continuation_count;
        size_t continuation;
        if (lead == 0u) return false;
        if (lead <= 0x7fu) continue;
        if (lead >= 0xc2u && lead <= 0xdfu) {
            codepoint = lead & 0x1fu;
            continuation_count = 1u;
        } else if (lead >= 0xe0u && lead <= 0xefu) {
            codepoint = lead & 0x0fu;
            continuation_count = 2u;
        } else if (lead >= 0xf0u && lead <= 0xf4u) {
            codepoint = lead & 0x07u;
            continuation_count = 3u;
        } else {
            return false;
        }
        if (continuation_count > size - index) return false;
        for (continuation = 0u;
             continuation < continuation_count; ++continuation) {
            const unsigned char next = (unsigned char)data[index++];
            if ((next & 0xc0u) != 0x80u) return false;
            codepoint = (codepoint << 6) | (next & 0x3fu);
        }
        if ((continuation_count == 2u && codepoint < 0x800u) ||
            (continuation_count == 3u && codepoint < 0x10000u) ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu) ||
            codepoint > 0x10ffffu)
            return false;
    }
    return true;
}

static bool writer_append_byte(codec_writer *writer, unsigned char value) {
    size_t next;
    if (writer == NULL || !checked_add(writer->size, 1u, &next)) return false;
    if (writer->data != NULL) {
        if (writer->size >= writer->capacity) return false;
        writer->data[writer->size] = value;
    }
    writer->size = next;
    return true;
}

static bool form_unreserved(unsigned char value) {
    return (value >= 'A' && value <= 'Z') ||
        (value >= 'a' && value <= 'z') ||
        (value >= '0' && value <= '9') || value == '-' || value == '.' ||
        value == '_' || value == '~';
}

static bool writer_append_form_component(
    codec_writer *writer, const char *data, size_t size) {
    size_t index;
    if ((data == NULL && size != 0u) || !utf8_valid(data, size)) return false;
    for (index = 0u; index < size; ++index) {
        const unsigned char value = (unsigned char)data[index];
        if (form_unreserved(value)) {
            if (!writer_append_byte(writer, value)) return false;
        } else if (value == ' ') {
            if (!writer_append_byte(writer, '+')) return false;
        } else if (!writer_append_byte(writer, '%') ||
                   !writer_append_byte(writer, HEX_DIGITS[value >> 4]) ||
                   !writer_append_byte(writer, HEX_DIGITS[value & 0x0fu])) {
            return false;
        }
    }
    return true;
}

static bool scalar_text(
    const scxml_payload_value *value, char *buffer, size_t buffer_capacity,
    const char **out_data, size_t *out_size) {
    int written;
    if (value == NULL || buffer == NULL || buffer_capacity == 0u ||
        out_data == NULL || out_size == NULL)
        return false;
    switch (value->kind) {
        case SCXML_PAYLOAD_VALUE_BOOL:
            *out_data = value->data.boolean ? "true" : "false";
            *out_size = value->data.boolean ? 4u : 5u;
            return true;
        case SCXML_PAYLOAD_VALUE_SINT:
            written = snprintf(
                buffer, buffer_capacity, "%" PRId64, value->data.sint);
            break;
        case SCXML_PAYLOAD_VALUE_UINT:
            written = snprintf(
                buffer, buffer_capacity, "%" PRIu64, value->data.uint);
            break;
        case SCXML_PAYLOAD_VALUE_FLOAT:
            if (!isfinite(value->data.number)) return false;
            written = snprintf(
                buffer, buffer_capacity, "%.17g", value->data.number);
            break;
        case SCXML_PAYLOAD_VALUE_STRING:
            if ((value->data.string.data == NULL &&
                 value->data.string.size != 0u) ||
                !utf8_valid(
                    value->data.string.data, value->data.string.size))
                return false;
            *out_data = value->data.string.data != NULL
                ? value->data.string.data : "";
            *out_size = value->data.string.size;
            return true;
        default:
            return false;
    }
    if (written < 0 || (size_t)written >= buffer_capacity) return false;
    *out_data = buffer;
    *out_size = (size_t)written;
    return true;
}

static bool encode_form(
    const scxml_send_request *request, codec_writer *writer) {
    size_t index;
    if (request == NULL || writer == NULL || request->event == NULL ||
        request->event_size == 0u ||
        !writer_append_form_component(
            writer, RESERVED_EVENT_NAME,
            sizeof(RESERVED_EVENT_NAME) - 1u) ||
        !writer_append_byte(writer, '=') ||
        !writer_append_form_component(
            writer, request->event, request->event_size))
        return false;
    if (request->payload.kind == SCXML_PAYLOAD_NONE) return true;
    if (request->payload.kind != SCXML_PAYLOAD_NAMED ||
        (request->payload.entry_count != 0u &&
         request->payload.entries == NULL))
        return false;
    for (index = 0u; index < request->payload.entry_count; ++index) {
        const scxml_payload_entry *entry = &request->payload.entries[index];
        char scalar_buffer[64];
        const char *value;
        size_t value_size;
        if (entry->name == NULL || entry->name_size == 0u ||
            (entry->name_size == sizeof(RESERVED_EVENT_NAME) - 1u &&
             memcmp(entry->name, RESERVED_EVENT_NAME,
                    entry->name_size) == 0) ||
            entry->value.kind != SCXML_CONTENT_SCALAR ||
            !scalar_text(
                &entry->value.scalar, scalar_buffer, sizeof(scalar_buffer),
                &value, &value_size) ||
            !writer_append_byte(writer, '&') ||
            !writer_append_form_component(
                writer, entry->name, entry->name_size) ||
            !writer_append_byte(writer, '=') ||
            !writer_append_form_component(writer, value, value_size))
            return false;
    }
    return true;
}

static scxml_adapter_status copy_content_body(
    const scxml_content_view *content,
    void *out_body, size_t body_capacity, size_t *out_body_size,
    const char **out_content_type, size_t *out_content_type_size) {
    char scalar_buffer[64];
    const char *source = NULL;
    size_t source_size = 0u;
    const char *content_type = TEXT_CONTENT_TYPE;
    size_t content_type_size = sizeof(TEXT_CONTENT_TYPE) - 1u;
    if (content == NULL) return SCXML_ADAPTER_INVALID_CONTRACT;
    if (content->kind == SCXML_CONTENT_SCALAR) {
        if (!scalar_text(
                &content->scalar, scalar_buffer, sizeof(scalar_buffer),
                &source, &source_size))
            return SCXML_ADAPTER_ERROR_EXECUTION;
    } else if (content->kind == SCXML_CONTENT_TEXT_UTF8 ||
               content->kind == SCXML_CONTENT_XML_UTF8) {
        source = content->bytes;
        source_size = content->byte_count;
        if ((source == NULL && source_size != 0u) ||
            !utf8_valid(source, source_size))
            return SCXML_ADAPTER_ERROR_EXECUTION;
        if (content->kind == SCXML_CONTENT_XML_UTF8) {
            content_type = XML_CONTENT_TYPE;
            content_type_size = sizeof(XML_CONTENT_TYPE) - 1u;
        }
    } else {
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    *out_body_size = source_size;
    *out_content_type = content_type;
    *out_content_type_size = content_type_size;
    if (source_size > body_capacity || (source_size != 0u && out_body == NULL))
        return SCXML_ADAPTER_FULL;
    if (source_size != 0u) memcpy(out_body, source, source_size);
    return SCXML_ADAPTER_ACCEPTED;
}

scxml_adapter_status scxml_chttp_encode_send_body(
    const scxml_send_request *request,
    void *out_body, size_t body_capacity, size_t *out_body_size,
    const char **out_content_type, size_t *out_content_type_size) {
    codec_writer measured = {0};
    codec_writer writer;
    if (out_body_size == NULL || out_content_type == NULL ||
        out_content_type_size == NULL || request == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    *out_body_size = 0u;
    *out_content_type = NULL;
    *out_content_type_size = 0u;
    if (request->payload.kind == SCXML_PAYLOAD_CONTENT)
        return copy_content_body(
            &request->payload.content, out_body, body_capacity,
            out_body_size, out_content_type, out_content_type_size);
    if (!encode_form(request, &measured))
        return SCXML_ADAPTER_ERROR_EXECUTION;
    *out_body_size = measured.size;
    *out_content_type = FORM_CONTENT_TYPE;
    *out_content_type_size = sizeof(FORM_CONTENT_TYPE) - 1u;
    if (measured.size > body_capacity ||
        (measured.size != 0u && out_body == NULL))
        return SCXML_ADAPTER_FULL;
    writer = (codec_writer){
        (unsigned char *)out_body, body_capacity, 0u};
    if (!encode_form(request, &writer) || writer.size != measured.size)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    return SCXML_ADAPTER_ACCEPTED;
}

static int hex_value(unsigned char value) {
    if (value >= '0' && value <= '9') return (int)(value - '0');
    if (value >= 'A' && value <= 'F') return (int)(value - 'A') + 10;
    if (value >= 'a' && value <= 'f') return (int)(value - 'a') + 10;
    return -1;
}

static scxml_chttp_decode_status decoded_component_equals(
    const unsigned char *source, size_t source_size,
    const char *literal, size_t literal_size, int *out_equal) {
    size_t source_index;
    size_t decoded_size = 0u;
    int equal = 1;
    if ((source == NULL && source_size != 0u) || literal == NULL ||
        out_equal == NULL)
        return SCXML_CHTTP_DECODE_FAILED;
    for (source_index = 0u; source_index < source_size; ++source_index) {
        unsigned char value = source[source_index];
        if (value == '%') {
            int high;
            int low;
            if (source_size - source_index < 3u ||
                (high = hex_value(source[source_index + 1u])) < 0 ||
                (low = hex_value(source[source_index + 2u])) < 0)
                return SCXML_CHTTP_DECODE_BAD_REQUEST;
            value = (unsigned char)((high << 4) | low);
            source_index += 2u;
        } else if (value == '+') {
            value = ' ';
        }
        if (value == 0u) return SCXML_CHTTP_DECODE_BAD_REQUEST;
        if (decoded_size >= literal_size ||
            value != (unsigned char)literal[decoded_size])
            equal = 0;
        ++decoded_size;
    }
    *out_equal = equal && decoded_size == literal_size;
    return SCXML_CHTTP_DECODE_OK;
}

static scxml_chttp_decode_status decode_component(
    const unsigned char *source, size_t source_size,
    char *storage, size_t storage_capacity, size_t *storage_index,
    size_t max_decoded_bytes, const char **out_data, size_t *out_size) {
    const size_t begin = storage_index != NULL ? *storage_index : 0u;
    size_t source_index;
    size_t decoded_size = 0u;
    if ((source == NULL && source_size != 0u) || storage == NULL ||
        storage_index == NULL || out_data == NULL || out_size == NULL)
        return SCXML_CHTTP_DECODE_FAILED;
    for (source_index = 0u; source_index < source_size; ++source_index) {
        unsigned char value = source[source_index];
        size_t destination;
        if (value == '%') {
            int high;
            int low;
            if (source_size - source_index < 3u ||
                (high = hex_value(source[source_index + 1u])) < 0 ||
                (low = hex_value(source[source_index + 2u])) < 0)
                return SCXML_CHTTP_DECODE_BAD_REQUEST;
            value = (unsigned char)((high << 4) | low);
            source_index += 2u;
        } else if (value == '+') {
            value = ' ';
        }
        if (value == 0u) return SCXML_CHTTP_DECODE_BAD_REQUEST;
        if (decoded_size >= max_decoded_bytes ||
            !checked_add(begin, decoded_size, &destination) ||
            destination >= storage_capacity)
            return SCXML_CHTTP_DECODE_LIMIT_EXCEEDED;
        storage[destination] = (char)value;
        ++decoded_size;
    }
    if (begin > storage_capacity || decoded_size >= storage_capacity - begin)
        return SCXML_CHTTP_DECODE_LIMIT_EXCEEDED;
    storage[begin + decoded_size] = '\0';
    if (!utf8_valid(storage + begin, decoded_size))
        return SCXML_CHTTP_DECODE_BAD_REQUEST;
    *out_data = storage + begin;
    *out_size = decoded_size;
    *storage_index = begin + decoded_size + 1u;
    return SCXML_CHTTP_DECODE_OK;
}

scxml_chttp_decode_status scxml_chttp_decode_form_body(
    const void *body, size_t body_size,
    char *storage, size_t storage_capacity,
    scxml_chttp_form_entry_view *entries, size_t entry_capacity,
    size_t max_event_name_bytes, size_t max_form_name_bytes,
    size_t max_form_value_bytes, scxml_chttp_decoded_form *out) {
    const unsigned char *bytes = (const unsigned char *)body;
    const char *event_name = NULL;
    size_t event_name_size = 0u;
    size_t storage_index = 0u;
    size_t entry_count = 0u;
    size_t cursor = 0u;
    if (out == NULL) return SCXML_CHTTP_DECODE_FAILED;
    *out = (scxml_chttp_decoded_form){0};
    if ((body == NULL && body_size != 0u) || storage == NULL ||
        (entries == NULL && entry_capacity != 0u) ||
        max_event_name_bytes == 0u || max_form_name_bytes == 0u ||
        max_form_value_bytes == 0u)
        return SCXML_CHTTP_DECODE_FAILED;
    while (cursor < body_size) {
        const size_t segment_begin = cursor;
        size_t segment_end;
        size_t equals;
        const char *name;
        size_t name_size;
        int reserved_name;
        scxml_chttp_decode_status status;
        while (cursor < body_size && bytes[cursor] != '&') ++cursor;
        segment_end = cursor;
        if (segment_end == segment_begin)
            return SCXML_CHTTP_DECODE_BAD_REQUEST;
        equals = segment_begin;
        while (equals < segment_end && bytes[equals] != '=') ++equals;
        status = decoded_component_equals(
            bytes + segment_begin, equals - segment_begin,
            RESERVED_EVENT_NAME, sizeof(RESERVED_EVENT_NAME) - 1u,
            &reserved_name);
        if (status != SCXML_CHTTP_DECODE_OK) return status;
        if (reserved_name) {
            if (event_name != NULL) return SCXML_CHTTP_DECODE_BAD_REQUEST;
            status = decode_component(
                equals < segment_end ? bytes + equals + 1u
                                     : bytes + segment_end,
                equals < segment_end ? segment_end - equals - 1u : 0u,
                storage, storage_capacity, &storage_index,
                max_event_name_bytes, &event_name, &event_name_size);
            if (status != SCXML_CHTTP_DECODE_OK) return status;
            if (event_name_size == 0u)
                return SCXML_CHTTP_DECODE_BAD_REQUEST;
        } else {
            const char *value;
            size_t value_size;
            status = decode_component(
                bytes + segment_begin, equals - segment_begin,
                storage, storage_capacity, &storage_index,
                max_form_name_bytes, &name, &name_size);
            if (status != SCXML_CHTTP_DECODE_OK) return status;
            if (name_size == 0u) return SCXML_CHTTP_DECODE_BAD_REQUEST;
            if (entry_count >= entry_capacity)
                return SCXML_CHTTP_DECODE_LIMIT_EXCEEDED;
            status = decode_component(
                equals < segment_end ? bytes + equals + 1u
                                     : bytes + segment_end,
                equals < segment_end ? segment_end - equals - 1u : 0u,
                storage, storage_capacity, &storage_index,
                max_form_value_bytes, &value, &value_size);
            if (status != SCXML_CHTTP_DECODE_OK) return status;
            entries[entry_count++] = (scxml_chttp_form_entry_view){
                name, name_size, value, value_size};
        }
        if (cursor < body_size) {
            ++cursor;
            if (cursor == body_size)
                return SCXML_CHTTP_DECODE_BAD_REQUEST;
        }
    }
    if (event_name == NULL) {
        if (sizeof(DEFAULT_EVENT_NAME) - 1u > max_event_name_bytes)
            return SCXML_CHTTP_DECODE_LIMIT_EXCEEDED;
        event_name = DEFAULT_EVENT_NAME;
        event_name_size = sizeof(DEFAULT_EVENT_NAME) - 1u;
    }
    *out = (scxml_chttp_decoded_form){
        event_name, event_name_size, entries, entry_count, storage_index};
    return SCXML_CHTTP_DECODE_OK;
}
