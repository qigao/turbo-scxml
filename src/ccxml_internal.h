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
    CCXML_ACTION_DIALOG_START,
    CCXML_ACTION_DIALOG_TERMINATE
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
    size_t first_action;
    size_t action_count;
} ccxml_transition_row;

typedef struct ccxml_program_impl {
    ccxml_transition_row *transitions;
    ccxml_action_row *actions;
    char *storage;
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
    bool uses_dialog_start;
    bool uses_dialog_terminate;
} ccxml_program_impl;

typedef struct ccxml_session_impl {
    const ccxml_program_impl *program;
    ccxml_telephony_adapter_v1 telephony;
    void *telephony_user;
    ccxml_datamodel_adapter_v1 datamodel;
    void *datamodel_user;
    cflow_statechart_effect_ticket *tickets;
    size_t ticket_capacity;
    bool closed;
    bool terminated;
} ccxml_session_impl;

size_t ccxml_program_transition_count(const ccxml_program *program);
const char *ccxml_program_transition_event(
    const ccxml_program *program, size_t index);
size_t ccxml_program_action_count(const ccxml_program *program);

#endif /* TURBO_CCXML_INTERNAL_H */
