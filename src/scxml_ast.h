#ifndef TURBO_SCXML_AST_H
#define TURBO_SCXML_AST_H

#include <scxml/scxml.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum scxml_element_kind {
    SCXML_ELEMENT_UNKNOWN = 0,
    SCXML_ELEMENT_SCXML,
    SCXML_ELEMENT_STATE,
    SCXML_ELEMENT_PARALLEL,
    SCXML_ELEMENT_TRANSITION,
    SCXML_ELEMENT_INITIAL,
    SCXML_ELEMENT_FINAL,
    SCXML_ELEMENT_HISTORY,
    SCXML_ELEMENT_ONENTRY,
    SCXML_ELEMENT_ONEXIT,
    SCXML_ELEMENT_RAISE,
    SCXML_ELEMENT_SEND,
    SCXML_ELEMENT_CANCEL,
    SCXML_ELEMENT_LOG,
    SCXML_ELEMENT_ASSIGN,
    SCXML_ELEMENT_FOREACH,
    SCXML_ELEMENT_IF,
    SCXML_ELEMENT_ELSEIF,
    SCXML_ELEMENT_ELSE,
    SCXML_ELEMENT_INVOKE,
    SCXML_ELEMENT_FINALIZE,
    SCXML_ELEMENT_CONTENT,
    SCXML_ELEMENT_PARAM,
    SCXML_ELEMENT_DATAMODEL,
    SCXML_ELEMENT_DATA,
    SCXML_ELEMENT_DONEDATA,
    SCXML_ELEMENT_SCRIPT
} scxml_element_kind;

typedef enum scxml_attribute_kind {
    SCXML_ATTRIBUTE_UNKNOWN = 0,
    SCXML_ATTRIBUTE_VERSION,
    SCXML_ATTRIBUTE_DATAMODEL,
    SCXML_ATTRIBUTE_INITIAL,
    SCXML_ATTRIBUTE_NAME,
    SCXML_ATTRIBUTE_BINDING,
    SCXML_ATTRIBUTE_ID,
    SCXML_ATTRIBUTE_TYPE,
    SCXML_ATTRIBUTE_EVENT,
    SCXML_ATTRIBUTE_TARGET,
    SCXML_ATTRIBUTE_COND,
    SCXML_ATTRIBUTE_EVENTEXPR,
    SCXML_ATTRIBUTE_TARGETEXPR,
    SCXML_ATTRIBUTE_TYPEEXPR,
    SCXML_ATTRIBUTE_IDLOCATION,
    SCXML_ATTRIBUTE_DELAY,
    SCXML_ATTRIBUTE_DELAYEXPR,
    SCXML_ATTRIBUTE_NAMELIST,
    SCXML_ATTRIBUTE_SENDID,
    SCXML_ATTRIBUTE_SENDIDEXPR,
    SCXML_ATTRIBUTE_LABEL,
    SCXML_ATTRIBUTE_EXPR,
    SCXML_ATTRIBUTE_LOCATION,
    SCXML_ATTRIBUTE_ARRAY,
    SCXML_ATTRIBUTE_ITEM,
    SCXML_ATTRIBUTE_INDEX,
    SCXML_ATTRIBUTE_SRC,
    SCXML_ATTRIBUTE_SRCEXPR,
    SCXML_ATTRIBUTE_AUTOFORWARD
} scxml_attribute_kind;

typedef uint32_t scxml_ast_node_id;
#define SCXML_AST_NODE_NONE UINT32_MAX

typedef turbo_xml_string_view scxml_ast_string_view;

typedef struct scxml_ast_limits {
    size_t max_nodes;
    size_t max_attributes;
    size_t max_storage_bytes;
    size_t max_depth;
} scxml_ast_limits;

typedef struct scxml_ast_attribute {
    scxml_attribute_kind kind;
    scxml_ast_string_view name;
    scxml_ast_string_view namespace_uri;
    scxml_ast_string_view value;
    turbo_xml_location location;
} scxml_ast_attribute;

typedef struct scxml_ast_node {
    scxml_ast_node_id id;
    turbo_xml_node_kind xml_kind;
    scxml_element_kind kind;
    scxml_ast_node_id parent;
    scxml_ast_node_id first_child;
    scxml_ast_node_id next_sibling;
    size_t first_child_index;
    size_t child_count;
    size_t first_attribute;
    size_t attribute_count;
    scxml_ast_string_view name;
    scxml_ast_string_view namespace_uri;
    scxml_ast_string_view value;
    scxml_ast_string_view text;
    scxml_ast_string_view serialized_children;
    turbo_xml_location location;
} scxml_ast_node;

typedef struct scxml_ast {
    void *impl;
} scxml_ast;

scxml_ast_limits scxml_ast_default_limits(void);

/**
 * Copy one TurboXML element tree into bounded immutable SCXML-owned storage.
 * The root and every borrowed XML view are used only for this call.
 */
scxml_status scxml_ast_build(
    scxml_ast *out, turbo_xml_node root,
    const scxml_ast_limits *limits_or_null,
    scxml_diagnostic *diagnostic);

void scxml_ast_destroy(scxml_ast *ast);
size_t scxml_ast_node_count(const scxml_ast *ast);
scxml_ast_node_id scxml_ast_root(const scxml_ast *ast);
const scxml_ast_node *scxml_ast_node_at(
    const scxml_ast *ast, scxml_ast_node_id node);
const scxml_ast_node *scxml_ast_node_child_at(
    const scxml_ast *ast, const scxml_ast_node *node, size_t index);
const scxml_ast_attribute *scxml_ast_node_attribute_at(
    const scxml_ast *ast, const scxml_ast_node *node, size_t index);
const scxml_ast_attribute *scxml_ast_node_find_attribute(
    const scxml_ast *ast, const scxml_ast_node *node,
    scxml_attribute_kind kind);
scxml_attribute_kind scxml_ast_attribute_kind_from_name(
    const char *local_name);

#ifdef __cplusplus
}
#endif

#endif
