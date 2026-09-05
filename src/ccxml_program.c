#include "ccxml_internal.h"
#include "scxml_time.h"
#include "scxml_xml_decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CCXML_NAMESPACE "http://www.w3.org/2002/09/ccxml"
#define CCXML_DEFAULT_MAX_TRANSITIONS 1024u
#define CCXML_DEFAULT_MAX_ACTIONS 4096u
#define CCXML_DEFAULT_MAX_NAME_BYTES (256u * 1024u)
#define CCXML_DEFAULT_MAX_FOREACH_ITERATIONS 256u
#define CCXML_DEFAULT_MAX_FOREACH_STORAGE_BYTES (1024u * 1024u)

typedef struct ccxml_measurement {
    size_t transition_count;
    size_t action_count;
    size_t payload_count;
    size_t name_bytes;
    size_t max_transition_actions;
    size_t max_transition_effects;
    size_t max_send_payload_entries;
} ccxml_measurement;

typedef enum ccxml_copy_item_kind {
    CCXML_COPY_ITEM_LEAF = 1,
    CCXML_COPY_ITEM_IF,
    CCXML_COPY_ITEM_ELSEIF,
    CCXML_COPY_ITEM_ELSE,
    CCXML_COPY_ITEM_ENDIF,
    CCXML_COPY_ITEM_FOREACH,
    CCXML_COPY_ITEM_ENDFOREACH
} ccxml_copy_item_kind;

typedef struct ccxml_copy_item {
    salts_xml_node node;
    ccxml_copy_item_kind kind;
    size_t branch_next;
    size_t block_end;
    size_t branch_owner;
} ccxml_copy_item;

static const cmeta_type_identity ccxml_event_identity =
    CMETA_TYPE_ID_ATOM_INIT("turbo.ccxml.event");
static const cmeta_type_traits ccxml_event_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY};
const cmeta_type_desc ccxml_event_cmeta_type = {
    .name = "ccxml_event",
    .size = sizeof(ccxml_event),
    .align = _Alignof(ccxml_event),
    .kind = CMETA_T_OBJECT,
    .traits = &ccxml_event_traits,
    .identity = &ccxml_event_identity};

static bool checked_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) return false;
    *out = left + right;
    return true;
}

static bool checked_multiply(size_t left, size_t right, size_t *out) {
    if (out == NULL || (right != 0u && left > SIZE_MAX / right))
        return false;
    *out = left * right;
    return true;
}

static bool view_equal(salts_xml_string_view view, const char *text) {
    const size_t size = text != NULL ? strlen(text) : 0u;
    return view.data != NULL && view.size == size &&
           memcmp(view.data, text, size) == 0;
}

static bool view_has_space(salts_xml_string_view view) {
    size_t index;
    for (index = 0u; index < view.size; ++index) {
        const char value = view.data[index];
        if (value == ' ' || value == '\t' || value == '\r' || value == '\n')
            return true;
    }
    return false;
}

static bool text_is_whitespace(salts_xml_string_view view) {
    size_t index;
    for (index = 0u; index < view.size; ++index) {
        const char value = view.data[index];
        if (value != ' ' && value != '\t' && value != '\r' && value != '\n')
            return false;
    }
    return true;
}

static bool xml_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static bool namelist_token_next(
    const char *data, size_t size, size_t *cursor,
    salts_xml_string_view *out) {
    size_t begin;
    if (data == NULL || cursor == NULL || out == NULL || *cursor > size)
        return false;
    while (*cursor < size && xml_space(data[*cursor])) ++*cursor;
    if (*cursor == size) return false;
    begin = *cursor;
    while (*cursor < size && !xml_space(data[*cursor])) ++*cursor;
    *out = (salts_xml_string_view){data + begin, *cursor - begin};
    return true;
}

static bool node_is_ignorable(salts_xml_node node) {
    const salts_xml_node_kind kind = salts_xml_node_type(node);
    return kind == SALTS_XML_COMMENT ||
           kind == SALTS_XML_PROCESSING_INSTRUCTION ||
           (kind == SALTS_XML_TEXT &&
            text_is_whitespace(salts_xml_node_value(node)));
}

static bool node_has_unqualified_attribute(
    salts_xml_node node, const char *name) {
    size_t index;
    for (index = 0u; index < salts_xml_node_attribute_count(node); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(node, index);
        if (salts_xml_attribute_namespace_uri(attribute).size == 0u &&
            view_equal(salts_xml_attribute_local_name(attribute), name))
            return true;
    }
    return false;
}

static salts_xml_attribute node_unqualified_attribute(
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

static bool view_has_nonspace(salts_xml_string_view view) {
    size_t index;
    for (index = 0u; index < view.size; ++index) {
        const char value = view.data[index];
        if (value != ' ' && value != '\t' && value != '\r' && value != '\n')
            return true;
    }
    return false;
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
    return ncname_start(codepoint) || codepoint == '-' ||
           (codepoint >= '0' && codepoint <= '9') || codepoint == 0xb7u ||
           (codepoint >= 0x300u && codepoint <= 0x36fu) ||
           (codepoint >= 0x203fu && codepoint <= 0x2040u);
}

static bool dotted_location_valid(salts_xml_string_view location) {
    size_t cursor = 0u;
    bool segment_start = true;
    if (location.data == NULL || location.size == 0u) return false;
    while (cursor < location.size) {
        uint32_t codepoint;
        if (location.data[cursor] == '.') {
            if (segment_start) return false;
            segment_start = true;
            ++cursor;
            continue;
        }
        if (!decode_utf8(
                location.data, location.size, &cursor, &codepoint) ||
            (segment_start ? !ncname_start(codepoint)
                           : !ncname_continue(codepoint)))
            return false;
        segment_start = false;
    }
    return !segment_start;
}

static ccxml_status fail(
    ccxml_diagnostic *diagnostic, ccxml_status status,
    salts_xml_location location, const char *message) {
    if (diagnostic != NULL) {
        diagnostic->status = status;
        diagnostic->location = location;
        if (message == NULL) message = "CCXML error";
        (void)snprintf(
            diagnostic->message, sizeof(diagnostic->message), "%s", message);
    }
    return status;
}

static ccxml_status build_native_statechart(
    ccxml_program_impl *impl, salts_xml_location location,
    ccxml_diagnostic *diagnostic) {
    const size_t runtime_transition_count =
        impl->transition_count != 0u ? impl->transition_count : 1u;
    const cflow_statechart_state states[] = {
        {1u, 0u, CFLOW_STATECHART_COMPOUND, 0u},
        {2u, 1u, CFLOW_STATECHART_INITIAL, 1u},
        {3u, 1u, CFLOW_STATECHART_ATOMIC, 2u}};
    const cflow_event_type event = {1u, &ccxml_event_cmeta_type};
    cflow_statechart_guard *guards = NULL;
    cflow_statechart_executable *executables = NULL;
    cflow_statechart_transition *transitions = NULL;
    cflow_statechart_transition_action *transition_actions = NULL;
    cflow_statechart_definition definition = {0};
    cflow_statechart_status native_status;
    size_t index;
    ccxml_status status = CCXML_OK;

    if (runtime_transition_count > SIZE_MAX / sizeof(*guards) ||
        runtime_transition_count >= SIZE_MAX / sizeof(*transitions) ||
        impl->transition_count > SIZE_MAX / sizeof(*executables) ||
        impl->transition_count > SIZE_MAX / sizeof(*transition_actions)) {
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED, location,
            "CCXML native Statechart lowering exceeds storage limits");
    }
    guards = (cflow_statechart_guard *)calloc(
        runtime_transition_count, sizeof(*guards));
    transitions = (cflow_statechart_transition *)calloc(
        runtime_transition_count + 1u, sizeof(*transitions));
    if (impl->transition_count != 0u) {
        executables = (cflow_statechart_executable *)calloc(
            impl->transition_count, sizeof(*executables));
        transition_actions = (cflow_statechart_transition_action *)calloc(
            impl->transition_count, sizeof(*transition_actions));
    }
    if (guards == NULL || transitions == NULL ||
        (impl->transition_count != 0u &&
         (executables == NULL || transition_actions == NULL))) {
        status = fail(
            diagnostic, CCXML_ALLOCATION_FAILED, location,
            "CCXML native Statechart lowering allocation failed");
        goto cleanup;
    }
    transitions[0] = (cflow_statechart_transition){
        1u,
        2u,
        CFLOW_STATECHART_TRIGGER_EVENTLESS,
        0u,
        0u,
        0u,
        3u,
        CFLOW_STATECHART_TRANSITION_EXTERNAL,
        0u,
        0u};
    for (index = 0u; index < runtime_transition_count; ++index) {
        const cflow_statechart_guard_id guard_id =
            (cflow_statechart_guard_id)(index + 1u);
        const cflow_statechart_transition_id transition_id =
            (cflow_statechart_transition_id)(index + 2u);
        const bool reads_state = index < impl->transition_count &&
            (impl->transitions[index].state != NULL ||
             impl->transitions[index].condition != NULL);
        guards[index] = (cflow_statechart_guard){
            guard_id,
            &cmeta_type_bool,
            reads_state ? CMETA_EFFECT_MAY_FAIL : CMETA_EFFECT_PURE,
            reads_state
                ? CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS
                : CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS};
        transitions[index + 1u] = (cflow_statechart_transition){
            transition_id,
            3u,
            CFLOW_STATECHART_TRIGGER_EVENT,
            1u,
            0u,
            guard_id,
            0u,
            CFLOW_STATECHART_TRANSITION_INTERNAL,
            (uint32_t)(index + 1u),
            (uint32_t)(index + 1u)};
        if (index < impl->transition_count) {
            const cflow_statechart_executable_id executable_id =
                (cflow_statechart_executable_id)(index + 1u);
            executables[index] = (cflow_statechart_executable){
                executable_id,
                &cmeta_type_bool,
                CMETA_EFFECT_STATEFUL | CMETA_EFFECT_MAY_FAIL |
                    CMETA_EFFECT_IO,
                CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS};
            transition_actions[index] =
                (cflow_statechart_transition_action){
                    transition_id, executable_id, 0u};
        }
    }
    definition = (cflow_statechart_definition){
        .state_type = &cmeta_type_bool,
        .states = states,
        .state_count = sizeof(states) / sizeof(states[0]),
        .events = &event,
        .event_count = 1u,
        .guards = guards,
        .guard_count = runtime_transition_count,
        .executables = executables,
        .executable_count = impl->transition_count,
        .transitions = transitions,
        .transition_count = runtime_transition_count + 1u,
        .transition_actions = transition_actions,
        .transition_action_count = impl->transition_count};
    native_status = cflow_statechart_build(&impl->statechart, &definition);
    if (native_status != CFLOW_STATECHART_OK) {
        char message[CCXML_DIAGNOSTIC_CAPACITY];
        (void)snprintf(
            message, sizeof(message),
            "native Statechart rejected CCXML lowering (status=%d)",
            (int)native_status);
        status = fail(
            diagnostic,
            native_status == CFLOW_STATECHART_ALLOCATION_FAILED
                ? CCXML_ALLOCATION_FAILED : CCXML_INVALID_CONTRACT,
            location, message);
    }

cleanup:
    free(transition_actions);
    free(transitions);
    free(executables);
    free(guards);
    return status;
}

static ccxml_status validate_attributes(
    salts_xml_node node, const char *only_name, bool required,
    salts_xml_attribute *out_attribute, ccxml_diagnostic *diagnostic) {
    size_t index;
    salts_xml_attribute found = {0};
    for (index = 0u; index < salts_xml_node_attribute_count(node); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(node, index);
        const salts_xml_string_view namespace_uri =
            salts_xml_attribute_namespace_uri(attribute);
        const salts_xml_string_view local_name =
            salts_xml_attribute_local_name(attribute);
        if (namespace_uri.size != 0u || only_name == NULL ||
            !view_equal(local_name, only_name) || found.impl != NULL) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported or duplicate CCXML attribute");
        }
        found = attribute;
    }
    if (required && found.impl == NULL) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_node_location(node), "required CCXML attribute is missing");
    }
    if (out_attribute != NULL) *out_attribute = found;
    return CCXML_OK;
}

static ccxml_status validate_empty_action(
    salts_xml_node action, ccxml_measurement *measurement,
    const ccxml_limits *limits, ccxml_diagnostic *diagnostic) {
    size_t index;
    if (salts_xml_node_attribute_count(action) != 0u) {
        return fail(
            diagnostic, CCXML_UNSUPPORTED_FEATURE,
            salts_xml_node_location(action),
            "CCXML MVP actions do not accept attributes");
    }
    for (index = 0u; index < salts_xml_node_child_count(action); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(action, index);
        if (!node_is_ignorable(child)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child),
                "CCXML MVP actions must be empty");
        }
    }
    if (measurement->action_count >= limits->max_actions ||
        !checked_add(measurement->action_count, 1u,
                     &measurement->action_count)) {
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            salts_xml_node_location(action), "CCXML action limit exceeded");
    }
    return CCXML_OK;
}

static ccxml_status validate_string_literal(
    salts_xml_attribute attribute, salts_xml_string_view *out_expression,
    ccxml_diagnostic *diagnostic) {
    const salts_xml_string_view expression =
        salts_xml_attribute_value(attribute);
    char quote;
    size_t index;
    if (expression.data == NULL || expression.size < 2u ||
        (expression.data[0] != '\'' && expression.data[0] != '"') ||
        expression.data[expression.size - 1u] != expression.data[0]) {
        return fail(
            diagnostic, CCXML_UNSUPPORTED_FEATURE,
            salts_xml_attribute_location(attribute),
            "CCXML value must be a quoted string literal");
    }
    quote = expression.data[0];
    if (expression.size == 2u) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(attribute),
            "CCXML string literal must be nonempty");
    }
    for (index = 1u; index + 1u < expression.size; ++index) {
        if (expression.data[index] == '\\' || expression.data[index] == quote) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "CCXML string literal escapes are not supported");
        }
    }
    if (out_expression != NULL) *out_expression = expression;
    return CCXML_OK;
}

static ccxml_status decode_destination_value(
    salts_xml_attribute attribute, bool allow_dynamic,
    char **out_value, size_t *out_size, bool *out_dynamic,
    ccxml_diagnostic *diagnostic);

static ccxml_status validate_destination_action(
    salts_xml_node action, ccxml_measurement *measurement,
    const ccxml_limits *limits, bool allow_dynamic,
    ccxml_diagnostic *diagnostic) {
    salts_xml_attribute destination_attribute = {0};
    char *value = NULL;
    size_t value_size = 0u;
    bool dynamic = false;
    size_t index;
    size_t retained_size;
    ccxml_status status = validate_attributes(
        action, "dest", true, &destination_attribute, diagnostic);
    if (status != CCXML_OK) return status;
    status = decode_destination_value(
        destination_attribute, allow_dynamic, &value, &value_size,
        &dynamic, diagnostic);
    (void)dynamic;
    if (status != CCXML_OK) return status;
    if (!checked_add(value_size, 1u, &retained_size)) {
        free(value);
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            salts_xml_attribute_location(destination_attribute),
            "CCXML destination expression storage overflow");
    }
    free(value);
    for (index = 0u; index < salts_xml_node_child_count(action); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(action, index);
        if (!node_is_ignorable(child)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child),
                "CCXML destination action must be empty");
        }
    }
    if (measurement->action_count >= limits->max_actions ||
        !checked_add(measurement->action_count, 1u,
                     &measurement->action_count) ||
        !checked_add(measurement->name_bytes, retained_size,
                     &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes) {
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            salts_xml_node_location(action),
            "CCXML action or retained-string limit exceeded");
    }
    return CCXML_OK;
}

static ccxml_status decode_attribute_value(
    salts_xml_attribute attribute, char **out_value, size_t *out_size,
    ccxml_diagnostic *diagnostic) {
    const salts_xml_string_view expression =
        salts_xml_attribute_value(attribute);
    scxml_xml_decode_status decode_status;
    char *value;
    size_t decoded_size = 0u;
    if (out_value == NULL || out_size == NULL) return CCXML_INVALID_ARGUMENT;
    *out_value = NULL;
    *out_size = 0u;
    decode_status = scxml_xml_decode_attribute_entities(
        expression.data, expression.size, NULL, 0u, &decoded_size);
    if (decode_status != SCXML_XML_DECODE_OK) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(attribute),
            decode_status == SCXML_XML_DECODE_INVALID_CHARACTER_REFERENCE
                ? "CCXML expression has an invalid XML character reference"
                : "CCXML expression has an unsupported XML entity reference");
    }
    value = (char *)malloc(decoded_size != 0u ? decoded_size : 1u);
    if (value == NULL) {
        return fail(
            diagnostic, CCXML_ALLOCATION_FAILED,
            salts_xml_attribute_location(attribute),
            "CCXML expression decoding allocation failed");
    }
    decode_status = scxml_xml_decode_attribute_entities(
        expression.data, expression.size,
        value, decoded_size, &decoded_size);
    if (decode_status != SCXML_XML_DECODE_OK) {
        free(value);
        return fail(
            diagnostic, CCXML_INVALID_CONTRACT,
            salts_xml_attribute_location(attribute),
            "CCXML expression decoding changed between passes");
    }
    *out_value = value;
    *out_size = decoded_size;
    return CCXML_OK;
}

static ccxml_status decode_destination_value(
    salts_xml_attribute attribute, bool allow_dynamic,
    char **out_value, size_t *out_size, bool *out_dynamic,
    ccxml_diagnostic *diagnostic) {
    char *value = NULL;
    size_t value_size = 0u;
    size_t index;
    char quote;
    ccxml_status status;
    if (out_value == NULL || out_size == NULL || out_dynamic == NULL)
        return CCXML_INVALID_ARGUMENT;
    *out_value = NULL;
    *out_size = 0u;
    *out_dynamic = false;
    status = decode_attribute_value(
        attribute, &value, &value_size, diagnostic);
    if (status != CCXML_OK) return status;
    if (value_size >= 2u && (value[0] == '\'' || value[0] == '"') &&
        value[value_size - 1u] == value[0]) {
        if (value_size == 2u) {
            free(value);
            return fail(
                diagnostic, CCXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(attribute),
                "CCXML string literal must be nonempty");
        }
        quote = value[0];
        for (index = 1u; index + 1u < value_size; ++index) {
            if (value[index] == '\\' || value[index] == quote) {
                free(value);
                return fail(
                    diagnostic, CCXML_UNSUPPORTED_FEATURE,
                    salts_xml_attribute_location(attribute),
                    "CCXML string literal escapes are not supported");
            }
        }
        value_size -= 2u;
        memmove(value, value + 1u, value_size);
    } else {
        if (!allow_dynamic) {
            free(value);
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "CCXML value must be a quoted string literal");
        }
        if (value_size == 0u) {
            free(value);
            return fail(
                diagnostic, CCXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(attribute),
                "CCXML destination expression must be nonempty");
        }
        *out_dynamic = true;
    }
    *out_value = value;
    *out_size = value_size;
    return CCXML_OK;
}

static ccxml_status decode_send_literal(
    salts_xml_attribute attribute, char **out_value, size_t *out_size,
    ccxml_diagnostic *diagnostic) {
    char *value = NULL;
    char quote;
    size_t decoded_size = 0u;
    size_t value_size;
    size_t index;
    ccxml_status status = decode_attribute_value(
        attribute, &value, &decoded_size, diagnostic);
    if (status != CCXML_OK) return status;
    if (decoded_size < 2u) {
        free(value);
        return fail(
            diagnostic, CCXML_UNSUPPORTED_FEATURE,
            salts_xml_attribute_location(attribute),
            "CCXML send value must be a quoted string literal");
    }
    if ((value[0] != '\'' && value[0] != '"') ||
        value[decoded_size - 1u] != value[0]) {
        free(value);
        return fail(
            diagnostic, CCXML_UNSUPPORTED_FEATURE,
            salts_xml_attribute_location(attribute),
            "CCXML send value must be a quoted string literal");
    }
    if (decoded_size == 2u) {
        free(value);
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(attribute),
            "CCXML send string literal must be nonempty");
    }
    quote = value[0];
    for (index = 1u; index + 1u < decoded_size; ++index) {
        if (value[index] == '\\' || value[index] == quote) {
            free(value);
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "CCXML send literal decodes to an unsupported escape or quote");
        }
    }
    value_size = decoded_size - 2u;
    memmove(value, value + 1u, value_size);
    *out_value = value;
    *out_size = value_size;
    return CCXML_OK;
}

static size_t copy_send_literal(
    salts_xml_attribute attribute, char *output) {
    const salts_xml_string_view expression =
        salts_xml_attribute_value(attribute);
    size_t decoded_size = 0u;
    if (scxml_xml_decode_attribute_entities(
            expression.data, expression.size,
            NULL, 0u, &decoded_size) != SCXML_XML_DECODE_OK ||
        scxml_xml_decode_attribute_entities(
            expression.data, expression.size,
            output, decoded_size, &decoded_size) != SCXML_XML_DECODE_OK) {
        return 0u;
    }
    memmove(output, output + 1u, decoded_size - 2u);
    output[decoded_size - 2u] = '\0';
    return decoded_size - 2u;
}

static bool send_delay_location_has_dot(salts_xml_string_view location) {
    return location.data != NULL && location.size > 0u &&
           memchr(location.data, '.', location.size) != NULL;
}

static ccxml_status decode_send_delay(
    salts_xml_attribute attribute, char **out_value, size_t *out_size,
    bool *out_is_literal, ccxml_diagnostic *diagnostic) {
    ccxml_status status;
    bool is_literal = false;
    char *value = NULL;
    size_t size;
    size_t decoded_size = 0u;
    char quote;
    size_t index;
    if (out_value == NULL || out_size == NULL || out_is_literal == NULL) {
        return CCXML_INVALID_ARGUMENT;
    }
    *out_value = NULL;
    *out_size = 0u;
    *out_is_literal = false;
    status = decode_attribute_value(
        attribute, &value, &decoded_size, diagnostic);
    if (status != CCXML_OK) return status;
    if (decoded_size == 0u) {
        free(value);
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(attribute),
            "CCXML send delay is missing");
    }
    if (value[0] == '\'' || value[0] == '"') {
        if (decoded_size < 2u) {
            free(value);
            return fail(
                diagnostic, CCXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(attribute),
                "CCXML send delay must be a quoted string literal");
        }
        if (value[decoded_size - 1u] != value[0]) {
            free(value);
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "CCXML send delay must be a quoted string literal");
        }
        quote = value[0];
        for (index = 1u; index + 1u < decoded_size; ++index) {
            if (value[index] == '\\' || value[index] == quote) {
                free(value);
                return fail(
                    diagnostic, CCXML_UNSUPPORTED_FEATURE,
                    salts_xml_attribute_location(attribute),
                    "CCXML send literal decodes to an "
                    "unsupported escape or quote");
            }
        }
        size = decoded_size - 2u;
        memmove(value, value + 1u, size);
        value[size] = '\0';
        is_literal = true;
    } else {
        const salts_xml_string_view location = {value, decoded_size};
        if (!send_delay_location_has_dot(location) ||
            !dotted_location_valid(location)) {
            free(value);
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "CCXML send delay must be a quoted literal or dotted "
                "NCName location");
        }
        size = decoded_size;
    }
    *out_value = value;
    *out_size = size;
    *out_is_literal = is_literal;
    return CCXML_OK;
}

static bool send_event_name_valid(const char *name, size_t name_size) {
    size_t index;
    if (name == NULL || name_size == 0u) return false;
    if (!((name[0] >= 'A' && name[0] <= 'Z') ||
          (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_'))
        return false;
    for (index = 1u; index < name_size; ++index) {
        const char value = name[index];
        if (!((value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') ||
              value == '_' || value == '.'))
            return false;
    }
    return true;
}

static ccxml_status validate_send_action(
    salts_xml_node action, ccxml_measurement *measurement,
    const ccxml_limits *limits, ccxml_diagnostic *diagnostic) {
    salts_xml_attribute target_attribute = {0};
    salts_xml_attribute name_attribute = {0};
    salts_xml_attribute type_attribute = {0};
    salts_xml_attribute delay_attribute = {0};
    salts_xml_attribute send_id_attribute = {0};
    salts_xml_attribute namelist_attribute = {0};
    char *target_value = NULL;
    char *name_value = NULL;
    char *type_value = NULL;
    char *delay_value = NULL;
    char *send_id_location = NULL;
    char *namelist_value = NULL;
    bool delay_is_literal = false;
    size_t target_size = 0u;
    size_t name_size = 0u;
    size_t type_size = 0u;
    size_t delay_size = 0u;
    size_t send_id_location_size = 0u;
    size_t namelist_size = 0u;
    size_t payload_count = 0u;
    uint64_t delay_ms = 0u;
    size_t retained_size = 0u;
    size_t part_size;
    size_t index;
    ccxml_status status;
    for (index = 0u; index < salts_xml_node_attribute_count(action); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(action, index);
        const salts_xml_string_view namespace_uri =
            salts_xml_attribute_namespace_uri(attribute);
        const salts_xml_string_view local_name =
            salts_xml_attribute_local_name(attribute);
        salts_xml_attribute *slot = NULL;
        if (namespace_uri.size == 0u && view_equal(local_name, "target"))
            slot = &target_attribute;
        else if (namespace_uri.size == 0u && view_equal(local_name, "name"))
            slot = &name_attribute;
        else if (namespace_uri.size == 0u &&
                 view_equal(local_name, "targettype"))
            slot = &type_attribute;
        else if (namespace_uri.size == 0u && view_equal(local_name, "delay"))
            slot = &delay_attribute;
        else if (namespace_uri.size == 0u && view_equal(local_name, "sendid"))
            slot = &send_id_attribute;
        else if (namespace_uri.size == 0u && view_equal(local_name, "namelist"))
            slot = &namelist_attribute;
        if (slot == NULL || slot->impl != NULL) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported or duplicate CCXML send attribute");
        }
        *slot = attribute;
    }
    if (target_attribute.impl == NULL || name_attribute.impl == NULL) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_node_location(action),
            "CCXML send requires target and name");
    }
    status = decode_send_literal(
        target_attribute, &target_value, &target_size, diagnostic);
    if (status != CCXML_OK) goto cleanup;
    status = decode_send_literal(
        name_attribute, &name_value, &name_size, diagnostic);
    if (status != CCXML_OK) goto cleanup;
    if (!send_event_name_valid(name_value, name_size)) {
        status = fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(name_attribute),
            "CCXML send name has invalid event-name syntax");
        goto cleanup;
    }
    if (type_attribute.impl != NULL) {
        status = decode_send_literal(
            type_attribute, &type_value, &type_size, diagnostic);
        if (status != CCXML_OK) goto cleanup;
    }
    if (delay_attribute.impl != NULL) {
        status = decode_send_delay(
            delay_attribute, &delay_value, &delay_size, &delay_is_literal,
            diagnostic);
        if (status != CCXML_OK) goto cleanup;
        if (delay_is_literal &&
            !scxml_time_parse_ms(
                (salts_xml_string_view){delay_value, delay_size},
                &delay_ms)) {
            status = fail(
                diagnostic, CCXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(delay_attribute),
                "CCXML send delay must be a non-negative ms or s literal "
                "with millisecond precision");
            goto cleanup;
        }
    }
    if (send_id_attribute.impl != NULL) {
        status = decode_attribute_value(
            send_id_attribute, &send_id_location,
            &send_id_location_size, diagnostic);
        if (status != CCXML_OK) goto cleanup;
        if (send_id_location_size == 0u) {
            status = fail(
                diagnostic, CCXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(send_id_attribute),
                "CCXML send sendid must be nonempty");
            goto cleanup;
        }
        if (!dotted_location_valid((salts_xml_string_view){
                send_id_location, send_id_location_size})) {
            status = fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(send_id_attribute),
                "CCXML send sendid must be a dotted NCName location");
            goto cleanup;
        }
    }
    if (namelist_attribute.impl != NULL) {
        size_t cursor = 0u;
        salts_xml_string_view token;
        status = decode_attribute_value(
            namelist_attribute, &namelist_value, &namelist_size, diagnostic);
        if (status != CCXML_OK) goto cleanup;
        while (namelist_token_next(
                   namelist_value, namelist_size, &cursor, &token)) {
            if (!dotted_location_valid(token)) {
                status = fail(
                    diagnostic, CCXML_UNSUPPORTED_FEATURE,
                    salts_xml_attribute_location(namelist_attribute),
                    "CCXML send namelist entries must be dotted NCName locations");
                goto cleanup;
            }
            if (payload_count >= SCXML_PAYLOAD_MAX_ENTRIES ||
                !checked_add(payload_count, 1u, &payload_count) ||
                !checked_add(token.size, 1u, &part_size) ||
                !checked_add(retained_size, part_size, &retained_size)) {
                status = fail(
                    diagnostic, CCXML_LIMIT_EXCEEDED,
                    salts_xml_attribute_location(namelist_attribute),
                    "CCXML send namelist entry limit exceeded");
                goto cleanup;
            }
        }
    }
    for (index = 0u; index < salts_xml_node_child_count(action); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(action, index);
        if (!node_is_ignorable(child)) {
            status = fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child),
                "CCXML send inline content is unsupported");
            goto cleanup;
        }
    }
    if (!checked_add(target_size, 1u, &part_size) ||
        !checked_add(retained_size, part_size, &retained_size) ||
        !checked_add(name_size, 1u, &part_size) ||
        !checked_add(retained_size, part_size, &retained_size) ||
        (type_attribute.impl != NULL &&
         (!checked_add(type_size, 1u, &part_size) ||
          !checked_add(retained_size, part_size, &retained_size))) ||
        (delay_attribute.impl != NULL &&
         (!checked_add(delay_size, 1u, &part_size) ||
          !checked_add(retained_size, part_size, &retained_size))) ||
        (send_id_attribute.impl != NULL &&
         (!checked_add(
              send_id_location_size, 1u, &part_size) ||
          !checked_add(retained_size, part_size, &retained_size))) ||
        !checked_add(measurement->payload_count, payload_count,
                     &measurement->payload_count) ||
        measurement->action_count >= limits->max_actions ||
        !checked_add(measurement->action_count, 1u,
                     &measurement->action_count) ||
        !checked_add(measurement->name_bytes, retained_size,
                     &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes) {
        status = fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            salts_xml_node_location(action),
            "CCXML send action or retained-string limit exceeded");
        goto cleanup;
    }
    if (payload_count > measurement->max_send_payload_entries)
        measurement->max_send_payload_entries = payload_count;
    status = CCXML_OK;

cleanup:
    free(namelist_value);
    free(send_id_location);
    free(delay_value);
    free(type_value);
    free(name_value);
    free(target_value);
    return status;
}

static ccxml_status validate_cancel_action(
    salts_xml_node action, ccxml_measurement *measurement,
    const ccxml_limits *limits, ccxml_diagnostic *diagnostic) {
    salts_xml_attribute send_id_attribute = {0};
    salts_xml_string_view expression;
    char *decoded_expression = NULL;
    size_t decoded_expression_size = 0u;
    char *literal = NULL;
    size_t retained_size = 0u;
    size_t literal_size = 0u;
    size_t index;
    ccxml_status status = validate_attributes(
        action, "sendid", true, &send_id_attribute, diagnostic);
    if (status != CCXML_OK) return status;
    expression = salts_xml_attribute_value(send_id_attribute);
    if (expression.data == NULL || expression.size == 0u) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(send_id_attribute),
            "CCXML cancel sendid must be nonempty");
    }
    status = decode_attribute_value(
        send_id_attribute, &decoded_expression,
        &decoded_expression_size, diagnostic);
    if (status != CCXML_OK) goto cleanup;
    if (decoded_expression_size == 0u) {
        status = fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(send_id_attribute),
            "CCXML cancel sendid must be nonempty");
        goto cleanup;
    }
    if (decoded_expression[0] == '\'' || decoded_expression[0] == '"') {
        status = decode_send_literal(
            send_id_attribute, &literal, &literal_size, diagnostic);
        if (status != CCXML_OK) goto cleanup;
        if (literal_size > SCXML_EVENT_METADATA_CAPACITY) {
            status = fail(
                diagnostic, CCXML_LIMIT_EXCEEDED,
                salts_xml_attribute_location(send_id_attribute),
                "CCXML cancel sendid exceeds Event I/O metadata capacity");
            goto cleanup;
        }
        retained_size = literal_size + 1u;
    } else {
        if (!dotted_location_valid((salts_xml_string_view){
                decoded_expression, decoded_expression_size})) {
            status = fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(send_id_attribute),
                "CCXML cancel sendid must be a quoted string or dotted "
                "NCName location");
            goto cleanup;
        }
        if (!checked_add(
                decoded_expression_size, 1u, &retained_size)) {
            status = fail(
                diagnostic, CCXML_LIMIT_EXCEEDED,
                salts_xml_attribute_location(send_id_attribute),
                "CCXML cancel sendid limit exceeded");
            goto cleanup;
        }
    }
    for (index = 0u; index < salts_xml_node_child_count(action); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(action, index);
        if (!node_is_ignorable(child)) {
            status = fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child),
                "CCXML cancel must be empty");
            goto cleanup;
        }
    }
    if (measurement->action_count >= limits->max_actions ||
        !checked_add(measurement->action_count, 1u,
                     &measurement->action_count) ||
        !checked_add(measurement->name_bytes, retained_size,
                     &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes) {
        status = fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            salts_xml_node_location(action),
            "CCXML cancel action or retained-string limit exceeded");
        goto cleanup;
    }
    status = CCXML_OK;

cleanup:
    free(decoded_expression);
    free(literal);
    return status;
}

static ccxml_status validate_two_identifier_action(
    salts_xml_node action, const char *id1_name, const char *id2_name,
    ccxml_measurement *measurement,
    const ccxml_limits *limits, ccxml_diagnostic *diagnostic) {
    salts_xml_attribute id1_attribute = {0};
    salts_xml_attribute id2_attribute = {0};
    salts_xml_string_view id1_expression;
    salts_xml_string_view id2_expression;
    size_t index;
    size_t id1_retained_size;
    size_t id2_retained_size;
    size_t retained_size;
    ccxml_status status;
    for (index = 0u; index < salts_xml_node_attribute_count(action); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(action, index);
        const salts_xml_string_view namespace_uri =
            salts_xml_attribute_namespace_uri(attribute);
        const salts_xml_string_view local_name =
            salts_xml_attribute_local_name(attribute);
        salts_xml_attribute *slot = NULL;
        if (namespace_uri.size == 0u && view_equal(local_name, id1_name)) {
            slot = &id1_attribute;
        } else if (namespace_uri.size == 0u &&
                   view_equal(local_name, id2_name)) {
            slot = &id2_attribute;
        }
        if (slot == NULL || slot->impl != NULL) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported or duplicate CCXML two-ID action attribute");
        }
        *slot = attribute;
    }
    if (id1_attribute.impl == NULL || id2_attribute.impl == NULL) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_node_location(action),
            "CCXML two-ID action requires both identifiers");
    }
    status = validate_string_literal(
        id1_attribute, &id1_expression, diagnostic);
    if (status != CCXML_OK) return status;
    status = validate_string_literal(
        id2_attribute, &id2_expression, diagnostic);
    if (status != CCXML_OK) return status;
    for (index = 0u; index < salts_xml_node_child_count(action); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(action, index);
        if (!node_is_ignorable(child)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child),
                "CCXML two-ID action must be empty");
        }
    }
    if (!checked_add(id1_expression.size - 2u, 1u, &id1_retained_size) ||
        !checked_add(id2_expression.size - 2u, 1u, &id2_retained_size) ||
        !checked_add(id1_retained_size, id2_retained_size, &retained_size) ||
        measurement->action_count >= limits->max_actions ||
        !checked_add(measurement->action_count, 1u,
                     &measurement->action_count) ||
        !checked_add(measurement->name_bytes, retained_size,
                     &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes) {
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            salts_xml_node_location(action),
            "CCXML two-ID action or retained-string limit exceeded");
    }
    return CCXML_OK;
}

static ccxml_status validate_create_conference_action(
    salts_xml_node action, ccxml_measurement *measurement,
    const ccxml_limits *limits, ccxml_diagnostic *diagnostic) {
    salts_xml_attribute id_attribute = {0};
    salts_xml_attribute name_attribute = {0};
    salts_xml_string_view location;
    salts_xml_string_view name_expression = {0};
    size_t index;
    size_t retained_size;
    ccxml_status status;
    for (index = 0u; index < salts_xml_node_attribute_count(action); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(action, index);
        const salts_xml_string_view namespace_uri =
            salts_xml_attribute_namespace_uri(attribute);
        const salts_xml_string_view local_name =
            salts_xml_attribute_local_name(attribute);
        salts_xml_attribute *slot = NULL;
        if (namespace_uri.size == 0u && view_equal(local_name, "conferenceid"))
            slot = &id_attribute;
        else if (namespace_uri.size == 0u && view_equal(local_name, "confname"))
            slot = &name_attribute;
        if (slot == NULL || slot->impl != NULL) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported or duplicate createconference attribute");
        }
        *slot = attribute;
    }
    if (id_attribute.impl == NULL) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_node_location(action),
            "createconference requires conferenceid");
    }
    location = salts_xml_attribute_value(id_attribute);
    if (location.data == NULL || location.size == 0u) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(id_attribute),
            "createconference conferenceid must be nonempty");
    }
    if (!dotted_location_valid(location)) {
        return fail(
            diagnostic, CCXML_UNSUPPORTED_FEATURE,
            salts_xml_attribute_location(id_attribute),
            "createconference conferenceid must be a dotted NCName location");
    }
    if (name_attribute.impl != NULL) {
        status = validate_string_literal(
            name_attribute, &name_expression, diagnostic);
        if (status != CCXML_OK) return status;
    }
    for (index = 0u; index < salts_xml_node_child_count(action); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(action, index);
        if (!node_is_ignorable(child)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child),
                "createconference must be empty");
        }
    }
    if (!checked_add(location.size, 1u, &retained_size) ||
        (name_attribute.impl != NULL &&
         (!checked_add(retained_size, name_expression.size - 1u,
                       &retained_size))) ||
        measurement->action_count >= limits->max_actions ||
        !checked_add(measurement->action_count, 1u,
                     &measurement->action_count) ||
        !checked_add(measurement->name_bytes, retained_size,
                     &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes) {
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            salts_xml_node_location(action),
            "createconference action or retained-string limit exceeded");
    }
    return CCXML_OK;
}

static ccxml_status validate_destroy_conference_action(
    salts_xml_node action, ccxml_measurement *measurement,
    const ccxml_limits *limits, ccxml_diagnostic *diagnostic) {
    salts_xml_attribute id_attribute = {0};
    salts_xml_string_view expression;
    size_t index;
    size_t retained_size;
    ccxml_status status = validate_attributes(
        action, "conferenceid", true, &id_attribute, diagnostic);
    if (status != CCXML_OK) return status;
    expression = salts_xml_attribute_value(id_attribute);
    if (expression.data == NULL || expression.size == 0u) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(id_attribute),
            "destroyconference conferenceid must be nonempty");
    }
    if (expression.data[0] == '\'' || expression.data[0] == '"') {
        status = validate_string_literal(
            id_attribute, &expression, diagnostic);
        if (status != CCXML_OK) return status;
        retained_size = expression.size - 1u;
    } else {
        if (!dotted_location_valid(expression)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(id_attribute),
                "destroyconference conferenceid must be a quoted string "
                "or dotted NCName location");
        }
        if (!checked_add(expression.size, 1u, &retained_size)) {
            return fail(
                diagnostic, CCXML_LIMIT_EXCEEDED,
                salts_xml_attribute_location(id_attribute),
                "destroyconference identifier limit exceeded");
        }
    }
    for (index = 0u; index < salts_xml_node_child_count(action); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(action, index);
        if (!node_is_ignorable(child)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child),
                "destroyconference must be empty");
        }
    }
    if (measurement->action_count >= limits->max_actions ||
        !checked_add(measurement->action_count, 1u,
                     &measurement->action_count) ||
        !checked_add(measurement->name_bytes, retained_size,
                     &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes) {
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            salts_xml_node_location(action),
            "destroyconference action or retained-string limit exceeded");
    }
    return CCXML_OK;
}

static ccxml_status validate_dialog_start_action(
    salts_xml_node action, ccxml_measurement *measurement,
    const ccxml_limits *limits, ccxml_diagnostic *diagnostic) {
    salts_xml_attribute dialog_id_attribute = {0};
    salts_xml_attribute source_attribute = {0};
    salts_xml_attribute connection_id_attribute = {0};
    salts_xml_string_view location;
    salts_xml_string_view source_expression;
    salts_xml_string_view connection_expression;
    size_t index;
    size_t retained_size;
    ccxml_status status;
    for (index = 0u; index < salts_xml_node_attribute_count(action); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(action, index);
        const salts_xml_string_view namespace_uri =
            salts_xml_attribute_namespace_uri(attribute);
        const salts_xml_string_view local_name =
            salts_xml_attribute_local_name(attribute);
        salts_xml_attribute *slot = NULL;
        if (namespace_uri.size == 0u && view_equal(local_name, "dialogid"))
            slot = &dialog_id_attribute;
        else if (namespace_uri.size == 0u && view_equal(local_name, "src"))
            slot = &source_attribute;
        else if (namespace_uri.size == 0u &&
                 view_equal(local_name, "connectionid"))
            slot = &connection_id_attribute;
        if (slot == NULL || slot->impl != NULL) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported or duplicate dialogstart attribute");
        }
        *slot = attribute;
    }
    if (dialog_id_attribute.impl == NULL || source_attribute.impl == NULL ||
        connection_id_attribute.impl == NULL) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_node_location(action),
            "dialogstart requires dialogid, src, and connectionid");
    }
    location = salts_xml_attribute_value(dialog_id_attribute);
    if (location.data == NULL || location.size == 0u) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(dialog_id_attribute),
            "dialogstart dialogid must be nonempty");
    }
    if (!dotted_location_valid(location)) {
        return fail(
            diagnostic, CCXML_UNSUPPORTED_FEATURE,
            salts_xml_attribute_location(dialog_id_attribute),
            "dialogstart dialogid must be a dotted NCName location");
    }
    status = validate_string_literal(
        source_attribute, &source_expression, diagnostic);
    if (status != CCXML_OK) return status;
    connection_expression =
        salts_xml_attribute_value(connection_id_attribute);
    if (!view_equal(connection_expression, "event$.connectionid")) {
        return fail(
            diagnostic, CCXML_UNSUPPORTED_FEATURE,
            salts_xml_attribute_location(connection_id_attribute),
            "dialogstart connectionid must be event$.connectionid");
    }
    for (index = 0u; index < salts_xml_node_child_count(action); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(action, index);
        if (!node_is_ignorable(child)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child),
                "dialogstart must be empty");
        }
    }
    if (!checked_add(location.size, 1u, &retained_size) ||
        !checked_add(
            retained_size, source_expression.size - 1u, &retained_size) ||
        measurement->action_count >= limits->max_actions ||
        !checked_add(measurement->action_count, 1u,
                     &measurement->action_count) ||
        !checked_add(measurement->name_bytes, retained_size,
                     &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes) {
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            salts_xml_node_location(action),
            "dialogstart action or retained-string limit exceeded");
    }
    return CCXML_OK;
}

static ccxml_status validate_prepared_dialog_start_action(
    salts_xml_node action, ccxml_measurement *measurement,
    const ccxml_limits *limits, ccxml_diagnostic *diagnostic) {
    salts_xml_attribute prepared_id_attribute = {0};
    salts_xml_attribute connection_id_attribute = {0};
    salts_xml_string_view location;
    salts_xml_string_view connection_expression;
    size_t index;
    size_t retained_size;
    for (index = 0u; index < salts_xml_node_attribute_count(action); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(action, index);
        const salts_xml_string_view namespace_uri =
            salts_xml_attribute_namespace_uri(attribute);
        const salts_xml_string_view local_name =
            salts_xml_attribute_local_name(attribute);
        salts_xml_attribute *slot = NULL;
        if (namespace_uri.size == 0u &&
            view_equal(local_name, "prepareddialogid"))
            slot = &prepared_id_attribute;
        else if (namespace_uri.size == 0u &&
                 view_equal(local_name, "connectionid"))
            slot = &connection_id_attribute;
        if (slot == NULL || slot->impl != NULL) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported or duplicate prepared dialogstart attribute");
        }
        *slot = attribute;
    }
    if (prepared_id_attribute.impl == NULL ||
        connection_id_attribute.impl == NULL) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_node_location(action),
            "prepared dialogstart requires prepareddialogid and connectionid");
    }
    location = salts_xml_attribute_value(prepared_id_attribute);
    if (location.data == NULL || location.size == 0u) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(prepared_id_attribute),
            "prepared dialogstart prepareddialogid must be nonempty");
    }
    if (!dotted_location_valid(location)) {
        return fail(
            diagnostic, CCXML_UNSUPPORTED_FEATURE,
            salts_xml_attribute_location(prepared_id_attribute),
            "prepared dialogstart prepareddialogid must be a dotted NCName "
            "location");
    }
    connection_expression =
        salts_xml_attribute_value(connection_id_attribute);
    if (!view_equal(connection_expression, "event$.connectionid")) {
        return fail(
            diagnostic, CCXML_UNSUPPORTED_FEATURE,
            salts_xml_attribute_location(connection_id_attribute),
            "prepared dialogstart connectionid must be event$.connectionid");
    }
    for (index = 0u; index < salts_xml_node_child_count(action); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(action, index);
        if (!node_is_ignorable(child)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child),
                "prepared dialogstart must be empty");
        }
    }
    if (!checked_add(location.size, 1u, &retained_size) ||
        measurement->action_count >= limits->max_actions ||
        !checked_add(measurement->action_count, 1u,
                     &measurement->action_count) ||
        !checked_add(measurement->name_bytes, retained_size,
                     &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes) {
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            salts_xml_node_location(action),
            "prepared dialogstart action or retained-string limit exceeded");
    }
    return CCXML_OK;
}

static ccxml_status validate_dialog_prepare_action(
    salts_xml_node action, ccxml_measurement *measurement,
    const ccxml_limits *limits, ccxml_diagnostic *diagnostic) {
    salts_xml_attribute dialog_id_attribute = {0};
    salts_xml_attribute source_attribute = {0};
    salts_xml_string_view location;
    salts_xml_string_view source_expression;
    size_t index;
    size_t retained_size;
    ccxml_status status;
    for (index = 0u; index < salts_xml_node_attribute_count(action); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(action, index);
        const salts_xml_string_view namespace_uri =
            salts_xml_attribute_namespace_uri(attribute);
        const salts_xml_string_view local_name =
            salts_xml_attribute_local_name(attribute);
        salts_xml_attribute *slot = NULL;
        if (namespace_uri.size == 0u && view_equal(local_name, "dialogid"))
            slot = &dialog_id_attribute;
        else if (namespace_uri.size == 0u && view_equal(local_name, "src"))
            slot = &source_attribute;
        if (slot == NULL || slot->impl != NULL) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported or duplicate dialogprepare attribute");
        }
        *slot = attribute;
    }
    if (dialog_id_attribute.impl == NULL || source_attribute.impl == NULL) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_node_location(action),
            "dialogprepare requires dialogid and src");
    }
    location = salts_xml_attribute_value(dialog_id_attribute);
    if (location.data == NULL || location.size == 0u) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(dialog_id_attribute),
            "dialogprepare dialogid must be nonempty");
    }
    if (!dotted_location_valid(location)) {
        return fail(
            diagnostic, CCXML_UNSUPPORTED_FEATURE,
            salts_xml_attribute_location(dialog_id_attribute),
            "dialogprepare dialogid must be a dotted NCName location");
    }
    status = validate_string_literal(
        source_attribute, &source_expression, diagnostic);
    if (status != CCXML_OK) return status;
    for (index = 0u; index < salts_xml_node_child_count(action); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(action, index);
        if (!node_is_ignorable(child)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child),
                "dialogprepare must be empty");
        }
    }
    if (!checked_add(location.size, 1u, &retained_size) ||
        !checked_add(
            retained_size, source_expression.size - 1u, &retained_size) ||
        measurement->action_count >= limits->max_actions ||
        !checked_add(measurement->action_count, 1u,
                     &measurement->action_count) ||
        !checked_add(measurement->name_bytes, retained_size,
                     &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes) {
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            salts_xml_node_location(action),
            "dialogprepare action or retained-string limit exceeded");
    }
    return CCXML_OK;
}

static ccxml_status validate_dialog_terminate_action(
    salts_xml_node action, ccxml_measurement *measurement,
    const ccxml_limits *limits, ccxml_diagnostic *diagnostic) {
    salts_xml_attribute id_attribute = {0};
    salts_xml_string_view expression;
    size_t index;
    size_t retained_size;
    ccxml_status status = validate_attributes(
        action, "dialogid", true, &id_attribute, diagnostic);
    if (status != CCXML_OK) return status;
    expression = salts_xml_attribute_value(id_attribute);
    if (expression.data == NULL || expression.size == 0u) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(id_attribute),
            "dialogterminate dialogid must be nonempty");
    }
    if (expression.data[0] == '\'' || expression.data[0] == '"') {
        status = validate_string_literal(
            id_attribute, &expression, diagnostic);
        if (status != CCXML_OK) return status;
        retained_size = expression.size - 1u;
    } else {
        if (!dotted_location_valid(expression)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(id_attribute),
                "dialogterminate dialogid must be a quoted string "
                "or dotted NCName location");
        }
        if (!checked_add(expression.size, 1u, &retained_size)) {
            return fail(
                diagnostic, CCXML_LIMIT_EXCEEDED,
                salts_xml_attribute_location(id_attribute),
                "dialogterminate identifier limit exceeded");
        }
    }
    for (index = 0u; index < salts_xml_node_child_count(action); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(action, index);
        if (!node_is_ignorable(child)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child),
                "dialogterminate must be empty");
        }
    }
    if (measurement->action_count >= limits->max_actions ||
        !checked_add(measurement->action_count, 1u,
                     &measurement->action_count) ||
        !checked_add(measurement->name_bytes, retained_size,
                     &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes) {
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            salts_xml_node_location(action),
            "dialogterminate action or retained-string limit exceeded");
    }
    return CCXML_OK;
}

static ccxml_status validate_string_binding(
    salts_xml_node node, bool counts_as_action,
    ccxml_measurement *measurement, const ccxml_limits *limits,
    ccxml_diagnostic *diagnostic, salts_xml_string_view *out_name) {
    salts_xml_attribute name_attribute = {0};
    salts_xml_attribute expression_attribute = {0};
    salts_xml_string_view name;
    salts_xml_string_view expression;
    size_t retained_size;
    size_t index;
    ccxml_status status;
    for (index = 0u; index < salts_xml_node_attribute_count(node); ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(node, index);
        const salts_xml_string_view local_name =
            salts_xml_attribute_local_name(attribute);
        if (salts_xml_attribute_namespace_uri(attribute).size != 0u) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "namespaced CCXML variable attributes are unsupported");
        }
        if (view_equal(local_name, "name") && name_attribute.impl == NULL)
            name_attribute = attribute;
        else if (view_equal(local_name, "expr") &&
                 expression_attribute.impl == NULL)
            expression_attribute = attribute;
        else
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported or duplicate CCXML variable attribute");
    }
    if (name_attribute.impl == NULL || expression_attribute.impl == NULL) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_node_location(node),
            "CCXML string variable requires name and expr");
    }
    name = salts_xml_attribute_value(name_attribute);
    if (!dotted_location_valid(name)) {
        return fail(
            diagnostic, CCXML_UNSUPPORTED_FEATURE,
            salts_xml_attribute_location(name_attribute),
            "CCXML string variable name must be a dotted NCName location");
    }
    status = validate_string_literal(
        expression_attribute, &expression, diagnostic);
    if (status != CCXML_OK) return status;
    for (index = 0u; index < salts_xml_node_child_count(node); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(node, index);
        if (!node_is_ignorable(child)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child),
                "CCXML string variable element must be empty");
        }
    }
    if (!checked_add(name.size, 1u, &retained_size) ||
        !checked_add(retained_size, expression.size - 1u, &retained_size) ||
        (counts_as_action &&
         (measurement->action_count >= limits->max_actions ||
          !checked_add(measurement->action_count, 1u,
                       &measurement->action_count))) ||
        !checked_add(measurement->name_bytes, retained_size,
                     &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes) {
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            salts_xml_node_location(node),
            counts_as_action
                ? "CCXML assign action or retained-string limit exceeded"
                : "CCXML var retained-string limit exceeded");
    }
    if (out_name != NULL) *out_name = name;
    return CCXML_OK;
}

static ccxml_status measure_conditional_control(
    salts_xml_node node, bool requires_condition, bool requires_empty,
    ccxml_measurement *measurement, const ccxml_limits *limits,
    ccxml_diagnostic *diagnostic) {
    salts_xml_attribute condition_attribute = {0};
    size_t index;
    size_t condition_size = 0u;
    size_t retained_size = 0u;
    ccxml_status status;

    if (requires_condition || requires_empty) {
        status = validate_attributes(
            node, requires_condition ? "cond" : NULL, requires_condition,
            &condition_attribute, diagnostic);
        if (status != CCXML_OK) return status;
    }
    if (requires_empty) {
        for (index = 0u; index < salts_xml_node_child_count(node); ++index) {
            const salts_xml_node child = salts_xml_node_child_at(node, index);
            if (!node_is_ignorable(child)) {
                return fail(
                    diagnostic, CCXML_UNSUPPORTED_FEATURE,
                    salts_xml_node_location(child),
                    "CCXML conditional branch marker must be empty");
            }
        }
    }
    if (requires_condition) {
        const salts_xml_string_view condition =
            salts_xml_attribute_value(condition_attribute);
        const scxml_xml_decode_status decode_status =
            scxml_xml_decode_attribute_entities(
                condition.data, condition.size, NULL, 0u, &condition_size);
        if (!view_has_nonspace(condition) ||
            decode_status != SCXML_XML_DECODE_OK || condition_size == 0u) {
            return fail(
                diagnostic, CCXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(condition_attribute),
                "CCXML conditional expression must be nonempty and XML-decodable");
        }
        if (!checked_add(condition_size, 1u, &retained_size)) {
            return fail(
                diagnostic, CCXML_LIMIT_EXCEEDED,
                salts_xml_attribute_location(condition_attribute),
                "CCXML conditional expression storage overflow");
        }
    }
    if (measurement->action_count >= limits->max_actions ||
        !checked_add(measurement->action_count, 1u,
                     &measurement->action_count) ||
        !checked_add(measurement->name_bytes, retained_size,
                     &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes) {
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED, salts_xml_node_location(node),
            "CCXML conditional action or retained-string limit exceeded");
    }
    return CCXML_OK;
}

static ccxml_status validate_leaf_action(
    salts_xml_node action, salts_xml_string_view name,
    ccxml_measurement *measurement, const ccxml_limits *limits,
    salts_xml_string_view declared_variable, size_t *out_effect_count,
    ccxml_diagnostic *diagnostic) {
    ccxml_status status;
    if (!view_equal(name, "accept") && !view_equal(name, "exit") &&
        !view_equal(name, "createcall") && !view_equal(name, "disconnect") &&
        !view_equal(name, "reject") && !view_equal(name, "redirect") &&
        !view_equal(name, "join") && !view_equal(name, "unjoin") &&
        !view_equal(name, "merge") && !view_equal(name, "createconference") &&
        !view_equal(name, "destroyconference") &&
        !view_equal(name, "dialogprepare") && !view_equal(name, "dialogstart") &&
        !view_equal(name, "dialogterminate") && !view_equal(name, "assign") &&
        !view_equal(name, "send") && !view_equal(name, "cancel")) {
        return fail(
            diagnostic, CCXML_UNSUPPORTED_FEATURE,
            salts_xml_node_location(action), "unsupported CCXML executable content");
    }
    if (view_equal(name, "createcall") || view_equal(name, "redirect")) {
        status = validate_destination_action(
            action, measurement, limits, view_equal(name, "createcall"),
            diagnostic);
    } else if (view_equal(name, "join") || view_equal(name, "unjoin")) {
        status = validate_two_identifier_action(
            action, "id1", "id2", measurement, limits, diagnostic);
    } else if (view_equal(name, "merge")) {
        status = validate_two_identifier_action(
            action, "connectionid1", "connectionid2", measurement, limits,
            diagnostic);
    } else if (view_equal(name, "createconference")) {
        status = validate_create_conference_action(action, measurement, limits, diagnostic);
    } else if (view_equal(name, "destroyconference")) {
        status = validate_destroy_conference_action(action, measurement, limits, diagnostic);
    } else if (view_equal(name, "dialogprepare")) {
        status = validate_dialog_prepare_action(action, measurement, limits, diagnostic);
    } else if (view_equal(name, "dialogstart")) {
        status = node_has_unqualified_attribute(action, "prepareddialogid")
            ? validate_prepared_dialog_start_action(action, measurement, limits, diagnostic)
            : validate_dialog_start_action(action, measurement, limits, diagnostic);
    } else if (view_equal(name, "dialogterminate")) {
        status = validate_dialog_terminate_action(action, measurement, limits, diagnostic);
    } else if (view_equal(name, "assign")) {
        salts_xml_string_view assignment_name = {0};
        status = validate_string_binding(
            action, true, measurement, limits, diagnostic, &assignment_name);
        if (status == CCXML_OK &&
            (declared_variable.data == NULL ||
             declared_variable.size != assignment_name.size ||
             memcmp(declared_variable.data, assignment_name.data,
                    assignment_name.size) != 0)) {
            status = fail(
                diagnostic, CCXML_INVALID_STRUCTURE, salts_xml_node_location(action),
                "assign must name the declared root string var");
        }
    } else if (view_equal(name, "send")) {
        status = validate_send_action(action, measurement, limits, diagnostic);
    } else if (view_equal(name, "cancel")) {
        status = validate_cancel_action(action, measurement, limits, diagnostic);
    } else {
        status = validate_empty_action(action, measurement, limits, diagnostic);
    }
    if (status != CCXML_OK) return status;
    *out_effect_count = view_equal(name, "createconference") ||
                        view_equal(name, "dialogprepare") ||
                        (view_equal(name, "dialogstart") &&
                         !node_has_unqualified_attribute(action, "prepareddialogid")) ||
                        (view_equal(name, "send") &&
                         node_has_unqualified_attribute(action, "sendid"))
        ? 2u : 1u;
    return CCXML_OK;
}

static ccxml_status validate_executable_action(
    salts_xml_node action, ccxml_measurement *measurement,
    const ccxml_limits *limits, salts_xml_string_view declared_variable,
    size_t *local_action_count, size_t *local_effect_count,
    ccxml_diagnostic *diagnostic);

static bool contains_ccxml_foreach(salts_xml_node node) {
    size_t index;
    for (index = 0u; index < salts_xml_node_child_count(node); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(node, index);
        if (node_is_ignorable(child)) continue;
        if (salts_xml_node_type(child) == SALTS_XML_ELEMENT &&
            view_equal(salts_xml_node_namespace_uri(child), CCXML_NAMESPACE) &&
            view_equal(salts_xml_node_local_name(child), "foreach"))
            return true;
        if (contains_ccxml_foreach(child)) return true;
    }
    return false;
}

static ccxml_status validate_foreach_action(
    salts_xml_node action, ccxml_measurement *measurement,
    const ccxml_limits *limits, salts_xml_string_view declared_variable,
    size_t *local_action_count, size_t *local_effect_count,
    ccxml_diagnostic *diagnostic) {
    salts_xml_attribute array_attribute = {0};
    salts_xml_attribute item_attribute = {0};
    salts_xml_attribute index_attribute = {0};
    salts_xml_string_view array;
    salts_xml_string_view item;
    salts_xml_string_view index_name = {0};
    size_t index;
    size_t item_retained_size;
    size_t index_retained_size = 0u;
    size_t retained_size;
    size_t body_action_count = 0u;
    size_t body_effect_count = 0u;
    size_t expanded_effect_count = 0u;
    ccxml_status status;
    for (index = 0u; index < salts_xml_node_attribute_count(action); ++index) {
        const salts_xml_attribute attribute = salts_xml_node_attribute_at(action, index);
        const salts_xml_string_view name = salts_xml_attribute_local_name(attribute);
        if (salts_xml_attribute_namespace_uri(attribute).size != 0u ||
            (view_equal(name, "array") && array_attribute.impl != NULL) ||
            (view_equal(name, "item") && item_attribute.impl != NULL) ||
            (view_equal(name, "index") && index_attribute.impl != NULL) ||
            (!view_equal(name, "array") && !view_equal(name, "item") &&
             !view_equal(name, "index")))
            return fail(diagnostic, CCXML_UNSUPPORTED_FEATURE,
                        salts_xml_attribute_location(attribute),
                        "unsupported or duplicate foreach attribute");
        if (view_equal(name, "array")) array_attribute = attribute;
        else if (view_equal(name, "item")) item_attribute = attribute;
        else index_attribute = attribute;
    }
    if (array_attribute.impl == NULL || item_attribute.impl == NULL)
        return fail(diagnostic, CCXML_INVALID_STRUCTURE,
                    salts_xml_node_location(action),
                    "foreach requires array and item attributes");
    array = salts_xml_attribute_value(array_attribute);
    item = salts_xml_attribute_value(item_attribute);
    if (index_attribute.impl != NULL)
        index_name = salts_xml_attribute_value(index_attribute);
    if (!dotted_location_valid(array) || !dotted_location_valid(item) ||
        memchr(item.data, '.', item.size) != NULL || view_has_space(item))
        return fail(diagnostic, CCXML_UNSUPPORTED_FEATURE,
                    salts_xml_node_location(action),
                    "foreach array must be a dotted location and item a NCName");
    if (index_attribute.impl != NULL &&
        (!dotted_location_valid(index_name) ||
         memchr(index_name.data, '.', index_name.size) != NULL ||
         view_has_space(index_name) ||
         (index_name.size == item.size &&
          memcmp(index_name.data, item.data, item.size) == 0)))
        return fail(diagnostic, CCXML_UNSUPPORTED_FEATURE,
                    salts_xml_attribute_location(index_attribute),
                    "foreach index must be a distinct NCName");
    if (index_attribute.impl != NULL &&
        !checked_add(index_name.size, 1u, &index_retained_size))
        return fail(diagnostic, CCXML_LIMIT_EXCEEDED,
                    salts_xml_attribute_location(index_attribute),
                    "foreach index storage overflowed");
    if (!checked_add(item.size, 1u, &item_retained_size) ||
        !checked_add(array.size, 1u, &retained_size) ||
        !checked_add(retained_size, item_retained_size, &retained_size) ||
        !checked_add(retained_size, index_retained_size, &retained_size) ||
        limits->max_actions < 2u ||
        measurement->action_count > limits->max_actions - 2u ||
        !checked_add(measurement->action_count, 2u, &measurement->action_count) ||
        !checked_add(measurement->name_bytes, retained_size, &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes)
        return fail(diagnostic, CCXML_LIMIT_EXCEEDED,
                    salts_xml_node_location(action),
                    "foreach control or retained-string limit exceeded");
    if (!checked_add(*local_action_count, 2u, local_action_count))
        return fail(diagnostic, CCXML_LIMIT_EXCEEDED,
                    salts_xml_node_location(action),
                    "foreach action count overflowed");
    for (index = 0u; index < salts_xml_node_child_count(action); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(action, index);
        const salts_xml_string_view name = salts_xml_node_local_name(child);
        if (node_is_ignorable(child)) continue;
        if (salts_xml_node_type(child) != SALTS_XML_ELEMENT ||
            !view_equal(salts_xml_node_namespace_uri(child), CCXML_NAMESPACE))
            return fail(diagnostic, CCXML_UNSUPPORTED_FEATURE,
                        salts_xml_node_location(child),
                        "unsupported foreach executable content");
        if (view_equal(name, "foreach") || contains_ccxml_foreach(child))
            return fail(diagnostic, CCXML_UNSUPPORTED_FEATURE,
                        salts_xml_node_location(child),
                        "nested foreach content is unsupported");
        status = validate_executable_action(
            child, measurement, limits, declared_variable,
            &body_action_count, &body_effect_count, diagnostic);
        if (status != CCXML_OK) return status;
    }
    if (!checked_add(*local_action_count, body_action_count,
                     local_action_count) ||
        !checked_multiply(body_effect_count, limits->max_foreach_iterations,
                          &expanded_effect_count) ||
        !checked_add(*local_effect_count, expanded_effect_count,
                     local_effect_count))
        return fail(diagnostic, CCXML_LIMIT_EXCEEDED,
                    salts_xml_node_location(action),
                    "foreach action or effect limit exceeded");
    return CCXML_OK;
}

static ccxml_status validate_if_action(
    salts_xml_node action, ccxml_measurement *measurement,
    const ccxml_limits *limits, salts_xml_string_view declared_variable,
    size_t *local_action_count, size_t *local_effect_count,
    ccxml_diagnostic *diagnostic) {
    size_t index;
    bool seen_else = false;
    ccxml_status status = measure_conditional_control(
        action, true, false, measurement, limits, diagnostic);
    if (status != CCXML_OK) return status;
    ++*local_action_count;
    for (index = 0u; index < salts_xml_node_child_count(action); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(action, index);
        salts_xml_string_view name;
        if (node_is_ignorable(child)) continue;
        if (salts_xml_node_type(child) != SALTS_XML_ELEMENT ||
            !view_equal(salts_xml_node_namespace_uri(child), CCXML_NAMESPACE)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child), "unsupported CCXML executable content");
        }
        name = salts_xml_node_local_name(child);
        if (view_equal(name, "elseif")) {
            if (seen_else) {
                return fail(
                    diagnostic, CCXML_INVALID_STRUCTURE,
                    salts_xml_node_location(child),
                    "CCXML elseif cannot follow else");
            }
            status = measure_conditional_control(
                child, true, true, measurement, limits, diagnostic);
            if (status != CCXML_OK) return status;
            ++*local_action_count;
        } else if (view_equal(name, "else")) {
            if (seen_else) {
                return fail(
                    diagnostic, CCXML_INVALID_STRUCTURE,
                    salts_xml_node_location(child),
                    "CCXML if accepts at most one else");
            }
            seen_else = true;
            status = measure_conditional_control(
                child, false, true, measurement, limits, diagnostic);
            if (status != CCXML_OK) return status;
            ++*local_action_count;
        } else {
            status = validate_executable_action(
                child, measurement, limits, declared_variable, local_action_count,
                local_effect_count, diagnostic);
            if (status != CCXML_OK) return status;
        }
    }
    status = measure_conditional_control(
        action, false, false, measurement, limits, diagnostic);
    if (status != CCXML_OK) return status;
    ++*local_action_count;
    return CCXML_OK;
}

static ccxml_status validate_executable_action(
    salts_xml_node action, ccxml_measurement *measurement,
    const ccxml_limits *limits, salts_xml_string_view declared_variable,
    size_t *local_action_count, size_t *local_effect_count,
    ccxml_diagnostic *diagnostic) {
    const salts_xml_string_view name = salts_xml_node_local_name(action);
    size_t effect_count = 0u;
    ccxml_status status;
    if (view_equal(name, "if")) {
        return validate_if_action(
            action, measurement, limits, declared_variable, local_action_count,
            local_effect_count, diagnostic);
    }
    if (view_equal(name, "foreach")) {
        return validate_foreach_action(
            action, measurement, limits, declared_variable, local_action_count,
            local_effect_count, diagnostic);
    }
    if (view_equal(name, "elseif") || view_equal(name, "else")) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE, salts_xml_node_location(action),
            "CCXML conditional branch marker must be inside if");
    }
    status = validate_leaf_action(
        action, name, measurement, limits, declared_variable, &effect_count,
        diagnostic);
    if (status != CCXML_OK) return status;
    ++*local_action_count;
    if (!checked_add(*local_effect_count, effect_count, local_effect_count)) {
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED, salts_xml_node_location(action),
            "CCXML transition effect limit exceeded");
    }
    return CCXML_OK;
}

static ccxml_status validate_transition(
    salts_xml_node transition, ccxml_measurement *measurement,
    const ccxml_limits *limits, bool has_statevariable,
    salts_xml_string_view declared_variable,
    ccxml_diagnostic *diagnostic) {
    salts_xml_attribute event_attribute = {0};
    salts_xml_attribute state_attribute = {0};
    salts_xml_attribute condition_attribute = {0};
    salts_xml_string_view event;
    salts_xml_string_view state = {0};
    salts_xml_string_view condition = {0};
    size_t condition_decoded_size = 0u;
    size_t condition_retained_size = 0u;
    size_t event_retained_size = 0u;
    size_t state_retained_size = 0u;
    size_t index;
    size_t local_action_count = 0u;
    size_t local_effect_count = 0u;
    ccxml_status status;
    for (index = 0u; index < salts_xml_node_attribute_count(transition);
         ++index) {
        const salts_xml_attribute attribute =
            salts_xml_node_attribute_at(transition, index);
        const salts_xml_string_view name =
            salts_xml_attribute_local_name(attribute);
        if (salts_xml_attribute_namespace_uri(attribute).size != 0u) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "namespaced transition attributes are unsupported");
        }
        if (view_equal(name, "event") && event_attribute.impl == NULL)
            event_attribute = attribute;
        else if (view_equal(name, "state") && state_attribute.impl == NULL)
            state_attribute = attribute;
        else if (view_equal(name, "cond") &&
                 condition_attribute.impl == NULL)
            condition_attribute = attribute;
        else
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(attribute),
                "unsupported or duplicate CCXML transition attribute");
    }
    event = event_attribute.impl != NULL
        ? salts_xml_attribute_value(event_attribute)
        : (salts_xml_string_view){.data = "*", .size = 1u};
    if (event_attribute.impl != NULL &&
        (event.data == NULL || event.size == 0u)) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_attribute_location(event_attribute),
            "CCXML transition event must be nonempty");
    }
    if (view_has_space(event)) {
        return fail(
            diagnostic, CCXML_UNSUPPORTED_FEATURE,
            salts_xml_attribute_location(event_attribute),
            "CCXML transition event pattern cannot contain whitespace");
    }
    if (state_attribute.impl != NULL) {
        state = salts_xml_attribute_value(state_attribute);
        if (!has_statevariable) {
            return fail(
                diagnostic, CCXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(state_attribute),
                "transition state requires eventprocessor statevariable");
        }
        if (!view_has_nonspace(state)) {
            return fail(
                diagnostic, CCXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(state_attribute),
                "CCXML transition state list must be nonempty");
        }
    }
    if (condition_attribute.impl != NULL) {
        scxml_xml_decode_status decode_status;
        condition = salts_xml_attribute_value(condition_attribute);
        if (!view_has_nonspace(condition)) {
            return fail(
                diagnostic, CCXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(condition_attribute),
                "CCXML transition condition must be nonempty");
        }
        decode_status = scxml_xml_decode_attribute_entities(
            condition.data, condition.size, NULL, 0u,
            &condition_decoded_size);
        if (decode_status != SCXML_XML_DECODE_OK ||
            condition_decoded_size == 0u) {
            return fail(
                diagnostic, CCXML_INVALID_STRUCTURE,
                salts_xml_attribute_location(condition_attribute),
                decode_status ==
                        SCXML_XML_DECODE_INVALID_CHARACTER_REFERENCE
                    ? "CCXML condition has an invalid XML character reference"
                    : "CCXML condition has an unsupported XML entity reference");
        }
    }
    if (!checked_add(event.size, 1u, &event_retained_size) ||
        (state_attribute.impl != NULL &&
         !checked_add(state.size, 1u, &state_retained_size)) ||
        (condition_attribute.impl != NULL &&
         !checked_add(condition_decoded_size, 1u,
                      &condition_retained_size)) ||
        measurement->transition_count >= limits->max_transitions ||
        !checked_add(measurement->transition_count, 1u,
                     &measurement->transition_count) ||
        !checked_add(measurement->name_bytes, event_retained_size,
                     &measurement->name_bytes) ||
        (state_attribute.impl != NULL &&
         !checked_add(measurement->name_bytes, state_retained_size,
                      &measurement->name_bytes)) ||
        (condition_attribute.impl != NULL &&
         !checked_add(measurement->name_bytes,
                      condition_retained_size,
                      &measurement->name_bytes)) ||
        measurement->name_bytes > limits->max_name_bytes) {
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            salts_xml_node_location(transition),
            "CCXML transition or retained-name limit exceeded");
    }
    for (index = 0u; index < salts_xml_node_child_count(transition); ++index) {
        const salts_xml_node action =
            salts_xml_node_child_at(transition, index);
        salts_xml_string_view name;
        if (node_is_ignorable(action)) continue;
        if (salts_xml_node_type(action) != SALTS_XML_ELEMENT ||
            !view_equal(salts_xml_node_namespace_uri(action), CCXML_NAMESPACE)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(action),
                "unsupported CCXML executable content");
        }
        name = salts_xml_node_local_name(action);
        if (view_equal(name, "if")) {
            status = validate_if_action(
                action, measurement, limits, declared_variable,
                &local_action_count, &local_effect_count, diagnostic);
            if (status != CCXML_OK) return status;
            continue;
        }
        if (view_equal(name, "foreach")) {
            status = validate_foreach_action(
                action, measurement, limits, declared_variable,
                &local_action_count, &local_effect_count, diagnostic);
            if (status != CCXML_OK) return status;
            continue;
        }
        if (view_equal(name, "elseif") || view_equal(name, "else")) {
            return fail(
                diagnostic, CCXML_INVALID_STRUCTURE,
                salts_xml_node_location(action),
                "CCXML conditional branch marker must be inside if");
        }
        if (!view_equal(name, "accept") && !view_equal(name, "exit") &&
            !view_equal(name, "createcall") &&
            !view_equal(name, "disconnect") && !view_equal(name, "reject") &&
            !view_equal(name, "redirect") && !view_equal(name, "join") &&
            !view_equal(name, "unjoin") && !view_equal(name, "merge") &&
            !view_equal(name, "createconference") &&
            !view_equal(name, "destroyconference") &&
            !view_equal(name, "dialogprepare") &&
            !view_equal(name, "dialogstart") &&
            !view_equal(name, "dialogterminate") &&
            !view_equal(name, "assign") && !view_equal(name, "send") &&
            !view_equal(name, "cancel")) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(action),
                "unsupported CCXML executable content");
        }
        if (view_equal(name, "createcall") || view_equal(name, "redirect")) {
            status = validate_destination_action(
                action, measurement, limits, view_equal(name, "createcall"),
                diagnostic);
        } else if (view_equal(name, "join") || view_equal(name, "unjoin")) {
            status = validate_two_identifier_action(
                action, "id1", "id2", measurement, limits, diagnostic);
        } else if (view_equal(name, "merge")) {
            status = validate_two_identifier_action(
                action, "connectionid1", "connectionid2", measurement,
                limits, diagnostic);
        } else if (view_equal(name, "createconference")) {
            status = validate_create_conference_action(
                action, measurement, limits, diagnostic);
        } else if (view_equal(name, "destroyconference")) {
            status = validate_destroy_conference_action(
                action, measurement, limits, diagnostic);
        } else if (view_equal(name, "dialogprepare")) {
            status = validate_dialog_prepare_action(
                action, measurement, limits, diagnostic);
        } else if (view_equal(name, "dialogstart")) {
            status = node_has_unqualified_attribute(
                         action, "prepareddialogid")
                ? validate_prepared_dialog_start_action(
                      action, measurement, limits, diagnostic)
                : validate_dialog_start_action(
                      action, measurement, limits, diagnostic);
        } else if (view_equal(name, "dialogterminate")) {
            status = validate_dialog_terminate_action(
                action, measurement, limits, diagnostic);
        } else if (view_equal(name, "assign")) {
            salts_xml_string_view assignment_name = {0};
            status = validate_string_binding(
                action, true, measurement, limits,
                diagnostic, &assignment_name);
            if (status == CCXML_OK &&
                (declared_variable.data == NULL ||
                 declared_variable.size != assignment_name.size ||
                 memcmp(declared_variable.data, assignment_name.data,
                        assignment_name.size) != 0)) {
                status = fail(
                    diagnostic, CCXML_INVALID_STRUCTURE,
                    salts_xml_node_location(action),
                    "assign must name the declared root string var");
            }
        } else if (view_equal(name, "send")) {
            status = validate_send_action(
                action, measurement, limits, diagnostic);
        } else if (view_equal(name, "cancel")) {
            status = validate_cancel_action(
                action, measurement, limits, diagnostic);
        } else {
            status = validate_empty_action(
                action, measurement, limits, diagnostic);
        }
        if (status != CCXML_OK) return status;
        ++local_action_count;
        if (!checked_add(
                local_effect_count,
                view_equal(name, "createconference") ||
                        view_equal(name, "dialogprepare") ||
                        (view_equal(name, "dialogstart") &&
                         !node_has_unqualified_attribute(
                             action, "prepareddialogid")) ||
                        (view_equal(name, "send") &&
                         node_has_unqualified_attribute(action, "sendid"))
                    ? 2u : 1u,
                &local_effect_count)) {
            return fail(
                diagnostic, CCXML_LIMIT_EXCEEDED,
                salts_xml_node_location(action),
                "CCXML transition effect limit exceeded");
        }
    }
    if (local_action_count > measurement->max_transition_actions)
        measurement->max_transition_actions = local_action_count;
    if (local_effect_count > measurement->max_transition_effects)
        measurement->max_transition_effects = local_effect_count;
    return CCXML_OK;
}

static ccxml_status validate_eventprocessor(
    salts_xml_node processor, ccxml_measurement *measurement,
    const ccxml_limits *limits, ccxml_diagnostic *diagnostic,
    salts_xml_string_view declared_variable,
    salts_xml_string_view *out_statevariable) {
    salts_xml_attribute statevariable_attribute = {0};
    salts_xml_string_view statevariable = {0};
    size_t index;
    ccxml_status status = validate_attributes(
        processor, "statevariable", false,
        &statevariable_attribute, diagnostic);
    if (status != CCXML_OK) return status;
    if (statevariable_attribute.impl != NULL) {
        statevariable = salts_xml_attribute_value(statevariable_attribute);
        if (!dotted_location_valid(statevariable)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_attribute_location(statevariable_attribute),
                "eventprocessor statevariable must be a dotted NCName location");
        }
        if (!checked_add(
                measurement->name_bytes, statevariable.size + 1u,
                &measurement->name_bytes) ||
            measurement->name_bytes > limits->max_name_bytes) {
            return fail(
                diagnostic, CCXML_LIMIT_EXCEEDED,
                salts_xml_attribute_location(statevariable_attribute),
                "eventprocessor statevariable exceeds retained-name limit");
        }
    }
    for (index = 0u; index < salts_xml_node_child_count(processor); ++index) {
        const salts_xml_node transition =
            salts_xml_node_child_at(processor, index);
        if (node_is_ignorable(transition)) continue;
        if (salts_xml_node_type(transition) != SALTS_XML_ELEMENT ||
            !view_equal(salts_xml_node_namespace_uri(transition), CCXML_NAMESPACE) ||
            !view_equal(salts_xml_node_local_name(transition), "transition")) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(transition),
                "eventprocessor accepts transition elements only");
        }
        {
            const ccxml_status transition_status = validate_transition(
                transition, measurement, limits,
                statevariable_attribute.impl != NULL,
                declared_variable, diagnostic);
            if (transition_status != CCXML_OK) return transition_status;
        }
    }
    if (out_statevariable != NULL) *out_statevariable = statevariable;
    return CCXML_OK;
}

static ccxml_status validate_document(
    salts_xml_node root, ccxml_measurement *measurement,
    const ccxml_limits *limits, ccxml_diagnostic *diagnostic) {
    salts_xml_attribute version_attribute = {0};
    salts_xml_node processor = {0};
    salts_xml_node variable = {0};
    salts_xml_string_view variable_name = {0};
    salts_xml_string_view statevariable = {0};
    size_t index;
    size_t processor_count = 0u;
    size_t variable_count = 0u;
    ccxml_status status;
    if (salts_xml_node_type(root) != SALTS_XML_ELEMENT ||
        !view_equal(salts_xml_node_local_name(root), "ccxml") ||
        !view_equal(salts_xml_node_namespace_uri(root), CCXML_NAMESPACE)) {
        return fail(
            diagnostic, CCXML_INVALID_NAMESPACE,
            salts_xml_node_location(root),
            "root must be W3C CCXML ccxml element");
    }
    status = validate_attributes(
        root, "version", true, &version_attribute, diagnostic);
    if (status != CCXML_OK) return status;
    if (!view_equal(salts_xml_attribute_value(version_attribute), "1.0")) {
        return fail(
            diagnostic, CCXML_INVALID_VERSION,
            salts_xml_attribute_location(version_attribute),
            "CCXML version must be 1.0");
    }
    for (index = 0u; index < salts_xml_node_child_count(root); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(root, index);
        salts_xml_string_view name;
        if (node_is_ignorable(child)) continue;
        if (salts_xml_node_type(child) != SALTS_XML_ELEMENT ||
            !view_equal(salts_xml_node_namespace_uri(child), CCXML_NAMESPACE)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child),
                "unsupported CCXML root content");
        }
        name = salts_xml_node_local_name(child);
        if (view_equal(name, "eventprocessor")) {
            processor = child;
            ++processor_count;
        } else if (view_equal(name, "var")) {
            variable = child;
            ++variable_count;
        } else {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child),
                "CCXML root supports one string var and eventprocessor");
        }
        if (processor_count > 1u || variable_count > 1u) {
            return fail(
                diagnostic,
                processor_count > 1u
                    ? CCXML_INVALID_STRUCTURE : CCXML_UNSUPPORTED_FEATURE,
                salts_xml_node_location(child),
                processor_count > 1u
                    ? "CCXML MVP requires exactly one eventprocessor"
                    : "CCXML bounded profile supports one root string var");
        }
    }
    if (processor_count != 1u) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_node_location(root),
            "CCXML MVP requires exactly one eventprocessor");
    }
    if (variable_count != 0u) {
        status = validate_string_binding(
            variable, false, measurement, limits,
            diagnostic, &variable_name);
        if (status != CCXML_OK) return status;
    }
    status = validate_eventprocessor(
        processor, measurement, limits, diagnostic,
        variable_name, &statevariable);
    if (status != CCXML_OK) return status;
    if (statevariable.data != NULL &&
        (variable_name.data == NULL ||
         variable_name.size != statevariable.size ||
         memcmp(variable_name.data, statevariable.data,
                statevariable.size) != 0)) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            salts_xml_node_location(processor),
            "eventprocessor statevariable must name the root string var");
    }
    return CCXML_OK;
}

static ccxml_status append_copy_item(
    ccxml_copy_item *items, size_t capacity, size_t *count,
    salts_xml_node node, ccxml_copy_item_kind kind, size_t branch_owner,
    size_t *out_index, ccxml_diagnostic *diagnostic) {
    ccxml_copy_item *item;
    if (items == NULL || count == NULL || *count >= capacity) {
        return fail(
            diagnostic, CCXML_INVALID_CONTRACT, salts_xml_node_location(node),
            "CCXML executable-content emission exceeded admission");
    }
    item = &items[*count];
    *item = (ccxml_copy_item){
        .node = node,
        .kind = kind,
        .branch_next = SIZE_MAX,
        .block_end = SIZE_MAX,
        .branch_owner = branch_owner};
    if (out_index != NULL) *out_index = *count;
    ++*count;
    return CCXML_OK;
}

static ccxml_status append_executable_copy_items(
    salts_xml_node action, ccxml_copy_item *items, size_t capacity,
    size_t *count, ccxml_diagnostic *diagnostic);

static ccxml_status append_foreach_copy_items(
    salts_xml_node action, ccxml_copy_item *items, size_t capacity,
    size_t *count, ccxml_diagnostic *diagnostic) {
    size_t foreach_index;
    size_t index;
    size_t end_index;
    ccxml_status status = append_copy_item(
        items, capacity, count, action, CCXML_COPY_ITEM_FOREACH, 0u,
        &foreach_index, diagnostic);
    if (status != CCXML_OK) return status;
    for (index = 0u; index < salts_xml_node_child_count(action); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(action, index);
        if (node_is_ignorable(child)) continue;
        status = append_executable_copy_items(
            child, items, capacity, count, diagnostic);
        if (status != CCXML_OK) return status;
    }
    status = append_copy_item(items, capacity, count, (salts_xml_node){0},
                              CCXML_COPY_ITEM_ENDFOREACH, foreach_index,
                              &end_index, diagnostic);
    if (status != CCXML_OK) return status;
    items[foreach_index].branch_next = foreach_index + 1u;
    items[foreach_index].block_end = end_index + 1u;
    return CCXML_OK;
}

static ccxml_status append_if_copy_items(
    salts_xml_node action, ccxml_copy_item *items, size_t capacity,
    size_t *count, ccxml_diagnostic *diagnostic) {
    size_t if_index;
    size_t previous_branch;
    size_t index;
    size_t end_index;
    ccxml_status status = append_copy_item(
        items, capacity, count, action, CCXML_COPY_ITEM_IF, 0u, &if_index,
        diagnostic);
    if (status != CCXML_OK) return status;
    items[if_index].branch_owner = if_index;
    previous_branch = if_index;
    for (index = 0u; index < salts_xml_node_child_count(action); ++index) {
        const salts_xml_node child = salts_xml_node_child_at(action, index);
        salts_xml_string_view name;
        if (node_is_ignorable(child)) continue;
        name = salts_xml_node_local_name(child);
        if (view_equal(name, "elseif") || view_equal(name, "else")) {
            size_t branch_index;
            const ccxml_copy_item_kind kind = view_equal(name, "elseif")
                ? CCXML_COPY_ITEM_ELSEIF : CCXML_COPY_ITEM_ELSE;
            status = append_copy_item(
                items, capacity, count, child, kind, if_index, &branch_index,
                diagnostic);
            if (status != CCXML_OK) return status;
            items[previous_branch].branch_next = branch_index;
            previous_branch = branch_index;
        } else {
            status = append_executable_copy_items(
                child, items, capacity, count, diagnostic);
            if (status != CCXML_OK) return status;
        }
    }
    status = append_copy_item(
        items, capacity, count, (salts_xml_node){0}, CCXML_COPY_ITEM_ENDIF,
        if_index, &end_index, diagnostic);
    if (status != CCXML_OK) return status;
    items[previous_branch].branch_next = end_index;
    for (index = if_index; index < end_index; ++index) {
        if (items[index].branch_owner == if_index &&
            (items[index].kind == CCXML_COPY_ITEM_IF ||
             items[index].kind == CCXML_COPY_ITEM_ELSEIF ||
             items[index].kind == CCXML_COPY_ITEM_ELSE)) {
            items[index].block_end = end_index;
        }
    }
    return CCXML_OK;
}

static ccxml_status append_executable_copy_items(
    salts_xml_node action, ccxml_copy_item *items, size_t capacity,
    size_t *count, ccxml_diagnostic *diagnostic) {
    const salts_xml_string_view name = salts_xml_node_local_name(action);
    if (view_equal(name, "if")) {
        return append_if_copy_items(action, items, capacity, count, diagnostic);
    }
    if (view_equal(name, "foreach")) {
        return append_foreach_copy_items(action, items, capacity, count,
                                         diagnostic);
    }
    return append_copy_item(
        items, capacity, count, action, CCXML_COPY_ITEM_LEAF, SIZE_MAX, NULL,
        diagnostic);
}

static ccxml_status copy_program(
    ccxml_program_impl *impl, salts_xml_node root,
    ccxml_diagnostic *diagnostic) {
    ccxml_status status = CCXML_OK;
    size_t root_index;
    size_t transition_index = 0u;
    size_t action_index = 0u;
    size_t payload_index = 0u;
    char *cursor = impl->storage;
    ccxml_copy_item *items = NULL;
    if (impl->max_transition_actions != 0u) {
        items = (ccxml_copy_item *)calloc(
            impl->max_transition_actions, sizeof(*items));
        if (items == NULL) {
            return fail(
                diagnostic, CCXML_ALLOCATION_FAILED, salts_xml_node_location(root),
                "CCXML executable-content emission allocation failed");
        }
    }
    for (root_index = 0u;
         root_index < salts_xml_node_child_count(root); ++root_index) {
        const salts_xml_node processor =
            salts_xml_node_child_at(root, root_index);
        size_t processor_index;
        if (salts_xml_node_type(processor) != SALTS_XML_ELEMENT)
            continue;
        if (view_equal(salts_xml_node_local_name(processor), "var")) {
            const salts_xml_attribute name_attribute =
                node_unqualified_attribute(processor, "name");
            const salts_xml_attribute expression_attribute =
                node_unqualified_attribute(processor, "expr");
            const salts_xml_string_view name =
                salts_xml_attribute_value(name_attribute);
            const salts_xml_string_view expression =
                salts_xml_attribute_value(expression_attribute);
            impl->initial_variable = cursor;
            impl->initial_variable_size = name.size;
            memcpy(cursor, name.data, name.size);
            cursor[name.size] = '\0';
            cursor += name.size + 1u;
            impl->initial_value = cursor;
            impl->initial_value_size = expression.size - 2u;
            memcpy(cursor, expression.data + 1u, impl->initial_value_size);
            cursor[impl->initial_value_size] = '\0';
            cursor += impl->initial_value_size + 1u;
            continue;
        }
        if (!view_equal(
                salts_xml_node_local_name(processor), "eventprocessor"))
            continue;
        {
            const salts_xml_attribute statevariable_attribute =
                node_unqualified_attribute(processor, "statevariable");
            if (statevariable_attribute.impl != NULL) {
                const salts_xml_string_view statevariable =
                    salts_xml_attribute_value(statevariable_attribute);
                impl->statevariable = cursor;
                impl->statevariable_size = statevariable.size;
                memcpy(cursor, statevariable.data, statevariable.size);
                cursor[statevariable.size] = '\0';
                cursor += statevariable.size + 1u;
                impl->uses_statevariable = true;
                impl->uses_datamodel_read = true;
            }
        }
        for (processor_index = 0u;
             processor_index < salts_xml_node_child_count(processor);
             ++processor_index) {
            const salts_xml_node transition =
                salts_xml_node_child_at(processor, processor_index);
            salts_xml_attribute event_attribute = {0};
            salts_xml_attribute state_attribute = {0};
            salts_xml_attribute condition_attribute = {0};
            salts_xml_string_view event;
            size_t attribute_index;
            size_t child_index;
            ccxml_transition_row *row;
            if (salts_xml_node_type(transition) != SALTS_XML_ELEMENT) continue;
            for (attribute_index = 0u;
                 attribute_index < salts_xml_node_attribute_count(transition);
                 ++attribute_index) {
                const salts_xml_attribute candidate =
                    salts_xml_node_attribute_at(transition, attribute_index);
                if (view_equal(
                        salts_xml_attribute_local_name(candidate), "event")) {
                    event_attribute = candidate;
                } else if (view_equal(
                               salts_xml_attribute_local_name(candidate),
                               "state"))
                    state_attribute = candidate;
                else if (view_equal(
                             salts_xml_attribute_local_name(candidate),
                             "cond"))
                    condition_attribute = candidate;
            }
            event = event_attribute.impl != NULL
                ? salts_xml_attribute_value(event_attribute)
                : (salts_xml_string_view){.data = "*", .size = 1u};
            row = &impl->transitions[transition_index++];
            row->event = cursor;
            row->event_size = event.size;
            row->first_action = action_index;
            memcpy(cursor, event.data, event.size);
            cursor[event.size] = '\0';
            cursor += event.size + 1u;
            if (state_attribute.impl != NULL) {
                const salts_xml_string_view state =
                    salts_xml_attribute_value(state_attribute);
                row->state = cursor;
                row->state_size = state.size;
                memcpy(cursor, state.data, state.size);
                cursor[state.size] = '\0';
                cursor += state.size + 1u;
            }
            if (condition_attribute.impl != NULL) {
                const salts_xml_string_view condition =
                    salts_xml_attribute_value(condition_attribute);
                size_t decoded_size = 0u;
                row->condition = cursor;
                (void)scxml_xml_decode_attribute_entities(
                    condition.data, condition.size, cursor,
                    condition.size, &decoded_size);
                row->condition_size = decoded_size;
                cursor[decoded_size] = '\0';
                cursor += decoded_size + 1u;
                impl->uses_condition = true;
            }
            {
                size_t source_index;
                size_t item_count = 0u;
                for (source_index = 0u;
                     source_index < salts_xml_node_child_count(transition);
                     ++source_index) {
                    const salts_xml_node source_action =
                        salts_xml_node_child_at(transition, source_index);
                    if (salts_xml_node_type(source_action) != SALTS_XML_ELEMENT)
                        continue;
                    status = append_executable_copy_items(
                        source_action, items, impl->max_transition_actions,
                        &item_count, diagnostic);
                    if (status != CCXML_OK) {
                        free(items);
                        return status;
                    }
                }
            for (child_index = 0u;
                 child_index < item_count;
                 ++child_index) {
                const ccxml_copy_item *item = &items[child_index];
                const salts_xml_node action = item->node;
                ccxml_action_row *action_row = &impl->actions[action_index++];
                const salts_xml_string_view action_name =
                    item->kind == CCXML_COPY_ITEM_LEAF
                    ? salts_xml_node_local_name(action)
                    : (salts_xml_string_view){0};
                if (item->kind == CCXML_COPY_ITEM_IF ||
                    item->kind == CCXML_COPY_ITEM_ELSEIF) {
                    const salts_xml_attribute action_condition_attribute =
                        node_unqualified_attribute(action, "cond");
                    const salts_xml_string_view condition =
                        salts_xml_attribute_value(action_condition_attribute);
                    size_t decoded_size = 0u;
                    action_row->kind = item->kind == CCXML_COPY_ITEM_IF
                        ? CCXML_ACTION_IF : CCXML_ACTION_ELSEIF;
                    action_row->condition = cursor;
                    (void)scxml_xml_decode_attribute_entities(
                        condition.data, condition.size, cursor, condition.size,
                        &decoded_size);
                    action_row->condition_size = decoded_size;
                    cursor[decoded_size] = '\0';
                    cursor += decoded_size + 1u;
                    action_row->branch_next = row->first_action + item->branch_next;
                    action_row->block_end = row->first_action + item->block_end;
                    impl->uses_condition = true;
                } else if (item->kind == CCXML_COPY_ITEM_ELSE) {
                    action_row->kind = CCXML_ACTION_ELSE;
                    action_row->branch_next = row->first_action + item->branch_next;
                    action_row->block_end = row->first_action + item->block_end;
                } else if (item->kind == CCXML_COPY_ITEM_ENDIF) {
                    action_row->kind = CCXML_ACTION_ENDIF;
                } else if (item->kind == CCXML_COPY_ITEM_FOREACH) {
                    const salts_xml_attribute array_attribute =
                        node_unqualified_attribute(action, "array");
                    const salts_xml_attribute item_attribute =
                        node_unqualified_attribute(action, "item");
                    const salts_xml_attribute index_attribute =
                        node_unqualified_attribute(action, "index");
                    const salts_xml_string_view array =
                        salts_xml_attribute_value(array_attribute);
                    const salts_xml_string_view item_name =
                        salts_xml_attribute_value(item_attribute);
                    const salts_xml_string_view index_name =
                        index_attribute.impl != NULL
                            ? salts_xml_attribute_value(index_attribute)
                            : (salts_xml_string_view){0};
                    action_row->kind = CCXML_ACTION_FOREACH;
                    action_row->location = cursor;
                    action_row->location_size = array.size;
                    memcpy(cursor, array.data, array.size);
                    cursor[array.size] = '\0';
                    cursor += array.size + 1u;
                    action_row->name = cursor;
                    action_row->name_size = item_name.size;
                    memcpy(cursor, item_name.data, item_name.size);
                    cursor[item_name.size] = '\0';
                    cursor += item_name.size + 1u;
                    if (index_attribute.impl != NULL) {
                        action_row->index = cursor;
                        action_row->index_size = index_name.size;
                        memcpy(cursor, index_name.data, index_name.size);
                        cursor[index_name.size] = '\0';
                        cursor += index_name.size + 1u;
                    }
                    action_row->branch_next = row->first_action + item->branch_next;
                    action_row->block_end = row->first_action + item->block_end;
                    impl->uses_foreach = true;
                } else if (item->kind == CCXML_COPY_ITEM_ENDFOREACH) {
                    action_row->kind = CCXML_ACTION_ENDFOREACH;
                    action_row->branch_next =
                        row->first_action + item->branch_owner;
                } else if (view_equal(action_name, "accept")) {
                    action_row->kind = CCXML_ACTION_ACCEPT;
                } else if (view_equal(action_name, "exit")) {
                    action_row->kind = CCXML_ACTION_EXIT;
                } else if (view_equal(action_name, "createcall") ||
                           view_equal(action_name, "redirect")) {
                    size_t destination_attribute_index;
                    salts_xml_attribute destination_attribute = {0};
                    char *destination_value = NULL;
                    size_t destination_size = 0u;
                    bool destination_is_dynamic = false;
                    if (view_equal(action_name, "createcall")) {
                        action_row->kind = CCXML_ACTION_CREATE_CALL;
                        impl->uses_create_call = true;
                    } else {
                        action_row->kind = CCXML_ACTION_REDIRECT;
                        impl->uses_redirect = true;
                    }
                    for (destination_attribute_index = 0u;
                         destination_attribute_index <
                             salts_xml_node_attribute_count(action);
                         ++destination_attribute_index) {
                        const salts_xml_attribute candidate =
                            salts_xml_node_attribute_at(
                                action, destination_attribute_index);
                        if (view_equal(
                                salts_xml_attribute_local_name(candidate),
                                "dest")) {
                            destination_attribute = candidate;
                            break;
                        }
                    }
                    status = decode_destination_value(
                        destination_attribute,
                        view_equal(action_name, "createcall"),
                        &destination_value, &destination_size,
                        &destination_is_dynamic, diagnostic);
                    if (status != CCXML_OK) {
                        free(items);
                        return status;
                    }
                    action_row->destination = cursor;
                    action_row->destination_is_dynamic =
                        destination_is_dynamic;
                    if (action_row->destination_is_dynamic) {
                        impl->uses_string_expression = true;
                    }
                    action_row->destination_size = destination_size;
                    memcpy(cursor, destination_value, destination_size);
                    free(destination_value);
                    cursor[action_row->destination_size] = '\0';
                    cursor += action_row->destination_size + 1u;
                } else if (view_equal(action_name, "disconnect")) {
                    action_row->kind = CCXML_ACTION_DISCONNECT;
                    impl->uses_disconnect = true;
                } else if (view_equal(action_name, "reject")) {
                    action_row->kind = CCXML_ACTION_REJECT;
                    impl->uses_reject = true;
                } else if (view_equal(action_name, "createconference")) {
                    size_t conference_attribute_index;
                    salts_xml_attribute id_attribute = {0};
                    salts_xml_attribute name_attribute = {0};
                    salts_xml_string_view value;
                    action_row->kind = CCXML_ACTION_CREATE_CONFERENCE;
                    impl->uses_create_conference = true;
                    for (conference_attribute_index = 0u;
                         conference_attribute_index <
                             salts_xml_node_attribute_count(action);
                         ++conference_attribute_index) {
                        const salts_xml_attribute candidate =
                            salts_xml_node_attribute_at(
                                action, conference_attribute_index);
                        const salts_xml_string_view local_name =
                            salts_xml_attribute_local_name(candidate);
                        if (view_equal(local_name, "conferenceid"))
                            id_attribute = candidate;
                        else if (view_equal(local_name, "confname"))
                            name_attribute = candidate;
                    }
                    value = salts_xml_attribute_value(id_attribute);
                    action_row->location = cursor;
                    action_row->location_size = value.size;
                    memcpy(cursor, value.data, value.size);
                    cursor[value.size] = '\0';
                    cursor += value.size + 1u;
                    if (name_attribute.impl != NULL) {
                        value = salts_xml_attribute_value(name_attribute);
                        action_row->destination = cursor;
                        action_row->destination_size = value.size - 2u;
                        memcpy(
                            cursor, value.data + 1u,
                            action_row->destination_size);
                        cursor[action_row->destination_size] = '\0';
                        cursor += action_row->destination_size + 1u;
                    }
                } else if (view_equal(
                               action_name, "destroyconference")) {
                    size_t conference_attribute_index;
                    salts_xml_attribute id_attribute = {0};
                    salts_xml_string_view expression;
                    action_row->kind = CCXML_ACTION_DESTROY_CONFERENCE;
                    impl->uses_destroy_conference = true;
                    for (conference_attribute_index = 0u;
                         conference_attribute_index <
                             salts_xml_node_attribute_count(action);
                         ++conference_attribute_index) {
                        const salts_xml_attribute candidate =
                            salts_xml_node_attribute_at(
                                action, conference_attribute_index);
                        if (view_equal(
                                salts_xml_attribute_local_name(candidate),
                                "conferenceid")) {
                            id_attribute = candidate;
                            break;
                        }
                    }
                    expression = salts_xml_attribute_value(id_attribute);
                    if (expression.data[0] == '\'' ||
                        expression.data[0] == '"') {
                        action_row->id1 = cursor;
                        action_row->id1_size = expression.size - 2u;
                        memcpy(
                            cursor, expression.data + 1u,
                            action_row->id1_size);
                        cursor[action_row->id1_size] = '\0';
                        cursor += action_row->id1_size + 1u;
                    } else {
                        action_row->location = cursor;
                        action_row->location_size = expression.size;
                        memcpy(cursor, expression.data, expression.size);
                        cursor[expression.size] = '\0';
                        cursor += expression.size + 1u;
                        impl->uses_datamodel_read = true;
                    }
                } else if (view_equal(action_name, "dialogstart") &&
                           node_has_unqualified_attribute(
                               action, "prepareddialogid")) {
                    size_t dialog_attribute_index;
                    salts_xml_attribute prepared_id_attribute = {0};
                    salts_xml_string_view location;
                    action_row->kind = CCXML_ACTION_PREPARED_DIALOG_START;
                    impl->uses_prepared_dialog_start = true;
                    impl->uses_datamodel_read = true;
                    for (dialog_attribute_index = 0u;
                         dialog_attribute_index <
                             salts_xml_node_attribute_count(action);
                         ++dialog_attribute_index) {
                        const salts_xml_attribute candidate =
                            salts_xml_node_attribute_at(
                                action, dialog_attribute_index);
                        if (view_equal(
                                salts_xml_attribute_local_name(candidate),
                                "prepareddialogid")) {
                            prepared_id_attribute = candidate;
                            break;
                        }
                    }
                    location =
                        salts_xml_attribute_value(prepared_id_attribute);
                    action_row->location = cursor;
                    action_row->location_size = location.size;
                    memcpy(cursor, location.data, location.size);
                    cursor[location.size] = '\0';
                    cursor += location.size + 1u;
                } else if (view_equal(action_name, "dialogprepare") ||
                           view_equal(action_name, "dialogstart")) {
                    size_t dialog_attribute_index;
                    salts_xml_attribute id_attribute = {0};
                    salts_xml_attribute source_attribute = {0};
                    salts_xml_string_view value;
                    if (view_equal(action_name, "dialogprepare")) {
                        action_row->kind = CCXML_ACTION_DIALOG_PREPARE;
                        impl->uses_dialog_prepare = true;
                    } else {
                        action_row->kind = CCXML_ACTION_DIALOG_START;
                        impl->uses_dialog_start = true;
                    }
                    for (dialog_attribute_index = 0u;
                         dialog_attribute_index <
                             salts_xml_node_attribute_count(action);
                         ++dialog_attribute_index) {
                        const salts_xml_attribute candidate =
                            salts_xml_node_attribute_at(
                                action, dialog_attribute_index);
                        const salts_xml_string_view local_name =
                            salts_xml_attribute_local_name(candidate);
                        if (view_equal(local_name, "dialogid"))
                            id_attribute = candidate;
                        else if (view_equal(local_name, "src"))
                            source_attribute = candidate;
                    }
                    value = salts_xml_attribute_value(id_attribute);
                    action_row->location = cursor;
                    action_row->location_size = value.size;
                    memcpy(cursor, value.data, value.size);
                    cursor[value.size] = '\0';
                    cursor += value.size + 1u;
                    value = salts_xml_attribute_value(source_attribute);
                    action_row->destination = cursor;
                    action_row->destination_size = value.size - 2u;
                    memcpy(
                        cursor, value.data + 1u,
                        action_row->destination_size);
                    cursor[action_row->destination_size] = '\0';
                    cursor += action_row->destination_size + 1u;
                } else if (view_equal(
                               action_name, "dialogterminate")) {
                    size_t dialog_attribute_index;
                    salts_xml_attribute id_attribute = {0};
                    salts_xml_string_view expression;
                    action_row->kind = CCXML_ACTION_DIALOG_TERMINATE;
                    impl->uses_dialog_terminate = true;
                    for (dialog_attribute_index = 0u;
                         dialog_attribute_index <
                             salts_xml_node_attribute_count(action);
                         ++dialog_attribute_index) {
                        const salts_xml_attribute candidate =
                            salts_xml_node_attribute_at(
                                action, dialog_attribute_index);
                        if (view_equal(
                                salts_xml_attribute_local_name(candidate),
                                "dialogid")) {
                            id_attribute = candidate;
                            break;
                        }
                    }
                    expression = salts_xml_attribute_value(id_attribute);
                    if (expression.data[0] == '\'' ||
                        expression.data[0] == '"') {
                        action_row->id1 = cursor;
                        action_row->id1_size = expression.size - 2u;
                        memcpy(
                            cursor, expression.data + 1u,
                            action_row->id1_size);
                        cursor[action_row->id1_size] = '\0';
                        cursor += action_row->id1_size + 1u;
                    } else {
                        action_row->location = cursor;
                        action_row->location_size = expression.size;
                        memcpy(cursor, expression.data, expression.size);
                        cursor[expression.size] = '\0';
                        cursor += expression.size + 1u;
                        impl->uses_datamodel_read = true;
                    }
                } else if (view_equal(action_name, "assign")) {
                    const salts_xml_attribute name_attribute =
                        node_unqualified_attribute(action, "name");
                    const salts_xml_attribute expression_attribute =
                        node_unqualified_attribute(action, "expr");
                    salts_xml_string_view value =
                        salts_xml_attribute_value(name_attribute);
                    action_row->kind = CCXML_ACTION_ASSIGN_STRING;
                    action_row->location = cursor;
                    action_row->location_size = value.size;
                    memcpy(cursor, value.data, value.size);
                    cursor[value.size] = '\0';
                    cursor += value.size + 1u;
                    value = salts_xml_attribute_value(expression_attribute);
                    action_row->destination = cursor;
                    action_row->destination_size = value.size - 2u;
                    memcpy(
                        cursor, value.data + 1u,
                        action_row->destination_size);
                    cursor[action_row->destination_size] = '\0';
                    cursor += action_row->destination_size + 1u;
                    impl->uses_assign = true;
                } else if (view_equal(action_name, "send")) {
                    const salts_xml_attribute target_attribute =
                        node_unqualified_attribute(action, "target");
                    const salts_xml_attribute name_attribute =
                        node_unqualified_attribute(action, "name");
                    const salts_xml_attribute type_attribute =
                        node_unqualified_attribute(action, "targettype");
                    const salts_xml_attribute delay_attribute =
                        node_unqualified_attribute(action, "delay");
                    const salts_xml_attribute send_id_attribute =
                        node_unqualified_attribute(action, "sendid");
                    const salts_xml_attribute namelist_attribute =
                        node_unqualified_attribute(action, "namelist");
                    static const char default_type[] = "ccxml";
                    action_row->kind = CCXML_ACTION_SEND;
                    action_row->payload_first = payload_index;
                    action_row->destination = cursor;
                    action_row->destination_size =
                        copy_send_literal(target_attribute, cursor);
                    cursor += action_row->destination_size + 1u;
                    action_row->name = cursor;
                    action_row->name_size =
                        copy_send_literal(name_attribute, cursor);
                    cursor += action_row->name_size + 1u;
                    if (type_attribute.impl != NULL) {
                        action_row->target_type = cursor;
                        action_row->target_type_size =
                            copy_send_literal(type_attribute, cursor);
                        cursor += action_row->target_type_size + 1u;
                    } else {
                        action_row->target_type = default_type;
                        action_row->target_type_size = sizeof(default_type) - 1u;
                    }
                    if (delay_attribute.impl != NULL) {
                        bool delay_is_literal = false;
                        char *delay_value = NULL;
                        status = decode_send_delay(
                            delay_attribute, &delay_value, &action_row->delay_size,
                            &delay_is_literal, diagnostic);
                        if (status != CCXML_OK) return status;
                        action_row->delay = cursor;
                        action_row->delay_is_dynamic = !delay_is_literal;
                        memcpy(cursor, delay_value, action_row->delay_size);
                        cursor[action_row->delay_size] = '\0';
                        if (delay_is_literal) {
                            (void)scxml_time_parse_ms(
                                (salts_xml_string_view){
                                    action_row->delay, action_row->delay_size},
                                &action_row->delay_ms);
                            if (action_row->delay_ms > 0u)
                                impl->uses_delayed_send = true;
                        } else {
                            impl->uses_send_delay = true;
                            impl->uses_delayed_send = true;
                            action_row->delay_ms = 0u;
                        }
                        free(delay_value);
                        cursor += action_row->delay_size + 1u;
                    } else {
                        action_row->delay_is_dynamic = false;
                    }
                    if (send_id_attribute.impl != NULL) {
                        size_t location_size = 0u;
                        action_row->location = cursor;
                        (void)scxml_xml_decode_attribute_entities(
                            salts_xml_attribute_value(send_id_attribute).data,
                            salts_xml_attribute_value(send_id_attribute).size,
                            cursor,
                            salts_xml_attribute_value(send_id_attribute).size,
                            &location_size);
                        action_row->location_size = location_size;
                        cursor[location_size] = '\0';
                        cursor += location_size + 1u;
                        impl->uses_send_id = true;
                    }
                    if (namelist_attribute.impl != NULL) {
                        char *decoded = NULL;
                        size_t decoded_size = 0u;
                        size_t token_cursor = 0u;
                        salts_xml_string_view token;
                        ccxml_status decode_status = decode_attribute_value(
                            namelist_attribute, &decoded, &decoded_size,
                            diagnostic);
                        if (decode_status != CCXML_OK) return decode_status;
                        while (namelist_token_next(
                                   decoded, decoded_size, &token_cursor,
                                   &token)) {
                            if (payload_index >= impl->payload_count) {
                                free(decoded);
                                return fail(
                                    diagnostic, CCXML_INVALID_CONTRACT,
                                    salts_xml_attribute_location(
                                        namelist_attribute),
                                    "CCXML send payload emission exceeded admission");
                            }
                            ccxml_payload_row *payload =
                                &impl->payloads[payload_index++];
                            payload->name = cursor;
                            payload->name_size = token.size;
                            memcpy(cursor, token.data, token.size);
                            cursor[token.size] = '\0';
                            cursor += token.size + 1u;
                        }
                        free(decoded);
                    }
                    action_row->payload_count =
                        payload_index - action_row->payload_first;
                    if (action_row->payload_count != 0u)
                        impl->uses_send_payload = true;
                    impl->uses_send = true;
                } else if (view_equal(action_name, "cancel")) {
                    const salts_xml_attribute send_id_attribute =
                        node_unqualified_attribute(action, "sendid");
                    const salts_xml_string_view expression =
                        salts_xml_attribute_value(send_id_attribute);
                    size_t decoded_size = 0u;
                    action_row->kind = CCXML_ACTION_CANCEL;
                    (void)scxml_xml_decode_attribute_entities(
                        expression.data, expression.size, cursor,
                        expression.size, &decoded_size);
                    if (cursor[0] == '\'' || cursor[0] == '"') {
                        action_row->id1 = cursor;
                        memmove(cursor, cursor + 1u, decoded_size - 2u);
                        action_row->id1_size = decoded_size - 2u;
                        cursor[action_row->id1_size] = '\0';
                        cursor += action_row->id1_size + 1u;
                    } else {
                        action_row->location = cursor;
                        action_row->location_size = decoded_size;
                        cursor[decoded_size] = '\0';
                        cursor += decoded_size + 1u;
                        impl->uses_datamodel_read = true;
                    }
                    impl->uses_cancel = true;
                } else {
                    size_t bridge_attribute_index;
                    salts_xml_attribute id1_attribute = {0};
                    salts_xml_attribute id2_attribute = {0};
                    salts_xml_string_view expression;
                    const bool is_merge = view_equal(action_name, "merge");
                    const char *id1_name =
                        is_merge ? "connectionid1" : "id1";
                    const char *id2_name =
                        is_merge ? "connectionid2" : "id2";
                    if (view_equal(action_name, "join")) {
                        action_row->kind = CCXML_ACTION_JOIN;
                        impl->uses_join = true;
                    } else if (view_equal(action_name, "unjoin")) {
                        action_row->kind = CCXML_ACTION_UNJOIN;
                        impl->uses_unjoin = true;
                    } else {
                        action_row->kind = CCXML_ACTION_MERGE;
                        impl->uses_merge = true;
                    }
                    for (bridge_attribute_index = 0u;
                         bridge_attribute_index <
                             salts_xml_node_attribute_count(action);
                         ++bridge_attribute_index) {
                        const salts_xml_attribute candidate =
                            salts_xml_node_attribute_at(
                                action, bridge_attribute_index);
                        const salts_xml_string_view local_name =
                            salts_xml_attribute_local_name(candidate);
                        if (view_equal(local_name, id1_name)) {
                            id1_attribute = candidate;
                        } else if (view_equal(local_name, id2_name)) {
                            id2_attribute = candidate;
                        }
                    }
                    expression = salts_xml_attribute_value(id1_attribute);
                    action_row->id1 = cursor;
                    action_row->id1_size = expression.size - 2u;
                    memcpy(cursor, expression.data + 1u, action_row->id1_size);
                    cursor[action_row->id1_size] = '\0';
                    cursor += action_row->id1_size + 1u;
                    expression = salts_xml_attribute_value(id2_attribute);
                    action_row->id2 = cursor;
                    action_row->id2_size = expression.size - 2u;
                    memcpy(cursor, expression.data + 1u, action_row->id2_size);
                    cursor[action_row->id2_size] = '\0';
                    cursor += action_row->id2_size + 1u;
                }
                ++row->action_count;
            }
            }
        }
    }
    if (payload_index != impl->payload_count) {
        free(items);
        return fail(
            diagnostic, CCXML_INVALID_CONTRACT,
            salts_xml_node_location(root),
            "CCXML send payload emission mismatched admission");
    }
    free(items);
    return CCXML_OK;
}

ccxml_limits ccxml_default_limits(void) {
    const ccxml_limits limits = {
        salts_xml_default_limits(),
        CCXML_DEFAULT_MAX_TRANSITIONS,
        CCXML_DEFAULT_MAX_ACTIONS,
        CCXML_DEFAULT_MAX_NAME_BYTES,
        CCXML_DEFAULT_MAX_FOREACH_ITERATIONS,
        CCXML_DEFAULT_MAX_FOREACH_STORAGE_BYTES};
    return limits;
}

ccxml_status ccxml_compile(
    ccxml_program *out, const char *input, size_t input_size,
    const ccxml_limits *limits_or_null, ccxml_diagnostic *diagnostic) {
    const ccxml_limits limits = limits_or_null != NULL
        ? *limits_or_null : ccxml_default_limits();
    salts_xml_document document = {0};
    salts_xml_diagnostic xml_diagnostic = {0};
    ccxml_measurement measurement = {0};
    ccxml_program_impl *impl = NULL;
    size_t storage_allocation_bytes = 0u;
    ccxml_status status;
    salts_xml_status xml_status;
    if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
    if (out == NULL || out->impl != NULL || input == NULL || input_size == 0u ||
        limits.xml.max_input_bytes == 0u || limits.xml.max_nodes == 0u ||
        limits.xml.max_attributes == 0u || limits.xml.max_depth == 0u ||
        limits.xml.max_retained_string_bytes == 0u ||
        limits.max_transitions == 0u || limits.max_actions == 0u ||
        limits.max_name_bytes == 0u || limits.max_foreach_iterations == 0u ||
        limits.max_foreach_storage_bytes == 0u) {
        return fail(
            diagnostic, CCXML_INVALID_ARGUMENT,
            (salts_xml_location){0u, 0u, 0u},
            "output, input, and all CCXML limits must be valid");
    }
    xml_status = salts_xml_parse(
        &document, input, input_size, &limits.xml, &xml_diagnostic);
    if (xml_status != SALTS_XML_OK) {
        ccxml_status mapped = CCXML_XML_ERROR;
        if (xml_status == SALTS_XML_LIMIT_EXCEEDED)
            mapped = CCXML_LIMIT_EXCEEDED;
        else if (xml_status == SALTS_XML_ALLOCATION_FAILED)
            mapped = CCXML_ALLOCATION_FAILED;
        return fail(
            diagnostic, mapped, xml_diagnostic.location,
            xml_diagnostic.message);
    }
    status = validate_document(
        salts_xml_document_root(&document), &measurement, &limits, diagnostic);
    if (status != CCXML_OK) goto cleanup;
    if (measurement.name_bytes != 0u &&
        !checked_add(
            measurement.name_bytes, 1u, &storage_allocation_bytes)) {
        status = fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            salts_xml_node_location(salts_xml_document_root(&document)),
            "CCXML program storage size overflow");
        goto cleanup;
    }
    if (measurement.payload_count >
        SIZE_MAX / sizeof(ccxml_payload_row)) {
        status = fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            salts_xml_node_location(salts_xml_document_root(&document)),
            "CCXML payload row storage size overflow");
        goto cleanup;
    }
    impl = (ccxml_program_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) {
        status = fail(
            diagnostic, CCXML_ALLOCATION_FAILED,
            salts_xml_node_location(salts_xml_document_root(&document)),
            "CCXML program allocation failed");
        goto cleanup;
    }
    if (measurement.transition_count != 0u)
        impl->transitions = (ccxml_transition_row *)calloc(
            measurement.transition_count, sizeof(*impl->transitions));
    if (measurement.action_count != 0u)
        impl->actions = (ccxml_action_row *)calloc(
            measurement.action_count, sizeof(*impl->actions));
    if (measurement.payload_count != 0u)
        impl->payloads = (ccxml_payload_row *)calloc(
            measurement.payload_count, sizeof(*impl->payloads));
    if (measurement.name_bytes != 0u)
        impl->storage = (char *)malloc(storage_allocation_bytes);
    if ((measurement.transition_count != 0u && impl->transitions == NULL) ||
        (measurement.action_count != 0u && impl->actions == NULL) ||
        (measurement.payload_count != 0u && impl->payloads == NULL) ||
        (measurement.name_bytes != 0u && impl->storage == NULL)) {
        status = fail(
            diagnostic, CCXML_ALLOCATION_FAILED,
            salts_xml_node_location(salts_xml_document_root(&document)),
            "CCXML program storage allocation failed");
        goto cleanup;
    }
    impl->transition_count = measurement.transition_count;
    impl->action_count = measurement.action_count;
    impl->payload_count = measurement.payload_count;
    impl->max_send_payload_entries = measurement.max_send_payload_entries;
    impl->max_transition_actions = measurement.max_transition_actions;
    impl->max_transition_effects = measurement.max_transition_effects;
    impl->max_foreach_iterations = limits.max_foreach_iterations;
    impl->max_foreach_storage_bytes = limits.max_foreach_storage_bytes;
    status = copy_program(
        impl, salts_xml_document_root(&document), diagnostic);
    if (status != CCXML_OK) goto cleanup;
    status = build_native_statechart(
        impl, salts_xml_node_location(salts_xml_document_root(&document)),
        diagnostic);
    if (status != CCXML_OK) goto cleanup;
    out->impl = impl;
    impl = NULL;
    status = CCXML_OK;

cleanup:
    if (impl != NULL) {
        cflow_statechart_destroy(&impl->statechart);
        free(impl->storage);
        free(impl->payloads);
        free(impl->actions);
        free(impl->transitions);
        free(impl);
    }
    salts_xml_document_destroy(&document);
    return status;
}

void ccxml_program_destroy(ccxml_program *program) {
    ccxml_program_impl *impl = program != NULL
        ? (ccxml_program_impl *)program->impl : NULL;
    if (impl == NULL) return;
    cflow_statechart_destroy(&impl->statechart);
    free(impl->storage);
    free(impl->payloads);
    free(impl->actions);
    free(impl->transitions);
    free(impl);
    program->impl = NULL;
}

size_t ccxml_program_transition_count(const ccxml_program *program) {
    const ccxml_program_impl *impl = program != NULL
        ? (const ccxml_program_impl *)program->impl : NULL;
    return impl != NULL ? impl->transition_count : 0u;
}

const char *ccxml_program_transition_event(
    const ccxml_program *program, size_t index) {
    const ccxml_program_impl *impl = program != NULL
        ? (const ccxml_program_impl *)program->impl : NULL;
    return impl != NULL && index < impl->transition_count
        ? impl->transitions[index].event : NULL;
}

size_t ccxml_program_action_count(const ccxml_program *program) {
    const ccxml_program_impl *impl = program != NULL
        ? (const ccxml_program_impl *)program->impl : NULL;
    return impl != NULL ? impl->action_count : 0u;
}
