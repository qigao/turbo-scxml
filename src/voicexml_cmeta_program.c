#include <voicexml/cmeta.h>

#include "voicexml_cmeta_internal.h"
#include "voicexml_allocator.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VXML_NAMESPACE "http://www.w3.org/2001/vxml"

static bool cmeta_root_supported(const cmeta_data_desc *root) {
    const cmeta_data_struct_shape *shape;
    const cmeta_type_traits *traits;
    size_t index;

    if (!cmeta_data_desc_valid(root) || root->kind != CMETA_DATA_STRUCT ||
        root->storage_type == NULL || root->shape == NULL)
        return false;
    shape = (const cmeta_data_struct_shape *)root->shape;
    if (shape->layout == NULL || shape->field_count != shape->layout->field_count ||
        (shape->field_count != 0u && shape->fields == NULL) ||
        shape->layout->size != root->storage_type->size ||
        shape->layout->align != root->storage_type->align)
        return false;
    traits = root->storage_type->traits;
    if (traits == NULL ||
        ((traits->flags & (CMETA_TRAIT_TRIVIAL_COPY |
                           CMETA_TRAIT_TRIVIAL_DESTROY)) !=
         (CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY) &&
         cmeta_type_require_traits(root->storage_type,
             CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY) !=
             CMETA_OK))
        return false;
    for (index = 0u; index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &shape->fields[index];
        const cmeta_field_desc *layout_field =
            cmeta_struct_field(shape->layout, index);
        if (field->stable_id == NULL || field->name == NULL ||
            !cmeta_data_desc_valid(field->value) || layout_field == NULL ||
            layout_field->type == NULL || field->value->storage_type == NULL ||
            strcmp(field->name, layout_field->name) != 0 ||
            field->offset != layout_field->offset ||
            layout_field->offset > root->storage_type->size ||
            layout_field->size >
                root->storage_type->size - layout_field->offset ||
            !cmeta_type_equal(layout_field->type, field->value->storage_type) ||
            layout_field->size != field->value->storage_type->size ||
            layout_field->align != field->value->storage_type->align)
            return false;
    }
    return true;
}

static bool compile_options_valid(const vxml_cmeta_compile_options_v1 *options) {
    size_t index;
    if (options == NULL ||
        options->abi_version != VXML_CMETA_COMPILE_OPTIONS_ABI_V1 ||
        options->struct_size < sizeof(*options) ||
        !cmeta_root_supported(options->root) ||
        options->semantic_data_count >
            SIZE_MAX / sizeof(*options->semantic_data) ||
        ((options->semantic_data == NULL) != (options->semantic_data_count == 0u)) ||
        options->max_expression_bytes == 0u ||
        options->max_expression_instructions == 0u ||
        options->max_expression_operands == 0u ||
        options->max_expression_depth == 0u || options->max_path_depth == 0u ||
        options->max_literal_bytes == 0u || options->max_string_bytes == 0u ||
        options->max_scope_slots == 0u || options->max_scope_storage_bytes == 0u ||
        options->max_conditional_depth == 0u)
        return false;
    for (index = 0u; index < options->semantic_data_count; ++index) {
        size_t prior;
        const cmeta_data_desc *descriptor = options->semantic_data[index];
        if (!cmeta_data_desc_valid(descriptor) || descriptor->storage_type == NULL)
            return false;
        for (prior = 0u; prior < index; ++prior) {
            const cmeta_data_desc *previous = options->semantic_data[prior];
            if (cmeta_type_equal(descriptor->storage_type, previous->storage_type))
                return false;
        }
    }
    return true;
}

static bool session_options_valid(const vxml_cmeta_session_options_v1 *options) {
    return options != NULL &&
        options->abi_version == VXML_CMETA_SESSION_OPTIONS_ABI_V1 &&
        options->struct_size >= sizeof(*options) &&
        options->max_transaction_bytes != 0u &&
        options->max_execution_steps != 0u &&
        ((options->initially_undefined == NULL) ==
         (options->initially_undefined_count == 0u));
}

static bool cmeta_decode_utf8(
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

static bool cmeta_xml_character(uint32_t codepoint) {
    return codepoint == 0x9u || codepoint == 0xau || codepoint == 0xdu ||
           (codepoint >= 0x20u && codepoint <= 0xd7ffu) ||
           (codepoint >= 0xe000u && codepoint <= 0xfffdu) ||
           (codepoint >= 0x10000u && codepoint <= 0x10ffffu);
}

static bool cmeta_ncname_start(uint32_t codepoint) {
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

static bool cmeta_ncname_continue(uint32_t codepoint) {
    return cmeta_ncname_start(codepoint) || codepoint == '-' ||
           codepoint == '.' || (codepoint >= '0' && codepoint <= '9') ||
           codepoint == 0xb7u ||
           (codepoint >= 0x300u && codepoint <= 0x36fu) ||
           (codepoint >= 0x203fu && codepoint <= 0x2040u);
}

static bool cmeta_is_ncname(salts_xml_string_view name) {
    size_t cursor = 0u;
    uint32_t codepoint;
    if (name.data == NULL || name.size == 0u ||
        !cmeta_decode_utf8(name.data, name.size, &cursor, &codepoint) ||
        !cmeta_ncname_start(codepoint))
        return false;
    while (cursor < name.size) {
        if (!cmeta_decode_utf8(
                name.data, name.size, &cursor, &codepoint) ||
            !cmeta_ncname_continue(codepoint))
            return false;
    }
    return true;
}

static bool cmeta_append_utf8(
    char *output, size_t capacity, size_t *size, uint32_t codepoint) {
    const size_t required = codepoint <= 0x7fu ? 1u
        : codepoint <= 0x7ffu ? 2u : codepoint <= 0xffffu ? 3u : 4u;
    if (!cmeta_xml_character(codepoint) || *size > SIZE_MAX - required)
        return false;
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

static bool cmeta_decode_reference(
    salts_xml_string_view input, size_t *cursor, uint32_t *out_codepoint) {
    const size_t start = *cursor;
    size_t consumed = 0u;
    uint32_t codepoint = 0u;
    if (input.size - start >= 4u &&
        memcmp(input.data + start, "&lt;", 4u) == 0) {
        codepoint = '<'; consumed = 4u;
    } else if (input.size - start >= 4u &&
               memcmp(input.data + start, "&gt;", 4u) == 0) {
        codepoint = '>'; consumed = 4u;
    } else if (input.size - start >= 5u &&
               memcmp(input.data + start, "&amp;", 5u) == 0) {
        codepoint = '&'; consumed = 5u;
    } else if (input.size - start >= 6u &&
               memcmp(input.data + start, "&quot;", 6u) == 0) {
        codepoint = '"'; consumed = 6u;
    } else if (input.size - start >= 6u &&
               memcmp(input.data + start, "&apos;", 6u) == 0) {
        codepoint = '\''; consumed = 6u;
    }
    if (consumed != 0u) {
        *cursor = start + consumed;
        *out_codepoint = codepoint;
        return true;
    }
    if (input.size - start >= 4u && input.data[start + 1u] == '#') {
        const bool hexadecimal = start + 2u < input.size &&
            (input.data[start + 2u] == 'x' || input.data[start + 2u] == 'X');
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
            !cmeta_xml_character(codepoint))
            return false;
        *cursor = end + 1u;
        *out_codepoint = codepoint;
        return true;
    }
    return false;
}

static bool cmeta_next_decoded_codepoint(
    salts_xml_string_view input, size_t *cursor, uint32_t *out_codepoint) {
    if (*cursor >= input.size) return false;
    if (input.data[*cursor] == '&')
        return cmeta_decode_reference(input, cursor, out_codepoint);
    return cmeta_decode_utf8(
               input.data, input.size, cursor, out_codepoint) &&
        cmeta_xml_character(*out_codepoint);
}

static bool cmeta_decode_entities(
    salts_xml_string_view input, char *output, size_t capacity,
    size_t *out_size) {
    size_t cursor = 0u;
    size_t decoded_size = 0u;
    while (cursor < input.size) {
        uint32_t codepoint;
        if (!cmeta_next_decoded_codepoint(input, &cursor, &codepoint) ||
            !cmeta_append_utf8(output, capacity, &decoded_size, codepoint))
            return false;
    }
    *out_size = decoded_size;
    return true;
}

static bool cmeta_decoded_equal(
    salts_xml_string_view input, const char *expected) {
    size_t cursor = 0u;
    size_t expected_index = 0u;
    while (cursor < input.size) {
        uint32_t codepoint;
        if (!cmeta_next_decoded_codepoint(input, &cursor, &codepoint) ||
            codepoint > 0x7fu || expected[expected_index] == '\0' ||
            codepoint != (unsigned char)expected[expected_index++])
            return false;
    }
    return expected[expected_index] == '\0';
}

static bool cmeta_decoded_views_equal(
    salts_xml_string_view left, salts_xml_string_view right) {
    size_t left_cursor = 0u;
    size_t right_cursor = 0u;
    while (left_cursor < left.size && right_cursor < right.size) {
        uint32_t left_codepoint;
        uint32_t right_codepoint;
        if (!cmeta_next_decoded_codepoint(
                left, &left_cursor, &left_codepoint) ||
            !cmeta_next_decoded_codepoint(
                right, &right_cursor, &right_codepoint) ||
            left_codepoint != right_codepoint)
            return false;
    }
    return left_cursor == left.size && right_cursor == right.size;
}

static bool raw_view_equal(salts_xml_string_view view, const char *text) {
    const size_t text_size = text != NULL ? strlen(text) : 0u;
    return view.data != NULL && view.size == text_size &&
        memcmp(view.data, text, text_size) == 0;
}

static vxml_status map_cmeta_xml_status(salts_xml_status status) {
    switch (status) {
        case SALTS_XML_OK: return VXML_OK;
        case SALTS_XML_INVALID_ARGUMENT: return VXML_INVALID_ARGUMENT;
        case SALTS_XML_LIMIT_EXCEEDED: return VXML_LIMIT_EXCEEDED;
        case SALTS_XML_ALLOCATION_FAILED: return VXML_ALLOCATION_FAILED;
        case SALTS_XML_UNSUPPORTED: return VXML_UNSUPPORTED_FEATURE;
        case SALTS_XML_EMBEDDED_NUL:
        case SALTS_XML_MALFORMED: return VXML_XML_ERROR;
    }
    return VXML_XML_ERROR;
}

static vxml_status admit_cmeta_datamodel(
    const char *bytes, size_t size, const vxml_limits *limits,
    vxml_diagnostic *diagnostic) {
    const salts_xml_limits *xml_limits = limits != NULL ? &limits->xml : NULL;
    salts_xml_document document = {0};
    salts_xml_diagnostic xml_diagnostic = {0};
    salts_xml_node root;
    size_t index;
    salts_xml_status xml_status = salts_xml_parse(
        &document, bytes, size, xml_limits, &xml_diagnostic);
    if (xml_status != SALTS_XML_OK) {
        const vxml_status status = map_cmeta_xml_status(xml_status);
        if (diagnostic != NULL) {
            diagnostic->status = status;
            diagnostic->location = xml_diagnostic.location;
            (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                           "%s", xml_diagnostic.message);
        }
        return status;
    }
    root = salts_xml_document_root(&document);
    for (index = 0u; index < salts_xml_node_attribute_count(root); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(root, index);
        const salts_xml_string_view value =
            salts_xml_attribute_value(attribute);
        if (salts_xml_attribute_namespace_uri(attribute).size == 0u &&
            raw_view_equal(
                salts_xml_attribute_local_name(attribute), "datamodel") &&
            cmeta_decoded_equal(value, "cmeta")) {
            salts_xml_document_destroy(&document);
            return VXML_OK;
        }
    }
    if (diagnostic != NULL) {
        diagnostic->status = VXML_INVALID_CONTRACT;
        diagnostic->location = salts_xml_node_location(root);
        (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                       "%s", "CMeta compilation requires datamodel=cmeta");
    }
    salts_xml_document_destroy(&document);
    return VXML_INVALID_CONTRACT;
}

typedef struct cmeta_program_measurement {
    size_t form_count;
    size_t block_count;
    size_t scope_count;
    size_t declaration_count;
    size_t action_count;
    size_t branch_count;
    size_t expression_count;
    size_t location_count;
    size_t exit_count;
    size_t name_bytes;
} cmeta_program_measurement;

static vxml_status cmeta_program_fail(
    vxml_diagnostic *diagnostic, vxml_status status,
    salts_xml_location location, const char *message) {
    if (diagnostic != NULL) {
        diagnostic->status = status;
        diagnostic->location = location;
        (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                       "%s", message != NULL ? message : "VoiceXML error");
    }
    return status;
}

static salts_xml_attribute cmeta_attribute(
    salts_xml_node node, const char *name) {
    size_t index;
    for (index = 0u; index < salts_xml_node_attribute_count(node); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(node, index);
        if (salts_xml_attribute_namespace_uri(attribute).size == 0u &&
            raw_view_equal(salts_xml_attribute_local_name(attribute), name))
            return attribute;
    }
    return (salts_xml_attribute){0};
}

static bool cmeta_node_ignorable(salts_xml_node node);

static vxml_status cmeta_validate_attributes(
    salts_xml_node node, const char *const *allowed, size_t allowed_count,
    vxml_diagnostic *diagnostic) {
    size_t index;
    for (index = 0u; index < salts_xml_node_attribute_count(node); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(node, index);
        const salts_xml_string_view local =
            salts_xml_attribute_local_name(attribute);
        size_t allowed_index;
        bool matched = false;
        if (salts_xml_attribute_namespace_uri(attribute).size == 0u) {
            for (allowed_index = 0u; allowed_index < allowed_count;
                 ++allowed_index) {
                if (raw_view_equal(local, allowed[allowed_index])) {
                    matched = true;
                    break;
                }
            }
        }
        if (!matched)
            return cmeta_program_fail(
                diagnostic, VXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported VoiceXML attribute");
    }
    return VXML_OK;
}

static vxml_status cmeta_validate_empty_element(
    salts_xml_node node, vxml_diagnostic *diagnostic) {
    size_t index;
    for (index = 0u; index < salts_xml_node_child_count(node); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(node, index);
        if (!cmeta_node_ignorable(child))
            return cmeta_program_fail(
                diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_node_location(child),
                "VoiceXML executable leaf element must be empty");
    }
    return VXML_OK;
}

static bool cmeta_node_named(salts_xml_node node, const char *name) {
    const salts_xml_string_view uri = salts_xml_node_namespace_uri(node);
    return salts_xml_node_type(node) == SALTS_XML_ELEMENT &&
        raw_view_equal(salts_xml_node_local_name(node), name) &&
        cmeta_decoded_equal(uri, VXML_NAMESPACE);
}

static bool cmeta_text_whitespace(salts_xml_string_view text) {
    size_t cursor = 0u;
    while (cursor < text.size) {
        uint32_t codepoint;
        if (!cmeta_next_decoded_codepoint(text, &cursor, &codepoint) ||
            (codepoint != ' ' && codepoint != '\t' &&
             codepoint != '\r' && codepoint != '\n'))
            return false;
    }
    return true;
}

static bool cmeta_node_ignorable(salts_xml_node node) {
    const salts_xml_node_kind kind = salts_xml_node_type(node);
    return kind == SALTS_XML_COMMENT ||
        kind == SALTS_XML_PROCESSING_INSTRUCTION ||
        (kind == SALTS_XML_TEXT &&
         cmeta_text_whitespace(salts_xml_node_value(node)));
}

static bool cmeta_known_profile_element(salts_xml_node node) {
    return cmeta_node_named(node, "vxml") || cmeta_node_named(node, "form") ||
        cmeta_node_named(node, "block") || cmeta_node_named(node, "var") ||
        cmeta_node_named(node, "assign") || cmeta_node_named(node, "clear") ||
        cmeta_node_named(node, "if") || cmeta_node_named(node, "elseif") ||
        cmeta_node_named(node, "else") || cmeta_node_named(node, "exit");
}

static bool cmeta_measure_increment(size_t *value) {
    if (*value == SIZE_MAX) return false;
    ++*value;
    return true;
}

static vxml_status cmeta_measure_name(
    salts_xml_attribute attribute, cmeta_program_measurement *measurement,
    const vxml_limits *limits, vxml_diagnostic *diagnostic) {
    size_t decoded_size = 0u;
    if (attribute.impl == NULL) return VXML_OK;
    if (!cmeta_decode_entities(
            salts_xml_attribute_value(attribute), NULL, 0u, &decoded_size))
        return cmeta_program_fail(
            diagnostic, VXML_XML_ERROR,
            salts_xml_attribute_location(attribute),
            "VoiceXML name contains an invalid XML reference");
    if (decoded_size == SIZE_MAX ||
        measurement->name_bytes > SIZE_MAX - (decoded_size + 1u) ||
        measurement->name_bytes + decoded_size + 1u > limits->max_name_bytes)
        return cmeta_program_fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_attribute_location(attribute),
            "VoiceXML decoded names exceed max_name_bytes");
    measurement->name_bytes += decoded_size + 1u;
    return VXML_OK;
}

static bool cmeta_namelist_next(
    salts_xml_string_view list, size_t *cursor,
    salts_xml_string_view *out_name) {
    size_t start;
    while (*cursor < list.size &&
           (list.data[*cursor] == ' ' || list.data[*cursor] == '\t' ||
            list.data[*cursor] == '\r' || list.data[*cursor] == '\n'))
        ++*cursor;
    if (*cursor == list.size) return false;
    start = *cursor;
    while (*cursor < list.size &&
           list.data[*cursor] != ' ' && list.data[*cursor] != '\t' &&
           list.data[*cursor] != '\r' && list.data[*cursor] != '\n')
        ++*cursor;
    out_name->data = list.data + start;
    out_name->size = *cursor - start;
    return true;
}

static vxml_status cmeta_measure_namelist(
    salts_xml_attribute attribute, cmeta_program_measurement *measurement,
    const vxml_limits *limits, vxml_diagnostic *diagnostic) {
    const salts_xml_string_view list = salts_xml_attribute_value(attribute);
    size_t cursor = 0u;
    size_t name_size = 0u;
    bool in_name = false;
    while (cursor < list.size) {
        uint32_t codepoint;
        if (!cmeta_next_decoded_codepoint(list, &cursor, &codepoint))
            return cmeta_program_fail(
                diagnostic, VXML_XML_ERROR,
                salts_xml_attribute_location(attribute),
                "VoiceXML namelist contains an invalid XML reference");
        if (codepoint == ' ' || codepoint == '\t' ||
            codepoint == '\r' || codepoint == '\n') {
            if (in_name) {
                if (measurement->name_bytes > SIZE_MAX - (name_size + 1u) ||
                    measurement->name_bytes + name_size + 1u >
                        limits->max_name_bytes)
                    return cmeta_program_fail(
                        diagnostic, VXML_LIMIT_EXCEEDED,
                        salts_xml_attribute_location(attribute),
                        "VoiceXML decoded names exceed max_name_bytes");
                measurement->name_bytes += name_size + 1u;
                name_size = 0u;
            }
            in_name = false;
        } else if (!in_name) {
            if (!cmeta_measure_increment(&measurement->location_count))
                return cmeta_program_fail(
                    diagnostic, VXML_LIMIT_EXCEEDED,
                    salts_xml_attribute_location(attribute),
                    "VoiceXML namelist count overflow");
            in_name = true;
        }
        if (in_name) {
            const size_t width = codepoint <= 0x7fu ? 1u
                : codepoint <= 0x7ffu ? 2u
                : codepoint <= 0xffffu ? 3u : 4u;
            if (name_size > SIZE_MAX - width)
                return cmeta_program_fail(
                    diagnostic, VXML_LIMIT_EXCEEDED,
                    salts_xml_attribute_location(attribute),
                    "VoiceXML namelist size overflow");
            name_size += width;
        }
    }
    if (in_name) {
        if (measurement->name_bytes > SIZE_MAX - (name_size + 1u) ||
            measurement->name_bytes + name_size + 1u > limits->max_name_bytes)
            return cmeta_program_fail(
                diagnostic, VXML_LIMIT_EXCEEDED,
                salts_xml_attribute_location(attribute),
                "VoiceXML decoded names exceed max_name_bytes");
        measurement->name_bytes += name_size + 1u;
    }
    return VXML_OK;
}

static bool cmeta_prior_var_in_tree(
    salts_xml_node container, salts_xml_node target,
    salts_xml_string_view name, bool *reached_target) {
    size_t index;
    for (index = 0u; index < salts_xml_node_child_count(container); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(container, index);
        const salts_xml_attribute previous_name = cmeta_attribute(child, "name");
        if (child.impl == target.impl) {
            *reached_target = true;
            return false;
        }
        if (cmeta_node_named(child, "var") && previous_name.impl != NULL) {
            const salts_xml_string_view value =
                salts_xml_attribute_value(previous_name);
            if (cmeta_decoded_views_equal(value, name))
                return true;
        }
        if (cmeta_node_named(child, "if")) {
            const bool found = cmeta_prior_var_in_tree(
                child, target, name, reached_target);
            if (found || *reached_target) return found;
        }
    }
    return false;
}

static vxml_status cmeta_measure_repeated_vars(
    salts_xml_node block, salts_xml_node container,
    cmeta_program_measurement *measurement,
    vxml_diagnostic *diagnostic) {
    size_t index;
    for (index = 0u; index < salts_xml_node_child_count(container); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(container, index);
        if (cmeta_node_named(child, "var")) {
            const salts_xml_attribute name_attribute = cmeta_attribute(child, "name");
            bool reached_target = false;
            if (name_attribute.impl != NULL &&
                cmeta_prior_var_in_tree(
                    block, child, salts_xml_attribute_value(name_attribute),
                    &reached_target) &&
                !cmeta_measure_increment(&measurement->location_count))
                return cmeta_program_fail(
                    diagnostic, VXML_LIMIT_EXCEEDED,
                    salts_xml_node_location(child),
                    "VoiceXML location count overflow");
        }
        if (cmeta_node_named(child, "if")) {
            const vxml_status status = cmeta_measure_repeated_vars(
                block, child, measurement, diagnostic);
            if (status != VXML_OK) return status;
        }
    }
    return VXML_OK;
}

static vxml_status cmeta_measure_executable(
    salts_xml_node node, salts_xml_node parent, size_t child_index,
    cmeta_program_measurement *measurement, const vxml_limits *limits,
    vxml_diagnostic *diagnostic);

static vxml_status cmeta_measure_conditional(
    salts_xml_node node, cmeta_program_measurement *measurement,
    const vxml_limits *limits, vxml_diagnostic *diagnostic) {
    const salts_xml_attribute condition = cmeta_attribute(node, "cond");
    size_t index;
    bool saw_else = false;
    if (condition.impl == NULL)
        return cmeta_program_fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_node_location(node), "VoiceXML if requires cond");
    if (!cmeta_measure_increment(&measurement->branch_count) ||
        !cmeta_measure_increment(&measurement->expression_count))
        return cmeta_program_fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(node),
            "VoiceXML conditional row count overflow");
    for (index = 0u; index < salts_xml_node_child_count(node); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(node, index);
        if (cmeta_node_ignorable(child)) continue;
        if (cmeta_node_named(child, "elseif")) {
            static const char *const allowed[] = {"cond"};
            vxml_status status = cmeta_validate_attributes(
                child, allowed, 1u, diagnostic);
            if (status == VXML_OK)
                status = cmeta_validate_empty_element(child, diagnostic);
            if (status != VXML_OK) return status;
            if (saw_else || cmeta_attribute(child, "cond").impl == NULL)
                return cmeta_program_fail(
                    diagnostic, VXML_INVALID_STRUCTURE,
                    salts_xml_node_location(child),
                    "VoiceXML elseif requires cond and must precede else");
            if (!cmeta_measure_increment(&measurement->branch_count) ||
                !cmeta_measure_increment(&measurement->expression_count))
                return cmeta_program_fail(
                    diagnostic, VXML_LIMIT_EXCEEDED,
                    salts_xml_node_location(child),
                    "VoiceXML conditional row count overflow");
            continue;
        }
        if (cmeta_node_named(child, "else")) {
            vxml_status status = cmeta_validate_attributes(
                child, NULL, 0u, diagnostic);
            if (status == VXML_OK)
                status = cmeta_validate_empty_element(child, diagnostic);
            if (status != VXML_OK) return status;
            if (saw_else)
                return cmeta_program_fail(
                    diagnostic, VXML_INVALID_STRUCTURE,
                    salts_xml_node_location(child),
                    "VoiceXML if may contain only one else");
            saw_else = true;
            if (!cmeta_measure_increment(&measurement->branch_count))
                return cmeta_program_fail(
                    diagnostic, VXML_LIMIT_EXCEEDED,
                    salts_xml_node_location(child),
                    "VoiceXML conditional row count overflow");
            continue;
        }
        {
            const vxml_status status = cmeta_measure_executable(
                child, node, index, measurement, limits, diagnostic);
            if (status != VXML_OK) return status;
        }
    }
    return VXML_OK;
}

static vxml_status cmeta_measure_executable(
    salts_xml_node node, salts_xml_node parent, size_t child_index,
    cmeta_program_measurement *measurement, const vxml_limits *limits,
    vxml_diagnostic *diagnostic) {
    salts_xml_attribute expression;
    vxml_status status;
    (void)parent;
    (void)child_index;
    if (salts_xml_node_type(node) != SALTS_XML_ELEMENT)
        return cmeta_program_fail(
            diagnostic, VXML_UNSUPPORTED_FEATURE,
            salts_xml_node_location(node),
            "implicit VoiceXML prompt text is unsupported");
    if (!cmeta_node_named(node, "var") &&
        !cmeta_node_named(node, "assign") &&
        !cmeta_node_named(node, "clear") &&
        !cmeta_node_named(node, "if") &&
        !cmeta_node_named(node, "exit"))
        return cmeta_program_fail(
            diagnostic,
            cmeta_known_profile_element(node)
                ? VXML_INVALID_STRUCTURE : VXML_UNSUPPORTED_FEATURE,
            salts_xml_node_location(node),
            cmeta_known_profile_element(node)
                ? "VoiceXML element is invalid in executable content"
                : "unsupported VoiceXML executable element");
    if (cmeta_node_named(node, "var") || cmeta_node_named(node, "assign")) {
        static const char *const allowed[] = {"name", "expr"};
        status = cmeta_validate_attributes(node, allowed, 2u, diagnostic);
    } else if (cmeta_node_named(node, "clear")) {
        static const char *const allowed[] = {"namelist"};
        status = cmeta_validate_attributes(node, allowed, 1u, diagnostic);
    } else if (cmeta_node_named(node, "if")) {
        static const char *const allowed[] = {"cond"};
        status = cmeta_validate_attributes(node, allowed, 1u, diagnostic);
    } else {
        static const char *const allowed[] = {"expr", "namelist"};
        status = cmeta_validate_attributes(node, allowed, 2u, diagnostic);
    }
    if (status != VXML_OK) return status;
    if (cmeta_node_named(node, "var") || cmeta_node_named(node, "assign")) {
        status = cmeta_measure_name(
            cmeta_attribute(node, "name"), measurement, limits, diagnostic);
        if (status != VXML_OK) return status;
    }
    if (!cmeta_node_named(node, "if")) {
        status = cmeta_validate_empty_element(node, diagnostic);
        if (status != VXML_OK) return status;
    }
    if (!cmeta_measure_increment(&measurement->action_count) ||
        measurement->action_count > limits->max_actions)
        return cmeta_program_fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(node),
            "VoiceXML action count exceeds max_actions");
    expression = cmeta_attribute(node, "expr");
    if (expression.impl != NULL &&
        !cmeta_measure_increment(&measurement->expression_count))
        return cmeta_program_fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_attribute_location(expression),
            "VoiceXML expression count overflow");
    if (cmeta_node_named(node, "assign") &&
        !cmeta_measure_increment(&measurement->location_count))
        return cmeta_program_fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(node),
            "VoiceXML location count overflow");
    if (cmeta_node_named(node, "clear")) {
        const salts_xml_attribute namelist = cmeta_attribute(node, "namelist");
        if (namelist.impl != NULL) {
            status = cmeta_measure_namelist(
                namelist, measurement, limits, diagnostic);
            if (status != VXML_OK) return status;
        }
    }
    if (cmeta_node_named(node, "exit")) {
        const salts_xml_attribute namelist = cmeta_attribute(node, "namelist");
        if (!cmeta_measure_increment(&measurement->exit_count))
            return cmeta_program_fail(
                diagnostic, VXML_LIMIT_EXCEEDED,
                salts_xml_node_location(node),
                "VoiceXML exit row count overflow");
        if (namelist.impl != NULL) {
            status = cmeta_measure_namelist(
                namelist, measurement, limits, diagnostic);
            if (status != VXML_OK) return status;
        }
    }
    if (cmeta_node_named(node, "if"))
        return cmeta_measure_conditional(
            node, measurement, limits, diagnostic);
    return VXML_OK;
}

static vxml_status cmeta_measure_block(
    salts_xml_node block, cmeta_program_measurement *measurement,
    const vxml_limits *limits, vxml_diagnostic *diagnostic) {
    size_t index;
    const salts_xml_attribute block_cond = cmeta_attribute(block, "cond");
    const salts_xml_attribute block_expr = cmeta_attribute(block, "expr");
    {
        static const char *const allowed[] = {"name", "expr", "cond"};
        const vxml_status status = cmeta_validate_attributes(
            block, allowed, 3u, diagnostic);
        if (status != VXML_OK) return status;
    }
    {
        const vxml_status status = cmeta_measure_name(
            cmeta_attribute(block, "name"), measurement, limits, diagnostic);
        if (status != VXML_OK) return status;
    }
    if (!cmeta_measure_increment(&measurement->block_count) ||
        measurement->block_count > limits->max_blocks ||
        !cmeta_measure_increment(&measurement->scope_count))
        return cmeta_program_fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(block), "VoiceXML block limit exceeded");
    if ((block_cond.impl != NULL &&
         !cmeta_measure_increment(&measurement->expression_count)) ||
        (block_expr.impl != NULL &&
         !cmeta_measure_increment(&measurement->expression_count)))
        return cmeta_program_fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(block),
            "VoiceXML expression count overflow");
    for (index = 0u; index < salts_xml_node_child_count(block); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(block, index);
        if (cmeta_node_ignorable(child)) continue;
        {
            const vxml_status status = cmeta_measure_executable(
                child, block, index, measurement, limits, diagnostic);
            if (status != VXML_OK) return status;
        }
    }
    return cmeta_measure_repeated_vars(
        block, block, measurement, diagnostic);
}

static vxml_status cmeta_measure_form(
    salts_xml_node form, cmeta_program_measurement *measurement,
    const vxml_limits *limits, vxml_diagnostic *diagnostic) {
    size_t index;
    bool saw_block = false;
    const size_t first_block = measurement->block_count;
    {
        static const char *const allowed[] = {"id"};
        const vxml_status status = cmeta_validate_attributes(
            form, allowed, 1u, diagnostic);
        if (status != VXML_OK) return status;
    }
    {
        const vxml_status status = cmeta_measure_name(
            cmeta_attribute(form, "id"), measurement, limits, diagnostic);
        if (status != VXML_OK) return status;
    }
    if (!cmeta_measure_increment(&measurement->form_count) ||
        measurement->form_count > limits->max_forms ||
        !cmeta_measure_increment(&measurement->scope_count))
        return cmeta_program_fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(form), "VoiceXML form limit exceeded");
    for (index = 0u; index < salts_xml_node_child_count(form); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(form, index);
        if (cmeta_node_ignorable(child)) continue;
        if (cmeta_node_named(child, "var")) {
            const salts_xml_attribute expression =
                cmeta_attribute(child, "expr");
            const vxml_status name_status = cmeta_measure_name(
                cmeta_attribute(child, "name"), measurement,
                limits, diagnostic);
            if (name_status != VXML_OK) return name_status;
            if (saw_block)
                return cmeta_program_fail(
                    diagnostic, VXML_INVALID_STRUCTURE,
                    salts_xml_node_location(child),
                    "form variables must precede form items");
            if (!cmeta_measure_increment(&measurement->declaration_count) ||
                (expression.impl != NULL &&
                 !cmeta_measure_increment(&measurement->expression_count)))
                return cmeta_program_fail(
                    diagnostic, VXML_LIMIT_EXCEEDED,
                    salts_xml_node_location(child),
                    "VoiceXML declaration count overflow");
            continue;
        }
        if (!cmeta_node_named(child, "block"))
            return cmeta_program_fail(
                diagnostic,
                cmeta_known_profile_element(child)
                    ? VXML_INVALID_STRUCTURE : VXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child),
                cmeta_known_profile_element(child)
                    ? "VoiceXML element has invalid form placement"
                    : "unsupported VoiceXML form child element");
        saw_block = true;
        {
            const vxml_status status = cmeta_measure_block(
                child, measurement, limits, diagnostic);
            if (status != VXML_OK) return status;
        }
    }
    if (measurement->block_count == first_block)
        return cmeta_program_fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_node_location(form),
            "VoiceXML form requires at least one block");
    return VXML_OK;
}

static vxml_status cmeta_measure_program(
    const char *bytes, size_t size, const vxml_limits *limits,
    cmeta_program_measurement *measurement,
    vxml_diagnostic *diagnostic) {
    salts_xml_document document = {0};
    salts_xml_diagnostic xml_diagnostic = {0};
    salts_xml_status xml_status;
    salts_xml_node root;
    size_t index;
    bool saw_form = false;
    vxml_status status = VXML_OK;
    memset(measurement, 0, sizeof(*measurement));
    measurement->scope_count = 1u;
    xml_status = salts_xml_parse(
        &document, bytes, size, &limits->xml, &xml_diagnostic);
    if (xml_status != SALTS_XML_OK)
        return cmeta_program_fail(
            diagnostic, map_cmeta_xml_status(xml_status),
            xml_diagnostic.location, xml_diagnostic.message);
    root = salts_xml_document_root(&document);
    if (!cmeta_node_named(root, "vxml")) {
        status = cmeta_program_fail(
            diagnostic, VXML_INVALID_NAMESPACE,
            salts_xml_node_location(root),
            "VoiceXML root must be vxml in the VoiceXML namespace");
    } else {
        static const char *const allowed[] = {"version", "datamodel"};
        const salts_xml_attribute version = cmeta_attribute(root, "version");
        status = cmeta_validate_attributes(root, allowed, 2u, diagnostic);
        if (status == VXML_OK &&
            (version.impl == NULL ||
             (!cmeta_decoded_equal(
                  salts_xml_attribute_value(version), "2.0") &&
              !cmeta_decoded_equal(
                  salts_xml_attribute_value(version), "2.1"))))
            status = cmeta_program_fail(
                diagnostic, VXML_INVALID_VERSION,
                version.impl != NULL
                    ? salts_xml_attribute_location(version)
                    : salts_xml_node_location(root),
                "VoiceXML version must be 2.0 or 2.1");
    }
    if (status != VXML_OK) {
        salts_xml_document_destroy(&document);
        return status;
    }
    for (index = 0u; index < salts_xml_node_child_count(root); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(root, index);
        if (cmeta_node_ignorable(child)) continue;
        if (cmeta_node_named(child, "var")) {
            const salts_xml_attribute expression =
                cmeta_attribute(child, "expr");
            const vxml_status name_status = cmeta_measure_name(
                cmeta_attribute(child, "name"), measurement,
                limits, diagnostic);
            if (name_status != VXML_OK) {
                status = name_status;
                break;
            }
            if (saw_form) {
                status = cmeta_program_fail(
                    diagnostic, VXML_INVALID_STRUCTURE,
                    salts_xml_node_location(child),
                    "document variables must precede forms");
                break;
            }
            if (!cmeta_measure_increment(&measurement->declaration_count) ||
                (expression.impl != NULL &&
                 !cmeta_measure_increment(&measurement->expression_count))) {
                status = cmeta_program_fail(
                    diagnostic, VXML_LIMIT_EXCEEDED,
                    salts_xml_node_location(child),
                    "VoiceXML declaration count overflow");
                break;
            }
            continue;
        }
        if (!cmeta_node_named(child, "form")) {
            status = cmeta_program_fail(
                diagnostic,
                cmeta_known_profile_element(child)
                    ? VXML_INVALID_STRUCTURE : VXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child),
                cmeta_known_profile_element(child)
                    ? "VoiceXML element has invalid document placement"
                    : "unsupported VoiceXML document child element");
            break;
        }
        saw_form = true;
        status = cmeta_measure_form(
            child, measurement, limits, diagnostic);
        if (status != VXML_OK) break;
    }
    if (status == VXML_OK && measurement->form_count == 0u)
        status = cmeta_program_fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_node_location(root),
            "VoiceXML document requires at least one form");
    if (status == VXML_OK &&
        (measurement->declaration_count > limits->max_actions ||
         measurement->action_count >
             limits->max_actions - measurement->declaration_count))
        status = cmeta_program_fail(
            diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(root),
            "VoiceXML total action count exceeds max_actions");
    salts_xml_document_destroy(&document);
    return status;
}

typedef struct cmeta_program_builder {
    vxml_program_impl *impl;
    vxml_cmeta_program_data *profile;
    const vxml_cmeta_compile_options_v1 *options;
    vxml_diagnostic *diagnostic;
    size_t form_index;
    size_t block_index;
    size_t declaration_index;
    size_t action_index;
    size_t branch_index;
    size_t expression_index;
    size_t location_index;
    size_t candidate_index;
    size_t generic_action_index;
    size_t string_index;
} cmeta_program_builder;

static void *cmeta_scope_allocate(void *user, size_t size) {
    (void)user;
    return vxml_malloc(size);
}

static void *cmeta_scope_allocate_zero(
    void *user, size_t count, size_t size) {
    (void)user;
    return vxml_calloc(count, size);
}

static void cmeta_scope_deallocate(void *user, void *pointer) {
    (void)user;
    vxml_free(pointer);
}

static const cmeta_scope_allocator cmeta_program_scope_allocator = {
    NULL,
    cmeta_scope_allocate,
    cmeta_scope_allocate_zero,
    cmeta_scope_deallocate};

static void cmeta_program_data_destroy(vxml_cmeta_program_data *profile) {
    size_t index;
    if (profile == NULL) return;
    if (profile->expressions != NULL)
        for (index = 0u; index < profile->expression_count; ++index)
            vxml_cmeta_expr_program_destroy(
                &profile->expressions[index].program);
    if (profile->scopes != NULL)
        for (index = 0u; index < profile->scope_count; ++index)
            cmeta_scope_schema_destroy(&profile->scopes[index].schema);
    vxml_free(profile->location_candidates);
    vxml_free(profile->locations);
    vxml_free(profile->expressions);
    vxml_free(profile->branches);
    vxml_free(profile->actions);
    vxml_free(profile->declarations);
    vxml_free(profile->blocks);
    vxml_free(profile->forms);
    vxml_free(profile->scopes);
    vxml_free(profile->strings);
    vxml_free(profile->semantic_data);
    vxml_free(profile);
}

static bool cmeta_allocate_rows(
    const cmeta_program_measurement *measurement, size_t input_size,
    const vxml_cmeta_compile_options_v1 *options,
    vxml_program_impl **out_impl, vxml_cmeta_program_data **out_profile) {
    vxml_program_impl *impl = NULL;
    vxml_cmeta_program_data *profile = NULL;
    size_t candidate_count;
    size_t string_capacity;
    if (measurement->location_count > SIZE_MAX / 4u ||
        input_size == SIZE_MAX)
        return false;
    candidate_count = measurement->location_count * 4u;
    string_capacity = input_size + 1u;
    impl = (vxml_program_impl *)vxml_calloc(1u, sizeof(*impl));
    profile = (vxml_cmeta_program_data *)vxml_calloc(1u, sizeof(*profile));
    if (impl == NULL || profile == NULL) goto failure;
    profile->root = options->root;
    profile->semantic_data_count = options->semantic_data_count;
    profile->scope_count = measurement->scope_count;
    profile->document_scope = 0u;
    profile->form_count = measurement->form_count;
    profile->block_count = measurement->block_count;
    profile->declaration_count = measurement->declaration_count;
    profile->action_count = measurement->action_count;
    profile->branch_count = measurement->branch_count;
    profile->expression_count = measurement->expression_count;
    profile->location_count = measurement->location_count;
    profile->location_candidate_count = candidate_count;
    profile->max_string_bytes = options->max_string_bytes;
    if (options->semantic_data_count != 0u) {
        profile->semantic_data = (const cmeta_data_desc **)vxml_malloc(
            options->semantic_data_count * sizeof(*profile->semantic_data));
        if (profile->semantic_data == NULL) goto failure;
        memcpy(profile->semantic_data, options->semantic_data,
               options->semantic_data_count * sizeof(*profile->semantic_data));
    }
#define CMETA_ALLOC_ROWS(member, count) \
    do { \
        if ((count) != 0u) { \
            profile->member = vxml_calloc((count), sizeof(*profile->member)); \
            if (profile->member == NULL) goto failure; \
        } \
    } while (0)
    CMETA_ALLOC_ROWS(scopes, measurement->scope_count);
    CMETA_ALLOC_ROWS(forms, measurement->form_count);
    CMETA_ALLOC_ROWS(blocks, measurement->block_count);
    CMETA_ALLOC_ROWS(declarations, measurement->declaration_count);
    CMETA_ALLOC_ROWS(actions, measurement->action_count);
    CMETA_ALLOC_ROWS(branches, measurement->branch_count);
    CMETA_ALLOC_ROWS(expressions, measurement->expression_count);
    CMETA_ALLOC_ROWS(locations, measurement->location_count);
    CMETA_ALLOC_ROWS(location_candidates, candidate_count);
#undef CMETA_ALLOC_ROWS
    profile->strings = (char *)vxml_malloc(string_capacity);
    if (profile->strings == NULL) goto failure;
    profile->string_size = string_capacity;
    if (measurement->form_count != 0u) {
        impl->forms = (vxml_form_row *)vxml_calloc(
            measurement->form_count, sizeof(*impl->forms));
        if (impl->forms == NULL) goto failure;
    }
    if (measurement->block_count != 0u) {
        impl->blocks = (vxml_block_row *)vxml_calloc(
            measurement->block_count, sizeof(*impl->blocks));
        if (impl->blocks == NULL) goto failure;
    }
    if (measurement->exit_count != 0u) {
        impl->actions = (vxml_action_row *)vxml_calloc(
            measurement->exit_count, sizeof(*impl->actions));
        if (impl->actions == NULL) goto failure;
    }
    impl->form_count = measurement->form_count;
    impl->block_count = measurement->block_count;
    impl->action_count = measurement->exit_count;
    impl->allocation_size = sizeof(*impl);
    *out_impl = impl;
    *out_profile = profile;
    return true;

failure:
    if (impl != NULL) {
        vxml_free(impl->actions);
        vxml_free(impl->blocks);
        vxml_free(impl->forms);
        vxml_free(impl);
    }
    cmeta_program_data_destroy(profile);
    return false;
}

static const char *cmeta_retain_view(
    cmeta_program_builder *builder, salts_xml_string_view view) {
    char *destination;
    if (builder->string_index > builder->profile->string_size ||
        view.size >= builder->profile->string_size - builder->string_index)
        return NULL;
    destination = builder->profile->strings + builder->string_index;
    memcpy(destination, view.data, view.size);
    destination[view.size] = '\0';
    builder->string_index += view.size + 1u;
    return destination;
}

typedef struct cmeta_decoded_value {
    salts_xml_string_view view;
    char *owned;
} cmeta_decoded_value;

static vxml_status cmeta_decode_temporary(
    cmeta_program_builder *builder, salts_xml_string_view raw,
    salts_xml_location location, cmeta_decoded_value *out) {
    size_t decoded_size = 0u;
    char *decoded;
    memset(out, 0, sizeof(*out));
    if (!cmeta_decode_entities(raw, NULL, 0u, &decoded_size) ||
        decoded_size == SIZE_MAX)
        return cmeta_program_fail(
            builder->diagnostic, VXML_XML_ERROR, location,
            "VoiceXML attribute contains an invalid XML reference");
    decoded = (char *)vxml_malloc(decoded_size + 1u);
    if (decoded == NULL)
        return cmeta_program_fail(
            builder->diagnostic, VXML_ALLOCATION_FAILED, location,
            "VoiceXML decoded attribute allocation failed");
    if (!cmeta_decode_entities(raw, decoded, decoded_size, &decoded_size)) {
        vxml_free(decoded);
        return cmeta_program_fail(
            builder->diagnostic, VXML_XML_ERROR, location,
            "VoiceXML attribute contains an invalid XML reference");
    }
    decoded[decoded_size] = '\0';
    out->view.data = decoded;
    out->view.size = decoded_size;
    out->owned = decoded;
    return VXML_OK;
}

static void cmeta_decoded_value_destroy(cmeta_decoded_value *value) {
    if (value == NULL) return;
    vxml_free(value->owned);
    memset(value, 0, sizeof(*value));
}

static vxml_status cmeta_retain_decoded_view(
    cmeta_program_builder *builder, salts_xml_string_view raw,
    salts_xml_location location, const char **out_data, size_t *out_size) {
    size_t decoded_size = 0u;
    char *destination;
    if (!cmeta_decode_entities(raw, NULL, 0u, &decoded_size))
        return cmeta_program_fail(
            builder->diagnostic, VXML_XML_ERROR, location,
            "VoiceXML attribute contains an invalid XML reference");
    if (builder->string_index > builder->profile->string_size ||
        decoded_size >=
            builder->profile->string_size - builder->string_index)
        return cmeta_program_fail(
            builder->diagnostic, VXML_LIMIT_EXCEEDED, location,
            "VoiceXML retained string storage overflow");
    destination = builder->profile->strings + builder->string_index;
    if (!cmeta_decode_entities(
            raw, destination, decoded_size, &decoded_size))
        return cmeta_program_fail(
            builder->diagnostic, VXML_XML_ERROR, location,
            "VoiceXML attribute contains an invalid XML reference");
    destination[decoded_size] = '\0';
    builder->string_index += decoded_size + 1u;
    *out_data = destination;
    *out_size = decoded_size;
    return VXML_OK;
}

static const cmeta_data_field_desc *cmeta_root_field(
    const cmeta_data_desc *root, salts_xml_string_view name,
    size_t *out_index) {
    const cmeta_data_struct_shape *shape =
        (const cmeta_data_struct_shape *)root->shape;
    size_t index;
    for (index = 0u; index < shape->field_count; ++index) {
        const char *field_name = shape->fields[index].name;
        if (field_name != NULL &&
            raw_view_equal(name, field_name)) {
            if (out_index != NULL) *out_index = index;
            return &shape->fields[index];
        }
    }
    return NULL;
}

static bool cmeta_ascii_ncname(salts_xml_string_view name) {
    return cmeta_is_ncname(name);
}

static bool cmeta_scope_storage_limit_exceeded(
    const cmeta_scope_schema *schema, const cmeta_data_desc *value) {
    const cmeta_type_desc *type = value != NULL ? value->storage_type : NULL;
    size_t aligned;
    if (schema == NULL || type == NULL || type->align == 0u)
        return false;
    if (schema->storage_size > SIZE_MAX - (type->align - 1u)) return true;
    aligned = (schema->storage_size + type->align - 1u) &
        ~(type->align - 1u);
    return aligned > SIZE_MAX - type->size ||
        aligned + type->size > schema->max_storage_bytes;
}

static vxml_status cmeta_register_variable(
    cmeta_program_builder *builder, size_t scope_index,
    salts_xml_node node, bool allow_repeat,
    size_t *out_slot, bool *out_repeated) {
    const salts_xml_attribute name_attribute = cmeta_attribute(node, "name");
    cmeta_decoded_value decoded = {0};
    salts_xml_string_view name = {0};
    const cmeta_data_field_desc *field;
    const cmeta_scope_slot *existing;
    cmeta_scope_schema *schema =
        &builder->profile->scopes[scope_index].schema;
    bool conflict = false;
    size_t slot = VXML_CMETA_NO_INDEX;
    vxml_status status = VXML_OK;
    if (out_slot != NULL) *out_slot = VXML_CMETA_NO_INDEX;
    if (out_repeated != NULL) *out_repeated = false;
    if (name_attribute.impl == NULL)
        return cmeta_program_fail(
            builder->diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_node_location(node),
            "VoiceXML var name must be a decoded XML NCName");
    status = cmeta_decode_temporary(
        builder, salts_xml_attribute_value(name_attribute),
        salts_xml_attribute_location(name_attribute), &decoded);
    if (status != VXML_OK) return status;
    name = decoded.view;
    if (!cmeta_ascii_ncname(name)) {
        status = cmeta_program_fail(
            builder->diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(name_attribute),
            "VoiceXML var name must be a decoded XML NCName");
        goto done;
    }
    field = cmeta_root_field(builder->profile->root, name, NULL);
    if (field == NULL) {
        status = cmeta_program_fail(
            builder->diagnostic, VXML_SEMANTIC_ERROR,
            salts_xml_attribute_location(name_attribute),
            "VoiceXML var name has no matching application-root field");
        goto done;
    }
    existing = cmeta_scope_find(schema, name.data, name.size, &slot);
    if (existing != NULL) {
        if (!allow_repeat) {
            status = cmeta_program_fail(
                builder->diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(name_attribute),
                "duplicate VoiceXML lexical declaration");
            goto done;
        }
        if (out_slot != NULL) *out_slot = slot;
        if (out_repeated != NULL) *out_repeated = true;
        goto done;
    }
    if (schema->slot_count >= builder->options->max_scope_slots) {
        status = cmeta_program_fail(
            builder->diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_attribute_location(name_attribute),
            "VoiceXML lexical scope exceeds max_scope_slots");
        goto done;
    }
    if (cmeta_scope_storage_limit_exceeded(schema, field->value)) {
        status = cmeta_program_fail(
            builder->diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_attribute_location(name_attribute),
            "VoiceXML lexical scope exceeds max_scope_storage_bytes");
        goto done;
    }
    if (!cmeta_scope_register(
            schema, name.data, name.size, field->value,
            &slot, &conflict)) {
        status = cmeta_program_fail(
            builder->diagnostic,
            conflict ? VXML_INVALID_STRUCTURE : VXML_ALLOCATION_FAILED,
            salts_xml_attribute_location(name_attribute),
            conflict ? "VoiceXML lexical name has conflicting types"
                     : "VoiceXML lexical schema allocation failed");
        goto done;
    }
    if (out_slot != NULL) *out_slot = slot;
done:
    cmeta_decoded_value_destroy(&decoded);
    return status;
}

static vxml_status cmeta_register_form_item(
    cmeta_program_builder *builder, size_t form_scope,
    salts_xml_node block, size_t block_index, size_t *out_slot,
    const char **out_name, size_t *out_name_size) {
    const salts_xml_attribute name_attribute = cmeta_attribute(block, "name");
    salts_xml_string_view name;
    char anonymous_name[48];
    bool conflict = false;
    size_t slot = VXML_CMETA_NO_INDEX;
    if (name_attribute.impl != NULL) {
        vxml_status status = cmeta_retain_decoded_view(
            builder, salts_xml_attribute_value(name_attribute),
            salts_xml_attribute_location(name_attribute),
            out_name, out_name_size);
        if (status != VXML_OK) return status;
        name.data = *out_name;
        name.size = *out_name_size;
        if (!cmeta_ascii_ncname(name))
            return cmeta_program_fail(
                builder->diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(name_attribute),
                "VoiceXML block name must be a decoded XML NCName");
    } else {
        const int written = snprintf(
            anonymous_name, sizeof(anonymous_name), "\x1f" "block:%zu",
            block_index);
        if (written <= 0 || (size_t)written >= sizeof(anonymous_name))
            return cmeta_program_fail(
                builder->diagnostic, VXML_LIMIT_EXCEEDED,
                salts_xml_node_location(block),
                "VoiceXML anonymous block identity overflow");
        name.data = anonymous_name;
        name.size = (size_t)written;
        *out_name = NULL;
        *out_name_size = 0u;
    }
    if (cmeta_scope_find(
            &builder->profile->scopes[form_scope].schema,
            name.data, name.size, NULL) != NULL)
        return cmeta_program_fail(
            builder->diagnostic, VXML_INVALID_STRUCTURE,
            name_attribute.impl != NULL
                ? salts_xml_attribute_location(name_attribute)
                : salts_xml_node_location(block),
            "VoiceXML form item collides with the dialog namespace");
    if (builder->profile->scopes[form_scope].schema.slot_count >=
            builder->options->max_scope_slots)
        return cmeta_program_fail(
            builder->diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(block),
            "VoiceXML dialog scope exceeds max_scope_slots");
    if (cmeta_scope_storage_limit_exceeded(
            &builder->profile->scopes[form_scope].schema,
            &cmeta_data_bool))
        return cmeta_program_fail(
            builder->diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(block),
            "VoiceXML dialog scope exceeds max_scope_storage_bytes");
    if (!cmeta_scope_register(
            &builder->profile->scopes[form_scope].schema,
            name.data, name.size, &cmeta_data_bool,
            &slot, &conflict))
        return cmeta_program_fail(
            builder->diagnostic,
            conflict ? VXML_INVALID_STRUCTURE : VXML_ALLOCATION_FAILED,
            salts_xml_node_location(block),
            conflict ? "VoiceXML block form-item type conflict"
                     : "VoiceXML form-item schema allocation failed");
    *out_slot = slot;
    return VXML_OK;
}

static vxml_status cmeta_register_executable_variables(
    cmeta_program_builder *builder, size_t scope_index,
    salts_xml_node container) {
    size_t index;
    for (index = 0u; index < salts_xml_node_child_count(container); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(container, index);
        if (cmeta_node_named(child, "var")) {
            const vxml_status status = cmeta_register_variable(
                builder, scope_index, child, true, NULL, NULL);
            if (status != VXML_OK) return status;
        }
        if (cmeta_node_named(child, "if")) {
            const vxml_status status = cmeta_register_executable_variables(
                builder, scope_index, child);
            if (status != VXML_OK) return status;
        }
    }
    return VXML_OK;
}

static vxml_status cmeta_build_schemas(
    cmeta_program_builder *builder, salts_xml_node root,
    const cmeta_program_measurement *measurement) {
    size_t capacity = measurement->declaration_count;
    size_t root_child;
    size_t scope_index;
    vxml_status status;
    if (capacity > SIZE_MAX - measurement->action_count ||
        capacity + measurement->action_count >
            SIZE_MAX - measurement->block_count)
        return cmeta_program_fail(
            builder->diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(root),
            "VoiceXML lexical schema capacity overflow");
    capacity += measurement->action_count + measurement->block_count;
    if (capacity > builder->options->max_scope_slots)
        capacity = builder->options->max_scope_slots;
    for (scope_index = 0u; scope_index < measurement->scope_count;
         ++scope_index) {
        vxml_cmeta_scope_row *scope = &builder->profile->scopes[scope_index];
        scope->kind = scope_index == 0u
            ? VXML_CMETA_SCOPE_DOCUMENT
            : scope_index <= measurement->form_count
                ? VXML_CMETA_SCOPE_FORM : VXML_CMETA_SCOPE_BLOCK;
        scope->owner = VXML_CMETA_NO_INDEX;
        if (!cmeta_scope_schema_init(
                &scope->schema, capacity,
                builder->options->max_scope_storage_bytes,
                &cmeta_program_scope_allocator))
            return cmeta_program_fail(
                builder->diagnostic, VXML_ALLOCATION_FAILED,
                salts_xml_node_location(root),
                "VoiceXML lexical schema allocation failed");
    }
    builder->profile->first_document_declaration = 0u;
    for (root_child = 0u;
         root_child < salts_xml_node_child_count(root); ++root_child) {
        const salts_xml_node child =
            salts_xml_node_child_at(root, root_child);
        if (cmeta_node_ignorable(child)) continue;
        if (cmeta_node_named(child, "var")) {
            const salts_xml_attribute name_attribute =
                cmeta_attribute(child, "name");
            vxml_cmeta_declaration_row *declaration =
                &builder->profile->declarations[builder->declaration_index++];
            status = cmeta_register_variable(
                builder, 0u, child, false, &declaration->slot, NULL);
            if (status != VXML_OK) return status;
            declaration->scope = 0u;
            status = cmeta_retain_decoded_view(
                builder, salts_xml_attribute_value(name_attribute),
                salts_xml_attribute_location(name_attribute),
                &declaration->name, &declaration->name_size);
            if (status != VXML_OK) return status;
            declaration->expression = VXML_CMETA_NO_INDEX;
            declaration->location = salts_xml_node_location(child);
            ++builder->profile->document_declaration_count;
            continue;
        }
        if (cmeta_node_named(child, "form")) {
            const size_t form_index = builder->form_index++;
            const size_t form_scope = 1u + form_index;
            vxml_cmeta_form_row *form = &builder->profile->forms[form_index];
            vxml_form_row *base_form = &builder->impl->forms[form_index];
            size_t form_child;
            form->scope = form_scope;
            form->first_declaration = builder->declaration_index;
            form->first_block = builder->block_index;
            builder->profile->scopes[form_scope].owner = form_index;
            base_form->first_block = builder->block_index;
            {
                const salts_xml_attribute id = cmeta_attribute(child, "id");
                if (id.impl != NULL) {
                    size_t prior_form;
                    salts_xml_string_view decoded_id;
                    status = cmeta_retain_decoded_view(
                        builder, salts_xml_attribute_value(id),
                        salts_xml_attribute_location(id),
                        &base_form->id, &base_form->id_size);
                    if (status != VXML_OK) return status;
                    decoded_id.data = base_form->id;
                    decoded_id.size = base_form->id_size;
                    if (!cmeta_is_ncname(decoded_id))
                        return cmeta_program_fail(
                            builder->diagnostic, VXML_INVALID_STRUCTURE,
                            salts_xml_attribute_location(id),
                            "VoiceXML form id must be a decoded XML NCName");
                    for (prior_form = 0u; prior_form < form_index;
                         ++prior_form) {
                        const vxml_form_row *previous =
                            &builder->impl->forms[prior_form];
                        if (previous->id != NULL &&
                            previous->id_size == base_form->id_size &&
                            memcmp(previous->id, base_form->id,
                                   base_form->id_size) == 0)
                            return cmeta_program_fail(
                                builder->diagnostic, VXML_DUPLICATE_ID,
                                salts_xml_attribute_location(id),
                                "duplicate VoiceXML form id");
                    }
                }
            }
            for (form_child = 0u;
                 form_child < salts_xml_node_child_count(child);
                 ++form_child) {
                const salts_xml_node item =
                    salts_xml_node_child_at(child, form_child);
                if (cmeta_node_ignorable(item)) continue;
                if (cmeta_node_named(item, "var")) {
                    const salts_xml_attribute name_attribute =
                        cmeta_attribute(item, "name");
                    vxml_cmeta_declaration_row *declaration =
                        &builder->profile->declarations[
                            builder->declaration_index++];
                    status = cmeta_register_variable(
                        builder, form_scope, item, false,
                        &declaration->slot, NULL);
                    if (status != VXML_OK) return status;
                    declaration->scope = form_scope;
                    status = cmeta_retain_decoded_view(
                        builder, salts_xml_attribute_value(name_attribute),
                        salts_xml_attribute_location(name_attribute),
                        &declaration->name, &declaration->name_size);
                    if (status != VXML_OK) return status;
                    declaration->expression = VXML_CMETA_NO_INDEX;
                    declaration->location = salts_xml_node_location(item);
                    continue;
                }
                if (cmeta_node_named(item, "block")) {
                    const size_t block_index = builder->block_index++;
                    const size_t block_scope =
                        1u + measurement->form_count + block_index;
                    vxml_cmeta_block_row *block =
                        &builder->profile->blocks[block_index];
                    block->form = form_index;
                    block->scope = block_scope;
                    block->initial_expression = VXML_CMETA_NO_INDEX;
                    block->condition = VXML_CMETA_NO_INDEX;
                    builder->profile->scopes[block_scope].owner = block_index;
                    status = cmeta_register_form_item(
                        builder, form_scope, item, block_index,
                        &block->form_item_slot, &block->name,
                        &block->name_size);
                    if (status != VXML_OK) return status;
                    status = cmeta_register_executable_variables(
                        builder, block_scope, item);
                    if (status != VXML_OK) return status;
                }
            }
            form->declaration_count =
                builder->declaration_index - form->first_declaration;
            form->block_count = builder->block_index - form->first_block;
            base_form->block_count = form->block_count;
        }
    }
    return VXML_OK;
}

static vxml_cmeta_expr_limits cmeta_expression_limits(
    const vxml_cmeta_compile_options_v1 *options) {
    return (vxml_cmeta_expr_limits){
        options->max_expression_bytes,
        options->max_expression_instructions,
        options->max_expression_operands,
        options->max_expression_depth,
        options->max_path_depth,
        options->max_literal_bytes,
        options->max_string_bytes};
}

static vxml_cmeta_value_kind cmeta_data_value_kind(
    const cmeta_data_desc *data) {
    if (data == NULL) return VXML_CMETA_VALUE_UNDEFINED;
    switch (data->kind) {
        case CMETA_DATA_BOOL: return VXML_CMETA_VALUE_BOOL;
        case CMETA_DATA_SINT: return VXML_CMETA_VALUE_SINT;
        case CMETA_DATA_UINT: return VXML_CMETA_VALUE_UINT;
        case CMETA_DATA_FLOAT: return VXML_CMETA_VALUE_FLOAT;
        case CMETA_DATA_STRING: return VXML_CMETA_VALUE_STRING;
        default: return VXML_CMETA_VALUE_UNDEFINED;
    }
}

static bool cmeta_value_compatible(
    const cmeta_data_desc *target, vxml_cmeta_value_kind value_kind) {
    return cmeta_data_value_kind(target) == value_kind;
}

static vxml_status cmeta_append_expression(
    cmeta_program_builder *builder, salts_xml_attribute attribute,
    const vxml_cmeta_expr_compile_scope *scopes, size_t scope_count,
    bool condition, size_t *out_expression) {
    cmeta_decoded_value source = {0};
    const vxml_cmeta_expr_limits limits =
        cmeta_expression_limits(builder->options);
    vxml_cmeta_expression_row *row;
    vxml_cmeta_expr_diagnostic expression_diagnostic = {0};
    vxml_status status;
    size_t scratch_bytes;
    if (builder->expression_index >= builder->profile->expression_count)
        return cmeta_program_fail(
            builder->diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_attribute_location(attribute),
            "VoiceXML expression row count changed between passes");
    status = cmeta_decode_temporary(
        builder, salts_xml_attribute_value(attribute),
        salts_xml_attribute_location(attribute), &source);
    if (status != VXML_OK) return status;
    row = &builder->profile->expressions[builder->expression_index];
    status = condition
        ? vxml_cmeta_expr_compile_condition(
            &row->program, source.view.data, source.view.size,
            builder->profile->root, scopes, scope_count,
            &limits, &expression_diagnostic)
        : vxml_cmeta_expr_compile_value(
            &row->program, source.view.data, source.view.size,
            builder->profile->root, scopes, scope_count,
            &limits, &expression_diagnostic);
    cmeta_decoded_value_destroy(&source);
    if (status != VXML_OK) {
        if (builder->diagnostic != NULL) {
            builder->diagnostic->status = status;
            builder->diagnostic->location =
                salts_xml_attribute_location(attribute);
            (void)snprintf(
                builder->diagnostic->message,
                sizeof(builder->diagnostic->message),
                "%s at expression byte offset %zu",
                expression_diagnostic.message[0] != '\0'
                    ? expression_diagnostic.message
                    : "VoiceXML CMeta expression error",
                expression_diagnostic.byte_offset);
        }
        return status;
    }
    row->location = salts_xml_attribute_location(attribute);
    scratch_bytes = vxml_cmeta_expr_program_scratch_bytes(&row->program);
    if (scratch_bytes > builder->profile->expression_scratch_bytes)
        builder->profile->expression_scratch_bytes = scratch_bytes;
    *out_expression = builder->expression_index++;
    return VXML_OK;
}

static bool cmeta_same_location_type(
    const cmeta_data_desc *left, const cmeta_data_desc *right) {
    return left != NULL && right != NULL &&
        cmeta_data_value_kind(left) != VXML_CMETA_VALUE_UNDEFINED &&
        cmeta_data_value_kind(left) == cmeta_data_value_kind(right) &&
        left->storage_type != NULL && right->storage_type != NULL &&
        cmeta_type_equal(left->storage_type, right->storage_type);
}

static vxml_status cmeta_add_location_candidate(
    cmeta_program_builder *builder, vxml_cmeta_location_row *row,
    size_t scope, size_t root_field,
    const cmeta_scope_schema *schema, cmeta_location location) {
    vxml_cmeta_location_candidate_row *candidate;
    if (builder->candidate_index >=
        builder->profile->location_candidate_count)
        return cmeta_program_fail(
            builder->diagnostic, VXML_LIMIT_EXCEEDED,
            row->location,
            "VoiceXML location candidate count changed between passes");
    if (cmeta_data_value_kind(location.value) == VXML_CMETA_VALUE_UNDEFINED)
        return cmeta_program_fail(
            builder->diagnostic, VXML_SEMANTIC_ERROR,
            row->location,
            "VoiceXML location terminal is not a supported scalar at byte offset 0");
    if (row->candidate_count != 0u &&
        !cmeta_same_location_type(row->value, location.value))
        return cmeta_program_fail(
            builder->diagnostic, VXML_SEMANTIC_ERROR,
            row->location,
            "VoiceXML location candidates have incompatible types at byte offset 0");
    candidate = &builder->profile->location_candidates[
        builder->candidate_index++];
    candidate->scope = scope;
    candidate->root_field = root_field;
    candidate->schema = schema;
    candidate->location = location;
    if (row->candidate_count == 0u) row->value = location.value;
    ++row->candidate_count;
    return VXML_OK;
}

static vxml_status cmeta_location_compile_error(
    cmeta_program_builder *builder, cmeta_location_status location_status,
    const cmeta_location_diagnostic *detail,
    salts_xml_location source_location) {
    const vxml_status status = location_status == CMETA_LOCATION_LIMIT_EXCEEDED
        ? VXML_LIMIT_EXCEEDED : VXML_SEMANTIC_ERROR;
    if (builder->diagnostic != NULL) {
        builder->diagnostic->status = status;
        builder->diagnostic->location = source_location;
        (void)snprintf(
            builder->diagnostic->message,
            sizeof(builder->diagnostic->message),
            "VoiceXML location path is invalid at byte offset %zu",
            detail != NULL ? detail->byte_offset : 0u);
    }
    return status;
}

static vxml_status cmeta_append_location_view(
    cmeta_program_builder *builder, salts_xml_string_view name,
    salts_xml_location source_location,
    const vxml_cmeta_expr_compile_scope *scopes, size_t scope_count,
    size_t *out_location) {
    vxml_cmeta_location_row *row;
    size_t index;
    size_t first_size = 0u;
    size_t path_depth = 1u;
    size_t root_field_index = VXML_CMETA_NO_INDEX;
    const cmeta_data_field_desc *root_field;
    if (builder->location_index >= builder->profile->location_count)
        return cmeta_program_fail(
            builder->diagnostic, VXML_LIMIT_EXCEEDED,
            source_location,
            "VoiceXML location row count changed between passes");
    if (!cmeta_location_path_valid(name.data, name.size, SIZE_MAX)) {
        cmeta_location invalid = {0};
        cmeta_location_diagnostic detail = {0};
        const cmeta_location_status location_status =
            cmeta_location_compile_detailed(
                &invalid, name.data, name.size, builder->profile->root,
                SIZE_MAX, &detail);
        return cmeta_location_compile_error(
            builder, location_status, &detail, source_location);
    }
    while (first_size < name.size && name.data[first_size] != '.')
        ++first_size;
    for (index = 0u; index < name.size; ++index) {
        if (name.data[index] == '.') {
            if (path_depth == SIZE_MAX ||
                ++path_depth > builder->options->max_path_depth)
                return cmeta_location_compile_error(
                    builder, CMETA_LOCATION_LIMIT_EXCEEDED, NULL,
                    source_location);
        }
    }
    row = &builder->profile->locations[builder->location_index];
    row->location = source_location;
    row->first_candidate = builder->candidate_index;
    row->name = cmeta_retain_view(builder, name);
    row->name_size = name.size;
    if (row->name == NULL)
        return cmeta_program_fail(
            builder->diagnostic, VXML_LIMIT_EXCEEDED,
            row->location, "VoiceXML retained location storage overflow");
    for (index = 0u; index < scope_count; ++index) {
        const cmeta_scope_slot *slot;
        size_t slot_index = VXML_CMETA_NO_INDEX;
        cmeta_location location = {0};
        slot = cmeta_scope_find(
            scopes[index].schema, name.data, first_size, &slot_index);
        if (slot == NULL) continue;
        location.root = builder->profile->root;
        location.value = slot->value;
        location.storage_size = slot->value->storage_type->size;
        location.slot = slot_index;
        location.kind = CMETA_LOCATION_SCOPE;
        if (first_size < name.size) {
            cmeta_location nested = {0};
            cmeta_location_diagnostic detail = {0};
            const char *suffix = name.data + first_size + 1u;
            const size_t suffix_size = name.size - first_size - 1u;
            const cmeta_location_status location_status =
                cmeta_location_compile_detailed(
                    &nested, suffix, suffix_size, slot->value,
                    builder->options->max_path_depth - 1u, &detail);
            if (location_status != CMETA_LOCATION_OK) {
                detail.byte_offset += first_size + 1u;
                return cmeta_location_compile_error(
                    builder, location_status, &detail, source_location);
            }
            location.value = nested.value;
            location.offset = nested.offset;
            location.storage_size = nested.storage_size;
        }
        {
            const vxml_status status = cmeta_add_location_candidate(
                builder, row, scopes[index].scope_id,
                VXML_CMETA_NO_INDEX, scopes[index].schema, location);
            if (status != VXML_OK) return status;
        }
    }
    root_field = cmeta_root_field(
        builder->profile->root,
        (salts_xml_string_view){name.data, first_size},
        &root_field_index);
    if (root_field != NULL) {
        cmeta_location location = {0};
        cmeta_location_diagnostic detail = {0};
        const cmeta_location_status location_status =
            cmeta_location_compile_detailed(
                &location, name.data, name.size, builder->profile->root,
                builder->options->max_path_depth, &detail);
        if (location_status != CMETA_LOCATION_OK)
            return cmeta_location_compile_error(
                builder, location_status, &detail, source_location);
        {
            const vxml_status status = cmeta_add_location_candidate(
                builder, row, VXML_CMETA_NO_INDEX,
                root_field_index, NULL, location);
            if (status != VXML_OK) return status;
        }
    }
    if (row->candidate_count == 0u)
        return cmeta_location_compile_error(
            builder, CMETA_LOCATION_UNKNOWN, NULL, row->location);
    *out_location = builder->location_index++;
    return VXML_OK;
}

static vxml_status cmeta_append_location(
    cmeta_program_builder *builder, salts_xml_attribute attribute,
    const vxml_cmeta_expr_compile_scope *scopes, size_t scope_count,
    size_t *out_location) {
    cmeta_decoded_value decoded = {0};
    vxml_status status = cmeta_decode_temporary(
        builder, salts_xml_attribute_value(attribute),
        salts_xml_attribute_location(attribute), &decoded);
    if (status == VXML_OK)
        status = cmeta_append_location_view(
            builder, decoded.view, salts_xml_attribute_location(attribute),
            scopes, scope_count, out_location);
    cmeta_decoded_value_destroy(&decoded);
    return status;
}

static void cmeta_compile_scope_chain(
    const vxml_cmeta_program_data *profile, size_t block_index,
    vxml_cmeta_expr_compile_scope scopes[3]) {
    const vxml_cmeta_block_row *block = &profile->blocks[block_index];
    const vxml_cmeta_form_row *form = &profile->forms[block->form];
    scopes[0] = (vxml_cmeta_expr_compile_scope){
        block->scope, &profile->scopes[block->scope].schema};
    scopes[1] = (vxml_cmeta_expr_compile_scope){
        form->scope, &profile->scopes[form->scope].schema};
    scopes[2] = (vxml_cmeta_expr_compile_scope){
        profile->document_scope,
        &profile->scopes[profile->document_scope].schema};
}

static vxml_status cmeta_compile_declaration_expression(
    cmeta_program_builder *builder, salts_xml_node node,
    vxml_cmeta_declaration_row *declaration,
    const vxml_cmeta_expr_compile_scope *scopes, size_t scope_count) {
    const salts_xml_attribute expression = cmeta_attribute(node, "expr");
    vxml_status status;
    if (expression.impl == NULL) return VXML_OK;
    status = cmeta_append_expression(
        builder, expression, scopes, scope_count,
        false, &declaration->expression);
    if (status != VXML_OK) return status;
    if (!cmeta_value_compatible(
            builder->profile->scopes[declaration->scope]
                .schema.slots[declaration->slot].value,
            vxml_cmeta_expr_program_value_kind(
                &builder->profile->expressions[
                    declaration->expression].program)))
        return cmeta_program_fail(
            builder->diagnostic, VXML_SEMANTIC_ERROR,
            salts_xml_attribute_location(expression),
            "VoiceXML declaration expression has an incompatible type");
    return VXML_OK;
}

static void cmeta_action_init(vxml_cmeta_action_row *action) {
    memset(action, 0, sizeof(*action));
    action->scope = VXML_CMETA_NO_INDEX;
    action->slot = VXML_CMETA_NO_INDEX;
    action->target = VXML_CMETA_NO_INDEX;
    action->expression = VXML_CMETA_NO_INDEX;
    action->first_location = VXML_CMETA_NO_INDEX;
    action->first_branch = VXML_CMETA_NO_INDEX;
}

static bool cmeta_prior_var_action(
    const cmeta_program_builder *builder, size_t first_action,
    size_t scope, size_t slot) {
    size_t index;
    for (index = first_action; index < builder->action_index; ++index) {
        const vxml_cmeta_action_row *action =
            &builder->profile->actions[index];
        if (action->kind == VXML_CMETA_ACTION_VAR &&
            action->scope == scope && action->slot == slot)
            return true;
    }
    return false;
}

static vxml_status cmeta_lower_namelist(
    cmeta_program_builder *builder, salts_xml_attribute attribute,
    const vxml_cmeta_expr_compile_scope *scopes, size_t scope_count,
    size_t *out_first, size_t *out_count) {
    cmeta_decoded_value decoded = {0};
    salts_xml_string_view list;
    const salts_xml_location location =
        salts_xml_attribute_location(attribute);
    salts_xml_string_view name;
    size_t cursor = 0u;
    vxml_status status = cmeta_decode_temporary(
        builder, salts_xml_attribute_value(attribute), location, &decoded);
    if (status != VXML_OK) return status;
    list = decoded.view;
    *out_first = builder->location_index;
    *out_count = 0u;
    while (cmeta_namelist_next(list, &cursor, &name)) {
        size_t ignored_location;
        status = cmeta_append_location_view(
            builder, name, location, scopes, scope_count,
            &ignored_location);
        if (status != VXML_OK) goto done;
        ++*out_count;
    }
    if (*out_count == 0u)
        status = cmeta_program_fail(
            builder->diagnostic, VXML_INVALID_STRUCTURE, location,
            "VoiceXML namelist must contain at least one name");
done:
    cmeta_decoded_value_destroy(&decoded);
    return status;
}

static vxml_status cmeta_lower_simple_action(
    cmeta_program_builder *builder, salts_xml_node node,
    size_t block_index, const vxml_cmeta_expr_compile_scope scopes[3],
    size_t first_action) {
    const vxml_cmeta_block_row *block = &builder->profile->blocks[block_index];
    vxml_cmeta_action_row *action;
    vxml_status status;
    if (builder->action_index >= builder->profile->action_count)
        return cmeta_program_fail(
            builder->diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(node),
            "VoiceXML action count changed between passes");
    action = &builder->profile->actions[builder->action_index];
    cmeta_action_init(action);
    action->location = salts_xml_node_location(node);
    if (cmeta_node_named(node, "var")) {
        const salts_xml_attribute name_attribute = cmeta_attribute(node, "name");
        const salts_xml_attribute expression = cmeta_attribute(node, "expr");
        cmeta_decoded_value decoded_name = {0};
        const cmeta_scope_slot *slot;
        size_t slot_index = VXML_CMETA_NO_INDEX;
        status = cmeta_decode_temporary(
            builder, salts_xml_attribute_value(name_attribute),
            salts_xml_attribute_location(name_attribute), &decoded_name);
        if (status != VXML_OK) return status;
        slot = cmeta_scope_find(
            &builder->profile->scopes[block->scope].schema,
            decoded_name.view.data, decoded_name.view.size, &slot_index);
        if (slot == NULL) {
            cmeta_decoded_value_destroy(&decoded_name);
            return cmeta_program_fail(
                builder->diagnostic, VXML_INVALID_STRUCTURE,
                salts_xml_node_location(node),
                "VoiceXML executable declaration changed between passes");
        }
        if (cmeta_prior_var_action(
                builder, first_action, block->scope, slot_index)) {
            action->kind = VXML_CMETA_ACTION_ASSIGN;
            status = cmeta_append_location(
                builder, name_attribute, scopes, 3u, &action->target);
            if (status != VXML_OK) {
                cmeta_decoded_value_destroy(&decoded_name);
                return status;
            }
        } else {
            action->kind = VXML_CMETA_ACTION_VAR;
            action->scope = block->scope;
            action->slot = slot_index;
        }
        if (expression.impl != NULL) {
            status = cmeta_append_expression(
                builder, expression, scopes, 3u, false,
                &action->expression);
            if (status != VXML_OK) {
                cmeta_decoded_value_destroy(&decoded_name);
                return status;
            }
            if (!cmeta_value_compatible(
                    slot->value,
                    vxml_cmeta_expr_program_value_kind(
                        &builder->profile->expressions[
                            action->expression].program))) {
                cmeta_decoded_value_destroy(&decoded_name);
                return cmeta_program_fail(
                    builder->diagnostic, VXML_SEMANTIC_ERROR,
                    salts_xml_attribute_location(expression),
                    "VoiceXML executable var expression has an incompatible type");
            }
        }
        cmeta_decoded_value_destroy(&decoded_name);
    } else if (cmeta_node_named(node, "assign")) {
            const salts_xml_attribute name_attribute =
                cmeta_attribute(node, "name");
            const salts_xml_attribute expression =
                cmeta_attribute(node, "expr");
            action->kind = VXML_CMETA_ACTION_ASSIGN;
            if (name_attribute.impl == NULL || expression.impl == NULL)
                return cmeta_program_fail(
                    builder->diagnostic, VXML_INVALID_STRUCTURE,
                    salts_xml_node_location(node),
                    "VoiceXML assign requires name and expr");
            status = cmeta_append_location(
                builder, name_attribute, scopes, 3u, &action->target);
            if (status != VXML_OK) return status;
            status = cmeta_append_expression(
                builder, expression, scopes, 3u, false,
                &action->expression);
            if (status != VXML_OK) return status;
            if (!cmeta_value_compatible(
                    builder->profile->locations[action->target].value,
                    vxml_cmeta_expr_program_value_kind(
                        &builder->profile->expressions[
                            action->expression].program)))
                return cmeta_program_fail(
                    builder->diagnostic, VXML_SEMANTIC_ERROR,
                    salts_xml_attribute_location(expression),
                    "VoiceXML assignment expression has an incompatible type");
    } else if (cmeta_node_named(node, "clear")) {
            const salts_xml_attribute namelist =
                cmeta_attribute(node, "namelist");
            action->kind = VXML_CMETA_ACTION_CLEAR;
            action->clear_all_form_items = namelist.impl == NULL;
            if (namelist.impl != NULL) {
                status = cmeta_lower_namelist(
                    builder, namelist, scopes, 3u,
                    &action->first_location, &action->location_count);
                if (status != VXML_OK) return status;
            }
    } else if (cmeta_node_named(node, "exit")) {
            const salts_xml_attribute expression =
                cmeta_attribute(node, "expr");
            const salts_xml_attribute namelist =
                cmeta_attribute(node, "namelist");
            action->kind = VXML_CMETA_ACTION_EXIT;
            if (expression.impl != NULL && namelist.impl != NULL)
                return cmeta_program_fail(
                    builder->diagnostic, VXML_INVALID_STRUCTURE,
                    salts_xml_node_location(node),
                    "error.badfetch: exit expr and namelist are mutually exclusive");
            action->exit_kind = expression.impl != NULL
                ? VXML_CMETA_EXIT_EXPRESSION
                : namelist.impl != NULL
                    ? VXML_CMETA_EXIT_NAMELIST : VXML_CMETA_EXIT_EMPTY;
            if (expression.impl != NULL) {
                status = cmeta_append_expression(
                    builder, expression, scopes, 3u, false,
                    &action->expression);
                if (status != VXML_OK) return status;
            }
            if (namelist.impl != NULL) {
                status = cmeta_lower_namelist(
                    builder, namelist, scopes, 3u,
                    &action->first_location, &action->location_count);
                if (status != VXML_OK) return status;
            }
            if (builder->generic_action_index >= builder->impl->action_count)
                return cmeta_program_fail(
                    builder->diagnostic, VXML_LIMIT_EXCEEDED,
                    action->location,
                    "VoiceXML exit count changed between passes");
            builder->impl->actions[builder->generic_action_index++].kind =
                VXML_ACTION_EXIT;
    } else {
        return cmeta_program_fail(
            builder->diagnostic, VXML_UNSUPPORTED_FEATURE,
            salts_xml_node_location(node),
            "unsupported VoiceXML executable element");
    }
    action->next_action = builder->action_index + 1u;
    ++builder->action_index;
    return VXML_OK;
}

static vxml_status cmeta_lower_executable(
    cmeta_program_builder *builder, salts_xml_node node,
    size_t block_index, const vxml_cmeta_expr_compile_scope scopes[3],
    size_t first_action, size_t conditional_depth);

static vxml_status cmeta_lower_conditional(
    cmeta_program_builder *builder, salts_xml_node node,
    size_t block_index, const vxml_cmeta_expr_compile_scope scopes[3],
    size_t first_action, size_t conditional_depth) {
    const salts_xml_attribute condition = cmeta_attribute(node, "cond");
    vxml_cmeta_action_row *action;
    size_t action_index;
    size_t branch_count = 1u;
    size_t branch_offset = 0u;
    size_t child_index;
    vxml_status status;
    if (conditional_depth > builder->options->max_conditional_depth)
        return cmeta_program_fail(
            builder->diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(node),
            "VoiceXML conditional depth exceeds max_conditional_depth");
    for (child_index = 0u;
         child_index < salts_xml_node_child_count(node); ++child_index) {
        const salts_xml_node child = salts_xml_node_child_at(node, child_index);
        if (cmeta_node_named(child, "elseif") ||
            cmeta_node_named(child, "else")) {
            if (!cmeta_measure_increment(&branch_count))
                return cmeta_program_fail(
                    builder->diagnostic, VXML_LIMIT_EXCEEDED,
                    salts_xml_node_location(child),
                    "VoiceXML conditional branch count overflow");
        }
    }
    if (builder->action_index >= builder->profile->action_count ||
        builder->branch_index > builder->profile->branch_count ||
        branch_count > builder->profile->branch_count - builder->branch_index)
        return cmeta_program_fail(
            builder->diagnostic, VXML_LIMIT_EXCEEDED,
            salts_xml_node_location(node),
            "VoiceXML conditional rows changed between passes");
    action_index = builder->action_index++;
    action = &builder->profile->actions[action_index];
    cmeta_action_init(action);
    action->kind = VXML_CMETA_ACTION_IF;
    action->location = salts_xml_node_location(node);
    action->first_branch = builder->branch_index;
    action->branch_count = branch_count;
    builder->branch_index += branch_count;
    for (child_index = 0u; child_index < branch_count; ++child_index) {
        vxml_cmeta_branch_row *branch =
            &builder->profile->branches[action->first_branch + child_index];
        branch->condition = VXML_CMETA_NO_INDEX;
    }
    {
        vxml_cmeta_branch_row *branch =
            &builder->profile->branches[action->first_branch];
        branch->location = salts_xml_node_location(node);
        branch->first_action = builder->action_index;
        status = cmeta_append_expression(
            builder, condition, scopes, 3u, true, &branch->condition);
        if (status != VXML_OK) return status;
    }
    for (child_index = 0u;
         child_index < salts_xml_node_child_count(node); ++child_index) {
        const salts_xml_node child = salts_xml_node_child_at(node, child_index);
        vxml_cmeta_branch_row *branch =
            &builder->profile->branches[action->first_branch + branch_offset];
        if (cmeta_node_ignorable(child)) continue;
        if (cmeta_node_named(child, "elseif") ||
            cmeta_node_named(child, "else")) {
            branch->action_end = builder->action_index;
            ++branch_offset;
            branch = &builder->profile->branches[
                action->first_branch + branch_offset];
            branch->location = salts_xml_node_location(child);
            branch->first_action = builder->action_index;
            if (cmeta_node_named(child, "elseif")) {
                status = cmeta_append_expression(
                    builder, cmeta_attribute(child, "cond"), scopes, 3u,
                    true, &branch->condition);
                if (status != VXML_OK) return status;
            }
            continue;
        }
        status = cmeta_lower_executable(
            builder, child, block_index, scopes, first_action,
            conditional_depth);
        if (status != VXML_OK) return status;
    }
    builder->profile->branches[
        action->first_branch + branch_offset].action_end =
            builder->action_index;
    action->next_action = builder->action_index;
    return VXML_OK;
}

static vxml_status cmeta_lower_executable(
    cmeta_program_builder *builder, salts_xml_node node,
    size_t block_index, const vxml_cmeta_expr_compile_scope scopes[3],
    size_t first_action, size_t conditional_depth) {
    if (cmeta_node_named(node, "if")) {
        if (conditional_depth == SIZE_MAX)
            return cmeta_program_fail(
                builder->diagnostic, VXML_LIMIT_EXCEEDED,
                salts_xml_node_location(node),
                "VoiceXML conditional depth overflow");
        return cmeta_lower_conditional(
            builder, node, block_index, scopes, first_action,
            conditional_depth + 1u);
    }
    return cmeta_lower_simple_action(
        builder, node, block_index, scopes, first_action);
}

static vxml_status cmeta_lower_block_actions(
    cmeta_program_builder *builder, salts_xml_node block_node,
    size_t block_index) {
    vxml_cmeta_block_row *block = &builder->profile->blocks[block_index];
    vxml_block_row *base_block = &builder->impl->blocks[block_index];
    vxml_cmeta_expr_compile_scope scopes[3];
    size_t child_index;
    vxml_status status;
    cmeta_compile_scope_chain(builder->profile, block_index, scopes);
    {
        const salts_xml_attribute expression =
            cmeta_attribute(block_node, "expr");
        const salts_xml_attribute condition =
            cmeta_attribute(block_node, "cond");
        if (expression.impl != NULL) {
            status = cmeta_append_expression(
                builder, expression, scopes, 3u, true,
                &block->initial_expression);
            if (status != VXML_OK) return status;
        }
        if (condition.impl != NULL) {
            status = cmeta_append_expression(
                builder, condition, scopes, 3u, true,
                &block->condition);
            if (status != VXML_OK) return status;
        }
    }
    block->first_action = builder->action_index;
    base_block->first_action = builder->generic_action_index;
    for (child_index = 0u;
         child_index < salts_xml_node_child_count(block_node);
         ++child_index) {
        const salts_xml_node node =
            salts_xml_node_child_at(block_node, child_index);
        if (cmeta_node_ignorable(node)) continue;
        status = cmeta_lower_executable(
            builder, node, block_index, scopes, block->first_action, 0u);
        if (status != VXML_OK) return status;
    }
    block->action_end = builder->action_index;
    base_block->action_count =
        builder->generic_action_index - base_block->first_action;
    return VXML_OK;
}

static vxml_status cmeta_lower_program(
    cmeta_program_builder *builder, salts_xml_node root) {
    size_t declaration_index = 0u;
    size_t form_index = 0u;
    size_t block_index = 0u;
    size_t root_child;
    vxml_status status;
    for (root_child = 0u;
         root_child < salts_xml_node_child_count(root); ++root_child) {
        const salts_xml_node child =
            salts_xml_node_child_at(root, root_child);
        if (cmeta_node_ignorable(child)) continue;
        if (cmeta_node_named(child, "var")) {
            const vxml_cmeta_expr_compile_scope scope = {
                0u, &builder->profile->scopes[0].schema};
            status = cmeta_compile_declaration_expression(
                builder, child,
                &builder->profile->declarations[declaration_index++],
                &scope, 1u);
            if (status != VXML_OK) return status;
            continue;
        }
        if (cmeta_node_named(child, "form")) {
            const vxml_cmeta_form_row *form =
                &builder->profile->forms[form_index];
            const vxml_cmeta_expr_compile_scope scopes[2] = {
                {form->scope,
                 &builder->profile->scopes[form->scope].schema},
                {0u, &builder->profile->scopes[0].schema}};
            size_t form_child;
            for (form_child = 0u;
                 form_child < salts_xml_node_child_count(child);
                 ++form_child) {
                const salts_xml_node item =
                    salts_xml_node_child_at(child, form_child);
                if (cmeta_node_ignorable(item)) continue;
                if (cmeta_node_named(item, "var")) {
                    status = cmeta_compile_declaration_expression(
                        builder, item,
                        &builder->profile->declarations[
                            declaration_index++],
                        scopes, 2u);
                    if (status != VXML_OK) return status;
                } else if (cmeta_node_named(item, "block")) {
                    status = cmeta_lower_block_actions(
                        builder, item, block_index++);
                    if (status != VXML_OK) return status;
                }
            }
            ++form_index;
        }
    }
    return VXML_OK;
}

static void cmeta_program_impl_destroy(vxml_program_impl *impl) {
    if (impl == NULL) return;
    vxml_free(impl->actions);
    vxml_free(impl->blocks);
    vxml_free(impl->forms);
    vxml_free(impl);
}

static vxml_status cmeta_write_program(
    const char *bytes, size_t size, const vxml_limits *limits,
    const vxml_cmeta_compile_options_v1 *options,
    const cmeta_program_measurement *measurement,
    vxml_program *out, vxml_diagnostic *diagnostic) {
    salts_xml_document document = {0};
    salts_xml_diagnostic xml_diagnostic = {0};
    salts_xml_status xml_status;
    vxml_program_impl *impl = NULL;
    vxml_cmeta_program_data *profile = NULL;
    cmeta_program_builder builder = {0};
    vxml_status status;
    xml_status = salts_xml_parse(
        &document, bytes, size, &limits->xml, &xml_diagnostic);
    if (xml_status != SALTS_XML_OK)
        return cmeta_program_fail(
            diagnostic, map_cmeta_xml_status(xml_status),
            xml_diagnostic.location, xml_diagnostic.message);
    if (!cmeta_allocate_rows(
            measurement, size, options, &impl, &profile)) {
        const salts_xml_location location =
            salts_xml_node_location(salts_xml_document_root(&document));
        salts_xml_document_destroy(&document);
        return cmeta_program_fail(
            diagnostic, VXML_ALLOCATION_FAILED, location,
            "VoiceXML CMeta immutable row allocation failed");
    }
    builder.impl = impl;
    builder.profile = profile;
    builder.options = options;
    builder.diagnostic = diagnostic;
    status = cmeta_build_schemas(
        &builder, salts_xml_document_root(&document), measurement);
    if (status == VXML_OK)
        status = cmeta_lower_program(
            &builder, salts_xml_document_root(&document));
    if (status == VXML_OK &&
        (builder.form_index != measurement->form_count ||
         builder.block_index != measurement->block_count ||
         builder.declaration_index != measurement->declaration_count ||
         builder.action_index != measurement->action_count ||
         builder.branch_index != measurement->branch_count ||
         builder.expression_index != measurement->expression_count ||
         builder.location_index != measurement->location_count ||
         builder.generic_action_index != measurement->exit_count))
        status = cmeta_program_fail(
            diagnostic, VXML_INVALID_STRUCTURE,
            salts_xml_node_location(salts_xml_document_root(&document)),
            "VoiceXML CMeta document changed between compiler passes");
    if (status == VXML_OK) {
        profile->location_candidate_count = builder.candidate_index;
        profile->string_size = builder.string_index;
        impl->storage = profile->strings;
        impl->storage_size = profile->string_size;
        impl->profile_kind = VXML_PROFILE_CMETA;
        impl->profile_data = profile;
        impl->profile_session_init = vxml_cmeta_session_init_profile;
        impl->profile_session_start = vxml_cmeta_session_start_profile;
        impl->profile_session_destroy = vxml_cmeta_session_destroy_profile;
        impl->profile_program_destroy = vxml_cmeta_program_destroy_profile;
        out->impl = impl;
        profile = NULL;
        impl = NULL;
    }
    cmeta_program_data_destroy(profile);
    cmeta_program_impl_destroy(impl);
    salts_xml_document_destroy(&document);
    return status;
}

vxml_status vxml_compile_cmeta(
    const void *bytes, size_t size, const vxml_limits *limits,
    const vxml_cmeta_compile_options_v1 *options,
    vxml_program *out, vxml_diagnostic *diagnostic) {
    const vxml_limits active_limits =
        limits != NULL ? *limits : vxml_default_limits();
    cmeta_program_measurement measurement = {0};
    vxml_status status;

    if (out != NULL) out->impl = NULL;
    if (!compile_options_valid(options)) return VXML_INVALID_CONTRACT;
    if (bytes == NULL || size == 0u || out == NULL)
        return VXML_INVALID_CONTRACT;
    status = admit_cmeta_datamodel(
        (const char *)bytes, size, &active_limits, diagnostic);
    if (status != VXML_OK) return status;
    status = cmeta_measure_program(
        (const char *)bytes, size, &active_limits,
        &measurement, diagnostic);
    if (status != VXML_OK) return status;
    return cmeta_write_program(
        (const char *)bytes, size, &active_limits, options,
        &measurement, out, diagnostic);
}

void vxml_cmeta_program_destroy_profile(vxml_program_impl *program) {
    vxml_cmeta_program_data *profile;
    if (program == NULL) return;
    profile = (vxml_cmeta_program_data *)program->profile_data;
    if (profile == NULL) return;
    cmeta_program_data_destroy(profile);
    vxml_free(program->actions);
    vxml_free(program->blocks);
    vxml_free(program->forms);
    program->actions = NULL;
    program->blocks = NULL;
    program->forms = NULL;
    program->storage = NULL;
    program->profile_data = NULL;
}

vxml_status vxml_cmeta_session_init_profile(
    vxml_session_impl *session, const void *options) {
    (void)session;
    return session_options_valid((const vxml_cmeta_session_options_v1 *)options)
        ? VXML_OK : VXML_INVALID_CONTRACT;
}

vxml_status vxml_cmeta_session_start_profile(vxml_session_impl *session) {
    return vxml_session_start_literal(session);
}

void vxml_cmeta_session_destroy_profile(vxml_session_impl *session) {
    (void)session;
}

vxml_status vxml_session_init_cmeta(
    vxml_session *session, const vxml_program *program,
    const vxml_cmeta_session_options_v1 *options) {
    if (session == NULL) return VXML_INVALID_ARGUMENT;
    session->impl = NULL;
    if (!session_options_valid(options)) return VXML_INVALID_CONTRACT;
    if (program == NULL || program->impl == NULL) return VXML_INVALID_ARGUMENT;
    if (((const vxml_program_impl *)program->impl)->profile_kind !=
        VXML_PROFILE_CMETA)
        return VXML_INVALID_CONTRACT;
    return vxml_session_init_profile(session, program, options);
}

static const vxml_session_impl *cmeta_session(const vxml_session *session) {
    const vxml_session_impl *impl;
    if (session == NULL || session->impl == NULL) return NULL;
    impl = (const vxml_session_impl *)session->impl;
    return impl->program != NULL && impl->program->profile_kind == VXML_PROFILE_CMETA
        ? impl : NULL;
}

vxml_status vxml_session_cmeta_read(
    const vxml_session *session, const char *name, size_t name_size,
    vxml_cmeta_value_view *out_value) {
    if (out_value != NULL) *out_value = (vxml_cmeta_value_view){0};
    if (name == NULL || name_size == 0u || out_value == NULL)
        return VXML_INVALID_ARGUMENT;
    return cmeta_session(session) != NULL ? VXML_SEMANTIC_ERROR : VXML_INVALID_CONTRACT;
}

vxml_status vxml_session_cmeta_exit_kind(
    const vxml_session *session, vxml_cmeta_exit_kind *out_kind) {
    const vxml_session_impl *impl = cmeta_session(session);
    if (out_kind == NULL) return VXML_INVALID_ARGUMENT;
    if (impl == NULL) return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_EXITED) return VXML_INVALID_STATE;
    *out_kind = VXML_CMETA_EXIT_EMPTY;
    return VXML_OK;
}

size_t vxml_session_cmeta_exit_count(const vxml_session *session) {
    return cmeta_session(session) != NULL ? 0u : 0u;
}

vxml_status vxml_session_cmeta_exit_at(
    const vxml_session *session, size_t index, vxml_cmeta_name_view *out_name,
    vxml_cmeta_value_view *out_value) {
    if (out_name != NULL) *out_name = (vxml_cmeta_name_view){0};
    if (out_value != NULL) *out_value = (vxml_cmeta_value_view){0};
    if (out_name == NULL || out_value == NULL) return VXML_INVALID_ARGUMENT;
    if (cmeta_session(session) == NULL) return VXML_INVALID_CONTRACT;
    (void)index;
    return VXML_INVALID_ARGUMENT;
}
