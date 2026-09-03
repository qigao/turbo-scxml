#ifndef TURBO_CCXML_INTERNAL_H
#define TURBO_CCXML_INTERNAL_H

#include <ccxml/ccxml.h>

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
    CCXML_ACTION_ASSIGN_STRING
} ccxml_action_kind;

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
    char *storage;
    const char *initial_variable;
    size_t initial_variable_size;
    const char *initial_value;
    size_t initial_value_size;
    const char *statevariable;
    size_t statevariable_size;
    size_t transition_count;
    size_t action_count;
    size_t max_transition_actions;
    size_t max_transition_effects;
    bool uses_create_call;
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
} ccxml_program_impl;

typedef struct ccxml_transition_binding ccxml_transition_binding;

typedef struct ccxml_session_impl {
    const ccxml_program_impl *program;
    ccxml_telephony_adapter_v1 telephony;
    void *telephony_user;
    ccxml_datamodel_adapter_v1 datamodel;
    void *datamodel_user;
    cflow_executor executor;
    cflow_statechart_instance instance;
    cflow_statechart_guard_binding *guard_bindings;
    cflow_statechart_executable_binding *executable_bindings;
    ccxml_transition_binding *transition_bindings;
    cflow_statechart_effect_ticket *tickets;
    size_t ticket_capacity;
    size_t prepared_ticket_count;
    ccxml_status dispatch_status;
    bool closed;
    bool terminated;
    bool transition_selected;
    bool exit_requested;
    bool dispatching;
} ccxml_session_impl;

struct ccxml_transition_binding {
    ccxml_session_impl *session;
    const ccxml_transition_row *transition;
    ccxml_condition condition;
};

extern const cmeta_type_desc ccxml_event_cmeta_type;

size_t ccxml_program_transition_count(const ccxml_program *program);
const char *ccxml_program_transition_event(
    const ccxml_program *program, size_t index);
size_t ccxml_program_action_count(const ccxml_program *program);

#endif /* TURBO_CCXML_INTERNAL_H */
