#include "ccxml_internal.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static bool adapter_valid(const ccxml_telephony_adapter_v1 *adapter) {
    const size_t legacy_size =
        offsetof(ccxml_telephony_adapter_v1, is_quiescent) +
        sizeof(adapter->is_quiescent);
    return adapter != NULL &&
           adapter->abi_version == CCXML_TELEPHONY_ADAPTER_ABI_V1 &&
           adapter->struct_size >= legacy_size &&
           adapter->prepare_accept != NULL && adapter->close != NULL &&
           adapter->is_quiescent != NULL;
}

static ccxml_telephony_adapter_v1 copy_adapter(
    const ccxml_telephony_adapter_v1 *adapter) {
    ccxml_telephony_adapter_v1 copy = {0};
    const size_t copy_size = adapter->struct_size < sizeof(copy)
        ? adapter->struct_size : sizeof(copy);
    memcpy(&copy, adapter, copy_size);
    return copy;
}

static void close_adapter(ccxml_session_impl *impl) {
    if (impl == NULL || impl->closed) return;
    impl->closed = true;
    impl->telephony.close(impl->telephony_user);
}

static void discard_tickets(
    cflow_statechart_effect_ticket *tickets, size_t count) {
    while (count != 0u) {
        cflow_statechart_effect_ticket *ticket = &tickets[--count];
        ticket->discard(ticket->user);
        *ticket = (cflow_statechart_effect_ticket){0};
    }
}

static const ccxml_transition_row *find_transition(
    const ccxml_program_impl *program, const ccxml_event *event) {
    size_t index;
    for (index = 0u; index < program->transition_count; ++index) {
        const ccxml_transition_row *transition = &program->transitions[index];
        if (transition->event_size == event->name_size &&
            memcmp(transition->event, event->name, event->name_size) == 0)
            return transition;
    }
    return NULL;
}

static ccxml_status retain_ticket(
    ccxml_session_impl *impl, scxml_adapter_status adapter_status,
    cflow_statechart_effect_ticket ticket, size_t *prepared) {
    if (adapter_status != SCXML_ADAPTER_ACCEPTED) {
        discard_tickets(impl->tickets, *prepared);
        *prepared = 0u;
        return CCXML_ADAPTER_ERROR;
    }
    if (ticket.commit == NULL || ticket.discard == NULL) {
        if (ticket.discard != NULL) ticket.discard(ticket.user);
        discard_tickets(impl->tickets, *prepared);
        *prepared = 0u;
        return CCXML_INVALID_CONTRACT;
    }
    if (*prepared >= impl->ticket_capacity) {
        ticket.discard(ticket.user);
        discard_tickets(impl->tickets, *prepared);
        *prepared = 0u;
        return CCXML_LIMIT_EXCEEDED;
    }
    impl->tickets[(*prepared)++] = ticket;
    return CCXML_OK;
}

ccxml_status ccxml_session_init(
    ccxml_session *session, const ccxml_session_config *config) {
    const ccxml_program_impl *program;
    ccxml_telephony_adapter_v1 telephony;
    ccxml_session_impl *impl;
    if (session == NULL || session->impl != NULL || config == NULL ||
        config->program == NULL || config->program->impl == NULL ||
        !adapter_valid(config->telephony)) {
        return CCXML_INVALID_ARGUMENT;
    }
    program = (const ccxml_program_impl *)config->program->impl;
    telephony = copy_adapter(config->telephony);
    if (program->uses_create_call) {
        const size_t create_call_size =
            offsetof(ccxml_telephony_adapter_v1, prepare_create_call) +
            sizeof(telephony.prepare_create_call);
        if (config->telephony->struct_size < create_call_size ||
            telephony.prepare_create_call == NULL) {
            return CCXML_INVALID_ARGUMENT;
        }
    }
    if (program->max_transition_actions >
        SIZE_MAX / sizeof(cflow_statechart_effect_ticket)) {
        return CCXML_LIMIT_EXCEEDED;
    }
    impl = (ccxml_session_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return CCXML_ALLOCATION_FAILED;
    if (program->max_transition_actions != 0u) {
        impl->tickets = (cflow_statechart_effect_ticket *)calloc(
            program->max_transition_actions, sizeof(*impl->tickets));
        if (impl->tickets == NULL) {
            free(impl);
            return CCXML_ALLOCATION_FAILED;
        }
    }
    impl->program = program;
    impl->telephony = telephony;
    impl->telephony_user = config->telephony_user;
    impl->ticket_capacity = program->max_transition_actions;
    session->impl = impl;
    return CCXML_OK;
}

ccxml_status ccxml_session_dispatch(
    ccxml_session *session, const ccxml_event *event) {
    ccxml_session_impl *impl = session != NULL
        ? (ccxml_session_impl *)session->impl : NULL;
    const ccxml_transition_row *transition;
    size_t index;
    size_t prepared = 0u;
    bool exit_requested = false;
    if (impl == NULL || event == NULL || event->name == NULL ||
        event->name_size == 0u ||
        (event->connection_id == NULL && event->connection_id_size != 0u)) {
        return CCXML_INVALID_ARGUMENT;
    }
    if (impl->closed || impl->terminated) return CCXML_CLOSED;
    transition = find_transition(impl->program, event);
    if (transition == NULL) return CCXML_OK;
    for (index = 0u; index < transition->action_count; ++index) {
        const ccxml_action_row *action =
            &impl->program->actions[transition->first_action + index];
        if (action->kind == CCXML_ACTION_EXIT) {
            exit_requested = true;
            break;
        }
        if (action->kind == CCXML_ACTION_ACCEPT) {
            cflow_statechart_effect_ticket ticket = {0};
            const char *error = NULL;
            scxml_adapter_status adapter_status;
            const ccxml_accept_request request = {
                .connection_id = event->connection_id,
                .connection_id_size = event->connection_id_size};
            if (event->connection_id == NULL ||
                event->connection_id_size == 0u) {
                discard_tickets(impl->tickets, prepared);
                return CCXML_INVALID_EVENT;
            }
            adapter_status = impl->telephony.prepare_accept(
                impl->telephony_user, &request, &ticket, &error);
            (void)error;
            {
                const ccxml_status status = retain_ticket(
                    impl, adapter_status, ticket, &prepared);
                if (status != CCXML_OK) return status;
            }
        }
        if (action->kind == CCXML_ACTION_CREATE_CALL) {
            cflow_statechart_effect_ticket ticket = {0};
            const char *error = NULL;
            const ccxml_create_call_request request = {
                .destination = action->destination,
                .destination_size = action->destination_size};
            const scxml_adapter_status adapter_status =
                impl->telephony.prepare_create_call(
                    impl->telephony_user, &request, &ticket, &error);
            const ccxml_status status = retain_ticket(
                impl, adapter_status, ticket, &prepared);
            (void)error;
            if (status != CCXML_OK) return status;
        }
    }
    for (index = 0u; index < prepared; ++index) {
        cflow_statechart_effect_ticket *ticket = &impl->tickets[index];
        ticket->commit(ticket->user);
        *ticket = (cflow_statechart_effect_ticket){0};
    }
    if (exit_requested) {
        impl->terminated = true;
        close_adapter(impl);
    }
    return CCXML_OK;
}

void ccxml_session_close(ccxml_session *session) {
    ccxml_session_impl *impl = session != NULL
        ? (ccxml_session_impl *)session->impl : NULL;
    close_adapter(impl);
}

bool ccxml_session_is_terminated(const ccxml_session *session) {
    const ccxml_session_impl *impl = session != NULL
        ? (const ccxml_session_impl *)session->impl : NULL;
    return impl != NULL && impl->terminated;
}

ccxml_status ccxml_session_destroy(ccxml_session *session) {
    ccxml_session_impl *impl;
    if (session == NULL) return CCXML_INVALID_ARGUMENT;
    impl = (ccxml_session_impl *)session->impl;
    if (impl == NULL) return CCXML_OK;
    close_adapter(impl);
    if (!impl->telephony.is_quiescent(impl->telephony_user))
        return CCXML_BUSY;
    free(impl->tickets);
    free(impl);
    session->impl = NULL;
    return CCXML_OK;
}
