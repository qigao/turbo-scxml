#include "voicexml_internal.h"
#include "voicexml_allocator.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VXML_NAMESPACE "http://www.w3.org/2001/vxml"

typedef struct vxml_decoded_id {
    char *data;
    size_t size;
} vxml_decoded_id;

typedef struct vxml_measurement {
    size_t form_count;
    size_t block_count;
    size_t action_count;
    size_t name_bytes;
    vxml_decoded_id *ids;
    size_t id_count;
    size_t id_capacity;
} vxml_measurement;

typedef struct vxml_writer {
    vxml_program_impl *impl;
    const vxml_measurement *measurement;
    size_t form_index;
    size_t block_index;
    size_t action_index;
    size_t storage_index;
} vxml_writer;

static vxml_status fail(
    vxml_diagnostic *diagnostic, vxml_status status,
    salts_xml_location location, const char *message) {
    if (diagnostic != NULL) {
        diagnostic->status = status;
        diagnostic->location = location;
        if (message == NULL) message = "VoiceXML error";
        (void)snprintf(
            diagnostic->message, sizeof(diagnostic->message), "%s", message);
    }
    return status;
}

static bool checked_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) return false;
    *out = left + right;
    return true;
}

static bool checked_multiply(size_t left, size_t right, size_t *out) {
    if (out == NULL || (right != 0u && left > SIZE_MAX / right)) return false;
    *out = left * right;
    return true;
}

static bool checked_align(size_t value, size_t alignment, size_t *out) {
    const size_t remainder = value % alignment;
    return remainder == 0u
        ? (*out = value, true)
        : checked_add(value, alignment - remainder, out);
}

static bool view_equal(salts_xml_string_view view, const char *text) {
    const size_t size = text != NULL ? strlen(text) : 0u;
    return view.data != NULL && view.size == size &&
           memcmp(view.data, text, size) == 0;
}

static bool decode_utf8(
    const char *data, size_t size, size_t *cursor, uint32_t *out_codepoint) {
    const size_t start = *cursor;
    const unsigned char lead = (unsigned char)data[start];
    size_t width;
    size_t index;
    uint32_t value;
    if (lead <= 0x7fu) {
        *out_codepoint = lead;
        *cursor = start + 1u;
        return true;
    }
    if (lead >= 0xc2u && lead <= 0xdfu) {
        width = 2u;
        value = lead & 0x1fu;
    } else if (lead >= 0xe0u && lead <= 0xefu) {
        width = 3u;
        value = lead & 0x0fu;
    } else if (lead >= 0xf0u && lead <= 0xf4u) {
        width = 4u;
        value = lead & 0x07u;
    } else {
        return false;
    }
    if (width > size - start) return false;
    for (index = 1u; index < width; ++index) {
        const unsigned char continuation = (unsigned char)data[start + index];
        if ((continuation & 0xc0u) != 0x80u) return false;
        value = (value << 6u) | (continuation & 0x3fu);
    }
    if ((width == 3u && value < 0x800u) ||
        (width == 4u && value < 0x10000u) ||
        (value >= 0xd800u && value <= 0xdfffu) || value > 0x10ffffu)
        return false;
    *out_codepoint = value;
    *cursor = start + width;
    return true;
}

static bool is_xml_character(uint32_t codepoint) {
    return codepoint == 0x9u || codepoint == 0xau || codepoint == 0xdu ||
           (codepoint >= 0x20u && codepoint <= 0xd7ffu) ||
           (codepoint >= 0xe000u && codepoint <= 0xfffdu) ||
           (codepoint >= 0x10000u && codepoint <= 0x10ffffu);
}

static salts_xml_location input_location_at(
    const char *input, size_t offset) {
    salts_xml_location location = {offset, 1u, 1u};
    size_t index;
    for (index = 0u; index < offset; ++index) {
        if (input[index] == '\n') {
            if (location.line != UINT32_MAX) ++location.line;
            location.column = 1u;
        } else if (location.column != UINT32_MAX) {
            ++location.column;
        }
    }
    return location;
}

static vxml_status validate_xml_characters(
    const char *input, size_t size, vxml_diagnostic *diagnostic) {
    salts_xml_location location = {0u, 1u, 1u};
    size_t cursor = 0u;
    while (cursor < size) {
        const size_t start = cursor;
        const salts_xml_location codepoint_location = location;
        uint32_t codepoint;
        if (!decode_utf8(input, size, &cursor, &codepoint)) {
            return fail(
                diagnostic, VXML_XML_ERROR, codepoint_location,
                "VoiceXML input contains malformed UTF-8");
        }
        if (!is_xml_character(codepoint)) {
            return fail(
                diagnostic, VXML_XML_ERROR, codepoint_location,
                "VoiceXML input contains an invalid XML character");
        }
        location.byte_offset = cursor;
        if (codepoint == 0xau) {
            if (location.line != UINT32_MAX) ++location.line;
            location.column = 1u;
        } else {
            const size_t width = cursor - start;
            if (width >= UINT32_MAX ||
                location.column > UINT32_MAX - (uint32_t)width)
                location.column = UINT32_MAX;
            else
                location.column += (uint32_t)width;
        }
    }
    return VXML_OK;
}

static bool ncname_start(uint32_t codepoint) {
    return codepoint == '_' || (codepoint >= 'A' && codepoint <= 'Z') ||
           (codepoint >= 'a' && codepoint <= 'z') ||
           (codepoint >= 0xc0u && codepoint <= 0xd6u) ||
           (codepoint >= 0xd8u && codepoint <= 0xf6u) ||
           (codepoint >= 0xf8u && codepoint <= 0x2ffu) ||
           (codepoint >= 0x370u && codepoint <= 0x37du) ||
           (codepoint >= 0x37fu && codepoint <= 0x1fffu) ||
           (codepoint >= 0x200cu && codepoint <= 0x200du) ||
           (codepoint >= 0x2070u && codepoint <= 0x218fu) ||
           (codepoint >= 0x2c00u && codepoint <= 0x2fefu) ||
           (codepoint >= 0x3001u && codepoint <= 0xd7ffu) ||
           (codepoint >= 0xf900u && codepoint <= 0xfdcfu) ||
           (codepoint >= 0xfdf0u && codepoint <= 0xfffdu) ||
           (codepoint >= 0x10000u && codepoint <= 0xeffffu);
}

static bool ncname_continue(uint32_t codepoint) {
    return ncname_start(codepoint) || codepoint == '-' || codepoint == '.' ||
           (codepoint >= '0' && codepoint <= '9') || codepoint == 0xb7u ||
           (codepoint >= 0x300u && codepoint <= 0x36fu) ||
           (codepoint >= 0x203fu && codepoint <= 0x2040u);
}

static bool is_ncname(const char *data, size_t size) {
    size_t cursor = 0u;
    uint32_t codepoint;
    if (data == NULL || size == 0u ||
        !decode_utf8(data, size, &cursor, &codepoint) ||
        !ncname_start(codepoint))
        return false;
    while (cursor < size) {
        if (!decode_utf8(data, size, &cursor, &codepoint) ||
            !ncname_continue(codepoint))
            return false;
    }
    return true;
}

static bool append_utf8(
    char *output, size_t capacity, size_t *size, uint32_t codepoint) {
    size_t required;
    if (!is_xml_character(codepoint)) return false;
    required = codepoint <= 0x7fu ? 1u
             : codepoint <= 0x7ffu ? 2u
             : codepoint <= 0xffffu ? 3u : 4u;
    if (*size > SIZE_MAX - required) return false;
    if (output != NULL && (*size > capacity || required > capacity - *size))
        return false;
    if (output == NULL) {
        *size += required;
    } else if (required == 1u) {
        output[(*size)++] = (char)codepoint;
    } else if (required == 2u) {
        output[(*size)++] = (char)(0xc0u | (codepoint >> 6u));
        output[(*size)++] = (char)(0x80u | (codepoint & 0x3fu));
    } else if (required == 3u) {
        output[(*size)++] = (char)(0xe0u | (codepoint >> 12u));
        output[(*size)++] = (char)(0x80u | ((codepoint >> 6u) & 0x3fu));
        output[(*size)++] = (char)(0x80u | (codepoint & 0x3fu));
    } else {
        output[(*size)++] = (char)(0xf0u | (codepoint >> 18u));
        output[(*size)++] = (char)(0x80u | ((codepoint >> 12u) & 0x3fu));
        output[(*size)++] = (char)(0x80u | ((codepoint >> 6u) & 0x3fu));
        output[(*size)++] = (char)(0x80u | (codepoint & 0x3fu));
    }
    return true;
}

static bool decode_reference(
    salts_xml_string_view input, size_t *cursor, uint32_t *out_codepoint) {
    const size_t start = *cursor;
    size_t consumed = 0u;
    uint32_t codepoint = 0u;
    if (input.size - start >= 4u &&
        memcmp(input.data + start, "&lt;", 4u) == 0) {
        codepoint = '<';
        consumed = 4u;
    } else if (input.size - start >= 4u &&
               memcmp(input.data + start, "&gt;", 4u) == 0) {
        codepoint = '>';
        consumed = 4u;
    } else if (input.size - start >= 5u &&
               memcmp(input.data + start, "&amp;", 5u) == 0) {
        codepoint = '&';
        consumed = 5u;
    } else if (input.size - start >= 6u &&
               memcmp(input.data + start, "&quot;", 6u) == 0) {
        codepoint = '"';
        consumed = 6u;
    } else if (input.size - start >= 6u &&
               memcmp(input.data + start, "&apos;", 6u) == 0) {
        codepoint = '\'';
        consumed = 6u;
    }
    if (consumed != 0u) {
        *cursor = start + consumed;
        *out_codepoint = codepoint;
        return true;
    }
    if (input.size - start >= 4u && input.data[start + 1u] == '#') {
        const bool hexadecimal =
            start + 2u < input.size &&
            (input.data[start + 2u] == 'x' ||
             input.data[start + 2u] == 'X');
        const uint32_t base = hexadecimal ? 16u : 10u;
        size_t end = start + (hexadecimal ? 3u : 2u);
        bool has_digit = false;
        while (end < input.size && input.data[end] != ';') {
            const unsigned char ch = (unsigned char)input.data[end];
            uint32_t digit;
            if (ch >= '0' && ch <= '9') digit = ch - '0';
            else if (hexadecimal && ch >= 'a' && ch <= 'f')
                digit = 10u + ch - 'a';
            else if (hexadecimal && ch >= 'A' && ch <= 'F')
                digit = 10u + ch - 'A';
            else return false;
            if (codepoint > (0x10ffffu - digit) / base) return false;
            codepoint = codepoint * base + digit;
            has_digit = true;
            ++end;
        }
        if (!has_digit || end >= input.size || input.data[end] != ';' ||
            !is_xml_character(codepoint))
            return false;
        *cursor = end + 1u;
        *out_codepoint = codepoint;
        return true;
    }
    return false;
}

static bool next_normalized_codepoint(
    salts_xml_string_view input, size_t *cursor, uint32_t *out_codepoint) {
    if (*cursor >= input.size) return false;
    if (input.data[*cursor] == '&')
        return decode_reference(input, cursor, out_codepoint);
    return decode_utf8(input.data, input.size, cursor, out_codepoint) &&
           is_xml_character(*out_codepoint);
}

static bool decode_entities(
    salts_xml_string_view input, char *output, size_t capacity,
    size_t *out_size) {
    size_t cursor = 0u;
    size_t decoded_size = 0u;
    while (cursor < input.size) {
        uint32_t codepoint;
        if (!next_normalized_codepoint(input, &cursor, &codepoint) ||
            !append_utf8(output, capacity, &decoded_size, codepoint))
            return false;
    }
    *out_size = decoded_size;
    return true;
}

static bool normalized_view_equal(
    salts_xml_string_view view, const char *text) {
    const size_t text_size = text != NULL ? strlen(text) : 0u;
    size_t cursor = 0u;
    size_t text_index = 0u;
    while (cursor < view.size) {
        char encoded[4];
        size_t encoded_size = 0u;
        uint32_t codepoint;
        if (!next_normalized_codepoint(view, &cursor, &codepoint) ||
            !append_utf8(
                encoded, sizeof(encoded), &encoded_size, codepoint) ||
            text_index > text_size || encoded_size > text_size - text_index ||
            memcmp(text + text_index, encoded, encoded_size) != 0)
            return false;
        text_index += encoded_size;
    }
    return text_index == text_size;
}

static bool text_is_whitespace(salts_xml_string_view view) {
    size_t cursor = 0u;
    while (cursor < view.size) {
        uint32_t codepoint;
        if (!next_normalized_codepoint(view, &cursor, &codepoint) ||
            (codepoint != 0x20u && codepoint != 0x9u &&
             codepoint != 0xau && codepoint != 0xdu))
            return false;
    }
    return true;
}

static bool node_is_ignorable(salts_xml_node node) {
    const salts_xml_node_kind kind = salts_xml_node_type(node);
    return kind == SALTS_XML_COMMENT ||
           kind == SALTS_XML_PROCESSING_INSTRUCTION ||
           (kind == SALTS_XML_TEXT &&
            text_is_whitespace(salts_xml_node_value(node)));
}

static bool node_is_known_profile_element(salts_xml_node node) {
    const salts_xml_string_view local_name =
        salts_xml_node_local_name(node);
    return salts_xml_node_type(node) == SALTS_XML_ELEMENT &&
           normalized_view_equal(
               salts_xml_node_namespace_uri(node), VXML_NAMESPACE) &&
           (view_equal(local_name, "vxml") ||
            view_equal(local_name, "form") ||
            view_equal(local_name, "block") ||
            view_equal(local_name, "exit"));
}

static salts_xml_attribute unqualified_attribute(
    salts_xml_node node, const char *name) {
    size_t index;
    for (index = 0u; index < salts_xml_node_attribute_count(node); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(node, index);
        if (salts_xml_attribute_namespace_uri(attribute).size == 0u &&
            view_equal(salts_xml_attribute_local_name(attribute), name))
            return attribute;
    }
    return (salts_xml_attribute){0};
}

static vxml_status validate_attributes(
    salts_xml_node node, const char *allowed,
    vxml_diagnostic *diagnostic) {
    size_t index;
    bool seen = false;
    for (index = 0u; index < salts_xml_node_attribute_count(node); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(node, index);
        if (salts_xml_attribute_namespace_uri(attribute).size != 0u ||
            allowed == NULL ||
            !view_equal(salts_xml_attribute_local_name(attribute), allowed) ||
            seen) {
            return fail(
                diagnostic, VXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported or duplicate VoiceXML attribute");
        }
        seen = true;
    }
    return VXML_OK;
}

static void measurement_destroy(vxml_measurement *measurement) {
    size_t index;
    if (measurement == NULL) return;
    for (index = 0u; index < measurement->id_count; ++index)
        vxml_free(measurement->ids[index].data);
    vxml_free(measurement->ids);
    memset(measurement, 0, sizeof(*measurement));
}

static vxml_status append_id(
    vxml_measurement *measurement, salts_xml_attribute attribute,
    const vxml_limits *limits, vxml_diagnostic *diagnostic) {
    vxml_decoded_id id = {0};
    size_t retained_size;
    size_t index;
    if (attribute.impl != NULL) {
        const salts_xml_string_view raw = salts_xml_attribute_value(attribute);
        if (!decode_entities(raw, NULL, 0u, &id.size) ||
            !checked_add(id.size, 1u, &retained_size)) {
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(attribute),
                "VoiceXML form id has an invalid XML entity reference");
        }
        id.data = (char *)vxml_malloc(retained_size);
        if (id.data == NULL) {
            return fail(
                diagnostic, VXML_ALLOCATION_FAILED,
                salts_xml_attribute_location(attribute),
                "VoiceXML form id decoding allocation failed");
        }
        if (!decode_entities(raw, id.data, id.size, &id.size)) {
            vxml_free(id.data);
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(attribute),
                "VoiceXML form id decoding changed between passes");
        }
        id.data[id.size] = '\0';
        if (!is_ncname(id.data, id.size)) {
            vxml_free(id.data);
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(attribute),
                "VoiceXML form id must be a nonempty XML NCName");
        }
        for (index = 0u; index < measurement->id_count; ++index) {
            const vxml_decoded_id previous = measurement->ids[index];
            if (previous.data != NULL && previous.size == id.size &&
                memcmp(previous.data, id.data, id.size) == 0) {
                vxml_free(id.data);
                return fail(
                    diagnostic, VXML_DUPLICATE_ID,
                    salts_xml_attribute_location(attribute),
                    "duplicate VoiceXML form id");
            }
        }
        if (!checked_add(measurement->name_bytes, retained_size,
                         &measurement->name_bytes) ||
            measurement->name_bytes > limits->max_name_bytes) {
            vxml_free(id.data);
            return fail(
                diagnostic, VXML_LIMIT_EXCEEDED,
                salts_xml_attribute_location(attribute),
                "VoiceXML retained form id bytes exceed max_name_bytes");
        }
    }
    if (measurement->id_count == measurement->id_capacity) {
        size_t capacity = measurement->id_capacity == 0u
            ? 4u : measurement->id_capacity * 2u;
        size_t allocation_size;
        vxml_decoded_id *ids;
        if (capacity < measurement->id_capacity ||
            capacity > limits->max_forms)
            capacity = limits->max_forms;
        if (capacity <= measurement->id_capacity ||
            !checked_multiply(capacity, sizeof(*ids), &allocation_size)) {
            vxml_free(id.data);
            return fail(
                diagnostic, VXML_LIMIT_EXCEEDED,
                attribute.impl != NULL
                    ? salts_xml_attribute_location(attribute)
                    : (salts_xml_location){0},
                "VoiceXML temporary form id table size overflow");
        }
        ids = (vxml_decoded_id *)vxml_realloc(
            measurement->ids, allocation_size);
        if (ids == NULL) {
            vxml_free(id.data);
            return fail(
                diagnostic, VXML_ALLOCATION_FAILED,
                attribute.impl != NULL
                    ? salts_xml_attribute_location(attribute)
                    : (salts_xml_location){0},
                "VoiceXML temporary form id table allocation failed");
        }
        measurement->ids = ids;
        measurement->id_capacity = capacity;
    }
    measurement->ids[measurement->id_count++] = id;
    return VXML_OK;
}

static vxml_status reject_non_element(
    salts_xml_node node, vxml_diagnostic *diagnostic) {
    return fail(
        diagnostic,
        salts_xml_node_type(node) == SALTS_XML_TEXT
            ? VXML_INVALID_STRUCTURE : VXML_UNSUPPORTED_FEATURE,
        salts_xml_node_location(node),
        salts_xml_node_type(node) == SALTS_XML_TEXT
            ? "non-whitespace text is invalid in the VoiceXML MVP profile"
            : "unsupported VoiceXML content");
}

static vxml_status reject_unexpected_element(
    salts_xml_node node, vxml_diagnostic *diagnostic,
    const char *unsupported_message) {
    const bool known = node_is_known_profile_element(node);
    return fail(
        diagnostic,
        known ? VXML_INVALID_STRUCTURE : VXML_UNSUPPORTED_FEATURE,
        salts_xml_node_location(node),
        known ? "VoiceXML profile element is in an invalid position"
              : unsupported_message);
}

static vxml_status measure_exit(
    salts_xml_node node, vxml_measurement *measurement,
    const vxml_limits *limits, vxml_diagnostic *diagnostic) {
    size_t index;
    vxml_status status = validate_attributes(node, NULL, diagnostic);
    if (status != VXML_OK) return status;
    for (index = 0u; index < salts_xml_node_child_count(node); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(node, index);
        if (node_is_ignorable(child)) continue;
        if (salts_xml_node_type(child) == SALTS_XML_ELEMENT)
            return reject_unexpected_element(
                child, diagnostic, "unsupported VoiceXML exit child element");
        return reject_non_element(child, diagnostic);
    }
    if (measurement->action_count >= limits->max_actions ||
        !checked_add(measurement->action_count, 1u,
                     &measurement->action_count)) {
        return fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(node),
            "VoiceXML action count exceeds max_actions");
    }
    return VXML_OK;
}

static vxml_status measure_block(
    salts_xml_node node, vxml_measurement *measurement,
    const vxml_limits *limits, vxml_diagnostic *diagnostic) {
    size_t index;
    size_t exits = 0u;
    vxml_status status = validate_attributes(node, NULL, diagnostic);
    if (status != VXML_OK) return status;
    if (measurement->block_count >= limits->max_blocks ||
        !checked_add(measurement->block_count, 1u,
                     &measurement->block_count)) {
        return fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(node),
            "VoiceXML block count exceeds max_blocks");
    }
    for (index = 0u; index < salts_xml_node_child_count(node); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(node, index);
        if (node_is_ignorable(child)) continue;
        if (salts_xml_node_type(child) != SALTS_XML_ELEMENT)
            return reject_non_element(child, diagnostic);
        if (!normalized_view_equal(
                salts_xml_node_namespace_uri(child), VXML_NAMESPACE) ||
            !view_equal(salts_xml_node_local_name(child), "exit")) {
            return reject_unexpected_element(
                child, diagnostic, "unsupported VoiceXML block child element");
        }
        if (exits != 0u) {
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_node_location(child),
                "VoiceXML block accepts at most one exit child");
        }
        ++exits;
        status = measure_exit(child, measurement, limits, diagnostic);
        if (status != VXML_OK) return status;
    }
    return VXML_OK;
}

static vxml_status measure_form(
    salts_xml_node node, vxml_measurement *measurement,
    const vxml_limits *limits, vxml_diagnostic *diagnostic) {
    const size_t first_block = measurement->block_count;
    salts_xml_attribute id_attribute;
    size_t index;
    vxml_status status = validate_attributes(node, "id", diagnostic);
    if (status != VXML_OK) return status;
    if (measurement->form_count >= limits->max_forms ||
        !checked_add(measurement->form_count, 1u,
                     &measurement->form_count)) {
        return fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(node),
            "VoiceXML form count exceeds max_forms");
    }
    id_attribute = unqualified_attribute(node, "id");
    status = append_id(measurement, id_attribute, limits, diagnostic);
    if (status != VXML_OK) return status;
    for (index = 0u; index < salts_xml_node_child_count(node); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(node, index);
        if (node_is_ignorable(child)) continue;
        if (salts_xml_node_type(child) != SALTS_XML_ELEMENT)
            return reject_non_element(child, diagnostic);
        if (!normalized_view_equal(
                salts_xml_node_namespace_uri(child), VXML_NAMESPACE) ||
            !view_equal(salts_xml_node_local_name(child), "block")) {
            return reject_unexpected_element(
                child, diagnostic, "unsupported VoiceXML form child element");
        }
        status = measure_block(child, measurement, limits, diagnostic);
        if (status != VXML_OK) return status;
    }
    if (measurement->block_count == first_block) {
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_node_location(node),
            "VoiceXML form requires at least one block");
    }
    return VXML_OK;
}

static vxml_status measure_document(
    salts_xml_node root, vxml_measurement *measurement,
    const vxml_limits *limits, vxml_diagnostic *diagnostic) {
    const salts_xml_attribute version = unqualified_attribute(root, "version");
    size_t index;
    vxml_status status;
    if (salts_xml_node_type(root) != SALTS_XML_ELEMENT ||
        !view_equal(salts_xml_node_local_name(root), "vxml") ||
        !normalized_view_equal(
            salts_xml_node_namespace_uri(root), VXML_NAMESPACE)) {
        return fail(
            diagnostic, VXML_INVALID_NAMESPACE,
            salts_xml_node_location(root),
            "root must be the W3C VoiceXML vxml element");
    }
    status = validate_attributes(root, "version", diagnostic);
    if (status != VXML_OK) return status;
    if (version.impl == NULL ||
        (!normalized_view_equal(
             salts_xml_attribute_value(version), "2.0") &&
         !normalized_view_equal(
             salts_xml_attribute_value(version), "2.1"))) {
        return fail(
            diagnostic, VXML_INVALID_VERSION,
            version.impl != NULL
                ? salts_xml_attribute_location(version)
                : salts_xml_node_location(root),
            "VoiceXML version must be 2.0 or 2.1");
    }
    for (index = 0u; index < salts_xml_node_child_count(root); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(root, index);
        if (node_is_ignorable(child)) continue;
        if (salts_xml_node_type(child) != SALTS_XML_ELEMENT)
            return reject_non_element(child, diagnostic);
        if (!normalized_view_equal(
                salts_xml_node_namespace_uri(child), VXML_NAMESPACE) ||
            !view_equal(salts_xml_node_local_name(child), "form")) {
            return reject_unexpected_element(
                child, diagnostic, "unsupported VoiceXML root child element");
        }
        status = measure_form(child, measurement, limits, diagnostic);
        if (status != VXML_OK) return status;
    }
    if (measurement->form_count == 0u) {
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_node_location(root),
            "VoiceXML document requires at least one form");
    }
    return VXML_OK;
}

static bool measure_allocation(
    const vxml_measurement *measurement, size_t *forms_offset,
    size_t *blocks_offset, size_t *actions_offset,
    size_t *storage_offset, size_t *allocation_size) {
    size_t cursor = sizeof(vxml_program_impl);
    size_t bytes;
    if (!checked_align(cursor, _Alignof(vxml_form_row), &cursor)) return false;
    *forms_offset = cursor;
    if (!checked_multiply(
            measurement->form_count, sizeof(vxml_form_row), &bytes) ||
        !checked_add(cursor, bytes, &cursor) ||
        !checked_align(cursor, _Alignof(vxml_block_row), &cursor))
        return false;
    *blocks_offset = cursor;
    if (!checked_multiply(
            measurement->block_count, sizeof(vxml_block_row), &bytes) ||
        !checked_add(cursor, bytes, &cursor) ||
        !checked_align(cursor, _Alignof(vxml_action_row), &cursor))
        return false;
    *actions_offset = cursor;
    if (!checked_multiply(
            measurement->action_count, sizeof(vxml_action_row), &bytes) ||
        !checked_add(cursor, bytes, &cursor))
        return false;
    *storage_offset = cursor;
    if (!checked_add(cursor, measurement->name_bytes, allocation_size))
        return false;
    return true;
}

static void write_exit(vxml_writer *writer) {
    writer->impl->actions[writer->action_index++].kind = VXML_ACTION_EXIT;
}

static void write_block(vxml_writer *writer, salts_xml_node node) {
    vxml_block_row *row = &writer->impl->blocks[writer->block_index++];
    size_t index;
    row->first_action = writer->action_index;
    for (index = 0u; index < salts_xml_node_child_count(node); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(node, index);
        if (salts_xml_node_type(child) == SALTS_XML_ELEMENT)
            write_exit(writer);
    }
    row->action_count = writer->action_index - row->first_action;
}

static void write_form(vxml_writer *writer, salts_xml_node node) {
    const vxml_decoded_id id =
        writer->measurement->ids[writer->form_index];
    vxml_form_row *row = &writer->impl->forms[writer->form_index++];
    size_t index;
    row->first_block = writer->block_index;
    if (id.data != NULL) {
        row->id = writer->impl->storage + writer->storage_index;
        row->id_size = id.size;
        memcpy(writer->impl->storage + writer->storage_index,
               id.data, id.size + 1u);
        writer->storage_index += id.size + 1u;
    }
    for (index = 0u; index < salts_xml_node_child_count(node); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(node, index);
        if (salts_xml_node_type(child) == SALTS_XML_ELEMENT)
            write_block(writer, child);
    }
    row->block_count = writer->block_index - row->first_block;
}

static void write_document(vxml_writer *writer, salts_xml_node root) {
    size_t index;
    for (index = 0u; index < salts_xml_node_child_count(root); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(root, index);
        if (salts_xml_node_type(child) == SALTS_XML_ELEMENT)
            write_form(writer, child);
    }
}

static vxml_status map_xml_status(salts_xml_status status) {
    switch (status) {
        case SALTS_XML_OK:
            return VXML_OK;
        case SALTS_XML_INVALID_ARGUMENT:
            return VXML_INVALID_ARGUMENT;
        case SALTS_XML_LIMIT_EXCEEDED:
            return VXML_LIMIT_EXCEEDED;
        case SALTS_XML_ALLOCATION_FAILED:
            return VXML_ALLOCATION_FAILED;
        case SALTS_XML_EMBEDDED_NUL:
        case SALTS_XML_MALFORMED:
            return VXML_XML_ERROR;
        case SALTS_XML_UNSUPPORTED:
            return VXML_UNSUPPORTED_FEATURE;
    }
    return VXML_XML_ERROR;
}

vxml_limits vxml_default_limits(void) {
    const vxml_limits limits = {
        salts_xml_default_limits(),
        VXML_DEFAULT_MAX_FORMS,
        VXML_DEFAULT_MAX_BLOCKS,
        VXML_DEFAULT_MAX_ACTIONS,
        VXML_DEFAULT_MAX_NAME_BYTES};
    return limits;
}

vxml_status vxml_compile(const void *bytes, size_t size,
                         const vxml_limits *limits,
                         vxml_program *out,
                         vxml_diagnostic *diagnostic) {
    const vxml_limits active_limits =
        limits != NULL ? *limits : vxml_default_limits();
    salts_xml_document document = {0};
    salts_xml_diagnostic xml_diagnostic = {0};
    vxml_measurement measurement = {0};
    vxml_program_impl *impl = NULL;
    vxml_writer writer = {0};
    vxml_status status;
    salts_xml_status xml_status;
    size_t forms_offset;
    size_t blocks_offset;
    size_t actions_offset;
    size_t storage_offset;
    size_t allocation_size;
    if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
    if (out != NULL) out->impl = NULL;
    if (bytes == NULL || size == 0u || out == NULL ||
        active_limits.xml.max_input_bytes == 0u ||
        active_limits.xml.max_nodes == 0u ||
        active_limits.xml.max_attributes == 0u ||
        active_limits.xml.max_depth == 0u ||
        active_limits.xml.max_retained_string_bytes == 0u ||
        active_limits.max_forms == 0u || active_limits.max_blocks == 0u ||
        active_limits.max_actions == 0u ||
        active_limits.max_name_bytes == 0u) {
        return fail(
            diagnostic, VXML_INVALID_ARGUMENT, (salts_xml_location){0},
            "output, input, and all VoiceXML limits must be valid");
    }
    if (size > active_limits.xml.max_input_bytes) {
        return fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            input_location_at(
                (const char *)bytes, active_limits.xml.max_input_bytes),
            "XML input exceeds max_input_bytes");
    }
    status = validate_xml_characters(
        (const char *)bytes, size, diagnostic);
    if (status != VXML_OK) return status;
    xml_status = salts_xml_parse(
        &document, (const char *)bytes, size, &active_limits.xml,
        &xml_diagnostic);
    if (xml_status != SALTS_XML_OK) {
        return fail(
            diagnostic, map_xml_status(xml_status), xml_diagnostic.location,
            xml_diagnostic.message);
    }
    status = measure_document(
        salts_xml_document_root(&document), &measurement,
        &active_limits, diagnostic);
    if (status != VXML_OK) goto cleanup;
    if (!measure_allocation(
            &measurement, &forms_offset, &blocks_offset, &actions_offset,
            &storage_offset, &allocation_size)) {
        status = fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(salts_xml_document_root(&document)),
            "VoiceXML immutable program allocation size overflow");
        goto cleanup;
    }
    impl = (vxml_program_impl *)vxml_calloc(1u, allocation_size);
    if (impl == NULL) {
        status = fail(
            diagnostic, VXML_ALLOCATION_FAILED,
            salts_xml_node_location(salts_xml_document_root(&document)),
            "VoiceXML immutable program allocation failed");
        goto cleanup;
    }
    impl->forms = (vxml_form_row *)((char *)impl + forms_offset);
    impl->blocks = (vxml_block_row *)((char *)impl + blocks_offset);
    impl->actions = (vxml_action_row *)((char *)impl + actions_offset);
    impl->storage = (char *)impl + storage_offset;
    impl->form_count = measurement.form_count;
    impl->block_count = measurement.block_count;
    impl->action_count = measurement.action_count;
    impl->storage_size = measurement.name_bytes;
    impl->allocation_size = allocation_size;
    writer.impl = impl;
    writer.measurement = &measurement;
    write_document(&writer, salts_xml_document_root(&document));
    if (writer.form_index != measurement.form_count ||
        writer.block_index != measurement.block_count ||
        writer.action_index != measurement.action_count ||
        writer.storage_index != measurement.name_bytes) {
        status = fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_node_location(salts_xml_document_root(&document)),
            "VoiceXML document changed between measurement and build");
        goto cleanup;
    }
    out->impl = impl;
    impl = NULL;
    status = VXML_OK;

cleanup:
    vxml_free(impl);
    measurement_destroy(&measurement);
    salts_xml_document_destroy(&document);
    return status;
}

void vxml_program_destroy(vxml_program *program) {
    if (program == NULL || program->impl == NULL) return;
    vxml_free(program->impl);
    program->impl = NULL;
}
