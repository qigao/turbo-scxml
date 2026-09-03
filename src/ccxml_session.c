#include "ccxml_internal.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
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

static bool event_io_send_adapter_valid(
    const scxml_event_io_adapter *adapter) {
    const uint64_t known = SCXML_EVENT_IO_CAP_SEND |
        SCXML_EVENT_IO_CAP_DELAYED_SEND | SCXML_EVENT_IO_CAP_CANCEL |
        SCXML_EVENT_IO_CAP_PAYLOAD | SCXML_EVENT_IO_CAP_CONTENT;
    return adapter != NULL && adapter->abi_version == SCXML_ADAPTER_ABI &&
           adapter->struct_size == sizeof(*adapter) &&
           (adapter->capabilities & ~known) == 0u &&
           (adapter->capabilities & SCXML_EVENT_IO_CAP_SEND) != 0u &&
           adapter->prepare_send != NULL && adapter->close != NULL &&
           adapter->is_quiescent != NULL &&
           ((adapter->capabilities & SCXML_EVENT_IO_CAP_CANCEL) == 0u ||
            ((adapter->capabilities & SCXML_EVENT_IO_CAP_DELAYED_SEND) != 0u &&
             adapter->prepare_cancel != NULL));
}

static bool datamodel_write_adapter_valid(
    const ccxml_datamodel_adapter_v1 *adapter) {
    const size_t write_size =
        offsetof(ccxml_datamodel_adapter_v1, prepare_assign_string) +
        sizeof(adapter->prepare_assign_string);
    return adapter != NULL &&
           adapter->abi_version == CCXML_DATAMODEL_ADAPTER_ABI_V1 &&
           adapter->struct_size >= write_size &&
           adapter->validate_string_location != NULL &&
           adapter->prepare_assign_string != NULL;
}

static bool datamodel_read_adapter_valid(
    const ccxml_datamodel_adapter_v1 *adapter) {
    const size_t read_size =
        offsetof(ccxml_datamodel_adapter_v1, read_string) +
        sizeof(adapter->read_string);
    return adapter != NULL &&
           adapter->abi_version == CCXML_DATAMODEL_ADAPTER_ABI_V1 &&
           adapter->struct_size >= read_size &&
           adapter->validate_readable_string_location != NULL &&
           adapter->read_string != NULL;
}

static bool datamodel_condition_adapter_valid(
    const ccxml_datamodel_adapter_v1 *adapter) {
    const size_t condition_size =
        offsetof(ccxml_datamodel_adapter_v1, destroy_condition) +
        sizeof(adapter->destroy_condition);
    return adapter != NULL &&
           adapter->abi_version == CCXML_DATAMODEL_ADAPTER_ABI_V1 &&
           adapter->struct_size >= condition_size &&
           adapter->compile_condition != NULL &&
           adapter->evaluate_condition != NULL &&
           adapter->destroy_condition != NULL;
}

static bool datamodel_payload_adapter_valid(
    const ccxml_datamodel_adapter_v1 *adapter) {
    const size_t payload_size =
        offsetof(ccxml_datamodel_adapter_v1, read_payload) +
        sizeof(adapter->read_payload);
    return adapter != NULL &&
           adapter->abi_version == CCXML_DATAMODEL_ADAPTER_ABI_V1 &&
           adapter->struct_size >= payload_size &&
           adapter->validate_payload_location != NULL &&
           adapter->read_payload != NULL;
}

static bool payload_content_valid(const scxml_content_view *content) {
    if (content == NULL) return false;
    if (content->kind == SCXML_CONTENT_SCALAR) {
        if (content->scalar.kind < SCXML_PAYLOAD_VALUE_BOOL ||
            content->scalar.kind > SCXML_PAYLOAD_VALUE_STRING)
            return false;
        return content->scalar.kind != SCXML_PAYLOAD_VALUE_STRING ||
               content->scalar.data.string.data != NULL ||
               content->scalar.data.string.size == 0u;
    }
    return content->kind == SCXML_CONTENT_CMETA &&
           content->schema != NULL && content->object != NULL &&
           cmeta_data_desc_valid(content->schema) &&
           content->schema->storage_type != NULL &&
           content->schema->storage_type->align != 0u &&
           (uintptr_t)content->object %
                   content->schema->storage_type->align ==
               0u;
}

static ccxml_status datamodel_payload_failure_status(
    scxml_adapter_status adapter_status) {
    if (adapter_status == SCXML_ADAPTER_ACCEPTED ||
        adapter_status == SCXML_ADAPTER_INVALID_CONTRACT) {
        return CCXML_INVALID_CONTRACT;
    }
    if (adapter_status == SCXML_ADAPTER_FULL) {
        return CCXML_ALLOCATION_FAILED;
    }
    return CCXML_ADAPTER_ERROR;
}

static ccxml_status read_delay_milliseconds(
    const scxml_content_view *content, uint64_t *out_delay_ms) {
    double whole;
    if (content == NULL || out_delay_ms == NULL ||
        content->kind != SCXML_CONTENT_SCALAR) {
        return CCXML_INVALID_CONTRACT;
    }
    switch (content->scalar.kind) {
        case SCXML_PAYLOAD_VALUE_SINT:
            if (content->scalar.data.sint < 0) return CCXML_INVALID_CONTRACT;
            *out_delay_ms = (uint64_t)content->scalar.data.sint;
            return CCXML_OK;
        case SCXML_PAYLOAD_VALUE_UINT:
            *out_delay_ms = content->scalar.data.uint;
            return CCXML_OK;
        case SCXML_PAYLOAD_VALUE_FLOAT:
            if (!isfinite(content->scalar.data.number) ||
                content->scalar.data.number < 0.0 ||
                content->scalar.data.number > (double)UINT64_MAX ||
                modf(content->scalar.data.number, &whole) != 0.0) {
                return CCXML_INVALID_CONTRACT;
            }
            *out_delay_ms = (uint64_t)whole;
            return CCXML_OK;
        default:
            return CCXML_INVALID_CONTRACT;
    }
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

static void destroy_conditions(ccxml_session_impl *impl) {
    size_t index;
    if (impl == NULL || impl->transition_bindings == NULL ||
        impl->datamodel.destroy_condition == NULL)
        return;
    for (index = 0u; index < impl->program->transition_count; ++index) {
        ccxml_condition *condition =
            &impl->transition_bindings[index].condition;
        if (condition->impl != NULL) {
            impl->datamodel.destroy_condition(
                impl->datamodel_user, condition);
            condition->impl = NULL;
        }
    }
}

static void close_adapter(ccxml_session_impl *impl) {
    if (impl == NULL || impl->closed) return;
    impl->closed = true;
    impl->telephony.close(impl->telephony_user);
    if (impl->event_io.close != NULL)
        impl->event_io.close(impl->event_io_user);
}

static void discard_tickets(
    cflow_statechart_effect_ticket *tickets, size_t count) {
    while (count != 0u) {
        cflow_statechart_effect_ticket *ticket = &tickets[--count];
        ticket->discard(ticket->user);
        *ticket = (cflow_statechart_effect_ticket){0};
    }
}

static unsigned char ascii_fold(unsigned char value) {
    return value >= (unsigned char)'A' && value <= (unsigned char)'Z'
        ? (unsigned char)(value + ((unsigned char)'a' - (unsigned char)'A'))
        : value;
}

static bool event_pattern_matches(
    const char *pattern, size_t pattern_size,
    const char *name, size_t name_size) {
    size_t pattern_index = 0u;
    size_t name_index = 0u;
    size_t wildcard_index = SIZE_MAX;
    size_t wildcard_name_index = 0u;
    if (pattern == NULL || name == NULL) return false;
    while (name_index < name_size) {
        if (pattern_index < pattern_size &&
            pattern[pattern_index] != '*' &&
            ascii_fold((unsigned char)pattern[pattern_index]) ==
                ascii_fold((unsigned char)name[name_index])) {
            ++pattern_index;
            ++name_index;
        } else if (pattern_index < pattern_size &&
                   pattern[pattern_index] == '*') {
            wildcard_index = pattern_index++;
            wildcard_name_index = name_index;
        } else if (wildcard_index != SIZE_MAX) {
            pattern_index = wildcard_index + 1u;
            name_index = ++wildcard_name_index;
        } else {
            return false;
        }
    }
    while (pattern_index < pattern_size && pattern[pattern_index] == '*')
        ++pattern_index;
    return pattern_index == pattern_size;
}

static bool unhandled_event_is_fatal(const ccxml_event *event) {
    static const char error_pattern[] = "error.*";
    static const char kill_name[] = "ccxml.kill";
    static const char kill_pattern[] = "ccxml.kill.*";
    return event_pattern_matches(
               error_pattern, sizeof(error_pattern) - 1u,
               event->name, event->name_size) ||
           event_pattern_matches(
               kill_name, sizeof(kill_name) - 1u,
               event->name, event->name_size) ||
           event_pattern_matches(
               kill_pattern, sizeof(kill_pattern) - 1u,
               event->name, event->name_size);
}

static bool state_list_matches(
    const char *states, size_t states_size,
    const char *value, size_t value_size) {
    size_t cursor = 0u;
    while (cursor < states_size) {
        size_t start;
        while (cursor < states_size &&
               (states[cursor] == ' ' || states[cursor] == '\t' ||
                states[cursor] == '\r' || states[cursor] == '\n'))
            ++cursor;
        start = cursor;
        while (cursor < states_size &&
               states[cursor] != ' ' && states[cursor] != '\t' &&
               states[cursor] != '\r' && states[cursor] != '\n')
            ++cursor;
        if (cursor - start == value_size &&
            memcmp(states + start, value, value_size) == 0)
            return true;
    }
    return false;
}

static bool transition_guard(
    void *user, const void *state, const cflow_event_view *event,
    bool *out_enabled, const char **out_error) {
    const ccxml_transition_binding *binding =
        (const ccxml_transition_binding *)user;
    ccxml_session_impl *impl;
    const ccxml_event *ccxml_event_view;
    ccxml_string_view state_value = {0};
    const char *error = NULL;
    scxml_adapter_status adapter_status;
    (void)state;
    if (out_error != NULL) *out_error = NULL;
    if (out_enabled == NULL || binding == NULL || binding->session == NULL ||
        event == NULL ||
        event->id != 1u || event->payload_type != &ccxml_event_cmeta_type ||
        event->payload == NULL) {
        if (out_error != NULL)
            *out_error = "invalid CCXML eventprocessor guard contract";
        return false;
    }
    impl = binding->session;
    ccxml_event_view = (const ccxml_event *)event->payload;
    if (impl->dispatch_status != CCXML_OK) {
        *out_enabled = false;
        return true;
    }
    *out_enabled = binding->transition != NULL &&
        event_pattern_matches(
            binding->transition->event, binding->transition->event_size,
            ccxml_event_view->name, ccxml_event_view->name_size);
    if (!*out_enabled) return true;
    if (binding->transition->state != NULL) {
        adapter_status = impl->datamodel.read_string(
            impl->datamodel_user, impl->program->statevariable,
            impl->program->statevariable_size, &state_value, &error);
        (void)error;
        if (adapter_status != SCXML_ADAPTER_ACCEPTED) {
            impl->dispatch_status = adapter_status ==
                    SCXML_ADAPTER_INVALID_CONTRACT
                ? CCXML_INVALID_CONTRACT : CCXML_ADAPTER_ERROR;
            *out_enabled = false;
            return true;
        }
        if (state_value.data == NULL || state_value.size == 0u ||
            memchr(state_value.data, '\0', state_value.size) != NULL) {
            impl->dispatch_status = CCXML_INVALID_CONTRACT;
            *out_enabled = false;
            return true;
        }
        *out_enabled = state_list_matches(
            binding->transition->state, binding->transition->state_size,
            state_value.data, state_value.size);
        if (!*out_enabled) return true;
    }
    if (binding->transition->condition != NULL) {
        bool condition_value = false;
        error = NULL;
        adapter_status = impl->datamodel.evaluate_condition(
            impl->datamodel_user, &binding->condition,
            ccxml_event_view, &condition_value, &error);
        (void)error;
        if (adapter_status != SCXML_ADAPTER_ACCEPTED) {
            impl->dispatch_status = adapter_status ==
                    SCXML_ADAPTER_INVALID_CONTRACT
                ? CCXML_INVALID_CONTRACT : CCXML_ADAPTER_ERROR;
            *out_enabled = false;
            return true;
        }
        *out_enabled = condition_value;
    }
    return true;
}

static bool execute_transition_binding(
    void *user, const cflow_statechart_executable_context *context,
    const char **out_error);

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
    scxml_event_io_adapter event_io = {0};
    void *event_io_user = NULL;
    ccxml_session_impl *impl;
    cflow_statechart_instance_config native_config = {0};
    cflow_statechart_instance_status native_status;
    const bool initial_state = false;
    size_t guard_count;
    size_t executable_count;
    size_t action_index;
    size_t transition_index;
    if (session == NULL || session->impl != NULL || config == NULL ||
        config->program == NULL || config->program->impl == NULL ||
        !adapter_valid(config->telephony)) {
        return CCXML_INVALID_ARGUMENT;
    }
    program = (const ccxml_program_impl *)config->program->impl;
    telephony = copy_adapter(config->telephony);
    if (program->uses_send || program->uses_cancel) {
        if (!event_io_send_adapter_valid(config->event_io))
            return CCXML_INVALID_ARGUMENT;
        if (program->uses_delayed_send &&
            (config->event_io->capabilities &
             SCXML_EVENT_IO_CAP_DELAYED_SEND) == 0u)
            return CCXML_INVALID_ARGUMENT;
        if (program->uses_cancel &&
            (config->event_io->capabilities &
             SCXML_EVENT_IO_CAP_CANCEL) == 0u)
            return CCXML_INVALID_ARGUMENT;
        if (program->uses_send_payload &&
            (config->event_io->capabilities &
             SCXML_EVENT_IO_CAP_PAYLOAD) == 0u)
            return CCXML_INVALID_ARGUMENT;
        event_io = *config->event_io;
        event_io_user = config->event_io_user;
    }
    if (program->initial_variable != NULL || program->uses_assign ||
        program->uses_send_id) {
        if (!datamodel_write_adapter_valid(config->datamodel))
            return CCXML_INVALID_ARGUMENT;
        datamodel = copy_datamodel_adapter(config->datamodel);
        if (program->initial_variable != NULL) {
            const char *error = NULL;
            if (datamodel.validate_string_location(
                    config->datamodel_user, program->initial_variable,
                    program->initial_variable_size, &error) !=
                SCXML_ADAPTER_ACCEPTED) {
                (void)error;
                return CCXML_INVALID_ARGUMENT;
            }
        }
        for (action_index = 0u; action_index < program->action_count;
             ++action_index) {
            const ccxml_action_row *action = &program->actions[action_index];
            const char *error = NULL;
            if ((action->kind == CCXML_ACTION_ASSIGN_STRING ||
                 (action->kind == CCXML_ACTION_SEND &&
                  action->location != NULL)) &&
                datamodel.validate_string_location(
                    config->datamodel_user, action->location,
                    action->location_size, &error) !=
                    SCXML_ADAPTER_ACCEPTED) {
                (void)error;
                return CCXML_INVALID_ARGUMENT;
            }
        }
    }
    if (program->uses_statevariable) {
        const char *error = NULL;
        if (!datamodel_read_adapter_valid(config->datamodel))
            return CCXML_INVALID_ARGUMENT;
        datamodel = copy_datamodel_adapter(config->datamodel);
        if (datamodel.validate_readable_string_location(
                config->datamodel_user, program->statevariable,
                program->statevariable_size, &error) !=
            SCXML_ADAPTER_ACCEPTED) {
            (void)error;
            return CCXML_INVALID_ARGUMENT;
        }
    }
    if (program->uses_condition) {
        if (!datamodel_condition_adapter_valid(config->datamodel))
            return CCXML_INVALID_ARGUMENT;
        datamodel = copy_datamodel_adapter(config->datamodel);
    }
    if (program->uses_send_payload || program->uses_send_delay) {
        size_t payload_index;
        if (!datamodel_payload_adapter_valid(config->datamodel))
            return CCXML_INVALID_ARGUMENT;
        datamodel = copy_datamodel_adapter(config->datamodel);
        for (payload_index = 0u; payload_index < program->payload_count;
             ++payload_index) {
            const ccxml_payload_row *payload =
                &program->payloads[payload_index];
            const char *error = NULL;
            if (datamodel.validate_payload_location(
                    config->datamodel_user, payload->name,
                    payload->name_size, &error) !=
                SCXML_ADAPTER_ACCEPTED) {
                (void)error;
                return CCXML_INVALID_ARGUMENT;
            }
        }
        for (action_index = 0u; action_index < program->action_count;
             ++action_index) {
            const ccxml_action_row *action = &program->actions[action_index];
            const char *error = NULL;
            if (action->kind == CCXML_ACTION_SEND && action->delay_is_dynamic &&
                datamodel.validate_payload_location(
                    config->datamodel_user, action->delay, action->delay_size,
                    &error) != SCXML_ADAPTER_ACCEPTED) {
                (void)error;
                return CCXML_INVALID_ARGUMENT;
            }
        }
    }
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
            !datamodel_write_adapter_valid(config->datamodel)) {
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
    if (program->uses_destroy_conference) {
        const size_t destroy_conference_size =
            offsetof(
                ccxml_telephony_adapter_v1,
                prepare_destroy_conference) +
            sizeof(telephony.prepare_destroy_conference);
        if (config->telephony->struct_size < destroy_conference_size ||
            telephony.prepare_destroy_conference == NULL) {
            return CCXML_INVALID_ARGUMENT;
        }
    }
    if (program->uses_dialog_prepare) {
        const size_t dialog_prepare_size =
            offsetof(
                ccxml_telephony_adapter_v1,
                prepare_dialog_prepare) +
            sizeof(telephony.prepare_dialog_prepare);
        if (config->telephony->struct_size < dialog_prepare_size ||
            telephony.prepare_dialog_prepare == NULL ||
            !datamodel_write_adapter_valid(config->datamodel)) {
            return CCXML_INVALID_ARGUMENT;
        }
        datamodel = copy_datamodel_adapter(config->datamodel);
        for (action_index = 0u; action_index < program->action_count;
             ++action_index) {
            const ccxml_action_row *action = &program->actions[action_index];
            const char *error = NULL;
            if (action->kind == CCXML_ACTION_DIALOG_PREPARE &&
                datamodel.validate_string_location(
                    config->datamodel_user, action->location,
                    action->location_size, &error) !=
                    SCXML_ADAPTER_ACCEPTED) {
                (void)error;
                return CCXML_INVALID_ARGUMENT;
            }
        }
    }
    if (program->uses_dialog_start) {
        const size_t dialog_start_size =
            offsetof(
                ccxml_telephony_adapter_v1,
                prepare_dialog_start) +
            sizeof(telephony.prepare_dialog_start);
        if (config->telephony->struct_size < dialog_start_size ||
            telephony.prepare_dialog_start == NULL ||
            !datamodel_write_adapter_valid(config->datamodel)) {
            return CCXML_INVALID_ARGUMENT;
        }
        datamodel = copy_datamodel_adapter(config->datamodel);
        for (action_index = 0u; action_index < program->action_count;
             ++action_index) {
            const ccxml_action_row *action = &program->actions[action_index];
            const char *error = NULL;
            if (action->kind == CCXML_ACTION_DIALOG_START &&
                datamodel.validate_string_location(
                    config->datamodel_user, action->location,
                    action->location_size, &error) !=
                    SCXML_ADAPTER_ACCEPTED) {
                (void)error;
                return CCXML_INVALID_ARGUMENT;
            }
        }
    }
    if (program->uses_prepared_dialog_start) {
        const size_t prepared_dialog_start_size =
            offsetof(
                ccxml_telephony_adapter_v1,
                prepare_prepared_dialog_start) +
            sizeof(telephony.prepare_prepared_dialog_start);
        if (config->telephony->struct_size < prepared_dialog_start_size ||
            telephony.prepare_prepared_dialog_start == NULL) {
            return CCXML_INVALID_ARGUMENT;
        }
    }
    if (program->uses_dialog_terminate) {
        const size_t dialog_terminate_size =
            offsetof(
                ccxml_telephony_adapter_v1,
                prepare_dialog_terminate) +
            sizeof(telephony.prepare_dialog_terminate);
        if (config->telephony->struct_size < dialog_terminate_size ||
            telephony.prepare_dialog_terminate == NULL) {
            return CCXML_INVALID_ARGUMENT;
        }
    }
    if (program->uses_datamodel_read) {
        if (!datamodel_read_adapter_valid(config->datamodel))
            return CCXML_INVALID_ARGUMENT;
        datamodel = copy_datamodel_adapter(config->datamodel);
        for (action_index = 0u; action_index < program->action_count;
             ++action_index) {
            const ccxml_action_row *action = &program->actions[action_index];
            const char *error = NULL;
            if ((action->kind == CCXML_ACTION_DESTROY_CONFERENCE ||
                 action->kind == CCXML_ACTION_DIALOG_TERMINATE ||
                 action->kind == CCXML_ACTION_PREPARED_DIALOG_START ||
                 action->kind == CCXML_ACTION_CANCEL) &&
                action->location != NULL &&
                datamodel.validate_readable_string_location(
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
    if (program->max_send_payload_entries >
        SIZE_MAX / sizeof(scxml_payload_entry))
        return CCXML_LIMIT_EXCEEDED;
    if (program->uses_send_id &&
        SCXML_EVENT_METADATA_CAPACITY < CCXML_SEND_ID_MAX_SIZE)
        return CCXML_LIMIT_EXCEEDED;
    impl = (ccxml_session_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return CCXML_ALLOCATION_FAILED;
    if (program->uses_send_id) {
        salts_uuid_t uuid;
        if (salts_uuid_v4_generate(&uuid) != SALTS_OK ||
            salts_uuid_format(
                &uuid, impl->send_namespace,
                sizeof(impl->send_namespace)) != SALTS_OK) {
            free(impl);
            return CCXML_INVALID_CONTRACT;
        }
        impl->next_send_token = UINT64_C(1);
    }
    if (program->max_transition_effects != 0u) {
        impl->tickets = (cflow_statechart_effect_ticket *)calloc(
            program->max_transition_effects, sizeof(*impl->tickets));
        if (impl->tickets == NULL) {
            free(impl);
            return CCXML_ALLOCATION_FAILED;
        }
    }
    if (program->max_send_payload_entries != 0u) {
        impl->payload_scratch = (scxml_payload_entry *)calloc(
            program->max_send_payload_entries,
            sizeof(*impl->payload_scratch));
        if (impl->payload_scratch == NULL) {
            free(impl->tickets);
            free(impl);
            return CCXML_ALLOCATION_FAILED;
        }
    }
    impl->program = program;
    impl->telephony = telephony;
    impl->telephony_user = config->telephony_user;
    impl->datamodel = datamodel;
    impl->datamodel_user = config->datamodel_user;
    impl->event_io = event_io;
    impl->event_io_user = event_io_user;
    impl->ticket_capacity = program->max_transition_effects;
    impl->payload_scratch_capacity = program->max_send_payload_entries;
    guard_count = cflow_statechart_guard_count(&program->statechart);
    executable_count =
        cflow_statechart_executable_count(&program->statechart);
    if (guard_count > SIZE_MAX / sizeof(*impl->guard_bindings) ||
        guard_count > SIZE_MAX / sizeof(*impl->transition_bindings) ||
        executable_count > SIZE_MAX / sizeof(*impl->executable_bindings)) {
        close_adapter(impl);
        free(impl->payload_scratch);
        free(impl->tickets);
        free(impl);
        return CCXML_LIMIT_EXCEEDED;
    }
    impl->guard_bindings = (cflow_statechart_guard_binding *)calloc(
        guard_count, sizeof(*impl->guard_bindings));
    impl->transition_bindings = (ccxml_transition_binding *)calloc(
        guard_count, sizeof(*impl->transition_bindings));
    if (executable_count != 0u) {
        impl->executable_bindings =
            (cflow_statechart_executable_binding *)calloc(
                executable_count, sizeof(*impl->executable_bindings));
    }
    if ((guard_count != 0u &&
         (impl->guard_bindings == NULL ||
          impl->transition_bindings == NULL)) ||
        (executable_count != 0u && impl->executable_bindings == NULL)) {
        close_adapter(impl);
        free(impl->payload_scratch);
        free(impl->transition_bindings);
        free(impl->executable_bindings);
        free(impl->guard_bindings);
        free(impl->tickets);
        free(impl);
        return CCXML_ALLOCATION_FAILED;
    }
    for (transition_index = 0u; transition_index < guard_count;
         ++transition_index) {
        ccxml_transition_binding *binding =
            &impl->transition_bindings[transition_index];
        binding->session = impl;
        binding->transition = transition_index < program->transition_count
            ? &program->transitions[transition_index] : NULL;
        if (binding->transition != NULL &&
            binding->transition->condition != NULL) {
            const char *error = NULL;
            const scxml_adapter_status adapter_status =
                impl->datamodel.compile_condition(
                    impl->datamodel_user,
                    binding->transition->condition,
                    binding->transition->condition_size,
                    &binding->condition, &error);
            ccxml_status status = CCXML_OK;
            (void)error;
            if (adapter_status != SCXML_ADAPTER_ACCEPTED) {
                status = adapter_status == SCXML_ADAPTER_FULL
                    ? CCXML_ALLOCATION_FAILED
                    : adapter_status == SCXML_ADAPTER_INVALID_CONTRACT
                        ? CCXML_INVALID_CONTRACT : CCXML_ADAPTER_ERROR;
            } else if (binding->condition.impl == NULL) {
                status = CCXML_INVALID_CONTRACT;
            }
            if (status != CCXML_OK) {
                close_adapter(impl);
                destroy_conditions(impl);
                free(impl->payload_scratch);
                free(impl->transition_bindings);
                free(impl->executable_bindings);
                free(impl->guard_bindings);
                free(impl->tickets);
                free(impl);
                return status;
            }
        }
        impl->guard_bindings[transition_index] =
            (cflow_statechart_guard_binding){
                .id = (cflow_statechart_guard_id)(transition_index + 1u),
                .fn = transition_guard,
                .user = binding};
        if (transition_index < executable_count) {
            impl->executable_bindings[transition_index] =
                (cflow_statechart_executable_binding){
                    .id = (cflow_statechart_executable_id)(
                        transition_index + 1u),
                    .user = binding,
                    .contextual_fn = execute_transition_binding};
        }
    }
    if (!cflow_executor_serial_init(&impl->executor)) {
        close_adapter(impl);
        destroy_conditions(impl);
        free(impl->payload_scratch);
        free(impl->transition_bindings);
        free(impl->executable_bindings);
        free(impl->guard_bindings);
        free(impl->tickets);
        free(impl);
        return CCXML_ALLOCATION_FAILED;
    }
    native_config = (cflow_statechart_instance_config){
        .statechart = &program->statechart,
        .initial_state = &initial_state,
        .guards = impl->guard_bindings,
        .guard_count = guard_count,
        .executables = impl->executable_bindings,
        .executable_count = executable_count,
        .external_event_capacity = 1u,
        .internal_event_capacity = 1u,
        .completion_capacity = 1u,
        .microstep_limit = 1u,
        .executor = &impl->executor,
        .effect_capacity = program->max_transition_effects != 0u ? 1u : 0u};
    native_status =
        cflow_statechart_instance_init(&impl->instance, &native_config);
    if (native_status != CFLOW_STATECHART_INSTANCE_OK) {
        cflow_executor_destroy(&impl->executor);
        close_adapter(impl);
        destroy_conditions(impl);
        free(impl->payload_scratch);
        free(impl->transition_bindings);
        free(impl->executable_bindings);
        free(impl->guard_bindings);
        free(impl->tickets);
        free(impl);
        return native_status == CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED
            ? CCXML_ALLOCATION_FAILED : CCXML_INVALID_CONTRACT;
    }
    if (program->initial_variable != NULL) {
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;
        const scxml_adapter_status adapter_status =
            impl->datamodel.prepare_assign_string(
                impl->datamodel_user, program->initial_variable,
                program->initial_variable_size, program->initial_value,
                program->initial_value_size, &ticket, &error);
        ccxml_status initialization_status = CCXML_OK;
        (void)error;
        if (adapter_status != SCXML_ADAPTER_ACCEPTED) {
            initialization_status = adapter_status == SCXML_ADAPTER_FULL
                ? CCXML_ALLOCATION_FAILED
                : adapter_status == SCXML_ADAPTER_INVALID_CONTRACT
                    ? CCXML_INVALID_CONTRACT : CCXML_ADAPTER_ERROR;
        } else if (ticket.commit == NULL || ticket.discard == NULL) {
            if (ticket.discard != NULL) ticket.discard(ticket.user);
            initialization_status = CCXML_INVALID_CONTRACT;
        } else {
            ticket.commit(ticket.user);
        }
        if (initialization_status != CCXML_OK) {
            cflow_statechart_instance_close(&impl->instance);
            (void)cflow_statechart_instance_destroy(&impl->instance);
            cflow_executor_destroy(&impl->executor);
            close_adapter(impl);
            destroy_conditions(impl);
            free(impl->payload_scratch);
            free(impl->transition_bindings);
            free(impl->executable_bindings);
            free(impl->guard_bindings);
            free(impl->tickets);
            free(impl);
            return initialization_status;
        }
    }
    session->impl = impl;
    return CCXML_OK;
}

static ccxml_status execute_transition_actions(
    ccxml_session_impl *impl, const ccxml_transition_row *transition,
    const ccxml_event *event, size_t *out_prepared,
    bool *out_exit_requested) {
    size_t index;
    size_t prepared = 0u;
    bool exit_requested = false;
    for (index = 0u; index < transition->action_count; ++index) {
        const ccxml_action_row *action =
            &impl->program->actions[transition->first_action + index];
        if (action->kind == CCXML_ACTION_EXIT) {
            exit_requested = true;
            break;
        }
        if (action->kind == CCXML_ACTION_ASSIGN_STRING) {
            cflow_statechart_effect_ticket ticket = {0};
            const char *error = NULL;
            const scxml_adapter_status adapter_status =
                impl->datamodel.prepare_assign_string(
                    impl->datamodel_user, action->location,
                    action->location_size, action->destination,
                    action->destination_size, &ticket, &error);
            const ccxml_status status = retain_ticket(
                impl, adapter_status, ticket, &prepared);
            (void)error;
            if (status != CCXML_OK) return status;
        }
        if (action->kind == CCXML_ACTION_SEND) {
            cflow_statechart_effect_ticket send_ticket = {0};
            cflow_statechart_effect_ticket datamodel_ticket = {0};
            const char *error = NULL;
            char id_storage[SCXML_EVENT_METADATA_CAPACITY + 1u] = {0};
            scxml_send_request request = {
                .event = action->name,
                .event_size = action->name_size,
                .target = action->destination,
                .target_size = action->destination_size,
                .type = action->target_type,
                .type_size = action->target_type_size,
                .delay_ms = action->delay_ms,
                .payload = {.kind = SCXML_PAYLOAD_NONE}};
            scxml_adapter_status adapter_status;
            ccxml_status status;
            if (action->delay_is_dynamic) {
                scxml_content_view delay_value = {0};
                adapter_status = impl->datamodel.read_payload(
                    impl->datamodel_user, action->delay, action->delay_size,
                    &delay_value, &error);
                if (adapter_status != SCXML_ADAPTER_ACCEPTED) {
                    discard_tickets(impl->tickets, prepared);
                    return datamodel_payload_failure_status(adapter_status);
                }
                status = read_delay_milliseconds(&delay_value, &request.delay_ms);
                if (status != CCXML_OK) {
                    discard_tickets(impl->tickets, prepared);
                    return status;
                }
            }
            if (action->payload_count != 0u) {
                size_t payload_index;
                if (action->payload_first > impl->program->payload_count ||
                    action->payload_count >
                        impl->program->payload_count - action->payload_first ||
                    action->payload_count > impl->payload_scratch_capacity ||
                    impl->payload_scratch == NULL) {
                    discard_tickets(impl->tickets, prepared);
                    return CCXML_INVALID_CONTRACT;
                }
                for (payload_index = 0u;
                     payload_index < action->payload_count;
                     ++payload_index) {
                    const ccxml_payload_row *payload =
                        &impl->program->payloads[
                            action->payload_first + payload_index];
                    scxml_payload_entry *entry =
                        &impl->payload_scratch[payload_index];
                    entry->name = payload->name;
                    entry->name_size = payload->name_size;
                    entry->value = (scxml_content_view){0};
                    adapter_status = impl->datamodel.read_payload(
                        impl->datamodel_user, payload->name,
                        payload->name_size, &entry->value, &error);
                    if (adapter_status != SCXML_ADAPTER_ACCEPTED ||
                        !payload_content_valid(&entry->value)) {
                        discard_tickets(impl->tickets, prepared);
                        if (adapter_status != SCXML_ADAPTER_ACCEPTED)
                            return datamodel_payload_failure_status(
                                adapter_status);
                        return CCXML_INVALID_CONTRACT;
                    }
                }
                request.payload = (scxml_payload_view){
                    .kind = SCXML_PAYLOAD_NAMED,
                    .entries = impl->payload_scratch,
                    .entry_count = action->payload_count};
            }
            if (action->location != NULL) {
                const uint64_t token = impl->next_send_token;
                const int written = token == 0u ? -1 : snprintf(
                    id_storage, sizeof(id_storage), "send.%s.%" PRIu64,
                    impl->send_namespace, token);
                if (written < 0 ||
                    (size_t)written > SCXML_EVENT_METADATA_CAPACITY) {
                    discard_tickets(impl->tickets, prepared);
                    return token == 0u
                        ? CCXML_LIMIT_EXCEEDED : CCXML_INVALID_CONTRACT;
                }
                impl->next_send_token = token == UINT64_MAX
                    ? UINT64_C(0) : token + UINT64_C(1);
                request.id = id_storage;
                request.id_size = (size_t)written;
            }
            adapter_status =
                impl->event_io.prepare_send(
                    impl->event_io_user, &request, &send_ticket, &error);
            status = retain_ticket(
                impl, adapter_status, send_ticket, &prepared);
            (void)error;
            if (status != CCXML_OK) return status;
            if (action->location != NULL) {
                error = NULL;
                adapter_status = impl->datamodel.prepare_assign_string(
                    impl->datamodel_user, action->location,
                    action->location_size, request.id, request.id_size,
                    &datamodel_ticket, &error);
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
        if (action->kind == CCXML_ACTION_CANCEL) {
            cflow_statechart_effect_ticket ticket = {0};
            ccxml_string_view send_id = {
                .data = action->id1, .size = action->id1_size};
            const char *error = NULL;
            scxml_adapter_status adapter_status;
            ccxml_status status;
            if (action->location != NULL) {
                adapter_status = impl->datamodel.read_string(
                    impl->datamodel_user, action->location,
                    action->location_size, &send_id, &error);
                (void)error;
                if (adapter_status != SCXML_ADAPTER_ACCEPTED) {
                    discard_tickets(impl->tickets, prepared);
                    return adapter_status == SCXML_ADAPTER_INVALID_CONTRACT
                        ? CCXML_INVALID_CONTRACT : CCXML_ADAPTER_ERROR;
                }
            }
            if (send_id.data == NULL || send_id.size == 0u ||
                send_id.size > SCXML_EVENT_METADATA_CAPACITY ||
                memchr(send_id.data, '\0', send_id.size) != NULL) {
                discard_tickets(impl->tickets, prepared);
                return CCXML_INVALID_CONTRACT;
            }
            {
                const scxml_cancel_request request = {
                    .send_id = send_id.data,
                    .send_id_size = send_id.size};
                error = NULL;
                adapter_status = impl->event_io.prepare_cancel(
                    impl->event_io_user, &request, &ticket, &error);
            }
            status = retain_ticket(
                impl, adapter_status, ticket, &prepared);
            (void)error;
            if (status != CCXML_OK) return status;
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
        if (action->kind == CCXML_ACTION_DESTROY_CONFERENCE) {
            cflow_statechart_effect_ticket ticket = {0};
            ccxml_string_view conference_id = {
                .data = action->id1,
                .size = action->id1_size};
            const char *error = NULL;
            scxml_adapter_status adapter_status;
            ccxml_status status;
            if (action->location != NULL) {
                adapter_status = impl->datamodel.read_string(
                    impl->datamodel_user, action->location,
                    action->location_size, &conference_id, &error);
                (void)error;
                if (adapter_status != SCXML_ADAPTER_ACCEPTED) {
                    discard_tickets(impl->tickets, prepared);
                    return CCXML_ADAPTER_ERROR;
                }
            }
            if (conference_id.data == NULL || conference_id.size == 0u ||
                memchr(
                    conference_id.data, '\0', conference_id.size) != NULL) {
                discard_tickets(impl->tickets, prepared);
                return CCXML_INVALID_CONTRACT;
            }
            {
                const ccxml_destroy_conference_request request = {
                    .conference_id = conference_id.data,
                    .conference_id_size = conference_id.size};
                adapter_status =
                    impl->telephony.prepare_destroy_conference(
                        impl->telephony_user, &request, &ticket, &error);
            }
            status = retain_ticket(
                impl, adapter_status, ticket, &prepared);
            (void)error;
            if (status != CCXML_OK) return status;
        }
        if (action->kind == CCXML_ACTION_DIALOG_PREPARE) {
            static const char voice_xml_media_type[] =
                "application/voicexml+xml";
            cflow_statechart_effect_ticket provider_ticket = {0};
            cflow_statechart_effect_ticket datamodel_ticket = {0};
            ccxml_string_view dialog_id = {0};
            const char *error = NULL;
            scxml_adapter_status adapter_status;
            ccxml_status status;
            const ccxml_dialog_prepare_request request = {
                .source = action->destination,
                .source_size = action->destination_size,
                .media_type = voice_xml_media_type,
                .media_type_size = sizeof(voice_xml_media_type) - 1u};
            adapter_status = impl->telephony.prepare_dialog_prepare(
                impl->telephony_user, &request, &dialog_id,
                &provider_ticket, &error);
            status = retain_ticket(
                impl, adapter_status, provider_ticket, &prepared);
            (void)error;
            if (status != CCXML_OK) return status;
            if (dialog_id.data == NULL || dialog_id.size == 0u ||
                memchr(dialog_id.data, '\0', dialog_id.size) != NULL) {
                discard_tickets(impl->tickets, prepared);
                return CCXML_INVALID_CONTRACT;
            }
            error = NULL;
            adapter_status = impl->datamodel.prepare_assign_string(
                impl->datamodel_user, action->location,
                action->location_size, dialog_id.data,
                dialog_id.size, &datamodel_ticket, &error);
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
        if (action->kind == CCXML_ACTION_DIALOG_START) {
            static const char voice_xml_media_type[] =
                "application/voicexml+xml";
            cflow_statechart_effect_ticket provider_ticket = {0};
            cflow_statechart_effect_ticket datamodel_ticket = {0};
            ccxml_string_view dialog_id = {0};
            const char *error = NULL;
            scxml_adapter_status adapter_status;
            ccxml_status status;
            const ccxml_dialog_start_request request = {
                .source = action->destination,
                .source_size = action->destination_size,
                .media_type = voice_xml_media_type,
                .media_type_size = sizeof(voice_xml_media_type) - 1u,
                .connection_id = event->connection_id,
                .connection_id_size = event->connection_id_size};
            if (!connection_id_valid(event)) {
                discard_tickets(impl->tickets, prepared);
                return CCXML_INVALID_EVENT;
            }
            adapter_status = impl->telephony.prepare_dialog_start(
                impl->telephony_user, &request, &dialog_id,
                &provider_ticket, &error);
            status = retain_ticket(
                impl, adapter_status, provider_ticket, &prepared);
            (void)error;
            if (status != CCXML_OK) return status;
            if (dialog_id.data == NULL || dialog_id.size == 0u ||
                memchr(dialog_id.data, '\0', dialog_id.size) != NULL) {
                discard_tickets(impl->tickets, prepared);
                return CCXML_INVALID_CONTRACT;
            }
            error = NULL;
            adapter_status = impl->datamodel.prepare_assign_string(
                impl->datamodel_user, action->location,
                action->location_size, dialog_id.data,
                dialog_id.size, &datamodel_ticket, &error);
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
        if (action->kind == CCXML_ACTION_PREPARED_DIALOG_START) {
            cflow_statechart_effect_ticket ticket = {0};
            ccxml_string_view dialog_id = {0};
            const char *error = NULL;
            scxml_adapter_status adapter_status;
            ccxml_status status;
            if (!connection_id_valid(event)) {
                discard_tickets(impl->tickets, prepared);
                return CCXML_INVALID_EVENT;
            }
            adapter_status = impl->datamodel.read_string(
                impl->datamodel_user, action->location,
                action->location_size, &dialog_id, &error);
            (void)error;
            if (adapter_status != SCXML_ADAPTER_ACCEPTED) {
                discard_tickets(impl->tickets, prepared);
                return CCXML_ADAPTER_ERROR;
            }
            if (dialog_id.data == NULL || dialog_id.size == 0u ||
                memchr(dialog_id.data, '\0', dialog_id.size) != NULL) {
                discard_tickets(impl->tickets, prepared);
                return CCXML_INVALID_CONTRACT;
            }
            {
                const ccxml_prepared_dialog_start_request request = {
                    .dialog_id = dialog_id.data,
                    .dialog_id_size = dialog_id.size,
                    .connection_id = event->connection_id,
                    .connection_id_size = event->connection_id_size};
                adapter_status =
                    impl->telephony.prepare_prepared_dialog_start(
                        impl->telephony_user, &request, &ticket, &error);
            }
            status = retain_ticket(
                impl, adapter_status, ticket, &prepared);
            (void)error;
            if (status != CCXML_OK) return status;
        }
        if (action->kind == CCXML_ACTION_DIALOG_TERMINATE) {
            cflow_statechart_effect_ticket ticket = {0};
            ccxml_string_view dialog_id = {
                .data = action->id1,
                .size = action->id1_size};
            const char *error = NULL;
            scxml_adapter_status adapter_status;
            ccxml_status status;
            if (action->location != NULL) {
                adapter_status = impl->datamodel.read_string(
                    impl->datamodel_user, action->location,
                    action->location_size, &dialog_id, &error);
                (void)error;
                if (adapter_status != SCXML_ADAPTER_ACCEPTED) {
                    discard_tickets(impl->tickets, prepared);
                    return CCXML_ADAPTER_ERROR;
                }
            }
            if (dialog_id.data == NULL || dialog_id.size == 0u ||
                memchr(dialog_id.data, '\0', dialog_id.size) != NULL) {
                discard_tickets(impl->tickets, prepared);
                return CCXML_INVALID_CONTRACT;
            }
            {
                const ccxml_dialog_terminate_request request = {
                    .dialog_id = dialog_id.data,
                    .dialog_id_size = dialog_id.size,
                    .immediate = false};
                adapter_status =
                    impl->telephony.prepare_dialog_terminate(
                        impl->telephony_user, &request, &ticket, &error);
            }
            status = retain_ticket(
                impl, adapter_status, ticket, &prepared);
            (void)error;
            if (status != CCXML_OK) return status;
        }
    }
    *out_prepared = prepared;
    *out_exit_requested = exit_requested;
    return CCXML_OK;
}

static void commit_prepared_tickets(void *user) {
    ccxml_session_impl *impl = (ccxml_session_impl *)user;
    size_t index;
    const size_t count = impl != NULL ? impl->prepared_ticket_count : 0u;
    if (impl == NULL) return;
    impl->prepared_ticket_count = 0u;
    for (index = 0u; index < count; ++index) {
        cflow_statechart_effect_ticket *ticket = &impl->tickets[index];
        ticket->commit(ticket->user);
        *ticket = (cflow_statechart_effect_ticket){0};
    }
}

static void discard_prepared_tickets(void *user) {
    ccxml_session_impl *impl = (ccxml_session_impl *)user;
    const size_t count = impl != NULL ? impl->prepared_ticket_count : 0u;
    if (impl == NULL) return;
    impl->prepared_ticket_count = 0u;
    discard_tickets(impl->tickets, count);
}

static bool execute_transition_binding(
    void *user, const cflow_statechart_executable_context *context,
    const char **out_error) {
    ccxml_transition_binding *binding =
        (ccxml_transition_binding *)user;
    ccxml_session_impl *impl;
    const ccxml_event *event;
    bool staged_state;
    bool exit_requested = false;
    size_t prepared = 0u;
    ccxml_status status;
    if (out_error != NULL) *out_error = NULL;
    if (binding == NULL || binding->session == NULL ||
        binding->transition == NULL || context == NULL ||
        context->state == NULL || context->out_state == NULL ||
        context->event == NULL || context->event->id != 1u ||
        context->event->payload_type != &ccxml_event_cmeta_type ||
        context->event->payload == NULL) {
        if (out_error != NULL)
            *out_error = "invalid CCXML transition executable contract";
        return false;
    }
    impl = binding->session;
    event = (const ccxml_event *)context->event->payload;
    impl->transition_selected = true;
    staged_state = *(const bool *)context->state;
    status = execute_transition_actions(
        impl, binding->transition, event, &prepared, &exit_requested);
    impl->dispatch_status = status;
    impl->exit_requested = exit_requested;
    if (status == CCXML_OK && prepared != 0u) {
        const cflow_statechart_effect_ticket aggregate = {
            .commit = commit_prepared_tickets,
            .discard = discard_prepared_tickets,
            .user = impl};
        const char *stage_error = NULL;
        if (impl->prepared_ticket_count != 0u ||
            context->stage_effect == NULL) {
            discard_tickets(impl->tickets, prepared);
            impl->dispatch_status = CCXML_INVALID_CONTRACT;
            if (out_error != NULL)
                *out_error = "CCXML effect journal is unavailable";
            return false;
        }
        impl->prepared_ticket_count = prepared;
        if (!context->stage_effect(
                context->effect_user, &aggregate, &stage_error)) {
            discard_prepared_tickets(impl);
            impl->dispatch_status = CCXML_LIMIT_EXCEEDED;
            if (out_error != NULL) {
                *out_error = stage_error != NULL
                    ? stage_error : "CCXML effect staging failed";
            }
            return false;
        }
    }
    *(bool *)context->out_state = staged_state;
    return true;
}

static ccxml_status map_mailbox_status(cflow_mailbox_status status) {
    switch (status) {
        case CFLOW_MAILBOX_OK:
            return CCXML_OK;
        case CFLOW_MAILBOX_FULL:
        case CFLOW_MAILBOX_BUFFER_TOO_SMALL:
            return CCXML_LIMIT_EXCEEDED;
        case CFLOW_MAILBOX_ALLOCATION_FAILED:
            return CCXML_ALLOCATION_FAILED;
        case CFLOW_MAILBOX_CLOSED:
        case CFLOW_MAILBOX_CANCELLED:
            return CCXML_CLOSED;
        case CFLOW_MAILBOX_INVALID_ARGUMENT:
        case CFLOW_MAILBOX_TYPE_MISMATCH:
        case CFLOW_MAILBOX_EMPTY:
        default:
            return CCXML_INVALID_CONTRACT;
    }
}

ccxml_status ccxml_session_dispatch(
    ccxml_session *session, const ccxml_event *event) {
    ccxml_session_impl *impl = session != NULL
        ? (ccxml_session_impl *)session->impl : NULL;
    const cflow_event_view native_event = {
        1u, &ccxml_event_cmeta_type, event};
    cflow_statechart_instance_stats stats = {0};
    ccxml_status status;
    if (impl == NULL || event == NULL || event->name == NULL ||
        event->name_size == 0u ||
        (event->connection_id == NULL && event->connection_id_size != 0u)) {
        return CCXML_INVALID_ARGUMENT;
    }
    if (impl->closed || impl->terminated) return CCXML_CLOSED;
    if (impl->dispatching) return CCXML_BUSY;
    impl->dispatching = true;
    impl->dispatch_status = CCXML_OK;
    impl->transition_selected = false;
    impl->exit_requested = false;
    status = map_mailbox_status(
        cflow_statechart_instance_try_send(&impl->instance, &native_event));
    if (status != CCXML_OK) {
        impl->dispatching = false;
        return status;
    }
    if (!cflow_executor_wait_idle(&impl->executor)) {
        impl->dispatching = false;
        return CCXML_BUSY;
    }
    status = impl->dispatch_status;
    if (!cflow_statechart_instance_get_stats(&impl->instance, &stats)) {
        status = CCXML_INVALID_CONTRACT;
    } else if (stats.errored && status == CCXML_OK) {
        status = CCXML_INVALID_CONTRACT;
    }
    if (status == CCXML_OK && !impl->transition_selected &&
        unhandled_event_is_fatal(event)) {
        impl->exit_requested = true;
    }
    if (impl->exit_requested && status == CCXML_OK) {
        impl->terminated = true;
        cflow_statechart_instance_close(&impl->instance);
        close_adapter(impl);
    }
    impl->dispatching = false;
    return status;
}

void ccxml_session_close(ccxml_session *session) {
    ccxml_session_impl *impl = session != NULL
        ? (ccxml_session_impl *)session->impl : NULL;
    if (impl != NULL) cflow_statechart_instance_close(&impl->instance);
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
    cflow_statechart_instance_close(&impl->instance);
    close_adapter(impl);
    if (!impl->telephony.is_quiescent(impl->telephony_user))
        return CCXML_BUSY;
    if (impl->event_io.is_quiescent != NULL &&
        !impl->event_io.is_quiescent(impl->event_io_user))
        return CCXML_BUSY;
    if (cflow_statechart_instance_destroy(&impl->instance) !=
        CFLOW_STATECHART_INSTANCE_OK)
        return CCXML_BUSY;
    cflow_executor_destroy(&impl->executor);
    destroy_conditions(impl);
    free(impl->transition_bindings);
    free(impl->executable_bindings);
    free(impl->guard_bindings);
    free(impl->payload_scratch);
    free(impl->tickets);
    free(impl);
    session->impl = NULL;
    return CCXML_OK;
}
