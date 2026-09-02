#ifndef TURBO_SCXML_SYNTAX_H
#define TURBO_SCXML_SYNTAX_H

#include "scxml_ast.h"

#include <string.h>

typedef struct scxml_syntax_node {
    const scxml_ast *ast;
    const scxml_ast_node *impl;
} scxml_syntax_node;

typedef struct scxml_syntax_attribute {
    const scxml_ast_attribute *impl;
} scxml_syntax_attribute;

static inline scxml_syntax_node scxml_syntax_root(
    const scxml_ast *ast) {
    const scxml_syntax_node result = {
        ast, scxml_ast_node_at(ast, scxml_ast_root(ast))};
    return result;
}

static inline turbo_xml_node_kind scxml_syntax_node_type(
    scxml_syntax_node node) {
    return node.impl != NULL ? node.impl->xml_kind : TURBO_XML_INVALID_NODE;
}

static inline turbo_xml_location scxml_syntax_node_location(
    scxml_syntax_node node) {
    return node.impl != NULL
        ? node.impl->location : (turbo_xml_location){0u, 0u, 0u};
}

static inline turbo_xml_string_view scxml_syntax_node_local_name(
    scxml_syntax_node node) {
    return node.impl != NULL
        ? node.impl->name : (turbo_xml_string_view){NULL, 0u};
}

static inline turbo_xml_string_view scxml_syntax_node_namespace_uri(
    scxml_syntax_node node) {
    return node.impl != NULL
        ? node.impl->namespace_uri : (turbo_xml_string_view){NULL, 0u};
}

static inline turbo_xml_string_view scxml_syntax_node_value(
    scxml_syntax_node node) {
    return node.impl != NULL
        ? node.impl->value : (turbo_xml_string_view){NULL, 0u};
}

static inline turbo_xml_string_view scxml_syntax_node_text(
    scxml_syntax_node node) {
    return node.impl != NULL
        ? node.impl->text : (turbo_xml_string_view){NULL, 0u};
}

static inline size_t scxml_syntax_node_child_count(
    scxml_syntax_node node) {
    return node.impl != NULL ? node.impl->child_count : 0u;
}

static inline scxml_syntax_node scxml_syntax_node_child_at(
    scxml_syntax_node node, size_t index) {
    const scxml_syntax_node result = {
        node.ast,
        node.impl != NULL
            ? scxml_ast_node_child_at(node.ast, node.impl, index) : NULL};
    return result;
}

static inline size_t scxml_syntax_node_attribute_count(
    scxml_syntax_node node) {
    return node.impl != NULL ? node.impl->attribute_count : 0u;
}

static inline scxml_syntax_attribute scxml_syntax_node_attribute_at(
    scxml_syntax_node node, size_t index) {
    const scxml_syntax_attribute result = {
        node.impl != NULL
            ? scxml_ast_node_attribute_at(node.ast, node.impl, index) : NULL};
    return result;
}

static inline turbo_xml_location scxml_syntax_attribute_location(
    scxml_syntax_attribute attribute) {
    return attribute.impl != NULL
        ? attribute.impl->location : (turbo_xml_location){0u, 0u, 0u};
}

static inline turbo_xml_string_view scxml_syntax_attribute_local_name(
    scxml_syntax_attribute attribute) {
    return attribute.impl != NULL
        ? attribute.impl->name : (turbo_xml_string_view){NULL, 0u};
}

static inline turbo_xml_string_view scxml_syntax_attribute_namespace_uri(
    scxml_syntax_attribute attribute) {
    return attribute.impl != NULL
        ? attribute.impl->namespace_uri : (turbo_xml_string_view){NULL, 0u};
}

static inline turbo_xml_string_view scxml_syntax_attribute_value(
    scxml_syntax_attribute attribute) {
    return attribute.impl != NULL
        ? attribute.impl->value : (turbo_xml_string_view){NULL, 0u};
}

static inline scxml_syntax_attribute scxml_syntax_node_find_attribute(
    scxml_syntax_node node, scxml_attribute_kind kind) {
    const scxml_syntax_attribute result = {
        node.impl != NULL
            ? scxml_ast_node_find_attribute(node.ast, node.impl, kind) : NULL};
    return result;
}

static inline turbo_xml_string_view scxml_syntax_serialized_children(
    scxml_syntax_node node) {
    if (node.impl == NULL ||
        (node.impl->kind != SCXML_ELEMENT_CONTENT &&
         node.impl->kind != SCXML_ELEMENT_DATA))
        return (turbo_xml_string_view){NULL, 0u};
    return (turbo_xml_string_view){
        node.impl->serialized_children.data,
        node.impl->serialized_children.size};
}

static inline turbo_xml_status scxml_syntax_serialize_children(
    scxml_syntax_node node, char *output, size_t output_capacity,
    size_t max_bytes, size_t *out_size) {
    size_t required_capacity;
    if (node.impl == NULL || out_size == NULL ||
        (node.impl->kind != SCXML_ELEMENT_CONTENT &&
         node.impl->kind != SCXML_ELEMENT_DATA))
        return TURBO_XML_INVALID_ARGUMENT;
    if (node.impl->serialized_children.size > max_bytes)
        return TURBO_XML_LIMIT_EXCEEDED;
    *out_size = node.impl->serialized_children.size;
    if (output == NULL && output_capacity == 0u) return TURBO_XML_OK;
    if (output == NULL ||
        node.impl->serialized_children.size == SIZE_MAX ||
        (required_capacity = node.impl->serialized_children.size + 1u) >
            output_capacity)
        return TURBO_XML_LIMIT_EXCEEDED;
    if (node.impl->serialized_children.size != 0u)
        memcpy(output, node.impl->serialized_children.data,
               node.impl->serialized_children.size);
    output[node.impl->serialized_children.size] = '\0';
    return TURBO_XML_OK;
}

#endif
