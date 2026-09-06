#ifndef TURBO_VOICEXML_CMETA_INTERNAL_H
#define TURBO_VOICEXML_CMETA_INTERNAL_H

#include "cmeta_location.h"
#include "voicexml_cmeta_expr.h"
#include "voicexml_internal.h"

#define VXML_CMETA_NO_INDEX ((size_t)-1)

typedef enum vxml_cmeta_scope_kind {
    VXML_CMETA_SCOPE_DOCUMENT = 0,
    VXML_CMETA_SCOPE_FORM,
    VXML_CMETA_SCOPE_BLOCK
} vxml_cmeta_scope_kind;

typedef enum vxml_cmeta_action_kind {
    VXML_CMETA_ACTION_VAR = 0,
    VXML_CMETA_ACTION_ASSIGN,
    VXML_CMETA_ACTION_CLEAR,
    VXML_CMETA_ACTION_IF,
    VXML_CMETA_ACTION_EXIT
} vxml_cmeta_action_kind;

typedef struct vxml_cmeta_scope_row {
    vxml_cmeta_scope_kind kind;
    size_t owner;
    cmeta_scope_schema schema;
} vxml_cmeta_scope_row;

typedef struct vxml_cmeta_form_row {
    size_t scope;
    size_t first_declaration;
    size_t declaration_count;
    size_t first_block;
    size_t block_count;
} vxml_cmeta_form_row;

typedef struct vxml_cmeta_block_row {
    size_t form;
    size_t scope;
    size_t form_item_slot;
    const char *name;
    size_t name_size;
    size_t initial_expression;
    size_t condition;
    size_t first_action;
    size_t action_end;
} vxml_cmeta_block_row;

typedef struct vxml_cmeta_declaration_row {
    size_t scope;
    size_t slot;
    const char *name;
    size_t name_size;
    size_t expression;
    salts_xml_location location;
} vxml_cmeta_declaration_row;

typedef struct vxml_cmeta_expression_row {
    vxml_cmeta_expr_program program;
    salts_xml_location location;
} vxml_cmeta_expression_row;

typedef struct vxml_cmeta_location_candidate_row {
    size_t scope;
    size_t root_field;
    const cmeta_scope_schema *schema;
    cmeta_location location;
} vxml_cmeta_location_candidate_row;

typedef struct vxml_cmeta_location_row {
    const char *name;
    size_t name_size;
    const cmeta_data_desc *value;
    size_t first_candidate;
    size_t candidate_count;
    salts_xml_location location;
} vxml_cmeta_location_row;

typedef struct vxml_cmeta_branch_row {
    size_t condition;
    size_t first_action;
    size_t action_end;
    salts_xml_location location;
} vxml_cmeta_branch_row;

typedef struct vxml_cmeta_action_row {
    vxml_cmeta_action_kind kind;
    salts_xml_location location;
    size_t next_action;
    size_t scope;
    size_t slot;
    size_t target;
    size_t expression;
    size_t first_location;
    size_t location_count;
    size_t first_branch;
    size_t branch_count;
    vxml_cmeta_exit_kind exit_kind;
    bool clear_all_form_items;
} vxml_cmeta_action_row;

typedef struct vxml_cmeta_program_data {
    const cmeta_data_desc *root;
    const cmeta_data_desc **semantic_data;
    size_t semantic_data_count;
    vxml_cmeta_scope_row *scopes;
    size_t scope_count;
    size_t document_scope;
    vxml_cmeta_form_row *forms;
    size_t form_count;
    vxml_cmeta_block_row *blocks;
    size_t block_count;
    vxml_cmeta_declaration_row *declarations;
    size_t declaration_count;
    size_t first_document_declaration;
    size_t document_declaration_count;
    vxml_cmeta_action_row *actions;
    size_t action_count;
    vxml_cmeta_branch_row *branches;
    size_t branch_count;
    vxml_cmeta_expression_row *expressions;
    size_t expression_count;
    vxml_cmeta_location_row *locations;
    size_t location_count;
    vxml_cmeta_location_candidate_row *location_candidates;
    size_t location_candidate_count;
    char *strings;
    size_t string_size;
    size_t expression_scratch_bytes;
    size_t max_string_bytes;
} vxml_cmeta_program_data;

vxml_status vxml_cmeta_session_init_profile(
    vxml_session_impl *session, const void *options);
vxml_status vxml_cmeta_session_start_profile(vxml_session_impl *session);
void vxml_cmeta_session_destroy_profile(vxml_session_impl *session);
void vxml_cmeta_program_destroy_profile(vxml_program_impl *program);

#endif /* TURBO_VOICEXML_CMETA_INTERNAL_H */
