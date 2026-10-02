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

typedef struct vxml_literal_fetch_audio_policy {
    salts_xml_attribute uri_value;
    bool has_delay;
    uint64_t delay_us;
    bool has_minimum;
    uint64_t minimum_us;
} vxml_literal_fetch_audio_policy;

typedef struct vxml_decoded_goto {
    char *target;
    size_t target_size;
    char *fetchaudio;
    size_t fetchaudio_size;
    bool has_fetchaudio_delay;
    uint64_t fetchaudio_delay_us;
    bool has_fetchaudio_minimum;
    uint64_t fetchaudio_minimum_us;
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
    char *charset;
    size_t charset_size;
    salts_xml_location location;
} vxml_decoded_script;

typedef struct vxml_measurement {
    uint64_t features;
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
            view_equal(local_name, "script") ||
            view_equal(local_name, "property"));
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

static vxml_status parse_time_designation(
    salts_xml_attribute attribute,
    uint64_t *out_us,
    vxml_diagnostic *diagnostic) {
    const salts_xml_string_view raw =
        salts_xml_attribute_value(attribute);
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
    vxml_status status = VXML_OK;

    if (attribute.impl == NULL || out_us == NULL)
        return VXML_INVALID_ARGUMENT;
    *out_us = UINT64_C(0);

    if (!decode_entities(raw, NULL, 0u, &decoded_size) ||
        decoded_size < 2u)
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(attribute),
            "VoiceXML property requires a non-negative time designation");
    decoded = (char *)vxml_malloc(decoded_size + 1u);
    if (decoded == NULL)
        return fail(
            diagnostic, VXML_ALLOCATION_FAILED,
            salts_xml_attribute_location(attribute),
            "VoiceXML time designation allocation failed");
    if (!decode_entities(
            raw, decoded, decoded_size, &decoded_size)) {
        status = fail(
            diagnostic, VXML_XML_ERROR,
            salts_xml_attribute_location(attribute),
            "VoiceXML time designation decoding changed");
        goto done;
    }
    decoded[decoded_size] = '\0';

    if (decoded_size >= 2u &&
        decoded[decoded_size - 2u] == 'm' &&
        decoded[decoded_size - 1u] == 's') {
        unit_us = UINT64_C(1000);
        fractional_limit = 3u;
        number_size = decoded_size - 2u;
    } else if (decoded[decoded_size - 1u] == 's') {
        unit_us = UINT64_C(1000000);
        fractional_limit = 6u;
        number_size = decoded_size - 1u;
    } else {
        status = fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(attribute),
            "VoiceXML time designation requires ms or s units");
        goto done;
    }

    if (number_size == 0u) {
        status = fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(attribute),
            "VoiceXML time designation is missing its numeric value");
        goto done;
    }

    while (cursor < number_size) {
        const unsigned char ch =
            (unsigned char)decoded[cursor++];
        if (ch == '.') {
            if (saw_decimal || !saw_digit ||
                cursor == number_size) {
                status = fail(
                    diagnostic, VXML_INVALID_STRUCTURE,
                    salts_xml_attribute_location(attribute),
                    "VoiceXML time designation has an invalid decimal form");
                goto done;
            }
            saw_decimal = true;
            continue;
        }
        if (ch < '0' || ch > '9') {
            status = fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(attribute),
                "VoiceXML time designation must be a non-negative decimal");
            goto done;
        }
        saw_digit = true;
        if (!saw_decimal) {
            const uint64_t digit = (uint64_t)(ch - '0');
            if (integer_part >
                (UINT64_MAX - digit) / UINT64_C(10)) {
                status = fail(
                    diagnostic, VXML_LIMIT_EXCEEDED,
                    salts_xml_attribute_location(attribute),
                    "VoiceXML time designation overflows");
                goto done;
            }
            integer_part =
                integer_part * UINT64_C(10) + digit;
        } else {
            if (fractional_digits >= fractional_limit) {
                status = fail(
                    diagnostic, VXML_LIMIT_EXCEEDED,
                    salts_xml_attribute_location(attribute),
                    "VoiceXML time designation exceeds microsecond precision");
                goto done;
            }
            fractional_part =
                fractional_part * UINT64_C(10) +
                (uint64_t)(ch - '0');
            ++fractional_digits;
        }
    }

    if (!saw_digit ||
        integer_part > UINT64_MAX / unit_us) {
        status = fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_attribute_location(attribute),
            "VoiceXML time designation exceeds uint64 microseconds");
        goto done;
    }

    *out_us = integer_part * unit_us;
    if (fractional_digits != 0u) {
        uint64_t scale = unit_us;
        size_t index;
        for (index = 0u; index < fractional_digits; ++index)
            scale /= UINT64_C(10);
        if (fractional_part >
            (UINT64_MAX - *out_us) / scale) {
            status = fail(
                diagnostic, VXML_LIMIT_EXCEEDED,
                salts_xml_attribute_location(attribute),
                "VoiceXML time designation exceeds uint64 microseconds");
            goto done;
        }
        *out_us += fractional_part * scale;
    }

done:
    vxml_free(decoded);
    return status;
}

static vxml_status validate_property_attributes(
    salts_xml_node node,
    vxml_diagnostic *diagnostic) {
    bool seen_name = false;
    bool seen_value = false;
    size_t index;

    for (index = 0u;
         index < salts_xml_node_attribute_count(node);
         ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(node, index);
        const salts_xml_string_view local =
            salts_xml_attribute_local_name(attribute);
        bool *seen = NULL;

        if (salts_xml_attribute_namespace_uri(attribute).size == 0u &&
            view_equal(local, "name"))
            seen = &seen_name;
        else if (
            salts_xml_attribute_namespace_uri(attribute).size == 0u &&
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

    for (index = 0u;
         index < salts_xml_node_child_count(node);
         ++index) {
        const salts_xml_node child =
            salts_xml_node_child_at(node, index);
        if (!node_is_ignorable(child))
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_node_location(child),
                "VoiceXML property must be empty");
    }
    return VXML_OK;
}

static vxml_status apply_fetch_audio_property(
    salts_xml_node node,
    vxml_literal_fetch_audio_policy *policy,
    bool *saw_uri,
    bool *saw_delay,
    bool *saw_minimum,
    vxml_diagnostic *diagnostic) {
    const salts_xml_attribute name =
        unqualified_attribute(node, "name");
    const salts_xml_attribute value =
        unqualified_attribute(node, "value");
    vxml_status status;

    if (policy == NULL || saw_uri == NULL ||
        saw_delay == NULL || saw_minimum == NULL)
        return VXML_INVALID_ARGUMENT;

    status = validate_property_attributes(node, diagnostic);
    if (status != VXML_OK) return status;

    if (normalized_view_equal(
            salts_xml_attribute_value(name), "fetchaudio")) {
        size_t decoded_size = 0u;
        if (*saw_uri)
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(name),
                "duplicate VoiceXML fetchaudio property");
        *saw_uri = true;
        if (!decode_entities(
                salts_xml_attribute_value(value),
                NULL, 0u, &decoded_size) ||
            decoded_size == 0u)
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(value),
                "VoiceXML fetchaudio must be one nonempty URI");
        policy->uri_value = value;
        return VXML_OK;
    }

    if (normalized_view_equal(
            salts_xml_attribute_value(name),
            "fetchaudiodelay")) {
        if (*saw_delay)
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(name),
                "duplicate VoiceXML fetchaudiodelay property");
        *saw_delay = true;
        status = parse_time_designation(
            value, &policy->delay_us, diagnostic);
        if (status == VXML_OK)
            policy->has_delay = true;
        return status;
    }

    if (normalized_view_equal(
            salts_xml_attribute_value(name),
            "fetchaudiominimum")) {
        if (*saw_minimum)
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(name),
                "duplicate VoiceXML fetchaudiominimum property");
        *saw_minimum = true;
        status = parse_time_designation(
            value, &policy->minimum_us, diagnostic);
        if (status == VXML_OK)
            policy->has_minimum = true;
        return status;
    }

    return fail(
        diagnostic, VXML_UNSUPPORTED_FEATURE,
        salts_xml_attribute_location(name),
        "unsupported VoiceXML property in literal profile");
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
        vxml_free(measurement->scripts[index].charset);
    }
    vxml_free(measurement->ids);
    vxml_free(measurement->gotos);
    vxml_free(measurement->submits);
    vxml_free(measurement->scripts);
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
    const vxml_literal_fetch_audio_policy *inherited,
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

    if (fetchaudio_attribute.impl == NULL &&
        inherited != NULL &&
        inherited->uri_value.impl != NULL)
        fetchaudio_attribute = inherited->uri_value;

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
            if (inherited != NULL) {
                entry.has_fetchaudio_delay =
                    inherited->has_delay;
                entry.fetchaudio_delay_us =
                    inherited->delay_us;
                entry.has_fetchaudio_minimum =
                    inherited->has_minimum;
                entry.fetchaudio_minimum_us =
                    inherited->minimum_us;
            }
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
    salts_xml_attribute src_attribute,
    salts_xml_attribute charset_attribute,
    const vxml_limits *limits,
    vxml_diagnostic *diagnostic) {
    static const char utf8[] = "UTF-8";
    vxml_decoded_script entry = {0};
    const salts_xml_string_view raw_src =
        salts_xml_attribute_value(src_attribute);
    size_t src_size = 0u;
    size_t src_retained;
    size_t charset_retained = sizeof(utf8);
    size_t allocation_size;

    if (src_attribute.impl == NULL)
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            (salts_xml_location){0},
            "VoiceXML external script requires src");
    if (charset_attribute.impl != NULL &&
        !ascii_case_view_equal(
            salts_xml_attribute_value(charset_attribute), utf8))
        return fail(
            diagnostic, VXML_UNSUPPORTED_FEATURE,
            salts_xml_attribute_location(charset_attribute),
            "VoiceXML external script supports UTF-8 charset only");
    if (!decode_entities(raw_src, NULL, 0u, &src_size) ||
        src_size == 0u ||
        !checked_add(src_size, 1u, &src_retained))
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(src_attribute),
            "VoiceXML script src must be one nonempty URI");
    if (!checked_add(
            measurement->name_bytes, src_retained,
            &measurement->name_bytes) ||
        !checked_add(
            measurement->name_bytes, charset_retained,
            &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes)
        return fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_attribute_location(src_attribute),
            "VoiceXML retained script metadata exceeds max_name_bytes");

    entry.src = (char *)vxml_malloc(src_retained);
    entry.charset = (char *)vxml_malloc(charset_retained);
    if (entry.src == NULL || entry.charset == NULL) {
        vxml_free(entry.charset);
        vxml_free(entry.src);
        return fail(
            diagnostic, VXML_ALLOCATION_FAILED,
            salts_xml_attribute_location(src_attribute),
            "VoiceXML script metadata allocation failed");
    }
    if (!decode_entities(
            raw_src, entry.src, src_size, &src_size)) {
        vxml_free(entry.charset);
        vxml_free(entry.src);
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(src_attribute),
            "VoiceXML script src decoding changed between passes");
    }
    entry.src[src_size] = '\0';
    entry.src_size = src_size;
    memcpy(entry.charset, utf8, sizeof(utf8));
    entry.charset_size = sizeof(utf8) - 1u;
    entry.location = salts_xml_attribute_location(src_attribute);

    if (measurement->script_count == measurement->script_capacity) {
        size_t capacity = measurement->script_capacity == 0u
            ? 4u : measurement->script_capacity * 2u;
        vxml_decoded_script *rows;
        if (capacity < measurement->script_capacity ||
            capacity > limits->max_actions)
            capacity = limits->max_actions;
        if (capacity <= measurement->script_capacity ||
            !checked_multiply(
                capacity, sizeof(*rows), &allocation_size)) {
            vxml_free(entry.charset);
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
            vxml_free(entry.src);
            return fail(
                diagnostic, VXML_ALLOCATION_FAILED,
                entry.location,
                "VoiceXML temporary script table allocation failed");
        }
        measurement->scripts = rows;
        measurement->script_capacity = capacity;
    }
    measurement->scripts[measurement->script_count++] = entry;
    return VXML_OK;
}

static vxml_status measure_external_script(
    salts_xml_node node,
    vxml_measurement *measurement,
    const vxml_limits *limits,
    vxml_diagnostic *diagnostic) {
    salts_xml_attribute src = {0};
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
    if (src.impl == NULL)
        return fail(
            diagnostic,
            has_inline ? VXML_UNSUPPORTED_FEATURE
                       : VXML_INVALID_STRUCTURE,
            salts_xml_node_location(node),
            has_inline
                ? "inline VoiceXML script execution is outside the external-script profile"
                : "VoiceXML script requires src or inline content");
    if (has_inline)
        return fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_node_location(node),
            "VoiceXML script cannot combine src with inline content");
    if (measurement->action_count >= limits->max_actions ||
        !checked_add(
            measurement->action_count, 1u,
            &measurement->action_count))
        return fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(node),
            "VoiceXML action count exceeds max_actions");
    return append_external_script(
        measurement, src, charset, limits, diagnostic);
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
    const vxml_literal_fetch_audio_policy *fetch_audio,
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
        measurement, next, fetchaudio, fetch_audio,
        limits, diagnostic);
}

static vxml_status measure_block(
    salts_xml_node node, vxml_measurement *measurement,
    const vxml_literal_fetch_audio_policy *fetch_audio,
    const vxml_limits *limits, vxml_diagnostic *diagnostic) {
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
             !view_equal(local_name, "script")))
            return reject_unexpected_element(
                child, diagnostic, "unsupported VoiceXML block child element");
        if (actions != 0u)
            return fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_node_location(child),
                "VoiceXML literal block accepts at most one transfer action");
        ++actions;
        status = view_equal(local_name, "goto")
            ? measure_goto(
                child, measurement, fetch_audio,
                limits, diagnostic)
            : view_equal(local_name, "submit")
                ? measure_submit(
                    child, measurement, limits, diagnostic)
                : view_equal(local_name, "script")
                    ? measure_external_script(
                        child, measurement, limits, diagnostic)
                    : measure_exit(
                        child, measurement, limits, diagnostic);
        if (status != VXML_OK) return status;
    }
    return VXML_OK;
}

static vxml_status measure_form(
    salts_xml_node node, vxml_measurement *measurement,
    const vxml_literal_fetch_audio_policy *document_fetch_audio,
    const vxml_limits *limits, vxml_diagnostic *diagnostic) {
    const size_t first_block = measurement->block_count;
    vxml_literal_fetch_audio_policy fetch_audio =
        document_fetch_audio != NULL
            ? *document_fetch_audio
            : (vxml_literal_fetch_audio_policy){0};
    bool saw_fetchaudio = false;
    bool saw_delay = false;
    bool saw_minimum = false;
    bool saw_block = false;
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
                salts_xml_node_namespace_uri(child), VXML_NAMESPACE))
            return reject_unexpected_element(
                child, diagnostic,
                "unsupported VoiceXML form child element");
        if (view_equal(
                salts_xml_node_local_name(child), "property")) {
            if (saw_block)
                return fail(
                    diagnostic, VXML_INVALID_STRUCTURE,
                    salts_xml_node_location(child),
                    "VoiceXML form property must precede blocks");
            status = apply_fetch_audio_property(
                child, &fetch_audio,
                &saw_fetchaudio, &saw_delay, &saw_minimum,
                diagnostic);
            if (status != VXML_OK) return status;
            continue;
        }
        if (!view_equal(
                salts_xml_node_local_name(child), "block"))
            return reject_unexpected_element(
                child, diagnostic,
                "unsupported VoiceXML form child element");
        saw_block = true;
        status = measure_block(
            child, measurement, &fetch_audio,
            limits, diagnostic);
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
    vxml_literal_fetch_audio_policy fetch_audio = {0};
    bool saw_fetchaudio = false;
    bool saw_delay = false;
    bool saw_minimum = false;
    bool saw_form = false;
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
                salts_xml_node_namespace_uri(child), VXML_NAMESPACE))
            return reject_unexpected_element(
                child, diagnostic,
                "unsupported VoiceXML root child element");
        if (view_equal(
                salts_xml_node_local_name(child), "property")) {
            if (saw_form)
                return fail(
                    diagnostic, VXML_INVALID_STRUCTURE,
                    salts_xml_node_location(child),
                    "VoiceXML document property must precede forms");
            status = apply_fetch_audio_property(
                child, &fetch_audio,
                &saw_fetchaudio, &saw_delay, &saw_minimum,
                diagnostic);
            if (status != VXML_OK) return status;
            continue;
        }
        if (!view_equal(
                salts_xml_node_local_name(child), "form"))
            return reject_unexpected_element(
                child, diagnostic,
                "unsupported VoiceXML root child element");
        saw_form = true;
        status = measure_form(
            child, measurement, &fetch_audio,
            limits, diagnostic);
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
    vxml_action_row *action =
        &writer->impl->actions[writer->action_index++];
    action->kind = VXML_ACTION_EXIT;
    action->target_form = SIZE_MAX;
    action->target_uri = NULL;
    action->target_uri_size = 0u;
    action->fetchaudio_uri = NULL;
    action->fetchaudio_uri_size = 0u;
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
    action->script_src =
        writer->impl->storage + writer->storage_index;
    action->script_src_size = script.src_size;
    memcpy(
        writer->impl->storage + writer->storage_index,
        script.src, script.src_size + 1u);
    writer->storage_index += script.src_size + 1u;
    action->script_charset =
        writer->impl->storage + writer->storage_index;
    action->script_charset_size = script.charset_size;
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
            *out_action = &impl->actions[block->first_action];
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
    size_t storage_offset;
    size_t allocation_size;
    if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
    if (out != NULL) out->impl = NULL;
    if ((features & ~VXML_COMPILE_FEATURE_EXTERNAL_SCRIPT) != 0u)
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
        writer.goto_index != measurement.goto_count ||
        writer.submit_index != measurement.submit_count ||
        writer.script_index != measurement.script_count ||
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
