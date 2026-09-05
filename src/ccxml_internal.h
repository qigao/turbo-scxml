#ifndef TURBO_CCXML_INTERNAL_H
#define TURBO_CCXML_INTERNAL_H

#include <ccxml/ccxml.h>
#include <salts_uuid.h>

#include "scxml_foreach.h"
#include "scxml_scope.h"

#define CCXML_SEND_ID_MAX_SIZE \
    ((sizeof("send.") - 1u) + SALTS_UUID_STRING_LENGTH + 1u + 20u)

typedef enum ccxml_action_kind {
    CCXML_ACTION_ACCEPT = 1,
    CCXML_ACTION_EXIT,
    CCXML_ACTION_CREATE_CALL,
    CCXML_ACTION_DISCONNECT,
    CCXML_ACTION_REJECT,
    CCXML_ACTION_REDIRECT,
    CCXML_ACTION_JOIN,
    CCXML_ACTION_UNJOIN,
    CCXML_ACTION_MERGE,
    CCXML_ACTION_CREATE_CONFERENCE,
    CCXML_ACTION_DESTROY_CONFERENCE,
    CCXML_ACTION_DIALOG_PREPARE,
    CCXML_ACTION_DIALOG_START,
    CCXML_ACTION_PREPARED_DIALOG_START,
    CCXML_ACTION_DIALOG_TERMINATE,
    CCXML_ACTION_ASSIGN_STRING,
    CCXML_ACTION_SEND,
    CCXML_ACTION_CANCEL,
    CCXML_ACTION_IF,
    CCXML_ACTION_ELSEIF,
    CCXML_ACTION_ELSE,
    CCXML_ACTION_ENDIF,
    CCXML_ACTION_FOREACH,
    CCXML_ACTION_ENDFOREACH
} ccxml_action_kind;

typedef struct ccxml_payload_row {
    const char *name;
    size_t name_size;
} ccxml_payload_row;

typedef struct ccxml_action_row {
    ccxml_action_kind kind;
    const char *destination;
    size_t destination_size;
    const char *id1;
    size_t id1_size;
    const char *id2;
    size_t id2_size;
    const char *location;
    size_t location_size;
    const char *name;
    size_t name_size;
    const char *index;
    size_t index_size;
    const char *target_type;
    size_t target_type_size;
    const char *condition;
    size_t condition_size;
    const char *delay;
    size_t delay_size;
    uint64_t delay_ms;
    bool destination_is_dynamic;
    bool delay_is_dynamic;
    size_t branch_next;
    size_t block_end;
    size_t payload_first;
    size_t payload_count;
} ccxml_action_row;

typedef struct ccxml_transition_row {
    const char *event;
    size_t event_size;
    const char *state;
    size_t state_size;
    const char *condition;
    size_t condition_size;
    size_t first_action;
    size_t action_count;
} ccxml_transition_row;

typedef struct ccxml_program_impl {
    cflow_statechart statechart;
    ccxml_transition_row *transitions;
    ccxml_action_row *actions;
    ccxml_payload_row *payloads;
    char *storage;
    const char *initial_variable;
    size_t initial_variable_size;
    const char *initial_value;
    size_t initial_value_size;
    const char *statevariable;
    size_t statevariable_size;
    size_t transition_count;
    size_t action_count;
    size_t payload_count;
    size_t max_send_payload_entries;
    size_t max_transition_actions;
    size_t max_transition_effects;
    size_t max_foreach_iterations;
    size_t max_foreach_storage_bytes;
    bool uses_create_call;
    bool uses_string_expression;
    bool uses_disconnect;
    bool uses_reject;
    bool uses_redirect;
    bool uses_join;
    bool uses_unjoin;
    bool uses_merge;
    bool uses_create_conference;
    bool uses_destroy_conference;
    bool uses_datamodel_read;
    bool uses_dialog_prepare;
    bool uses_dialog_start;
    bool uses_prepared_dialog_start;
    bool uses_dialog_terminate;
    bool uses_assign;
    bool uses_statevariable;
    bool uses_condition;
    bool uses_send;
    bool uses_send_payload;
    bool uses_send_delay;
    bool uses_send_id;
    bool uses_delayed_send;
    bool uses_cancel;
    bool uses_foreach;
} ccxml_program_impl;

typedef struct ccxml_transition_binding ccxml_transition_binding;

typedef struct ccxml_conditional_frame {
    size_t block_end;
    bool branch_taken;
} ccxml_conditional_frame;

typedef struct ccxml_foreach_frame {
    size_t action_index;
    size_t iteration;
    size_t length;
    bool live;
    scxml_foreach_snapshot snapshot;
    scxml_foreach_value value;
} ccxml_foreach_frame;

typedef struct ccxml_session_impl {
    const ccxml_program_impl *program;
    ccxml_telephony_adapter_v1 telephony;
    void *telephony_user;
    ccxml_datamodel_adapter_v1 datamodel;
    void *datamodel_user;
    scxml_event_io_adapter event_io;
    void *event_io_user;
    cflow_executor executor;
    cflow_statechart_instance instance;
    cflow_statechart_guard_binding *guard_bindings;
    cflow_statechart_executable_binding *executable_bindings;
    ccxml_transition_binding *transition_bindings;
    ccxml_condition *action_conditions;
    ccxml_string_expression *action_string_expressions;
    ccxml_conditional_frame *conditional_frames;
    scxml_scope_schema foreach_scope;
    scxml_scope_view foreach_scope_committed;
    scxml_scope_view foreach_scope_staged;
    scxml_scope_view foreach_scope_checkpoint;
    void *foreach_scope_committed_allocation;
    void *foreach_scope_staged_allocation;
    void *foreach_scope_checkpoint_allocation;
    scxml_foreach_program *foreach_programs;
    void *foreach_root;
    ccxml_foreach_frame foreach_frame;
    cflow_statechart_effect_ticket *tickets;
    scxml_payload_entry *payload_scratch;
    size_t ticket_capacity;
    size_t payload_scratch_capacity;
    size_t conditional_frame_capacity;
    size_t prepared_ticket_count;
    char send_namespace[SALTS_UUID_STRING_SIZE];
    uint64_t next_send_token;
    ccxml_status dispatch_status;
    bool closed;
    bool terminated;
    bool transition_selected;
    bool exit_requested;
    bool dispatching;
    bool foreach_transaction_pending;
    bool foreach_checkpoint_live;
} ccxml_session_impl;

struct ccxml_transition_binding {
    ccxml_session_impl *session;
    const ccxml_transition_row *transition;
    ccxml_condition condition;
};

extern const cmeta_type_desc ccxml_event_cmeta_type;

scxml_adapter_status ccxml_cmeta_compile_foreach_scope(
    void *user, const char *array, size_t array_size,
    const char *item, size_t item_size,
    const char *index_or_null, size_t index_size,
    size_t max_iterations,
    scxml_scope_schema *scope, scxml_foreach_program *out_program,
    const char **out_error);

scxml_adapter_status ccxml_cmeta_compile_condition_with_scope(
    void *user, const char *source, size_t source_size,
    const scxml_scope_schema *scope,
    ccxml_condition *out_condition, const char **out_error);

scxml_adapter_status ccxml_cmeta_evaluate_condition_with_scope(
    void *user, const ccxml_condition *condition,
    const ccxml_event *event, scxml_scope_view *scope,
    bool *out_value, const char **out_error);

bool ccxml_cmeta_datamodel_state(void *user, void **out_state);

size_t ccxml_program_transition_count(const ccxml_program *program);
const char *ccxml_program_transition_event(
    const ccxml_program *program, size_t index);
size_t ccxml_program_action_count(const ccxml_program *program);

#endif /* TURBO_CCXML_INTERNAL_H */
