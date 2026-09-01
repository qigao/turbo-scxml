#include "scxml_emit.h"
#include "scxml_analyze.h"
#include "scxml_runtime.h"

static scxml_status resolve_condition_state(
    scxml_build *build, scxml_syntax_node node,
    cflow_machine_state_id *out_state) {
    const scxml_syntax_attribute condition = scxml_analyze_find_attribute(node, "cond");
    turbo_xml_string_view state_name;
    const scxml_name_ref *state;
    if (condition.impl == NULL ||
        !scxml_analyze_parse_null_in_condition(
            scxml_syntax_attribute_value(condition), &state_name)) {
        return scxml_analyze_fail(
            build, SCXML_INVALID_STRUCTURE,
            condition.impl != NULL
                ? scxml_syntax_attribute_location(condition)
                : scxml_syntax_node_location(node),
            "null-model condition must be In(id)");
    }
    state = scxml_analyze_find_name_ref(
        build->state_names, build->state_name_index, state_name);
    if (state == NULL) {
        return scxml_analyze_fail(
            build, SCXML_UNKNOWN_TARGET,
            scxml_syntax_attribute_location(condition),
            "In(id) names an unknown SCXML state");
    }
    if (state->id == 0u || state->id > build->state_index) {
        return scxml_analyze_fail(
            build, SCXML_NATIVE_IR_REJECTED,
            scxml_syntax_attribute_location(condition),
            "In(id) resolved outside native state storage");
    }
    *out_state = (cflow_machine_state_id)state->id;
    return SCXML_OK;
}

static bool resolve_cmeta_condition_state(
    void *user, const char *name, size_t name_size,
    cflow_machine_state_id *out_state) {
    const scxml_build *build = (const scxml_build *)user;
    const turbo_xml_string_view wanted = {name, name_size};
    const scxml_name_ref *state;
    if (build == NULL || name == NULL || name_size == 0u ||
        out_state == NULL) {
        return false;
    }
    state = scxml_analyze_find_name_ref(
        build->state_names, build->state_name_index, wanted);
    if (state == NULL || state->id == 0u || state->id > build->state_index)
        return false;
    *out_state = (cflow_machine_state_id)state->id;
    return true;
}

static bool append_utf8_codepoint(
    char *output, size_t capacity, size_t *size, uint32_t codepoint) {
    size_t required;
    if (output == NULL || size == NULL || codepoint == 0u ||
        codepoint > UINT32_C(0x10ffff) ||
        (codepoint >= UINT32_C(0xd800) &&
         codepoint <= UINT32_C(0xdfff))) {
        return false;
    }
    required = codepoint <= UINT32_C(0x7f) ? 1u
             : codepoint <= UINT32_C(0x7ff) ? 2u
             : codepoint <= UINT32_C(0xffff) ? 3u
                                             : 4u;
    if (*size > capacity || required > capacity - *size) return false;
    if (required == 1u) {
        output[(*size)++] = (char)codepoint;
    } else if (required == 2u) {
        output[(*size)++] = (char)(UINT32_C(0xc0) | (codepoint >> 6u));
        output[(*size)++] = (char)(UINT32_C(0x80) | (codepoint & 0x3fu));
    } else if (required == 3u) {
        output[(*size)++] = (char)(UINT32_C(0xe0) | (codepoint >> 12u));
        output[(*size)++] =
            (char)(UINT32_C(0x80) | ((codepoint >> 6u) & 0x3fu));
        output[(*size)++] = (char)(UINT32_C(0x80) | (codepoint & 0x3fu));
    } else {
        output[(*size)++] = (char)(UINT32_C(0xf0) | (codepoint >> 18u));
        output[(*size)++] =
            (char)(UINT32_C(0x80) | ((codepoint >> 12u) & 0x3fu));
        output[(*size)++] =
            (char)(UINT32_C(0x80) | ((codepoint >> 6u) & 0x3fu));
        output[(*size)++] = (char)(UINT32_C(0x80) | (codepoint & 0x3fu));
    }
    return true;
}

static scxml_status decode_cmeta_attribute_source(
    scxml_build *build, scxml_syntax_attribute attribute,
    const char *subject, char **out_source, size_t *out_size) {
    const turbo_xml_string_view source = scxml_syntax_attribute_value(attribute);
    char message[SCXML_DIAGNOSTIC_CAPACITY];
    char *decoded;
    size_t input = 0u;
    size_t output = 0u;
    if (out_source == NULL || out_size == NULL || source.size == 0u)
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_attribute_location(attribute),
                          "CMeta attribute must not be empty");
    if (source.size > build->expression_limits.max_source_bytes)
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_attribute_location(attribute),
                          "CMeta attribute source byte limit exceeded");
    *out_source = NULL;
    *out_size = 0u;
    decoded = (char *)malloc(source.size);
    if (decoded == NULL)
        return scxml_analyze_fail(build, SCXML_ALLOCATION_FAILED,
                          scxml_syntax_attribute_location(attribute),
                          "unable to decode CMeta attribute");
    while (input < source.size) {
        if (source.data[input] != '&') {
            decoded[output++] = source.data[input++];
            continue;
        }
        if (source.size - input >= 4u &&
            memcmp(source.data + input, "&lt;", 4u) == 0) {
            decoded[output++] = '<';
            input += 4u;
        } else if (source.size - input >= 4u &&
                   memcmp(source.data + input, "&gt;", 4u) == 0) {
            decoded[output++] = '>';
            input += 4u;
        } else if (source.size - input >= 5u &&
                   memcmp(source.data + input, "&amp;", 5u) == 0) {
            decoded[output++] = '&';
            input += 5u;
        } else if (source.size - input >= 6u &&
                   memcmp(source.data + input, "&quot;", 6u) == 0) {
            decoded[output++] = '"';
            input += 6u;
        } else if (source.size - input >= 6u &&
                   memcmp(source.data + input, "&apos;", 6u) == 0) {
            decoded[output++] = '\'';
            input += 6u;
        } else if (source.size - input >= 4u &&
                   source.data[input + 1u] == '#') {
            const bool hexadecimal =
                input + 2u < source.size &&
                (source.data[input + 2u] == 'x' ||
                 source.data[input + 2u] == 'X');
            const uint32_t base = hexadecimal ? 16u : 10u;
            size_t cursor = input + (hexadecimal ? 3u : 2u);
            uint32_t codepoint = 0u;
            bool has_digit = false;
            while (cursor < source.size && source.data[cursor] != ';') {
                const unsigned char ch =
                    (unsigned char)source.data[cursor];
                uint32_t digit;
                if (ch >= '0' && ch <= '9') digit = ch - '0';
                else if (hexadecimal && ch >= 'a' && ch <= 'f')
                    digit = UINT32_C(10) + ch - 'a';
                else if (hexadecimal && ch >= 'A' && ch <= 'F')
                    digit = UINT32_C(10) + ch - 'A';
                else break;
                if (codepoint > (UINT32_C(0x10ffff) - digit) / base)
                    break;
                codepoint = codepoint * base + digit;
                has_digit = true;
                ++cursor;
            }
            if (!has_digit || cursor >= source.size ||
                source.data[cursor] != ';' ||
                !append_utf8_codepoint(
                    decoded, source.size, &output, codepoint)) {
                free(decoded);
                return scxml_analyze_fail(
                    build, SCXML_INVALID_STRUCTURE,
                    scxml_syntax_attribute_location(attribute),
                    "CMeta attribute has an invalid XML character reference");
            }
            input = cursor + 1u;
        } else {
            free(decoded);
            (void)snprintf(message, sizeof(message),
                           "CMeta %s has an unsupported XML entity reference",
                           subject != NULL ? subject : "attribute");
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_attribute_location(attribute),
                              message);
        }
    }
    *out_source = decoded;
    *out_size = output;
    return SCXML_OK;
}

static scxml_status compile_cmeta_condition_program(
    scxml_build *build, scxml_syntax_attribute condition,
    scxml_expr_program *program) {
    scxml_expr_diagnostic expression_diagnostic = {0};
    scxml_expr_status expression_status;
    scxml_status status;
    char message[SCXML_DIAGNOSTIC_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    status = decode_cmeta_attribute_source(
        build, condition, "condition", &source, &source_size);
    if (status != SCXML_OK) return status;
    expression_status = scxml_expr_compile_value(
        program, source, source_size,
        build->cmeta_root, resolve_cmeta_condition_state, build,
        &build->expression_limits, &expression_diagnostic);
    free(source);
    if (expression_status == SCXML_EXPR_OK)
        return SCXML_OK;
    if (expression_status == SCXML_EXPR_LIMIT_EXCEEDED)
        status = SCXML_LIMIT_EXCEEDED;
    else if (expression_status == SCXML_EXPR_ALLOCATION_FAILED)
        status = SCXML_ALLOCATION_FAILED;
    else
        status = SCXML_INVALID_STRUCTURE;
    (void)snprintf(
        message, sizeof(message),
        "CMeta condition byte %zu: %s", expression_diagnostic.byte_offset,
        expression_diagnostic.message[0] != '\0'
            ? expression_diagnostic.message
            : "expression compilation failed");
    return scxml_analyze_fail(build, status, scxml_syntax_attribute_location(condition),
                      message);
}

scxml_status scxml_emit_compile_cmeta_value_program(
    scxml_build *build, scxml_syntax_attribute attribute,
    const char *subject, scxml_expr_program *program,
    scxml_expr_value_kind required_kind) {
    scxml_expr_diagnostic diagnostic = {0};
    scxml_expr_status expression_status;
    scxml_status status;
    char message[SCXML_DIAGNOSTIC_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    status = decode_cmeta_attribute_source(
        build, attribute, subject, &source, &source_size);
    if (status != SCXML_OK) return status;
    expression_status = scxml_expr_compile_value(
        program, source, source_size, build->cmeta_root,
        resolve_cmeta_condition_state, build,
        &build->expression_limits, &diagnostic);
    free(source);
    if (expression_status == SCXML_EXPR_OK &&
        (required_kind == SCXML_EXPR_VALUE_INVALID ||
         scxml_expr_program_value_kind(program) == required_kind))
        return SCXML_OK;
    if (expression_status == SCXML_EXPR_OK) {
        scxml_expr_program_destroy(program);
        expression_status = SCXML_EXPR_TYPE_MISMATCH;
        (void)snprintf(diagnostic.message, sizeof(diagnostic.message),
                       "%s", "expression result type is not supported here");
    }
    status = expression_status == SCXML_EXPR_LIMIT_EXCEEDED
        ? SCXML_LIMIT_EXCEEDED
        : expression_status == SCXML_EXPR_ALLOCATION_FAILED
            ? SCXML_ALLOCATION_FAILED
            : SCXML_INVALID_STRUCTURE;
    (void)snprintf(
        message, sizeof(message), "CMeta %s byte %zu: %s",
        subject, diagnostic.byte_offset,
        diagnostic.message[0] != '\0'
            ? diagnostic.message : "expression compilation failed");
    return scxml_analyze_fail(build, status, scxml_syntax_attribute_location(attribute),
                      message);
}

static scxml_status compile_cmeta_condition(
    scxml_build *build, scxml_syntax_attribute condition,
    scxml_guard_user *guard) {
    guard->data_model = SCXML_DATA_MODEL_CMETA;
    guard->execution_error_event = build->execution_error_event;
    return compile_cmeta_condition_program(
        build, condition, &guard->value.expression);
}

static bool evaluate_cmeta_active_state(
    void *user, cflow_machine_state_id state, bool *out_active) {
    const cflow_statechart_guard_context *context =
        (const cflow_statechart_guard_context *)user;
    if (context == NULL || context->is_active == NULL ||
        context->configuration_user == NULL || out_active == NULL) {
        return false;
    }
    *out_active = context->is_active(context->configuration_user, state);
    return true;
}

static bool evaluate_scxml_transition_guard_impl(
    const scxml_guard_user *guard,
    const scxml_expr_system_values *system_values,
    bool preserve_observed_event_name,
    const cflow_statechart_guard_context *context,
    bool *out_enabled, const char **out_error) {
    if (guard == NULL || context == NULL || context->state == NULL ||
        context->is_active == NULL || context->configuration_user == NULL ||
        out_enabled == NULL || out_error == NULL) {
        return false;
    }
    *out_error = NULL;
    if (guard->data_model == SCXML_DATA_MODEL_NULL) {
        *out_enabled = context->is_active(
            context->configuration_user, guard->value.state);
        return true;
    }
    if (guard->data_model == SCXML_DATA_MODEL_CMETA) {
        const bool null_value = false;
        scxml_expr_diagnostic diagnostic = {0};
        scxml_expr_system_values current_system_values;
        if (!scxml_analyze_bind_current_event_system_values(
                system_values, guard->event_names_by_id,
                guard->event_name_count, context->event,
                &current_system_values)) {
            *out_error = "SCXML guard Event is not in the program map";
            return false;
        }
        if (preserve_observed_event_name)
            current_system_values.event_name = system_values->event_name;
        if (scxml_expr_evaluate_with_system(
                &guard->value.expression, context->state,
                evaluate_cmeta_active_state, (void *)context,
                &current_system_values, out_enabled, &diagnostic) ==
            SCXML_EXPR_OK) {
            return true;
        }
        *out_enabled = false;
        if (guard->execution_error_event == 0u) {
            *out_error = "SCXML transition condition error event is unavailable";
            return false;
        }
        {
            const cflow_event_view raised = {
                guard->execution_error_event, &cmeta_type_bool, &null_value};
            return cflow_statechart_guard_context_raise_internal(
                context, &raised, 0u, out_error);
        }
    }
    return false;
}

static bool evaluate_scxml_transition_guard(
    void *user, const cflow_statechart_guard_context *context,
    bool *out_enabled, const char **out_error) {
    const scxml_guard_user *guard = (const scxml_guard_user *)user;
    return evaluate_scxml_transition_guard_impl(
        guard, guard != NULL ? &guard->system_values : NULL,
        false, context, out_enabled, out_error);
}

bool scxml_emit_evaluate_session_transition_guard(
    void *user, const cflow_statechart_guard_context *context,
    bool *out_enabled, const char **out_error) {
    const scxml_session_guard_user *binding =
        (const scxml_session_guard_user *)user;
    if (binding == NULL || binding->session == NULL) {
        if (out_error != NULL)
            *out_error = "SCXML session guard binding is invalid";
        return false;
    }
    return evaluate_scxml_transition_guard_impl(
        binding->guard, &binding->session->system_values,
        true, context, out_enabled, out_error);
}

static scxml_status emit_executable_node(scxml_build *build, scxml_syntax_node node);

static scxml_status emit_raise_step(
    scxml_build *build, scxml_syntax_node node) {
    const scxml_syntax_attribute event_attribute = scxml_analyze_find_attribute(node, "event");
    const scxml_name_ref *event = scxml_analyze_find_name_ref(
        build->event_names, build->event_name_count,
        scxml_syntax_attribute_value(event_attribute));
    const size_t step = build->step_index;
    if (event == NULL) {
        return scxml_analyze_fail(
            build, SCXML_NATIVE_IR_REJECTED,
            scxml_syntax_attribute_location(event_attribute),
            "raise event was not retained in the event map");
    }
    if (step >= build->step_capacity) {
        return scxml_analyze_fail(
            build, SCXML_NATIVE_IR_REJECTED,
            scxml_syntax_node_location(node),
            "raise exceeded admitted step storage");
    }
    ++build->step_index;
    build->steps[step] = (scxml_step){
        SCXML_STEP_RAISE, build->step_index,
        (cflow_event_id)event->id, 0u, 0u};
    return SCXML_OK;
}

static bool retain_effect_attribute(scxml_build *build,
                                    scxml_syntax_attribute attribute,
                                    const char **out_data,
                                    size_t *out_size) {
    turbo_xml_string_view value;
    size_t retained;
    char *stored;
    *out_data = NULL;
    *out_size = 0u;
    if (attribute.impl == NULL) return true;
    value = scxml_syntax_attribute_value(attribute);
    if (!scxml_analyze_checked_add(value.size, 1u, &retained) ||
        build->effect_storage_index > build->effect_storage_capacity ||
        retained >
            build->effect_storage_capacity - build->effect_storage_index) {
        return false;
    }
    stored = build->effect_storage + build->effect_storage_index;
    if (value.size != 0u) memcpy(stored, value.data, value.size);
    stored[value.size] = '\0';
    build->effect_storage_index += retained;
    *out_data = stored;
    *out_size = value.size;
    return true;
}

static bool retain_effect_view(scxml_build *build,
                               turbo_xml_string_view value,
                               const char **out_data,
                               size_t *out_size) {
    size_t retained;
    char *stored;
    *out_data = NULL;
    *out_size = 0u;
    if (value.data == NULL) return true;
    if (!scxml_analyze_checked_add(value.size, 1u, &retained) ||
        build->effect_storage_index > build->effect_storage_capacity ||
        retained >
            build->effect_storage_capacity - build->effect_storage_index)
        return false;
    stored = build->effect_storage + build->effect_storage_index;
    if (value.size != 0u) memcpy(stored, value.data, value.size);
    stored[value.size] = '\0';
    build->effect_storage_index += retained;
    *out_data = stored;
    *out_size = value.size;
    return true;
}

static scxml_status retain_effect_inline_content(
    scxml_build *build, scxml_syntax_node node,
    scxml_content_descriptor *content) {
    size_t retained;
    size_t actual = 0u;
    scxml_status status = scxml_analyze_inspect_inline_content(
        build, node, &content->kind, &content->byte_count);
    if (status != SCXML_OK) return status;
    if (!scxml_analyze_checked_add(content->byte_count, 1u, &retained) ||
        build->effect_storage_index > build->effect_storage_capacity ||
        retained > build->effect_storage_capacity -
                       build->effect_storage_index)
        return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                          scxml_syntax_node_location(node),
                          "SCXML inline content storage mismatched admission");
    content->bytes = build->effect_storage + build->effect_storage_index;
    if (scxml_syntax_serialize_children(
            node, (char *)content->bytes, retained,
            build->limits.max_name_bytes, &actual) != TURBO_XML_OK ||
        actual != content->byte_count)
        return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                          scxml_syntax_node_location(node),
                          "SCXML inline content changed during emission");
    build->effect_storage_index += retained;
    return SCXML_OK;
}

scxml_status scxml_emit_compile_cmeta_payload_token(
    scxml_build *build, turbo_xml_string_view source,
    turbo_xml_location location, const char *subject,
    scxml_expr_program *program) {
    scxml_expr_diagnostic diagnostic = {0};
    scxml_location compiled_location = {0};
    scxml_expr_status expression_status;
    scxml_status status;
    char message[SCXML_DIAGNOSTIC_CAPACITY];
    if (source.size > build->expression_limits.max_source_bytes)
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED, location,
                          "CMeta payload location byte limit exceeded");
    expression_status = scxml_location_compile(
        &compiled_location, source.data, source.size, build->cmeta_root,
        build->expression_limits.max_path_depth, false,
        &diagnostic);
    if (expression_status == SCXML_EXPR_OK) {
        diagnostic = (scxml_expr_diagnostic){0};
        expression_status = scxml_expr_compile_value(
            program, source.data, source.size, build->cmeta_root,
            resolve_cmeta_condition_state, build,
            &build->expression_limits, &diagnostic);
    }
    if (expression_status == SCXML_EXPR_OK)
        return SCXML_OK;
    status = expression_status == SCXML_EXPR_LIMIT_EXCEEDED
        ? SCXML_LIMIT_EXCEEDED
        : expression_status == SCXML_EXPR_ALLOCATION_FAILED
            ? SCXML_ALLOCATION_FAILED
            : SCXML_INVALID_STRUCTURE;
    (void)snprintf(
        message, sizeof(message), "CMeta %s byte %zu: %s", subject,
        diagnostic.byte_offset,
        diagnostic.message[0] != '\0'
            ? diagnostic.message : "expression compilation failed");
    return scxml_analyze_fail(build, status, location, message);
}

scxml_status scxml_emit_compile_cmeta_content_expression(
    scxml_build *build, scxml_syntax_attribute expression,
    const char *subject, scxml_content_descriptor *content,
    scxml_expr_program *scalar_program) {
    const turbo_xml_string_view source =
        scxml_syntax_attribute_value(expression);
    scxml_expr_diagnostic diagnostic = {0};
    scxml_location location = {0};
    if (scxml_location_compile(
            &location, source.data, source.size, build->cmeta_root,
            build->expression_limits.max_path_depth, false,
            &diagnostic) == SCXML_EXPR_OK &&
        location.value != NULL &&
        !scxml_analyze_cmeta_content_kind_is_scalar(location.value->kind)) {
        content->kind = SCXML_CONTENT_CMETA;
        content->location = location;
        return SCXML_OK;
    }
    {
        const scxml_status status = scxml_emit_compile_cmeta_value_program(
            build, expression, subject, scalar_program,
            SCXML_EXPR_VALUE_INVALID);
        if (status == SCXML_OK)
            content->kind = SCXML_CONTENT_SCALAR;
        return status;
    }
}

scxml_status scxml_emit_compile_cmeta_owned_string_location(
    scxml_build *build, scxml_syntax_attribute attribute,
    const char *subject, scxml_location *out) {
    const turbo_xml_string_view source = scxml_syntax_attribute_value(attribute);
    scxml_expr_diagnostic diagnostic = {0};
    scxml_expr_status expression_status;
    scxml_status status;
    const cmeta_data_buffer_ops *ops;
    char message[SCXML_DIAGNOSTIC_CAPACITY];
    if (source.size > build->expression_limits.max_source_bytes)
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_attribute_location(attribute),
                          "SCXML location byte limit exceeded");
    expression_status = scxml_location_compile(
        out, source.data, source.size, build->cmeta_root,
        build->expression_limits.max_path_depth, true, &diagnostic);
    ops = expression_status == SCXML_EXPR_OK &&
                  out->value->kind == CMETA_DATA_STRING
        ? cmeta_data_buffer_ops_of(out->value) : NULL;
    if (expression_status == SCXML_EXPR_OK &&
        (ops == NULL || ops->ownership != CMETA_DATA_BUFFER_OWNED)) {
        expression_status = SCXML_EXPR_TYPE_MISMATCH;
        diagnostic.status = expression_status;
        diagnostic.byte_offset = 0u;
        (void)snprintf(
            diagnostic.message, sizeof(diagnostic.message),
            "%s must name a writable owned CMeta string", subject);
    }
    if (expression_status == SCXML_EXPR_OK)
        return SCXML_OK;
    *out = (scxml_location){0};
    status = expression_status == SCXML_EXPR_LIMIT_EXCEEDED
        ? SCXML_LIMIT_EXCEEDED
        : expression_status == SCXML_EXPR_ALLOCATION_FAILED
            ? SCXML_ALLOCATION_FAILED
            : SCXML_INVALID_STRUCTURE;
    (void)snprintf(
        message, sizeof(message), "CMeta %s byte %zu: %s", subject,
        diagnostic.byte_offset,
        diagnostic.message[0] != '\0'
            ? diagnostic.message : "location compilation failed");
    return scxml_analyze_fail(build, status,
                      scxml_syntax_attribute_location(attribute), message);
}

static scxml_status emit_send_step(scxml_build *build,
                                         scxml_syntax_node node) {
    const scxml_syntax_attribute event_attribute = scxml_analyze_find_attribute(node, "event");
    const scxml_syntax_attribute event_expr_attribute =
        scxml_analyze_find_attribute(node, "eventexpr");
    const scxml_syntax_attribute target_attribute = scxml_analyze_find_attribute(node, "target");
    const scxml_syntax_attribute target_expr_attribute =
        scxml_analyze_find_attribute(node, "targetexpr");
    const scxml_syntax_attribute type_attribute = scxml_analyze_find_attribute(node, "type");
    const scxml_syntax_attribute type_expr_attribute =
        scxml_analyze_find_attribute(node, "typeexpr");
    const scxml_syntax_attribute id_attribute = scxml_analyze_find_attribute(node, "id");
    const scxml_syntax_attribute delay_attribute = scxml_analyze_find_attribute(node, "delay");
    const scxml_syntax_attribute delay_expr_attribute =
        scxml_analyze_find_attribute(node, "delayexpr");
    const scxml_syntax_attribute idlocation_attribute =
        scxml_analyze_find_attribute(node, "idlocation");
    const scxml_syntax_attribute namelist_attribute =
        scxml_analyze_find_attribute(node, "namelist");
    const scxml_name_ref *event = event_attribute.impl != NULL
        ? scxml_analyze_find_name_ref(build->event_names, build->event_name_count,
                        scxml_syntax_attribute_value(event_attribute))
        : NULL;
    const size_t step = build->step_index;
    const size_t effect = build->effect_index;
    scxml_effect_descriptor *descriptor;
    turbo_xml_string_view target;
    size_t child_index;
    scxml_status status;
    if ((event_attribute.impl != NULL && event == NULL) ||
        step >= build->step_capacity ||
        effect >= build->effect_capacity) {
        return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                          scxml_syntax_node_location(node),
                          "send exceeded admitted descriptor storage");
    }
    descriptor = &build->effects[effect];
    descriptor->kind = SCXML_EFFECT_SEND;
    descriptor->event_id = event != NULL ? (cflow_event_id)event->id : 0u;
    descriptor->payload_first = build->payload_index;
    if (!retain_effect_attribute(
            build, event_attribute, &descriptor->request.send.event,
            &descriptor->request.send.event_size) ||
        !retain_effect_attribute(
            build, target_attribute, &descriptor->request.send.target,
            &descriptor->request.send.target_size) ||
        !retain_effect_attribute(
            build, type_attribute, &descriptor->request.send.type,
            &descriptor->request.send.type_size) ||
        !retain_effect_attribute(
            build, id_attribute, &descriptor->request.send.id,
            &descriptor->request.send.id_size) ||
        (delay_attribute.impl != NULL &&
         !scxml_analyze_parse_delay_ms(scxml_syntax_attribute_value(delay_attribute),
                         &descriptor->request.send.delay_ms))) {
        return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                          scxml_syntax_node_location(node),
                          "send descriptor mismatched admitted storage");
    }
    if (event_expr_attribute.impl != NULL) {
        descriptor->has_event_expr = true;
        status = scxml_emit_compile_cmeta_value_program(
            build, event_expr_attribute, "send eventexpr",
            &descriptor->event_expr,
            SCXML_EXPR_VALUE_STRING);
        if (status != SCXML_OK) return status;
    }
    if (target_expr_attribute.impl != NULL) {
        descriptor->has_target_expr = true;
        status = scxml_emit_compile_cmeta_value_program(
            build, target_expr_attribute, "send targetexpr",
            &descriptor->target_expr,
            SCXML_EXPR_VALUE_STRING);
        if (status != SCXML_OK) return status;
    }
    if (type_expr_attribute.impl != NULL) {
        descriptor->has_type_expr = true;
        status = scxml_emit_compile_cmeta_value_program(
            build, type_expr_attribute, "send typeexpr",
            &descriptor->type_expr,
            SCXML_EXPR_VALUE_STRING);
        if (status != SCXML_OK) return status;
    }
    if (delay_expr_attribute.impl != NULL) {
        scxml_expr_value_kind kind;
        descriptor->has_delay_expr = true;
        status = scxml_emit_compile_cmeta_value_program(
            build, delay_expr_attribute, "send delayexpr",
            &descriptor->delay_expr,
            SCXML_EXPR_VALUE_INVALID);
        if (status != SCXML_OK) return status;
        kind = scxml_expr_program_value_kind(
            &descriptor->delay_expr);
        if (kind != SCXML_EXPR_VALUE_SINT &&
            kind != SCXML_EXPR_VALUE_UINT) {
            scxml_expr_program_destroy(&descriptor->delay_expr);
            descriptor->has_delay_expr = false;
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_attribute_location(
                                  delay_expr_attribute),
                              "send delayexpr must produce an integer millisecond value");
        }
    }
    if (idlocation_attribute.impl != NULL) {
        descriptor->has_id_location = true;
        status = scxml_emit_compile_cmeta_owned_string_location(
            build, idlocation_attribute, "send idlocation",
            &descriptor->id_location);
        if (status != SCXML_OK) return status;
    }
    if (namelist_attribute.impl != NULL) {
        const turbo_xml_string_view namelist =
            scxml_syntax_attribute_value(namelist_attribute);
        size_t cursor = 0u;
        turbo_xml_string_view token;
        while (scxml_analyze_token_next(namelist, &cursor, &token)) {
            scxml_payload_descriptor *payload;
            if (build->payload_index >= build->payload_capacity)
                return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                                  scxml_syntax_attribute_location(
                                      namelist_attribute),
                                  "send payload emission exceeded admission");
            payload = &build->payloads[build->payload_index];
            if (!retain_effect_view(
                    build, token, &payload->name, &payload->name_size))
                return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                                  scxml_syntax_attribute_location(
                                      namelist_attribute),
                                  "send namelist storage mismatched admission");
            status = scxml_emit_compile_cmeta_payload_token(
                build, token, scxml_syntax_attribute_location(
                                  namelist_attribute),
                "send namelist", &payload->expression);
            if (status != SCXML_OK) return status;
            ++build->payload_index;
        }
    }
    for (child_index = 0u;
         child_index < scxml_syntax_node_child_count(node); ++child_index) {
        const scxml_syntax_node child =
            scxml_syntax_node_child_at(node, child_index);
        const scxml_element_kind kind = scxml_analyze_element_kind(child);
        if (scxml_syntax_node_type(child) != TURBO_XML_ELEMENT)
            continue;
        if (kind == SCXML_ELEMENT_PARAM) {
            const scxml_syntax_attribute name = scxml_analyze_find_attribute(child, "name");
            const scxml_syntax_attribute expression =
                scxml_analyze_find_attribute(child, "expr");
            const scxml_syntax_attribute location =
                scxml_analyze_find_attribute(child, "location");
            scxml_payload_descriptor *payload;
            if (build->payload_index >= build->payload_capacity)
                return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                                  scxml_syntax_node_location(child),
                                  "send param emission exceeded admission");
            payload = &build->payloads[build->payload_index];
            if (!retain_effect_view(
                    build, scxml_syntax_attribute_value(name),
                    &payload->name, &payload->name_size))
                return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                                  scxml_syntax_attribute_location(name),
                                  "send param storage mismatched admission");
            if (location.impl != NULL)
                status = scxml_emit_compile_cmeta_payload_token(
                    build, scxml_syntax_attribute_value(location),
                    scxml_syntax_attribute_location(location),
                    "send param location", &payload->expression);
            else
                status = scxml_emit_compile_cmeta_value_program(
                    build, expression, "send param", &payload->expression,
                    SCXML_EXPR_VALUE_INVALID);
            if (status != SCXML_OK) return status;
            ++build->payload_index;
            continue;
        }
        if (kind != SCXML_ELEMENT_CONTENT) continue;
        if (scxml_analyze_find_attribute(child, "expr").impl != NULL) {
            status = scxml_emit_compile_cmeta_content_expression(
                build, scxml_analyze_find_attribute(child, "expr"), "send content",
                &descriptor->content, &descriptor->data_expr);
        } else {
            status = retain_effect_inline_content(
                build, child, &descriptor->content);
        }
        if (status != SCXML_OK) return status;
    }
    descriptor->payload_count =
        build->payload_index - descriptor->payload_first;
    target = target_attribute.impl != NULL
                 ? scxml_syntax_attribute_value(target_attribute)
                 : (turbo_xml_string_view){NULL, 0u};
    descriptor->internal_target =
        scxml_analyze_view_equal_raw(target, "#_internal") ||
        scxml_analyze_view_equal_raw(target, "_internal");
    ++build->effect_index;
    ++build->step_index;
    build->steps[step] = (scxml_step){
        .kind = SCXML_STEP_SEND,
        .next = build->step_index,
        .effect = effect};
    return SCXML_OK;
}

static scxml_status emit_cancel_step(scxml_build *build,
                                           scxml_syntax_node node) {
    const scxml_syntax_attribute sendid_attribute = scxml_analyze_find_attribute(node, "sendid");
    const scxml_syntax_attribute sendid_expr_attribute =
        scxml_analyze_find_attribute(node, "sendidexpr");
    const size_t step = build->step_index;
    const size_t effect = build->effect_index;
    scxml_effect_descriptor *descriptor;
    if (step >= build->step_capacity || effect >= build->effect_capacity) {
        return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                          scxml_syntax_node_location(node),
                          "cancel exceeded admitted descriptor storage");
    }
    descriptor = &build->effects[effect];
    descriptor->kind = SCXML_EFFECT_CANCEL;
    if (!retain_effect_attribute(
            build, sendid_attribute, &descriptor->request.cancel.send_id,
            &descriptor->request.cancel.send_id_size)) {
        return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                          scxml_syntax_node_location(node),
                          "cancel descriptor mismatched admitted storage");
    }
    if (sendid_expr_attribute.impl != NULL) {
        const scxml_status status = scxml_emit_compile_cmeta_value_program(
            build, sendid_expr_attribute, "cancel sendidexpr",
            &descriptor->send_id_expr,
            SCXML_EXPR_VALUE_STRING);
        if (status != SCXML_OK) return status;
        descriptor->has_send_id_expr = true;
    }
    ++build->effect_index;
    ++build->step_index;
    build->steps[step] = (scxml_step){
        .kind = SCXML_STEP_CANCEL,
        .next = build->step_index,
        .effect = effect};
    return SCXML_OK;
}

static scxml_status emit_conditional_branch(
    scxml_build *build, scxml_syntax_node node, scxml_branch *branch) {
    if (build->data_model == SCXML_DATA_MODEL_CMETA) {
        return compile_cmeta_condition_program(
            build, scxml_analyze_find_attribute(node, "cond"), &branch->condition);
    }
    return resolve_condition_state(build, node, &branch->state);
}

static scxml_status emit_conditional_step(
    scxml_build *build, scxml_syntax_node node) {
    const size_t step = build->step_index;
    const size_t branch_first = build->branch_index;
    size_t branch_count;
    size_t current_branch = branch_first;
    size_t index;
    scxml_status status;
    if (!scxml_analyze_checked_add(scxml_analyze_element_child_count(node, SCXML_ELEMENT_ELSEIF),
                     scxml_analyze_element_child_count(node, SCXML_ELEMENT_ELSE),
                     &branch_count) ||
        !scxml_analyze_checked_add(branch_count, 1u, &branch_count) ||
        step >= build->step_capacity ||
        branch_first > build->branch_capacity ||
        branch_count > build->branch_capacity - branch_first) {
        return scxml_analyze_fail(
            build, SCXML_NATIVE_IR_REJECTED,
            scxml_syntax_node_location(node),
            "conditional exceeded admitted storage");
    }
    ++build->step_index;
    build->branch_index += branch_count;
    status = emit_conditional_branch(
        build, node, &build->branches[current_branch]);
    if (status != SCXML_OK) return status;
    build->branches[current_branch].step_begin = build->step_index;
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        const scxml_element_kind kind = scxml_analyze_element_kind(child);
        if (scxml_syntax_node_type(child) != TURBO_XML_ELEMENT) continue;
        if (kind == SCXML_ELEMENT_ELSEIF || kind == SCXML_ELEMENT_ELSE) {
            build->branches[current_branch].step_end = build->step_index;
            ++current_branch;
            build->branches[current_branch].step_begin = build->step_index;
            if (kind == SCXML_ELEMENT_ELSE) {
                build->branches[current_branch].unconditional = true;
            } else {
                status = emit_conditional_branch(
                    build, child, &build->branches[current_branch]);
                if (status != SCXML_OK) return status;
            }
        } else {
            status = emit_executable_node(build, child);
            if (status != SCXML_OK) return status;
        }
    }
    if (current_branch + 1u != branch_first + branch_count) {
        return scxml_analyze_fail(
            build, SCXML_NATIVE_IR_REJECTED,
            scxml_syntax_node_location(node),
            "conditional branch emission count mismatched admission");
    }
    build->branches[current_branch].step_end = build->step_index;
    build->steps[step] = (scxml_step){
        SCXML_STEP_IF, build->step_index, 0u, branch_first, branch_count};
    return SCXML_OK;
}

static scxml_status emit_log_step(scxml_build *build,
                                        scxml_syntax_node node) {
    const scxml_syntax_attribute label_attribute = scxml_analyze_find_attribute(node, "label");
    const turbo_xml_string_view label =
        label_attribute.impl != NULL
            ? scxml_syntax_attribute_value(label_attribute)
            : (turbo_xml_string_view){NULL, 0u};
    const size_t step = build->step_index;
    size_t retained_bytes;
    char *stored_label;
    if (!scxml_analyze_checked_add(label.size, 1u, &retained_bytes) ||
        step >= build->step_capacity ||
        build->log_storage_index > build->log_storage_capacity ||
        retained_bytes >
            build->log_storage_capacity - build->log_storage_index) {
        return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                          scxml_syntax_node_location(node),
                          "log exceeded admitted storage");
    }
    stored_label = build->log_storage + build->log_storage_index;
    if (label.size != 0u)
        memcpy(stored_label, label.data, label.size);
    stored_label[label.size] = '\0';
    build->log_storage_index += retained_bytes;
    ++build->step_index;
    build->steps[step] = (scxml_step){
        .kind = SCXML_STEP_LOG,
        .next = build->step_index,
        .label = stored_label};
    return SCXML_OK;
}

static scxml_status emit_assign_step(scxml_build *build,
                                           scxml_syntax_node node) {
    const scxml_syntax_attribute location_attribute =
        scxml_analyze_find_attribute(node, "location");
    const scxml_syntax_attribute expression_attribute =
        scxml_analyze_find_attribute(node, "expr");
    const size_t step = build->step_index;
    const size_t assignment = build->assignment_index;
    scxml_expr_diagnostic assignment_diagnostic = {0};
    scxml_expr_status assignment_status;
    scxml_status status;
    char *location = NULL;
    size_t location_size = 0u;
    char *expression = NULL;
    size_t expression_size = 0u;
    char message[SCXML_DIAGNOSTIC_CAPACITY];
    if (step >= build->step_capacity ||
        assignment >= build->assignment_capacity)
        return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                          scxml_syntax_node_location(node),
                          "assign exceeded admitted descriptor storage");
    status = decode_cmeta_attribute_source(
        build, location_attribute, "assignment location",
        &location, &location_size);
    if (status != SCXML_OK) return status;
    status = decode_cmeta_attribute_source(
        build, expression_attribute, "assignment expression",
        &expression, &expression_size);
    if (status != SCXML_OK) {
        free(location);
        return status;
    }
    assignment_status = scxml_assign_compile(
        &build->assignments[assignment], location, location_size,
        expression, expression_size, build->cmeta_root,
        resolve_cmeta_condition_state, build,
        &build->expression_limits, SCXML_ASSIGN_LOCATION_RUNTIME,
        &assignment_diagnostic);
    free(location);
    free(expression);
    if (assignment_status != SCXML_EXPR_OK) {
        const scxml_status public_status =
            assignment_status == SCXML_EXPR_LIMIT_EXCEEDED
                ? SCXML_LIMIT_EXCEEDED
                : assignment_status ==
                          SCXML_EXPR_ALLOCATION_FAILED
                      ? SCXML_ALLOCATION_FAILED
                      : SCXML_INVALID_STRUCTURE;
        (void)snprintf(
            message, sizeof(message), "CMeta assignment byte %zu: %s",
            assignment_diagnostic.byte_offset,
            assignment_diagnostic.message[0] != '\0'
                ? assignment_diagnostic.message
                : "assignment compilation failed");
        return scxml_analyze_fail(build, public_status,
                          scxml_syntax_node_location(node), message);
    }
    ++build->assignment_index;
    ++build->step_index;
    build->steps[step] = (scxml_step){
        .kind = SCXML_STEP_ASSIGN,
        .next = build->step_index,
        .assignment = assignment};
    return SCXML_OK;
}

static scxml_status emit_data_initializer(
    scxml_build *build, scxml_syntax_node data) {
    const scxml_syntax_attribute location = scxml_analyze_find_attribute(data, "id");
    const scxml_syntax_attribute expression = scxml_analyze_find_attribute(data, "expr");
    scxml_expr_diagnostic diagnostic = {0};
    scxml_expr_status expression_status;
    scxml_status status;
    char *location_source = NULL;
    size_t location_size = 0u;
    char *expression_source = NULL;
    size_t expression_size = 0u;
    char message[SCXML_DIAGNOSTIC_CAPACITY];
    if (build->assignment_index >= build->assignment_capacity)
        return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                          scxml_syntax_node_location(data),
                          "data initializer exceeded admitted storage");
    status = decode_cmeta_attribute_source(
        build, location, "data id", &location_source, &location_size);
    if (status != SCXML_OK) return status;
    status = decode_cmeta_attribute_source(
        build, expression, "data expr", &expression_source,
        &expression_size);
    if (status != SCXML_OK) {
        free(location_source);
        return status;
    }
    expression_status = scxml_assign_compile(
        &build->assignments[build->assignment_index],
        location_source, location_size, expression_source, expression_size,
        build->cmeta_root, resolve_cmeta_condition_state, build,
        &build->expression_limits, SCXML_ASSIGN_LOCATION_STRICT, &diagnostic);
    free(location_source);
    free(expression_source);
    if (expression_status != SCXML_EXPR_OK) {
        const scxml_status public_status =
            expression_status == SCXML_EXPR_LIMIT_EXCEEDED
                ? SCXML_LIMIT_EXCEEDED
                : expression_status ==
                          SCXML_EXPR_ALLOCATION_FAILED
                      ? SCXML_ALLOCATION_FAILED
                      : SCXML_INVALID_STRUCTURE;
        (void)snprintf(
            message, sizeof(message), "CMeta data initializer byte %zu: %s",
            diagnostic.byte_offset,
            diagnostic.message[0] != '\0'
                ? diagnostic.message : "initializer compilation failed");
        return scxml_analyze_fail(build, public_status,
                          scxml_syntax_node_location(data), message);
    }
    ++build->assignment_index;
    return SCXML_OK;
}

static scxml_status emit_datamodel_assignments(
    scxml_build *build, scxml_syntax_node datamodel,
    size_t *out_first, size_t *out_count) {
    const size_t first = build->assignment_index;
    size_t index;
    for (index = 0u; index < scxml_syntax_node_child_count(datamodel); ++index) {
        const scxml_syntax_node data =
            scxml_syntax_node_child_at(datamodel, index);
        scxml_status status;
        if (scxml_syntax_node_type(data) != TURBO_XML_ELEMENT ||
            scxml_analyze_element_kind(data) != SCXML_ELEMENT_DATA)
            continue;
        status = emit_data_initializer(build, data);
        if (status != SCXML_OK) return status;
    }
    *out_first = first;
    *out_count = build->assignment_index - first;
    return SCXML_OK;
}

scxml_status scxml_emit_data_initializers(
    scxml_build *build, scxml_syntax_node node, bool is_root) {
    size_t index;
    if (build->late_binding) return SCXML_OK;
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        const scxml_element_kind kind = scxml_analyze_element_kind(child);
        if (scxml_syntax_node_type(child) != TURBO_XML_ELEMENT) continue;
        if (kind == SCXML_ELEMENT_DATAMODEL) {
            size_t first;
            size_t count;
            const scxml_status status = emit_datamodel_assignments(
                build, child, &first, &count);
            if (status != SCXML_OK) return status;
            if (is_root) {
                build->top_level_data_initializer_first = first;
                build->top_level_data_initializer_count = count;
            }
        } else if (scxml_analyze_is_state_element(kind) ||
                   kind == SCXML_ELEMENT_INITIAL ||
                   kind == SCXML_ELEMENT_HISTORY) {
            const scxml_status status =
                scxml_emit_data_initializers(build, child, false);
            if (status != SCXML_OK) return status;
        }
    }
    return SCXML_OK;
}

static const cmeta_data_field_desc *find_done_data_field(
    const cmeta_data_desc *root, const char *name, size_t name_size) {
    const cmeta_data_struct_shape *shape;
    size_t index;
    if (root == NULL || root->kind != CMETA_DATA_STRUCT ||
        root->shape == NULL || name == NULL || name_size == 0u)
        return NULL;
    shape = (const cmeta_data_struct_shape *)root->shape;
    for (index = 0u; index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &shape->fields[index];
        if (field->name != NULL && strlen(field->name) == name_size &&
            memcmp(field->name, name, name_size) == 0)
            return field;
    }
    return NULL;
}

static scxml_status emit_done_data_param(
    scxml_build *build, scxml_syntax_node param,
    scxml_done_data_descriptor *descriptor, size_t field_index) {
    const scxml_syntax_attribute name =
        scxml_analyze_find_attribute(param, "name");
    const scxml_syntax_attribute expression =
        scxml_analyze_find_attribute(param, "expr");
    const scxml_syntax_attribute location =
        scxml_analyze_find_attribute(param, "location");
    const scxml_syntax_attribute source_attribute =
        location.impl != NULL ? location : expression;
    const cmeta_data_field_desc *field;
    scxml_expr_diagnostic diagnostic = {0};
    scxml_expr_status expression_status;
    scxml_status status;
    char *name_source = NULL;
    size_t name_size = 0u;
    char *value_source = NULL;
    size_t value_size = 0u;
    char message[SCXML_DIAGNOSTIC_CAPACITY];
    if (build->assignment_index >= build->assignment_capacity)
        return scxml_analyze_fail(
            build, SCXML_NATIVE_IR_REJECTED,
            scxml_syntax_node_location(param),
            "donedata param exceeded admitted assignment storage");
    status = decode_cmeta_attribute_source(
        build, name, "donedata param name", &name_source, &name_size);
    if (status != SCXML_OK) return status;
    field = find_done_data_field(build->cmeta_root, name_source, name_size);
    if (field == NULL) {
        free(name_source);
        return scxml_analyze_fail(
            build, SCXML_INVALID_STRUCTURE,
            scxml_syntax_attribute_location(name),
            "donedata param name is not a top-level CMeta field");
    }
    status = decode_cmeta_attribute_source(
        build, source_attribute,
        location.impl != NULL ? "donedata param location"
                              : "donedata param expr",
        &value_source, &value_size);
    if (status != SCXML_OK) {
        free(name_source);
        return status;
    }
    expression_status = scxml_assign_compile(
        &build->assignments[build->assignment_index],
        name_source, name_size, value_source, value_size,
        build->cmeta_root, resolve_cmeta_condition_state, build,
        &build->expression_limits, SCXML_ASSIGN_LOCATION_STRICT, &diagnostic);
    free(name_source);
    free(value_source);
    if (expression_status != SCXML_EXPR_OK) {
        const scxml_status public_status =
            expression_status == SCXML_EXPR_LIMIT_EXCEEDED
                ? SCXML_LIMIT_EXCEEDED
                : expression_status == SCXML_EXPR_ALLOCATION_FAILED
                      ? SCXML_ALLOCATION_FAILED
                      : SCXML_INVALID_STRUCTURE;
        (void)snprintf(
            message, sizeof(message), "CMeta donedata param byte %zu: %s",
            diagnostic.byte_offset,
            diagnostic.message[0] != '\0'
                ? diagnostic.message : "param compilation failed");
        return scxml_analyze_fail(
            build, public_status, scxml_syntax_node_location(param), message);
    }
    descriptor->fields[field_index] = *field;
    ++build->assignment_index;
    return SCXML_OK;
}

static scxml_status initialize_done_data_schema(
    scxml_build *build, scxml_done_data_descriptor *descriptor,
    size_t field_count) {
    static const char display_name[] = "SCXML completion data";
    const cmeta_data_struct_shape *root_shape =
        (const cmeta_data_struct_shape *)build->cmeta_root->shape;
    descriptor->fields = (cmeta_data_field_desc *)scxml_emit_allocate_rows(
        field_count, sizeof(*descriptor->fields));
    if (descriptor->fields == NULL)
        return scxml_analyze_fail(
            build, SCXML_ALLOCATION_FAILED, (turbo_xml_location){0},
            "unable to allocate donedata schema storage");
    descriptor->shape = (cmeta_data_struct_shape){
        .layout = root_shape->layout,
        .fields = descriptor->fields,
        .field_count = field_count};
    descriptor->schema = (cmeta_data_desc){
        .struct_size = sizeof(cmeta_data_desc),
        .abi_version = CMETA_DATA_DESC_ABI_VERSION,
        .stable_id = NULL,
        .display_name = display_name,
        .kind = CMETA_DATA_STRUCT,
        .storage_type = build->cmeta_root->storage_type,
        .shape = &descriptor->shape};
    return SCXML_OK;
}

static scxml_status finalize_done_data_schema_id(
    scxml_build *build, scxml_done_data_descriptor *descriptor) {
    static const char suffix_prefix[] = "#scxml-donedata-";
    enum { SIZE_DECIMAL_CAPACITY = 3u * sizeof(size_t) + 1u };
    char suffix[sizeof(suffix_prefix) + 3u * sizeof(cflow_machine_state_id)];
    const int suffix_size = snprintf(
        suffix, sizeof(suffix), "%s%llu", suffix_prefix,
        (unsigned long long)descriptor->final_state);
    const size_t root_size = strlen(build->cmeta_root->stable_id);
    size_t stable_id_size, field_index, offset;

    if (suffix_size < 0 || (size_t)suffix_size >= sizeof(suffix) ||
        !scxml_analyze_checked_add(root_size, (size_t)suffix_size,
                                   &stable_id_size))
        return scxml_analyze_fail(
            build, SCXML_LIMIT_EXCEEDED, (turbo_xml_location){0},
            "donedata schema identifier exceeds the size bound");
    for (field_index = 0u; field_index < descriptor->shape.field_count;
         ++field_index) {
        const char *name = descriptor->fields[field_index].name;
        const size_t field_size = name != NULL ? strlen(name) : 0u;
        char decimal[SIZE_DECIMAL_CAPACITY];
        const int decimal_size = name != NULL
            ? snprintf(decimal, sizeof(decimal), "%zu", field_size)
            : -1;
        size_t encoded_size;
        if (decimal_size < 0 || (size_t)decimal_size >= sizeof(decimal) ||
            !scxml_analyze_checked_add(
                (size_t)decimal_size, 2u, &encoded_size) ||
            !scxml_analyze_checked_add(
                encoded_size, field_size, &encoded_size) ||
            !scxml_analyze_checked_add(
                stable_id_size, encoded_size, &stable_id_size))
            return scxml_analyze_fail(
                build, SCXML_LIMIT_EXCEEDED, (turbo_xml_location){0},
                "donedata schema identifier exceeds the size bound");
    }
    if (!scxml_analyze_checked_add(stable_id_size, 1u, &stable_id_size))
        return scxml_analyze_fail(
            build, SCXML_LIMIT_EXCEEDED, (turbo_xml_location){0},
            "donedata schema identifier exceeds the size bound");
    descriptor->schema_stable_id = (char *)malloc(stable_id_size);
    if (descriptor->schema_stable_id == NULL)
        return scxml_analyze_fail(
            build, SCXML_ALLOCATION_FAILED, (turbo_xml_location){0},
            "unable to allocate donedata schema identifier");
    memcpy(descriptor->schema_stable_id,
           build->cmeta_root->stable_id, root_size);
    memcpy(descriptor->schema_stable_id + root_size,
           suffix, (size_t)suffix_size);
    offset = root_size + (size_t)suffix_size;
    for (field_index = 0u; field_index < descriptor->shape.field_count;
         ++field_index) {
        const char *name = descriptor->fields[field_index].name;
        const size_t field_size = strlen(name);
        const int written = snprintf(
            descriptor->schema_stable_id + offset,
            stable_id_size - offset, ":%zu:", field_size);
        if (written < 0 ||
            (size_t)written >= stable_id_size - offset)
            return scxml_analyze_fail(
                build, SCXML_LIMIT_EXCEEDED, (turbo_xml_location){0},
                "donedata schema identifier emission exceeded its bound");
        offset += (size_t)written;
        memcpy(descriptor->schema_stable_id + offset, name, field_size);
        offset += field_size;
    }
    descriptor->schema_stable_id[offset] = '\0';
    descriptor->schema.stable_id = descriptor->schema_stable_id;
    return SCXML_OK;
}

scxml_status scxml_emit_done_data(
    scxml_build *build, scxml_syntax_node node, size_t node_count,
    cflow_machine_state_id parent) {
    const scxml_element_kind kind = scxml_analyze_element_kind(node);
    const cflow_machine_state_id current = scxml_analyze_node_id(build, node, node_count);
    size_t index;
    if (kind == SCXML_ELEMENT_FINAL) {
        for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
            const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
            size_t content_index;
            size_t param_count = 0u;
            size_t param_index = 0u;
            scxml_done_data_descriptor *descriptor;
            scxml_status status;
            if (scxml_syntax_node_type(child) != TURBO_XML_ELEMENT ||
                scxml_analyze_element_kind(child) != SCXML_ELEMENT_DONEDATA)
                continue;
            if (build->done_data_index >= build->done_data_capacity)
                return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                                  scxml_syntax_node_location(child),
                                  "donedata emission exceeded declarations");
            descriptor = &build->done_data[build->done_data_index];
            descriptor->parent = parent;
            descriptor->final_state = current;
            descriptor->assignment_first = build->assignment_index;
            for (content_index = 0u;
                 content_index < scxml_syntax_node_child_count(child);
                 ++content_index) {
                const scxml_syntax_node content =
                    scxml_syntax_node_child_at(child, content_index);
                if (scxml_syntax_node_type(content) == TURBO_XML_ELEMENT &&
                    scxml_analyze_element_kind(content) == SCXML_ELEMENT_PARAM)
                    ++param_count;
            }
            if (param_count != 0u) {
                status = initialize_done_data_schema(
                    build, descriptor, param_count);
                if (status != SCXML_OK) return status;
            }
            for (content_index = 0u;
                 content_index < scxml_syntax_node_child_count(child);
                 ++content_index) {
                const scxml_syntax_node content =
                    scxml_syntax_node_child_at(child, content_index);
                const scxml_element_kind content_kind =
                    scxml_analyze_element_kind(content);
                if (scxml_syntax_node_type(content) != TURBO_XML_ELEMENT)
                    continue;
                if (content_kind == SCXML_ELEMENT_PARAM) {
                    status = emit_done_data_param(
                        build, content, descriptor, param_index++);
                } else if (content_kind == SCXML_ELEMENT_CONTENT &&
                           scxml_analyze_find_attribute(content, "expr").impl != NULL) {
                    status = scxml_emit_compile_cmeta_content_expression(
                        build, scxml_analyze_find_attribute(content, "expr"),
                        "donedata content", &descriptor->content,
                        &descriptor->expression);
                } else if (content_kind == SCXML_ELEMENT_CONTENT) {
                    status = retain_effect_inline_content(
                        build, content, &descriptor->content);
                } else continue;
                if (status != SCXML_OK) return status;
            }
            descriptor->assignment_count =
                build->assignment_index - descriptor->assignment_first;
            if (param_count != 0u &&
                (status = finalize_done_data_schema_id(
                     build, descriptor)) != SCXML_OK)
                return status;
            if (param_count != 0u &&
                !cmeta_data_desc_valid(&descriptor->schema))
                return scxml_analyze_fail(
                    build, SCXML_NATIVE_IR_REJECTED,
                    scxml_syntax_node_location(child),
                    "donedata subset schema failed validation");
            ++build->done_data_index;
        }
    }
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        if (scxml_syntax_node_type(child) == TURBO_XML_ELEMENT &&
            scxml_analyze_is_state_element(scxml_analyze_element_kind(child))) {
            scxml_status status = scxml_emit_done_data(
                build, child, node_count, current);
            if (status != SCXML_OK) return status;
        }
    }
    return SCXML_OK;
}

static scxml_status emit_foreach_step(scxml_build *build,
                                            scxml_syntax_node node) {
    const scxml_syntax_attribute array_attribute = scxml_analyze_find_attribute(node, "array");
    const scxml_syntax_attribute item_attribute = scxml_analyze_find_attribute(node, "item");
    const scxml_syntax_attribute index_attribute = scxml_analyze_find_attribute(node, "index");
    const size_t step = build->step_index;
    const size_t foreach_index = build->foreach_index;
    scxml_foreach_descriptor *descriptor;
    scxml_expr_diagnostic foreach_diagnostic = {0};
    scxml_expr_status foreach_status;
    char *array = NULL;
    size_t array_size = 0u;
    char *item = NULL;
    size_t item_size = 0u;
    char *index = NULL;
    size_t index_size = 0u;
    size_t child_index;
    scxml_status status;
    char message[SCXML_DIAGNOSTIC_CAPACITY];
    if (step >= build->step_capacity ||
        foreach_index >= build->foreach_capacity)
        return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                          scxml_syntax_node_location(node),
                          "foreach exceeded admitted descriptor storage");
    status = decode_cmeta_attribute_source(
        build, array_attribute, "foreach array", &array, &array_size);
    if (status != SCXML_OK) return status;
    status = decode_cmeta_attribute_source(
        build, item_attribute, "foreach item", &item, &item_size);
    if (status != SCXML_OK) {
        free(array);
        return status;
    }
    if (index_attribute.impl != NULL) {
        status = decode_cmeta_attribute_source(
            build, index_attribute, "foreach index", &index, &index_size);
        if (status != SCXML_OK) {
            free(array);
            free(item);
            return status;
        }
    }
    descriptor = &build->foreach_descriptors[foreach_index];
    foreach_status = scxml_foreach_compile(
        &descriptor->program, array, array_size, item, item_size,
        index, index_size, build->cmeta_root,
        build->expression_limits.max_path_depth,
        build->max_iterations, &foreach_diagnostic);
    free(array);
    free(item);
    free(index);
    if (foreach_status != SCXML_EXPR_OK) {
        const scxml_status public_status =
            foreach_status == SCXML_EXPR_LIMIT_EXCEEDED
                ? SCXML_LIMIT_EXCEEDED
                : foreach_status ==
                          SCXML_EXPR_ALLOCATION_FAILED
                      ? SCXML_ALLOCATION_FAILED
                      : SCXML_INVALID_STRUCTURE;
        (void)snprintf(
            message, sizeof(message), "CMeta foreach byte %zu: %s",
            foreach_diagnostic.byte_offset,
            foreach_diagnostic.message[0] != '\0'
                ? foreach_diagnostic.message
                : "foreach compilation failed");
        return scxml_analyze_fail(build, public_status,
                          scxml_syntax_node_location(node), message);
    }
    ++build->step_index;
    ++build->foreach_index;
    descriptor->step_begin = build->step_index;
    for (child_index = 0u;
         child_index < scxml_syntax_node_child_count(node); ++child_index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, child_index);
        if (scxml_syntax_node_type(child) != TURBO_XML_ELEMENT) continue;
        status = emit_executable_node(build, child);
        if (status != SCXML_OK) return status;
    }
    descriptor->step_end = build->step_index;
    build->steps[step] = (scxml_step){
        .kind = SCXML_STEP_FOREACH,
        .next = build->step_index,
        .foreach_descriptor = foreach_index};
    return SCXML_OK;
}

static scxml_status emit_executable_node(
    scxml_build *build, scxml_syntax_node node) {
    const scxml_element_kind kind = scxml_analyze_element_kind(node);
    if (kind == SCXML_ELEMENT_RAISE) return emit_raise_step(build, node);
    if (kind == SCXML_ELEMENT_SEND) return emit_send_step(build, node);
    if (kind == SCXML_ELEMENT_CANCEL) return emit_cancel_step(build, node);
    if (kind == SCXML_ELEMENT_LOG) return emit_log_step(build, node);
    if (kind == SCXML_ELEMENT_ASSIGN) return emit_assign_step(build, node);
    if (kind == SCXML_ELEMENT_FOREACH) return emit_foreach_step(build, node);
    if (kind == SCXML_ELEMENT_IF) return emit_conditional_step(build, node);
    return scxml_analyze_fail(
        build, SCXML_NATIVE_IR_REJECTED,
        scxml_syntax_node_location(node),
        "admitted executable element could not be emitted");
}

static scxml_status emit_executable_block(
    scxml_build *build, scxml_syntax_node node,
    cflow_statechart_executable_id *out_executable) {
    const size_t block_index = build->block_index;
    const size_t executable_index = build->executable_index;
    const size_t first_step = build->step_index;
    const size_t first_log_byte = build->log_storage_index;
    const size_t first_effect = build->effect_index;
    const cflow_statechart_executable_id executable =
        (cflow_statechart_executable_id)(executable_index + 1u);
    size_t index;
    *out_executable = 0u;
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        scxml_status status;
        if (scxml_syntax_node_type(child) != TURBO_XML_ELEMENT)
            continue;
        status = emit_executable_node(build, child);
        if (status != SCXML_OK)
            return status;
    }
    if (build->step_index == first_step) return SCXML_OK;
    build->blocks[block_index] = (scxml_block){
        .state_type = build->data_model == SCXML_DATA_MODEL_CMETA
                          ? build->cmeta_root->storage_type
                          : &cmeta_type_bool,
        .steps = build->steps,
        .branches = build->branches,
        .effects = build->effects,
        .assignments = build->assignments,
        .payloads = build->payloads,
        .foreach_descriptors = build->foreach_descriptors,
        .invocations = build->invocations,
        .step_begin = first_step,
        .step_end = build->step_index,
        .step_storage_count = build->step_capacity,
        .branch_storage_count = build->branch_capacity,
        .effect_storage_count = build->effect_capacity,
        .assignment_storage_count = build->assignment_capacity,
        .payload_storage_count = build->payload_capacity,
        .foreach_storage_count = build->foreach_capacity,
        .invocation_storage_count = build->invocation_capacity,
        .execution_error_event = build->execution_error_event,
        .max_conditional_depth = build->max_conditional_depth};
    build->executables[executable_index] = (cflow_statechart_executable){
        executable,
        build->data_model == SCXML_DATA_MODEL_CMETA
            ? build->cmeta_root->storage_type
            : &cmeta_type_bool,
        CMETA_EFFECT_STATEFUL | CMETA_EFFECT_MAY_FAIL |
            (build->log_storage_index != first_log_byte ||
             build->effect_index != first_effect
                 ? CMETA_EFFECT_IO : CMETA_EFFECT_PURE),
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS};
    build->bindings[executable_index] = (cflow_statechart_executable_binding){
        .id = executable,
        .user = &build->blocks[block_index],
        .contextual_fn = scxml_runtime_execute_block};
    ++build->executable_index;
    ++build->block_index;
    *out_executable = executable;
    return SCXML_OK;
}

static scxml_status emit_invoke_lifecycle_block(
    scxml_build *build, size_t invocation, bool enter,
    cflow_statechart_executable_id *out_executable) {
    const size_t block_index = build->block_index;
    const size_t executable_index = build->executable_index;
    const size_t step_index = build->step_index;
    const cflow_statechart_executable_id executable =
        (cflow_statechart_executable_id)(executable_index + 1u);
    if (invocation >= build->invocation_capacity ||
        step_index >= build->step_capacity) {
        return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                          (turbo_xml_location){0u, 0u, 0u},
                          "invoke lifecycle block exceeded admitted storage");
    }
    ++build->step_index;
    build->steps[step_index] = (scxml_step){
        .kind = enter ? SCXML_STEP_INVOKE_ENTER : SCXML_STEP_INVOKE_EXIT,
        .next = build->step_index,
        .invocation = invocation};
    build->blocks[block_index] = (scxml_block){
        .state_type = build->data_model == SCXML_DATA_MODEL_CMETA
                          ? build->cmeta_root->storage_type
                          : &cmeta_type_bool,
        .steps = build->steps,
        .branches = build->branches,
        .effects = build->effects,
        .assignments = build->assignments,
        .payloads = build->payloads,
        .foreach_descriptors = build->foreach_descriptors,
        .invocations = build->invocations,
        .step_begin = step_index,
        .step_end = build->step_index,
        .step_storage_count = build->step_capacity,
        .branch_storage_count = build->branch_capacity,
        .effect_storage_count = build->effect_capacity,
        .assignment_storage_count = build->assignment_capacity,
        .payload_storage_count = build->payload_capacity,
        .foreach_storage_count = build->foreach_capacity,
        .invocation_storage_count = build->invocation_capacity,
        .execution_error_event = build->execution_error_event,
        .max_conditional_depth = build->max_conditional_depth};
    build->executables[executable_index] = (cflow_statechart_executable){
        executable,
        build->data_model == SCXML_DATA_MODEL_CMETA
            ? build->cmeta_root->storage_type
            : &cmeta_type_bool,
        CMETA_EFFECT_STATEFUL | CMETA_EFFECT_MAY_FAIL | CMETA_EFFECT_IO,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS};
    build->bindings[executable_index] = (cflow_statechart_executable_binding){
        .id = executable,
        .user = &build->blocks[block_index],
        .contextual_fn = scxml_runtime_execute_block};
    ++build->executable_index;
    ++build->block_index;
    *out_executable = executable;
    return SCXML_OK;
}

static scxml_status emit_finalize_block(
    scxml_build *build, scxml_syntax_node node,
    const scxml_block **out_block) {
    const size_t block_index = build->block_index;
    const size_t first_step = build->step_index;
    size_t index;
    *out_block = NULL;
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        scxml_status status;
        if (scxml_syntax_node_type(child) != TURBO_XML_ELEMENT) continue;
        status = emit_executable_node(build, child);
        if (status != SCXML_OK) return status;
    }
    if (build->step_index == first_step) return SCXML_OK;
    build->blocks[block_index] = (scxml_block){
        .steps = build->steps,
        .branches = build->branches,
        .effects = build->effects,
        .assignments = build->assignments,
        .payloads = build->payloads,
        .foreach_descriptors = build->foreach_descriptors,
        .invocations = build->invocations,
        .step_begin = first_step,
        .step_end = build->step_index,
        .step_storage_count = build->step_capacity,
        .branch_storage_count = build->branch_capacity,
        .effect_storage_count = build->effect_capacity,
        .assignment_storage_count = build->assignment_capacity,
        .payload_storage_count = build->payload_capacity,
        .foreach_storage_count = build->foreach_capacity,
        .invocation_storage_count = build->invocation_capacity,
        .execution_error_event = build->execution_error_event,
        .max_conditional_depth = build->max_conditional_depth};
    *out_block = &build->blocks[block_index];
    ++build->block_index;
    return SCXML_OK;
}

static scxml_status emit_late_initializer_block(
    scxml_build *build, scxml_syntax_node datamodel,
    bool top_level,
    cflow_statechart_executable_id *out_executable) {
    const size_t block_index = build->block_index;
    const size_t executable_index = build->executable_index;
    const size_t step_index = build->step_index;
    const size_t initializer_index = build->late_initializer_index;
    const cflow_statechart_executable_id executable =
        (cflow_statechart_executable_id)(executable_index + 1u);
    size_t assignment_first;
    size_t assignment_count;
    scxml_status status;
    *out_executable = 0u;
    status = emit_datamodel_assignments(
        build, datamodel, &assignment_first, &assignment_count);
    if (status != SCXML_OK || assignment_count == 0u) return status;
    if (top_level) {
        build->top_level_data_initializer_first = assignment_first;
        build->top_level_data_initializer_count = assignment_count;
    }
    if (build->step_index >= build->step_capacity) {
        return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                          scxml_syntax_node_location(datamodel),
                          "late initializer exceeded admitted storage");
    }
    build->steps[step_index] = (scxml_step){
        .kind = SCXML_STEP_LATE_INITIALIZE,
        .next = step_index + 1u,
        .assignment = assignment_first,
        .assignment_count = assignment_count,
        .late_initializer = initializer_index};
    ++build->step_index;
    build->blocks[block_index] = (scxml_block){
        .state_type = build->cmeta_root->storage_type,
        .steps = build->steps,
        .branches = build->branches,
        .effects = build->effects,
        .assignments = build->assignments,
        .payloads = build->payloads,
        .foreach_descriptors = build->foreach_descriptors,
        .invocations = build->invocations,
        .step_begin = step_index,
        .step_end = build->step_index,
        .step_storage_count = build->step_capacity,
        .branch_storage_count = build->branch_capacity,
        .effect_storage_count = build->effect_capacity,
        .assignment_storage_count = build->assignment_capacity,
        .payload_storage_count = build->payload_capacity,
        .foreach_storage_count = build->foreach_capacity,
        .invocation_storage_count = build->invocation_capacity,
        .execution_error_event = build->execution_error_event,
        .max_conditional_depth = build->max_conditional_depth};
    build->executables[executable_index] = (cflow_statechart_executable){
        executable, build->cmeta_root->storage_type,
        CMETA_EFFECT_STATEFUL | CMETA_EFFECT_MAY_FAIL,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS};
    build->bindings[executable_index] = (cflow_statechart_executable_binding){
        .id = executable,
        .user = &build->blocks[block_index],
        .contextual_fn = scxml_runtime_execute_block};
    ++build->late_initializer_index;
    ++build->executable_index;
    ++build->block_index;
    *out_executable = executable;
    return SCXML_OK;
}

static scxml_status emit_done_data_block(
    scxml_build *build, size_t done_data,
    cflow_statechart_executable_id *out_executable) {
    const size_t block_index = build->block_index;
    const size_t executable_index = build->executable_index;
    const size_t step_index = build->step_index;
    const cflow_statechart_executable_id executable =
        (cflow_statechart_executable_id)(executable_index + 1u);
    if (done_data >= build->done_data_index ||
        step_index >= build->step_capacity) {
        return scxml_analyze_fail(
            build, SCXML_NATIVE_IR_REJECTED,
            (turbo_xml_location){0u, 0u, 0u},
            "donedata entry block exceeded admitted storage");
    }
    build->steps[step_index] = (scxml_step){
        .kind = SCXML_STEP_DONEDATA,
        .next = step_index + 1u,
        .done_data = done_data};
    ++build->step_index;
    build->blocks[block_index] = (scxml_block){
        .state_type = build->data_model == SCXML_DATA_MODEL_CMETA
                          ? build->cmeta_root->storage_type
                          : &cmeta_type_bool,
        .steps = build->steps,
        .branches = build->branches,
        .effects = build->effects,
        .assignments = build->assignments,
        .payloads = build->payloads,
        .foreach_descriptors = build->foreach_descriptors,
        .invocations = build->invocations,
        .done_data = build->done_data,
        .step_begin = step_index,
        .step_end = build->step_index,
        .step_storage_count = build->step_capacity,
        .branch_storage_count = build->branch_capacity,
        .effect_storage_count = build->effect_capacity,
        .assignment_storage_count = build->assignment_capacity,
        .payload_storage_count = build->payload_capacity,
        .foreach_storage_count = build->foreach_capacity,
        .invocation_storage_count = build->invocation_capacity,
        .done_data_storage_count = build->done_data_capacity,
        .execution_error_event = build->execution_error_event,
        .max_conditional_depth = build->max_conditional_depth};
    build->executables[executable_index] = (cflow_statechart_executable){
        executable,
        build->data_model == SCXML_DATA_MODEL_CMETA
            ? build->cmeta_root->storage_type
            : &cmeta_type_bool,
        CMETA_EFFECT_STATEFUL | CMETA_EFFECT_MAY_FAIL | CMETA_EFFECT_IO,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS};
    build->bindings[executable_index] = (cflow_statechart_executable_binding){
        .id = executable,
        .user = &build->blocks[block_index],
        .contextual_fn = scxml_runtime_execute_block};
    ++build->executable_index;
    ++build->block_index;
    *out_executable = executable;
    return SCXML_OK;
}

scxml_status scxml_emit_state_executables(scxml_build *build,
                                                 scxml_syntax_node node,
                                                 size_t node_count,
                                                 bool is_root) {
    const cflow_machine_state_id owner = scxml_analyze_node_id(build, node, node_count);
    uint32_t entry_order = 0u;
    uint32_t exit_order = 0u;
    size_t index;
    if (owner == 0u) {
        return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                          scxml_syntax_node_location(node),
                          "SCXML state has no native owner for executable content");
    }
    if (build->late_binding) {
        for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
            const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
            if (scxml_syntax_node_type(child) == TURBO_XML_ELEMENT &&
                scxml_analyze_element_kind(child) == SCXML_ELEMENT_DATAMODEL) {
                cflow_statechart_executable_id executable = 0u;
                scxml_status status = emit_late_initializer_block(
                    build, child, is_root, &executable);
                if (status != SCXML_OK) return status;
                if (executable != 0u) {
                    build->state_actions[build->state_action_index++] =
                        (cflow_statechart_state_action){
                            owner, CFLOW_STATECHART_STATE_ACTION_ENTRY,
                            executable, entry_order++};
                }
                break;
            }
        }
    }
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        const scxml_element_kind child_kind = scxml_analyze_element_kind(child);
        scxml_status status;
        if (scxml_syntax_node_type(child) != TURBO_XML_ELEMENT) continue;
        if (child_kind == SCXML_ELEMENT_ONENTRY ||
            child_kind == SCXML_ELEMENT_ONEXIT) {
            cflow_statechart_executable_id executable = 0u;
            status = emit_executable_block(build, child, &executable);
            if (status != SCXML_OK) return status;
            if (executable != 0u) {
                const cflow_statechart_state_action_kind action_kind =
                    child_kind == SCXML_ELEMENT_ONENTRY
                        ? CFLOW_STATECHART_STATE_ACTION_ENTRY
                        : CFLOW_STATECHART_STATE_ACTION_EXIT;
                uint32_t *order = child_kind == SCXML_ELEMENT_ONENTRY
                    ? &entry_order : &exit_order;
                build->state_actions[build->state_action_index++] =
                    (cflow_statechart_state_action){
                        owner, action_kind, executable, (*order)++};
            }
        }
    }
    for (index = 0u; index < build->done_data_index; ++index) {
        const scxml_done_data_descriptor *descriptor =
            &build->done_data[index];
        cflow_statechart_executable_id executable = 0u;
        scxml_status status;
        if (descriptor->final_state != owner) continue;
        status = emit_done_data_block(build, index, &executable);
        if (status != SCXML_OK) return status;
        build->state_actions[build->state_action_index++] =
            (cflow_statechart_state_action){
                owner, CFLOW_STATECHART_STATE_ACTION_ENTRY,
                executable, entry_order++};
        break;
    }
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        const scxml_element_kind child_kind = scxml_analyze_element_kind(child);
        scxml_invocation_descriptor *descriptor;
        cflow_statechart_executable_id enter = 0u;
        cflow_statechart_executable_id exit = 0u;
        size_t child_index;
        scxml_status status;
        if (scxml_syntax_node_type(child) != TURBO_XML_ELEMENT ||
            child_kind != SCXML_ELEMENT_INVOKE)
            continue;
        if (build->invocation_emit_index >= build->invocation_index) {
            return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                              scxml_syntax_node_location(child),
                              "invoke emission exceeded declarations");
        }
        descriptor =
            &build->invocations[build->invocation_emit_index];
        if (descriptor->owner != owner ||
            descriptor->source_node_id != child.impl->id) {
            return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                              scxml_syntax_node_location(child),
                              "invoke declaration order mismatched emission");
        }
        for (child_index = 0u;
             child_index < scxml_syntax_node_child_count(child);
             ++child_index) {
            const scxml_syntax_node content =
                scxml_syntax_node_child_at(child, child_index);
            if (scxml_syntax_node_type(content) == TURBO_XML_ELEMENT &&
                scxml_analyze_element_kind(content) == SCXML_ELEMENT_FINALIZE) {
                status = emit_finalize_block(
                    build, content, &descriptor->finalize);
                if (status != SCXML_OK) return status;
            }
        }
        status = emit_invoke_lifecycle_block(
            build, build->invocation_emit_index, true, &enter);
        if (status != SCXML_OK) return status;
        status = emit_invoke_lifecycle_block(
            build, build->invocation_emit_index, false, &exit);
        if (status != SCXML_OK) return status;
        build->state_actions[build->state_action_index++] =
            (cflow_statechart_state_action){
                owner, CFLOW_STATECHART_STATE_ACTION_ENTRY,
                enter, entry_order++};
        build->state_actions[build->state_action_index++] =
            (cflow_statechart_state_action){
                owner, CFLOW_STATECHART_STATE_ACTION_EXIT,
                exit, exit_order++};
        descriptor->source_node_id = SCXML_AST_NODE_NONE;
        ++build->invocation_emit_index;
    }
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        const scxml_element_kind child_kind = scxml_analyze_element_kind(child);
        if (scxml_syntax_node_type(child) == TURBO_XML_ELEMENT &&
            (scxml_analyze_is_state_element(child_kind) ||
             child_kind == SCXML_ELEMENT_INITIAL ||
             child_kind == SCXML_ELEMENT_HISTORY)) {
            scxml_status status = scxml_emit_state_executables(
                build, child, node_count, false);
            if (status != SCXML_OK) return status;
        }
    }
    return SCXML_OK;
}

static scxml_status emit_transition_targets(
    scxml_build *build, cflow_statechart_transition_id transition,
    turbo_xml_string_view value, turbo_xml_location location) {
    turbo_xml_string_view target;
    size_t cursor = 0u;
    uint32_t order = 0u;
    while (scxml_analyze_token_next(value, &cursor, &target)) {
        const scxml_name_ref *resolved = scxml_analyze_find_name_ref(
            build->state_names, build->state_name_index, target);
        if (resolved == NULL) {
            return scxml_analyze_fail(
                build, SCXML_UNKNOWN_TARGET, location,
                "transition target does not name a declared state");
        }
        if (build->transition_target_index >=
            build->transition_target_capacity) {
            return scxml_analyze_fail(
                build, SCXML_NATIVE_IR_REJECTED, location,
                "transition target emission exceeded admission");
        }
        build->transition_targets[build->transition_target_index++] =
            (cflow_statechart_transition_target){
                transition, (cflow_machine_state_id)resolved->id, order++};
    }
    return SCXML_OK;
}

static scxml_status emit_transition_token(
    scxml_build *build, cflow_machine_state_id source,
    scxml_syntax_node transition_node, turbo_xml_string_view event_token,
    bool has_event, cflow_machine_state_id completion_override,
    cflow_statechart_transition_id *out_transition) {
    const scxml_syntax_attribute target_attribute =
        scxml_analyze_find_attribute(transition_node, "target");
    const scxml_syntax_attribute type_attribute =
        scxml_analyze_find_attribute(transition_node, "type");
    const scxml_syntax_attribute condition_attribute =
        scxml_analyze_find_attribute(transition_node, "cond");
    cflow_statechart_transition row;
    scxml_status status;

    memset(&row, 0, sizeof(row));
    row.id = (cflow_statechart_transition_id)(build->transition_index + 1u);
    row.source = source;
    row.kind = type_attribute.impl != NULL &&
                       scxml_analyze_view_equal_raw(scxml_syntax_attribute_value(type_attribute),
                                      "internal")
                   ? CFLOW_STATECHART_TRANSITION_INTERNAL
                   : CFLOW_STATECHART_TRANSITION_EXTERNAL;
    if (type_attribute.impl != NULL &&
        !scxml_analyze_view_equal_raw(scxml_syntax_attribute_value(type_attribute), "internal") &&
        !scxml_analyze_view_equal_raw(scxml_syntax_attribute_value(type_attribute), "external")) {
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_attribute_location(type_attribute),
                          "transition type must be internal or external");
    }
    row.priority = (uint32_t)build->transition_index;
    row.document_order = (uint32_t)build->transition_index;
    if (completion_override != 0u) {
        row.trigger = CFLOW_STATECHART_TRIGGER_COMPLETION;
        row.completion = completion_override;
    } else if (!has_event) {
        row.trigger = CFLOW_STATECHART_TRIGGER_EVENTLESS;
    } else {
        turbo_xml_string_view completed = {NULL, 0u};
        if (scxml_analyze_completion_token(event_token, &completed)) {
            const scxml_name_ref *state =
                scxml_analyze_find_name_ref(build->state_names, build->state_name_index,
                              completed);
            if (state == NULL) {
                return scxml_analyze_fail(build, SCXML_UNKNOWN_TARGET,
                                  scxml_syntax_node_location(transition_node),
                                  "done.state event names an unknown state");
            }
            row.trigger = CFLOW_STATECHART_TRIGGER_COMPLETION;
            row.completion = (cflow_machine_state_id)state->id;
        } else {
            const scxml_name_ref *event =
                scxml_analyze_find_name_ref(build->event_names, build->event_name_count,
                              event_token);
            if (event == NULL) {
                return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                                  scxml_syntax_node_location(transition_node),
                                  "SCXML event map invariant failed");
            }
            row.trigger = CFLOW_STATECHART_TRIGGER_EVENT;
            row.event = (cflow_event_id)event->id;
        }
    }
    if (condition_attribute.impl != NULL) {
        const size_t guard_index = build->guard_index;
        if (guard_index >= build->guard_capacity) {
            return scxml_analyze_fail(
                build, SCXML_NATIVE_IR_REJECTED,
                scxml_syntax_attribute_location(condition_attribute),
                "transition guard storage invariant failed");
        }
        if (build->data_model == SCXML_DATA_MODEL_CMETA) {
            status = compile_cmeta_condition(
                build, condition_attribute, &build->guard_users[guard_index]);
        } else {
            cflow_machine_state_id condition_state = 0u;
            status = resolve_condition_state(
                build, transition_node, &condition_state);
            if (status == SCXML_OK) {
                build->guard_users[guard_index].data_model =
                    SCXML_DATA_MODEL_NULL;
                build->guard_users[guard_index].value.state = condition_state;
            }
        }
        if (status != SCXML_OK) return status;
        row.guard = (cflow_statechart_guard_id)(guard_index + 1u);
        build->guards[guard_index] = (cflow_statechart_guard){
            row.guard,
            build->data_model == SCXML_DATA_MODEL_CMETA
                ? build->cmeta_root->storage_type
                : &cmeta_type_bool,
            CMETA_EFFECT_MAY_FAIL,
            CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS};
        build->guard_bindings[guard_index] =
            (cflow_statechart_guard_binding){
                .id = row.guard,
                .user = &build->guard_users[guard_index],
                .contextual_fn = evaluate_scxml_transition_guard};
        ++build->guard_index;
    }
    if (target_attribute.impl != NULL) {
        status = emit_transition_targets(
            build, row.id, scxml_syntax_attribute_value(target_attribute),
            scxml_syntax_attribute_location(target_attribute));
        if (status != SCXML_OK) return status;
    }
    build->transitions[build->transition_index++] = row;
    *out_transition = row.id;
    return SCXML_OK;
}

static const scxml_synthetic_initial *find_synthetic(
    const scxml_build *build, size_t synthetic_count,
    cflow_machine_state_id parent) {
    size_t low = 0u;
    size_t high = synthetic_count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        if (build->synthetic_initials[middle].parent < parent)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < synthetic_count &&
                   build->synthetic_initials[low].parent == parent
               ? &build->synthetic_initials[low]
               : NULL;
}

static scxml_status emit_transition_with_action(
    scxml_build *build, cflow_machine_state_id source,
    scxml_syntax_node transition_node, turbo_xml_string_view event_name,
    bool has_event, cflow_statechart_executable_id executable) {
    cflow_statechart_transition_id transition = 0u;
    scxml_status status = emit_transition_token(
        build, source, transition_node, event_name, has_event, 0u,
        &transition);
    if (status != SCXML_OK) return status;
    if (executable != 0u) {
        build->transition_actions[build->transition_action_index++] =
            (cflow_statechart_transition_action){
                transition, executable, 0u};
    }
    return SCXML_OK;
}

static scxml_status emit_completion_transition_with_action(
    scxml_build *build, cflow_machine_state_id source,
    scxml_syntax_node transition_node, cflow_machine_state_id completion,
    cflow_statechart_executable_id executable) {
    cflow_statechart_transition_id transition = 0u;
    const turbo_xml_string_view empty = {NULL, 0u};
    scxml_status status = emit_transition_token(
        build, source, transition_node, empty, true, completion,
        &transition);
    if (status != SCXML_OK) return status;
    if (executable != 0u)
        build->transition_actions[build->transition_action_index++] =
            (cflow_statechart_transition_action){
                transition, executable, 0u};
    return SCXML_OK;
}

static bool emitted_state_can_complete(
    const scxml_build *build, cflow_machine_state_id state) {
    size_t index;
    for (index = 0u; index < build->state_index; ++index) {
        if (build->states[index].id != state) continue;
        return build->states[index].kind == CFLOW_STATECHART_COMPOUND ||
               build->states[index].kind == CFLOW_STATECHART_PARALLEL;
    }
    return false;
}

scxml_status scxml_emit_transitions(scxml_build *build,
                                           scxml_syntax_node node,
                                           size_t node_count,
                                           size_t synthetic_count) {
    const cflow_machine_state_id source = scxml_analyze_node_id(build, node, node_count);
    const scxml_synthetic_initial *synthetic =
        find_synthetic(build, synthetic_count, source);
    size_t index;
    if (synthetic != NULL) {
        cflow_statechart_transition row;
        scxml_status status;
        memset(&row, 0, sizeof(row));
        row.id = (cflow_statechart_transition_id)(build->transition_index + 1u);
        row.source = synthetic->state;
        row.trigger = CFLOW_STATECHART_TRIGGER_EVENTLESS;
        row.kind = CFLOW_STATECHART_TRANSITION_EXTERNAL;
        row.priority = (uint32_t)build->transition_index;
        row.document_order = (uint32_t)build->transition_index;
        status = emit_transition_targets(
            build, row.id, synthetic->target, synthetic->location);
        if (status != SCXML_OK) return status;
        build->transitions[build->transition_index++] = row;
    }
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        const scxml_element_kind child_kind = scxml_analyze_element_kind(child);
        if (scxml_syntax_node_type(child) != TURBO_XML_ELEMENT) continue;
        if (child_kind == SCXML_ELEMENT_TRANSITION) {
            const scxml_syntax_attribute event_attribute =
                scxml_analyze_find_attribute(child, "event");
            cflow_statechart_executable_id executable = 0u;
            scxml_status status = emit_executable_block(
                build, child, &executable);
            if (status != SCXML_OK) return status;
            if (event_attribute.impl == NULL) {
                status = emit_transition_with_action(
                    build, source, child, (turbo_xml_string_view){NULL, 0u},
                    false, executable);
                if (status != SCXML_OK) return status;
            } else {
                const turbo_xml_string_view value =
                    scxml_syntax_attribute_value(event_attribute);
                turbo_xml_string_view token;
                size_t cursor = 0u;
                while (scxml_analyze_token_next(value, &cursor, &token)) {
                    turbo_xml_string_view completed = {NULL, 0u};
                    if (!scxml_analyze_completion_token(token, &completed)) continue;
                    status = emit_transition_with_action(
                        build, source, child, token, true, executable);
                    if (status != SCXML_OK) return status;
                }
                for (cursor = 0u; cursor < build->event_name_count;
                     ++cursor) {
                    const turbo_xml_string_view event_name =
                        build->event_names[cursor].name;
                    turbo_xml_string_view descriptor;
                    size_t descriptor_cursor = 0u;
                    bool matches = false;
                    while (scxml_analyze_token_next(value, &descriptor_cursor,
                                      &descriptor)) {
                        turbo_xml_string_view completed = {NULL, 0u};
                        if (!scxml_analyze_completion_token(descriptor, &completed) &&
                            scxml_analyze_event_descriptor_matches(
                                descriptor, event_name)) {
                            matches = true;
                            break;
                        }
                    }
                    if (!matches) continue;
                    status = emit_transition_with_action(
                        build, source, child, event_name, true, executable);
                    if (status != SCXML_OK) return status;
                }
                for (cursor = 0u; cursor < build->state_name_index;
                     ++cursor) {
                    const scxml_name_ref *completed_state =
                        &build->state_names[cursor];
                    turbo_xml_string_view descriptor;
                    size_t descriptor_cursor = 0u;
                    bool matches = false;
                    if (!emitted_state_can_complete(
                            build,
                            (cflow_machine_state_id)completed_state->id))
                        continue;
                    while (scxml_analyze_token_next(value, &descriptor_cursor,
                                      &descriptor)) {
                        turbo_xml_string_view exact = {NULL, 0u};
                        if (!scxml_analyze_completion_token(descriptor, &exact) &&
                            scxml_analyze_completion_descriptor_matches(
                                descriptor, completed_state->name)) {
                            matches = true;
                            break;
                        }
                    }
                    if (!matches) continue;
                    status = emit_completion_transition_with_action(
                        build, source, child,
                        (cflow_machine_state_id)completed_state->id,
                        executable);
                    if (status != SCXML_OK) return status;
                }
            }
        } else if (scxml_analyze_is_state_element(child_kind) ||
                   child_kind == SCXML_ELEMENT_INITIAL ||
                   child_kind == SCXML_ELEMENT_HISTORY) {
            scxml_status status = scxml_emit_transitions(
                build, child, node_count, synthetic_count);
            if (status != SCXML_OK) return status;
        }
    }
    return SCXML_OK;
}

void scxml_emit_destroy_guard_users(scxml_guard_user *users, size_t count) {
    size_t index;
    if (users == NULL) return;
    for (index = 0u; index < count; ++index) {
        if (users[index].data_model == SCXML_DATA_MODEL_CMETA) {
            scxml_expr_program_destroy(
                &users[index].value.expression);
        }
    }
}

void scxml_emit_destroy_assignments(
    scxml_assign_program *assignments, size_t count) {
    size_t index;
    if (assignments == NULL) return;
    for (index = 0u; index < count; ++index)
        scxml_assign_program_destroy(&assignments[index]);
}

void scxml_emit_destroy_branches(scxml_branch *branches, size_t count) {
    size_t index;
    if (branches == NULL) return;
    for (index = 0u; index < count; ++index)
        scxml_expr_program_destroy(&branches[index].condition);
}

void scxml_emit_destroy_effects(scxml_effect_descriptor *effects, size_t count) {
    size_t index;
    if (effects == NULL) return;
    for (index = 0u; index < count; ++index) {
        scxml_expr_program_destroy(&effects[index].event_expr);
        scxml_expr_program_destroy(&effects[index].target_expr);
        scxml_expr_program_destroy(&effects[index].type_expr);
        scxml_expr_program_destroy(&effects[index].delay_expr);
        scxml_expr_program_destroy(&effects[index].send_id_expr);
        scxml_expr_program_destroy(&effects[index].data_expr);
    }
}

void scxml_emit_destroy_payloads(scxml_payload_descriptor *payloads,
                             size_t count) {
    size_t index;
    if (payloads == NULL) return;
    for (index = 0u; index < count; ++index)
        scxml_expr_program_destroy(
            &payloads[index].expression);
}

void scxml_emit_destroy_invocations(
    scxml_invocation_descriptor *invocations, size_t count) {
    size_t index;
    if (invocations == NULL) return;
    for (index = 0u; index < count; ++index) {
        scxml_expr_program_destroy(
            &invocations[index].type_expr);
        scxml_expr_program_destroy(
            &invocations[index].src_expr);
        scxml_expr_program_destroy(
            &invocations[index].data_expr);
    }
}

void scxml_emit_destroy_done_data(
    scxml_done_data_descriptor *descriptors, size_t count) {
    size_t index;
    if (descriptors == NULL) return;
    for (index = 0u; index < count; ++index) {
        scxml_expr_program_destroy(
            &descriptors[index].expression);
        free(descriptors[index].fields);
        free(descriptors[index].schema_stable_id);
    }
}

void scxml_emit_free_build(scxml_build *build) {
    free(build->states);
    free(build->transitions);
    free(build->transition_targets);
    free(build->guards);
    free(build->events);
    free(build->executables);
    free(build->state_actions);
    free(build->transition_actions);
    free(build->bindings);
    free(build->guard_bindings);
    scxml_emit_destroy_guard_users(build->guard_users, build->guard_capacity);
    free(build->guard_users);
    free(build->blocks);
    free(build->steps);
    scxml_emit_destroy_branches(build->branches, build->branch_capacity);
    free(build->branches);
    scxml_emit_destroy_effects(build->effects, build->effect_capacity);
    free(build->effects);
    scxml_emit_destroy_payloads(build->payloads, build->payload_capacity);
    free(build->payloads);
    scxml_emit_destroy_assignments(build->assignments, build->assignment_capacity);
    free(build->assignments);
    free(build->foreach_descriptors);
    scxml_emit_destroy_invocations(build->invocations, build->invocation_capacity);
    free(build->invocations);
    scxml_emit_destroy_done_data(build->done_data, build->done_data_capacity);
    free(build->done_data);
    free(build->invocation_names);
    free(build->log_storage);
    free(build->effect_storage);
    free(build->invocation_storage);
    free(build->state_names);
    free(build->event_names);
    free(build->event_occurrences);
    free(build->node_refs);
    free(build->synthetic_initials);
    memset(build, 0, sizeof(*build));
}

void *scxml_emit_allocate_rows(size_t count, size_t element_size) {
    size_t bytes;
    if (count == 0u) return NULL;
    if (!scxml_analyze_checked_multiply(count, element_size, &bytes)) return NULL;
    return calloc(1u, bytes);
}

void scxml_emit_copy_program_names(scxml_program_name *destination,
                               const scxml_name_ref *source,
                               size_t count, char **cursor) {
    size_t index;
    for (index = 0u; index < count; ++index) {
        destination[index].name = *cursor;
        destination[index].size = source[index].name.size;
        destination[index].id = source[index].id;
        memcpy(*cursor, source[index].name.data, source[index].name.size);
        *cursor += source[index].name.size;
    }
}
