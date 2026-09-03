#include "ccxml_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CCXML_NAMESPACE "http://www.w3.org/2002/09/ccxml"
#define CCXML_DEFAULT_MAX_TRANSITIONS 1024u
#define CCXML_DEFAULT_MAX_ACTIONS 4096u
#define CCXML_DEFAULT_MAX_NAME_BYTES (256u * 1024u)

typedef struct ccxml_measurement {
    size_t transition_count;
    size_t action_count;
    size_t name_bytes;
    size_t max_transition_actions;
} ccxml_measurement;

static bool checked_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) return false;
    *out = left + right;
    return true;
}

static bool view_equal(turbo_xml_string_view view, const char *text) {
    const size_t size = text != NULL ? strlen(text) : 0u;
    return view.data != NULL && view.size == size &&
           memcmp(view.data, text, size) == 0;
}

static bool view_has_space(turbo_xml_string_view view) {
    size_t index;
    for (index = 0u; index < view.size; ++index) {
        const char value = view.data[index];
        if (value == ' ' || value == '\t' || value == '\r' || value == '\n')
            return true;
    }
    return false;
}

static bool text_is_whitespace(turbo_xml_string_view view) {
    size_t index;
    for (index = 0u; index < view.size; ++index) {
        const char value = view.data[index];
        if (value != ' ' && value != '\t' && value != '\r' && value != '\n')
            return false;
    }
    return true;
}

static bool node_is_ignorable(turbo_xml_node node) {
    const turbo_xml_node_kind kind = turbo_xml_node_type(node);
    return kind == TURBO_XML_COMMENT ||
           kind == TURBO_XML_PROCESSING_INSTRUCTION ||
           (kind == TURBO_XML_TEXT &&
            text_is_whitespace(turbo_xml_node_value(node)));
}

static ccxml_status fail(
    ccxml_diagnostic *diagnostic, ccxml_status status,
    turbo_xml_location location, const char *message) {
    if (diagnostic != NULL) {
        diagnostic->status = status;
        diagnostic->location = location;
        if (message == NULL) message = "CCXML error";
        (void)snprintf(
            diagnostic->message, sizeof(diagnostic->message), "%s", message);
    }
    return status;
}

static ccxml_status validate_attributes(
    turbo_xml_node node, const char *only_name, bool required,
    turbo_xml_attribute *out_attribute, ccxml_diagnostic *diagnostic) {
    size_t index;
    turbo_xml_attribute found = {0};
    for (index = 0u; index < turbo_xml_node_attribute_count(node); ++index) {
        const turbo_xml_attribute attribute =
            turbo_xml_node_attribute_at(node, index);
        const turbo_xml_string_view namespace_uri =
            turbo_xml_attribute_namespace_uri(attribute);
        const turbo_xml_string_view local_name =
            turbo_xml_attribute_local_name(attribute);
        if (namespace_uri.size != 0u || only_name == NULL ||
            !view_equal(local_name, only_name) || found.impl != NULL) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                turbo_xml_attribute_location(attribute),
                "unsupported or duplicate CCXML attribute");
        }
        found = attribute;
    }
    if (required && found.impl == NULL) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            turbo_xml_node_location(node), "required CCXML attribute is missing");
    }
    if (out_attribute != NULL) *out_attribute = found;
    return CCXML_OK;
}

static ccxml_status validate_empty_action(
    turbo_xml_node action, ccxml_measurement *measurement,
    const ccxml_limits *limits, ccxml_diagnostic *diagnostic) {
    size_t index;
    if (turbo_xml_node_attribute_count(action) != 0u) {
        return fail(
            diagnostic, CCXML_UNSUPPORTED_FEATURE,
            turbo_xml_node_location(action),
            "CCXML MVP actions do not accept attributes");
    }
    for (index = 0u; index < turbo_xml_node_child_count(action); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(action, index);
        if (!node_is_ignorable(child)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                turbo_xml_node_location(child),
                "CCXML MVP actions must be empty");
        }
    }
    if (measurement->action_count >= limits->max_actions ||
        !checked_add(measurement->action_count, 1u,
                     &measurement->action_count)) {
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            turbo_xml_node_location(action), "CCXML action limit exceeded");
    }
    return CCXML_OK;
}

static ccxml_status validate_create_call(
    turbo_xml_node action, ccxml_measurement *measurement,
    const ccxml_limits *limits, ccxml_diagnostic *diagnostic) {
    turbo_xml_attribute destination_attribute = {0};
    turbo_xml_string_view expression;
    char quote;
    size_t index;
    size_t retained_size;
    ccxml_status status = validate_attributes(
        action, "dest", true, &destination_attribute, diagnostic);
    if (status != CCXML_OK) return status;
    expression = turbo_xml_attribute_value(destination_attribute);
    if (expression.data == NULL || expression.size < 2u ||
        (expression.data[0] != '\'' && expression.data[0] != '"') ||
        expression.data[expression.size - 1u] != expression.data[0]) {
        return fail(
            diagnostic, CCXML_UNSUPPORTED_FEATURE,
            turbo_xml_attribute_location(destination_attribute),
            "CCXML createcall dest must be a quoted string literal");
    }
    quote = expression.data[0];
    if (expression.size == 2u) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            turbo_xml_attribute_location(destination_attribute),
            "CCXML createcall dest must be nonempty");
    }
    for (index = 1u; index + 1u < expression.size; ++index) {
        if (expression.data[index] == '\\' || expression.data[index] == quote) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                turbo_xml_attribute_location(destination_attribute),
                "CCXML createcall dest escapes are not supported");
        }
    }
    for (index = 0u; index < turbo_xml_node_child_count(action); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(action, index);
        if (!node_is_ignorable(child)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                turbo_xml_node_location(child),
                "CCXML createcall must be empty");
        }
    }
    if (!checked_add(expression.size - 2u, 1u, &retained_size) ||
        measurement->action_count >= limits->max_actions ||
        !checked_add(measurement->action_count, 1u,
                     &measurement->action_count) ||
        !checked_add(measurement->name_bytes, retained_size,
                     &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes) {
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            turbo_xml_node_location(action),
            "CCXML action or retained-string limit exceeded");
    }
    return CCXML_OK;
}

static ccxml_status validate_transition(
    turbo_xml_node transition, ccxml_measurement *measurement,
    const ccxml_limits *limits, ccxml_diagnostic *diagnostic) {
    turbo_xml_attribute event_attribute = {0};
    turbo_xml_string_view event;
    size_t index;
    size_t local_action_count = 0u;
    ccxml_status status = validate_attributes(
        transition, "event", true, &event_attribute, diagnostic);
    if (status != CCXML_OK) return status;
    event = turbo_xml_attribute_value(event_attribute);
    if (event.data == NULL || event.size == 0u) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            turbo_xml_attribute_location(event_attribute),
            "CCXML transition event must be nonempty");
    }
    if (view_has_space(event)) {
        return fail(
            diagnostic, CCXML_UNSUPPORTED_FEATURE,
            turbo_xml_attribute_location(event_attribute),
            "CCXML MVP supports one exact event name per transition");
    }
    if (measurement->transition_count >= limits->max_transitions ||
        !checked_add(measurement->transition_count, 1u,
                     &measurement->transition_count) ||
        !checked_add(measurement->name_bytes, event.size + 1u,
                     &measurement->name_bytes) ||
        measurement->name_bytes > limits->max_name_bytes) {
        return fail(
            diagnostic, CCXML_LIMIT_EXCEEDED,
            turbo_xml_node_location(transition),
            "CCXML transition or retained-name limit exceeded");
    }
    for (index = 0u; index < turbo_xml_node_child_count(transition); ++index) {
        const turbo_xml_node action =
            turbo_xml_node_child_at(transition, index);
        turbo_xml_string_view name;
        if (node_is_ignorable(action)) continue;
        if (turbo_xml_node_type(action) != TURBO_XML_ELEMENT ||
            !view_equal(turbo_xml_node_namespace_uri(action), CCXML_NAMESPACE)) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                turbo_xml_node_location(action),
                "unsupported CCXML executable content");
        }
        name = turbo_xml_node_local_name(action);
        if (!view_equal(name, "accept") && !view_equal(name, "exit") &&
            !view_equal(name, "createcall")) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                turbo_xml_node_location(action),
                "unsupported CCXML executable content");
        }
        status = view_equal(name, "createcall")
            ? validate_create_call(action, measurement, limits, diagnostic)
            : validate_empty_action(action, measurement, limits, diagnostic);
        if (status != CCXML_OK) return status;
        ++local_action_count;
    }
    if (local_action_count > measurement->max_transition_actions)
        measurement->max_transition_actions = local_action_count;
    return CCXML_OK;
}

static ccxml_status validate_eventprocessor(
    turbo_xml_node processor, ccxml_measurement *measurement,
    const ccxml_limits *limits, ccxml_diagnostic *diagnostic) {
    size_t index;
    if (turbo_xml_node_attribute_count(processor) != 0u) {
        return fail(
            diagnostic, CCXML_UNSUPPORTED_FEATURE,
            turbo_xml_node_location(processor),
            "CCXML MVP eventprocessor does not accept attributes");
    }
    for (index = 0u; index < turbo_xml_node_child_count(processor); ++index) {
        const turbo_xml_node transition =
            turbo_xml_node_child_at(processor, index);
        if (node_is_ignorable(transition)) continue;
        if (turbo_xml_node_type(transition) != TURBO_XML_ELEMENT ||
            !view_equal(turbo_xml_node_namespace_uri(transition), CCXML_NAMESPACE) ||
            !view_equal(turbo_xml_node_local_name(transition), "transition")) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                turbo_xml_node_location(transition),
                "eventprocessor accepts transition elements only");
        }
        {
            const ccxml_status status = validate_transition(
                transition, measurement, limits, diagnostic);
            if (status != CCXML_OK) return status;
        }
    }
    return CCXML_OK;
}

static ccxml_status validate_document(
    turbo_xml_node root, ccxml_measurement *measurement,
    const ccxml_limits *limits, ccxml_diagnostic *diagnostic) {
    turbo_xml_attribute version_attribute = {0};
    size_t index;
    size_t processor_count = 0u;
    ccxml_status status;
    if (turbo_xml_node_type(root) != TURBO_XML_ELEMENT ||
        !view_equal(turbo_xml_node_local_name(root), "ccxml") ||
        !view_equal(turbo_xml_node_namespace_uri(root), CCXML_NAMESPACE)) {
        return fail(
            diagnostic, CCXML_INVALID_NAMESPACE,
            turbo_xml_node_location(root),
            "root must be W3C CCXML ccxml element");
    }
    status = validate_attributes(
        root, "version", true, &version_attribute, diagnostic);
    if (status != CCXML_OK) return status;
    if (!view_equal(turbo_xml_attribute_value(version_attribute), "1.0")) {
        return fail(
            diagnostic, CCXML_INVALID_VERSION,
            turbo_xml_attribute_location(version_attribute),
            "CCXML version must be 1.0");
    }
    for (index = 0u; index < turbo_xml_node_child_count(root); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(root, index);
        if (node_is_ignorable(child)) continue;
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT ||
            !view_equal(turbo_xml_node_namespace_uri(child), CCXML_NAMESPACE) ||
            !view_equal(turbo_xml_node_local_name(child), "eventprocessor")) {
            return fail(
                diagnostic, CCXML_UNSUPPORTED_FEATURE,
                turbo_xml_node_location(child),
                "CCXML MVP root accepts eventprocessor only");
        }
        ++processor_count;
        if (processor_count > 1u) {
            return fail(
                diagnostic, CCXML_INVALID_STRUCTURE,
                turbo_xml_node_location(child),
                "CCXML MVP requires exactly one eventprocessor");
        }
        status = validate_eventprocessor(
            child, measurement, limits, diagnostic);
        if (status != CCXML_OK) return status;
    }
    if (processor_count != 1u) {
        return fail(
            diagnostic, CCXML_INVALID_STRUCTURE,
            turbo_xml_node_location(root),
            "CCXML MVP requires exactly one eventprocessor");
    }
    return CCXML_OK;
}

static void copy_program(
    ccxml_program_impl *impl, turbo_xml_node root) {
    size_t root_index;
    size_t transition_index = 0u;
    size_t action_index = 0u;
    char *cursor = impl->storage;
    for (root_index = 0u;
         root_index < turbo_xml_node_child_count(root); ++root_index) {
        const turbo_xml_node processor =
            turbo_xml_node_child_at(root, root_index);
        size_t processor_index;
        if (turbo_xml_node_type(processor) != TURBO_XML_ELEMENT ||
            !view_equal(turbo_xml_node_local_name(processor), "eventprocessor"))
            continue;
        for (processor_index = 0u;
             processor_index < turbo_xml_node_child_count(processor);
             ++processor_index) {
            const turbo_xml_node transition =
                turbo_xml_node_child_at(processor, processor_index);
            turbo_xml_attribute event_attribute = {0};
            turbo_xml_string_view event;
            size_t attribute_index;
            size_t child_index;
            ccxml_transition_row *row;
            if (turbo_xml_node_type(transition) != TURBO_XML_ELEMENT) continue;
            for (attribute_index = 0u;
                 attribute_index < turbo_xml_node_attribute_count(transition);
                 ++attribute_index) {
                const turbo_xml_attribute candidate =
                    turbo_xml_node_attribute_at(transition, attribute_index);
                if (view_equal(
                        turbo_xml_attribute_local_name(candidate), "event")) {
                    event_attribute = candidate;
                    break;
                }
            }
            event = turbo_xml_attribute_value(event_attribute);
            row = &impl->transitions[transition_index++];
            row->event = cursor;
            row->event_size = event.size;
            row->first_action = action_index;
            memcpy(cursor, event.data, event.size);
            cursor[event.size] = '\0';
            cursor += event.size + 1u;
            for (child_index = 0u;
                 child_index < turbo_xml_node_child_count(transition);
                 ++child_index) {
                const turbo_xml_node action =
                    turbo_xml_node_child_at(transition, child_index);
                if (turbo_xml_node_type(action) != TURBO_XML_ELEMENT) continue;
                ccxml_action_row *action_row = &impl->actions[action_index++];
                const turbo_xml_string_view action_name =
                    turbo_xml_node_local_name(action);
                if (view_equal(action_name, "accept")) {
                    action_row->kind = CCXML_ACTION_ACCEPT;
                } else if (view_equal(action_name, "exit")) {
                    action_row->kind = CCXML_ACTION_EXIT;
                } else {
                    size_t create_attribute_index;
                    turbo_xml_attribute destination_attribute = {0};
                    turbo_xml_string_view expression;
                    action_row->kind = CCXML_ACTION_CREATE_CALL;
                    impl->uses_create_call = true;
                    for (create_attribute_index = 0u;
                         create_attribute_index <
                             turbo_xml_node_attribute_count(action);
                         ++create_attribute_index) {
                        const turbo_xml_attribute candidate =
                            turbo_xml_node_attribute_at(
                                action, create_attribute_index);
                        if (view_equal(
                                turbo_xml_attribute_local_name(candidate),
                                "dest")) {
                            destination_attribute = candidate;
                            break;
                        }
                    }
                    expression =
                        turbo_xml_attribute_value(destination_attribute);
                    action_row->destination = cursor;
                    action_row->destination_size = expression.size - 2u;
                    memcpy(
                        cursor, expression.data + 1u,
                        action_row->destination_size);
                    cursor[action_row->destination_size] = '\0';
                    cursor += action_row->destination_size + 1u;
                }
                ++row->action_count;
            }
        }
    }
}

ccxml_limits ccxml_default_limits(void) {
    const ccxml_limits limits = {
        turbo_xml_default_limits(),
        CCXML_DEFAULT_MAX_TRANSITIONS,
        CCXML_DEFAULT_MAX_ACTIONS,
        CCXML_DEFAULT_MAX_NAME_BYTES};
    return limits;
}

ccxml_status ccxml_compile(
    ccxml_program *out, const char *input, size_t input_size,
    const ccxml_limits *limits_or_null, ccxml_diagnostic *diagnostic) {
    const ccxml_limits limits = limits_or_null != NULL
        ? *limits_or_null : ccxml_default_limits();
    turbo_xml_document document = {0};
    turbo_xml_diagnostic xml_diagnostic = {0};
    ccxml_measurement measurement = {0};
    ccxml_program_impl *impl = NULL;
    ccxml_status status;
    turbo_xml_status xml_status;
    if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
    if (out == NULL || out->impl != NULL || input == NULL || input_size == 0u ||
        limits.xml.max_input_bytes == 0u || limits.xml.max_nodes == 0u ||
        limits.xml.max_attributes == 0u || limits.xml.max_depth == 0u ||
        limits.xml.max_retained_string_bytes == 0u ||
        limits.max_transitions == 0u || limits.max_actions == 0u ||
        limits.max_name_bytes == 0u) {
        return fail(
            diagnostic, CCXML_INVALID_ARGUMENT,
            (turbo_xml_location){0u, 0u, 0u},
            "output, input, and all CCXML limits must be valid");
    }
    xml_status = turbo_xml_parse(
        &document, input, input_size, &limits.xml, &xml_diagnostic);
    if (xml_status != TURBO_XML_OK) {
        ccxml_status mapped = CCXML_XML_ERROR;
        if (xml_status == TURBO_XML_LIMIT_EXCEEDED)
            mapped = CCXML_LIMIT_EXCEEDED;
        else if (xml_status == TURBO_XML_ALLOCATION_FAILED)
            mapped = CCXML_ALLOCATION_FAILED;
        return fail(
            diagnostic, mapped, xml_diagnostic.location,
            xml_diagnostic.message);
    }
    status = validate_document(
        turbo_xml_document_root(&document), &measurement, &limits, diagnostic);
    if (status != CCXML_OK) goto cleanup;
    impl = (ccxml_program_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) {
        status = fail(
            diagnostic, CCXML_ALLOCATION_FAILED,
            turbo_xml_node_location(turbo_xml_document_root(&document)),
            "CCXML program allocation failed");
        goto cleanup;
    }
    if (measurement.transition_count != 0u)
        impl->transitions = (ccxml_transition_row *)calloc(
            measurement.transition_count, sizeof(*impl->transitions));
    if (measurement.action_count != 0u)
        impl->actions = (ccxml_action_row *)calloc(
            measurement.action_count, sizeof(*impl->actions));
    if (measurement.name_bytes != 0u)
        impl->storage = (char *)malloc(measurement.name_bytes);
    if ((measurement.transition_count != 0u && impl->transitions == NULL) ||
        (measurement.action_count != 0u && impl->actions == NULL) ||
        (measurement.name_bytes != 0u && impl->storage == NULL)) {
        status = fail(
            diagnostic, CCXML_ALLOCATION_FAILED,
            turbo_xml_node_location(turbo_xml_document_root(&document)),
            "CCXML program storage allocation failed");
        goto cleanup;
    }
    impl->transition_count = measurement.transition_count;
    impl->action_count = measurement.action_count;
    impl->max_transition_actions = measurement.max_transition_actions;
    copy_program(impl, turbo_xml_document_root(&document));
    out->impl = impl;
    impl = NULL;
    status = CCXML_OK;

cleanup:
    if (impl != NULL) {
        free(impl->storage);
        free(impl->actions);
        free(impl->transitions);
        free(impl);
    }
    turbo_xml_document_destroy(&document);
    return status;
}

void ccxml_program_destroy(ccxml_program *program) {
    ccxml_program_impl *impl = program != NULL
        ? (ccxml_program_impl *)program->impl : NULL;
    if (impl == NULL) return;
    free(impl->storage);
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
