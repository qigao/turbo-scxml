#include "scxml_ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCXML_AST_DEFAULT_MAX_NODES (1024u * 1024u)
#define SCXML_AST_DEFAULT_MAX_ATTRIBUTES (4u * 1024u * 1024u)
#define SCXML_AST_DEFAULT_MAX_STORAGE_BYTES (64u * 1024u * 1024u)
#define SCXML_AST_DEFAULT_MAX_DEPTH 256u
#define SCXML_AST_NAMESPACE "http://www.w3.org/2005/07/scxml"

typedef struct scxml_ast_impl {
    scxml_ast_node *nodes;
    scxml_ast_attribute *attributes;
    scxml_ast_node_id *children;
    char *storage;
    size_t node_count;
    size_t attribute_count;
    size_t child_count;
    size_t storage_size;
} scxml_ast_impl;

typedef struct scxml_ast_measurement {
    size_t node_count;
    size_t attribute_count;
    size_t storage_size;
} scxml_ast_measurement;

typedef struct scxml_ast_writer {
    scxml_ast_impl *impl;
    size_t node_index;
    size_t attribute_index;
    size_t child_index;
    size_t storage_index;
} scxml_ast_writer;

static bool checked_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) return false;
    *out = left + right;
    return true;
}

static bool checked_multiply(size_t left, size_t right, size_t *out) {
    if (out == NULL || (left != 0u && right > SIZE_MAX / left)) return false;
    *out = left * right;
    return true;
}

static bool view_equal(turbo_xml_string_view view, const char *text) {
    const size_t size = strlen(text);
    return view.data != NULL && view.size == size &&
        memcmp(view.data, text, size) == 0;
}

static scxml_status ast_fail(
    scxml_diagnostic *diagnostic, scxml_status status,
    turbo_xml_location location, const char *message) {
    if (diagnostic != NULL) {
        diagnostic->status = status;
        diagnostic->location = location;
        (void)snprintf(
            diagnostic->message, sizeof(diagnostic->message), "%s",
            message != NULL ? message : "SCXML AST construction failed");
    }
    return status;
}

static scxml_element_kind element_kind(turbo_xml_string_view name) {
    if (view_equal(name, "scxml")) return SCXML_ELEMENT_SCXML;
    if (view_equal(name, "state")) return SCXML_ELEMENT_STATE;
    if (view_equal(name, "parallel")) return SCXML_ELEMENT_PARALLEL;
    if (view_equal(name, "transition")) return SCXML_ELEMENT_TRANSITION;
    if (view_equal(name, "initial")) return SCXML_ELEMENT_INITIAL;
    if (view_equal(name, "final")) return SCXML_ELEMENT_FINAL;
    if (view_equal(name, "history")) return SCXML_ELEMENT_HISTORY;
    if (view_equal(name, "onentry")) return SCXML_ELEMENT_ONENTRY;
    if (view_equal(name, "onexit")) return SCXML_ELEMENT_ONEXIT;
    if (view_equal(name, "raise")) return SCXML_ELEMENT_RAISE;
    if (view_equal(name, "send")) return SCXML_ELEMENT_SEND;
    if (view_equal(name, "cancel")) return SCXML_ELEMENT_CANCEL;
    if (view_equal(name, "log")) return SCXML_ELEMENT_LOG;
    if (view_equal(name, "assign")) return SCXML_ELEMENT_ASSIGN;
    if (view_equal(name, "foreach")) return SCXML_ELEMENT_FOREACH;
    if (view_equal(name, "if")) return SCXML_ELEMENT_IF;
    if (view_equal(name, "elseif")) return SCXML_ELEMENT_ELSEIF;
    if (view_equal(name, "else")) return SCXML_ELEMENT_ELSE;
    if (view_equal(name, "invoke")) return SCXML_ELEMENT_INVOKE;
    if (view_equal(name, "finalize")) return SCXML_ELEMENT_FINALIZE;
    if (view_equal(name, "content")) return SCXML_ELEMENT_CONTENT;
    if (view_equal(name, "param")) return SCXML_ELEMENT_PARAM;
    if (view_equal(name, "datamodel")) return SCXML_ELEMENT_DATAMODEL;
    if (view_equal(name, "data")) return SCXML_ELEMENT_DATA;
    if (view_equal(name, "donedata")) return SCXML_ELEMENT_DONEDATA;
    if (view_equal(name, "script")) return SCXML_ELEMENT_SCRIPT;
    return SCXML_ELEMENT_UNKNOWN;
}

static scxml_attribute_kind attribute_kind(turbo_xml_string_view name) {
    if (view_equal(name, "version")) return SCXML_ATTRIBUTE_VERSION;
    if (view_equal(name, "datamodel")) return SCXML_ATTRIBUTE_DATAMODEL;
    if (view_equal(name, "initial")) return SCXML_ATTRIBUTE_INITIAL;
    if (view_equal(name, "name")) return SCXML_ATTRIBUTE_NAME;
    if (view_equal(name, "binding")) return SCXML_ATTRIBUTE_BINDING;
    if (view_equal(name, "id")) return SCXML_ATTRIBUTE_ID;
    if (view_equal(name, "type")) return SCXML_ATTRIBUTE_TYPE;
    if (view_equal(name, "event")) return SCXML_ATTRIBUTE_EVENT;
    if (view_equal(name, "target")) return SCXML_ATTRIBUTE_TARGET;
    if (view_equal(name, "cond")) return SCXML_ATTRIBUTE_COND;
    if (view_equal(name, "eventexpr")) return SCXML_ATTRIBUTE_EVENTEXPR;
    if (view_equal(name, "targetexpr")) return SCXML_ATTRIBUTE_TARGETEXPR;
    if (view_equal(name, "typeexpr")) return SCXML_ATTRIBUTE_TYPEEXPR;
    if (view_equal(name, "idlocation")) return SCXML_ATTRIBUTE_IDLOCATION;
    if (view_equal(name, "delay")) return SCXML_ATTRIBUTE_DELAY;
    if (view_equal(name, "delayexpr")) return SCXML_ATTRIBUTE_DELAYEXPR;
    if (view_equal(name, "namelist")) return SCXML_ATTRIBUTE_NAMELIST;
    if (view_equal(name, "sendid")) return SCXML_ATTRIBUTE_SENDID;
    if (view_equal(name, "sendidexpr")) return SCXML_ATTRIBUTE_SENDIDEXPR;
    if (view_equal(name, "label")) return SCXML_ATTRIBUTE_LABEL;
    if (view_equal(name, "expr")) return SCXML_ATTRIBUTE_EXPR;
    if (view_equal(name, "location")) return SCXML_ATTRIBUTE_LOCATION;
    if (view_equal(name, "array")) return SCXML_ATTRIBUTE_ARRAY;
    if (view_equal(name, "item")) return SCXML_ATTRIBUTE_ITEM;
    if (view_equal(name, "index")) return SCXML_ATTRIBUTE_INDEX;
    if (view_equal(name, "src")) return SCXML_ATTRIBUTE_SRC;
    if (view_equal(name, "srcexpr")) return SCXML_ATTRIBUTE_SRCEXPR;
    if (view_equal(name, "autoforward"))
        return SCXML_ATTRIBUTE_AUTOFORWARD;
    return SCXML_ATTRIBUTE_UNKNOWN;
}

static bool source_has_serialized_children(turbo_xml_node source) {
    scxml_element_kind kind;
    if (turbo_xml_node_type(source) != TURBO_XML_ELEMENT ||
        !view_equal(turbo_xml_node_namespace_uri(source), SCXML_AST_NAMESPACE))
        return false;
    kind = element_kind(turbo_xml_node_local_name(source));
    return kind == SCXML_ELEMENT_CONTENT || kind == SCXML_ELEMENT_DATA ||
           kind == SCXML_ELEMENT_SCRIPT;
}

static bool measure_view(
    turbo_xml_string_view view, scxml_ast_measurement *measurement) {
    return checked_add(
        measurement->storage_size, view.size,
        &measurement->storage_size);
}

static scxml_status measure_serialized_children(
    turbo_xml_node source, const scxml_ast_limits *limits,
    scxml_ast_measurement *measurement, scxml_diagnostic *diagnostic) {
    size_t serialized_size = 0u;
    size_t retained_size;
    turbo_xml_status xml_status;
    if (!source_has_serialized_children(source)) return SCXML_OK;
    xml_status = turbo_xml_serialize_children(
        source, NULL, 0u, limits->max_storage_bytes, &serialized_size);
    if (xml_status != TURBO_XML_OK) {
        return ast_fail(
            diagnostic,
            xml_status == TURBO_XML_LIMIT_EXCEEDED
                ? SCXML_LIMIT_EXCEEDED
                : xml_status == TURBO_XML_ALLOCATION_FAILED
                    ? SCXML_ALLOCATION_FAILED
                    : SCXML_INVALID_STRUCTURE,
            turbo_xml_node_location(source),
            "SCXML AST inline content serialization failed");
    }
    if (!checked_add(serialized_size, 1u, &retained_size) ||
        !checked_add(measurement->storage_size, retained_size,
                     &measurement->storage_size)) {
        return ast_fail(
            diagnostic, SCXML_LIMIT_EXCEEDED,
            turbo_xml_node_location(source),
            "SCXML AST string storage overflow");
    }
    return SCXML_OK;
}

static scxml_status measure_node(
    turbo_xml_node source, const scxml_ast_limits *limits,
    size_t depth, scxml_ast_measurement *measurement,
    scxml_diagnostic *diagnostic) {
    size_t index;
    scxml_status status;
    if (depth > limits->max_depth) {
        return ast_fail(
            diagnostic, SCXML_LIMIT_EXCEEDED,
            turbo_xml_node_location(source),
            "SCXML AST depth exceeds max_depth");
    }
    if (measurement->node_count >= limits->max_nodes ||
        measurement->node_count >= (size_t)SCXML_AST_NODE_NONE) {
        return ast_fail(
            diagnostic, SCXML_LIMIT_EXCEEDED,
            turbo_xml_node_location(source),
            "SCXML AST node count exceeds max_nodes");
    }
    ++measurement->node_count;
    if (!measure_view(turbo_xml_node_local_name(source), measurement) ||
        !measure_view(turbo_xml_node_namespace_uri(source), measurement) ||
        !measure_view(turbo_xml_node_value(source), measurement) ||
        !measure_view(turbo_xml_node_text_view(source), measurement)) {
        return ast_fail(
            diagnostic, SCXML_LIMIT_EXCEEDED,
            turbo_xml_node_location(source),
            "SCXML AST string storage overflow");
    }
    for (index = 0u; index < turbo_xml_node_attribute_count(source); ++index) {
        const turbo_xml_attribute attribute =
            turbo_xml_node_attribute_at(source, index);
        if (measurement->attribute_count >= limits->max_attributes) {
            return ast_fail(
                diagnostic, SCXML_LIMIT_EXCEEDED,
                turbo_xml_attribute_location(attribute),
                "SCXML AST attribute count exceeds max_attributes");
        }
        ++measurement->attribute_count;
        if (!measure_view(
                turbo_xml_attribute_local_name(attribute), measurement) ||
            !measure_view(
                turbo_xml_attribute_namespace_uri(attribute), measurement) ||
            !measure_view(turbo_xml_attribute_value(attribute), measurement)) {
            return ast_fail(
                diagnostic, SCXML_LIMIT_EXCEEDED,
                turbo_xml_attribute_location(attribute),
                "SCXML AST string storage overflow");
        }
    }
    status = measure_serialized_children(
        source, limits, measurement, diagnostic);
    if (status != SCXML_OK) return status;
    if (measurement->storage_size > limits->max_storage_bytes) {
        return ast_fail(
            diagnostic, SCXML_LIMIT_EXCEEDED,
            turbo_xml_node_location(source),
            "SCXML AST strings exceed max_storage_bytes");
    }
    for (index = 0u; index < turbo_xml_node_child_count(source); ++index) {
        status = measure_node(
            turbo_xml_node_child_at(source, index), limits, depth + 1u,
            measurement, diagnostic);
        if (status != SCXML_OK) return status;
    }
    return SCXML_OK;
}

static scxml_ast_string_view copy_view(
    scxml_ast_writer *writer, turbo_xml_string_view source) {
    scxml_ast_string_view result = {NULL, source.size};
    if (source.size == 0u) return result;
    result.data = writer->impl->storage + writer->storage_index;
    memcpy(writer->impl->storage + writer->storage_index,
           source.data, source.size);
    writer->storage_index += source.size;
    return result;
}

static scxml_status write_serialized_children(
    scxml_ast_writer *writer, turbo_xml_node source,
    scxml_ast_node *node, scxml_diagnostic *diagnostic) {
    turbo_xml_status xml_status;
    size_t serialized_size = 0u;
    const size_t capacity =
        writer->impl->storage_size - writer->storage_index;
    if (!source_has_serialized_children(source)) return SCXML_OK;
    node->serialized_children.data =
        writer->impl->storage + writer->storage_index;
    xml_status = turbo_xml_serialize_children(
        source, writer->impl->storage + writer->storage_index,
        capacity, capacity, &serialized_size);
    if (xml_status != TURBO_XML_OK || serialized_size >= capacity) {
        return ast_fail(
            diagnostic,
            xml_status == TURBO_XML_ALLOCATION_FAILED
                ? SCXML_ALLOCATION_FAILED
                : SCXML_INVALID_STRUCTURE,
            turbo_xml_node_location(source),
            "SCXML AST source changed during inline serialization");
    }
    node->serialized_children.size = serialized_size;
    writer->storage_index += serialized_size + 1u;
    return SCXML_OK;
}

static scxml_status write_node(
    scxml_ast_writer *writer, turbo_xml_node source,
    scxml_ast_node_id parent, scxml_ast_node_id *out_id,
    scxml_diagnostic *diagnostic) {
    const scxml_ast_node_id node_id =
        (scxml_ast_node_id)writer->node_index++;
    scxml_ast_node *node = &writer->impl->nodes[node_id];
    scxml_ast_node_id previous_child = SCXML_AST_NODE_NONE;
    const turbo_xml_string_view namespace_uri =
        turbo_xml_node_namespace_uri(source);
    size_t index;
    scxml_status status;
    node->id = node_id;
    node->xml_kind = turbo_xml_node_type(source);
    node->kind = node->xml_kind == TURBO_XML_ELEMENT &&
            view_equal(namespace_uri, SCXML_AST_NAMESPACE)
        ? element_kind(turbo_xml_node_local_name(source))
        : SCXML_ELEMENT_UNKNOWN;
    node->parent = parent;
    node->first_child = SCXML_AST_NODE_NONE;
    node->next_sibling = SCXML_AST_NODE_NONE;
    node->first_child_index = writer->child_index;
    node->child_count = turbo_xml_node_child_count(source);
    writer->child_index += node->child_count;
    node->first_attribute = writer->attribute_index;
    node->attribute_count = turbo_xml_node_attribute_count(source);
    node->name = copy_view(writer, turbo_xml_node_local_name(source));
    node->namespace_uri = copy_view(writer, namespace_uri);
    node->value = copy_view(writer, turbo_xml_node_value(source));
    node->text = copy_view(writer, turbo_xml_node_text_view(source));
    node->location = turbo_xml_node_location(source);
    for (index = 0u; index < node->attribute_count; ++index) {
        const turbo_xml_attribute source_attribute =
            turbo_xml_node_attribute_at(source, index);
        scxml_ast_attribute *attribute =
            &writer->impl->attributes[writer->attribute_index++];
        const turbo_xml_string_view attribute_namespace =
            turbo_xml_attribute_namespace_uri(source_attribute);
        attribute->kind = attribute_namespace.size == 0u
            ? attribute_kind(
                  turbo_xml_attribute_local_name(source_attribute))
            : SCXML_ATTRIBUTE_UNKNOWN;
        attribute->name = copy_view(
            writer, turbo_xml_attribute_local_name(source_attribute));
        attribute->namespace_uri = copy_view(writer, attribute_namespace);
        attribute->value = copy_view(
            writer, turbo_xml_attribute_value(source_attribute));
        attribute->location = turbo_xml_attribute_location(source_attribute);
    }
    status = write_serialized_children(writer, source, node, diagnostic);
    if (status != SCXML_OK) return status;
    for (index = 0u; index < node->child_count; ++index) {
        scxml_ast_node_id child = SCXML_AST_NODE_NONE;
        status = write_node(
            writer, turbo_xml_node_child_at(source, index), node_id,
            &child, diagnostic);
        if (status != SCXML_OK) return status;
        writer->impl->children[node->first_child_index + index] = child;
        if (previous_child == SCXML_AST_NODE_NONE)
            node->first_child = child;
        else
            writer->impl->nodes[previous_child].next_sibling = child;
        previous_child = child;
    }
    *out_id = node_id;
    return SCXML_OK;
}

static void destroy_impl(scxml_ast_impl *impl) {
    if (impl == NULL) return;
    free(impl->storage);
    free(impl->children);
    free(impl->attributes);
    free(impl->nodes);
    free(impl);
}

static bool node_index(
    const scxml_ast_impl *impl, const scxml_ast_node *node,
    size_t *out_index) {
    uintptr_t begin;
    uintptr_t value;
    size_t bytes;
    size_t offset;
    if (impl == NULL || node == NULL || out_index == NULL ||
        !checked_multiply(
            impl->node_count, sizeof(*impl->nodes), &bytes))
        return false;
    begin = (uintptr_t)impl->nodes;
    value = (uintptr_t)node;
    if (value < begin || value - begin >= bytes) return false;
    offset = (size_t)(value - begin);
    if (offset % sizeof(*impl->nodes) != 0u) return false;
    *out_index = offset / sizeof(*impl->nodes);
    return true;
}

scxml_ast_limits scxml_ast_default_limits(void) {
    const scxml_ast_limits limits = {
        SCXML_AST_DEFAULT_MAX_NODES,
        SCXML_AST_DEFAULT_MAX_ATTRIBUTES,
        SCXML_AST_DEFAULT_MAX_STORAGE_BYTES,
        SCXML_AST_DEFAULT_MAX_DEPTH};
    return limits;
}

scxml_status scxml_ast_build(
    scxml_ast *out, turbo_xml_node root,
    const scxml_ast_limits *limits_or_null,
    scxml_diagnostic *diagnostic) {
    const scxml_ast_limits limits = limits_or_null != NULL
        ? *limits_or_null : scxml_ast_default_limits();
    scxml_ast_measurement measurement = {0};
    scxml_ast_impl *impl = NULL;
    scxml_ast_writer writer = {0};
    scxml_ast_node_id root_id = SCXML_AST_NODE_NONE;
    scxml_status status;
    size_t ignored_bytes;
    if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
    if (out == NULL || out->impl != NULL || root.impl == NULL ||
        limits.max_nodes == 0u || limits.max_attributes == 0u ||
        limits.max_storage_bytes == 0u || limits.max_depth == 0u ||
        turbo_xml_node_type(root) != TURBO_XML_ELEMENT) {
        return ast_fail(
            diagnostic, SCXML_INVALID_ARGUMENT,
            root.impl != NULL ? turbo_xml_node_location(root)
                              : (turbo_xml_location){0u, 0u, 0u},
            "SCXML AST output root and limits must be valid");
    }
    if (!view_equal(turbo_xml_node_local_name(root), "scxml") ||
        !view_equal(
            turbo_xml_node_namespace_uri(root), SCXML_AST_NAMESPACE)) {
        return ast_fail(
            diagnostic, SCXML_INVALID_NAMESPACE,
            turbo_xml_node_location(root),
            "root must be W3C SCXML scxml element");
    }
    status = measure_node(root, &limits, 1u, &measurement, diagnostic);
    if (status != SCXML_OK) return status;
    if (!checked_multiply(
            measurement.node_count, sizeof(scxml_ast_node),
            &ignored_bytes) ||
        !checked_multiply(
            measurement.attribute_count, sizeof(scxml_ast_attribute),
            &ignored_bytes) ||
        !checked_multiply(
            measurement.node_count - 1u, sizeof(scxml_ast_node_id),
            &ignored_bytes)) {
        return ast_fail(
            diagnostic, SCXML_LIMIT_EXCEEDED,
            turbo_xml_node_location(root),
            "SCXML AST allocation size overflow");
    }
    impl = (scxml_ast_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) {
        return ast_fail(
            diagnostic, SCXML_ALLOCATION_FAILED,
            turbo_xml_node_location(root),
            "SCXML AST allocation failed");
    }
    impl->nodes = (scxml_ast_node *)calloc(
        measurement.node_count, sizeof(*impl->nodes));
    if (measurement.attribute_count != 0u)
        impl->attributes = (scxml_ast_attribute *)calloc(
            measurement.attribute_count, sizeof(*impl->attributes));
    if (measurement.node_count > 1u)
        impl->children = (scxml_ast_node_id *)calloc(
            measurement.node_count - 1u, sizeof(*impl->children));
    if (measurement.storage_size != 0u)
        impl->storage = (char *)malloc(measurement.storage_size);
    if (impl->nodes == NULL ||
        (measurement.attribute_count != 0u && impl->attributes == NULL) ||
        (measurement.node_count > 1u && impl->children == NULL) ||
        (measurement.storage_size != 0u && impl->storage == NULL)) {
        destroy_impl(impl);
        return ast_fail(
            diagnostic, SCXML_ALLOCATION_FAILED,
            turbo_xml_node_location(root),
            "SCXML AST allocation failed");
    }
    impl->node_count = measurement.node_count;
    impl->attribute_count = measurement.attribute_count;
    impl->child_count = measurement.node_count - 1u;
    impl->storage_size = measurement.storage_size;
    writer.impl = impl;
    status = write_node(
        &writer, root, SCXML_AST_NODE_NONE, &root_id, diagnostic);
    if (status != SCXML_OK || root_id != 0u ||
        writer.node_index != impl->node_count ||
        writer.attribute_index != impl->attribute_count ||
        writer.child_index != impl->child_count ||
        writer.storage_index != impl->storage_size) {
        destroy_impl(impl);
        return status != SCXML_OK
            ? status
            : ast_fail(
                  diagnostic, SCXML_INVALID_STRUCTURE,
                  turbo_xml_node_location(root),
                  "SCXML AST source changed during construction");
    }
    out->impl = impl;
    return SCXML_OK;
}

void scxml_ast_destroy(scxml_ast *ast) {
    scxml_ast_impl *impl = ast != NULL
        ? (scxml_ast_impl *)ast->impl : NULL;
    if (impl == NULL) return;
    destroy_impl(impl);
    ast->impl = NULL;
}

size_t scxml_ast_node_count(const scxml_ast *ast) {
    const scxml_ast_impl *impl = ast != NULL
        ? (const scxml_ast_impl *)ast->impl : NULL;
    return impl != NULL ? impl->node_count : 0u;
}

scxml_ast_node_id scxml_ast_root(const scxml_ast *ast) {
    return scxml_ast_node_count(ast) != 0u
        ? (scxml_ast_node_id)0u : SCXML_AST_NODE_NONE;
}

const scxml_ast_node *scxml_ast_node_at(
    const scxml_ast *ast, scxml_ast_node_id node) {
    const scxml_ast_impl *impl = ast != NULL
        ? (const scxml_ast_impl *)ast->impl : NULL;
    return impl != NULL && (size_t)node < impl->node_count
        ? &impl->nodes[node] : NULL;
}

const scxml_ast_node *scxml_ast_node_child_at(
    const scxml_ast *ast, const scxml_ast_node *node, size_t index) {
    const scxml_ast_impl *impl = ast != NULL
        ? (const scxml_ast_impl *)ast->impl : NULL;
    size_t owner_index;
    scxml_ast_node_id child;
    if (impl == NULL || !node_index(impl, node, &owner_index) ||
        index >= node->child_count ||
        node->first_child_index > impl->child_count ||
        node->child_count > impl->child_count - node->first_child_index)
        return NULL;
    (void)owner_index;
    child = impl->children[node->first_child_index + index];
    return (size_t)child < impl->node_count ? &impl->nodes[child] : NULL;
}

const scxml_ast_attribute *scxml_ast_node_find_attribute(
    const scxml_ast *ast, const scxml_ast_node *node,
    scxml_attribute_kind kind) {
    const scxml_ast_impl *impl = ast != NULL
        ? (const scxml_ast_impl *)ast->impl : NULL;
    size_t owner_index;
    size_t index;
    if (impl == NULL || kind == SCXML_ATTRIBUTE_UNKNOWN ||
        !node_index(impl, node, &owner_index) ||
        node->first_attribute > impl->attribute_count ||
        node->attribute_count >
            impl->attribute_count - node->first_attribute)
        return NULL;
    (void)owner_index;
    for (index = 0u; index < node->attribute_count; ++index) {
        const scxml_ast_attribute *attribute =
            &impl->attributes[node->first_attribute + index];
        if (attribute->kind == kind) return attribute;
    }
    return NULL;
}

const scxml_ast_attribute *scxml_ast_node_attribute_at(
    const scxml_ast *ast, const scxml_ast_node *node, size_t index) {
    const scxml_ast_impl *impl = ast != NULL
        ? (const scxml_ast_impl *)ast->impl : NULL;
    size_t owner_index;
    if (impl == NULL || !node_index(impl, node, &owner_index) ||
        index >= node->attribute_count ||
        node->first_attribute > impl->attribute_count ||
        node->attribute_count >
            impl->attribute_count - node->first_attribute)
        return NULL;
    (void)owner_index;
    return &impl->attributes[node->first_attribute + index];
}

scxml_attribute_kind scxml_ast_attribute_kind_from_name(
    const char *local_name) {
    const turbo_xml_string_view name = {
        local_name, local_name != NULL ? strlen(local_name) : 0u};
    return local_name != NULL
        ? attribute_kind(name) : SCXML_ATTRIBUTE_UNKNOWN;
}
