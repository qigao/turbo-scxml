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

typedef struct vxml_decoded_goto {
    char *target;
    size_t target_size;
    char *fetchaudio;
    size_t fetchaudio_size;
    size_t target_form;
    salts_xml_location location;
    bool external;
} vxml_decoded_goto;

typedef struct vxml_decoded_submit {
    char *target;
    size_t target_size;
    vxml_submit_method method;
    vxml_submit_enctype enctype;
    salts_xml_location location;
} vxml_decoded_submit;

typedef struct vxml_decoded_script {
    char *src;
    size_t src_size;
    char *srcexpr;
    size_t srcexpr_size;
    char *charset;
    size_t charset_size;
    salts_xml_location location;
} vxml_decoded_script;

typedef struct vxml_decoded_data_policy {
    char *fetchaudio_uri;
    size_t fetchaudio_uri_size;
    bool has_fetchaudio_delay;
    uint64_t fetchaudio_delay_us;
    bool has_fetchaudio_minimum;
    uint64_t fetchaudio_minimum_us;
    bool has_timeout;
    uint64_t timeout_us;
    vxml_cmeta_data_fetch_hint fetch_hint;
    bool has_max_age;
    uint64_t max_age_seconds;
    bool has_max_stale;
    uint64_t max_stale_seconds;
} vxml_decoded_data_policy;

typedef struct vxml_data_property_seen {
    bool fetchaudio;
    bool fetchaudio_delay;
    bool fetchaudio_minimum;
    bool timeout;
    bool hint;
    bool max_age;
    bool max_stale;
} vxml_data_property_seen;

typedef struct vxml_decoded_data {
    vxml_data_placement placement;
    size_t owner_form;
    char *name;
    size_t name_size;
    char *uri;
    size_t uri_size;
    char *uri_expression;
    size_t uri_expression_size;
    vxml_submit_method method;
    vxml_submit_enctype enctype;
    char *namelist;
    size_t namelist_size;
    size_t namelist_count;
    vxml_decoded_data_policy fetch_policy;
    salts_xml_location location;
} vxml_decoded_data;

typedef struct vxml_measurement {
    uint64_t features;
    bool version_21;
    size_t form_count;
    size_t block_count;
    size_t action_count;
    size_t name_bytes;
    vxml_decoded_id *ids;
    size_t id_count;
    size_t id_capacity;
    vxml_decoded_goto *gotos;
    size_t goto_count;
    size_t goto_capacity;
    vxml_decoded_submit *submits;
    size_t submit_count;
    size_t submit_capacity;
    vxml_decoded_script *scripts;
    size_t script_count;
    size_t script_capacity;
    vxml_decoded_data *data_rows;
    size_t data_count;
    size_t data_capacity;
} vxml_measurement;

typedef struct vxml_writer {
    vxml_program_impl *impl;
    const vxml_measurement *measurement;
    size_t form_index;
    size_t block_index;
    size_t action_index;
    size_t goto_index;
    size_t submit_index;
    size_t script_index;
    size_t data_index;
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
        if (input[index] == '\r') {
            if (location.line != UINT32_MAX) ++location.line;
            location.column = 1u;
            if (index + 1u < offset && input[index + 1u] == '\n')
                ++index;
        } else if (input[index] == '\n') {
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
    bool previous_was_cr = false;
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
        if (codepoint == 0xdu) {
            if (location.line != UINT32_MAX) ++location.line;
            location.column = 1u;
            previous_was_cr = true;
        } else if (codepoint == 0xau) {
            if (!previous_was_cr && location.line != UINT32_MAX)
                ++location.line;
            location.column = 1u;
            previous_was_cr = false;
        } else {
            const size_t width = cursor - start;
            previous_was_cr = false;
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

static bool ascii_case_view_equal(
    salts_xml_string_view view, const char *literal) {
    const size_t literal_size =
        literal != NULL ? strlen(literal) : 0u;
    size_t index;
    if (view.data == NULL || view.size != literal_size)
        return false;
    for (index = 0u; index < view.size; ++index) {
        unsigned char left = (unsigned char)view.data[index];
        unsigned char right = (unsigned char)literal[index];
        if (left >= 'A' && left <= 'Z')
            left = (unsigned char)(left + ('a' - 'A'));
        if (right >= 'A' && right <= 'Z')
            right = (unsigned char)(right + ('a' - 'A'));
        if (left != right) return false;
    }
    return true;
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
            view_equal(local_name, "exit") ||
            view_equal(local_name, "goto") ||
            view_equal(local_name, "submit") ||
            view_equal(local_name, "script"));
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

static vxml_status validate_submit_attributes(
    salts_xml_node node, vxml_diagnostic *diagnostic) {
    static const char *const allowed[] = {
        "next", "method", "enctype", "namelist"};
    bool seen[4] = {false, false, false, false};
    size_t index;
    for (index = 0u; index < salts_xml_node_attribute_count(node); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(node, index);
        const salts_xml_string_view local =
            salts_xml_attribute_local_name(attribute);
        size_t allowed_index;
        if (salts_xml_attribute_namespace_uri(attribute).size != 0u)
            return fail(
                diagnostic, VXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported VoiceXML submit attribute");
        for (allowed_index = 0u;
             allowed_index < sizeof(allowed) / sizeof(allowed[0]);
             ++allowed_index)
            if (view_equal(local, allowed[allowed_index]))
                break;
        if (allowed_index == sizeof(allowed) / sizeof(allowed[0]) ||
            seen[allowed_index])
            return fail(
                diagnostic, VXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported or duplicate VoiceXML submit attribute");
        seen[allowed_index] = true;
    }
    return VXML_OK;
}

static vxml_status validate_goto_attributes(
    salts_xml_node node, vxml_diagnostic *diagnostic) {
    size_t index;
    bool seen_next = false;
    bool seen_fetchaudio = false;
    for (index = 0u; index < salts_xml_node_attribute_count(node); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(node, index);
        const salts_xml_string_view local =
            salts_xml_attribute_local_name(attribute);
        bool *seen = NULL;
        if (salts_xml_attribute_namespace_uri(attribute).size == 0u &&
            view_equal(local, "next"))
            seen = &seen_next;
        else if (salts_xml_attribute_namespace_uri(attribute).size == 0u &&
                 view_equal(local, "fetchaudio"))
            seen = &seen_fetchaudio;
        if (seen == NULL || *seen)
            return fail(
                diagnostic, VXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported or duplicate VoiceXML goto attribute");
        *seen = true;
    }
    return VXML_OK;
}


static bool data_feature_enabled(const vxml_measurement *measurement) {
    return measurement != NULL &&
        (measurement->features & VXML_COMPILE_FEATURE_DATA_REQUEST) != 0u;
}

static bool xml_space(unsigned char value) {
    return value == (unsigned char)' ' ||
        value == (unsigned char)'\t' ||
        value == (unsigned char)'\r' ||
        value == (unsigned char)'\n';
}

static void decoded_data_policy_destroy(
    vxml_decoded_data_policy *policy) {
    if (policy == NULL) return;
    vxml_free(policy->fetchaudio_uri);
    memset(policy, 0, sizeof(*policy));
}

static void decoded_data_destroy(vxml_decoded_data *row) {
    if (row == NULL) return;
    vxml_free(row->name);
    vxml_free(row->uri);
    vxml_free(row->uri_expression);
    vxml_free(row->namelist);
    decoded_data_policy_destroy(&row->fetch_policy);
    memset(row, 0, sizeof(*row));
}

static vxml_status decode_attribute_owned(
    salts_xml_attribute attribute,
    char **out_data, size_t *out_size,
    vxml_diagnostic *diagnostic,
    const char *message) {
    const salts_xml_string_view raw =
        salts_xml_attribute_value(attribute);
    size_t decoded_size = 0u;
    char *decoded;
    if (out_data == NULL || out_size == NULL ||
        attribute.impl == NULL)
        return VXML_INVALID_ARGUMENT;
    *out_data = NULL;
    *out_size = 0u;
    if (!decode_entities(raw, NULL, 0u, &decoded_size) ||
        decoded_size == SIZE_MAX)
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(attribute), message);
    decoded = (char *)vxml_malloc(decoded_size + 1u);
    if (decoded == NULL)
        return fail(
            diagnostic, VXML_ALLOCATION_FAILED,
            salts_xml_attribute_location(attribute),
            "VoiceXML attribute decoding allocation failed");
    if (!decode_entities(
            raw, decoded, decoded_size, &decoded_size)) {
        vxml_free(decoded);
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(attribute),
            "VoiceXML attribute decoding changed between passes");
    }
    decoded[decoded_size] = '\0';
    *out_data = decoded;
    *out_size = decoded_size;
    return VXML_OK;
}

static vxml_status retain_attribute_owned(
    vxml_measurement *measurement,
    salts_xml_attribute attribute,
    const vxml_limits *limits,
    char **out_data, size_t *out_size,
    vxml_diagnostic *diagnostic,
    const char *message) {
    vxml_status status;
    size_t retained;
    status = decode_attribute_owned(
        attribute, out_data, out_size, diagnostic, message);
    if (status != VXML_OK) return status;
    if (!checked_add(*out_size, 1u, &retained) ||
        !checked_add(
            measurement->name_bytes, retained,
            &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes) {
        vxml_free(*out_data);
        *out_data = NULL;
        *out_size = 0u;
        return fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_attribute_location(attribute),
            "VoiceXML retained data metadata exceeds max_name_bytes");
    }
    return VXML_OK;
}

static vxml_status retain_policy_uri_copy(
    vxml_measurement *measurement,
    const vxml_limits *limits,
    const vxml_decoded_data_policy *source,
    vxml_decoded_data_policy *destination,
    salts_xml_location location,
    vxml_diagnostic *diagnostic) {
    size_t retained;
    *destination = *source;
    destination->fetchaudio_uri = NULL;
    if (source->fetchaudio_uri_size == 0u)
        return VXML_OK;
    if (source->fetchaudio_uri == NULL ||
        !checked_add(source->fetchaudio_uri_size, 1u, &retained) ||
        !checked_add(
            measurement->name_bytes, retained,
            &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes)
        return fail(
            diagnostic, VXML_LIMIT_EXCEEDED, location,
            "VoiceXML retained data fetchaudio exceeds max_name_bytes");
    destination->fetchaudio_uri =
        (char *)vxml_malloc(retained);
    if (destination->fetchaudio_uri == NULL)
        return fail(
            diagnostic, VXML_ALLOCATION_FAILED, location,
            "VoiceXML data fetchaudio allocation failed");
    memcpy(
        destination->fetchaudio_uri,
        source->fetchaudio_uri, retained);
    return VXML_OK;
}

static vxml_status clone_policy_scope(
    const vxml_decoded_data_policy *source,
    vxml_decoded_data_policy *destination,
    salts_xml_location location,
    vxml_diagnostic *diagnostic) {
    size_t retained;
    if (source == NULL || destination == NULL)
        return VXML_INVALID_ARGUMENT;
    *destination = *source;
    destination->fetchaudio_uri = NULL;
    if (source->fetchaudio_uri_size == 0u)
        return VXML_OK;
    if (source->fetchaudio_uri == NULL ||
        !checked_add(source->fetchaudio_uri_size, 1u, &retained))
        return fail(
            diagnostic, VXML_LIMIT_EXCEEDED, location,
            "VoiceXML data property URI size overflow");
    destination->fetchaudio_uri =
        (char *)vxml_malloc(retained);
    if (destination->fetchaudio_uri == NULL)
        return fail(
            diagnostic, VXML_ALLOCATION_FAILED, location,
            "VoiceXML data property URI allocation failed");
    memcpy(
        destination->fetchaudio_uri,
        source->fetchaudio_uri, retained);
    return VXML_OK;
}

static vxml_status parse_time_designation(
    salts_xml_attribute attribute,
    bool *out_has_value, uint64_t *out_value_us,
    vxml_diagnostic *diagnostic) {
    char *decoded = NULL;
    size_t decoded_size = 0u;
    size_t number_size;
    size_t cursor = 0u;
    size_t fractional_digits = 0u;
    size_t fractional_limit;
    uint64_t unit_us;
    uint64_t integer_part = UINT64_C(0);
    uint64_t fractional_part = UINT64_C(0);
    bool saw_digit = false;
    bool saw_decimal = false;
    vxml_status status;
    if (out_has_value == NULL || out_value_us == NULL ||
        attribute.impl == NULL)
        return VXML_INVALID_ARGUMENT;
    *out_has_value = false;
    *out_value_us = UINT64_C(0);
    status = decode_attribute_owned(
        attribute, &decoded, &decoded_size, diagnostic,
        "VoiceXML time designation is invalid");
    if (status != VXML_OK) return status;
    if (decoded_size >= 2u &&
        decoded[decoded_size - 2u] == 'm' &&
        decoded[decoded_size - 1u] == 's') {
        unit_us = UINT64_C(1000);
        fractional_limit = 3u;
        number_size = decoded_size - 2u;
    } else if (decoded_size >= 1u &&
               decoded[decoded_size - 1u] == 's') {
        unit_us = UINT64_C(1000000);
        fractional_limit = 6u;
        number_size = decoded_size - 1u;
    } else {
        vxml_free(decoded);
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(attribute),
            "VoiceXML time designation requires ms or s units");
    }
    if (number_size == 0u) {
        vxml_free(decoded);
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(attribute),
            "VoiceXML time designation is missing its numeric value");
    }
    while (cursor < number_size) {
        const unsigned char ch = (unsigned char)decoded[cursor++];
        if (ch == (unsigned char)'.') {
            if (saw_decimal || !saw_digit || cursor == number_size) {
                vxml_free(decoded);
                return fail(
                    diagnostic, VXML_INVALID_STRUCTURE,
                    salts_xml_attribute_location(attribute),
                    "VoiceXML time designation has an invalid decimal form");
            }
            saw_decimal = true;
            continue;
        }
        if (ch < (unsigned char)'0' || ch > (unsigned char)'9') {
            vxml_free(decoded);
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(attribute),
                "VoiceXML time designation must be non-negative");
        }
        saw_digit = true;
        if (!saw_decimal) {
            const uint64_t digit = (uint64_t)(ch - (unsigned char)'0');
            if (integer_part >
                (UINT64_MAX - digit) / UINT64_C(10)) {
                vxml_free(decoded);
                return fail(
                    diagnostic, VXML_LIMIT_EXCEEDED,
                    salts_xml_attribute_location(attribute),
                    "VoiceXML time designation overflows");
            }
            integer_part = integer_part * UINT64_C(10) + digit;
        } else {
            if (fractional_digits >= fractional_limit) {
                vxml_free(decoded);
                return fail(
                    diagnostic, VXML_LIMIT_EXCEEDED,
                    salts_xml_attribute_location(attribute),
                    "VoiceXML time designation exceeds microsecond precision");
            }
            fractional_part =
                fractional_part * UINT64_C(10) +
                (uint64_t)(ch - (unsigned char)'0');
            ++fractional_digits;
        }
    }
    if (!saw_digit || integer_part > UINT64_MAX / unit_us) {
        vxml_free(decoded);
        return fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_attribute_location(attribute),
            "VoiceXML time designation exceeds uint64 microseconds");
    }
    *out_value_us = integer_part * unit_us;
    if (fractional_digits != 0u) {
        uint64_t scale = unit_us;
        size_t index;
        for (index = 0u; index < fractional_digits; ++index)
            scale /= UINT64_C(10);
        if (fractional_part >
            (UINT64_MAX - *out_value_us) / scale) {
            vxml_free(decoded);
            return fail(
                diagnostic, VXML_LIMIT_EXCEEDED,
                salts_xml_attribute_location(attribute),
                "VoiceXML time designation exceeds uint64 microseconds");
        }
        *out_value_us += fractional_part * scale;
    }
    *out_has_value = true;
    vxml_free(decoded);
    return VXML_OK;
}

static vxml_status parse_nonnegative_seconds(
    salts_xml_attribute attribute,
    bool *out_has_value, uint64_t *out_seconds,
    vxml_diagnostic *diagnostic) {
    char *decoded = NULL;
    size_t decoded_size = 0u;
    size_t index;
    uint64_t value = UINT64_C(0);
    vxml_status status;
    if (out_has_value == NULL || out_seconds == NULL ||
        attribute.impl == NULL)
        return VXML_INVALID_ARGUMENT;
    *out_has_value = false;
    *out_seconds = UINT64_C(0);
    status = decode_attribute_owned(
        attribute, &decoded, &decoded_size, diagnostic,
        "VoiceXML cache age must be a non-negative integer");
    if (status != VXML_OK) return status;
    if (decoded_size == 0u) {
        vxml_free(decoded);
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(attribute),
            "VoiceXML cache age must be a non-negative integer");
    }
    for (index = 0u; index < decoded_size; ++index) {
        const unsigned char ch = (unsigned char)decoded[index];
        const uint64_t digit =
            ch >= (unsigned char)'0' && ch <= (unsigned char)'9'
                ? (uint64_t)(ch - (unsigned char)'0')
                : UINT64_MAX;
        if (digit == UINT64_MAX) {
            vxml_free(decoded);
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(attribute),
                "VoiceXML cache age must be a non-negative integer");
        }
        if (value > (UINT64_MAX - digit) / UINT64_C(10)) {
            vxml_free(decoded);
            return fail(
                diagnostic, VXML_LIMIT_EXCEEDED,
                salts_xml_attribute_location(attribute),
                "VoiceXML cache age exceeds uint64 seconds");
        }
        value = value * UINT64_C(10) + digit;
    }
    *out_has_value = true;
    *out_seconds = value;
    vxml_free(decoded);
    return VXML_OK;
}

static vxml_status validate_property_attributes(
    salts_xml_node node, vxml_diagnostic *diagnostic) {
    bool seen_name = false;
    bool seen_value = false;
    size_t index;
    for (index = 0u; index < salts_xml_node_attribute_count(node); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(node, index);
        const salts_xml_string_view local =
            salts_xml_attribute_local_name(attribute);
        bool *seen = NULL;
        if (salts_xml_attribute_namespace_uri(attribute).size == 0u &&
            view_equal(local, "name"))
            seen = &seen_name;
        else if (salts_xml_attribute_namespace_uri(attribute).size == 0u &&
                 view_equal(local, "value"))
            seen = &seen_value;
        if (seen == NULL || *seen)
            return fail(
                diagnostic, VXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported or duplicate VoiceXML property attribute");
        *seen = true;
    }
    if (!seen_name || !seen_value)
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_node_location(node),
            "VoiceXML property requires name and value");
    for (index = 0u; index < salts_xml_node_child_count(node); ++index)
        if (!node_is_ignorable(salts_xml_node_child_at(node, index)))
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_node_location(node),
                "VoiceXML property must be empty");
    return VXML_OK;
}

static vxml_status apply_data_property(
    salts_xml_node node, bool version_21,
    vxml_decoded_data_policy *policy,
    vxml_data_property_seen *seen,
    vxml_diagnostic *diagnostic) {
    const salts_xml_attribute name =
        unqualified_attribute(node, "name");
    const salts_xml_attribute value =
        unqualified_attribute(node, "value");
    const salts_xml_string_view raw_name =
        salts_xml_attribute_value(name);
    vxml_status status = validate_property_attributes(node, diagnostic);
    if (status != VXML_OK) return status;

    if (normalized_view_equal(raw_name, "fetchaudio")) {
        char *decoded = NULL;
        size_t decoded_size = 0u;
        if (seen->fetchaudio)
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(name),
                "duplicate VoiceXML fetchaudio property");
        status = decode_attribute_owned(
            value, &decoded, &decoded_size, diagnostic,
            "VoiceXML fetchaudio property is invalid");
        if (status != VXML_OK) return status;
        if (decoded_size == 0u) {
            vxml_free(decoded);
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(value),
                "VoiceXML fetchaudio property must be nonempty");
        }
        vxml_free(policy->fetchaudio_uri);
        policy->fetchaudio_uri = decoded;
        policy->fetchaudio_uri_size = decoded_size;
        seen->fetchaudio = true;
        return VXML_OK;
    }
    if (normalized_view_equal(raw_name, "fetchaudiodelay")) {
        if (seen->fetchaudio_delay)
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(name),
                "duplicate VoiceXML fetchaudiodelay property");
        seen->fetchaudio_delay = true;
        return parse_time_designation(
            value, &policy->has_fetchaudio_delay,
            &policy->fetchaudio_delay_us, diagnostic);
    }
    if (normalized_view_equal(raw_name, "fetchaudiominimum")) {
        if (seen->fetchaudio_minimum)
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(name),
                "duplicate VoiceXML fetchaudiominimum property");
        seen->fetchaudio_minimum = true;
        return parse_time_designation(
            value, &policy->has_fetchaudio_minimum,
            &policy->fetchaudio_minimum_us, diagnostic);
    }
    if (normalized_view_equal(raw_name, "fetchtimeout")) {
        if (seen->timeout)
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(name),
                "duplicate VoiceXML fetchtimeout property");
        seen->timeout = true;
        return parse_time_designation(
            value, &policy->has_timeout,
            &policy->timeout_us, diagnostic);
    }
    if (normalized_view_equal(raw_name, "datafetchhint")) {
        if (!version_21)
            return fail(
                diagnostic, VXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(name),
                "VoiceXML datafetchhint requires version 2.1");
        if (seen->hint)
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(name),
                "duplicate VoiceXML datafetchhint property");
        if (normalized_view_equal(
                salts_xml_attribute_value(value), "prefetch"))
            policy->fetch_hint =
                VXML_CMETA_DATA_FETCH_HINT_PREFETCH;
        else if (normalized_view_equal(
                     salts_xml_attribute_value(value), "safe"))
            policy->fetch_hint =
                VXML_CMETA_DATA_FETCH_HINT_SAFE;
        else
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(value),
                "VoiceXML datafetchhint must be prefetch or safe");
        seen->hint = true;
        return VXML_OK;
    }
    if (normalized_view_equal(raw_name, "datamaxage")) {
        if (!version_21)
            return fail(
                diagnostic, VXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(name),
                "VoiceXML datamaxage requires version 2.1");
        if (seen->max_age)
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(name),
                "duplicate VoiceXML datamaxage property");
        seen->max_age = true;
        return parse_nonnegative_seconds(
            value, &policy->has_max_age,
            &policy->max_age_seconds, diagnostic);
    }
    if (normalized_view_equal(raw_name, "datamaxstale")) {
        if (!version_21)
            return fail(
                diagnostic, VXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(name),
                "VoiceXML datamaxstale requires version 2.1");
        if (seen->max_stale)
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(name),
                "duplicate VoiceXML datamaxstale property");
        seen->max_stale = true;
        return parse_nonnegative_seconds(
            value, &policy->has_max_stale,
            &policy->max_stale_seconds, diagnostic);
    }
    return fail(
        diagnostic, VXML_UNSUPPORTED_FEATURE,
        salts_xml_attribute_location(name),
        "unsupported VoiceXML property in QuickJS data profile");
}

static vxml_status validate_data_attributes(
    salts_xml_node node, vxml_diagnostic *diagnostic) {
    static const char *const allowed[] = {
        "name", "src", "srcexpr", "method", "namelist", "enctype",
        "fetchaudio", "fetchhint", "fetchtimeout", "maxage", "maxstale"};
    bool seen[11] = {false};
    size_t index;
    for (index = 0u; index < salts_xml_node_attribute_count(node); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(node, index);
        const salts_xml_string_view local =
            salts_xml_attribute_local_name(attribute);
        size_t allowed_index;
        if (salts_xml_attribute_namespace_uri(attribute).size != 0u)
            return fail(
                diagnostic, VXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported VoiceXML data attribute");
        for (allowed_index = 0u;
             allowed_index < sizeof(allowed) / sizeof(allowed[0]);
             ++allowed_index)
            if (view_equal(local, allowed[allowed_index]))
                break;
        if (allowed_index == sizeof(allowed) / sizeof(allowed[0]) ||
            seen[allowed_index])
            return fail(
                diagnostic, VXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported or duplicate VoiceXML data attribute");
        seen[allowed_index] = true;
    }
    for (index = 0u; index < salts_xml_node_child_count(node); ++index)
        if (!node_is_ignorable(salts_xml_node_child_at(node, index)))
            return fail(
                diagnostic, VXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(node),
                "QuickJS no-DOM data profile does not admit inline data content");
    return VXML_OK;
}

static bool next_namelist_token(
    const char *data, size_t size, size_t *cursor,
    const char **out_data, size_t *out_size) {
    size_t start;
    if (cursor == NULL || out_data == NULL || out_size == NULL)
        return false;
    while (*cursor < size &&
           xml_space((unsigned char)data[*cursor]))
        ++*cursor;
    if (*cursor == size) {
        *out_data = NULL;
        *out_size = 0u;
        return true;
    }
    start = *cursor;
    while (*cursor < size &&
           !xml_space((unsigned char)data[*cursor]))
        ++*cursor;
    *out_data = data + start;
    *out_size = *cursor - start;
    return true;
}

static vxml_status validate_namelist(
    const char *data, size_t size,
    size_t *out_count, salts_xml_location location,
    vxml_diagnostic *diagnostic) {
    size_t cursor = 0u;
    size_t count = 0u;
    const char *token;
    size_t token_size;
    while (cursor < size) {
        size_t previous_cursor = 0u;
        size_t previous_index = 0u;
        if (!next_namelist_token(
                data, size, &cursor, &token, &token_size))
            return VXML_INVALID_STRUCTURE;
        if (token == NULL) break;
        if (!is_ncname(token, token_size))
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE, location,
                "VoiceXML data namelist entries must be XML NCNames");
        while (previous_index < count) {
            const char *previous;
            size_t previous_size;
            if (!next_namelist_token(
                    data, size, &previous_cursor,
                    &previous, &previous_size) ||
                previous == NULL)
                return VXML_INVALID_STRUCTURE;
            if (previous_size == token_size &&
                memcmp(previous, token, token_size) == 0)
                return fail(
                    diagnostic, VXML_INVALID_STRUCTURE, location,
                    "VoiceXML data namelist contains a duplicate name");
            ++previous_index;
        }
        ++count;
    }
    if (out_count != NULL) *out_count = count;
    return VXML_OK;
}

static vxml_status append_data_row(
    vxml_measurement *measurement,
    salts_xml_node node,
    vxml_data_placement placement,
    size_t owner_form,
    const vxml_decoded_data_policy *inherited_policy,
    const vxml_limits *limits,
    vxml_diagnostic *diagnostic) {
    vxml_decoded_data row;
    const salts_xml_attribute name =
        unqualified_attribute(node, "name");
    const salts_xml_attribute src =
        unqualified_attribute(node, "src");
    const salts_xml_attribute srcexpr =
        unqualified_attribute(node, "srcexpr");
    const salts_xml_attribute method =
        unqualified_attribute(node, "method");
    const salts_xml_attribute namelist =
        unqualified_attribute(node, "namelist");
    const salts_xml_attribute enctype =
        unqualified_attribute(node, "enctype");
    const salts_xml_attribute fetchaudio =
        unqualified_attribute(node, "fetchaudio");
    const salts_xml_attribute fetchhint =
        unqualified_attribute(node, "fetchhint");
    const salts_xml_attribute fetchtimeout =
        unqualified_attribute(node, "fetchtimeout");
    const salts_xml_attribute maxage =
        unqualified_attribute(node, "maxage");
    const salts_xml_attribute maxstale =
        unqualified_attribute(node, "maxstale");
    size_t allocation_size;
    vxml_status status;
    memset(&row, 0, sizeof(row));
    row.placement = placement;
    row.owner_form = owner_form;
    row.method = VXML_SUBMIT_METHOD_GET;
    row.enctype = VXML_SUBMIT_ENCTYPE_URLENCODED;
    row.location = salts_xml_node_location(node);

    if (!measurement->version_21)
        return fail(
            diagnostic, VXML_UNSUPPORTED_FEATURE, row.location,
            "VoiceXML data requires version 2.1");
    status = validate_data_attributes(node, diagnostic);
    if (status != VXML_OK) return status;
    if ((src.impl == NULL) == (srcexpr.impl == NULL))
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE, row.location,
            "VoiceXML data requires exactly one of src or srcexpr");
    status = retain_policy_uri_copy(
        measurement, limits, inherited_policy,
        &row.fetch_policy, row.location, diagnostic);
    if (status != VXML_OK) goto fail_row;

    if (name.impl != NULL) {
        status = retain_attribute_owned(
            measurement, name, limits,
            &row.name, &row.name_size,
            diagnostic, "VoiceXML data name is invalid");
        if (status != VXML_OK) goto fail_row;
        if (!is_ncname(row.name, row.name_size)) {
            status = fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(name),
                "VoiceXML data name must be a nonempty XML NCName");
            goto fail_row;
        }
    }
    if (src.impl != NULL) {
        status = retain_attribute_owned(
            measurement, src, limits,
            &row.uri, &row.uri_size,
            diagnostic, "VoiceXML data src is invalid");
        if (status != VXML_OK) goto fail_row;
        if (row.uri_size == 0u) {
            status = fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(src),
                "VoiceXML data src must be nonempty");
            goto fail_row;
        }
    } else {
        status = retain_attribute_owned(
            measurement, srcexpr, limits,
            &row.uri_expression, &row.uri_expression_size,
            diagnostic, "VoiceXML data srcexpr is invalid");
        if (status != VXML_OK) goto fail_row;
        if (row.uri_expression_size == 0u) {
            status = fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(srcexpr),
                "VoiceXML data srcexpr must be nonempty");
            goto fail_row;
        }
    }

    if (method.impl != NULL) {
        if (normalized_view_equal(
                salts_xml_attribute_value(method), "get"))
            row.method = VXML_SUBMIT_METHOD_GET;
        else if (normalized_view_equal(
                     salts_xml_attribute_value(method), "post"))
            row.method = VXML_SUBMIT_METHOD_POST;
        else {
            status = fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(method),
                "VoiceXML data method must be get or post");
            goto fail_row;
        }
    }
    if (enctype.impl != NULL) {
        if (normalized_view_equal(
                salts_xml_attribute_value(enctype),
                "application/x-www-form-urlencoded"))
            row.enctype = VXML_SUBMIT_ENCTYPE_URLENCODED;
        else if (normalized_view_equal(
                     salts_xml_attribute_value(enctype),
                     "multipart/form-data"))
            row.enctype =
                VXML_SUBMIT_ENCTYPE_MULTIPART_FORM_DATA;
        else {
            status = fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(enctype),
                "VoiceXML data enctype is unsupported");
            goto fail_row;
        }
        if (row.method != VXML_SUBMIT_METHOD_POST) {
            status = fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(enctype),
                "VoiceXML data enctype requires method=post");
            goto fail_row;
        }
    }
    if (namelist.impl != NULL) {
        status = retain_attribute_owned(
            measurement, namelist, limits,
            &row.namelist, &row.namelist_size,
            diagnostic, "VoiceXML data namelist is invalid");
        if (status != VXML_OK) goto fail_row;
        status = validate_namelist(
            row.namelist, row.namelist_size,
            &row.namelist_count,
            salts_xml_attribute_location(namelist),
            diagnostic);
        if (status != VXML_OK) goto fail_row;
        if (row.namelist_count == 0u) {
            status = fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(namelist),
                "VoiceXML data namelist must not be empty");
            goto fail_row;
        }
    }

    if (fetchaudio.impl != NULL) {
        char *decoded = NULL;
        size_t decoded_size = 0u;
        status = retain_attribute_owned(
            measurement, fetchaudio, limits,
            &decoded, &decoded_size,
            diagnostic, "VoiceXML data fetchaudio is invalid");
        if (status != VXML_OK) goto fail_row;
        if (decoded_size == 0u) {
            vxml_free(decoded);
            status = fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(fetchaudio),
                "VoiceXML data fetchaudio must be nonempty");
            goto fail_row;
        }
        if (row.fetch_policy.fetchaudio_uri != NULL) {
            const size_t old_bytes =
                row.fetch_policy.fetchaudio_uri_size + 1u;
            if (measurement->name_bytes >= old_bytes)
                measurement->name_bytes -= old_bytes;
            vxml_free(row.fetch_policy.fetchaudio_uri);
        }
        row.fetch_policy.fetchaudio_uri = decoded;
        row.fetch_policy.fetchaudio_uri_size = decoded_size;
    }
    if (fetchhint.impl != NULL) {
        if (normalized_view_equal(
                salts_xml_attribute_value(fetchhint), "prefetch"))
            row.fetch_policy.fetch_hint =
                VXML_CMETA_DATA_FETCH_HINT_PREFETCH;
        else if (normalized_view_equal(
                     salts_xml_attribute_value(fetchhint), "safe"))
            row.fetch_policy.fetch_hint =
                VXML_CMETA_DATA_FETCH_HINT_SAFE;
        else {
            status = fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(fetchhint),
                "VoiceXML data fetchhint must be prefetch or safe");
            goto fail_row;
        }
    }
    if (fetchtimeout.impl != NULL) {
        status = parse_time_designation(
            fetchtimeout,
            &row.fetch_policy.has_timeout,
            &row.fetch_policy.timeout_us, diagnostic);
        if (status != VXML_OK) goto fail_row;
    }
    if (maxage.impl != NULL) {
        status = parse_nonnegative_seconds(
            maxage,
            &row.fetch_policy.has_max_age,
            &row.fetch_policy.max_age_seconds,
            diagnostic);
        if (status != VXML_OK) goto fail_row;
    }
    if (maxstale.impl != NULL) {
        status = parse_nonnegative_seconds(
            maxstale,
            &row.fetch_policy.has_max_stale,
            &row.fetch_policy.max_stale_seconds,
            diagnostic);
        if (status != VXML_OK) goto fail_row;
    }

    if (measurement->data_count >= limits->max_actions) {
        status = fail(
            diagnostic, VXML_LIMIT_EXCEEDED, row.location,
            "VoiceXML data row count exceeds max_actions");
        goto fail_row;
    }
    if (measurement->data_count == measurement->data_capacity) {
        size_t capacity = measurement->data_capacity == 0u
            ? 4u : measurement->data_capacity * 2u;
        vxml_decoded_data *rows;
        if (capacity < measurement->data_capacity ||
            capacity > limits->max_actions)
            capacity = limits->max_actions;
        if (capacity <= measurement->data_capacity ||
            !checked_multiply(
                capacity, sizeof(*rows),
                &allocation_size)) {
            status = fail(
                diagnostic, VXML_LIMIT_EXCEEDED, row.location,
                "VoiceXML temporary data table size overflow");
            goto fail_row;
        }
        rows = (vxml_decoded_data *)vxml_realloc(
            measurement->data_rows, allocation_size);
        if (rows == NULL) {
            status = fail(
                diagnostic, VXML_ALLOCATION_FAILED, row.location,
                "VoiceXML temporary data table allocation failed");
            goto fail_row;
        }
        measurement->data_rows = rows;
        measurement->data_capacity = capacity;
    }
    measurement->data_rows[measurement->data_count++] = row;
    return VXML_OK;

fail_row:
    decoded_data_destroy(&row);
    return status;
}


static void measurement_destroy(vxml_measurement *measurement) {
    size_t index;
    if (measurement == NULL) return;
    for (index = 0u; index < measurement->id_count; ++index)
        vxml_free(measurement->ids[index].data);
    for (index = 0u; index < measurement->goto_count; ++index) {
        vxml_free(measurement->gotos[index].target);
        vxml_free(measurement->gotos[index].fetchaudio);
    }
    for (index = 0u; index < measurement->submit_count; ++index)
        vxml_free(measurement->submits[index].target);
    for (index = 0u; index < measurement->script_count; ++index) {
        vxml_free(measurement->scripts[index].src);
        vxml_free(measurement->scripts[index].srcexpr);
        vxml_free(measurement->scripts[index].charset);
    }
    for (index = 0u; index < measurement->data_count; ++index)
        decoded_data_destroy(&measurement->data_rows[index]);
    vxml_free(measurement->ids);
    vxml_free(measurement->gotos);
    vxml_free(measurement->submits);
    vxml_free(measurement->scripts);
    vxml_free(measurement->data_rows);
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

static vxml_status append_goto(
    vxml_measurement *measurement,
    salts_xml_attribute attribute,
    salts_xml_attribute fetchaudio_attribute,
    const vxml_limits *limits, vxml_diagnostic *diagnostic) {
    vxml_decoded_goto entry = {0};
    const salts_xml_string_view raw =
        salts_xml_attribute_value(attribute);
    size_t decoded_size = 0u;
    size_t allocation_size;
    if (attribute.impl == NULL)
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            (salts_xml_location){0},
            "VoiceXML goto requires next");
    if (!decode_entities(raw, NULL, 0u, &decoded_size) ||
        !checked_add(decoded_size, 1u, &allocation_size))
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(attribute),
            "VoiceXML goto next has an invalid XML entity reference");
    if (decoded_size > limits->max_name_bytes)
        return fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_attribute_location(attribute),
            "VoiceXML goto next exceeds max_name_bytes");
    entry.target = (char *)vxml_malloc(allocation_size);
    if (entry.target == NULL)
        return fail(
            diagnostic, VXML_ALLOCATION_FAILED,
            salts_xml_attribute_location(attribute),
            "VoiceXML goto target decoding allocation failed");
    if (!decode_entities(
            raw, entry.target, decoded_size, &decoded_size)) {
        vxml_free(entry.target);
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(attribute),
            "VoiceXML goto target decoding changed between passes");
    }
    entry.target[decoded_size] = '\0';
    entry.location = salts_xml_attribute_location(attribute);
    if (decoded_size == 0u) {
        vxml_free(entry.target);
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(attribute),
            "VoiceXML goto next must be nonempty");
    }
    entry.target_form = SIZE_MAX;
    if (entry.target[0] == '#') {
        if (decoded_size == 1u ||
            !is_ncname(entry.target + 1u, decoded_size - 1u)) {
            vxml_free(entry.target);
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(attribute),
                "VoiceXML goto next must be one valid local form fragment");
        }
        memmove(entry.target, entry.target + 1u, decoded_size - 1u);
        entry.target_size = decoded_size - 1u;
        entry.target[entry.target_size] = '\0';
    } else {
        size_t retained_size;
        entry.external = true;
        entry.target_size = decoded_size;
        if (!checked_add(entry.target_size, 1u, &retained_size) ||
            !checked_add(
                measurement->name_bytes, retained_size,
                &measurement->name_bytes) ||
            measurement->name_bytes > limits->max_name_bytes) {
            vxml_free(entry.target);
            return fail(
                diagnostic, VXML_LIMIT_EXCEEDED,
                entry.location,
                "VoiceXML retained goto URI bytes exceed max_name_bytes");
        }
    }

    if (fetchaudio_attribute.impl != NULL) {
        const salts_xml_string_view raw_fetchaudio =
            salts_xml_attribute_value(fetchaudio_attribute);
        size_t fetchaudio_size = 0u;
        size_t retained_size;
        if (!decode_entities(
                raw_fetchaudio, NULL, 0u, &fetchaudio_size) ||
            fetchaudio_size == 0u ||
            !checked_add(fetchaudio_size, 1u, &retained_size)) {
            vxml_free(entry.target);
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(fetchaudio_attribute),
                "VoiceXML goto fetchaudio must be one nonempty URI");
        }
        if (entry.external) {
            if (!checked_add(
                    measurement->name_bytes, retained_size,
                    &measurement->name_bytes) ||
                measurement->name_bytes > limits->max_name_bytes) {
                vxml_free(entry.target);
                return fail(
                    diagnostic, VXML_LIMIT_EXCEEDED,
                    salts_xml_attribute_location(fetchaudio_attribute),
                    "VoiceXML goto fetchaudio exceeds max_name_bytes");
            }
            entry.fetchaudio = (char *)vxml_malloc(retained_size);
            if (entry.fetchaudio == NULL) {
                vxml_free(entry.target);
                return fail(
                    diagnostic, VXML_ALLOCATION_FAILED,
                    salts_xml_attribute_location(fetchaudio_attribute),
                    "VoiceXML goto fetchaudio allocation failed");
            }
            if (!decode_entities(
                    raw_fetchaudio, entry.fetchaudio,
                    fetchaudio_size, &fetchaudio_size)) {
                vxml_free(entry.fetchaudio);
                vxml_free(entry.target);
                return fail(
                    diagnostic, VXML_INVALID_STRUCTURE,
                    salts_xml_attribute_location(fetchaudio_attribute),
                    "VoiceXML goto fetchaudio decoding changed between passes");
            }
            entry.fetchaudio[fetchaudio_size] = '\0';
            entry.fetchaudio_size = fetchaudio_size;
        }
    }

    if (measurement->goto_count == measurement->goto_capacity) {
        size_t capacity = measurement->goto_capacity == 0u
            ? 4u : measurement->goto_capacity * 2u;
        vxml_decoded_goto *gotos;
        if (capacity < measurement->goto_capacity ||
            capacity > limits->max_actions)
            capacity = limits->max_actions;
        if (capacity <= measurement->goto_capacity ||
            !checked_multiply(capacity, sizeof(*gotos), &allocation_size)) {
            vxml_free(entry.fetchaudio);
            vxml_free(entry.target);
            return fail(
                diagnostic, VXML_LIMIT_EXCEEDED,
                entry.location,
                "VoiceXML temporary goto table size overflow");
        }
        gotos = (vxml_decoded_goto *)vxml_realloc(
            measurement->gotos, allocation_size);
        if (gotos == NULL) {
            vxml_free(entry.fetchaudio);
            vxml_free(entry.target);
            return fail(
                diagnostic, VXML_ALLOCATION_FAILED,
                entry.location,
                "VoiceXML temporary goto table allocation failed");
        }
        measurement->gotos = gotos;
        measurement->goto_capacity = capacity;
    }
    measurement->gotos[measurement->goto_count++] = entry;
    return VXML_OK;
}

static vxml_status reject_non_element(
    salts_xml_node node, vxml_diagnostic *diagnostic);
static vxml_status reject_unexpected_element(
    salts_xml_node node, vxml_diagnostic *diagnostic,
    const char *unsupported_message);

static vxml_status append_submit(
    vxml_measurement *measurement,
    salts_xml_attribute next_attribute,
    salts_xml_attribute method_attribute,
    salts_xml_attribute enctype_attribute,
    salts_xml_attribute namelist_attribute,
    const vxml_limits *limits,
    vxml_diagnostic *diagnostic) {
    vxml_decoded_submit entry = {0};
    const salts_xml_string_view raw_next =
        salts_xml_attribute_value(next_attribute);
    size_t target_size = 0u;
    size_t retained_size;
    size_t allocation_size;

    if (next_attribute.impl == NULL)
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            (salts_xml_location){0},
            "VoiceXML submit requires next");
    if (!decode_entities(raw_next, NULL, 0u, &target_size) ||
        target_size == 0u ||
        !checked_add(target_size, 1u, &retained_size))
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(next_attribute),
            "VoiceXML submit next must be one nonempty URI");
    if (!checked_add(
            measurement->name_bytes, retained_size,
            &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes)
        return fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_attribute_location(next_attribute),
            "VoiceXML retained submit URI exceeds max_name_bytes");

    entry.target = (char *)vxml_malloc(retained_size);
    if (entry.target == NULL)
        return fail(
            diagnostic, VXML_ALLOCATION_FAILED,
            salts_xml_attribute_location(next_attribute),
            "VoiceXML submit target allocation failed");
    if (!decode_entities(
            raw_next, entry.target, target_size, &target_size)) {
        vxml_free(entry.target);
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(next_attribute),
            "VoiceXML submit target decoding changed between passes");
    }
    entry.target[target_size] = '\0';
    entry.target_size = target_size;
    entry.location = salts_xml_attribute_location(next_attribute);
    entry.method = VXML_SUBMIT_METHOD_GET;
    entry.enctype = VXML_SUBMIT_ENCTYPE_URLENCODED;

    if (method_attribute.impl != NULL) {
        const salts_xml_string_view raw =
            salts_xml_attribute_value(method_attribute);
        if (normalized_view_equal(raw, "get"))
            entry.method = VXML_SUBMIT_METHOD_GET;
        else if (normalized_view_equal(raw, "post"))
            entry.method = VXML_SUBMIT_METHOD_POST;
        else {
            vxml_free(entry.target);
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(method_attribute),
                "VoiceXML submit method must be get or post");
        }
    }

    if (enctype_attribute.impl != NULL) {
        const salts_xml_string_view raw =
            salts_xml_attribute_value(enctype_attribute);
        if (!normalized_view_equal(
                raw, "application/x-www-form-urlencoded")) {
            vxml_free(entry.target);
            return fail(
                diagnostic, VXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(enctype_attribute),
                "VoiceXML literal submit supports urlencoded enctype only");
        }
    }

    if (namelist_attribute.impl != NULL) {
        const salts_xml_string_view raw =
            salts_xml_attribute_value(namelist_attribute);
        if (raw.size != 0u && !text_is_whitespace(raw)) {
            vxml_free(entry.target);
            return fail(
                diagnostic, VXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(namelist_attribute),
                "VoiceXML literal submit cannot bind namelist values");
        }
    }

    if (measurement->submit_count == measurement->submit_capacity) {
        size_t capacity = measurement->submit_capacity == 0u
            ? 4u : measurement->submit_capacity * 2u;
        vxml_decoded_submit *rows;
        if (capacity < measurement->submit_capacity ||
            capacity > limits->max_actions)
            capacity = limits->max_actions;
        if (capacity <= measurement->submit_capacity ||
            !checked_multiply(
                capacity, sizeof(*rows), &allocation_size)) {
            vxml_free(entry.target);
            return fail(
                diagnostic, VXML_LIMIT_EXCEEDED,
                entry.location,
                "VoiceXML temporary submit table size overflow");
        }
        rows = (vxml_decoded_submit *)vxml_realloc(
            measurement->submits, allocation_size);
        if (rows == NULL) {
            vxml_free(entry.target);
            return fail(
                diagnostic, VXML_ALLOCATION_FAILED,
                entry.location,
                "VoiceXML temporary submit table allocation failed");
        }
        measurement->submits = rows;
        measurement->submit_capacity = capacity;
    }
    measurement->submits[measurement->submit_count++] = entry;
    return VXML_OK;
}

static vxml_status measure_submit(
    salts_xml_node node,
    vxml_measurement *measurement,
    const vxml_limits *limits,
    vxml_diagnostic *diagnostic) {
    const salts_xml_attribute next =
        unqualified_attribute(node, "next");
    const salts_xml_attribute method =
        unqualified_attribute(node, "method");
    const salts_xml_attribute enctype =
        unqualified_attribute(node, "enctype");
    const salts_xml_attribute namelist =
        unqualified_attribute(node, "namelist");
    size_t index;
    vxml_status status =
        validate_submit_attributes(node, diagnostic);
    if (status != VXML_OK) return status;
    if (next.impl == NULL)
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_node_location(node),
            "VoiceXML submit requires next");
    for (index = 0u; index < salts_xml_node_child_count(node); ++index) {
        const salts_xml_node child =
            salts_xml_node_child_at(node, index);
        if (node_is_ignorable(child)) continue;
        if (salts_xml_node_type(child) == SALTS_XML_ELEMENT)
            return reject_unexpected_element(
                child, diagnostic,
                "unsupported VoiceXML submit child element");
        return reject_non_element(child, diagnostic);
    }
    if (measurement->action_count >= limits->max_actions ||
        !checked_add(
            measurement->action_count, 1u,
            &measurement->action_count))
        return fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(node),
            "VoiceXML action count exceeds max_actions");
    return append_submit(
        measurement, next, method, enctype, namelist,
        limits, diagnostic);
}

static vxml_status append_external_script(
    vxml_measurement *measurement,
    salts_xml_attribute reference_attribute,
    bool dynamic_reference,
    salts_xml_attribute charset_attribute,
    const vxml_limits *limits,
    vxml_diagnostic *diagnostic) {
    static const char utf8[] = "UTF-8";
    vxml_decoded_script entry = {0};
    const salts_xml_string_view raw_reference =
        salts_xml_attribute_value(reference_attribute);
    size_t reference_size = 0u;
    size_t reference_retained;
    size_t charset_retained = sizeof(utf8);
    size_t allocation_size;
    char **destination;
    size_t *destination_size;

    if (reference_attribute.impl == NULL)
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            (salts_xml_location){0},
            "VoiceXML external script requires src or srcexpr");
    if (charset_attribute.impl != NULL &&
        !ascii_case_view_equal(
            salts_xml_attribute_value(charset_attribute), utf8))
        return fail(
            diagnostic, VXML_UNSUPPORTED_FEATURE,
            salts_xml_attribute_location(charset_attribute),
            "VoiceXML external script supports UTF-8 charset only");
    if (!decode_entities(
            raw_reference, NULL, 0u, &reference_size) ||
        reference_size == 0u ||
        !checked_add(
            reference_size, 1u, &reference_retained))
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(reference_attribute),
            dynamic_reference
                ? "VoiceXML script srcexpr must be nonempty"
                : "VoiceXML script src must be one nonempty URI");
    if (!checked_add(
            measurement->name_bytes, reference_retained,
            &measurement->name_bytes) ||
        !checked_add(
            measurement->name_bytes, charset_retained,
            &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes)
        return fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_attribute_location(reference_attribute),
            "VoiceXML retained script metadata exceeds max_name_bytes");

    destination =
        dynamic_reference ? &entry.srcexpr : &entry.src;
    destination_size =
        dynamic_reference
            ? &entry.srcexpr_size : &entry.src_size;
    *destination = (char *)vxml_malloc(reference_retained);
    entry.charset = (char *)vxml_malloc(charset_retained);
    if (*destination == NULL || entry.charset == NULL) {
        vxml_free(entry.charset);
        vxml_free(*destination);
        return fail(
            diagnostic, VXML_ALLOCATION_FAILED,
            salts_xml_attribute_location(reference_attribute),
            "VoiceXML script metadata allocation failed");
    }
    if (!decode_entities(
            raw_reference, *destination,
            reference_size, &reference_size)) {
        vxml_free(entry.charset);
        vxml_free(*destination);
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(reference_attribute),
            "VoiceXML script reference decoding changed between passes");
    }
    (*destination)[reference_size] = '\0';
    *destination_size = reference_size;
    memcpy(entry.charset, utf8, sizeof(utf8));
    entry.charset_size = sizeof(utf8) - 1u;
    entry.location =
        salts_xml_attribute_location(reference_attribute);

    if (measurement->script_count ==
        measurement->script_capacity) {
        size_t capacity =
            measurement->script_capacity == 0u
                ? 4u
                : measurement->script_capacity * 2u;
        vxml_decoded_script *rows;
        if (capacity < measurement->script_capacity ||
            capacity > limits->max_actions)
            capacity = limits->max_actions;
        if (capacity <= measurement->script_capacity ||
            !checked_multiply(
                capacity, sizeof(*rows),
                &allocation_size)) {
            vxml_free(entry.charset);
            vxml_free(entry.srcexpr);
            vxml_free(entry.src);
            return fail(
                diagnostic, VXML_LIMIT_EXCEEDED,
                entry.location,
                "VoiceXML temporary script table size overflow");
        }
        rows = (vxml_decoded_script *)vxml_realloc(
            measurement->scripts, allocation_size);
        if (rows == NULL) {
            vxml_free(entry.charset);
            vxml_free(entry.srcexpr);
            vxml_free(entry.src);
            return fail(
                diagnostic, VXML_ALLOCATION_FAILED,
                entry.location,
                "VoiceXML temporary script table allocation failed");
        }
        measurement->scripts = rows;
        measurement->script_capacity = capacity;
    }
    measurement->scripts[
        measurement->script_count++] = entry;
    return VXML_OK;
}

static vxml_status measure_external_script(
    salts_xml_node node,
    vxml_measurement *measurement,
    const vxml_limits *limits,
    vxml_diagnostic *diagnostic) {
    salts_xml_attribute src = {0};
    salts_xml_attribute srcexpr = {0};
    salts_xml_attribute charset = {0};
    size_t index;
    bool has_inline = false;

    if ((measurement->features &
         VXML_COMPILE_FEATURE_EXTERNAL_SCRIPT) == 0u)
        return fail(
            diagnostic, VXML_UNSUPPORTED_FEATURE,
            salts_xml_node_location(node),
            "VoiceXML script requires an explicit script profile");

    for (index = 0u;
         index < salts_xml_node_attribute_count(node);
         ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(node, index);
        const salts_xml_string_view local =
            salts_xml_attribute_local_name(attribute);
        if (salts_xml_attribute_namespace_uri(attribute).size != 0u)
            return fail(
                diagnostic, VXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported VoiceXML script attribute");
        if (view_equal(local, "src")) {
            if (src.impl != NULL)
                return fail(
                    diagnostic, VXML_UNSUPPORTED_FEATURE,
                    salts_xml_attribute_location(attribute),
                    "duplicate VoiceXML script src");
            src = attribute;
        } else if (view_equal(local, "srcexpr")) {
            if ((measurement->features &
                 VXML_COMPILE_FEATURE_SCRIPT_SRCEXPR) == 0u)
                return fail(
                    diagnostic, VXML_UNSUPPORTED_FEATURE,
                    salts_xml_attribute_location(attribute),
                    "VoiceXML script srcexpr requires the QuickJS profile");
            if (srcexpr.impl != NULL)
                return fail(
                    diagnostic, VXML_UNSUPPORTED_FEATURE,
                    salts_xml_attribute_location(attribute),
                    "duplicate VoiceXML script srcexpr");
            srcexpr = attribute;
        } else if (view_equal(local, "charset")) {
            if (charset.impl != NULL)
                return fail(
                    diagnostic, VXML_UNSUPPORTED_FEATURE,
                    salts_xml_attribute_location(attribute),
                    "duplicate VoiceXML script charset");
            charset = attribute;
        } else {
            return fail(
                diagnostic, VXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported VoiceXML script attribute");
        }
    }

    for (index = 0u;
         index < salts_xml_node_child_count(node);
         ++index) {
        const salts_xml_node child =
            salts_xml_node_child_at(node, index);
        if (!node_is_ignorable(child)) {
            has_inline = true;
            break;
        }
    }

    if (src.impl != NULL && srcexpr.impl != NULL)
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_node_location(node),
            "VoiceXML script requires exactly one of src or srcexpr");
    if (src.impl == NULL && srcexpr.impl == NULL)
        return fail(
            diagnostic,
            has_inline ? VXML_UNSUPPORTED_FEATURE
                       : VXML_INVALID_STRUCTURE,
            salts_xml_node_location(node),
            has_inline
                ? "inline VoiceXML script execution is outside this profile"
                : "VoiceXML script requires src, srcexpr, or inline content");
    if (has_inline)
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_node_location(node),
            "VoiceXML script cannot combine external reference with inline content");
    if (measurement->action_count >= limits->max_actions ||
        !checked_add(
            measurement->action_count, 1u,
            &measurement->action_count))
        return fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(node),
            "VoiceXML action count exceeds max_actions");
    return append_external_script(
        measurement,
        srcexpr.impl != NULL ? srcexpr : src,
        srcexpr.impl != NULL,
        charset, limits, diagnostic);
}

static vxml_status resolve_gotos(
    vxml_measurement *measurement,
    vxml_diagnostic *diagnostic) {
    size_t goto_index;
    if (measurement == NULL ||
        measurement->id_count != measurement->form_count)
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            (salts_xml_location){0},
            "VoiceXML form metadata is inconsistent");
    for (goto_index = 0u;
         goto_index < measurement->goto_count;
         ++goto_index) {
        vxml_decoded_goto *entry =
            &measurement->gotos[goto_index];
        size_t form_index;
        if (entry->external)
            continue;
        for (form_index = 0u;
             form_index < measurement->id_count;
             ++form_index) {
            const vxml_decoded_id id =
                measurement->ids[form_index];
            if (id.data != NULL &&
                id.size == entry->target_size &&
                memcmp(
                    id.data, entry->target,
                    entry->target_size) == 0) {
                entry->target_form = form_index;
                break;
            }
        }
        if (entry->target_form == SIZE_MAX)
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                entry->location,
                "VoiceXML goto target form does not exist");
    }
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

static vxml_status measure_goto(
    salts_xml_node node, vxml_measurement *measurement,
    const vxml_limits *limits, vxml_diagnostic *diagnostic) {
    salts_xml_attribute next;
    salts_xml_attribute fetchaudio;
    size_t index;
    vxml_status status = validate_goto_attributes(node, diagnostic);
    if (status != VXML_OK) return status;
    next = unqualified_attribute(node, "next");
    fetchaudio = unqualified_attribute(node, "fetchaudio");
    if (next.impl == NULL)
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_node_location(node),
            "VoiceXML goto requires next");
    for (index = 0u; index < salts_xml_node_child_count(node); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(node, index);
        if (node_is_ignorable(child)) continue;
        if (salts_xml_node_type(child) == SALTS_XML_ELEMENT)
            return reject_unexpected_element(
                child, diagnostic, "unsupported VoiceXML goto child element");
        return reject_non_element(child, diagnostic);
    }
    if (measurement->action_count >= limits->max_actions ||
        !checked_add(measurement->action_count, 1u,
                     &measurement->action_count))
        return fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(node),
            "VoiceXML action count exceeds max_actions");
    return append_goto(
        measurement, next, fetchaudio, limits, diagnostic);
}

static vxml_status measure_block(
    salts_xml_node node, vxml_measurement *measurement,
    const vxml_limits *limits, vxml_diagnostic *diagnostic,
    const vxml_decoded_data_policy *data_policy,
    size_t owner_form) {
    size_t index;
    size_t actions = 0u;
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
        const salts_xml_string_view local_name =
            salts_xml_node_local_name(child);
        if (node_is_ignorable(child)) continue;
        if (salts_xml_node_type(child) != SALTS_XML_ELEMENT)
            return reject_non_element(child, diagnostic);
        if (!normalized_view_equal(
                salts_xml_node_namespace_uri(child), VXML_NAMESPACE) ||
            (!view_equal(local_name, "exit") &&
             !view_equal(local_name, "goto") &&
             !view_equal(local_name, "submit") &&
             !view_equal(local_name, "script") &&
             !(data_feature_enabled(measurement) &&
               view_equal(local_name, "data"))))
            return reject_unexpected_element(
                child, diagnostic, "unsupported VoiceXML block child element");
        if (actions != 0u)
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_node_location(child),
                "VoiceXML literal block accepts at most one transfer action");
        ++actions;
        if (view_equal(local_name, "goto"))
            status = measure_goto(
                child, measurement, limits, diagnostic);
        else if (view_equal(local_name, "submit"))
            status = measure_submit(
                child, measurement, limits, diagnostic);
        else if (view_equal(local_name, "script"))
            status = measure_external_script(
                child, measurement, limits, diagnostic);
        else if (view_equal(local_name, "data")) {
            if (measurement->action_count >= limits->max_actions ||
                !checked_add(
                    measurement->action_count, 1u,
                    &measurement->action_count))
                return fail(
                    diagnostic, VXML_LIMIT_EXCEEDED,
                    salts_xml_node_location(child),
                    "VoiceXML action count exceeds max_actions");
            status = append_data_row(
                measurement, child,
                VXML_DATA_EXECUTABLE, owner_form,
                data_policy, limits, diagnostic);
        } else {
            status = measure_exit(
                child, measurement, limits, diagnostic);
        }
        if (status != VXML_OK) return status;
    }
    return VXML_OK;
}

static vxml_status measure_form(
    salts_xml_node node, vxml_measurement *measurement,
    const vxml_limits *limits, vxml_diagnostic *diagnostic,
    const vxml_decoded_data_policy *document_data_policy) {
    const size_t first_block = measurement->block_count;
    const size_t form_index = measurement->form_count;
    vxml_decoded_data_policy data_policy = {0};
    vxml_data_property_seen data_seen = {0};
    salts_xml_attribute id_attribute;
    size_t index;
    vxml_status status = validate_attributes(node, "id", diagnostic);
    if (status != VXML_OK) return status;
    status = clone_policy_scope(
        document_data_policy, &data_policy,
        salts_xml_node_location(node), diagnostic);
    if (status != VXML_OK) return status;
    if (measurement->form_count >= limits->max_forms ||
        !checked_add(measurement->form_count, 1u,
                     &measurement->form_count)) {
        decoded_data_policy_destroy(&data_policy);
        return fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(node),
            "VoiceXML form count exceeds max_forms");
    }
    id_attribute = unqualified_attribute(node, "id");
    status = append_id(measurement, id_attribute, limits, diagnostic);
    if (status != VXML_OK) goto done;
    for (index = 0u; index < salts_xml_node_child_count(node); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(node, index);
        const salts_xml_string_view local_name =
            salts_xml_node_local_name(child);
        if (node_is_ignorable(child)) continue;
        if (salts_xml_node_type(child) != SALTS_XML_ELEMENT) {
            status = reject_non_element(child, diagnostic);
            goto done;
        }
        if (!normalized_view_equal(
                salts_xml_node_namespace_uri(child), VXML_NAMESPACE)) {
            status = reject_unexpected_element(
                child, diagnostic,
                "unsupported VoiceXML form child element");
            goto done;
        }
        if (data_feature_enabled(measurement) &&
            view_equal(local_name, "property")) {
            status = apply_data_property(
                child, measurement->version_21,
                &data_policy, &data_seen, diagnostic);
        } else if (data_feature_enabled(measurement) &&
                   view_equal(local_name, "data")) {
            status = append_data_row(
                measurement, child,
                VXML_DATA_FORM, form_index,
                &data_policy, limits, diagnostic);
        } else if (view_equal(local_name, "block")) {
            status = measure_block(
                child, measurement, limits, diagnostic,
                &data_policy, form_index);
        } else {
            status = reject_unexpected_element(
                child, diagnostic,
                "unsupported VoiceXML form child element");
        }
        if (status != VXML_OK) goto done;
    }
    if (measurement->block_count == first_block) {
        status = fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_node_location(node),
            "VoiceXML form requires at least one block");
        goto done;
    }
    status = VXML_OK;

done:
    decoded_data_policy_destroy(&data_policy);
    return status;
}

static vxml_status measure_document(
    salts_xml_node root, vxml_measurement *measurement,
    const vxml_limits *limits, vxml_diagnostic *diagnostic) {
    const salts_xml_attribute version = unqualified_attribute(root, "version");
    vxml_decoded_data_policy data_policy = {0};
    vxml_data_property_seen data_seen = {0};
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
    measurement->version_21 = normalized_view_equal(
        salts_xml_attribute_value(version), "2.1");

    for (index = 0u; index < salts_xml_node_child_count(root); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(root, index);
        const salts_xml_string_view local_name =
            salts_xml_node_local_name(child);
        if (node_is_ignorable(child)) continue;
        if (salts_xml_node_type(child) != SALTS_XML_ELEMENT) {
            status = reject_non_element(child, diagnostic);
            goto done;
        }
        if (!normalized_view_equal(
                salts_xml_node_namespace_uri(child), VXML_NAMESPACE)) {
            status = reject_unexpected_element(
                child, diagnostic,
                "unsupported VoiceXML root child element");
            goto done;
        }
        if (data_feature_enabled(measurement) &&
            view_equal(local_name, "property")) {
            status = apply_data_property(
                child, measurement->version_21,
                &data_policy, &data_seen, diagnostic);
        } else if (data_feature_enabled(measurement) &&
                   view_equal(local_name, "data")) {
            status = append_data_row(
                measurement, child,
                VXML_DATA_DOCUMENT, SIZE_MAX,
                &data_policy, limits, diagnostic);
        } else if (view_equal(local_name, "form")) {
            status = measure_form(
                child, measurement, limits, diagnostic,
                &data_policy);
        } else {
            status = reject_unexpected_element(
                child, diagnostic,
                "unsupported VoiceXML root child element");
        }
        if (status != VXML_OK) goto done;
    }
    if (measurement->form_count == 0u) {
        status = fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_node_location(root),
            "VoiceXML document requires at least one form");
        goto done;
    }
    status = VXML_OK;

done:
    decoded_data_policy_destroy(&data_policy);
    return status;
}

static bool measure_allocation(
    const vxml_measurement *measurement, size_t *forms_offset,
    size_t *blocks_offset, size_t *actions_offset,
    size_t *data_offset, size_t *storage_offset,
    size_t *allocation_size) {
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
        !checked_add(cursor, bytes, &cursor) ||
        !checked_align(cursor, _Alignof(vxml_data_row), &cursor))
        return false;
    *data_offset = cursor;
    if (!checked_multiply(
            measurement->data_count, sizeof(vxml_data_row), &bytes) ||
        !checked_add(cursor, bytes, &cursor))
        return false;
    *storage_offset = cursor;
    if (!checked_add(cursor, measurement->name_bytes, allocation_size))
        return false;
    return true;
}

static void write_exit(vxml_writer *writer) {
    vxml_action_row *action =
        &writer->impl->actions[writer->action_index++];
    action->kind = VXML_ACTION_EXIT;
    action->target_form = SIZE_MAX;
    action->target_uri = NULL;
    action->target_uri_size = 0u;
    action->fetchaudio_uri = NULL;
    action->fetchaudio_uri_size = 0u;
    action->data_index = SIZE_MAX;
}

static void write_external_script(vxml_writer *writer) {
    vxml_action_row *action =
        &writer->impl->actions[writer->action_index++];
    const vxml_decoded_script script =
        writer->measurement->scripts[writer->script_index++];
    action->kind = VXML_ACTION_SCRIPT_EXTERNAL;
    action->target_form = SIZE_MAX;
    action->target_uri = NULL;
    action->target_uri_size = 0u;
    action->fetchaudio_uri = NULL;
    action->fetchaudio_uri_size = 0u;
    action->submit_method = 0;
    action->submit_enctype = 0;
    action->script_src = NULL;
    action->script_src_size = 0u;
    action->script_srcexpr = NULL;
    action->script_srcexpr_size = 0u;
    if (script.srcexpr != NULL) {
        action->script_srcexpr =
            writer->impl->storage + writer->storage_index;
        action->script_srcexpr_size = script.srcexpr_size;
        memcpy(
            writer->impl->storage + writer->storage_index,
            script.srcexpr, script.srcexpr_size + 1u);
        writer->storage_index += script.srcexpr_size + 1u;
    } else {
        action->script_src =
            writer->impl->storage + writer->storage_index;
        action->script_src_size = script.src_size;
        memcpy(
            writer->impl->storage + writer->storage_index,
            script.src, script.src_size + 1u);
        writer->storage_index += script.src_size + 1u;
    }
    action->script_location = script.location;
    action->script_charset =
        writer->impl->storage + writer->storage_index;
    action->script_charset_size = script.charset_size;
    action->data_index = SIZE_MAX;
    memcpy(
        writer->impl->storage + writer->storage_index,
        script.charset, script.charset_size + 1u);
    writer->storage_index += script.charset_size + 1u;
}

static void write_submit(vxml_writer *writer) {
    vxml_action_row *action =
        &writer->impl->actions[writer->action_index++];
    const vxml_decoded_submit submit =
        writer->measurement->submits[writer->submit_index++];
    action->kind = VXML_ACTION_SUBMIT;
    action->target_form = SIZE_MAX;
    action->target_uri =
        writer->impl->storage + writer->storage_index;
    action->target_uri_size = submit.target_size;
    action->fetchaudio_uri = NULL;
    action->fetchaudio_uri_size = 0u;
    action->submit_method = submit.method;
    action->submit_enctype = submit.enctype;
    action->data_index = SIZE_MAX;
    memcpy(
        writer->impl->storage + writer->storage_index,
        submit.target, submit.target_size + 1u);
    writer->storage_index += submit.target_size + 1u;
}

static void write_goto(vxml_writer *writer) {
    vxml_action_row *action =
        &writer->impl->actions[writer->action_index++];
    const vxml_decoded_goto target =
        writer->measurement->gotos[writer->goto_index++];
    action->target_uri = NULL;
    action->target_uri_size = 0u;
    action->fetchaudio_uri = NULL;
    action->fetchaudio_uri_size = 0u;
    action->data_index = SIZE_MAX;
    if (target.external) {
        action->kind = VXML_ACTION_GOTO_EXTERNAL;
        action->target_form = SIZE_MAX;
        action->target_uri =
            writer->impl->storage + writer->storage_index;
        action->target_uri_size = target.target_size;
        memcpy(
            writer->impl->storage + writer->storage_index,
            target.target, target.target_size + 1u);
        writer->storage_index += target.target_size + 1u;
        if (target.fetchaudio != NULL &&
            target.fetchaudio_size != 0u) {
            action->fetchaudio_uri =
                writer->impl->storage + writer->storage_index;
            action->fetchaudio_uri_size =
                target.fetchaudio_size;
            memcpy(
                writer->impl->storage + writer->storage_index,
                target.fetchaudio, target.fetchaudio_size + 1u);
            writer->storage_index +=
                target.fetchaudio_size + 1u;
        }
    } else {
        action->kind = VXML_ACTION_GOTO;
        action->target_form = target.target_form;
    }
}

static void write_data_descriptor(vxml_writer *writer) {
    const vxml_decoded_data source =
        writer->measurement->data_rows[writer->data_index];
    vxml_data_row *row =
        &writer->impl->data_rows[writer->data_index++];
#define VXML_COPY_DATA_STRING(FIELD) do { \
    row->FIELD = NULL; \
    row->FIELD##_size = source.FIELD##_size; \
    if (source.FIELD != NULL) { \
        row->FIELD = writer->impl->storage + writer->storage_index; \
        memcpy( \
            writer->impl->storage + writer->storage_index, \
            source.FIELD, source.FIELD##_size + 1u); \
        writer->storage_index += source.FIELD##_size + 1u; \
    } \
} while (0)
    memset(row, 0, sizeof(*row));
    row->placement = source.placement;
    row->owner_form = source.owner_form;
    row->method = source.method;
    row->enctype = source.enctype;
    row->namelist_count = source.namelist_count;
    row->location = source.location;
    row->fetch_policy.has_fetchaudio_delay =
        source.fetch_policy.has_fetchaudio_delay;
    row->fetch_policy.fetchaudio_delay_us =
        source.fetch_policy.fetchaudio_delay_us;
    row->fetch_policy.has_fetchaudio_minimum =
        source.fetch_policy.has_fetchaudio_minimum;
    row->fetch_policy.fetchaudio_minimum_us =
        source.fetch_policy.fetchaudio_minimum_us;
    row->fetch_policy.has_timeout =
        source.fetch_policy.has_timeout;
    row->fetch_policy.timeout_us =
        source.fetch_policy.timeout_us;
    row->fetch_policy.fetch_hint =
        source.fetch_policy.fetch_hint;
    row->fetch_policy.has_max_age =
        source.fetch_policy.has_max_age;
    row->fetch_policy.max_age_seconds =
        source.fetch_policy.max_age_seconds;
    row->fetch_policy.has_max_stale =
        source.fetch_policy.has_max_stale;
    row->fetch_policy.max_stale_seconds =
        source.fetch_policy.max_stale_seconds;
    VXML_COPY_DATA_STRING(name);
    VXML_COPY_DATA_STRING(uri);
    VXML_COPY_DATA_STRING(uri_expression);
    VXML_COPY_DATA_STRING(namelist);
    row->fetch_policy.fetchaudio_uri = NULL;
    row->fetch_policy.fetchaudio_uri_size =
        source.fetch_policy.fetchaudio_uri_size;
    if (source.fetch_policy.fetchaudio_uri != NULL) {
        row->fetch_policy.fetchaudio_uri =
            writer->impl->storage + writer->storage_index;
        memcpy(
            writer->impl->storage + writer->storage_index,
            source.fetch_policy.fetchaudio_uri,
            source.fetch_policy.fetchaudio_uri_size + 1u);
        writer->storage_index +=
            source.fetch_policy.fetchaudio_uri_size + 1u;
    }
#undef VXML_COPY_DATA_STRING
}

static void write_data_action(vxml_writer *writer) {
    const size_t data_index = writer->data_index;
    vxml_action_row *action =
        &writer->impl->actions[writer->action_index++];
    memset(action, 0, sizeof(*action));
    action->kind = VXML_ACTION_DATA;
    action->target_form = SIZE_MAX;
    action->data_index = data_index;
    write_data_descriptor(writer);
}

static void write_block(vxml_writer *writer, salts_xml_node node) {
    vxml_block_row *row = &writer->impl->blocks[writer->block_index++];
    size_t index;
    row->first_action = writer->action_index;
    for (index = 0u; index < salts_xml_node_child_count(node); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(node, index);
        if (salts_xml_node_type(child) == SALTS_XML_ELEMENT) {
            if (view_equal(salts_xml_node_local_name(child), "goto"))
                write_goto(writer);
            else if (view_equal(
                         salts_xml_node_local_name(child), "submit"))
                write_submit(writer);
            else if (view_equal(
                         salts_xml_node_local_name(child), "script"))
                write_external_script(writer);
            else if (view_equal(
                         salts_xml_node_local_name(child), "data"))
                write_data_action(writer);
            else
                write_exit(writer);
        }
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
        const salts_xml_string_view local =
            salts_xml_node_local_name(child);
        if (salts_xml_node_type(child) != SALTS_XML_ELEMENT)
            continue;
        if (view_equal(local, "property"))
            continue;
        if (view_equal(local, "data"))
            write_data_descriptor(writer);
        else
            write_block(writer, child);
    }
    row->block_count = writer->block_index - row->first_block;
}

static void write_document(vxml_writer *writer, salts_xml_node root) {
    size_t index;
    for (index = 0u; index < salts_xml_node_child_count(root); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(root, index);
        const salts_xml_string_view local =
            salts_xml_node_local_name(child);
        if (salts_xml_node_type(child) != SALTS_XML_ELEMENT)
            continue;
        if (view_equal(local, "property"))
            continue;
        if (view_equal(local, "data"))
            write_data_descriptor(writer);
        else
            write_form(writer, child);
    }
}

static bool form_first_action(
    const vxml_program_impl *impl,
    size_t form_index,
    const vxml_action_row **out_action) {
    const vxml_form_row *form;
    size_t block_index;
    if (out_action == NULL || impl == NULL ||
        form_index >= impl->form_count)
        return false;
    *out_action = NULL;
    form = &impl->forms[form_index];
    if (form->block_count == 0u ||
        form->first_block > impl->block_count ||
        form->block_count > impl->block_count - form->first_block)
        return false;
    for (block_index = form->first_block;
         block_index < form->first_block + form->block_count;
         ++block_index) {
        const vxml_block_row *block = &impl->blocks[block_index];
        if (block->action_count > 1u ||
            block->first_action > impl->action_count ||
            block->action_count >
                impl->action_count - block->first_action)
            return false;
        if (block->action_count != 0u) {
            const vxml_action_row *action =
                &impl->actions[block->first_action];
            if (action->kind == VXML_ACTION_DATA)
                continue;
            *out_action = action;
            return true;
        }
    }
    return true;
}

static vxml_status validate_literal_goto_graph(
    const vxml_program_impl *impl,
    salts_xml_location location,
    vxml_diagnostic *diagnostic) {
    size_t start;
    if (impl == NULL || impl->forms == NULL ||
        impl->form_count == 0u)
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE, location,
            "VoiceXML literal Program has no forms");
    for (start = 0u; start < impl->form_count; ++start) {
        size_t current = start;
        size_t transitions = 0u;
        for (;;) {
            const vxml_action_row *action = NULL;
            if (!form_first_action(impl, current, &action))
                return fail(
                    diagnostic, VXML_INVALID_STRUCTURE, location,
                    "VoiceXML literal Program structure is invalid");
            if (action == NULL ||
                action->kind == VXML_ACTION_EXIT ||
                action->kind == VXML_ACTION_GOTO_EXTERNAL ||
                action->kind == VXML_ACTION_SUBMIT ||
                action->kind == VXML_ACTION_SCRIPT_EXTERNAL)
                break;
            if (action->kind != VXML_ACTION_GOTO ||
                action->target_form >= impl->form_count)
                return fail(
                    diagnostic, VXML_INVALID_STRUCTURE, location,
                    "VoiceXML literal goto target is invalid");
            current = action->target_form;
            ++transitions;
            if (transitions > impl->form_count)
                return fail(
                    diagnostic, VXML_INVALID_STRUCTURE, location,
                    "VoiceXML literal goto cycle is not supported");
        }
    }
    return VXML_OK;
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

vxml_status vxml_compile_with_features(
    const void *bytes, size_t size,
    const vxml_limits *limits,
    uint64_t features,
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
    size_t data_offset;
    size_t storage_offset;
    size_t allocation_size;
    if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
    if (out != NULL) out->impl = NULL;
    if ((features &
         ~(VXML_COMPILE_FEATURE_EXTERNAL_SCRIPT |
           VXML_COMPILE_FEATURE_SCRIPT_SRCEXPR |
           VXML_COMPILE_FEATURE_DATA_REQUEST)) != 0u)
        return fail(
            diagnostic, VXML_INVALID_ARGUMENT,
            (salts_xml_location){0},
            "unsupported VoiceXML compile feature");
    measurement.features = features;
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
    status = resolve_gotos(&measurement, diagnostic);
    if (status != VXML_OK) goto cleanup;
    if (!measure_allocation(
            &measurement, &forms_offset, &blocks_offset, &actions_offset,
            &data_offset, &storage_offset, &allocation_size)) {
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
    impl->data_rows = (vxml_data_row *)((char *)impl + data_offset);
    impl->storage = (char *)impl + storage_offset;
    impl->form_count = measurement.form_count;
    impl->block_count = measurement.block_count;
    impl->action_count = measurement.action_count;
    impl->data_row_count = measurement.data_count;
    impl->storage_size = measurement.name_bytes;
    impl->allocation_size = allocation_size;
    writer.impl = impl;
    writer.measurement = &measurement;
    write_document(&writer, salts_xml_document_root(&document));
    if (writer.form_index != measurement.form_count ||
        writer.block_index != measurement.block_count ||
        writer.action_index != measurement.action_count ||
        writer.goto_index != measurement.goto_count ||
        writer.submit_index != measurement.submit_count ||
        writer.script_index != measurement.script_count ||
        writer.data_index != measurement.data_count ||
        writer.storage_index != measurement.name_bytes) {
        status = fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_node_location(salts_xml_document_root(&document)),
            "VoiceXML document changed between measurement and build");
        goto cleanup;
    }
    status = validate_literal_goto_graph(
        impl,
        salts_xml_node_location(salts_xml_document_root(&document)),
        diagnostic);
    if (status != VXML_OK) goto cleanup;
    out->impl = impl;
    impl = NULL;
    status = VXML_OK;

cleanup:
    vxml_free(impl);
    measurement_destroy(&measurement);
    salts_xml_document_destroy(&document);
    return status;
}

vxml_status vxml_compile(
    const void *bytes, size_t size,
    const vxml_limits *limits,
    vxml_program *out,
    vxml_diagnostic *diagnostic) {
    return vxml_compile_with_features(
        bytes, size, limits, UINT64_C(0), out, diagnostic);
}

void vxml_program_destroy(vxml_program *program) {
    if (program == NULL || program->impl == NULL) return;
    {
        vxml_program_impl *impl = (vxml_program_impl *)program->impl;
        if (impl->profile_program_destroy != NULL)
            impl->profile_program_destroy(impl);
    }
    vxml_free(program->impl);
    program->impl = NULL;
}
