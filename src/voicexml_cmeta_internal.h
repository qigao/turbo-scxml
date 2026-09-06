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
    size_t max_conditional_depth;
} vxml_cmeta_program_data;

typedef struct vxml_cmeta_root_storage {
    void *allocation;
    unsigned char *storage;
    unsigned char *bound;
} vxml_cmeta_root_storage;

typedef struct vxml_cmeta_exec_frame {
    size_t next_action;
    size_t action_end;
} vxml_cmeta_exec_frame;

typedef struct vxml_cmeta_exit_entry {
    vxml_cmeta_name_view name;
    vxml_cmeta_value_view value;
} vxml_cmeta_exit_entry;

typedef struct vxml_cmeta_exit_snapshot {
    vxml_cmeta_exit_kind kind;
    vxml_cmeta_exit_entry *entries;
    size_t count;
    size_t entry_capacity;
    char *names;
    size_t name_size;
    size_t name_capacity;
    char *strings;
    size_t string_size;
    size_t string_capacity;
} vxml_cmeta_exit_snapshot;

typedef struct vxml_cmeta_session_data {
    size_t max_transaction_bytes;
    size_t max_execution_steps;
    size_t transaction_bytes_required;
    size_t execution_steps;
    size_t active_form;
    size_t active_block;
    vxml_cmeta_root_storage committed_root;
    vxml_cmeta_root_storage staged_root;
    cmeta_scope_storage *committed_scopes;
    cmeta_scope_storage *staged_scopes;
    size_t *declared_offsets;
    unsigned char *committed_declared;
    unsigned char *staged_declared;
    size_t declared_count;
    unsigned char *expression_scratch;
    size_t expression_scratch_bytes;
    unsigned char *read_scratch;
    size_t read_scratch_bytes;
    vxml_cmeta_expr_runtime_scope *runtime_scopes;
    size_t runtime_scope_capacity;
    vxml_cmeta_exec_frame *exec_frames;
    size_t exec_frame_capacity;
    vxml_cmeta_exit_snapshot pending_exit;
    vxml_cmeta_exit_snapshot terminal_exit;
    bool exit_requested;
} vxml_cmeta_session_data;

vxml_status vxml_cmeta_session_init_profile(
    vxml_session_impl *session, const void *options);
vxml_status vxml_cmeta_session_start_profile(vxml_session_impl *session);
void vxml_cmeta_session_destroy_profile(vxml_session_impl *session);
void vxml_cmeta_program_destroy_profile(vxml_program_impl *program);

#endif /* TURBO_VOICEXML_CMETA_INTERNAL_H */
