#ifndef TURBO_VOICEXML_CMETA_INTERNAL_H
#define TURBO_VOICEXML_CMETA_INTERNAL_H

#include "cmeta_location.h"
#include "voicexml_cmeta_expr.h"
#include "voicexml_internal.h"

#include <data_bind_native.h>
#include <stdatomic.h>

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
    VXML_CMETA_ACTION_EXIT,
    VXML_CMETA_ACTION_RETURN,
    VXML_CMETA_ACTION_DISCONNECT,
    VXML_CMETA_ACTION_THROW,
    VXML_CMETA_ACTION_RETHROW,
    VXML_CMETA_ACTION_REPROMPT
} vxml_cmeta_action_kind;

typedef struct vxml_cmeta_scope_row {
    vxml_cmeta_scope_kind kind;
    size_t owner;
    cmeta_scope_schema schema;
} vxml_cmeta_scope_row;

typedef enum vxml_cmeta_form_item_kind {
    VXML_CMETA_FORM_ITEM_FIELD = 1,
    VXML_CMETA_FORM_ITEM_INITIAL
} vxml_cmeta_form_item_kind;

typedef struct vxml_cmeta_form_item_row {
    vxml_cmeta_form_item_kind kind;
    size_t index;
} vxml_cmeta_form_item_row;

typedef struct vxml_cmeta_initial_row {
    size_t form;
    size_t form_item_slot;
    const char *name;
    size_t name_size;
    size_t initial_expression;
    size_t condition;
} vxml_cmeta_initial_row;

typedef struct vxml_cmeta_form_row {
    size_t scope;
    size_t first_declaration;
    size_t declaration_count;
    size_t first_field;
    size_t field_count;
    size_t first_initial;
    size_t initial_count;
    size_t first_item;
    size_t item_count;
    size_t first_filled;
    size_t filled_count;
    size_t first_block;
    size_t block_count;
    size_t menu;
    const char *grammar_type;
    size_t grammar_type_size;
    const char *grammar_src;
    size_t grammar_src_size;
    uint64_t grammar_required_capabilities;
} vxml_cmeta_form_row;

typedef struct vxml_cmeta_prompt_row {
    size_t field;
    const char *text;
    size_t text_size;
    vxml_cmeta_prompt_media_segment_kind media_kind;
    const char *media_payload;
    size_t media_payload_size;
    size_t first_segment;
    size_t segment_count;
    size_t first_fallback;
    size_t fallback_count;
    uint64_t required_capabilities;
    bool bargein;
    vxml_cmeta_prompt_bargein_type bargein_type;
    bool has_timeout;
    uint64_t timeout_us;
    unsigned count;
    size_t condition;
} vxml_cmeta_prompt_row;

typedef enum vxml_cmeta_menu_choice_target_kind {
    VXML_CMETA_MENU_CHOICE_EVENT = 1,
    VXML_CMETA_MENU_CHOICE_NEXT
} vxml_cmeta_menu_choice_target_kind;

typedef struct vxml_cmeta_menu_choice_target_row {
    vxml_cmeta_menu_choice_target_kind kind;
    const char *target;
    size_t target_size;
} vxml_cmeta_menu_choice_target_row;

typedef struct vxml_cmeta_menu_row {
    size_t form;
    size_t first_choice;
    size_t choice_count;
    size_t first_speech_policy;
    size_t speech_policy_count;
    size_t first_grammar;
    size_t grammar_count;
} vxml_cmeta_menu_row;

typedef struct vxml_cmeta_field_row {
    size_t form;
    size_t root_field;
    size_t field_offset;
    const cmeta_data_desc *field_data;
    const char *name;
    size_t name_size;
    size_t condition;
    const char *grammar_type;
    size_t grammar_type_size;
    const char *grammar_src;
    size_t grammar_src_size;
    uint64_t required_capabilities;
    size_t first_prompt;
    size_t prompt_count;
    size_t filled;
} vxml_cmeta_field_row;

typedef enum vxml_cmeta_filled_mode {
    VXML_CMETA_FILLED_FIELD = 0,
    VXML_CMETA_FILLED_ALL,
    VXML_CMETA_FILLED_ANY
} vxml_cmeta_filled_mode;

typedef struct vxml_cmeta_filled_row {
    size_t form;
    size_t field;
    vxml_cmeta_filled_mode mode;
    size_t first_target;
    size_t target_count;
    size_t first_action;
    size_t action_end;
} vxml_cmeta_filled_row;

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

typedef enum vxml_cmeta_event_scope_kind {
    VXML_CMETA_EVENT_DOCUMENT = 0,
    VXML_CMETA_EVENT_FORM,
    VXML_CMETA_EVENT_FIELD,
    VXML_CMETA_EVENT_INITIAL
} vxml_cmeta_event_scope_kind;

typedef struct vxml_cmeta_event_handler_row {
    vxml_cmeta_event_scope_kind scope_kind;
    size_t owner;
    const char *event;
    size_t event_size;
    unsigned count;
    size_t first_action;
    size_t action_end;
} vxml_cmeta_event_handler_row;

typedef struct vxml_cmeta_event_counter {
    vxml_cmeta_event_scope_kind scope_kind;
    size_t owner;
    char *event;
    size_t event_size;
    unsigned count;
} vxml_cmeta_event_counter;

typedef struct vxml_cmeta_external_data_row {
    const char *name;
    size_t name_size;
    const char *uri;
    size_t uri_size;
    size_t field_index;
    size_t field_offset;
    const cmeta_data_desc *field_data;
    DataBindNativePlan *plan;
    size_t decode_workspace_bytes;
    size_t workspace_alignment;
} vxml_cmeta_external_data_row;

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
    const char *event_name;
    size_t event_name_size;
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
    vxml_cmeta_menu_row *menus;
    size_t menu_count;
    vxml_cmeta_menu_choice_v1 *menu_choices;
    vxml_cmeta_menu_choice_target_row *menu_choice_targets;
    size_t menu_choice_count;
    vxml_cmeta_menu_speech_policy_v1 *menu_speech_policies;
    size_t menu_speech_policy_count;
    vxml_cmeta_menu_grammar_ref_v1 *menu_grammars;
    size_t menu_grammar_count;
    vxml_cmeta_field_row *fields;
    size_t field_count;
    vxml_cmeta_initial_row *initials;
    size_t initial_count;
    vxml_cmeta_form_item_row *form_items;
    size_t form_item_count;
    vxml_cmeta_prompt_row *prompts;
    size_t prompt_count;
    vxml_cmeta_prompt_media_segment_v1 *prompt_segments;
    size_t prompt_segment_count;
    vxml_cmeta_prompt_media_fallback_v1 *prompt_fallbacks;
    size_t prompt_fallback_count;
    vxml_cmeta_filled_row *filled;
    size_t filled_count;
    size_t *filled_root_fields;
    size_t filled_root_field_count;
    vxml_cmeta_event_handler_row *event_handlers;
    size_t event_handler_count;
    vxml_cmeta_block_row *blocks;
    size_t block_count;
    vxml_cmeta_declaration_row *declarations;
    size_t declaration_count;
    size_t first_document_declaration;
    size_t document_declaration_count;
    vxml_cmeta_external_data_row *external_data;
    size_t external_data_count;
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
    size_t max_data_bind_depth;
    size_t max_data_bind_items;
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

typedef enum vxml_cmeta_collect_item_kind {
    VXML_CMETA_COLLECT_ITEM_FIELD = 0,
    VXML_CMETA_COLLECT_ITEM_MENU,
    VXML_CMETA_COLLECT_ITEM_INITIAL
} vxml_cmeta_collect_item_kind;

typedef enum vxml_cmeta_collect_mailbox_state {
    VXML_CMETA_COLLECT_MAILBOX_DISARMED = 0,
    VXML_CMETA_COLLECT_MAILBOX_EMPTY,
    VXML_CMETA_COLLECT_MAILBOX_WRITING,
    VXML_CMETA_COLLECT_MAILBOX_READY,
    VXML_CMETA_COLLECT_MAILBOX_CLOSED
} vxml_cmeta_collect_mailbox_state;

typedef struct vxml_cmeta_collect_mailbox {
    atomic_uint state;
    atomic_uint_fast64_t generation;
    vxml_cmeta_collect_item_kind item_kind;
    size_t choice_index;
    const cmeta_data_desc *data;
    void *allocation;
    unsigned char *storage;
    size_t storage_bytes;
    size_t storage_alignment;
    size_t storage_stride;
    size_t *root_fields;
    size_t slot_count;
    size_t slot_capacity;
} vxml_cmeta_collect_mailbox;

typedef enum vxml_cmeta_prompt_media_mailbox_state {
    VXML_CMETA_PROMPT_MEDIA_MAILBOX_DISARMED = 0,
    VXML_CMETA_PROMPT_MEDIA_MAILBOX_EMPTY,
    VXML_CMETA_PROMPT_MEDIA_MAILBOX_WRITING,
    VXML_CMETA_PROMPT_MEDIA_MAILBOX_READY,
    VXML_CMETA_PROMPT_MEDIA_MAILBOX_CLOSED
} vxml_cmeta_prompt_media_mailbox_state;

typedef struct vxml_cmeta_prompt_media_mailbox {
    atomic_uint state;
    atomic_uint_fast64_t generation;
    vxml_cmeta_prompt_media_outcome outcome;
    vxml_cmeta_prompt_media_failure failure;
} vxml_cmeta_prompt_media_mailbox;

typedef struct vxml_cmeta_session_data {
    size_t max_transaction_bytes;
    size_t max_execution_steps;
    size_t transaction_bytes_required;
    size_t execution_steps;
    size_t active_form;
    size_t active_field;
    size_t active_initial;
    size_t active_menu;
    size_t active_block;
    uint64_t collect_generation;
    const vxml_cmeta_collect_adapter_v1 *collect_adapter;
    void *collect_user;
    vxml_cmeta_collect_ticket_v1 collect_ticket;
    bool collect_prepared;
    bool collect_in_flight;
    vxml_cmeta_collect_mailbox collect_mailbox;
    vxml_cmeta_event_counter *event_counters;
    size_t event_counter_count;
    unsigned char *retry_reset_pending;
    unsigned char *initial_retry_reset_pending;
    size_t event_counter_capacity;
    char *event_counter_names;
    size_t event_name_stride;
    size_t max_event_dispatch_depth;
    const char *thrown_event;
    size_t thrown_event_size;
    bool throw_requested;
    bool rethrow_requested;
    bool handler_reprompt_requested;
    bool reprompt_requested;
    bool event_dispatch_active;
    const vxml_cmeta_prompt_media_adapter_v1 *prompt_media_adapter;
    void *prompt_media_user;
    vxml_cmeta_prompt_media_ticket_v1 prompt_media_ticket;
    uint64_t prompt_media_generation;
    uint64_t prompt_media_barged_generation;
    uint64_t prompt_media_mark_generation;
    size_t prompt_media_last_mark_segment;
    bool prompt_media_prepared;
    bool prompt_media_in_flight;
    vxml_cmeta_prompt_media_mailbox prompt_media_mailbox;
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
    unsigned char *data_workspace_allocation;
    unsigned char *data_workspace;
    size_t data_workspace_bytes;
    unsigned char *data_value_allocation;
    unsigned char *data_value;
    size_t data_value_bytes;
    vxml_cmeta_expr_runtime_scope *runtime_scopes;
    size_t runtime_scope_capacity;
    vxml_cmeta_exec_frame *exec_frames;
    size_t exec_frame_capacity;
    vxml_cmeta_exit_snapshot pending_exit;
    vxml_cmeta_exit_snapshot terminal_exit;
    vxml_cmeta_terminal_kind pending_terminal_kind;
    const char *pending_terminal_event;
    size_t pending_terminal_event_size;
    vxml_cmeta_terminal_kind terminal_kind;
    const char *terminal_event;
    size_t terminal_event_size;
    bool exit_requested;
} vxml_cmeta_session_data;

vxml_status vxml_cmeta_session_init_profile(
    vxml_session_impl *session, const void *options);
vxml_status vxml_cmeta_session_start_profile(vxml_session_impl *session);
vxml_status vxml_cmeta_session_start_profile_at(
    vxml_session_impl *session, size_t form_index);
vxml_status vxml_cmeta_session_raise_event_profile(
    vxml_session_impl *session,
    const char *event_name,
    size_t event_name_size);
void vxml_cmeta_session_destroy_profile(vxml_session_impl *session);
void vxml_cmeta_program_destroy_profile(vxml_program_impl *program);

#endif /* TURBO_VOICEXML_CMETA_INTERNAL_H */
