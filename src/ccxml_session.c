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

static bool datamodel_adapter_valid(
    const ccxml_datamodel_adapter_v1 *adapter) {
    return adapter != NULL &&
           adapter->abi_version == CCXML_DATAMODEL_ADAPTER_ABI_V1 &&
           adapter->struct_size >= sizeof(*adapter) &&
           adapter->validate_string_location != NULL &&
           adapter->prepare_assign_string != NULL;
}

static ccxml_datamodel_adapter_v1 copy_datamodel_adapter(
    const ccxml_datamodel_adapter_v1 *adapter) {
    ccxml_datamodel_adapter_v1 copy = {0};
    if (adapter != NULL) {
        const size_t copy_size = adapter->struct_size < sizeof(copy)
            ? adapter->struct_size : sizeof(copy);
        memcpy(&copy, adapter, copy_size);
    }
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

static bool connection_id_valid(const ccxml_event *event) {
    return event->connection_id != NULL && event->connection_id_size != 0u &&
           memchr(
               event->connection_id, '\0', event->connection_id_size) == NULL;
}

ccxml_status ccxml_session_init(
    ccxml_session *session, const ccxml_session_config *config) {
    const ccxml_program_impl *program;
    ccxml_telephony_adapter_v1 telephony;
    ccxml_datamodel_adapter_v1 datamodel = {0};
    ccxml_session_impl *impl;
    size_t action_index;
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
    if (program->uses_disconnect) {
        const size_t disconnect_size =
            offsetof(ccxml_telephony_adapter_v1, prepare_disconnect) +
            sizeof(telephony.prepare_disconnect);
        if (config->telephony->struct_size < disconnect_size ||
            telephony.prepare_disconnect == NULL) {
            return CCXML_INVALID_ARGUMENT;
        }
    }
    if (program->uses_reject) {
        const size_t reject_size =
            offsetof(ccxml_telephony_adapter_v1, prepare_reject) +
            sizeof(telephony.prepare_reject);
        if (config->telephony->struct_size < reject_size ||
            telephony.prepare_reject == NULL) {
            return CCXML_INVALID_ARGUMENT;
        }
    }
    if (program->uses_redirect) {
        const size_t redirect_size =
            offsetof(ccxml_telephony_adapter_v1, prepare_redirect) +
            sizeof(telephony.prepare_redirect);
        if (config->telephony->struct_size < redirect_size ||
            telephony.prepare_redirect == NULL) {
            return CCXML_INVALID_ARGUMENT;
        }
    }
    if (program->uses_join) {
        const size_t join_size =
            offsetof(ccxml_telephony_adapter_v1, prepare_join) +
            sizeof(telephony.prepare_join);
        if (config->telephony->struct_size < join_size ||
            telephony.prepare_join == NULL) {
            return CCXML_INVALID_ARGUMENT;
        }
    }
    if (program->uses_unjoin) {
        const size_t unjoin_size =
            offsetof(ccxml_telephony_adapter_v1, prepare_unjoin) +
            sizeof(telephony.prepare_unjoin);
        if (config->telephony->struct_size < unjoin_size ||
            telephony.prepare_unjoin == NULL) {
            return CCXML_INVALID_ARGUMENT;
        }
    }
    if (program->uses_merge) {
        const size_t merge_size =
            offsetof(ccxml_telephony_adapter_v1, prepare_merge) +
            sizeof(telephony.prepare_merge);
        if (config->telephony->struct_size < merge_size ||
            telephony.prepare_merge == NULL) {
            return CCXML_INVALID_ARGUMENT;
        }
    }
    if (program->uses_create_conference) {
        const size_t create_conference_size =
            offsetof(
                ccxml_telephony_adapter_v1,
                prepare_create_conference) +
            sizeof(telephony.prepare_create_conference);
        if (config->telephony->struct_size < create_conference_size ||
            telephony.prepare_create_conference == NULL ||
            !datamodel_adapter_valid(config->datamodel)) {
            return CCXML_INVALID_ARGUMENT;
        }
        datamodel = copy_datamodel_adapter(config->datamodel);
        for (action_index = 0u; action_index < program->action_count;
             ++action_index) {
            const ccxml_action_row *action = &program->actions[action_index];
            const char *error = NULL;
            if (action->kind == CCXML_ACTION_CREATE_CONFERENCE &&
                datamodel.validate_string_location(
                    config->datamodel_user, action->location,
                    action->location_size, &error) !=
                    SCXML_ADAPTER_ACCEPTED) {
                (void)error;
                return CCXML_INVALID_ARGUMENT;
            }
        }
    }
    if (program->max_transition_effects >
        SIZE_MAX / sizeof(cflow_statechart_effect_ticket)) {
        return CCXML_LIMIT_EXCEEDED;
    }
    impl = (ccxml_session_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return CCXML_ALLOCATION_FAILED;
    if (program->max_transition_effects != 0u) {
        impl->tickets = (cflow_statechart_effect_ticket *)calloc(
            program->max_transition_effects, sizeof(*impl->tickets));
        if (impl->tickets == NULL) {
            free(impl);
            return CCXML_ALLOCATION_FAILED;
        }
    }
    impl->program = program;
    impl->telephony = telephony;
    impl->telephony_user = config->telephony_user;
    impl->datamodel = datamodel;
    impl->datamodel_user = config->datamodel_user;
    impl->ticket_capacity = program->max_transition_effects;
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
        if (action->kind == CCXML_ACTION_DISCONNECT) {
            cflow_statechart_effect_ticket ticket = {0};
            const char *error = NULL;
            scxml_adapter_status adapter_status;
            const ccxml_disconnect_request request = {
                .connection_id = event->connection_id,
                .connection_id_size = event->connection_id_size};
            if (!connection_id_valid(event)) {
                discard_tickets(impl->tickets, prepared);
                return CCXML_INVALID_EVENT;
            }
            adapter_status = impl->telephony.prepare_disconnect(
                impl->telephony_user, &request, &ticket, &error);
            (void)error;
            {
                const ccxml_status status = retain_ticket(
                    impl, adapter_status, ticket, &prepared);
                if (status != CCXML_OK) return status;
            }
        }
        if (action->kind == CCXML_ACTION_REJECT) {
            cflow_statechart_effect_ticket ticket = {0};
            const char *error = NULL;
            scxml_adapter_status adapter_status;
            const ccxml_reject_request request = {
                .connection_id = event->connection_id,
                .connection_id_size = event->connection_id_size};
            if (!connection_id_valid(event)) {
                discard_tickets(impl->tickets, prepared);
                return CCXML_INVALID_EVENT;
            }
            adapter_status = impl->telephony.prepare_reject(
                impl->telephony_user, &request, &ticket, &error);
            (void)error;
            {
                const ccxml_status status = retain_ticket(
                    impl, adapter_status, ticket, &prepared);
                if (status != CCXML_OK) return status;
            }
        }
        if (action->kind == CCXML_ACTION_REDIRECT) {
            cflow_statechart_effect_ticket ticket = {0};
            const char *error = NULL;
            scxml_adapter_status adapter_status;
            const ccxml_redirect_request request = {
                .connection_id = event->connection_id,
                .connection_id_size = event->connection_id_size,
                .destination = action->destination,
                .destination_size = action->destination_size};
            if (!connection_id_valid(event)) {
                discard_tickets(impl->tickets, prepared);
                return CCXML_INVALID_EVENT;
            }
            adapter_status = impl->telephony.prepare_redirect(
                impl->telephony_user, &request, &ticket, &error);
            (void)error;
            {
                const ccxml_status status = retain_ticket(
                    impl, adapter_status, ticket, &prepared);
                if (status != CCXML_OK) return status;
            }
        }
        if (action->kind == CCXML_ACTION_JOIN) {
            cflow_statechart_effect_ticket ticket = {0};
            const char *error = NULL;
            const ccxml_join_request request = {
                .id1 = action->id1,
                .id1_size = action->id1_size,
                .id2 = action->id2,
                .id2_size = action->id2_size};
            const scxml_adapter_status adapter_status =
                impl->telephony.prepare_join(
                    impl->telephony_user, &request, &ticket, &error);
            const ccxml_status status = retain_ticket(
                impl, adapter_status, ticket, &prepared);
            (void)error;
            if (status != CCXML_OK) return status;
        }
        if (action->kind == CCXML_ACTION_UNJOIN) {
            cflow_statechart_effect_ticket ticket = {0};
            const char *error = NULL;
            const ccxml_unjoin_request request = {
                .id1 = action->id1,
                .id1_size = action->id1_size,
                .id2 = action->id2,
                .id2_size = action->id2_size};
            const scxml_adapter_status adapter_status =
                impl->telephony.prepare_unjoin(
                    impl->telephony_user, &request, &ticket, &error);
            const ccxml_status status = retain_ticket(
                impl, adapter_status, ticket, &prepared);
            (void)error;
            if (status != CCXML_OK) return status;
        }
        if (action->kind == CCXML_ACTION_MERGE) {
            cflow_statechart_effect_ticket ticket = {0};
            const char *error = NULL;
            const ccxml_merge_request request = {
                .connection_id1 = action->id1,
                .connection_id1_size = action->id1_size,
                .connection_id2 = action->id2,
                .connection_id2_size = action->id2_size};
            const scxml_adapter_status adapter_status =
                impl->telephony.prepare_merge(
                    impl->telephony_user, &request, &ticket, &error);
            const ccxml_status status = retain_ticket(
                impl, adapter_status, ticket, &prepared);
            (void)error;
            if (status != CCXML_OK) return status;
        }
        if (action->kind == CCXML_ACTION_CREATE_CONFERENCE) {
            cflow_statechart_effect_ticket provider_ticket = {0};
            cflow_statechart_effect_ticket datamodel_ticket = {0};
            ccxml_string_view conference_id = {0};
            const char *error = NULL;
            const ccxml_create_conference_request request = {
                .conference_name = action->destination,
                .conference_name_size = action->destination_size};
            scxml_adapter_status adapter_status =
                impl->telephony.prepare_create_conference(
                    impl->telephony_user, &request, &conference_id,
                    &provider_ticket, &error);
            ccxml_status status = retain_ticket(
                impl, adapter_status, provider_ticket, &prepared);
            (void)error;
            if (status != CCXML_OK) return status;
            if (conference_id.data == NULL || conference_id.size == 0u ||
                memchr(conference_id.data, '\0', conference_id.size) != NULL) {
                discard_tickets(impl->tickets, prepared);
                return CCXML_INVALID_CONTRACT;
            }
            error = NULL;
            adapter_status = impl->datamodel.prepare_assign_string(
                impl->datamodel_user, action->location,
                action->location_size, conference_id.data,
                conference_id.size, &datamodel_ticket, &error);
            status = retain_ticket(
                impl, adapter_status, datamodel_ticket, &prepared);
            (void)error;
            if (status != CCXML_OK) return status;
            {
                cflow_statechart_effect_ticket swap =
                    impl->tickets[prepared - 2u];
                impl->tickets[prepared - 2u] =
                    impl->tickets[prepared - 1u];
                impl->tickets[prepared - 1u] = swap;
            }
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
