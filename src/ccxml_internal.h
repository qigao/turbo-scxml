#ifndef TURBO_CCXML_INTERNAL_H
#define TURBO_CCXML_INTERNAL_H

#include <ccxml/ccxml.h>

typedef enum ccxml_action_kind {
    CCXML_ACTION_ACCEPT = 1,
    CCXML_ACTION_EXIT,
    CCXML_ACTION_CREATE_CALL,
    CCXML_ACTION_DISCONNECT,
    CCXML_ACTION_REJECT,
    CCXML_ACTION_REDIRECT
} ccxml_action_kind;

typedef struct ccxml_action_row {
    ccxml_action_kind kind;
    const char *destination;
    size_t destination_size;
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
    bool uses_create_call;
    bool uses_disconnect;
    bool uses_reject;
    bool uses_redirect;
} ccxml_program_impl;

typedef struct ccxml_session_impl {
    const ccxml_program_impl *program;
    ccxml_telephony_adapter_v1 telephony;
    void *telephony_user;
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
