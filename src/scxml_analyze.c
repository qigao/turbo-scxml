#include "scxml_analyze.h"
#include "scxml_emit.h"
#include "scxml_program.h"
#include "scxml_runtime.h"

bool scxml_analyze_checked_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) return false;
    *out = left + right;
    return true;
}

static bool payload_scalar_to_text(
    const scxml_payload_value *value, char *storage,
    size_t capacity, const char **out_data, size_t *out_size) {
    scxml_expr_value converted = {0};
    if (value == NULL) return false;
    switch (value->kind) {
        case SCXML_PAYLOAD_VALUE_BOOL:
            converted.kind = SCXML_EXPR_VALUE_BOOL;
            converted.data.boolean = value->data.boolean;
            break;
        case SCXML_PAYLOAD_VALUE_SINT:
            converted.kind = SCXML_EXPR_VALUE_SINT;
            converted.data.sint = value->data.sint;
            break;
        case SCXML_PAYLOAD_VALUE_UINT:
            converted.kind = SCXML_EXPR_VALUE_UINT;
            converted.data.uint = value->data.uint;
            break;
        case SCXML_PAYLOAD_VALUE_FLOAT:
            converted.kind = SCXML_EXPR_VALUE_FLOAT;
            converted.data.number = value->data.number;
            break;
        case SCXML_PAYLOAD_VALUE_STRING:
            converted.kind = SCXML_EXPR_VALUE_STRING;
            converted.data.string.data = value->data.string.data;
            converted.data.string.size = value->data.string.size;
            break;
        default: return false;
    }
    return scxml_runtime_scalar_value_to_text(
        &converted, storage, capacity, out_data, out_size);
}

bool scxml_analyze_attach_event_content(
    scxml_session_impl *session,
    scxml_external_event_metadata_row *row,
    const scxml_content_view *content) {
    const char *data = NULL;
    size_t data_size = 0u;
    if (session == NULL || row == NULL || content == NULL || !row->in_use)
        return false;
    if (content->kind == SCXML_CONTENT_INVALID) return true;
    if (content->kind == SCXML_CONTENT_SCALAR) {
        if (!payload_scalar_to_text(
                &content->scalar, row->data, sizeof(row->data),
                &data, &data_size))
            return false;
        if (data_size != 0u) memmove(row->data, data, data_size);
        row->data[data_size] = '\0';
        row->data_size = data_size;
        return true;
    }
    if (content->kind == SCXML_CONTENT_TEXT_UTF8 ||
        content->kind == SCXML_CONTENT_XML_UTF8) {
        if (content->byte_count > SCXML_EVENT_METADATA_CAPACITY ||
            (content->byte_count != 0u && content->bytes == NULL))
            return false;
        if (content->byte_count != 0u)
            memcpy(row->data, content->bytes, content->byte_count);
        row->data[content->byte_count] = '\0';
        row->data_size = content->byte_count;
        return true;
    }
    if (content->kind != SCXML_CONTENT_CMETA ||
        session->program->cmeta_root == NULL ||
        content->schema != session->program->cmeta_root ||
        content->object == NULL ||
        !scxml_runtime_copy_event_data_object(
            content->schema, row->data_object.bytes, content->object))
        return false;
    row->data_schema = content->schema;
    row->data_object_live = true;
    return true;
}

bool scxml_analyze_checked_multiply(size_t left, size_t right, size_t *out) {
    if (out == NULL || (left != 0u && right > SIZE_MAX / left)) return false;
    *out = left * right;
    return true;
}

static bool view_equal(salts_xml_string_view left,
                       salts_xml_string_view right) {
    return left.size == right.size &&
           (left.size == 0u || memcmp(left.data, right.data, left.size) == 0);
}

bool scxml_analyze_view_equal_raw(salts_xml_string_view view, const char *raw) {
    const size_t size = strlen(raw);
    return view.size == size &&
           (size == 0u || memcmp(view.data, raw, size) == 0);
}

int scxml_analyze_compare_view(salts_xml_string_view left,
                        salts_xml_string_view right) {
    const size_t common = left.size < right.size ? left.size : right.size;
    const int compared = common != 0u ? memcmp(left.data, right.data, common) : 0;
    if (compared != 0) return compared;
    if (left.size < right.size) return -1;
    if (left.size > right.size) return 1;
    return 0;
}

int scxml_analyze_compare_name_ref(const void *left, const void *right) {
    return scxml_analyze_compare_view(((const scxml_name_ref *)left)->name,
                        ((const scxml_name_ref *)right)->name);
}

static int compare_name_order(const void *left, const void *right) {
    const size_t left_order = ((const scxml_name_ref *)left)->order;
    const size_t right_order = ((const scxml_name_ref *)right)->order;
    return left_order < right_order ? -1 : left_order > right_order ? 1 : 0;
}

const scxml_name_ref *scxml_analyze_find_earliest_duplicate(
        scxml_name_ref *names, size_t count) {
    const scxml_name_ref *earliest = NULL;
    size_t group_begin = 0u;

    while (group_begin < count) {
        const scxml_name_ref *first = NULL;
        const scxml_name_ref *second = NULL;
        size_t group_end = group_begin + 1u;
        size_t index;

        while (group_end < count &&
               view_equal(names[group_begin].name, names[group_end].name)) {
            ++group_end;
        }
        for (index = group_begin; index < group_end; ++index) {
            const scxml_name_ref *candidate = &names[index];
            if (first == NULL || candidate->order < first->order) {
                second = first;
                first = candidate;
            } else if (second == NULL || candidate->order < second->order) {
                second = candidate;
            }
        }
        if (second != NULL &&
            (earliest == NULL || second->order < earliest->order)) {
            earliest = second;
        }
        group_begin = group_end;
    }
    return earliest;
}

int scxml_analyze_compare_node_ref(const void *left, const void *right) {
    const scxml_ast_node_id left_node =
        ((const scxml_node_ref *)left)->node_id;
    const scxml_ast_node_id right_node =
        ((const scxml_node_ref *)right)->node_id;
    return left_node < right_node ? -1 : left_node > right_node ? 1 : 0;
}

int scxml_analyze_compare_program_name(const void *left, const void *right) {
    const scxml_program_name *left_name = (const scxml_program_name *)left;
    const scxml_program_name *right_name = (const scxml_program_name *)right;
    const salts_xml_string_view left_view = {left_name->name, left_name->size};
    const salts_xml_string_view right_view = {right_name->name, right_name->size};
    return scxml_analyze_compare_view(left_view, right_view);
}

bool scxml_analyze_bind_current_event_system_values(
    const scxml_expr_system_values *base,
    const scxml_program_name *const *event_names_by_id,
    size_t event_name_count,
    const cflow_event_view *event,
    scxml_expr_system_values *out) {
    const scxml_program_name *name;
    if (base == NULL || out == NULL) return false;
    *out = *base;
    if (event == NULL) return true;
    if (event_names_by_id == NULL || event->id == 0u ||
        event->id > event_name_count)
        return false;
    name = event_names_by_id[event->id - 1u];
    if (name == NULL || name->id != event->id) return false;
    out->event_name = (scxml_expr_string_view){
        name->name, name->size};
    return true;
}

scxml_status scxml_analyze_fail(scxml_build *build,
                                     scxml_status status,
                                     salts_xml_location location,
                                     const char *message) {
    if (build != NULL && build->diagnostic != NULL) {
        build->diagnostic->status = status;
        build->diagnostic->location = location;
        (void)snprintf(build->diagnostic->message,
                       sizeof(build->diagnostic->message), "%s", message);
    }
    return status;
}

static bool is_empty_view(salts_xml_string_view view) {
    return view.data == NULL || view.size == 0u;
}

static bool decode_utf8(const char *data, size_t size, size_t *cursor,
                        uint32_t *codepoint) {
    const size_t start = *cursor;
    const unsigned char lead = (unsigned char)data[start];
    size_t width;
    size_t index;
    uint32_t value;

    if (lead <= 0x7fu) {
        *codepoint = lead;
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
        value = (value << 6) | (continuation & 0x3fu);
    }
    if ((width == 3u && value < 0x800u) ||
        (width == 4u && value < 0x10000u) ||
        (value >= 0xd800u && value <= 0xdfffu) || value > 0x10ffffu) {
        return false;
    }
    *codepoint = value;
    *cursor = start + width;
    return true;
}

static bool is_ncname_start(uint32_t codepoint) {
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

static bool is_ncname_char(uint32_t codepoint) {
    return is_ncname_start(codepoint) || codepoint == '-' || codepoint == '.' ||
           (codepoint >= '0' && codepoint <= '9') || codepoint == 0xb7u ||
           (codepoint >= 0x300u && codepoint <= 0x36fu) ||
           (codepoint >= 0x203fu && codepoint <= 0x2040u);
}

bool scxml_analyze_is_xml_ncname(salts_xml_string_view name) {
    size_t cursor = 0u;
    uint32_t codepoint;
    if (is_empty_view(name) ||
        !decode_utf8(name.data, name.size, &cursor, &codepoint) ||
        !is_ncname_start(codepoint)) {
        return false;
    }
    while (cursor < name.size) {
        if (!decode_utf8(name.data, name.size, &cursor, &codepoint) ||
            !is_ncname_char(codepoint)) {
            return false;
        }
    }
    return true;
}

bool scxml_analyze_is_xml_nmtoken(salts_xml_string_view token) {
    size_t cursor = 0u;
    uint32_t codepoint;
    if (is_empty_view(token)) return false;
    while (cursor < token.size) {
        if (!decode_utf8(token.data, token.size, &cursor, &codepoint) ||
            !(is_ncname_char(codepoint) || codepoint == ':')) {
            return false;
        }
    }
    return true;
}

static bool xml_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static bool is_xml_whitespace(salts_xml_string_view value) {
    size_t index;
    for (index = 0u; index < value.size; ++index) {
        if (!xml_space(value.data[index])) return false;
    }
    return true;
}

static void skip_xml_space(salts_xml_string_view value, size_t *cursor) {
    while (*cursor < value.size && xml_space(value.data[*cursor]))
        ++*cursor;
}

bool scxml_analyze_parse_null_in_condition(
    salts_xml_string_view value, salts_xml_string_view *out_state) {
    size_t cursor = 0u;
    size_t begin;
    if (out_state == NULL) return false;
    *out_state = (salts_xml_string_view){NULL, 0u};
    skip_xml_space(value, &cursor);
    if (cursor > value.size || value.size - cursor < 2u ||
        memcmp(value.data + cursor, "In", 2u) != 0)
        return false;
    cursor += 2u;
    skip_xml_space(value, &cursor);
    if (cursor == value.size || value.data[cursor++] != '(') return false;
    skip_xml_space(value, &cursor);
    begin = cursor;
    while (cursor < value.size && value.data[cursor] != ')' &&
           !xml_space(value.data[cursor]))
        ++cursor;
    out_state->data = value.data + begin;
    out_state->size = cursor - begin;
    skip_xml_space(value, &cursor);
    if (cursor == value.size || value.data[cursor++] != ')') return false;
    skip_xml_space(value, &cursor);
    return cursor == value.size && scxml_analyze_is_xml_ncname(*out_state);
}

scxml_element_kind scxml_analyze_element_kind(scxml_syntax_node node) {
    return node.impl != NULL ? node.impl->kind : SCXML_ELEMENT_UNKNOWN;
}

bool scxml_analyze_is_state_element(scxml_element_kind kind) {
    return kind == SCXML_ELEMENT_STATE || kind == SCXML_ELEMENT_PARALLEL ||
           kind == SCXML_ELEMENT_FINAL;
}

scxml_syntax_attribute scxml_analyze_find_attribute(scxml_syntax_node node,
                                          const char *local_name) {
    return scxml_syntax_node_find_attribute(
        node, scxml_ast_attribute_kind_from_name(local_name));
}

static bool attribute_allowed(scxml_element_kind kind,
                              salts_xml_string_view name) {
    switch (kind) {
        case SCXML_ELEMENT_SCXML:
            return scxml_analyze_view_equal_raw(name, "version") ||
                   scxml_analyze_view_equal_raw(name, "datamodel") ||
                   scxml_analyze_view_equal_raw(name, "initial") ||
                   scxml_analyze_view_equal_raw(name, "name") ||
                   scxml_analyze_view_equal_raw(name, "binding") ||
                   scxml_analyze_view_equal_raw(name, "id");
        case SCXML_ELEMENT_STATE:
            return scxml_analyze_view_equal_raw(name, "id") ||
                   scxml_analyze_view_equal_raw(name, "initial");
        case SCXML_ELEMENT_PARALLEL:
        case SCXML_ELEMENT_FINAL:
            return scxml_analyze_view_equal_raw(name, "id");
        case SCXML_ELEMENT_HISTORY:
            return scxml_analyze_view_equal_raw(name, "id") ||
                   scxml_analyze_view_equal_raw(name, "type");
        case SCXML_ELEMENT_TRANSITION:
            return scxml_analyze_view_equal_raw(name, "event") ||
                   scxml_analyze_view_equal_raw(name, "target") ||
                   scxml_analyze_view_equal_raw(name, "type") ||
                   scxml_analyze_view_equal_raw(name, "cond");
        case SCXML_ELEMENT_RAISE:
            return scxml_analyze_view_equal_raw(name, "event");
        case SCXML_ELEMENT_SEND:
            return scxml_analyze_view_equal_raw(name, "event") ||
                   scxml_analyze_view_equal_raw(name, "eventexpr") ||
                   scxml_analyze_view_equal_raw(name, "target") ||
                   scxml_analyze_view_equal_raw(name, "targetexpr") ||
                   scxml_analyze_view_equal_raw(name, "type") ||
                   scxml_analyze_view_equal_raw(name, "typeexpr") ||
                   scxml_analyze_view_equal_raw(name, "id") ||
                   scxml_analyze_view_equal_raw(name, "idlocation") ||
                   scxml_analyze_view_equal_raw(name, "delay") ||
                   scxml_analyze_view_equal_raw(name, "delayexpr") ||
                   scxml_analyze_view_equal_raw(name, "namelist");
        case SCXML_ELEMENT_CANCEL:
            return scxml_analyze_view_equal_raw(name, "sendid") ||
                   scxml_analyze_view_equal_raw(name, "sendidexpr");
        case SCXML_ELEMENT_LOG:
            return scxml_analyze_view_equal_raw(name, "label") ||
                   scxml_analyze_view_equal_raw(name, "expr");
        case SCXML_ELEMENT_ASSIGN:
            return scxml_analyze_view_equal_raw(name, "location") ||
                   scxml_analyze_view_equal_raw(name, "expr");
        case SCXML_ELEMENT_FOREACH:
            return scxml_analyze_view_equal_raw(name, "array") ||
                   scxml_analyze_view_equal_raw(name, "item") ||
                   scxml_analyze_view_equal_raw(name, "index");
        case SCXML_ELEMENT_IF:
        case SCXML_ELEMENT_ELSEIF:
            return scxml_analyze_view_equal_raw(name, "cond");
        case SCXML_ELEMENT_INVOKE:
            return scxml_analyze_view_equal_raw(name, "id") ||
                   scxml_analyze_view_equal_raw(name, "idlocation") ||
                   scxml_analyze_view_equal_raw(name, "type") ||
                   scxml_analyze_view_equal_raw(name, "typeexpr") ||
                   scxml_analyze_view_equal_raw(name, "src") ||
                   scxml_analyze_view_equal_raw(name, "srcexpr") ||
                   scxml_analyze_view_equal_raw(name, "namelist") ||
                   scxml_analyze_view_equal_raw(name, "autoforward");
        case SCXML_ELEMENT_CONTENT:
            return scxml_analyze_view_equal_raw(name, "expr");
        case SCXML_ELEMENT_PARAM:
            return scxml_analyze_view_equal_raw(name, "name") ||
                   scxml_analyze_view_equal_raw(name, "expr") ||
                   scxml_analyze_view_equal_raw(name, "location");
        case SCXML_ELEMENT_DATA:
            return scxml_analyze_view_equal_raw(name, "id") ||
                   scxml_analyze_view_equal_raw(name, "expr") ||
                   scxml_analyze_view_equal_raw(name, "src");
        case SCXML_ELEMENT_SCRIPT:
            return scxml_analyze_view_equal_raw(name, "src");
        case SCXML_ELEMENT_DATAMODEL:
        case SCXML_ELEMENT_DONEDATA:
            return false;
        case SCXML_ELEMENT_ELSE:
        case SCXML_ELEMENT_FINALIZE:
        case SCXML_ELEMENT_INITIAL:
        case SCXML_ELEMENT_ONENTRY:
        case SCXML_ELEMENT_ONEXIT:
        case SCXML_ELEMENT_UNKNOWN: return false;
    }
    return false;
}

static scxml_status analyze_script(
    scxml_build *build, scxml_syntax_node node, scxml_counts *counts,
    bool root) {
    const scxml_syntax_attribute source =
        scxml_analyze_find_attribute(node, "src");
    const salts_xml_string_view inline_source = scxml_syntax_node_text(node);
    size_t retained;
    size_t index;
    scxml_status status = scxml_analyze_validate_element_attributes(
        build, node, SCXML_ELEMENT_SCRIPT);
    if (status != SCXML_OK) return status;
    if (!build->quickjs_profile)
        return scxml_analyze_fail(
            build, SCXML_UNSUPPORTED_FEATURE,
            scxml_syntax_node_location(node),
            "script requires the quickjs-sandbox data model");
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        if (scxml_syntax_node_type(child) != SALTS_XML_TEXT &&
            scxml_syntax_node_type(child) != SALTS_XML_COMMENT)
            return scxml_analyze_fail(
                build, SCXML_INVALID_STRUCTURE,
                scxml_syntax_node_location(child),
                "script accepts text content only");
    }
    if (source.impl != NULL && inline_source.size != 0u)
        return scxml_analyze_fail(
            build, SCXML_INVALID_STRUCTURE,
            scxml_syntax_node_location(node),
            "script src and inline content are mutually exclusive");
    if (source.impl != NULL &&
        is_empty_view(scxml_syntax_attribute_value(source)))
        return scxml_analyze_fail(
            build, SCXML_INVALID_STRUCTURE,
            scxml_syntax_attribute_location(source),
            "script src must be non-empty");
    retained = source.impl != NULL
        ? build->quickjs_options.max_source_bytes : inline_source.size;
    if (retained > build->quickjs_options.max_source_bytes ||
        !scxml_analyze_checked_add(retained, 1u, &retained) ||
        !scxml_analyze_checked_add(
            counts->script_source_bytes, retained,
            &counts->script_source_bytes) ||
        !scxml_analyze_checked_add(
            counts->script_rows, 1u, &counts->script_rows) ||
        (root && !scxml_analyze_checked_add(
            counts->root_script_rows, 1u,
            &counts->root_script_rows)) ||
        (!scxml_analyze_checked_add(
            counts->executable_steps, 1u,
            &counts->executable_steps)))
        return scxml_analyze_fail(
            build, SCXML_LIMIT_EXCEEDED,
            scxml_syntax_node_location(node),
            "script source exceeds admitted storage");
    return SCXML_OK;
}

scxml_status scxml_analyze_validate_element_attributes(
    scxml_build *build, scxml_syntax_node node, scxml_element_kind kind) {
    size_t index;
    for (index = 0u; index < scxml_syntax_node_attribute_count(node); ++index) {
        const scxml_syntax_attribute attribute =
            scxml_syntax_node_attribute_at(node, index);
        const salts_xml_string_view namespace_uri =
            scxml_syntax_attribute_namespace_uri(attribute);
        const salts_xml_string_view name =
            scxml_syntax_attribute_local_name(attribute);
        if (!is_empty_view(namespace_uri)) continue;
        if (kind == SCXML_ELEMENT_LOG && scxml_analyze_view_equal_raw(name, "expr")) {
            return scxml_analyze_fail(
                build, SCXML_UNSUPPORTED_FEATURE,
                scxml_syntax_attribute_location(attribute),
                "SCXML null data model has no log value expressions");
        }
        if (!attribute_allowed(kind, name)) {
            return scxml_analyze_fail(
                build,
                kind == SCXML_ELEMENT_LOG || kind == SCXML_ELEMENT_ASSIGN
                    ? SCXML_INVALID_STRUCTURE
                    : SCXML_UNSUPPORTED_FEATURE,
                scxml_syntax_attribute_location(attribute),
                kind == SCXML_ELEMENT_LOG || kind == SCXML_ELEMENT_ASSIGN
                    ? "executable element has an invalid unqualified attribute"
                    : "unsupported unqualified SCXML attribute");
        }
    }
    return SCXML_OK;
}

static scxml_status require_scxml_element(scxml_build *build,
                                                scxml_syntax_node node,
                                                scxml_element_kind *out_kind) {
    const salts_xml_string_view namespace_uri =
        scxml_syntax_node_namespace_uri(node);
    const scxml_element_kind kind = scxml_analyze_element_kind(node);
    if (!scxml_analyze_view_equal_raw(namespace_uri, SCXML_NAMESPACE)) {
        return scxml_analyze_fail(build, SCXML_INVALID_NAMESPACE,
                          scxml_syntax_node_location(node),
                          "SCXML elements must use the W3C SCXML namespace");
    }
    if (kind == SCXML_ELEMENT_UNKNOWN) {
        return scxml_analyze_fail(build, SCXML_UNSUPPORTED_FEATURE,
                          scxml_syntax_node_location(node),
                          "unsupported SCXML element");
    }
    *out_kind = kind;
    return scxml_analyze_validate_element_attributes(build, node, kind);
}

const scxml_cmeta_custom_action_v1 *scxml_analyze_find_custom_action(
    const scxml_build *build, scxml_syntax_node node) {
    const salts_xml_string_view namespace_uri =
        scxml_syntax_node_namespace_uri(node);
    const salts_xml_string_view local_name =
        scxml_syntax_node_local_name(node);
    size_t index;
    if (build == NULL || namespace_uri.data == NULL ||
        namespace_uri.size == 0u ||
        scxml_analyze_view_equal_raw(namespace_uri, SCXML_NAMESPACE))
        return NULL;
    for (index = 0u; index < build->custom_action_registry_count; ++index) {
        const scxml_cmeta_custom_action_v1 *action =
            &build->custom_action_registry[index];
        if (action->namespace_uri_size == namespace_uri.size &&
            action->local_name_size == local_name.size &&
            memcmp(action->namespace_uri, namespace_uri.data,
                   namespace_uri.size) == 0 &&
            memcmp(action->local_name, local_name.data,
                   local_name.size) == 0)
            return action;
    }
    return NULL;
}

static scxml_status analyze_custom_action(
    scxml_build *build, scxml_syntax_node node, scxml_counts *counts) {
    const scxml_cmeta_custom_action_v1 *action =
        scxml_analyze_find_custom_action(build, node);
    size_t index, matched = 0u;
    if (build->data_model != SCXML_DATA_MODEL_CMETA ||
        build->quickjs_profile || action == NULL)
        return scxml_analyze_fail(
            build, SCXML_UNSUPPORTED_FEATURE,
            scxml_syntax_node_location(node),
            "foreign executable element has no CMeta action registration");
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child =
            scxml_syntax_node_child_at(node, index);
        if (scxml_syntax_node_type(child) == SALTS_XML_COMMENT ||
            (scxml_syntax_node_type(child) == SALTS_XML_TEXT &&
             is_xml_whitespace(scxml_syntax_node_value(child))))
            continue;
        return scxml_analyze_fail(
            build, SCXML_INVALID_STRUCTURE,
            scxml_syntax_node_location(child),
            "CMeta custom action must not contain child content");
    }
    for (index = 0u; index < scxml_syntax_node_attribute_count(node); ++index) {
        const scxml_syntax_attribute attribute =
            scxml_syntax_node_attribute_at(node, index);
        const salts_xml_string_view namespace_uri =
            scxml_syntax_attribute_namespace_uri(attribute);
        const salts_xml_string_view name =
            scxml_syntax_attribute_local_name(attribute);
        size_t parameter;
        bool found = false;
        if (namespace_uri.size != 0u)
            return scxml_analyze_fail(
                build, SCXML_INVALID_STRUCTURE,
                scxml_syntax_attribute_location(attribute),
                "CMeta custom action arguments must be unqualified");
        for (parameter = 0u; parameter < action->parameter_count;
             ++parameter) {
            const char *expected = action->parameter_names[parameter];
            if (strlen(expected) == name.size &&
                memcmp(expected, name.data, name.size) == 0) {
                found = true;
                break;
            }
        }
        if (!found ||
            scxml_syntax_attribute_value(attribute).size == 0u)
            return scxml_analyze_fail(
                build, SCXML_INVALID_STRUCTURE,
                scxml_syntax_attribute_location(attribute),
                "CMeta custom action has an unknown or empty argument");
        ++matched;
    }
    if (matched != action->parameter_count)
        return scxml_analyze_fail(
            build, SCXML_INVALID_STRUCTURE,
            scxml_syntax_node_location(node),
            "CMeta custom action arguments do not match its callable");
    if (!scxml_analyze_checked_add(
            counts->executable_steps, 1u, &counts->executable_steps) ||
        !scxml_analyze_checked_add(
            counts->custom_action_rows, 1u,
            &counts->custom_action_rows) ||
        !scxml_analyze_checked_add(
            counts->custom_action_argument_rows,
            action->parameter_count,
            &counts->custom_action_argument_rows))
        return scxml_analyze_fail(
            build, SCXML_LIMIT_EXCEEDED,
            scxml_syntax_node_location(node),
            "CMeta custom action descriptor count overflow");
    return SCXML_OK;
}

size_t scxml_analyze_element_child_count(scxml_syntax_node node,
                                  scxml_element_kind wanted) {
    size_t count = 0u;
    size_t index;
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        if (scxml_syntax_node_type(child) == SALTS_XML_ELEMENT &&
            scxml_analyze_element_kind(child) == wanted) {
            ++count;
        }
    }
    return count;
}

static scxml_syntax_node first_real_child(scxml_syntax_node node) {
    size_t index;
    scxml_syntax_node empty = {NULL};
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        if (scxml_syntax_node_type(child) == SALTS_XML_ELEMENT &&
            scxml_analyze_is_state_element(scxml_analyze_element_kind(child))) {
            return child;
        }
    }
    return empty;
}

bool scxml_analyze_token_next(salts_xml_string_view value, size_t *cursor,
                       salts_xml_string_view *token) {
    size_t begin;
    if (cursor == NULL || token == NULL) return false;
    begin = *cursor;
    while (begin < value.size && isspace((unsigned char)value.data[begin]))
        ++begin;
    if (begin == value.size) {
        *cursor = begin;
        return false;
    }
    *cursor = begin;
    while (*cursor < value.size &&
           !isspace((unsigned char)value.data[*cursor])) {
        ++*cursor;
    }
    token->data = value.data + begin;
    token->size = *cursor - begin;
    return true;
}

static bool normalize_event_descriptor(salts_xml_string_view token,
                                       salts_xml_string_view *out_base,
                                       bool *out_match_all) {
    const char *wildcard;
    if (out_base == NULL || out_match_all == NULL || token.size == 0u)
        return false;
    *out_base = token;
    *out_match_all = false;
    if (token.size == 1u && token.data[0] == '*') {
        out_base->size = 0u;
        *out_match_all = true;
        return true;
    }
    wildcard = (const char *)memchr(token.data, '*', token.size);
    if (wildcard != NULL) {
        if ((size_t)(wildcard - token.data) + 1u != token.size ||
            token.size < 3u || token.data[token.size - 2u] != '.')
            return false;
        out_base->size -= 2u;
    } else if (token.data[token.size - 1u] == '.') {
        --out_base->size;
    }
    return out_base->size != 0u;
}

bool scxml_analyze_event_descriptor_matches(salts_xml_string_view descriptor,
                                     salts_xml_string_view event_name) {
    salts_xml_string_view base;
    bool match_all;
    if (!normalize_event_descriptor(descriptor, &base, &match_all))
        return false;
    if (match_all) return true;
    return event_name.size == base.size
        ? memcmp(event_name.data, base.data, base.size) == 0
        : event_name.size > base.size &&
              memcmp(event_name.data, base.data, base.size) == 0 &&
              event_name.data[base.size] == '.';
}

bool scxml_analyze_completion_token(salts_xml_string_view token,
                             salts_xml_string_view *state_name) {
    static const char prefix[] = "done.state.";
    if (token.size <= sizeof(prefix) - 1u ||
        memcmp(token.data, prefix, sizeof(prefix) - 1u) != 0) {
        return false;
    }
    state_name->data = token.data + sizeof(prefix) - 1u;
    state_name->size = token.size - (sizeof(prefix) - 1u);
    return scxml_analyze_is_xml_ncname(*state_name);
}

bool scxml_analyze_completion_descriptor_matches(
    salts_xml_string_view descriptor, salts_xml_string_view state_name) {
    static const char prefix[] = "done.state.";
    salts_xml_string_view base = {NULL, 0u};
    const size_t prefix_size = sizeof(prefix) - 1u;
    size_t event_size;
    bool match_all = false;
    size_t index;
    if (state_name.size > SIZE_MAX - prefix_size) return false;
    event_size = prefix_size + state_name.size;
    if (!normalize_event_descriptor(descriptor, &base, &match_all))
        return false;
    if (match_all) return true;
    if (base.size > event_size) return false;
    for (index = 0u; index < base.size; ++index) {
        const char expected = index < prefix_size
            ? prefix[index] : state_name.data[index - prefix_size];
        if (base.data[index] != expected) return false;
    }
    if (base.size == event_size) return true;
    return base.size < prefix_size
        ? prefix[base.size] == '.'
        : state_name.data[base.size - prefix_size] == '.';
}

static scxml_status analyze_raise(scxml_build *build,
                                        scxml_syntax_node node,
                                        scxml_counts *counts) {
    const scxml_syntax_attribute event_attribute = scxml_analyze_find_attribute(node, "event");
    scxml_status status = scxml_analyze_validate_element_attributes(
        build, node, SCXML_ELEMENT_RAISE);
    size_t index;
    if (status != SCXML_OK) return status;
    if (event_attribute.impl == NULL) {
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "raise requires one event NMTOKEN");
    }
    if (!scxml_analyze_is_xml_nmtoken(scxml_syntax_attribute_value(event_attribute))) {
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_attribute_location(event_attribute),
                          "raise event must be one XML NMTOKEN");
    }
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        if (scxml_syntax_node_type(child) != SALTS_XML_COMMENT) {
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_node_location(child),
                              "raise cannot contain executable or text content");
        }
    }
    if (!scxml_analyze_checked_add(counts->executable_steps, 1u,
                     &counts->executable_steps) ||
        !scxml_analyze_checked_add(counts->event_occurrences, 1u,
                     &counts->event_occurrences)) {
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_attribute_location(event_attribute),
                          "raise step count overflow");
    }
    return SCXML_OK;
}

bool scxml_analyze_parse_delay_ms(salts_xml_string_view value, uint64_t *out_ms) {
    size_t number_size;
    size_t index;
    size_t dot = SIZE_MAX;
    size_t fraction_digits = 0u;
    uint64_t whole = 0u;
    uint64_t fraction = 0u;
    bool seconds;
    if (out_ms == NULL || value.data == NULL || value.size < 2u)
        return false;
    if (value.size >= 3u && value.data[value.size - 2u] == 'm' &&
        value.data[value.size - 1u] == 's') {
        seconds = false;
        number_size = value.size - 2u;
    } else if (value.data[value.size - 1u] == 's') {
        seconds = true;
        number_size = value.size - 1u;
    } else {
        return false;
    }
    if (number_size == 0u) return false;
    for (index = 0u; index < number_size; ++index) {
        const unsigned char digit = (unsigned char)value.data[index];
        if (digit == '.') {
            if (!seconds || dot != SIZE_MAX || index == 0u ||
                index + 1u == number_size) {
                return false;
            }
            dot = index;
            continue;
        }
        if (!isdigit(digit)) return false;
        if (dot == SIZE_MAX) {
            if (whole > (UINT64_MAX - (uint64_t)(digit - '0')) / 10u)
                return false;
            whole = whole * 10u + (uint64_t)(digit - '0');
        } else {
            ++fraction_digits;
            if (fraction_digits > 3u) return false;
            fraction = fraction * 10u + (uint64_t)(digit - '0');
        }
    }
    if (!seconds) {
        *out_ms = whole;
        return true;
    }
    if (whole > UINT64_MAX / 1000u) return false;
    while (fraction_digits < 3u) {
        fraction *= 10u;
        ++fraction_digits;
    }
    if (whole * 1000u > UINT64_MAX - fraction) return false;
    *out_ms = whole * 1000u + fraction;
    return true;
}

static bool count_retained_view(salts_xml_string_view value,
                                size_t *total) {
    size_t retained;
    return scxml_analyze_checked_add(value.size, 1u, &retained) &&
           scxml_analyze_checked_add(*total, retained, total);
}

static scxml_status validate_empty_effect(
    scxml_build *build, scxml_syntax_node node, const char *message);

bool scxml_analyze_cmeta_content_kind_is_scalar(cmeta_data_kind kind) {
    return kind == CMETA_DATA_BOOL || kind == CMETA_DATA_SINT ||
           kind == CMETA_DATA_UINT || kind == CMETA_DATA_FLOAT ||
           kind == CMETA_DATA_STRING || kind == CMETA_DATA_ENUM;
}

scxml_status scxml_analyze_inspect_inline_content(
    scxml_build *build, scxml_syntax_node content,
    scxml_content_kind *out_kind, size_t *out_size) {
    size_t index;
    salts_xml_status xml_status;
    bool has_markup = false;
    if (build == NULL || out_kind == NULL || out_size == NULL)
        return SCXML_INVALID_ARGUMENT;
    for (index = 0u; index < scxml_syntax_node_child_count(content); ++index) {
        if (scxml_syntax_node_type(scxml_syntax_node_child_at(content, index)) !=
            SALTS_XML_TEXT) {
            has_markup = true;
            break;
        }
    }
    xml_status = scxml_syntax_serialize_children(
        content, NULL, 0u, build->limits.max_name_bytes, out_size);
    if (xml_status != SALTS_XML_OK) {
        return scxml_analyze_fail(
            build,
            xml_status == SALTS_XML_LIMIT_EXCEEDED
                ? SCXML_LIMIT_EXCEEDED
                : xml_status == SALTS_XML_ALLOCATION_FAILED
                    ? SCXML_ALLOCATION_FAILED
                    : SCXML_INVALID_STRUCTURE,
            scxml_syntax_node_location(content),
            "SCXML inline content serialization failed");
    }
    *out_kind = has_markup ? SCXML_CONTENT_XML_UTF8
                           : SCXML_CONTENT_TEXT_UTF8;
    return SCXML_OK;
}

static bool content_expression_is_rich(
    const scxml_build *build, scxml_syntax_attribute expression) {
    scxml_location location = {0};
    scxml_expr_diagnostic diagnostic = {0};
    const salts_xml_string_view source =
        scxml_syntax_attribute_value(expression);
    return build != NULL && expression.impl != NULL &&
        scxml_location_compile(
            &location, source.data, source.size, build->cmeta_root,
            build->expression_limits.max_path_depth, false,
            &diagnostic) == SCXML_EXPR_OK &&
        location.value != NULL &&
        !scxml_analyze_cmeta_content_kind_is_scalar(location.value->kind);
}

static scxml_status analyze_param(
    scxml_build *build, scxml_syntax_node node) {
    const scxml_syntax_attribute name = scxml_analyze_find_attribute(node, "name");
    const scxml_syntax_attribute expression = scxml_analyze_find_attribute(node, "expr");
    const scxml_syntax_attribute location = scxml_analyze_find_attribute(node, "location");
    scxml_status status = scxml_analyze_validate_element_attributes(
        build, node, SCXML_ELEMENT_PARAM);
    if (status != SCXML_OK) return status;
    if (build->data_model != SCXML_DATA_MODEL_CMETA)
        return scxml_analyze_fail(build, SCXML_UNSUPPORTED_FEATURE,
                          scxml_syntax_node_location(node),
                          "param requires the CMeta data model");
    if (name.impl == NULL ||
        is_empty_view(scxml_syntax_attribute_value(name)) ||
        ((expression.impl == NULL) == (location.impl == NULL)))
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "param requires a name and exactly one expr or location");
    if (validate_empty_effect(build, node,
                              "param cannot contain child content") !=
        SCXML_OK)
        return SCXML_INVALID_STRUCTURE;
    return SCXML_OK;
}

static scxml_status analyze_send_content(
    scxml_build *build, scxml_syntax_node node, scxml_counts *counts,
    bool *out_has_data, bool *out_is_rich_content,
    size_t *out_param_count) {
    size_t index;
    bool found = false;
    *out_has_data = false;
    *out_is_rich_content = false;
    *out_param_count = 0u;
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        scxml_element_kind child_kind;
        if (scxml_syntax_node_type(child) == SALTS_XML_COMMENT ||
            (scxml_syntax_node_type(child) == SALTS_XML_TEXT &&
             is_xml_whitespace(scxml_syntax_node_value(child)))) {
            continue;
        }
        if (scxml_syntax_node_type(child) == SALTS_XML_ELEMENT) {
            scxml_status namespace_status = require_scxml_element(
                build, child, &child_kind);
            if (namespace_status != SCXML_OK)
                return namespace_status;
        } else {
            child_kind = SCXML_ELEMENT_UNKNOWN;
        }
        if (child_kind == SCXML_ELEMENT_PARAM) {
            scxml_status status;
            if (found)
                return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                                  scxml_syntax_node_location(child),
                                  "send content is mutually exclusive with param");
            status = analyze_param(build, child);
            if (status != SCXML_OK) return status;
            if (!scxml_analyze_checked_add(*out_param_count, 1u, out_param_count))
                return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                                  scxml_syntax_node_location(child),
                                  "send param count overflow");
            continue;
        }
        if (child_kind != SCXML_ELEMENT_CONTENT) {
            return scxml_analyze_fail(build, SCXML_UNSUPPORTED_FEATURE,
                              scxml_syntax_node_location(child),
                              "send accepts only param or bounded scalar content");
        }
        if (found || *out_param_count != 0u) {
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_node_location(child),
                              "send content is unique and mutually exclusive with param");
        }
        found = true;
        {
            const scxml_syntax_attribute expression =
                scxml_analyze_find_attribute(child, "expr");
            size_t content_size = 0u;
            size_t retained = 0u;
            scxml_content_kind content_kind;
            scxml_status status = scxml_analyze_validate_element_attributes(
                build, child, SCXML_ELEMENT_CONTENT);
            if (status != SCXML_OK) return status;
            status = scxml_analyze_inspect_inline_content(
                build, child, &content_kind, &content_size);
            if (status != SCXML_OK) return status;
            if (expression.impl != NULL) {
                if (build->data_model != SCXML_DATA_MODEL_CMETA ||
                    is_empty_view(scxml_syntax_attribute_value(expression))) {
                    return scxml_analyze_fail(
                        build, SCXML_UNSUPPORTED_FEATURE,
                        scxml_syntax_attribute_location(expression),
                        "send content expr requires the CMeta data model");
                }
                if (content_size != 0u) {
                    return scxml_analyze_fail(
                        build, SCXML_INVALID_STRUCTURE,
                        scxml_syntax_node_location(child),
                        "content expr cannot have inline child content");
                }
                *out_is_rich_content = content_expression_is_rich(
                    build, expression);
            } else {
                if (!scxml_analyze_checked_add(content_size, 1u, &retained) ||
                    !scxml_analyze_checked_add(counts->effect_string_bytes, retained,
                                 &counts->effect_string_bytes)) {
                    return scxml_analyze_fail(
                        build, SCXML_LIMIT_EXCEEDED,
                        scxml_syntax_node_location(child),
                        "send inline content storage size overflow");
                }
                *out_is_rich_content = true;
            }
        }
    }
    *out_has_data = found;
    return SCXML_OK;
}

static scxml_status analyze_done_data(
    scxml_build *build, scxml_syntax_node node, scxml_counts *counts) {
    size_t index;
    size_t param_count = 0u;
    bool found_content = false;
    bool has_expression = false;
    scxml_status status = scxml_analyze_validate_element_attributes(
        build, node, SCXML_ELEMENT_DONEDATA);
    if (status != SCXML_OK) return status;
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        scxml_syntax_attribute expression = {0};
        scxml_element_kind child_kind;
        size_t content_size = 0u;
        size_t retained = 0u;
        scxml_content_kind content_kind;
        if (scxml_syntax_node_type(child) == SALTS_XML_COMMENT ||
            (scxml_syntax_node_type(child) == SALTS_XML_TEXT &&
             is_xml_whitespace(scxml_syntax_node_value(child))))
            continue;
        if (scxml_syntax_node_type(child) != SALTS_XML_ELEMENT)
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_node_location(child),
                              "donedata admits only param or content children");
        child_kind = scxml_analyze_element_kind(child);
        if (child_kind == SCXML_ELEMENT_PARAM) {
            if (found_content)
                return scxml_analyze_fail(
                    build, SCXML_INVALID_STRUCTURE,
                    scxml_syntax_node_location(child),
                    "donedata content is mutually exclusive with param");
            status = analyze_param(build, child);
            if (status != SCXML_OK) return status;
            if (!scxml_analyze_checked_add(param_count, 1u, &param_count) ||
                param_count > SCXML_PAYLOAD_MAX_ENTRIES)
                return scxml_analyze_fail(
                    build, SCXML_LIMIT_EXCEEDED,
                    scxml_syntax_node_location(child),
                    "donedata param count exceeds the payload bound");
            continue;
        }
        if (child_kind != SCXML_ELEMENT_CONTENT)
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_node_location(child),
                              "donedata admits only param or content children");
        expression = scxml_analyze_find_attribute(child, "expr");
        if (found_content || param_count != 0u)
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_node_location(child),
                              param_count != 0u
                                  ? "donedata content is mutually exclusive with param"
                                  : "donedata accepts exactly one content child");
        found_content = true;
        status = scxml_analyze_validate_element_attributes(
            build, child, SCXML_ELEMENT_CONTENT);
        if (status != SCXML_OK) return status;
        status = scxml_analyze_inspect_inline_content(
            build, child, &content_kind, &content_size);
        if (status != SCXML_OK) return status;
        (void)content_kind;
        if (expression.impl != NULL) {
            has_expression = true;
            if (build->data_model != SCXML_DATA_MODEL_CMETA ||
                is_empty_view(scxml_syntax_attribute_value(expression)))
                return scxml_analyze_fail(
                    build, SCXML_UNSUPPORTED_FEATURE,
                    scxml_syntax_node_location(child),
                    "donedata content expr requires the CMeta data model");
            if (content_size != 0u)
                return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                                  scxml_syntax_node_location(child),
                                  "donedata content expr cannot have inline children");
            if (content_expression_is_rich(build, expression)) {
                scxml_location location = {0};
                scxml_expr_diagnostic diagnostic = {0};
                const salts_xml_string_view source =
                    scxml_syntax_attribute_value(expression);
                const cmeta_type_desc *type;
                if (scxml_location_compile(
                        &location, source.data, source.size,
                        build->cmeta_root,
                        build->expression_limits.max_path_depth, false,
                        &diagnostic) != SCXML_EXPR_OK ||
                    location.value == NULL ||
                    location.value->kind != CMETA_DATA_STRUCT ||
                    (type = location.value->storage_type) == NULL ||
                    type->size > SCXML_EVENT_DATA_CAPACITY ||
                    type->align > _Alignof(scxml_event_data_storage) ||
                    (cmeta_type_require_traits(
                         type, CMETA_TRAIT_TRIVIAL_COPY |
                                   CMETA_TRAIT_TRIVIAL_DESTROY) != CMETA_OK &&
                     cmeta_type_require_traits(
                         type, CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY) !=
                         CMETA_OK))
                    return scxml_analyze_fail(
                        build, SCXML_LIMIT_EXCEEDED,
                        scxml_syntax_attribute_location(expression),
                        "structured donedata exceeds the bounded completion object contract");
            }
        } else {
            if (content_size > SCXML_EVENT_METADATA_CAPACITY ||
                !scxml_analyze_checked_add(content_size, 1u, &retained) ||
                !scxml_analyze_checked_add(counts->effect_string_bytes, retained,
                             &counts->effect_string_bytes))
                return scxml_analyze_fail(
                    build, SCXML_LIMIT_EXCEEDED,
                    scxml_syntax_node_location(child),
                    "donedata inline content exceeds the Event metadata bound");
        }
    }
    if (!found_content && param_count == 0u)
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "donedata requires content or at least one param");
    if (param_count != 0u) {
        const cmeta_type_desc *type =
            build->cmeta_root != NULL ? build->cmeta_root->storage_type : NULL;
        if (build->data_model != SCXML_DATA_MODEL_CMETA || type == NULL ||
            type->size > SCXML_EVENT_DATA_CAPACITY ||
            type->align > _Alignof(scxml_event_data_storage))
            return scxml_analyze_fail(
                build, SCXML_LIMIT_EXCEEDED,
                scxml_syntax_node_location(node),
                "donedata param object exceeds the bounded Event storage");
    }
    if (!scxml_analyze_checked_add(counts->done_data_rows, 1u,
                     &counts->done_data_rows) ||
        !scxml_analyze_checked_add(counts->executable_blocks, 1u,
                     &counts->executable_blocks) ||
        !scxml_analyze_checked_add(counts->block_rows, 1u,
                     &counts->block_rows) ||
        !scxml_analyze_checked_add(counts->executable_steps, 1u,
                     &counts->executable_steps) ||
        !scxml_analyze_checked_add(counts->state_action_rows, 1u,
                     &counts->state_action_rows) ||
        !scxml_analyze_checked_add(counts->assignment_rows, param_count,
                     &counts->assignment_rows) ||
        (has_expression &&
         !scxml_analyze_checked_add(counts->dynamic_expression_rows, 1u,
                      &counts->dynamic_expression_rows)))
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "donedata descriptor count overflow");
    return SCXML_OK;
}

static scxml_status validate_empty_effect(
    scxml_build *build, scxml_syntax_node node, const char *message) {
    size_t index;
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        if (scxml_syntax_node_type(child) == SALTS_XML_COMMENT ||
            (scxml_syntax_node_type(child) == SALTS_XML_TEXT &&
             is_xml_whitespace(scxml_syntax_node_value(child))))
            continue;
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(child), message);
    }
    return SCXML_OK;
}

static scxml_status analyze_send(scxml_build *build,
                                       scxml_syntax_node node,
                                       scxml_counts *counts) {
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
    const salts_xml_string_view event =
        event_attribute.impl != NULL
            ? scxml_syntax_attribute_value(event_attribute)
            : (salts_xml_string_view){NULL, 0u};
    const salts_xml_string_view target =
        target_attribute.impl != NULL
            ? scxml_syntax_attribute_value(target_attribute)
            : (salts_xml_string_view){NULL, 0u};
    const salts_xml_string_view type =
        type_attribute.impl != NULL
            ? scxml_syntax_attribute_value(type_attribute)
            : (salts_xml_string_view){NULL, 0u};
    const salts_xml_string_view id =
        id_attribute.impl != NULL
            ? scxml_syntax_attribute_value(id_attribute)
            : (salts_xml_string_view){NULL, 0u};
    uint64_t delay_ms = 0u;
    bool internal_target;
    bool has_data = false;
    bool rich_content = false;
    size_t payload_count = 0u;
    size_t param_count = 0u;
    size_t token_cursor = 0u;
    salts_xml_string_view token;
    size_t child_index;
    scxml_status status = scxml_analyze_validate_element_attributes(
        build, node, SCXML_ELEMENT_SEND);
    if (status != SCXML_OK) return status;
    if ((event_attribute.impl != NULL && event_expr_attribute.impl != NULL) ||
        (event_attribute.impl != NULL && !scxml_analyze_is_xml_nmtoken(event))) {
        return scxml_analyze_fail(
            build, SCXML_INVALID_STRUCTURE,
            event_attribute.impl != NULL
                ? scxml_syntax_attribute_location(event_attribute)
                : scxml_syntax_node_location(node),
            "send event and eventexpr are mutually exclusive");
    }
    if ((target_attribute.impl != NULL && target_expr_attribute.impl != NULL) ||
        (type_attribute.impl != NULL && type_expr_attribute.impl != NULL) ||
        (delay_attribute.impl != NULL && delay_expr_attribute.impl != NULL) ||
        (id_attribute.impl != NULL && idlocation_attribute.impl != NULL))
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "send literal and expression attributes are mutually exclusive");
    if ((event_expr_attribute.impl != NULL ||
         target_expr_attribute.impl != NULL ||
         type_expr_attribute.impl != NULL ||
         delay_expr_attribute.impl != NULL ||
         idlocation_attribute.impl != NULL ||
         namelist_attribute.impl != NULL) &&
        build->data_model != SCXML_DATA_MODEL_CMETA)
        return scxml_analyze_fail(build, SCXML_UNSUPPORTED_FEATURE,
                          scxml_syntax_node_location(node),
                          "send expressions require the CMeta data model");
    if (idlocation_attribute.impl != NULL &&
        is_empty_view(scxml_syntax_attribute_value(idlocation_attribute)))
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_attribute_location(idlocation_attribute),
                          "send idlocation must be non-empty");
    if ((target_attribute.impl != NULL && target.size == 0u) ||
        (type_attribute.impl != NULL && type.size == 0u) ||
        (id_attribute.impl != NULL && !scxml_analyze_is_xml_ncname(id))) {
        scxml_syntax_attribute owner = target_attribute.impl != NULL &&
                                            target.size == 0u
                                        ? target_attribute
                                    : type_attribute.impl != NULL &&
                                              type.size == 0u
                                        ? type_attribute
                                        : id_attribute;
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_attribute_location(owner),
                          "send literal attributes must be non-empty and id must be an XML NCName");
    }
    if (delay_attribute.impl != NULL &&
        !scxml_analyze_parse_delay_ms(scxml_syntax_attribute_value(delay_attribute),
                        &delay_ms)) {
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_attribute_location(delay_attribute),
                          "send delay must be an unsigned ms or s literal with millisecond precision");
    }
    if (delay_ms != 0u && id_attribute.impl == NULL &&
        idlocation_attribute.impl == NULL) {
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_attribute_location(delay_attribute),
                          "delayed send requires id or idlocation");
    }
    status = analyze_send_content(
        build, node, counts, &has_data, &rich_content,
        &param_count);
    if (status != SCXML_OK) return status;
    if (!has_data && event_attribute.impl == NULL &&
        event_expr_attribute.impl == NULL &&
        namelist_attribute.impl == NULL && param_count == 0u)
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "send requires an event, eventexpr, content, namelist, or param");
    if (has_data &&
        (namelist_attribute.impl != NULL || param_count != 0u))
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "send content is mutually exclusive with namelist and param");
    if (namelist_attribute.impl != NULL) {
        const salts_xml_string_view namelist =
            scxml_syntax_attribute_value(namelist_attribute);
        while (scxml_analyze_token_next(namelist, &token_cursor, &token)) {
            if (!scxml_analyze_checked_add(payload_count, 1u, &payload_count) ||
                !count_retained_view(token,
                                     &counts->effect_string_bytes))
                return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                                  scxml_syntax_attribute_location(
                                      namelist_attribute),
                                  "send namelist storage size overflow");
        }
        if (payload_count == 0u)
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_attribute_location(
                                  namelist_attribute),
                              "send namelist must contain one location");
    }
    for (child_index = 0u;
         child_index < scxml_syntax_node_child_count(node); ++child_index) {
        const scxml_syntax_node child =
            scxml_syntax_node_child_at(node, child_index);
        scxml_syntax_attribute name;
        if (scxml_syntax_node_type(child) != SALTS_XML_ELEMENT ||
            scxml_analyze_element_kind(child) != SCXML_ELEMENT_PARAM)
            continue;
        name = scxml_analyze_find_attribute(child, "name");
        if (!count_retained_view(scxml_syntax_attribute_value(name),
                                 &counts->effect_string_bytes))
            return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                              scxml_syntax_attribute_location(name),
                              "send param name storage size overflow");
    }
    if (!scxml_analyze_checked_add(payload_count, param_count, &payload_count) ||
        payload_count > SCXML_PAYLOAD_MAX_ENTRIES ||
        !scxml_analyze_checked_add(counts->payload_rows, payload_count,
                     &counts->payload_rows))
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "send payload entry limit exceeded");
    if (payload_count > counts->max_payload_entries)
        counts->max_payload_entries = payload_count;
    internal_target = scxml_analyze_view_equal_raw(target, "#_internal") ||
                      scxml_analyze_view_equal_raw(target, "_internal");
    if (has_data && internal_target &&
        (delay_attribute.impl != NULL || delay_expr_attribute.impl != NULL))
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "internal send content cannot be delayed");
    if (payload_count != 0u && internal_target &&
        build->data_model != SCXML_DATA_MODEL_CMETA)
        return scxml_analyze_fail(
            build, SCXML_UNSUPPORTED_FEATURE,
            scxml_syntax_node_location(node),
            "internal named-object payloads require the CMeta data model");
    if (payload_count != 0u && internal_target) {
        const cmeta_type_desc *root_type = build->cmeta_root != NULL
            ? build->cmeta_root->storage_type : NULL;
        if (root_type == NULL ||
            root_type->size > SCXML_EVENT_DATA_CAPACITY ||
            root_type->align > _Alignof(scxml_event_data_storage) ||
            (cmeta_type_require_traits(
                 root_type, CMETA_TRAIT_TRIVIAL_COPY |
                           CMETA_TRAIT_TRIVIAL_DESTROY) != CMETA_OK &&
             cmeta_type_require_traits(
                 root_type, CMETA_TRAIT_COPY |
                                CMETA_TRAIT_DESTROY) != CMETA_OK))
            return scxml_analyze_fail(
                build, SCXML_LIMIT_EXCEEDED,
                scxml_syntax_node_location(node),
                "internal named payload exceeds the bounded Event data contract");
    }
    if (payload_count != 0u && internal_target &&
        !scxml_analyze_checked_add(
            counts->assignment_rows, payload_count,
            &counts->assignment_rows))
        return scxml_analyze_fail(
            build, SCXML_LIMIT_EXCEEDED,
            scxml_syntax_node_location(node),
            "internal payload assignment count overflow");
    if (!scxml_analyze_checked_add(counts->executable_steps, 1u,
                     &counts->executable_steps) ||
        !scxml_analyze_checked_add(counts->effect_rows, 1u, &counts->effect_rows) ||
        (event_attribute.impl != NULL &&
         !scxml_analyze_checked_add(counts->event_occurrences, 1u,
                      &counts->event_occurrences)) ||
        (event_attribute.impl != NULL &&
         !count_retained_view(event, &counts->effect_string_bytes)) ||
        (target_attribute.impl != NULL &&
         !count_retained_view(target, &counts->effect_string_bytes)) ||
        (type_attribute.impl != NULL &&
         !count_retained_view(type, &counts->effect_string_bytes)) ||
        (id_attribute.impl != NULL &&
         !count_retained_view(id, &counts->effect_string_bytes))) {
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "send descriptor storage size overflow");
    }
    if ((event_expr_attribute.impl != NULL ||
         target_expr_attribute.impl != NULL ||
         type_expr_attribute.impl != NULL ||
         delay_expr_attribute.impl != NULL || has_data ||
         payload_count != 0u) &&
        !scxml_analyze_checked_add(counts->dynamic_expression_rows, 1u,
                     &counts->dynamic_expression_rows))
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "send expression count overflow");
    if (target_expr_attribute.impl != NULL || !internal_target ||
        delay_ms != 0u || delay_expr_attribute.impl != NULL)
        counts->requirements |= SCXML_REQUIREMENT_EVENT_IO;
    if ((payload_count != 0u &&
         (target_expr_attribute.impl != NULL || !internal_target)) ||
        (has_data && !rich_content &&
         (target_expr_attribute.impl != NULL || !internal_target)))
        counts->requirements |= SCXML_REQUIREMENT_PAYLOAD;
    if (rich_content &&
        (target_expr_attribute.impl != NULL || !internal_target))
        counts->requirements |= SCXML_REQUIREMENT_CONTENT;
    if (delay_ms != 0u || delay_expr_attribute.impl != NULL)
        counts->requirements |= SCXML_REQUIREMENT_DELAYED_SEND;
    return SCXML_OK;
}

static scxml_status analyze_cancel(scxml_build *build,
                                         scxml_syntax_node node,
                                         scxml_counts *counts) {
    const scxml_syntax_attribute sendid_attribute = scxml_analyze_find_attribute(node, "sendid");
    const scxml_syntax_attribute sendid_expr_attribute =
        scxml_analyze_find_attribute(node, "sendidexpr");
    const salts_xml_string_view sendid =
        sendid_attribute.impl != NULL
            ? scxml_syntax_attribute_value(sendid_attribute)
            : (salts_xml_string_view){NULL, 0u};
    scxml_status status = scxml_analyze_validate_element_attributes(
        build, node, SCXML_ELEMENT_CANCEL);
    if (status != SCXML_OK) return status;
    if ((sendid_attribute.impl == NULL) ==
            (sendid_expr_attribute.impl == NULL) ||
        (sendid_attribute.impl != NULL && !scxml_analyze_is_xml_ncname(sendid))) {
        return scxml_analyze_fail(
            build, SCXML_INVALID_STRUCTURE,
            sendid_attribute.impl != NULL
                ? scxml_syntax_attribute_location(sendid_attribute)
                : scxml_syntax_node_location(node),
            "cancel requires exactly one sendid or sendidexpr");
    }
    if (sendid_expr_attribute.impl != NULL &&
        build->data_model != SCXML_DATA_MODEL_CMETA)
        return scxml_analyze_fail(build, SCXML_UNSUPPORTED_FEATURE,
                          scxml_syntax_attribute_location(sendid_expr_attribute),
                          "cancel sendidexpr requires the CMeta data model");
    status = validate_empty_effect(build, node, "cancel must be empty");
    if (status != SCXML_OK) return status;
    if (!scxml_analyze_checked_add(counts->executable_steps, 1u,
                     &counts->executable_steps) ||
        !scxml_analyze_checked_add(counts->effect_rows, 1u, &counts->effect_rows) ||
        (sendid_attribute.impl != NULL &&
         !count_retained_view(sendid, &counts->effect_string_bytes))) {
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "cancel descriptor storage size overflow");
    }
    if (sendid_expr_attribute.impl != NULL &&
        !scxml_analyze_checked_add(counts->dynamic_expression_rows, 1u,
                     &counts->dynamic_expression_rows))
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "cancel expression count overflow");
    counts->requirements |= SCXML_REQUIREMENT_EVENT_IO |
                            SCXML_REQUIREMENT_DELAYED_SEND |
                            SCXML_REQUIREMENT_CANCEL;
    return SCXML_OK;
}

static scxml_status analyze_executable_content(
    scxml_build *build, scxml_syntax_node node, scxml_counts *counts,
    size_t conditional_depth);

static scxml_status analyze_log(scxml_build *build,
                                      scxml_syntax_node node,
                                      scxml_counts *counts) {
    const scxml_syntax_attribute label_attribute = scxml_analyze_find_attribute(node, "label");
    const salts_xml_string_view label =
        label_attribute.impl != NULL
            ? scxml_syntax_attribute_value(label_attribute)
            : (salts_xml_string_view){NULL, 0u};
    size_t retained_bytes;
    size_t index;
    scxml_status status = scxml_analyze_validate_element_attributes(
        build, node, SCXML_ELEMENT_LOG);
    if (status != SCXML_OK) return status;
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        if (scxml_syntax_node_type(child) != SALTS_XML_COMMENT) {
            return scxml_analyze_fail(
                build, SCXML_INVALID_STRUCTURE,
                scxml_syntax_node_location(child),
                "log cannot contain executable or text content");
        }
    }
    if (!scxml_analyze_checked_add(label.size, 1u, &retained_bytes) ||
        !scxml_analyze_checked_add(counts->log_label_bytes, retained_bytes,
                     &counts->log_label_bytes) ||
        !scxml_analyze_checked_add(counts->executable_steps, 1u,
                     &counts->executable_steps)) {
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "log label storage size overflow");
    }
    if (counts->log_label_bytes > build->limits.max_name_bytes) {
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "log labels exceed max_name_bytes");
    }
    return SCXML_OK;
}

static scxml_status analyze_assign(scxml_build *build,
                                         scxml_syntax_node node,
                                         scxml_counts *counts) {
    const scxml_syntax_attribute location = scxml_analyze_find_attribute(node, "location");
    const scxml_syntax_attribute expression = scxml_analyze_find_attribute(node, "expr");
    size_t index;
    scxml_status status = scxml_analyze_validate_element_attributes(
        build, node, SCXML_ELEMENT_ASSIGN);
    if (status != SCXML_OK) return status;
    if (build->data_model != SCXML_DATA_MODEL_CMETA)
        return scxml_analyze_fail(build, SCXML_UNSUPPORTED_FEATURE,
                          scxml_syntax_node_location(node),
                          "assign requires the CMeta data model");
    if (location.impl == NULL ||
        is_empty_view(scxml_syntax_attribute_value(location)) ||
        expression.impl == NULL ||
        is_empty_view(scxml_syntax_attribute_value(expression))) {
        const salts_xml_location failure_location =
            location.impl == NULL || expression.impl == NULL
                ? scxml_syntax_node_location(node)
                : location.impl != NULL &&
                          is_empty_view(scxml_syntax_attribute_value(location))
                      ? scxml_syntax_attribute_location(location)
                      : scxml_syntax_attribute_location(expression);
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          failure_location,
                          "assign requires non-empty location and expr attributes");
    }
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        if (scxml_syntax_node_type(child) != SALTS_XML_COMMENT)
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_node_location(child),
                              "assign cannot contain child content");
    }
    if (!scxml_analyze_checked_add(counts->executable_steps, 1u,
                     &counts->executable_steps) ||
        !scxml_analyze_checked_add(counts->assignment_rows, 1u,
                     &counts->assignment_rows))
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "assignment descriptor count overflow");
    return SCXML_OK;
}

static scxml_status analyze_foreach(
    scxml_build *build, scxml_syntax_node node, scxml_counts *counts,
    size_t executable_depth) {
    const scxml_syntax_attribute array = scxml_analyze_find_attribute(node, "array");
    const scxml_syntax_attribute item = scxml_analyze_find_attribute(node, "item");
    const scxml_syntax_attribute index = scxml_analyze_find_attribute(node, "index");
    const size_t first_child_step = counts->executable_steps;
    scxml_status status = scxml_analyze_validate_element_attributes(
        build, node, SCXML_ELEMENT_FOREACH);
    if (status != SCXML_OK) return status;
    if (build->data_model != SCXML_DATA_MODEL_CMETA)
        return scxml_analyze_fail(build, SCXML_UNSUPPORTED_FEATURE,
                          scxml_syntax_node_location(node),
                          "foreach requires the CMeta data model");
    if (array.impl == NULL ||
        is_empty_view(scxml_syntax_attribute_value(array)) ||
        item.impl == NULL ||
        is_empty_view(scxml_syntax_attribute_value(item)) ||
        (index.impl != NULL &&
         is_empty_view(scxml_syntax_attribute_value(index))))
        return scxml_analyze_fail(
            build, SCXML_INVALID_STRUCTURE,
            array.impl == NULL || item.impl == NULL
                ? scxml_syntax_node_location(node)
                : array.impl != NULL &&
                          is_empty_view(scxml_syntax_attribute_value(array))
                      ? scxml_syntax_attribute_location(array)
                      : item.impl != NULL &&
                                is_empty_view(scxml_syntax_attribute_value(item))
                            ? scxml_syntax_attribute_location(item)
                            : scxml_syntax_attribute_location(index),
            "foreach requires non-empty array and item, and a non-empty index when present");
    if (!scxml_analyze_checked_add(counts->executable_steps, 1u,
                     &counts->executable_steps) ||
        !scxml_analyze_checked_add(counts->foreach_rows, 1u, &counts->foreach_rows))
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "foreach descriptor count overflow");
    if (executable_depth > counts->max_conditional_depth)
        counts->max_conditional_depth = executable_depth;
    status = analyze_executable_content(
        build, node, counts, executable_depth);
    if (status != SCXML_OK) return status;
    if (counts->executable_steps == first_child_step + 1u)
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "foreach requires executable child content");
    return SCXML_OK;
}

static scxml_status validate_condition_attribute(
    scxml_build *build, scxml_syntax_node node) {
    const scxml_syntax_attribute condition = scxml_analyze_find_attribute(node, "cond");
    salts_xml_string_view state;
    if (build->data_model == SCXML_DATA_MODEL_CMETA) {
        if (condition.impl == NULL ||
            is_empty_view(scxml_syntax_attribute_value(condition))) {
            return scxml_analyze_fail(
                build, SCXML_INVALID_STRUCTURE,
                condition.impl != NULL
                    ? scxml_syntax_attribute_location(condition)
                    : scxml_syntax_node_location(node),
                "if and elseif require one non-empty CMeta condition");
        }
        return SCXML_OK;
    }
    if (condition.impl == NULL) {
        return scxml_analyze_fail(
            build, SCXML_INVALID_STRUCTURE,
            scxml_syntax_node_location(node),
            "if and elseif require one null-model In(id) condition");
    }
    if (!scxml_analyze_parse_null_in_condition(
            scxml_syntax_attribute_value(condition), &state)) {
        return scxml_analyze_fail(
            build, SCXML_INVALID_STRUCTURE,
            scxml_syntax_attribute_location(condition),
            "null-model condition must be In(id)");
    }
    return SCXML_OK;
}

static scxml_status validate_empty_marker(
    scxml_build *build, scxml_syntax_node node) {
    size_t index;
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        if (scxml_syntax_node_type(child) == SALTS_XML_COMMENT ||
            (scxml_syntax_node_type(child) == SALTS_XML_TEXT &&
             is_xml_whitespace(scxml_syntax_node_value(child))))
            continue;
        return scxml_analyze_fail(
            build, SCXML_INVALID_STRUCTURE,
            scxml_syntax_node_location(child),
            "elseif and else markers must be empty");
    }
    return SCXML_OK;
}

static scxml_status analyze_conditional(
    scxml_build *build, scxml_syntax_node node, scxml_counts *counts,
    size_t conditional_depth) {
    size_t branch_count = 1u;
    size_t index;
    bool saw_else = false;
    scxml_status status;

    status = validate_condition_attribute(build, node);
    if (status != SCXML_OK) return status;
    if (!scxml_analyze_checked_add(
            counts->executable_steps, 1u, &counts->executable_steps)) {
        return scxml_analyze_fail(
            build, SCXML_LIMIT_EXCEEDED,
            scxml_syntax_node_location(node), "conditional step count overflow");
    }
    if (conditional_depth > counts->max_conditional_depth)
        counts->max_conditional_depth = conditional_depth;
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        scxml_element_kind child_kind;
        if (scxml_syntax_node_type(child) == SALTS_XML_COMMENT ||
            (scxml_syntax_node_type(child) == SALTS_XML_TEXT &&
             is_xml_whitespace(scxml_syntax_node_value(child))))
            continue;
        if (scxml_syntax_node_type(child) != SALTS_XML_ELEMENT) {
            return scxml_analyze_fail(
                build, SCXML_UNSUPPORTED_FEATURE,
                scxml_syntax_node_location(child),
                "conditional text content is not supported");
        }
        if (!scxml_analyze_view_equal_raw(
                scxml_syntax_node_namespace_uri(child), SCXML_NAMESPACE)) {
            status = analyze_custom_action(build, child, counts);
            if (status != SCXML_OK) return status;
            continue;
        }
        status = require_scxml_element(build, child, &child_kind);
        if (status != SCXML_OK) return status;
        if (child_kind == SCXML_ELEMENT_ELSEIF ||
            child_kind == SCXML_ELEMENT_ELSE) {
            if (saw_else) {
                return scxml_analyze_fail(
                    build, SCXML_INVALID_STRUCTURE,
                    scxml_syntax_node_location(child),
                    "elseif and else cannot follow else");
            }
            if (child_kind == SCXML_ELEMENT_ELSEIF) {
                status = validate_condition_attribute(build, child);
                if (status != SCXML_OK) return status;
            } else {
                saw_else = true;
            }
            status = validate_empty_marker(build, child);
            if (status != SCXML_OK) return status;
            if (!scxml_analyze_checked_add(branch_count, 1u, &branch_count)) {
                return scxml_analyze_fail(
                    build, SCXML_LIMIT_EXCEEDED,
                    scxml_syntax_node_location(child),
                    "conditional branch count overflow");
            }
        } else if (child_kind == SCXML_ELEMENT_RAISE) {
            status = analyze_raise(build, child, counts);
            if (status != SCXML_OK) return status;
        } else if (child_kind == SCXML_ELEMENT_SEND) {
            status = analyze_send(build, child, counts);
            if (status != SCXML_OK) return status;
        } else if (child_kind == SCXML_ELEMENT_CANCEL) {
            status = analyze_cancel(build, child, counts);
            if (status != SCXML_OK) return status;
        } else if (child_kind == SCXML_ELEMENT_LOG) {
            status = analyze_log(build, child, counts);
            if (status != SCXML_OK) return status;
        } else if (child_kind == SCXML_ELEMENT_ASSIGN) {
            status = analyze_assign(build, child, counts);
            if (status != SCXML_OK) return status;
        } else if (child_kind == SCXML_ELEMENT_SCRIPT) {
            status = analyze_script(build, child, counts, false);
            if (status != SCXML_OK) return status;
        } else if (child_kind == SCXML_ELEMENT_IF) {
            size_t next_depth;
            if (!scxml_analyze_checked_add(conditional_depth, 1u, &next_depth)) {
                return scxml_analyze_fail(
                    build, SCXML_LIMIT_EXCEEDED,
                    scxml_syntax_node_location(child),
                    "conditional nesting depth overflow");
            }
            status = analyze_conditional(
                build, child, counts, next_depth);
            if (status != SCXML_OK) return status;
        } else if (child_kind == SCXML_ELEMENT_FOREACH) {
            size_t next_depth;
            if (!scxml_analyze_checked_add(conditional_depth, 1u, &next_depth))
                return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                                  scxml_syntax_node_location(child),
                                  "foreach nesting depth overflow");
            status = analyze_foreach(
                build, child, counts, next_depth);
            if (status != SCXML_OK) return status;
        } else {
            return scxml_analyze_fail(
                build, SCXML_UNSUPPORTED_FEATURE,
                scxml_syntax_node_location(child),
                "unsupported conditional executable element");
        }
    }
    if (!scxml_analyze_checked_add(counts->conditional_branches, branch_count,
                     &counts->conditional_branches)) {
        return scxml_analyze_fail(
            build, SCXML_LIMIT_EXCEEDED,
            scxml_syntax_node_location(node),
            "conditional branch count overflow");
    }
    return SCXML_OK;
}

static scxml_status analyze_executable_content(
    scxml_build *build, scxml_syntax_node node, scxml_counts *counts,
    size_t conditional_depth) {
    size_t index;
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        scxml_element_kind child_kind;
        scxml_status status;
        if (scxml_syntax_node_type(child) == SALTS_XML_COMMENT ||
            (scxml_syntax_node_type(child) == SALTS_XML_TEXT &&
             is_xml_whitespace(scxml_syntax_node_value(child))))
            continue;
        if (scxml_syntax_node_type(child) != SALTS_XML_ELEMENT) {
            return scxml_analyze_fail(build, SCXML_UNSUPPORTED_FEATURE,
                              scxml_syntax_node_location(child),
                              "executable block text content is not supported");
        }
        if (!scxml_analyze_view_equal_raw(
                scxml_syntax_node_namespace_uri(child), SCXML_NAMESPACE)) {
            status = analyze_custom_action(build, child, counts);
            if (status != SCXML_OK) return status;
            continue;
        }
        status = require_scxml_element(build, child, &child_kind);
        if (status != SCXML_OK) return status;
        if (child_kind == SCXML_ELEMENT_RAISE) {
            status = analyze_raise(build, child, counts);
        } else if (child_kind == SCXML_ELEMENT_SEND) {
            status = analyze_send(build, child, counts);
        } else if (child_kind == SCXML_ELEMENT_CANCEL) {
            status = analyze_cancel(build, child, counts);
        } else if (child_kind == SCXML_ELEMENT_LOG) {
            status = analyze_log(build, child, counts);
        } else if (child_kind == SCXML_ELEMENT_ASSIGN) {
            status = analyze_assign(build, child, counts);
        } else if (child_kind == SCXML_ELEMENT_SCRIPT) {
            status = analyze_script(build, child, counts, false);
        } else if (child_kind == SCXML_ELEMENT_IF) {
            size_t next_depth;
            if (!scxml_analyze_checked_add(conditional_depth, 1u, &next_depth)) {
                return scxml_analyze_fail(
                    build, SCXML_LIMIT_EXCEEDED,
                    scxml_syntax_node_location(child),
                    "conditional nesting depth overflow");
            }
            status = analyze_conditional(
                build, child, counts, next_depth);
        } else if (child_kind == SCXML_ELEMENT_FOREACH) {
            size_t next_depth;
            if (!scxml_analyze_checked_add(conditional_depth, 1u, &next_depth))
                return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                                  scxml_syntax_node_location(child),
                                  "foreach nesting depth overflow");
            status = analyze_foreach(
                build, child, counts, next_depth);
        } else if (child_kind == SCXML_ELEMENT_ELSEIF ||
                   child_kind == SCXML_ELEMENT_ELSE) {
            return scxml_analyze_fail(
                build, SCXML_INVALID_STRUCTURE,
                scxml_syntax_node_location(child),
                "elseif and else are legal only inside if");
        } else {
            return scxml_analyze_fail(build, SCXML_UNSUPPORTED_FEATURE,
                              scxml_syntax_node_location(child),
                              "unsupported executable element");
        }
        if (status != SCXML_OK) return status;
    }
    return SCXML_OK;
}

static scxml_status analyze_executable_block(
    scxml_build *build, scxml_syntax_node node, scxml_counts *counts,
    bool finalize_block, bool *out_nonempty) {
    const size_t first_step = counts->executable_steps;
    scxml_status status;
    *out_nonempty = false;
    status = analyze_executable_content(
        build, node, counts, 0u);
    if (status != SCXML_OK) return status;
    if (counts->executable_steps != first_step &&
        (!scxml_analyze_checked_add(counts->block_rows, 1u, &counts->block_rows) ||
         (!finalize_block &&
          !scxml_analyze_checked_add(counts->executable_blocks, 1u,
                       &counts->executable_blocks)))) {
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "executable block count overflow");
    }
    *out_nonempty = counts->executable_steps != first_step;
    return SCXML_OK;
}

static scxml_status analyze_transition(scxml_build *build,
                                             scxml_syntax_node node,
                                             bool require_default,
                                             scxml_counts *counts) {
    scxml_syntax_attribute event_attribute;
    scxml_syntax_attribute target_attribute;
    scxml_syntax_attribute condition_attribute;
    salts_xml_string_view value;
    salts_xml_string_view token;
    size_t cursor = 0u;
    size_t token_count = 0u;
    size_t target_count = 0u;
    size_t descriptor_count = 0u;
    size_t match_all_count = 0u;
    bool nonempty = false;
    scxml_status status;

    status = scxml_analyze_validate_element_attributes(build, node, SCXML_ELEMENT_TRANSITION);
    if (status != SCXML_OK) return status;
    event_attribute = scxml_analyze_find_attribute(node, "event");
    target_attribute = scxml_analyze_find_attribute(node, "target");
    condition_attribute = scxml_analyze_find_attribute(node, "cond");
    if (require_default && event_attribute.impl != NULL) {
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_attribute_location(event_attribute),
                          "initial and history defaults must be eventless");
    }
    if (require_default && target_attribute.impl == NULL) {
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "initial and history defaults require one target");
    }
    if (require_default && condition_attribute.impl != NULL) {
        return scxml_analyze_fail(
            build, SCXML_INVALID_STRUCTURE,
            scxml_syntax_attribute_location(condition_attribute),
            "initial and history default transitions cannot have cond");
    }
    if (condition_attribute.impl != NULL &&
        build->data_model == SCXML_DATA_MODEL_NULL) {
        salts_xml_string_view state_name;
        if (!scxml_analyze_parse_null_in_condition(
                scxml_syntax_attribute_value(condition_attribute), &state_name)) {
            return scxml_analyze_fail(
                build, SCXML_INVALID_STRUCTURE,
                scxml_syntax_attribute_location(condition_attribute),
                "null-model transition condition must be In(id)");
        }
    }
    if (target_attribute.impl != NULL) {
        value = scxml_syntax_attribute_value(target_attribute);
        cursor = 0u;
        while (scxml_analyze_token_next(value, &cursor, &token)) ++target_count;
        if (target_count == 0u) {
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_attribute_location(target_attribute),
                              "transition target must contain an IDREF");
        }
    }
    token_count = 0u;
    if (event_attribute.impl != NULL) {
        value = scxml_syntax_attribute_value(event_attribute);
        cursor = 0u;
        while (scxml_analyze_token_next(value, &cursor, &token)) {
            salts_xml_string_view completed = {NULL, 0u};
            salts_xml_string_view descriptor_base = {NULL, 0u};
            bool match_all = false;
            ++token_count;
            if (scxml_analyze_completion_token(token, &completed)) continue;
            if (!normalize_event_descriptor(
                    token, &descriptor_base, &match_all)) {
                return scxml_analyze_fail(
                    build, SCXML_INVALID_STRUCTURE,
                    scxml_syntax_attribute_location(event_attribute),
                    "event descriptor wildcard must be '*' or a trailing '.*'");
            }
            ++descriptor_count;
            if (match_all &&
                !scxml_analyze_checked_add(
                    match_all_count, 1u, &match_all_count)) {
                return scxml_analyze_fail(
                    build, SCXML_LIMIT_EXCEEDED,
                    scxml_syntax_attribute_location(event_attribute),
                    "wildcard Event descriptor count overflow");
            }
            if (!match_all &&
                !scxml_analyze_checked_add(counts->event_occurrences, 1u,
                             &counts->event_occurrences)) {
                return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                                  scxml_syntax_attribute_location(event_attribute),
                                  "event occurrence count overflow");
            }
        }
        if (token_count == 0u) {
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_attribute_location(event_attribute),
                              "event attribute must contain a descriptor");
        }
    } else {
        token_count = 1u;
    }
    if (!scxml_analyze_checked_add(counts->event_descriptor_rows, descriptor_count,
                     &counts->event_descriptor_rows)) {
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "event descriptor count overflow");
    }
    if (!scxml_analyze_checked_add(
            counts->external_unmatched_transition_rows, match_all_count,
            &counts->external_unmatched_transition_rows)) {
        return scxml_analyze_fail(
            build, SCXML_LIMIT_EXCEEDED,
            scxml_syntax_node_location(node),
            "private wildcard transition count overflow");
    }
    if (!scxml_analyze_checked_add(counts->transition_rows, token_count,
                     &counts->transition_rows)) {
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "transition count overflow");
    }
    if (target_count != 0u) {
        size_t emitted_target_count;
        if (!scxml_analyze_checked_multiply(target_count, token_count,
                              &emitted_target_count) ||
            !scxml_analyze_checked_add(counts->transition_target_rows,
                         emitted_target_count,
                         &counts->transition_target_rows)) {
            return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                              scxml_syntax_attribute_location(target_attribute),
                              "transition target count overflow");
        }
        if (!scxml_analyze_checked_multiply(target_count, descriptor_count,
                              &emitted_target_count) ||
            !scxml_analyze_checked_add(counts->event_descriptor_target_rows,
                         emitted_target_count,
                         &counts->event_descriptor_target_rows)) {
            return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                              scxml_syntax_attribute_location(target_attribute),
                              "descriptor target count overflow");
        }
        if (!scxml_analyze_checked_multiply(
                target_count, match_all_count, &emitted_target_count) ||
            !scxml_analyze_checked_add(
                counts->external_unmatched_target_rows,
                emitted_target_count,
                &counts->external_unmatched_target_rows)) {
            return scxml_analyze_fail(
                build, SCXML_LIMIT_EXCEEDED,
                scxml_syntax_attribute_location(target_attribute),
                "private wildcard target count overflow");
        }
    }
    if (condition_attribute.impl != NULL &&
        !scxml_analyze_checked_add(counts->guard_rows, token_count,
                     &counts->guard_rows)) {
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_attribute_location(condition_attribute),
                          "transition guard count overflow");
    }
    if (condition_attribute.impl != NULL &&
        !scxml_analyze_checked_add(counts->event_descriptor_guard_rows,
                     descriptor_count,
                     &counts->event_descriptor_guard_rows)) {
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_attribute_location(condition_attribute),
                          "descriptor guard count overflow");
    }
    if (condition_attribute.impl != NULL &&
        !scxml_analyze_checked_add(
            counts->external_unmatched_guard_rows, match_all_count,
            &counts->external_unmatched_guard_rows)) {
        return scxml_analyze_fail(
            build, SCXML_LIMIT_EXCEEDED,
            scxml_syntax_attribute_location(condition_attribute),
            "private wildcard guard count overflow");
    }
    status = analyze_executable_block(
        build, node, counts, false, &nonempty);
    if (status != SCXML_OK) return status;
    if (nonempty &&
        !scxml_analyze_checked_add(counts->transition_action_rows, token_count,
                     &counts->transition_action_rows)) {
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "transition action count overflow");
    }
    if (nonempty &&
        !scxml_analyze_checked_add(counts->event_descriptor_action_rows,
                     descriptor_count,
                     &counts->event_descriptor_action_rows)) {
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "descriptor action count overflow");
    }
    if (nonempty &&
        !scxml_analyze_checked_add(
            counts->external_unmatched_action_rows, match_all_count,
            &counts->external_unmatched_action_rows)) {
        return scxml_analyze_fail(
            build, SCXML_LIMIT_EXCEEDED,
            scxml_syntax_node_location(node),
            "private wildcard action count overflow");
    }
    return SCXML_OK;
}

static size_t decimal_digits(size_t value) {
    size_t digits = 1u;
    while (value >= 10u) {
        value /= 10u;
        ++digits;
    }
    return digits;
}

static scxml_status analyze_invoke(
    scxml_build *build, scxml_syntax_node node,
    salts_xml_string_view owner_id, size_t ordinal,
    scxml_counts *counts) {
    static const size_t generated_separator_size =
        sizeof(".invoke.") - 1u;
    static const size_t done_prefix_size =
        sizeof("done.invoke.") - 1u;
    static const size_t max_token_digits =
        sizeof("18446744073709551615") - 1u;
    const scxml_syntax_attribute id_attribute = scxml_analyze_find_attribute(node, "id");
    const scxml_syntax_attribute type_attribute = scxml_analyze_find_attribute(node, "type");
    const scxml_syntax_attribute src_attribute = scxml_analyze_find_attribute(node, "src");
    const scxml_syntax_attribute autoforward_attribute =
        scxml_analyze_find_attribute(node, "autoforward");
    const scxml_syntax_attribute type_expr_attribute =
        scxml_analyze_find_attribute(node, "typeexpr");
    const scxml_syntax_attribute src_expr_attribute =
        scxml_analyze_find_attribute(node, "srcexpr");
    const scxml_syntax_attribute idlocation_attribute =
        scxml_analyze_find_attribute(node, "idlocation");
    const scxml_syntax_attribute namelist_attribute =
        scxml_analyze_find_attribute(node, "namelist");
    const salts_xml_string_view id = id_attribute.impl != NULL
        ? scxml_syntax_attribute_value(id_attribute)
        : (salts_xml_string_view){NULL, 0u};
    const salts_xml_string_view type = type_attribute.impl != NULL
        ? scxml_syntax_attribute_value(type_attribute)
        : (salts_xml_string_view){NULL, 0u};
    const salts_xml_string_view src = src_attribute.impl != NULL
        ? scxml_syntax_attribute_value(src_attribute)
        : (salts_xml_string_view){NULL, 0u};
    size_t id_size = id.size;
    size_t done_size;
    size_t dynamic_id_size = 0u;
    size_t dynamic_done_size = 0u;
    size_t index;
    size_t finalize_count = 0u;
    size_t content_count = 0u;
    bool rich_content = false;
    size_t param_count = 0u;
    size_t payload_count = 0u;
    size_t token_cursor = 0u;
    salts_xml_string_view token;
    scxml_location compiled_id_location = {0};
    scxml_status status = scxml_analyze_validate_element_attributes(
        build, node, SCXML_ELEMENT_INVOKE);
    if (status != SCXML_OK) return status;
    if (id_attribute.impl != NULL && idlocation_attribute.impl != NULL)
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "invoke id and idlocation are mutually exclusive");
    if ((type_expr_attribute.impl != NULL ||
         src_expr_attribute.impl != NULL ||
         idlocation_attribute.impl != NULL ||
         namelist_attribute.impl != NULL) &&
        build->data_model != SCXML_DATA_MODEL_CMETA)
        return scxml_analyze_fail(build, SCXML_UNSUPPORTED_FEATURE,
                          scxml_syntax_node_location(node),
                          "invoke expressions require the CMeta data model");
    if ((type_attribute.impl != NULL && type_expr_attribute.impl != NULL) ||
        (src_attribute.impl != NULL && src_expr_attribute.impl != NULL))
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "invoke literal and expression attributes are mutually exclusive");
    if (idlocation_attribute.impl != NULL) {
        status = scxml_emit_compile_cmeta_owned_string_location(
            build, idlocation_attribute, "invoke idlocation",
            &compiled_id_location);
        if (status != SCXML_OK) return status;
        if (!scxml_analyze_checked_add(owner_id.size, 1u, &dynamic_id_size) ||
            !scxml_analyze_checked_add(dynamic_id_size, max_token_digits,
                         &dynamic_id_size) ||
            dynamic_id_size > SCXML_EVENT_METADATA_CAPACITY ||
            !scxml_analyze_checked_add(done_prefix_size, dynamic_id_size,
                         &dynamic_done_size) ||
            dynamic_done_size > SCXML_EVENT_METADATA_CAPACITY) {
            return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                              scxml_syntax_attribute_location(
                                  idlocation_attribute),
                              "dynamic invoke id or done Event exceeds metadata limit");
        }
    }
    if (id_attribute.impl != NULL && !scxml_analyze_is_xml_ncname(id)) {
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_attribute_location(id_attribute),
                          "invoke id must be an XML NCName");
    }
    if (id_attribute.impl == NULL &&
        (!scxml_analyze_checked_add(owner_id.size, generated_separator_size, &id_size) ||
         !scxml_analyze_checked_add(id_size, decimal_digits(ordinal), &id_size))) {
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "generated invoke id size overflow");
    }
    if ((type_attribute.impl != NULL && type.size == 0u) ||
        (src_attribute.impl != NULL && src.size == 0u)) {
        return scxml_analyze_fail(
            build, SCXML_INVALID_STRUCTURE,
            scxml_syntax_attribute_location(
                type_attribute.impl != NULL && type.size == 0u
                    ? type_attribute : src_attribute),
            "invoke literal type and src must be nonempty");
    }
    if (autoforward_attribute.impl != NULL) {
        const salts_xml_string_view value =
            scxml_syntax_attribute_value(autoforward_attribute);
        if (!scxml_analyze_view_equal_raw(value, "true") &&
            !scxml_analyze_view_equal_raw(value, "false")) {
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_attribute_location(
                                  autoforward_attribute),
                              "invoke autoforward must be true or false");
        }
    }
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        scxml_element_kind child_kind;
        bool nonempty = false;
        if (scxml_syntax_node_type(child) == SALTS_XML_COMMENT ||
            (scxml_syntax_node_type(child) == SALTS_XML_TEXT &&
             is_xml_whitespace(scxml_syntax_node_value(child))))
            continue;
        if (scxml_syntax_node_type(child) != SALTS_XML_ELEMENT) {
            return scxml_analyze_fail(build, SCXML_UNSUPPORTED_FEATURE,
                              scxml_syntax_node_location(child),
                              "invoke text content is not supported");
        }
        status = require_scxml_element(build, child, &child_kind);
        if (status != SCXML_OK) return status;
        if (child_kind == SCXML_ELEMENT_PARAM) {
            scxml_syntax_attribute name;
            status = analyze_param(build, child);
            if (status != SCXML_OK) return status;
            name = scxml_analyze_find_attribute(child, "name");
            if (!count_retained_view(
                    scxml_syntax_attribute_value(name),
                    &counts->invocation_string_bytes) ||
                !scxml_analyze_checked_add(param_count, 1u, &param_count))
                return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                                  scxml_syntax_node_location(child),
                                  "invoke param storage size overflow");
            continue;
        }
        if (child_kind == SCXML_ELEMENT_CONTENT) {
            const scxml_syntax_attribute expression =
                scxml_analyze_find_attribute(child, "expr");
            size_t content_size = 0u;
            size_t retained = 0u;
            scxml_content_kind content_kind;
            status = scxml_analyze_validate_element_attributes(
                build, child, SCXML_ELEMENT_CONTENT);
            if (status != SCXML_OK) return status;
            if (++content_count > 1u)
                return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                                  scxml_syntax_node_location(child),
                                  "invoke may contain at most one content child");
            status = scxml_analyze_inspect_inline_content(
                build, child, &content_kind, &content_size);
            if (status != SCXML_OK) return status;
            if (expression.impl != NULL) {
                if (build->data_model != SCXML_DATA_MODEL_CMETA ||
                    is_empty_view(scxml_syntax_attribute_value(expression)))
                    return scxml_analyze_fail(
                        build, SCXML_UNSUPPORTED_FEATURE,
                        scxml_syntax_node_location(child),
                        "invoke content expr requires the CMeta data model");
                if (content_size != 0u)
                    return scxml_analyze_fail(
                        build, SCXML_INVALID_STRUCTURE,
                        scxml_syntax_node_location(child),
                        "invoke content expr cannot have inline children");
                rich_content = content_expression_is_rich(
                    build, expression);
            } else {
                if (!scxml_analyze_checked_add(content_size, 1u, &retained) ||
                    !scxml_analyze_checked_add(counts->invocation_string_bytes, retained,
                                 &counts->invocation_string_bytes))
                    return scxml_analyze_fail(
                        build, SCXML_LIMIT_EXCEEDED,
                        scxml_syntax_node_location(child),
                        "invoke inline content storage size overflow");
                rich_content = true;
            }
            continue;
        }
        if (child_kind != SCXML_ELEMENT_FINALIZE) {
            return scxml_analyze_fail(build, SCXML_UNSUPPORTED_FEATURE,
                              scxml_syntax_node_location(child),
                              "invoke parameters and content are not supported");
        }
        if (++finalize_count > 1u) {
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_node_location(child),
                              "invoke may contain at most one finalize");
        }
        status = analyze_executable_block(
            build, child, counts, true, &nonempty);
        if (status != SCXML_OK) return status;
    }
    if (content_count != 0u &&
        (src_attribute.impl != NULL || src_expr_attribute.impl != NULL ||
         namelist_attribute.impl != NULL || param_count != 0u))
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "invoke content is mutually exclusive with src, namelist, and param");
    if (namelist_attribute.impl != NULL && param_count != 0u)
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "invoke namelist and param are mutually exclusive");
    if (namelist_attribute.impl != NULL) {
        const salts_xml_string_view namelist =
            scxml_syntax_attribute_value(namelist_attribute);
        while (scxml_analyze_token_next(namelist, &token_cursor, &token)) {
            if (!scxml_analyze_checked_add(payload_count, 1u, &payload_count) ||
                !count_retained_view(
                    token, &counts->invocation_string_bytes))
                return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                                  scxml_syntax_attribute_location(
                                      namelist_attribute),
                                  "invoke namelist storage size overflow");
        }
        if (payload_count == 0u)
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_attribute_location(
                                  namelist_attribute),
                              "invoke namelist must contain one location");
    }
    if (!scxml_analyze_checked_add(payload_count, param_count, &payload_count) ||
        payload_count > SCXML_PAYLOAD_MAX_ENTRIES ||
        !scxml_analyze_checked_add(counts->payload_rows, payload_count,
                     &counts->payload_rows))
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "invoke payload entry limit exceeded");
    if (payload_count > counts->max_payload_entries)
        counts->max_payload_entries = payload_count;
    if (!scxml_analyze_checked_add(done_prefix_size, id_size, &done_size) ||
        !scxml_analyze_checked_add(counts->invocation_rows, 1u,
                     &counts->invocation_rows) ||
        !scxml_analyze_checked_add(counts->event_occurrences, 1u,
                     &counts->event_occurrences) ||
        !scxml_analyze_checked_add(counts->executable_steps, 2u,
                     &counts->executable_steps) ||
        !scxml_analyze_checked_add(counts->executable_blocks, 2u,
                     &counts->executable_blocks) ||
        !scxml_analyze_checked_add(counts->block_rows, 2u,
                     &counts->block_rows) ||
        !scxml_analyze_checked_add(counts->state_action_rows, 2u,
                     &counts->state_action_rows) ||
        !scxml_analyze_checked_add(id_size, 1u, &id_size) ||
        !scxml_analyze_checked_add(counts->invocation_string_bytes, id_size,
                     &counts->invocation_string_bytes) ||
        !scxml_analyze_checked_add(done_size, 1u, &done_size) ||
        !scxml_analyze_checked_add(counts->invocation_string_bytes, done_size,
                     &counts->invocation_string_bytes) ||
        (type_attribute.impl != NULL &&
         !count_retained_view(type, &counts->invocation_string_bytes)) ||
        (src_attribute.impl != NULL &&
         !count_retained_view(src, &counts->invocation_string_bytes))) {
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "invoke descriptor storage size overflow");
    }
    if ((type_expr_attribute.impl != NULL || src_expr_attribute.impl != NULL ||
         content_count != 0u || payload_count != 0u) &&
        !scxml_analyze_checked_add(counts->dynamic_expression_rows, 1u,
                     &counts->dynamic_expression_rows))
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "invoke expression count overflow");
    counts->requirements |= SCXML_REQUIREMENT_INVOKE;
    if (idlocation_attribute.impl != NULL)
        counts->requirements |= SCXML_REQUIREMENT_INVOKE_IDLOCATION;
    if ((content_count != 0u && !rich_content) ||
        payload_count != 0u)
        counts->requirements |= SCXML_REQUIREMENT_INVOKE_PAYLOAD;
    if (rich_content)
        counts->requirements |= SCXML_REQUIREMENT_INVOKE_CONTENT;
    return SCXML_OK;
}

static scxml_status analyze_datamodel(
    scxml_build *build, scxml_syntax_node node, bool top_level,
    scxml_counts *counts) {
    size_t index;
    size_t data_count = 0u;
    scxml_status status = scxml_analyze_validate_element_attributes(
        build, node, SCXML_ELEMENT_DATAMODEL);
    if (status != SCXML_OK) return status;
    if (build->data_model != SCXML_DATA_MODEL_CMETA)
        return scxml_analyze_fail(build, SCXML_UNSUPPORTED_FEATURE,
                          scxml_syntax_node_location(node),
                          "data declarations require the CMeta data model");
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        scxml_syntax_attribute id;
        scxml_syntax_attribute expression;
        scxml_syntax_attribute source;
        scxml_content_kind content_kind;
        size_t content_size = 0u;
        size_t child_index;
        if (scxml_syntax_node_type(child) == SALTS_XML_COMMENT ||
            (scxml_syntax_node_type(child) == SALTS_XML_TEXT &&
             is_xml_whitespace(scxml_syntax_node_value(child))))
            continue;
        if (scxml_syntax_node_type(child) != SALTS_XML_ELEMENT ||
            scxml_analyze_element_kind(child) != SCXML_ELEMENT_DATA)
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_node_location(child),
                              "datamodel accepts only data children");
        id = scxml_analyze_find_attribute(child, "id");
        expression = scxml_analyze_find_attribute(child, "expr");
        source = scxml_analyze_find_attribute(child, "src");
        status = scxml_analyze_validate_element_attributes(
            build, child, SCXML_ELEMENT_DATA);
        if (status != SCXML_OK) return status;
        status = scxml_analyze_inspect_inline_content(
            build, child, &content_kind, &content_size);
        if (status != SCXML_OK) return status;
        if (id.impl == NULL ||
            is_empty_view(scxml_syntax_attribute_value(id)) ||
            (expression.impl == NULL && source.impl == NULL &&
             content_size == 0u) ||
            (expression.impl != NULL &&
             is_empty_view(scxml_syntax_attribute_value(expression))) ||
            (source.impl != NULL &&
             is_empty_view(scxml_syntax_attribute_value(source))))
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_node_location(child),
                              "CMeta data requires a nonempty id and exactly one of expr, src, or inline content");
        if (expression.impl != NULL && source.impl != NULL)
            return scxml_analyze_fail(
                build, SCXML_INVALID_STRUCTURE,
                scxml_syntax_node_location(child),
                "CMeta data cannot combine expr and src");
        if ((expression.impl != NULL || source.impl != NULL) &&
            content_size != 0u) {
            for (child_index = 0u;
                 child_index < scxml_syntax_node_child_count(child);
                 ++child_index) {
                const scxml_syntax_node content =
                    scxml_syntax_node_child_at(child, child_index);
                if (scxml_syntax_node_type(content) == SALTS_XML_COMMENT ||
                    (scxml_syntax_node_type(content) == SALTS_XML_TEXT &&
                     is_xml_whitespace(scxml_syntax_node_value(content))))
                    continue;
                return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                                  scxml_syntax_node_location(content),
                                  expression.impl != NULL
                                      ? "CMeta data expr cannot have child content"
                                      : "CMeta data src cannot have child content");
            }
        }
        if (!scxml_analyze_checked_add(counts->assignment_rows, 1u,
                         &counts->assignment_rows) ||
            !scxml_analyze_checked_add(counts->data_initializer_rows, 1u,
                         &counts->data_initializer_rows) ||
            (top_level &&
             !scxml_analyze_checked_add(
                 counts->top_level_data_initializer_rows, 1u,
                 &counts->top_level_data_initializer_rows)))
            return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                              scxml_syntax_node_location(child),
                              "CMeta data initializer count overflow");
        if (!scxml_analyze_checked_add(data_count, 1u, &data_count))
            return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                              scxml_syntax_node_location(child),
                              "CMeta data declaration count overflow");
        if (source.impl != NULL)
            counts->requirements |= SCXML_REQUIREMENT_DATA_RESOURCE;
    }
    if (build->late_binding && data_count != 0u) {
        if (!scxml_analyze_checked_add(counts->late_initializer_rows, 1u,
                         &counts->late_initializer_rows) ||
            !scxml_analyze_checked_add(counts->executable_blocks, 1u,
                         &counts->executable_blocks) ||
            !scxml_analyze_checked_add(counts->block_rows, 1u,
                         &counts->block_rows) ||
            !scxml_analyze_checked_add(counts->executable_steps, 1u,
                         &counts->executable_steps) ||
            !scxml_analyze_checked_add(counts->state_action_rows, 1u,
                         &counts->state_action_rows)) {
            return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                              scxml_syntax_node_location(node),
                              "late data initializer storage overflow");
        }
        counts->requirements |= SCXML_REQUIREMENT_LATE_BINDING;
    }
    return SCXML_OK;
}

scxml_status scxml_analyze_state(scxml_build *build,
                                        scxml_syntax_node node,
                                        scxml_element_kind kind,
                                        bool is_root,
                                        scxml_counts *counts) {
    const scxml_syntax_attribute id_attribute = scxml_analyze_find_attribute(node, "id");
    const scxml_syntax_attribute initial_attribute = scxml_analyze_find_attribute(node, "initial");
    const size_t real_children =
        scxml_analyze_element_child_count(node, SCXML_ELEMENT_STATE) +
        scxml_analyze_element_child_count(node, SCXML_ELEMENT_PARALLEL) +
        scxml_analyze_element_child_count(node, SCXML_ELEMENT_FINAL);
    const size_t explicit_initials =
        scxml_analyze_element_child_count(node, SCXML_ELEMENT_INITIAL);
    const bool compound = is_root ||
        (kind == SCXML_ELEMENT_STATE && real_children != 0u);
    size_t index;
    size_t invoke_ordinal = 0u;
    scxml_status status;

    if (scxml_analyze_element_child_count(node, SCXML_ELEMENT_DATAMODEL) > 1u)
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "a state may contain at most one datamodel");

    if (!scxml_analyze_checked_add(counts->state_rows, 1u, &counts->state_rows) ||
        !scxml_analyze_checked_add(counts->node_refs, 1u, &counts->node_refs)) {
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          scxml_syntax_node_location(node),
                          "state count overflow");
    }
    if (id_attribute.impl != NULL &&
        !scxml_analyze_is_xml_ncname(
            scxml_syntax_attribute_value(id_attribute))) {
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_attribute_location(id_attribute),
                          "SCXML state id must be an XML NCName");
    }
    if (is_root) {
        if (id_attribute.impl != NULL) {
            if (!scxml_analyze_checked_add(counts->state_names, 1u,
                             &counts->state_names)) {
                return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                                  scxml_syntax_attribute_location(id_attribute),
                                  "state name count overflow");
            }
        }
    } else if (kind != SCXML_ELEMENT_INITIAL) {
        if (id_attribute.impl == NULL ||
            is_empty_view(scxml_syntax_attribute_value(id_attribute))) {
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_node_location(node),
                              "supported SCXML states require a nonempty id");
        }
        if (!scxml_analyze_checked_add(counts->state_names, 1u, &counts->state_names)) {
            return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                              scxml_syntax_attribute_location(id_attribute),
                              "state name count overflow");
        }
    }
    if (kind == SCXML_ELEMENT_HISTORY) {
        const scxml_syntax_attribute type_attribute = scxml_analyze_find_attribute(node, "type");
        if (type_attribute.impl != NULL) {
            const salts_xml_string_view type =
                scxml_syntax_attribute_value(type_attribute);
            if (!scxml_analyze_view_equal_raw(type, "shallow") &&
                !scxml_analyze_view_equal_raw(type, "deep")) {
                return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                                  scxml_syntax_attribute_location(type_attribute),
                                  "history type must be shallow or deep");
            }
        }
    }
    if ((kind == SCXML_ELEMENT_PARALLEL && real_children == 0u) ||
        (is_root && real_children == 0u)) {
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "compound and parallel states require real children");
    }
    if (kind == SCXML_ELEMENT_STATE && !compound &&
        (explicit_initials != 0u || initial_attribute.impl != NULL ||
         scxml_analyze_element_child_count(node, SCXML_ELEMENT_HISTORY) != 0u)) {
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "atomic states cannot declare initial or history children");
    }
    if (kind == SCXML_ELEMENT_PARALLEL &&
        (explicit_initials != 0u || initial_attribute.impl != NULL)) {
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "parallel states do not declare one initial child");
    }
    if (compound) {
        if (explicit_initials > 1u ||
            (explicit_initials != 0u && initial_attribute.impl != NULL)) {
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_node_location(node),
                              "compound states require one initial declaration");
        }
        if (explicit_initials == 0u) {
            size_t initial_target_count = 1u;
            if (initial_attribute.impl != NULL) {
                const salts_xml_string_view initial_value =
                    scxml_syntax_attribute_value(initial_attribute);
                salts_xml_string_view initial_token;
                size_t initial_cursor = 0u;
                initial_target_count = 0u;
                while (scxml_analyze_token_next(initial_value, &initial_cursor,
                                  &initial_token))
                    ++initial_target_count;
                if (initial_target_count == 0u) {
                    return scxml_analyze_fail(
                        build, SCXML_INVALID_STRUCTURE,
                        scxml_syntax_attribute_location(initial_attribute),
                        "initial must contain an IDREF");
                }
            }
            if (!scxml_analyze_checked_add(counts->state_rows, 1u, &counts->state_rows) ||
                !scxml_analyze_checked_add(counts->synthetic_initials, 1u,
                             &counts->synthetic_initials) ||
                !scxml_analyze_checked_add(counts->transition_rows, 1u,
                             &counts->transition_rows) ||
                !scxml_analyze_checked_add(counts->transition_target_rows,
                             initial_target_count,
                             &counts->transition_target_rows)) {
                return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                                  scxml_syntax_node_location(node),
                                  "synthetic initial count overflow");
            }
        }
    }

    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        scxml_element_kind child_kind;
        if (scxml_syntax_node_type(child) == SALTS_XML_COMMENT) continue;
        if (scxml_syntax_node_type(child) != SALTS_XML_ELEMENT) {
            return scxml_analyze_fail(build, SCXML_UNSUPPORTED_FEATURE,
                              scxml_syntax_node_location(child),
                              "non-whitespace SCXML text is not supported");
        }
        status = require_scxml_element(build, child, &child_kind);
        if (status != SCXML_OK) return status;
        if (scxml_analyze_is_state_element(child_kind)) {
            if (kind == SCXML_ELEMENT_FINAL || kind == SCXML_ELEMENT_INITIAL ||
                kind == SCXML_ELEMENT_HISTORY) {
                return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                                  scxml_syntax_node_location(child),
                                  "this SCXML element cannot contain states");
            }
            status = scxml_analyze_state(build, child, child_kind, false, counts);
        } else if (child_kind == SCXML_ELEMENT_INITIAL) {
            if (!compound) {
                return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                                  scxml_syntax_node_location(child),
                                  "initial must be a child of a compound state");
            }
            status = scxml_analyze_state(build, child, child_kind, false, counts);
        } else if (child_kind == SCXML_ELEMENT_HISTORY) {
            if (!(compound || kind == SCXML_ELEMENT_PARALLEL)) {
                return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                                  scxml_syntax_node_location(child),
                                  "history must be a child of compound or parallel");
            }
            status = scxml_analyze_state(build, child, child_kind, false, counts);
        } else if (child_kind == SCXML_ELEMENT_TRANSITION) {
            if (is_root || kind == SCXML_ELEMENT_FINAL) {
                return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                                  scxml_syntax_node_location(child),
                                  "this SCXML element cannot contain transitions");
            }
            status = analyze_transition(
                build, child,
                kind == SCXML_ELEMENT_INITIAL || kind == SCXML_ELEMENT_HISTORY,
                counts);
        } else if (child_kind == SCXML_ELEMENT_ONENTRY ||
                   child_kind == SCXML_ELEMENT_ONEXIT) {
            bool nonempty = false;
            if (kind == SCXML_ELEMENT_INITIAL || kind == SCXML_ELEMENT_HISTORY ||
                is_root) {
                return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                                  scxml_syntax_node_location(child),
                                  "onentry/onexit is not allowed at this location");
            }
            status = analyze_executable_block(
                build, child, counts, false, &nonempty);
            if (status == SCXML_OK && nonempty &&
                !scxml_analyze_checked_add(counts->state_action_rows, 1u,
                             &counts->state_action_rows)) {
                status = scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                                    scxml_syntax_node_location(child),
                                    "state action count overflow");
            }
        } else if (child_kind == SCXML_ELEMENT_INVOKE) {
            if (is_root || kind == SCXML_ELEMENT_FINAL ||
                kind == SCXML_ELEMENT_INITIAL ||
                kind == SCXML_ELEMENT_HISTORY) {
                return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                                  scxml_syntax_node_location(child),
                                  "invoke is allowed only in state or parallel");
            }
            ++invoke_ordinal;
            status = analyze_invoke(
                build, child, scxml_syntax_attribute_value(id_attribute),
                invoke_ordinal, counts);
        } else if (child_kind == SCXML_ELEMENT_DATAMODEL) {
            if (kind == SCXML_ELEMENT_FINAL ||
                kind == SCXML_ELEMENT_INITIAL ||
                kind == SCXML_ELEMENT_HISTORY)
                return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                                  scxml_syntax_node_location(child),
                                  "datamodel is allowed only in scxml, state, or parallel");
            status = analyze_datamodel(build, child, is_root, counts);
        } else if (child_kind == SCXML_ELEMENT_SCRIPT) {
            if (!is_root)
                return scxml_analyze_fail(
                    build, SCXML_INVALID_STRUCTURE,
                    scxml_syntax_node_location(child),
                    "script is executable content or a direct scxml child");
            status = analyze_script(build, child, counts, true);
        } else if (child_kind == SCXML_ELEMENT_DONEDATA) {
            if (kind != SCXML_ELEMENT_FINAL)
                return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                                  scxml_syntax_node_location(child),
                                  "donedata is allowed only inside final");
            if (scxml_analyze_element_child_count(node, SCXML_ELEMENT_DONEDATA) != 1u)
                return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                                  scxml_syntax_node_location(child),
                                  "final accepts at most one donedata child");
            status = analyze_done_data(build, child, counts);
        } else if (child_kind == SCXML_ELEMENT_FINALIZE) {
            return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                              scxml_syntax_node_location(child),
                              "finalize is allowed only inside invoke");
        } else {
            return scxml_analyze_fail(build, SCXML_UNSUPPORTED_FEATURE,
                              scxml_syntax_node_location(child),
                              "unsupported SCXML child element");
        }
        if (status != SCXML_OK) return status;
    }

    if ((kind == SCXML_ELEMENT_INITIAL || kind == SCXML_ELEMENT_HISTORY) &&
        scxml_analyze_element_child_count(node, SCXML_ELEMENT_TRANSITION) != 1u) {
        return scxml_analyze_fail(build, SCXML_INVALID_STRUCTURE,
                          scxml_syntax_node_location(node),
                          "initial and history require exactly one transition");
    }
    return SCXML_OK;
}

static cflow_statechart_state_kind native_state_kind(scxml_syntax_node node,
                                                      bool is_root) {
    const scxml_element_kind kind = scxml_analyze_element_kind(node);
    if (is_root) return CFLOW_STATECHART_COMPOUND;
    if (kind == SCXML_ELEMENT_PARALLEL) return CFLOW_STATECHART_PARALLEL;
    if (kind == SCXML_ELEMENT_FINAL) return CFLOW_STATECHART_FINAL;
    if (kind == SCXML_ELEMENT_INITIAL) return CFLOW_STATECHART_INITIAL;
    if (kind == SCXML_ELEMENT_HISTORY) {
        const scxml_syntax_attribute type = scxml_analyze_find_attribute(node, "type");
        return type.impl != NULL &&
                       scxml_analyze_view_equal_raw(scxml_syntax_attribute_value(type), "deep")
                   ? CFLOW_STATECHART_HISTORY_DEEP
                   : CFLOW_STATECHART_HISTORY_SHALLOW;
    }
    return first_real_child(node).impl != NULL ? CFLOW_STATECHART_COMPOUND
                                                : CFLOW_STATECHART_ATOMIC;
}

scxml_status scxml_analyze_emit_state(scxml_build *build,
                                     scxml_syntax_node node,
                                     cflow_machine_state_id parent,
                                     bool is_root) {
    const scxml_element_kind kind = scxml_analyze_element_kind(node);
    const cflow_machine_state_id id =
        (cflow_machine_state_id)(build->state_index + 1u);
    const cflow_statechart_state_kind native_kind =
        native_state_kind(node, is_root);
    const scxml_syntax_attribute id_attribute = scxml_analyze_find_attribute(node, "id");
    const scxml_syntax_attribute initial_attribute = scxml_analyze_find_attribute(node, "initial");
    const bool compound = native_kind == CFLOW_STATECHART_COMPOUND;
    const bool has_explicit_initial =
        scxml_analyze_element_child_count(node, SCXML_ELEMENT_INITIAL) != 0u;
    size_t index;

    build->states[build->state_index] = (cflow_statechart_state){
        id, parent, native_kind, (uint32_t)build->state_index};
    ++build->state_index;
    build->node_refs[build->node_ref_index++] =
        (scxml_node_ref){node.impl->id, id};
    if (id_attribute.impl != NULL) {
        build->state_names[build->state_name_index] = (scxml_name_ref){
            scxml_syntax_attribute_value(id_attribute),
            scxml_syntax_attribute_location(id_attribute), id,
            build->state_name_index};
        ++build->state_name_index;
    }

    if (compound && !has_explicit_initial) {
        scxml_syntax_node target_node = first_real_child(node);
        scxml_syntax_attribute target_id;
        salts_xml_string_view target;
        salts_xml_location location;
        const cflow_machine_state_id initial_id =
            (cflow_machine_state_id)(build->state_index + 1u);
        if (initial_attribute.impl != NULL) {
            target = scxml_syntax_attribute_value(initial_attribute);
            location = scxml_syntax_attribute_location(initial_attribute);
        } else {
            target_id = scxml_analyze_find_attribute(target_node, "id");
            target = scxml_syntax_attribute_value(target_id);
            location = scxml_syntax_node_location(target_node);
        }
        build->states[build->state_index] = (cflow_statechart_state){
            initial_id, id, CFLOW_STATECHART_INITIAL,
            (uint32_t)build->state_index};
        ++build->state_index;
        build->synthetic_initials[build->synthetic_index++] =
            (scxml_synthetic_initial){id, initial_id, target, location};
    }

    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        const scxml_element_kind child_kind = scxml_analyze_element_kind(child);
        if (scxml_syntax_node_type(child) == SALTS_XML_ELEMENT &&
            (scxml_analyze_is_state_element(child_kind) ||
             child_kind == SCXML_ELEMENT_INITIAL ||
             child_kind == SCXML_ELEMENT_HISTORY)) {
            scxml_status status = scxml_analyze_emit_state(build, child, id, false);
            if (status != SCXML_OK) return status;
        }
    }
    (void)kind;
    return SCXML_OK;
}

const scxml_name_ref *scxml_analyze_find_name_ref(const scxml_name_ref *names,
                                           size_t count,
                                           salts_xml_string_view name) {
    size_t low = 0u;
    size_t high = count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        const int compared = scxml_analyze_compare_view(names[middle].name, name);
        if (compared < 0) low = middle + 1u;
        else high = middle;
    }
    return low < count && view_equal(names[low].name, name) ? &names[low]
                                                             : NULL;
}

cflow_machine_state_id scxml_analyze_node_id(const scxml_build *build,
                                      scxml_syntax_node node,
                                      size_t node_count) {
    size_t low = 0u;
    size_t high = node_count;
    const scxml_ast_node_id wanted = node.impl->id;
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        const scxml_ast_node_id value = build->node_refs[middle].node_id;
        if (value < wanted) low = middle + 1u;
        else high = middle;
    }
    return low < node_count && build->node_refs[low].node_id == wanted
               ? build->node_refs[low].id
               : 0u;
}

static char *reserve_invocation_storage(scxml_build *build, size_t size) {
    char *result;
    if (size == 0u ||
        build->invocation_storage_index >
            build->invocation_storage_capacity ||
        size > build->invocation_storage_capacity -
            build->invocation_storage_index)
        return NULL;
    result = build->invocation_storage + build->invocation_storage_index;
    build->invocation_storage_index += size;
    return result;
}

static bool retain_invocation_view(
    scxml_build *build, salts_xml_string_view value,
    const char **out_data, size_t *out_size) {
    char *stored;
    size_t retained;
    *out_data = NULL;
    *out_size = 0u;
    if (value.data == NULL) return true;
    if (!scxml_analyze_checked_add(value.size, 1u, &retained) ||
        (stored = reserve_invocation_storage(build, retained)) == NULL)
        return false;
    if (value.size != 0u) memcpy(stored, value.data, value.size);
    stored[value.size] = '\0';
    *out_data = stored;
    *out_size = value.size;
    return true;
}

static scxml_status retain_invocation_inline_content(
    scxml_build *build, scxml_syntax_node node,
    scxml_content_descriptor *content) {
    size_t retained;
    size_t actual = 0u;
    char *stored;
    scxml_status status = scxml_analyze_inspect_inline_content(
        build, node, &content->kind, &content->byte_count);
    if (status != SCXML_OK) return status;
    if (!scxml_analyze_checked_add(content->byte_count, 1u, &retained) ||
        (stored = reserve_invocation_storage(build, retained)) == NULL)
        return scxml_analyze_fail(
            build, SCXML_NATIVE_IR_REJECTED,
            scxml_syntax_node_location(node),
            "invoke inline content storage mismatched admission");
    content->bytes = stored;
    if (scxml_syntax_serialize_children(
            node, stored, retained, build->limits.max_name_bytes,
            &actual) != SALTS_XML_OK || actual != content->byte_count)
        return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                          scxml_syntax_node_location(node),
                          "invoke inline content changed during emission");
    return SCXML_OK;
}

scxml_status scxml_analyze_emit_invocation_declarations(
    scxml_build *build, scxml_syntax_node node, size_t node_count) {
    static const char generated_separator[] = ".invoke.";
    static const char done_prefix[] = "done.invoke.";
    static const size_t max_token_digits =
        sizeof("18446744073709551615") - 1u;
    const cflow_machine_state_id owner = scxml_analyze_node_id(build, node, node_count);
    const scxml_syntax_attribute owner_id_attribute = scxml_analyze_find_attribute(node, "id");
    const salts_xml_string_view owner_id =
        owner_id_attribute.impl != NULL
            ? scxml_syntax_attribute_value(owner_id_attribute)
            : (salts_xml_string_view){NULL, 0u};
    size_t ordinal = 0u;
    size_t index;
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        const scxml_element_kind kind = scxml_analyze_element_kind(child);
        scxml_invocation_descriptor *descriptor;
        scxml_syntax_attribute id_attribute;
        scxml_syntax_attribute type_attribute;
        scxml_syntax_attribute type_expr_attribute;
        scxml_syntax_attribute src_attribute;
        scxml_syntax_attribute src_expr_attribute;
        scxml_syntax_attribute idlocation_attribute;
        scxml_syntax_attribute namelist_attribute;
        scxml_syntax_attribute autoforward_attribute;
        salts_xml_location id_location;
        char *generated;
        size_t generated_size;
        size_t retained_size;
        char ordinal_buffer[3u * sizeof(size_t) + 1u];
        int ordinal_size;
        char *done_name;
        scxml_status expression_status;
        if (scxml_syntax_node_type(child) != SALTS_XML_ELEMENT ||
            kind != SCXML_ELEMENT_INVOKE)
            continue;
        ++ordinal;
        if (build->invocation_index >= build->invocation_capacity) {
            return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                              scxml_syntax_node_location(child),
                              "invoke exceeded admitted descriptor storage");
        }
        descriptor = &build->invocations[build->invocation_index];
        id_attribute = scxml_analyze_find_attribute(child, "id");
        type_attribute = scxml_analyze_find_attribute(child, "type");
        type_expr_attribute = scxml_analyze_find_attribute(child, "typeexpr");
        src_attribute = scxml_analyze_find_attribute(child, "src");
        src_expr_attribute = scxml_analyze_find_attribute(child, "srcexpr");
        idlocation_attribute = scxml_analyze_find_attribute(child, "idlocation");
        namelist_attribute = scxml_analyze_find_attribute(child, "namelist");
        autoforward_attribute = scxml_analyze_find_attribute(child, "autoforward");
        if (id_attribute.impl != NULL) {
            if (!retain_invocation_view(
                    build, scxml_syntax_attribute_value(id_attribute),
                    &descriptor->id, &descriptor->id_size)) {
                return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                                  scxml_syntax_node_location(child),
                                  "invoke id storage mismatched admission");
            }
            id_location = scxml_syntax_attribute_location(id_attribute);
        } else {
            ordinal_size = snprintf(
                ordinal_buffer, sizeof(ordinal_buffer), "%zu", ordinal);
            if (ordinal_size <= 0 ||
                !scxml_analyze_checked_add(owner_id.size,
                             sizeof(generated_separator) - 1u,
                             &generated_size) ||
                !scxml_analyze_checked_add(generated_size, (size_t)ordinal_size,
                             &generated_size) ||
                !scxml_analyze_checked_add(generated_size, 1u, &retained_size) ||
                (generated = reserve_invocation_storage(
                     build, retained_size)) == NULL) {
                return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                                  scxml_syntax_node_location(child),
                                  "generated invoke id storage mismatched admission");
            }
            memcpy(generated, owner_id.data, owner_id.size);
            memcpy(generated + owner_id.size, generated_separator,
                   sizeof(generated_separator) - 1u);
            memcpy(generated + owner_id.size +
                       sizeof(generated_separator) - 1u,
                   ordinal_buffer, (size_t)ordinal_size);
            generated[generated_size] = '\0';
            descriptor->id = generated;
            descriptor->id_size = generated_size;
            id_location = scxml_syntax_node_location(child);
        }
        if (!retain_invocation_view(
                build,
                type_attribute.impl != NULL
                    ? scxml_syntax_attribute_value(type_attribute)
                    : (salts_xml_string_view){NULL, 0u},
                &descriptor->type, &descriptor->type_size) ||
            !retain_invocation_view(
                build,
                src_attribute.impl != NULL
                    ? scxml_syntax_attribute_value(src_attribute)
                    : (salts_xml_string_view){NULL, 0u},
                &descriptor->src, &descriptor->src_size) ||
            !scxml_analyze_checked_add(sizeof(done_prefix) - 1u, descriptor->id_size,
                         &generated_size) ||
            !scxml_analyze_checked_add(generated_size, 1u, &retained_size) ||
            (done_name = reserve_invocation_storage(
                 build, retained_size)) == NULL) {
            return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                              scxml_syntax_node_location(child),
                              "invoke literal storage mismatched admission");
        }
        memcpy(done_name, done_prefix, sizeof(done_prefix) - 1u);
        memcpy(done_name + sizeof(done_prefix) - 1u,
               descriptor->id, descriptor->id_size);
        done_name[generated_size] = '\0';
        descriptor->owner = owner;
        descriptor->owner_id_size = owner_id.size;
        descriptor->payload_first = build->payload_index;
        descriptor->done_name = done_name;
        descriptor->done_name_size = generated_size;
        descriptor->source_node_id = child.impl->id;
        descriptor->autoforward =
            autoforward_attribute.impl != NULL &&
            scxml_analyze_view_equal_raw(
                scxml_syntax_attribute_value(autoforward_attribute), "true");
        if (idlocation_attribute.impl != NULL) {
            if (!scxml_analyze_checked_add(owner_id.size, 1u,
                             &descriptor->dynamic_id_max_size) ||
                !scxml_analyze_checked_add(descriptor->dynamic_id_max_size,
                             max_token_digits,
                             &descriptor->dynamic_id_max_size) ||
                !scxml_analyze_checked_add(sizeof(done_prefix) - 1u,
                             descriptor->dynamic_id_max_size,
                             &descriptor->dynamic_done_name_max_size) ||
                descriptor->dynamic_id_max_size >
                    SCXML_EVENT_METADATA_CAPACITY ||
                descriptor->dynamic_done_name_max_size >
                    SCXML_EVENT_METADATA_CAPACITY) {
                return scxml_analyze_fail(
                    build, SCXML_NATIVE_IR_REJECTED,
                    scxml_syntax_attribute_location(idlocation_attribute),
                    "dynamic invoke id budget mismatched admission");
            }
            expression_status = scxml_emit_compile_cmeta_owned_string_location(
                build, idlocation_attribute, "invoke idlocation",
                &descriptor->id_location);
            if (expression_status != SCXML_OK)
                return expression_status;
            descriptor->has_id_location = true;
        }
        if (type_expr_attribute.impl != NULL) {
            expression_status = scxml_emit_compile_cmeta_value_program(
                build, type_expr_attribute, "invoke typeexpr",
                &descriptor->type_expr,
                SCXML_EXPR_VALUE_STRING);
            if (expression_status != SCXML_OK)
                return expression_status;
            descriptor->has_type_expr = true;
        }
        if (src_expr_attribute.impl != NULL) {
            expression_status = scxml_emit_compile_cmeta_value_program(
                build, src_expr_attribute, "invoke srcexpr",
                &descriptor->src_expr,
                SCXML_EXPR_VALUE_STRING);
            if (expression_status != SCXML_OK)
                return expression_status;
            descriptor->has_src_expr = true;
        }
        if (namelist_attribute.impl != NULL) {
            const salts_xml_string_view namelist =
                scxml_syntax_attribute_value(namelist_attribute);
            size_t cursor = 0u;
            salts_xml_string_view token;
            while (scxml_analyze_token_next(namelist, &cursor, &token)) {
                scxml_payload_descriptor *payload;
                if (build->payload_index >= build->payload_capacity)
                    return scxml_analyze_fail(
                        build, SCXML_NATIVE_IR_REJECTED,
                        scxml_syntax_attribute_location(namelist_attribute),
                        "invoke payload emission exceeded admission");
                payload = &build->payloads[build->payload_index];
                if (!retain_invocation_view(
                        build, token, &payload->name,
                        &payload->name_size))
                    return scxml_analyze_fail(
                        build, SCXML_NATIVE_IR_REJECTED,
                        scxml_syntax_attribute_location(namelist_attribute),
                        "invoke namelist storage mismatched admission");
                expression_status = scxml_emit_compile_cmeta_payload_token(
                    build, token,
                    scxml_syntax_attribute_location(namelist_attribute),
                    "invoke namelist", &payload->expression);
                if (expression_status != SCXML_OK)
                    return expression_status;
                ++build->payload_index;
            }
        }
        {
            size_t payload_child_index;
            for (payload_child_index = 0u;
                 payload_child_index < scxml_syntax_node_child_count(child);
                 ++payload_child_index) {
                const scxml_syntax_node payload_child =
                    scxml_syntax_node_child_at(child, payload_child_index);
                const scxml_element_kind payload_kind =
                    scxml_analyze_element_kind(payload_child);
                if (scxml_syntax_node_type(payload_child) !=
                    SALTS_XML_ELEMENT)
                    continue;
                if (payload_kind == SCXML_ELEMENT_PARAM) {
                    const scxml_syntax_attribute name =
                        scxml_analyze_find_attribute(payload_child, "name");
                    const scxml_syntax_attribute expression =
                        scxml_analyze_find_attribute(payload_child, "expr");
                    const scxml_syntax_attribute location =
                        scxml_analyze_find_attribute(payload_child, "location");
                    scxml_payload_descriptor *payload;
                    if (build->payload_index >= build->payload_capacity)
                        return scxml_analyze_fail(
                            build, SCXML_NATIVE_IR_REJECTED,
                            scxml_syntax_node_location(payload_child),
                            "invoke param emission exceeded admission");
                    payload = &build->payloads[build->payload_index];
                    if (!retain_invocation_view(
                            build, scxml_syntax_attribute_value(name),
                            &payload->name, &payload->name_size))
                        return scxml_analyze_fail(
                            build, SCXML_NATIVE_IR_REJECTED,
                            scxml_syntax_attribute_location(name),
                            "invoke param storage mismatched admission");
                    if (location.impl != NULL)
                        expression_status = scxml_emit_compile_cmeta_payload_token(
                            build, scxml_syntax_attribute_value(location),
                            scxml_syntax_attribute_location(location),
                            "invoke param location", &payload->expression);
                    else
                        expression_status = scxml_emit_compile_cmeta_value_program(
                            build, expression, "invoke param",
                            &payload->expression,
                            SCXML_EXPR_VALUE_INVALID);
                    if (expression_status != SCXML_OK)
                        return expression_status;
                    ++build->payload_index;
                } else if (payload_kind == SCXML_ELEMENT_CONTENT) {
                    if (scxml_analyze_find_attribute(payload_child, "expr").impl != NULL) {
                        expression_status = scxml_emit_compile_cmeta_content_expression(
                            build, scxml_analyze_find_attribute(payload_child, "expr"),
                            "invoke content", &descriptor->content,
                            &descriptor->data_expr);
                    } else {
                        expression_status = retain_invocation_inline_content(
                            build, payload_child, &descriptor->content);
                    }
                    if (expression_status != SCXML_OK)
                        return expression_status;
                }
            }
        }
        descriptor->payload_count =
            build->payload_index - descriptor->payload_first;
        build->invocation_names[build->invocation_index] =
            (scxml_name_ref){
                {descriptor->id, descriptor->id_size}, id_location,
                build->invocation_index + 1u, build->invocation_index};
        build->event_occurrences[build->event_occurrence_index] =
            (scxml_name_ref){
                {descriptor->done_name, descriptor->done_name_size},
                id_location, 0u, build->event_occurrence_index};
        ++build->event_occurrence_index;
        ++build->invocation_index;
    }
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        const scxml_element_kind kind = scxml_analyze_element_kind(child);
        if (scxml_syntax_node_type(child) == SALTS_XML_ELEMENT &&
            (scxml_analyze_is_state_element(kind) || kind == SCXML_ELEMENT_INITIAL ||
             kind == SCXML_ELEMENT_HISTORY)) {
            scxml_status status = scxml_analyze_emit_invocation_declarations(
                build, child, node_count);
            if (status != SCXML_OK) return status;
        }
    }
    return SCXML_OK;
}

scxml_status scxml_analyze_resolve_invocation_events(scxml_build *build) {
    size_t index;
    for (index = 0u; index < build->invocation_index; ++index) {
        scxml_invocation_descriptor *descriptor =
            &build->invocations[index];
        const scxml_name_ref *event = scxml_analyze_find_name_ref(
            build->event_names, build->event_name_count,
            (salts_xml_string_view){
                descriptor->done_name, descriptor->done_name_size});
        if (event == NULL) {
            return scxml_analyze_fail(build, SCXML_NATIVE_IR_REJECTED,
                              (salts_xml_location){0u, 0u, 0u},
                              "invoke done Event was not retained");
        }
        descriptor->done_event = (cflow_event_id)event->id;
    }
    return SCXML_OK;
}

scxml_status scxml_analyze_collect_transition_events(
    scxml_build *build, scxml_syntax_node node) {
    size_t index;
    for (index = 0u; index < scxml_syntax_node_child_count(node); ++index) {
        const scxml_syntax_node child = scxml_syntax_node_child_at(node, index);
        const scxml_element_kind child_kind = scxml_analyze_element_kind(child);
        if (scxml_syntax_node_type(child) != SALTS_XML_ELEMENT) continue;
        if (child_kind == SCXML_ELEMENT_TRANSITION) {
            const scxml_syntax_attribute event_attribute =
                scxml_analyze_find_attribute(child, "event");
            if (event_attribute.impl != NULL) {
                const salts_xml_string_view value =
                    scxml_syntax_attribute_value(event_attribute);
                salts_xml_string_view token;
                size_t cursor = 0u;
                while (scxml_analyze_token_next(value, &cursor, &token)) {
                    salts_xml_string_view completed = {NULL, 0u};
                    if (!scxml_analyze_completion_token(token, &completed)) {
                        salts_xml_string_view descriptor_base = {NULL, 0u};
                        bool match_all = false;
                        if (!normalize_event_descriptor(
                                token, &descriptor_base, &match_all)) {
                            return scxml_analyze_fail(
                                build, SCXML_INVALID_STRUCTURE,
                                scxml_syntax_attribute_location(event_attribute),
                                "invalid event descriptor");
                        }
                        if (match_all) continue;
                        build->event_occurrences[build->event_occurrence_index] =
                            (scxml_name_ref){
                                descriptor_base,
                                scxml_syntax_attribute_location(event_attribute),
                                0u, build->event_occurrence_index};
                        ++build->event_occurrence_index;
                    }
                }
            }
            {
                scxml_status status =
                    scxml_analyze_collect_transition_events(build, child);
                if (status != SCXML_OK) return status;
            }
        } else if (child_kind == SCXML_ELEMENT_RAISE ||
                   (child_kind == SCXML_ELEMENT_SEND &&
                    scxml_analyze_find_attribute(child, "event").impl != NULL)) {
            const scxml_syntax_attribute event_attribute =
                scxml_analyze_find_attribute(child, "event");
            build->event_occurrences[build->event_occurrence_index] =
                (scxml_name_ref){
                    scxml_syntax_attribute_value(event_attribute),
                    scxml_syntax_attribute_location(event_attribute), 0u,
                    build->event_occurrence_index};
            ++build->event_occurrence_index;
        } else if (scxml_analyze_is_state_element(child_kind) ||
                   child_kind == SCXML_ELEMENT_INITIAL ||
                   child_kind == SCXML_ELEMENT_HISTORY ||
                   child_kind == SCXML_ELEMENT_ONENTRY ||
                   child_kind == SCXML_ELEMENT_ONEXIT ||
                   child_kind == SCXML_ELEMENT_INVOKE ||
                   child_kind == SCXML_ELEMENT_FINALIZE ||
                   child_kind == SCXML_ELEMENT_IF ||
                   child_kind == SCXML_ELEMENT_FOREACH) {
            scxml_status status =
                scxml_analyze_collect_transition_events(build, child);
            if (status != SCXML_OK) return status;
        }
    }
    return SCXML_OK;
}

void scxml_analyze_collect_reserved_error_events(scxml_build *build,
                                          salts_xml_location location) {
    size_t order = build->event_occurrence_index;
    build->event_occurrences[order] =
        (scxml_name_ref){
            {SCXML_ERROR_EXECUTION_EVENT,
             sizeof(SCXML_ERROR_EXECUTION_EVENT) - 1u},
            location, 0u, order};
    ++build->event_occurrence_index;
    order = build->event_occurrence_index;
    build->event_occurrences[order] =
        (scxml_name_ref){
            {SCXML_ERROR_COMMUNICATION_EVENT,
             sizeof(SCXML_ERROR_COMMUNICATION_EVENT) - 1u},
            location, 0u, order};
    ++build->event_occurrence_index;
}

scxml_status scxml_analyze_build_event_names(scxml_build *build,
                                            size_t occurrence_count) {
    size_t index = 0u;
    size_t unique = 0u;
    if (occurrence_count == 0u) {
        build->event_name_count = 0u;
        return SCXML_OK;
    }
    qsort(build->event_occurrences, occurrence_count,
          sizeof(*build->event_occurrences), scxml_analyze_compare_name_ref);
    while (index < occurrence_count) {
        size_t next = index + 1u;
        scxml_name_ref selected = build->event_occurrences[index];
        while (next < occurrence_count &&
               view_equal(build->event_occurrences[index].name,
                          build->event_occurrences[next].name)) {
            if (build->event_occurrences[next].order < selected.order)
                selected = build->event_occurrences[next];
            ++next;
        }
        build->event_names[unique++] = selected;
        index = next;
    }
    if (unique > build->limits.max_events) {
        return scxml_analyze_fail(build, SCXML_LIMIT_EXCEEDED,
                          build->event_names[build->limits.max_events].location,
                          "SCXML event count exceeds max_events");
    }
    qsort(build->event_names, unique, sizeof(*build->event_names),
          compare_name_order);
    for (index = 0u; index < unique; ++index) {
        build->event_names[index].id = (cflow_event_id)(index + 1u);
        build->events[index] = (cflow_event_type){
            (cflow_event_id)(index + 1u), &cmeta_type_bool};
    }
    qsort(build->event_names, unique, sizeof(*build->event_names),
          scxml_analyze_compare_name_ref);
    build->event_name_count = unique;
    return SCXML_OK;
}
