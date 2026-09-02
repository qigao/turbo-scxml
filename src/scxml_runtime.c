#include "scxml_runtime.h"
#include "scxml_analyze.h"
#include "scxml_program.h"
#include "scxml_quickjs.h"
#include "scxml_session.h"

#include <stdlib.h>

typedef enum scxml_execute_outcome {
    SCXML_EXECUTE_CONTINUE = 0,
    SCXML_EXECUTE_BLOCK_ABORTED,
    SCXML_EXECUTE_FATAL
} scxml_execute_outcome;

typedef struct scxml_mutable_state {
    void *value;
    cflow_statechart_host_context *host_context;
} scxml_mutable_state;

static void *mutable_state_get(
    scxml_mutable_state *state, const char **out_error) {
    if (state == NULL) return NULL;
    if (state->value == NULL && state->host_context != NULL) {
        state->value = cflow_statechart_host_context_edit_state(
            state->host_context, out_error);
    }
    return state->value;
}

static const void *mutable_state_read(
    const scxml_mutable_state *state,
    const cflow_statechart_executable_context *context) {
    return state != NULL && state->value != NULL
        ? state->value : context->state;
}

static scxml_execute_outcome execute_scxml_range(
    const scxml_block *block, scxml_session_impl *session,
    const cflow_statechart_executable_context *context,
    scxml_mutable_state *mutable_state,
    const scxml_expr_system_values *system_values,
    size_t begin, size_t end, size_t depth,
    bool abort_condition_error, const char **out_error);

static bool same_send_id(const scxml_delayed_send *row,
                         const char *id, size_t id_size) {
    return row->state != SCXML_DELAYED_FREE && row->id_size == id_size &&
           id != NULL &&
           memcmp(row->id, id, id_size) == 0;
}

static scxml_prepared_effect *acquire_prepared_effect_locked(
    scxml_session_impl *session) {
    size_t index;
    for (index = 0u; index < session->prepared_effect_capacity; ++index) {
        if (!session->prepared_effects[index].in_use) {
            scxml_prepared_effect *effect = &session->prepared_effects[index];
            memset(effect, 0, sizeof(*effect));
            effect->session = session;
            effect->registry_index = SIZE_MAX;
            effect->in_use = true;
            return effect;
        }
    }
    return NULL;
}

scxml_delayed_send *scxml_runtime_find_delayed_send_locked(
    scxml_session_impl *session, const char *id, size_t id_size,
    size_t *out_index) {
    size_t index;
    for (index = 0u; index < session->delayed_send_capacity; ++index) {
        if (same_send_id(&session->delayed_sends[index], id, id_size)) {
            if (out_index != NULL) *out_index = index;
            return &session->delayed_sends[index];
        }
    }
    return NULL;
}

static scxml_delayed_send *reserve_delayed_send_locked(
    scxml_session_impl *session, const char *id, size_t id_size,
    bool copy_generated_id, size_t *out_index, bool *out_duplicate) {
    size_t index;
    scxml_delayed_send *free_row = NULL;
    size_t free_index = SIZE_MAX;
    *out_duplicate = false;
    for (index = 0u; index < session->delayed_send_capacity; ++index) {
        scxml_delayed_send *row = &session->delayed_sends[index];
        if (same_send_id(row, id, id_size)) {
            *out_duplicate = true;
            return NULL;
        }
        if (free_row == NULL && row->state == SCXML_DELAYED_FREE) {
            free_row = row;
            free_index = index;
        }
    }
    if (free_row == NULL || id == NULL || id_size == 0u ||
        (copy_generated_id &&
         id_size > SCXML_EVENT_METADATA_CAPACITY))
        return NULL;
    *free_row = (scxml_delayed_send){0};
    if (copy_generated_id) {
        memcpy(free_row->generated_id, id, id_size);
        free_row->generated_id[id_size] = '\0';
        free_row->id = free_row->generated_id;
    } else {
        free_row->id = id;
    }
    free_row->id_size = id_size;
    free_row->state = SCXML_DELAYED_RESERVED;
    *out_index = free_index;
    return free_row;
}

static void rollback_prepared_effect_locked(scxml_prepared_effect *effect) {
    scxml_session_impl *session = effect->session;
    if (effect->registry_index != SIZE_MAX &&
        effect->registry_index < session->delayed_send_capacity) {
        scxml_delayed_send *row =
            &session->delayed_sends[effect->registry_index];
        if (effect->kind == SCXML_PREPARED_DELAYED_SEND) {
            if (row->state == SCXML_DELAYED_RESERVED) {
                *row = (scxml_delayed_send){0};
            } else if (row->state == SCXML_DELAYED_CANCEL_RESERVED &&
                       row->previous_state == SCXML_DELAYED_RESERVED) {
                row->previous_state = SCXML_DELAYED_FREE;
            }
        } else if (effect->kind == SCXML_PREPARED_CANCEL &&
                   row->state == SCXML_DELAYED_CANCEL_RESERVED) {
            if (row->previous_state == SCXML_DELAYED_FREE) {
                *row = (scxml_delayed_send){0};
            } else {
                row->state = row->previous_state;
                row->previous_state = SCXML_DELAYED_FREE;
            }
        }
    }
    effect->in_use = false;
}

static void commit_prepared_effect(void *user) {
    scxml_prepared_effect *effect = (scxml_prepared_effect *)user;
    scxml_session_impl *session;
    cflow_statechart_effect_ticket adapter_ticket;
    if (effect == NULL || !effect->in_use || effect->session == NULL) return;
    session = effect->session;
    turbo_mutex_lock(&session->registry_lock);
    adapter_ticket = effect->adapter_ticket;
    if (effect->registry_index != SIZE_MAX &&
        effect->registry_index < session->delayed_send_capacity) {
        scxml_delayed_send *row =
            &session->delayed_sends[effect->registry_index];
        if (effect->kind == SCXML_PREPARED_DELAYED_SEND &&
            row->state == SCXML_DELAYED_RESERVED) {
            row->state = SCXML_DELAYED_ACTIVE;
        } else if (effect->kind == SCXML_PREPARED_DELAYED_SEND &&
                   row->state == SCXML_DELAYED_CANCEL_RESERVED &&
                   row->previous_state == SCXML_DELAYED_RESERVED) {
            row->previous_state = SCXML_DELAYED_ACTIVE;
        } else if (effect->kind == SCXML_PREPARED_CANCEL &&
                   row->state == SCXML_DELAYED_CANCEL_RESERVED) {
            *row = (scxml_delayed_send){0};
        }
    }
    effect->in_use = false;
    turbo_mutex_unlock(&session->registry_lock);
    adapter_ticket.commit(adapter_ticket.user);
}

static void discard_prepared_effect(void *user) {
    scxml_prepared_effect *effect = (scxml_prepared_effect *)user;
    scxml_session_impl *session;
    cflow_statechart_effect_ticket adapter_ticket;
    if (effect == NULL || !effect->in_use || effect->session == NULL) return;
    session = effect->session;
    turbo_mutex_lock(&session->registry_lock);
    adapter_ticket = effect->adapter_ticket;
    rollback_prepared_effect_locked(effect);
    turbo_mutex_unlock(&session->registry_lock);
    adapter_ticket.discard(adapter_ticket.user);
}

void scxml_runtime_increment_u64(uint64_t *value) {
    if (*value != UINT64_MAX) ++*value;
}

static scxml_invocation_lifecycle_effect *
acquire_invocation_effect_locked(scxml_session_impl *session) {
    size_t index;
    for (index = 0u; index < session->invocation_effect_capacity; ++index) {
        scxml_invocation_lifecycle_effect *effect =
            &session->invocation_effects[index];
        if (!effect->in_use) {
            *effect = (scxml_invocation_lifecycle_effect){
                .session = session, .in_use = true};
            return effect;
        }
    }
    return NULL;
}

static cflow_mailbox_status report_invocation_adapter_error(
    scxml_session_impl *session, scxml_adapter_status status) {
    const bool null_value = false;
    const cflow_event_id id =
        status == SCXML_ADAPTER_ERROR_EXECUTION ||
        status == SCXML_ADAPTER_INVALID_CONTRACT
            ? session->program->execution_error_event
            : session->program->communication_error_event;
    const cflow_event_view event = {id, &cmeta_type_bool, &null_value};
    cflow_mailbox_status admission = CFLOW_MAILBOX_INVALID_ARGUMENT;
    if (id != 0u)
        admission = cflow_statechart_instance_try_send_internal(
            &session->instance, &event);
    if (admission != CFLOW_MAILBOX_OK) {
        turbo_mutex_lock(&session->registry_lock);
        scxml_runtime_increment_u64(&session->invoke_stats.adapter_error_rejected);
        turbo_mutex_unlock(&session->registry_lock);
    }
    return admission;
}

static void commit_invocation_lifecycle(void *user) {
    scxml_invocation_lifecycle_effect *effect =
        (scxml_invocation_lifecycle_effect *)user;
    scxml_session_impl *session;
    scxml_invoke_cancel_request request;
    cflow_statechart_effect_ticket adapter_ticket = {0};
    scxml_adapter_status status;
    const char *adapter_error = NULL;
    scxml_invocation_effect_kind kind;
    uint64_t token = 0u;
    char id[SCXML_EVENT_METADATA_CAPACITY + 1u] = {0};
    const char *cancel_id = id;
    size_t id_size = 0u;
    bool cancel = false;
    bool start = false;
    bool forward = false;
    if (effect == NULL || !effect->in_use || effect->session == NULL) return;
    session = effect->session;
    if (effect->invocation >= session->program->invocation_count) return;
    kind = effect->kind;
    if (kind == SCXML_INVOCATION_EFFECT_START ||
        kind == SCXML_INVOCATION_EFFECT_FORWARD)
        adapter_ticket = effect->adapter_ticket;
    turbo_mutex_lock(&session->registry_lock);
    if (effect->invocation < session->invocation_capacity) {
        scxml_invocation_row *row =
            &session->invocation_rows[effect->invocation];
        if (effect->kind == SCXML_INVOCATION_EFFECT_ENTER) {
            *row = (scxml_invocation_row){
                .state = SCXML_INVOCATION_PENDING};
        } else if (effect->kind == SCXML_INVOCATION_EFFECT_EXIT) {
            if (row->state == SCXML_INVOCATION_ACTIVE) {
                token = row->token;
                cancel = token != 0u;
                id_size = row->id_size;
                if (row->owns_id) {
                    if (id_size != 0u)
                        memcpy(id, row->id, id_size + 1u);
                } else {
                    cancel_id = row->id;
                }
                if (session->invoke_stats.active != 0u)
                    --session->invoke_stats.active;
            }
            *row = (scxml_invocation_row){0};
        } else if (effect->kind == SCXML_INVOCATION_EFFECT_START &&
                   row->state == SCXML_INVOCATION_START_RESERVED &&
                   row->token == effect->token) {
            row->state = SCXML_INVOCATION_ACTIVE;
            scxml_runtime_increment_u64(&session->invoke_stats.started);
            ++session->invoke_stats.active;
            start = true;
        } else if (effect->kind == SCXML_INVOCATION_EFFECT_FAIL &&
                   row->state == SCXML_INVOCATION_FAIL_RESERVED &&
                   row->token == effect->token) {
            row->state = SCXML_INVOCATION_FAILED;
            scxml_runtime_increment_u64(&session->invoke_stats.start_failed);
        } else if (effect->kind == SCXML_INVOCATION_EFFECT_COMPLETE &&
                   row->state == SCXML_INVOCATION_ACTIVE &&
                   row->token == effect->token) {
            *row = (scxml_invocation_row){0};
            if (session->invoke_stats.active != 0u)
                --session->invoke_stats.active;
            scxml_runtime_increment_u64(&session->invoke_stats.completed);
        } else if (effect->kind == SCXML_INVOCATION_EFFECT_FORWARD) {
            scxml_runtime_increment_u64(&session->invoke_stats.forwarded);
            forward = true;
        }
    }
    effect->in_use = false;
    turbo_mutex_unlock(&session->registry_lock);
    if (start) {
        adapter_ticket.commit(adapter_ticket.user);
        return;
    }
    if (forward) {
        adapter_ticket.commit(adapter_ticket.user);
        return;
    }
    if (kind == SCXML_INVOCATION_EFFECT_START &&
        adapter_ticket.discard != NULL) {
        adapter_ticket.discard(adapter_ticket.user);
        return;
    }
    if (!cancel) return;
    request = (scxml_invoke_cancel_request){
        .token = token, .id = cancel_id, .id_size = id_size};
    status = session->invoke.prepare_cancel(
        session->invoke_user, &request, &adapter_ticket, &adapter_error);
    (void)adapter_error;
    if (status != SCXML_ADAPTER_ACCEPTED ||
        adapter_ticket.commit == NULL || adapter_ticket.discard == NULL) {
        turbo_mutex_lock(&session->registry_lock);
        scxml_runtime_increment_u64(&session->invoke_stats.cancel_failed);
        turbo_mutex_unlock(&session->registry_lock);
        (void)report_invocation_adapter_error(
            session, status == SCXML_ADAPTER_ACCEPTED
                ? SCXML_ADAPTER_INVALID_CONTRACT : status);
        return;
    }
    turbo_mutex_lock(&session->registry_lock);
    scxml_runtime_increment_u64(&session->invoke_stats.cancelled);
    turbo_mutex_unlock(&session->registry_lock);
    adapter_ticket.commit(adapter_ticket.user);
}

static void discard_invocation_lifecycle(void *user) {
    scxml_invocation_lifecycle_effect *effect =
        (scxml_invocation_lifecycle_effect *)user;
    scxml_session_impl *session;
    cflow_statechart_effect_ticket adapter_ticket = {0};
    bool discard_adapter = false;
    if (effect == NULL || !effect->in_use || effect->session == NULL) return;
    session = effect->session;
    turbo_mutex_lock(&session->registry_lock);
    if (effect->invocation < session->invocation_capacity &&
        (effect->kind == SCXML_INVOCATION_EFFECT_START ||
         effect->kind == SCXML_INVOCATION_EFFECT_FAIL)) {
        scxml_invocation_row *row =
            &session->invocation_rows[effect->invocation];
        const scxml_invocation_state reserved =
            effect->kind == SCXML_INVOCATION_EFFECT_START
                ? SCXML_INVOCATION_START_RESERVED
                : SCXML_INVOCATION_FAIL_RESERVED;
        if (row->state == reserved && row->token == effect->token)
            *row = (scxml_invocation_row){
                .state = SCXML_INVOCATION_PENDING};
        if (effect->kind == SCXML_INVOCATION_EFFECT_START) {
            adapter_ticket = effect->adapter_ticket;
            discard_adapter = adapter_ticket.discard != NULL;
        }
    } else if (effect->kind == SCXML_INVOCATION_EFFECT_FORWARD) {
        adapter_ticket = effect->adapter_ticket;
        discard_adapter = adapter_ticket.discard != NULL;
    }
    effect->in_use = false;
    turbo_mutex_unlock(&session->registry_lock);
    if (discard_adapter) adapter_ticket.discard(adapter_ticket.user);
}

static scxml_execute_outcome execute_invocation_lifecycle(
    const scxml_block *block, scxml_session_impl *session,
    const scxml_step *step,
    const cflow_statechart_executable_context *context,
    const char **out_error) {
    scxml_invocation_lifecycle_effect *effect;
    cflow_statechart_effect_ticket ticket;
    if (session == NULL || !session->has_invoke ||
        block->invocations == NULL ||
        step->invocation >= block->invocation_storage_count ||
        step->invocation >= session->invocation_capacity ||
        context->stage_effect == NULL) {
        *out_error = "SCXML invoke requires an owning invocation session";
        return SCXML_EXECUTE_FATAL;
    }
    turbo_mutex_lock(&session->registry_lock);
    effect = acquire_invocation_effect_locked(session);
    if (effect != NULL) {
        effect->invocation = step->invocation;
        effect->kind = step->kind == SCXML_STEP_INVOKE_ENTER
            ? SCXML_INVOCATION_EFFECT_ENTER
            : SCXML_INVOCATION_EFFECT_EXIT;
    }
    turbo_mutex_unlock(&session->registry_lock);
    if (effect == NULL) {
        *out_error = "SCXML invocation effect storage is full";
        return SCXML_EXECUTE_FATAL;
    }
    ticket = (cflow_statechart_effect_ticket){
        commit_invocation_lifecycle, discard_invocation_lifecycle, effect};
    if (!context->stage_effect(context->effect_user, &ticket, out_error)) {
        discard_invocation_lifecycle(effect);
        return SCXML_EXECUTE_FATAL;
    }
    return SCXML_EXECUTE_CONTINUE;
}

typedef struct scxml_evaluation_context {
    const void *state;
    cflow_statechart_is_active_fn is_active;
    void *configuration_user;
    cflow_statechart_raise_fn raise_internal;
    void *raise_user;
} scxml_evaluation_context;

static bool enqueue_invocation_adapter_error(
    scxml_session_impl *session,
    const scxml_evaluation_context *context,
    scxml_adapter_status status, const char **out_error) {
    const bool null_value = false;
    const cflow_event_id id = status == SCXML_ADAPTER_ERROR_EXECUTION
        ? session->program->execution_error_event
        : session->program->communication_error_event;
    const cflow_event_view event = {id, &cmeta_type_bool, &null_value};
    if (id == 0u || context->raise_internal == NULL) {
        turbo_mutex_lock(&session->registry_lock);
        scxml_runtime_increment_u64(&session->invoke_stats.adapter_error_rejected);
        turbo_mutex_unlock(&session->registry_lock);
        *out_error = "SCXML invocation error Event is unavailable";
        return false;
    }
    if (!context->raise_internal(
            context->raise_user, &event, out_error)) {
        turbo_mutex_lock(&session->registry_lock);
        scxml_runtime_increment_u64(&session->invoke_stats.adapter_error_rejected);
        turbo_mutex_unlock(&session->registry_lock);
        return false;
    }
    return true;
}

static bool evaluate_runtime_hook_active(
    void *user, cflow_machine_state_id state, bool *out_active) {
    const scxml_evaluation_context *context =
        (const scxml_evaluation_context *)user;
    if (context == NULL || context->is_active == NULL ||
        context->configuration_user == NULL || out_active == NULL)
        return false;
    *out_active = context->is_active(context->configuration_user, state);
    return true;
}

static bool evaluate_invocation_string(
    const scxml_expr_program *program,
    scxml_session_impl *session,
    const scxml_evaluation_context *context,
    const char **out_data, size_t *out_size) {
    scxml_expr_diagnostic diagnostic = {0};
    scxml_expr_value value = {0};
    if (program == NULL || session == NULL || context == NULL ||
        context->state == NULL || out_data == NULL || out_size == NULL ||
        scxml_expr_evaluate_value_with_system(
            program, context->state, evaluate_runtime_hook_active,
            (void *)context, &session->system_values, &value,
            &diagnostic) != SCXML_EXPR_OK ||
        value.kind != SCXML_EXPR_VALUE_STRING ||
        value.data.string.size == 0u)
        return false;
    *out_data = value.data.string.data;
    *out_size = value.data.string.size;
    return true;
}

static bool evaluate_invocation_value(
    const scxml_expr_program *program,
    scxml_session_impl *session,
    const scxml_evaluation_context *context,
    scxml_payload_value *out) {
    scxml_expr_diagnostic diagnostic = {0};
    scxml_expr_value value = {0};
    return program != NULL && session != NULL && context != NULL &&
        context->state != NULL && out != NULL &&
        scxml_expr_evaluate_value_with_system(
            program, context->state, evaluate_runtime_hook_active,
            (void *)context, &session->system_values, &value,
            &diagnostic) == SCXML_EXPR_OK &&
        scxml_runtime_payload_value_from_cmeta(&value, out);
}

static bool materialize_invocation_payload(
    scxml_session_impl *session,
    const scxml_invocation_descriptor *invocation,
    const scxml_evaluation_context *context,
    scxml_payload_view *out) {
    size_t index;
    scxml_payload_value scalar = {0};
    if (session == NULL || session->program == NULL ||
        invocation == NULL || context == NULL || out == NULL ||
        invocation->payload_first > session->program->payload_count ||
        invocation->payload_count >
            session->program->payload_count - invocation->payload_first ||
        invocation->payload_count > session->payload_scratch_capacity ||
        (invocation->payload_count != 0u &&
         (session->program->payloads == NULL ||
          session->payload_scratch == NULL)))
        return false;
    *out = (scxml_payload_view){0};
    if (invocation->content.kind != SCXML_CONTENT_INVALID &&
        invocation->content.kind != SCXML_CONTENT_SCALAR) {
        out->kind = SCXML_PAYLOAD_CONTENT;
        return scxml_runtime_materialize_content_descriptor(
            &invocation->content, context->state,
            &session->system_values, &out->content);
    }
    if (invocation->content.kind == SCXML_CONTENT_SCALAR) {
        if (!evaluate_invocation_value(
                &invocation->data_expr, session, context, &scalar))
            return false;
        out->kind = SCXML_PAYLOAD_CONTENT;
        out->content = (scxml_content_view){
            .kind = SCXML_CONTENT_SCALAR, .scalar = scalar};
        return true;
    }
    if (invocation->payload_count == 0u) return true;
    for (index = 0u; index < invocation->payload_count; ++index) {
        const scxml_payload_descriptor *descriptor =
            &session->program->payloads[
                invocation->payload_first + index];
        scxml_payload_entry *entry =
            &session->payload_scratch[index];
        if (!evaluate_invocation_value(
                &descriptor->expression, session, context,
                &scalar))
            return false;
        entry->name = descriptor->name;
        entry->name_size = descriptor->name_size;
        entry->value = (scxml_content_view){
            .kind = SCXML_CONTENT_SCALAR, .scalar = scalar};
    }
    out->kind = SCXML_PAYLOAD_NAMED;
    out->entries = session->payload_scratch;
    out->entry_count = invocation->payload_count;
    return true;
}

static bool restore_invocation_id_location(
    const scxml_invocation_descriptor *descriptor,
    const void *published_state, void *staged_state,
    const scxml_expr_system_values *system_values,
    const char *id, size_t id_size, bool *out_restored) {
    scxml_expr_diagnostic diagnostic = {0};
    const unsigned char *previous = NULL;
    size_t previous_size = 0u;
    unsigned char *destination;
    cmeta_status restore_status;
    if (out_restored != NULL) *out_restored = true;
    if (descriptor == NULL || published_state == NULL ||
        staged_state == NULL || id == NULL || out_restored == NULL ||
        !descriptor->has_id_location ||
        descriptor->id_location.value == NULL)
        return false;
    if (scxml_expr_require_data_bound(
            system_values, descriptor->id_location.offset,
            descriptor->id_location.storage_size, &diagnostic) !=
        SCXML_EXPR_OK)
        return false;
    if (cmeta_data_buffer_read(
            descriptor->id_location.value,
            (const unsigned char *)published_state +
                descriptor->id_location.offset,
            SCXML_EVENT_METADATA_CAPACITY,
            &previous, &previous_size) != CMETA_OK)
        return false;
    if (scxml_location_assign_owned_string(
            &descriptor->id_location, staged_state, id, id_size,
            SCXML_EVENT_METADATA_CAPACITY,
            &diagnostic) == SCXML_EXPR_OK)
        return true;
    destination = (unsigned char *)staged_state +
        descriptor->id_location.offset;
    if (previous_size == 0u) {
        restore_status = cmeta_data_buffer_restore_zero(
            descriptor->id_location.value, destination);
        *out_restored = restore_status == CMETA_OK;
    } else {
        *out_restored =
            scxml_location_assign_owned_string(
                &descriptor->id_location, staged_state,
                (const char *)previous, previous_size,
                SCXML_EVENT_METADATA_CAPACITY,
                &diagnostic) == SCXML_EXPR_OK;
    }
    return false;
}

static bool host_context_is_active(
    void *user, cflow_machine_state_id state) {
    return cflow_statechart_host_context_is_active(
        (const cflow_statechart_host_context *)user, state);
}

static bool host_context_raise_internal(
    void *user, const cflow_event_view *event, const char **out_error) {
    return cflow_statechart_host_context_raise_internal(
        (cflow_statechart_host_context *)user, event, 0u, out_error);
}

static bool host_context_stage_effect(
    void *user, const cflow_statechart_effect_ticket *ticket,
    const char **out_error) {
    return cflow_statechart_host_context_stage_effect(
        (cflow_statechart_host_context *)user, ticket, out_error);
}

static scxml_evaluation_context host_evaluation_context(
    cflow_statechart_host_context *context, const void *state) {
    const scxml_evaluation_context evaluation = {
        .state = state,
        .is_active = host_context_is_active,
        .configuration_user = context,
        .raise_internal = host_context_raise_internal,
        .raise_user = context};
    return evaluation;
}

static bool stage_invocation_result(
    scxml_session_impl *session, size_t invocation,
    uint64_t token, const char *id, size_t id_size, bool own_id,
    scxml_invocation_effect_kind kind,
    const cflow_statechart_effect_ticket *adapter_ticket,
    cflow_statechart_host_context *context,
    const char **out_error) {
    scxml_invocation_lifecycle_effect *effect;
    cflow_statechart_effect_ticket ticket;
    scxml_invocation_row *row;
    const bool owns_adapter_ticket =
        kind == SCXML_INVOCATION_EFFECT_START && adapter_ticket != NULL &&
        adapter_ticket->discard != NULL;
    if (session == NULL || invocation >= session->invocation_capacity ||
        token == 0u ||
        (own_id && id_size > SCXML_EVENT_METADATA_CAPACITY) ||
        (id_size != 0u && id == NULL) ||
        (kind != SCXML_INVOCATION_EFFECT_START &&
         kind != SCXML_INVOCATION_EFFECT_FAIL) ||
        context == NULL || out_error == NULL) {
        if (out_error != NULL)
            *out_error = "SCXML invocation result reservation is invalid";
        if (owns_adapter_ticket)
            adapter_ticket->discard(adapter_ticket->user);
        return false;
    }
    turbo_mutex_lock(&session->registry_lock);
    row = &session->invocation_rows[invocation];
    effect = row->state == SCXML_INVOCATION_PENDING
        ? acquire_invocation_effect_locked(session) : NULL;
    if (effect != NULL) {
        *row = (scxml_invocation_row){
            .token = token,
            .id_size = id_size,
            .owns_id = own_id,
            .state = kind == SCXML_INVOCATION_EFFECT_START
                ? SCXML_INVOCATION_START_RESERVED
                : SCXML_INVOCATION_FAIL_RESERVED};
        if (own_id) {
            if (id_size != 0u) memcpy(row->owned_id, id, id_size);
            row->owned_id[id_size] = '\0';
            row->id = row->owned_id;
        } else {
            row->id = id;
        }
        effect->invocation = invocation;
        effect->token = token;
        effect->kind = kind;
        if (adapter_ticket != NULL)
            effect->adapter_ticket = *adapter_ticket;
    }
    turbo_mutex_unlock(&session->registry_lock);
    if (effect == NULL) {
        *out_error = "SCXML invocation effect storage is full";
        if (owns_adapter_ticket)
            adapter_ticket->discard(adapter_ticket->user);
        return false;
    }
    ticket = (cflow_statechart_effect_ticket){
        commit_invocation_lifecycle, discard_invocation_lifecycle, effect};
    if (!cflow_statechart_host_context_stage_effect(
            context, &ticket, out_error)) {
        discard_invocation_lifecycle(effect);
        return false;
    }
    return true;
}

static bool stage_invocation_completion(
    scxml_session_impl *session, size_t invocation, uint64_t token,
    cflow_statechart_host_context *context, const char **out_error) {
    scxml_invocation_lifecycle_effect *effect;
    cflow_statechart_effect_ticket ticket;
    if (session == NULL || invocation >= session->invocation_capacity ||
        token == 0u || context == NULL || out_error == NULL) {
        if (out_error != NULL)
            *out_error = "SCXML invocation completion is invalid";
        return false;
    }
    turbo_mutex_lock(&session->registry_lock);
    effect = acquire_invocation_effect_locked(session);
    if (effect != NULL) {
        effect->invocation = invocation;
        effect->token = token;
        effect->kind = SCXML_INVOCATION_EFFECT_COMPLETE;
    }
    turbo_mutex_unlock(&session->registry_lock);
    if (effect == NULL) {
        *out_error = "SCXML invocation effect storage is full";
        return false;
    }
    ticket = (cflow_statechart_effect_ticket){
        commit_invocation_lifecycle, discard_invocation_lifecycle, effect};
    if (!cflow_statechart_host_context_stage_effect(
            context, &ticket, out_error)) {
        discard_invocation_lifecycle(effect);
        return false;
    }
    return true;
}

static bool stage_invocation_forward(
    scxml_session_impl *session, size_t invocation,
    const cflow_statechart_effect_ticket *adapter_ticket,
    cflow_statechart_host_context *context, const char **out_error) {
    scxml_invocation_lifecycle_effect *effect;
    cflow_statechart_effect_ticket ticket;
    if (session == NULL || invocation >= session->invocation_capacity ||
        adapter_ticket == NULL || adapter_ticket->commit == NULL ||
        adapter_ticket->discard == NULL || context == NULL ||
        out_error == NULL) {
        if (adapter_ticket != NULL && adapter_ticket->discard != NULL)
            adapter_ticket->discard(adapter_ticket->user);
        if (out_error != NULL)
            *out_error = "SCXML invocation forward ticket is invalid";
        return false;
    }
    turbo_mutex_lock(&session->registry_lock);
    effect = acquire_invocation_effect_locked(session);
    if (effect != NULL) {
        effect->invocation = invocation;
        effect->kind = SCXML_INVOCATION_EFFECT_FORWARD;
        effect->adapter_ticket = *adapter_ticket;
    }
    turbo_mutex_unlock(&session->registry_lock);
    if (effect == NULL) {
        adapter_ticket->discard(adapter_ticket->user);
        *out_error = "SCXML invocation effect storage is full";
        return false;
    }
    ticket = (cflow_statechart_effect_ticket){
        commit_invocation_lifecycle, discard_invocation_lifecycle, effect};
    if (!cflow_statechart_host_context_stage_effect(
            context, &ticket, out_error)) {
        discard_invocation_lifecycle(effect);
        return false;
    }
    return true;
}

static cflow_statechart_host_result
scxml_runtime_start_pending_invocations(
    scxml_session_impl *session,
    cflow_statechart_host_context *context,
    const char **out_error) {
    scxml_evaluation_context evaluation;
    const void *published_state;
    void *staged_state;
    size_t index;
    if (out_error != NULL) *out_error = NULL;
    if (session == NULL || context == NULL || out_error == NULL) {
        if (out_error != NULL)
            *out_error = "SCXML invocation host transaction is invalid";
        return CFLOW_STATECHART_HOST_FATAL;
    }
    published_state = cflow_statechart_host_context_state(context);
    staged_state = cflow_statechart_host_context_edit_state(
        context, out_error);
    if (published_state == NULL || staged_state == NULL)
        return CFLOW_STATECHART_HOST_FATAL;
    evaluation = host_evaluation_context(context, staged_state);
    for (index = 0u; index < session->program->invocation_count; ++index) {
        const scxml_invocation_descriptor *descriptor =
            &session->program->invocations[index];
        scxml_invoke_start_request request;
        scxml_payload_view payload = {0};
        cflow_statechart_effect_ticket adapter_ticket = {0};
        scxml_adapter_status status;
        const char *adapter_error = NULL;
        const char *dynamic_type = descriptor->type;
        size_t dynamic_type_size = descriptor->type_size;
        const char *dynamic_src = descriptor->src;
        size_t dynamic_src_size = descriptor->src_size;
        char generated_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
        const char *id = descriptor->id;
        size_t id_size = descriptor->id_size;
        uint64_t token;
        int written;
        bool restored = true;

        turbo_mutex_lock(&session->registry_lock);
        if (session->invocation_rows[index].state !=
                SCXML_INVOCATION_PENDING) {
            turbo_mutex_unlock(&session->registry_lock);
            continue;
        }
        turbo_mutex_unlock(&session->registry_lock);
        if (!cflow_statechart_host_context_is_active(
                context, descriptor->owner))
            continue;

        turbo_mutex_lock(&session->registry_lock);
        token = session->next_invocation_token;
        if (token != 0u)
            session->next_invocation_token =
                token == UINT64_MAX ? 0u : token + 1u;
        turbo_mutex_unlock(&session->registry_lock);
        if (token == 0u) {
            *out_error = "SCXML invocation token space is exhausted";
            return CFLOW_STATECHART_HOST_FATAL;
        }
        if (descriptor->has_id_location) {
            written = snprintf(
                generated_id, sizeof(generated_id), "%.*s.%" PRIu64,
                (int)descriptor->owner_id_size, descriptor->id, token);
            if (written < 0 || (size_t)written >= sizeof(generated_id) ||
                (size_t)written > descriptor->dynamic_id_max_size) {
                *out_error = "SCXML dynamic invocation ID exceeds its bound";
                return CFLOW_STATECHART_HOST_FATAL;
            }
            id = generated_id;
            id_size = (size_t)written;
            if (!restore_invocation_id_location(
                    descriptor, published_state,
                    staged_state, &session->system_values,
                    id, id_size, &restored)) {
                if (!restored) {
                    *out_error =
                        "SCXML invocation idlocation rollback failed";
                    return CFLOW_STATECHART_HOST_FATAL;
                }
                if (!stage_invocation_result(
                        session, index, token, NULL, 0u, true,
                        SCXML_INVOCATION_EFFECT_FAIL, NULL,
                        context, out_error) ||
                    !enqueue_invocation_adapter_error(
                        session, &evaluation,
                        SCXML_ADAPTER_ERROR_EXECUTION, out_error))
                    return CFLOW_STATECHART_HOST_FATAL;
                continue;
            }
        }
        if ((descriptor->has_type_expr &&
             !evaluate_invocation_string(
                 &descriptor->type_expr, session, &evaluation,
                 &dynamic_type, &dynamic_type_size)) ||
            (descriptor->has_src_expr &&
             !evaluate_invocation_string(
                 &descriptor->src_expr, session, &evaluation,
                 &dynamic_src, &dynamic_src_size)) ||
            ((descriptor->content.kind != SCXML_CONTENT_INVALID ||
              descriptor->payload_count != 0u) &&
             !materialize_invocation_payload(
                 session, descriptor, &evaluation, &payload))) {
            if (!stage_invocation_result(
                    session, index, token, id, id_size,
                    descriptor->has_id_location,
                    SCXML_INVOCATION_EFFECT_FAIL, NULL,
                    context, out_error) ||
                !enqueue_invocation_adapter_error(
                    session, &evaluation,
                    SCXML_ADAPTER_ERROR_EXECUTION, out_error))
                return CFLOW_STATECHART_HOST_FATAL;
            continue;
        }
        request = (scxml_invoke_start_request){
            .token = token,
            .id = id,
            .id_size = id_size,
            .type = dynamic_type,
            .type_size = dynamic_type_size,
            .src = dynamic_src,
            .src_size = dynamic_src_size,
            .autoforward = descriptor->autoforward,
            .payload = payload};
        status = session->invoke.prepare_start(
            session->invoke_user, &request, &adapter_ticket,
            &adapter_error);
        if (status == SCXML_ADAPTER_ACCEPTED &&
            adapter_ticket.commit != NULL && adapter_ticket.discard != NULL) {
            if (!stage_invocation_result(
                    session, index, token, id, id_size,
                    descriptor->has_id_location,
                    SCXML_INVOCATION_EFFECT_START, &adapter_ticket,
                    context, out_error))
                return CFLOW_STATECHART_HOST_FATAL;
            continue;
        }
        if (status == SCXML_ADAPTER_INVALID_CONTRACT ||
            status == SCXML_ADAPTER_ACCEPTED) {
            *out_error = adapter_error != NULL && adapter_error[0] != '\0'
                ? adapter_error
                : "SCXML invocation adapter returned an invalid start ticket";
            return CFLOW_STATECHART_HOST_FATAL;
        }
        if (!stage_invocation_result(
                session, index, token, id, id_size,
                descriptor->has_id_location,
                SCXML_INVOCATION_EFFECT_FAIL, NULL,
                context, out_error) ||
            !enqueue_invocation_adapter_error(
                session, &evaluation, status, out_error))
            return CFLOW_STATECHART_HOST_FATAL;
    }
    return CFLOW_STATECHART_HOST_CONTINUE;
}

static bool execute_invocation_finalize(
    const scxml_invocation_descriptor *descriptor,
    scxml_session_impl *session,
    cflow_statechart_host_context *context,
    const char **out_error) {
    const scxml_block *block = descriptor->finalize;
    const cflow_statechart_observed_event *trigger;
    cflow_statechart_executable_context executable;
    scxml_mutable_state mutable_state = {0};
    scxml_execute_outcome outcome;
    if (block == NULL) return true;
    if (block->steps == NULL ||
        (block->branch_storage_count != 0u && block->branches == NULL) ||
        block->step_begin >= block->step_end ||
        block->step_end > block->step_storage_count) {
        *out_error = "SCXML finalize block context is invalid";
        return false;
    }
    trigger = cflow_statechart_host_context_trigger(context);
    if (trigger == NULL || trigger->kind != CFLOW_STATECHART_OBSERVED_EXTERNAL ||
        trigger->event == NULL) {
        *out_error = "SCXML finalize trigger is invalid";
        return false;
    }
    executable = (cflow_statechart_executable_context){
        .state = cflow_statechart_host_context_state(context),
        .event = trigger->event,
        .raise_internal = host_context_raise_internal,
        .raise_user = context,
        .stage_effect = host_context_stage_effect,
        .effect_user = context,
        .is_active = host_context_is_active,
        .configuration_user = context};
    mutable_state.host_context = context;
    outcome = execute_scxml_range(
        block, session, &executable, &mutable_state,
        &session->system_values, block->step_begin,
        block->step_end, 0u, true, out_error);
    if (outcome == SCXML_EXECUTE_CONTINUE) return true;
    if (*out_error == NULL)
        *out_error = "SCXML finalize execution failed";
    return false;
}

static scxml_event_envelope_view current_event_envelope(
    const scxml_expr_system_values *values) {
    scxml_content_view data = {
        .kind = SCXML_CONTENT_TEXT_UTF8,
        .bytes = values->event_data.data,
        .byte_count = values->event_data.size};
    if (values->event_data_schema != NULL &&
        values->event_data_object != NULL) {
        data = (scxml_content_view){
            .kind = SCXML_CONTENT_CMETA,
            .schema = values->event_data_schema,
            .object = values->event_data_object};
    }
    return (scxml_event_envelope_view){
        .abi_version = SCXML_EVENT_ENVELOPE_ABI,
        .struct_size = sizeof(scxml_event_envelope_view),
        .name = values->event_name.data,
        .name_size = values->event_name.size,
        .type = values->event_type.data,
        .type_size = values->event_type.size,
        .send_id = values->event_send_id.data,
        .send_id_size = values->event_send_id.size,
        .origin = values->event_origin.data,
        .origin_size = values->event_origin.size,
        .origin_type = values->event_origin_type.data,
        .origin_type_size = values->event_origin_type.size,
        .invoke_id = values->event_invoke_id.data,
        .invoke_id_size = values->event_invoke_id.size,
        .data = data};
}

static bool forward_external_to_invocations(
    scxml_session_impl *session,
    const scxml_evaluation_context *context,
    cflow_statechart_host_context *host_context,
    const cflow_event_view *event, size_t skipped_invocation,
    const char **out_error) {
    const scxml_event_envelope_view envelope =
        current_event_envelope(&session->system_values);
    size_t index;
    for (index = 0u; index < session->program->invocation_count; ++index) {
        const scxml_invocation_descriptor *descriptor =
            &session->program->invocations[index];
        scxml_invoke_forward_request request;
        cflow_statechart_effect_ticket adapter_ticket = {0};
        scxml_adapter_status status;
        const char *adapter_error = NULL;
        uint64_t token = 0u;
        char id[SCXML_EVENT_METADATA_CAPACITY + 1u] = {0};
        const char *request_id = NULL;
        size_t id_size = 0u;
        if (!descriptor->autoforward || index == skipped_invocation) continue;
        turbo_mutex_lock(&session->registry_lock);
        if (session->invocation_rows[index].state ==
                SCXML_INVOCATION_ACTIVE) {
            token = session->invocation_rows[index].token;
            id_size = session->invocation_rows[index].id_size;
            if (session->invocation_rows[index].owns_id) {
                if (id_size != 0u)
                    memcpy(id, session->invocation_rows[index].id,
                           id_size + 1u);
                request_id = id;
            } else {
                request_id = session->invocation_rows[index].id;
            }
        }
        turbo_mutex_unlock(&session->registry_lock);
        if (token == 0u) continue;
        request = (scxml_invoke_forward_request){
            .token = token,
            .id = request_id,
            .id_size = id_size,
            .event = event,
            .envelope = &envelope};
        status = session->invoke.prepare_forward(
            session->invoke_user, &request, &adapter_ticket, &adapter_error);
        if (status == SCXML_ADAPTER_ACCEPTED &&
            adapter_ticket.commit != NULL && adapter_ticket.discard != NULL) {
            if (!stage_invocation_forward(
                    session, index, &adapter_ticket, host_context,
                    out_error))
                return false;
            continue;
        }
        turbo_mutex_lock(&session->registry_lock);
        scxml_runtime_increment_u64(&session->invoke_stats.forward_failed);
        turbo_mutex_unlock(&session->registry_lock);
        if (status == SCXML_ADAPTER_INVALID_CONTRACT ||
            (status == SCXML_ADAPTER_ACCEPTED &&
             (adapter_ticket.commit == NULL ||
              adapter_ticket.discard == NULL))) {
            *out_error = adapter_error != NULL && adapter_error[0] != '\0'
                ? adapter_error
                : "SCXML invocation adapter returned an invalid forward ticket";
            return false;
        }
        if (!enqueue_invocation_adapter_error(
                session, context, status, out_error))
            return false;
    }
    return true;
}

static const scxml_program_name *find_state_program_name_by_id(
    const scxml_program_impl *program, cflow_machine_state_id id) {
    size_t index;
    if (program == NULL || id == 0u) return NULL;
    for (index = 0u; index < program->state_name_count; ++index) {
        if (program->state_names[index].id == id)
            return &program->state_names[index];
    }
    return NULL;
}

bool scxml_runtime_copy_event_data_object(const cmeta_data_desc *schema,
                                   void *destination,
                                   const void *source) {
    const cmeta_type_desc *type;
    if (!cmeta_data_desc_valid(schema) || schema->kind != CMETA_DATA_STRUCT ||
        schema->storage_type == NULL || destination == NULL || source == NULL)
        return false;
    type = schema->storage_type;
    if (type->size > SCXML_EVENT_DATA_CAPACITY ||
        type->align > _Alignof(scxml_event_data_storage))
        return false;
    if (cmeta_type_require_traits(
            type, CMETA_TRAIT_TRIVIAL_COPY |
                      CMETA_TRAIT_TRIVIAL_DESTROY) == CMETA_OK) {
        memcpy(destination, source, type->size);
        return true;
    }
    return cmeta_type_require_traits(
               type, CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY) == CMETA_OK &&
        type->traits->copy_construct(destination, source);
}

void scxml_runtime_destroy_event_data_object(const cmeta_data_desc *schema,
                                      void *object) {
    const cmeta_type_desc *type = schema != NULL ? schema->storage_type : NULL;
    if (type == NULL || object == NULL) return;
    if (cmeta_type_require_traits(
            type, CMETA_TRAIT_TRIVIAL_DESTROY) != CMETA_OK &&
        cmeta_type_require_traits(type, CMETA_TRAIT_DESTROY) == CMETA_OK)
        type->traits->destroy(object);
}

static void completion_data_discard_payload(
    scxml_completion_data_slot *slot) {
    if (slot == NULL) return;
    if (slot->data_object_live) {
        scxml_runtime_destroy_event_data_object(
            slot->data_schema, slot->data_object.bytes);
        slot->data_object_live = false;
    }
    slot->data_schema = NULL;
    slot->data_size = 0u;
    slot->data[0] = '\0';
}

static void completion_data_release(scxml_completion_data_slot *slot) {
    cmeta_data_field_desc *projection_fields;
    size_t projection_field_capacity;
    char *projection_stable_id;
    size_t projection_stable_id_capacity;
    if (slot == NULL) return;
    projection_fields = slot->projection_fields;
    projection_field_capacity = slot->projection_field_capacity;
    projection_stable_id = slot->projection_stable_id;
    projection_stable_id_capacity = slot->projection_stable_id_capacity;
    completion_data_discard_payload(slot);
    memset(slot, 0, sizeof(*slot));
    slot->projection_fields = projection_fields;
    slot->projection_field_capacity = projection_field_capacity;
    slot->projection_stable_id = projection_stable_id;
    slot->projection_stable_id_capacity =
        projection_stable_id_capacity;
}

static scxml_completion_data_slot *completion_data_reserve(
    scxml_session_impl *session, cflow_machine_state_id parent,
    const char **out_error) {
    size_t index;
    if (session == NULL || out_error == NULL || parent == 0u) {
        if (out_error != NULL)
            *out_error = "SCXML completion data reservation is invalid";
        return NULL;
    }
    if (session->next_completion_data_sequence == UINT64_MAX) {
        *out_error = "SCXML completion data sequence is exhausted";
        return NULL;
    }
    for (index = 0u; index < session->completion_data_capacity; ++index) {
        scxml_completion_data_slot *slot =
            &session->completion_data_slots[index];
        if (slot->state != SCXML_COMPLETION_DATA_FREE) continue;
        slot->state = SCXML_COMPLETION_DATA_BUILDING;
        slot->parent = parent;
        slot->sequence = session->next_completion_data_sequence++;
        return slot;
    }
    *out_error = "SCXML completion data storage is full";
    return NULL;
}

static void completion_data_publish_empty(
    scxml_completion_data_slot *slot) {
    completion_data_discard_payload(slot);
    slot->state = SCXML_COMPLETION_DATA_READY;
}

static scxml_completion_data_slot *completion_data_bind_oldest(
    scxml_session_impl *session, cflow_machine_state_id parent,
    size_t *out_index) {
    scxml_completion_data_slot *selected = NULL;
    size_t selected_index = SIZE_MAX;
    size_t index;
    for (index = 0u; index < session->completion_data_capacity; ++index) {
        scxml_completion_data_slot *candidate =
            &session->completion_data_slots[index];
        if (candidate->state != SCXML_COMPLETION_DATA_READY ||
            candidate->parent != parent ||
            (selected != NULL &&
             candidate->sequence >= selected->sequence))
            continue;
        selected = candidate;
        selected_index = index;
    }
    if (selected != NULL) selected->state = SCXML_COMPLETION_DATA_BOUND;
    if (out_index != NULL) *out_index = selected_index;
    return selected;
}

void scxml_runtime_clear_current_event_metadata(scxml_session_impl *session) {
    static const scxml_expr_string_view empty = {"", 0u};
    if (session->current_completion_data_slot != SIZE_MAX) {
        if (session->current_completion_data_slot <
            session->completion_data_capacity) {
            completion_data_release(
                &session->completion_data_slots[
                    session->current_completion_data_slot]);
        }
        session->current_completion_data_slot = SIZE_MAX;
    }
    if (session->current_event_data_object_live) {
        scxml_runtime_destroy_event_data_object(
            session->current_event_data_schema,
            session->current_event_data_object.bytes);
        session->current_event_data_object_live = false;
    }
    session->current_event_data_schema = NULL;
    session->system_values.event_data_schema = NULL;
    session->system_values.event_data_object = NULL;
    session->system_values.event_send_id = empty;
    session->system_values.event_origin = empty;
    session->system_values.event_origin_type = empty;
    session->system_values.event_invoke_id = empty;
    session->system_values.event_data = empty;
}

static bool bind_completion_done_data(
    scxml_session_impl *session,
    cflow_machine_state_id completion, const char **out_error) {
    size_t slot_index = SIZE_MAX;
    scxml_completion_data_slot *slot;
    if (session == NULL || out_error == NULL) {
        if (out_error != NULL)
            *out_error = "SCXML completion data binding is invalid";
        return false;
    }
    slot = completion_data_bind_oldest(
        session, completion, &slot_index);
    if (slot == NULL) return true;
    session->current_completion_data_slot = slot_index;
    if (slot->data_object_live) {
        if (!cmeta_data_desc_valid(slot->data_schema)) {
            *out_error = "SCXML completion data schema is invalid";
            return false;
        }
        session->current_event_data_schema = slot->data_schema;
        session->system_values.event_data =
            (scxml_expr_string_view){NULL, 0u};
        session->system_values.event_data_schema = slot->data_schema;
        session->system_values.event_data_object = slot->data_object.bytes;
    } else {
        if (slot->data_schema != NULL ||
            slot->data_size > SCXML_EVENT_METADATA_CAPACITY) {
            *out_error = "SCXML completion text data is invalid";
            return false;
        }
        session->system_values.event_data =
            (scxml_expr_string_view){slot->data, slot->data_size};
    }
    return true;
}

static bool scxml_runtime_observe_event(
    void *user, const scxml_evaluation_context *context,
    const cflow_statechart_observed_event *event, const char **out_error) {
    static const char external_type[] = "external";
    static const char internal_type[] = "internal";
    static const char platform_type[] = "platform";
    scxml_session_impl *session = (scxml_session_impl *)user;
    const scxml_program_name *name = NULL;
    size_t index;
    if (out_error != NULL) *out_error = NULL;
    if (session == NULL || event == NULL || out_error == NULL) {
        if (out_error != NULL) *out_error = "SCXML Event observation is invalid";
        return false;
    }
    scxml_runtime_clear_current_event_metadata(session);
    if (event->kind == CFLOW_STATECHART_OBSERVED_COMPLETION) {
        const scxml_program_name *state = find_state_program_name_by_id(
            session->program, event->completion);
        int written;
        if (state == NULL) {
            session->system_values.event_name =
                (scxml_expr_string_view){"", 0u};
            session->system_values.event_type =
                (scxml_expr_string_view){
                    internal_type, sizeof(internal_type) - 1u};
            return true;
        }
        if (state->size > sizeof(session->current_event_name) -
                              sizeof("done.state.")) {
            *out_error = "SCXML completion Event name exceeds metadata bound";
            return false;
        }
        written = snprintf(
            session->current_event_name,
            sizeof(session->current_event_name), "done.state.%.*s",
            (int)state->size, state->name);
        if (written < 0 ||
            (size_t)written >= sizeof(session->current_event_name)) {
            *out_error = "SCXML completion Event name exceeds metadata bound";
            return false;
        }
        session->system_values.event_name =
            (scxml_expr_string_view){
                session->current_event_name, (size_t)written};
        session->system_values.event_type =
            (scxml_expr_string_view){
                internal_type, sizeof(internal_type) - 1u};
        return bind_completion_done_data(
            session, event->completion, out_error);
    }
    if (event->event == NULL || event->event->id == 0u ||
        event->event->id > session->program->event_name_count) {
        *out_error = "SCXML observed Event is outside the program map";
        return false;
    }
    name = session->program->event_names_by_id[event->event->id - 1u];
    if (name == NULL) {
        *out_error = "SCXML observed Event name is unavailable";
        return false;
    }
    session->system_values.event_name =
        (scxml_expr_string_view){name->name, name->size};
    if (event->kind == CFLOW_STATECHART_OBSERVED_EXTERNAL) {
        session->system_values.event_type =
            (scxml_expr_string_view){
                external_type, sizeof(external_type) - 1u};
    } else if (event->event->id == session->program->execution_error_event ||
               event->event->id ==
                   session->program->communication_error_event) {
        session->system_values.event_type =
            (scxml_expr_string_view){
                platform_type, sizeof(platform_type) - 1u};
    } else {
        session->system_values.event_type =
            (scxml_expr_string_view){
                internal_type, sizeof(internal_type) - 1u};
    }

    if (event->origin_token == 0u ||
        (event->kind != CFLOW_STATECHART_OBSERVED_EXTERNAL &&
         (event->origin_token & SCXML_EXTERNAL_METADATA_TOKEN_BIT) == 0u))
        return true;
    turbo_mutex_lock(&session->registry_lock);
    if ((event->origin_token & SCXML_EXTERNAL_METADATA_TOKEN_BIT) != 0u) {
        scxml_external_event_metadata_row *row = NULL;
        bool row_data_live;
        for (index = 0u; index < session->external_metadata_capacity; ++index) {
            if (session->external_metadata_rows[index].in_use &&
                session->external_metadata_rows[index].token ==
                    event->origin_token) {
                row = &session->external_metadata_rows[index];
                break;
            }
        }
        if (row == NULL) {
            turbo_mutex_unlock(&session->registry_lock);
            *out_error = "SCXML external Event metadata token is stale";
            return false;
        }
#define SCXML_COPY_CURRENT(field)                                           \
        do {                                                                \
            if (row->field##_size != 0u)                                    \
                memcpy(session->current_event_##field, row->field,           \
                       row->field##_size);                                   \
            session->current_event_##field[row->field##_size] = '\0';       \
            session->system_values.event_##field =                          \
                (scxml_expr_string_view){                        \
                    session->current_event_##field, row->field##_size};      \
        } while (0)
        SCXML_COPY_CURRENT(send_id);
        SCXML_COPY_CURRENT(origin);
        SCXML_COPY_CURRENT(origin_type);
        SCXML_COPY_CURRENT(invoke_id);
        SCXML_COPY_CURRENT(data);
#undef SCXML_COPY_CURRENT
        row_data_live = row->data_object_live;
        if (!row_data_live) {
            memset(row, 0, sizeof(*row));
            turbo_mutex_unlock(&session->registry_lock);
            return true;
        }
        row->in_use = false;
        turbo_mutex_unlock(&session->registry_lock);
        if (!scxml_runtime_copy_event_data_object(
                row->data_schema,
                session->current_event_data_object.bytes,
                row->data_object.bytes)) {
            scxml_runtime_destroy_event_data_object(
                row->data_schema, row->data_object.bytes);
            turbo_mutex_lock(&session->registry_lock);
            memset(row, 0, sizeof(*row));
            turbo_mutex_unlock(&session->registry_lock);
            *out_error = "SCXML structured Event data copy failed";
            return false;
        }
        session->current_event_data_schema = row->data_schema;
        session->current_event_data_object_live = true;
        session->system_values.event_data =
            (scxml_expr_string_view){NULL, 0u};
        session->system_values.event_data_schema = row->data_schema;
        session->system_values.event_data_object =
            session->current_event_data_object.bytes;
        scxml_runtime_destroy_event_data_object(row->data_schema, row->data_object.bytes);
        turbo_mutex_lock(&session->registry_lock);
        memset(row, 0, sizeof(*row));
        turbo_mutex_unlock(&session->registry_lock);
        return true;
    } else {
        for (index = 0u; index < session->program->invocation_count; ++index) {
            if (session->invocation_rows[index].state ==
                    SCXML_INVOCATION_ACTIVE &&
                session->invocation_rows[index].token ==
                    event->origin_token) {
                static const char done_prefix[] = "done.invoke.";
                const scxml_invocation_descriptor *descriptor =
                    &session->program->invocations[index];
                const scxml_invocation_row *invocation =
                    &session->invocation_rows[index];
                if (invocation->id_size >
                    SCXML_EVENT_METADATA_CAPACITY) {
                    turbo_mutex_unlock(&session->registry_lock);
                    *out_error = "SCXML invoke ID exceeds metadata bound";
                    return false;
                }
                memcpy(session->current_event_invoke_id, invocation->id,
                       invocation->id_size);
                session->current_event_invoke_id[invocation->id_size] = '\0';
                session->system_values.event_invoke_id =
                    (scxml_expr_string_view){
                        session->current_event_invoke_id,
                        invocation->id_size};
                if (descriptor->has_id_location &&
                    event->event->id == descriptor->done_event) {
                    size_t dynamic_name_size;
                    if (!scxml_analyze_checked_add(
                            sizeof(done_prefix) - 1u,
                            invocation->id_size, &dynamic_name_size) ||
                        dynamic_name_size >
                            SCXML_EVENT_METADATA_CAPACITY) {
                        turbo_mutex_unlock(&session->registry_lock);
                        *out_error =
                            "SCXML dynamic done Event exceeds metadata bound";
                        return false;
                    }
                    memcpy(session->current_event_name, done_prefix,
                           sizeof(done_prefix) - 1u);
                    memcpy(session->current_event_name +
                               sizeof(done_prefix) - 1u,
                           invocation->id, invocation->id_size);
                    session->current_event_name[dynamic_name_size] = '\0';
                    session->system_values.event_name =
                        (scxml_expr_string_view){
                            session->current_event_name,
                            dynamic_name_size};
                }
                break;
            }
        }
    }
    turbo_mutex_unlock(&session->registry_lock);
    return true;
}

static cflow_statechart_host_result preprocess_invocation_external(
    scxml_session_impl *session, cflow_statechart_host_context *context,
    const cflow_event_view *event, uint64_t source_token,
    const char **out_error) {
    const scxml_invocation_descriptor *source = NULL;
    scxml_evaluation_context evaluation;
    size_t source_index = SIZE_MAX;
    size_t skipped_invocation = SIZE_MAX;
    size_t index;
    if (out_error != NULL) *out_error = NULL;
    if (session == NULL || context == NULL || event == NULL ||
        out_error == NULL) {
        if (out_error != NULL)
            *out_error = "SCXML invocation preprocess context is invalid";
        return CFLOW_STATECHART_HOST_FATAL;
    }
    evaluation = host_evaluation_context(
        context, cflow_statechart_host_context_state(context));
    if (source_token != 0u &&
        (source_token & SCXML_EXTERNAL_METADATA_TOKEN_BIT) == 0u) {
        turbo_mutex_lock(&session->registry_lock);
        for (index = 0u; index < session->program->invocation_count; ++index) {
            const scxml_invocation_row *row =
                &session->invocation_rows[index];
            if (row->state == SCXML_INVOCATION_ACTIVE &&
                row->token == source_token) {
                source_index = index;
                break;
            }
        }
        if (source_index == SIZE_MAX) {
            scxml_runtime_increment_u64(&session->invoke_stats.returned_rejected);
            turbo_mutex_unlock(&session->registry_lock);
            return CFLOW_STATECHART_HOST_DROP;
        }
        turbo_mutex_unlock(&session->registry_lock);
        source = &session->program->invocations[source_index];
        if (!execute_invocation_finalize(
                source, session, context, out_error))
            return CFLOW_STATECHART_HOST_FATAL;
        if (event->id == source->done_event) {
            if (!stage_invocation_completion(
                    session, source_index, source_token, context,
                    out_error))
                return CFLOW_STATECHART_HOST_FATAL;
            skipped_invocation = source_index;
        }
    }
    if (!forward_external_to_invocations(
            session, &evaluation, context, event, skipped_invocation,
            out_error))
        return CFLOW_STATECHART_HOST_FATAL;
    return CFLOW_STATECHART_HOST_CONTINUE;
}

static bool has_pending_active_invocation(
    scxml_session_impl *session,
    cflow_statechart_host_context *context) {
    size_t index;
    for (index = 0u; index < session->program->invocation_count; ++index) {
        bool pending;
        turbo_mutex_lock(&session->registry_lock);
        pending = session->invocation_rows[index].state ==
            SCXML_INVOCATION_PENDING;
        turbo_mutex_unlock(&session->registry_lock);
        if (pending && cflow_statechart_host_context_is_active(
                           context,
                           session->program->invocations[index].owner))
            return true;
    }
    return false;
}

static cflow_statechart_host_result prepare_invocation_quiescence(
    scxml_session_impl *session, cflow_statechart_host_context *context,
    const char **out_error) {
    if (!has_pending_active_invocation(session, context))
        return CFLOW_STATECHART_HOST_CONTINUE;
    return scxml_runtime_start_pending_invocations(
        session, context, out_error);
}

cflow_statechart_host_result scxml_runtime_host_transaction(
    void *user, cflow_statechart_host_context *context,
    const char **out_error) {
    scxml_session_impl *session = (scxml_session_impl *)user;
    cflow_statechart_host_phase phase;
    if (out_error != NULL) *out_error = NULL;
    if (session == NULL || context == NULL || out_error == NULL) {
        if (out_error != NULL)
            *out_error = "SCXML host transaction is invalid";
        return CFLOW_STATECHART_HOST_FATAL;
    }
    phase = cflow_statechart_host_context_phase(context);
    if (phase == CFLOW_STATECHART_HOST_PREPARE_TRIGGER) {
        const cflow_statechart_observed_event *trigger =
            cflow_statechart_host_context_trigger(context);
        scxml_evaluation_context evaluation;
        if (trigger == NULL) {
            *out_error = "SCXML host trigger is unavailable";
            return CFLOW_STATECHART_HOST_FATAL;
        }
        evaluation = host_evaluation_context(
            context, cflow_statechart_host_context_state(context));
        if (!scxml_runtime_observe_event(
                session, &evaluation, trigger, out_error))
            return CFLOW_STATECHART_HOST_FATAL;
        if (session->has_invoke &&
            trigger->kind == CFLOW_STATECHART_OBSERVED_EXTERNAL) {
            return preprocess_invocation_external(
                session, context, trigger->event,
                trigger->origin_token, out_error);
        }
        return CFLOW_STATECHART_HOST_CONTINUE;
    }
    if (phase == CFLOW_STATECHART_HOST_PREPARE_QUIESCENCE) {
        return session->has_invoke
            ? prepare_invocation_quiescence(session, context, out_error)
            : CFLOW_STATECHART_HOST_CONTINUE;
    }
    *out_error = "SCXML host transaction phase is invalid";
    return CFLOW_STATECHART_HOST_FATAL;
}

static scxml_execute_outcome raise_adapter_error(
    scxml_session_impl *session,
    const cflow_statechart_executable_context *context,
    scxml_adapter_error_kind kind, const char **out_error) {
    const bool null_value = false;
    const cflow_event_id event =
        kind == SCXML_ADAPTER_ERROR_KIND_EXECUTION
            ? session->program->execution_error_event
            : session->program->communication_error_event;
    const cflow_event_view raised = {
        event, &cmeta_type_bool, &null_value};
    if (event == 0u ||
        !context->raise_internal(context->raise_user, &raised, out_error)) {
        if (event == 0u && out_error != NULL)
            *out_error = "SCXML reserved adapter error event is unavailable";
        return SCXML_EXECUTE_FATAL;
    }
    return SCXML_EXECUTE_BLOCK_ABORTED;
}

static scxml_execute_outcome adapter_failure_outcome(
    scxml_session_impl *session,
    const cflow_statechart_executable_context *context,
    scxml_adapter_status status, const char *adapter_error,
    const char **out_error) {
    switch (status) {
        case SCXML_ADAPTER_ERROR_EXECUTION:
            return raise_adapter_error(
                session, context, SCXML_ADAPTER_ERROR_KIND_EXECUTION,
                out_error);
        case SCXML_ADAPTER_ERROR_COMMUNICATION:
        case SCXML_ADAPTER_FULL:
        case SCXML_ADAPTER_CLOSED:
            return raise_adapter_error(
                session, context, SCXML_ADAPTER_ERROR_KIND_COMMUNICATION,
                out_error);
        case SCXML_ADAPTER_INVALID_CONTRACT:
        case SCXML_ADAPTER_ACCEPTED:
            *out_error = adapter_error != NULL && adapter_error[0] != '\0'
                ? adapter_error : "SCXML Event I/O adapter contract violation";
            return SCXML_EXECUTE_FATAL;
    }
    *out_error = "SCXML Event I/O adapter returned an unknown status";
    return SCXML_EXECUTE_FATAL;
}

static void commit_event_metadata(void *user);

static scxml_execute_outcome send_failure_outcome(
    scxml_session_impl *session,
    const scxml_effect_descriptor *descriptor,
    const scxml_send_request *request,
    const cflow_statechart_executable_context *context,
    scxml_adapter_status status, const char *adapter_error,
    const char **out_error) {
    const bool null_value = false;
    scxml_adapter_error_kind kind;
    cflow_event_id event;
    cflow_event_view raised;
    scxml_event_metadata metadata = {0};
    scxml_external_event_metadata_row *metadata_row;
    cflow_statechart_effect_ticket metadata_ticket;
    uint64_t token = 0u;

    switch (status) {
        case SCXML_ADAPTER_ERROR_EXECUTION:
            kind = SCXML_ADAPTER_ERROR_KIND_EXECUTION;
            break;
        case SCXML_ADAPTER_ERROR_COMMUNICATION:
        case SCXML_ADAPTER_FULL:
        case SCXML_ADAPTER_CLOSED:
            kind = SCXML_ADAPTER_ERROR_KIND_COMMUNICATION;
            break;
        case SCXML_ADAPTER_INVALID_CONTRACT:
        case SCXML_ADAPTER_ACCEPTED:
        default:
            return adapter_failure_outcome(
                session, context, status, adapter_error, out_error);
    }
    if (request == NULL || request->id_size == 0u)
        return raise_adapter_error(session, context, kind, out_error);
    if (session == NULL || descriptor == NULL || request->id == NULL ||
        context->raise_internal_tagged == NULL ||
        context->stage_effect == NULL) {
        *out_error = "SCXML failed send metadata context is invalid";
        return SCXML_EXECUTE_FATAL;
    }

    event = kind == SCXML_ADAPTER_ERROR_KIND_EXECUTION
        ? session->program->execution_error_event
        : session->program->communication_error_event;
    if (event == 0u) {
        *out_error = "SCXML reserved adapter error event is unavailable";
        return SCXML_EXECUTE_FATAL;
    }
    metadata.abi_version = SCXML_EVENT_METADATA_ABI;
    metadata.struct_size = sizeof(metadata);
    metadata.send_id = request->id;
    metadata.send_id_size = request->id_size;
    metadata_row = scxml_runtime_reserve_event_metadata(
        session, &metadata, &token);
    if (metadata_row == NULL) {
        *out_error = "SCXML failed send metadata capacity is exhausted";
        return SCXML_EXECUTE_FATAL;
    }
    raised = (cflow_event_view){event, &cmeta_type_bool, &null_value};
    if (!context->raise_internal_tagged(
            context->raise_user, &raised, token, out_error)) {
        scxml_runtime_release_event_metadata(metadata_row);
        return SCXML_EXECUTE_FATAL;
    }
    metadata_ticket = (cflow_statechart_effect_ticket){
        commit_event_metadata, scxml_runtime_release_event_metadata,
        metadata_row};
    if (!context->stage_effect(
            context->effect_user, &metadata_ticket, out_error)) {
        scxml_runtime_release_event_metadata(metadata_row);
        return SCXML_EXECUTE_FATAL;
    }
    if (descriptor->has_id_location) {
        session->failed_send_id_location = descriptor->id_location;
        session->failed_send_id_size = request->id_size;
        memcpy(session->failed_send_id, request->id, request->id_size);
        session->failed_send_id[request->id_size] = '\0';
        session->failed_send_id_restore_live = true;
    }
    return SCXML_EXECUTE_BLOCK_ABORTED;
}

static bool evaluate_cmeta_executable_active(
    void *user, cflow_machine_state_id state, bool *out_active);

bool scxml_runtime_metadata_field_valid(const char *data, size_t size) {
    return size <= SCXML_EVENT_METADATA_CAPACITY &&
        (size == 0u || data != NULL);
}

scxml_external_event_metadata_row *scxml_runtime_reserve_event_metadata(
    scxml_session_impl *session,
    const scxml_event_metadata *metadata,
    uint64_t *out_token) {
    scxml_external_event_metadata_row *row = NULL;
    uint64_t token;
    size_t index;
    if (session == NULL || metadata == NULL || out_token == NULL ||
        !scxml_runtime_metadata_field_valid(metadata->send_id,
                                    metadata->send_id_size) ||
        !scxml_runtime_metadata_field_valid(metadata->origin,
                                    metadata->origin_size) ||
        !scxml_runtime_metadata_field_valid(metadata->origin_type,
                                    metadata->origin_type_size) ||
        !scxml_runtime_metadata_field_valid(metadata->invoke_id,
                                    metadata->invoke_id_size))
        return NULL;
    turbo_mutex_lock(&session->registry_lock);
    for (index = 0u; index < session->external_metadata_capacity; ++index) {
        if (!session->external_metadata_rows[index].in_use &&
            !session->external_metadata_rows[index].data_object_live) {
            row = &session->external_metadata_rows[index];
            break;
        }
    }
    if (row == NULL) {
        turbo_mutex_unlock(&session->registry_lock);
        return NULL;
    }
    token = session->next_external_metadata_token;
    session->next_external_metadata_token =
        SCXML_EXTERNAL_METADATA_TOKEN_BIT |
        (((token & ~SCXML_EXTERNAL_METADATA_TOKEN_BIT) + 1u) &
         ~SCXML_EXTERNAL_METADATA_TOKEN_BIT);
    if (session->next_external_metadata_token ==
        SCXML_EXTERNAL_METADATA_TOKEN_BIT)
        session->next_external_metadata_token |= UINT64_C(1);
    row->session = session;
    row->token = token;
    row->in_use = true;
#define SCXML_RETAIN_METADATA(field)                                      \
    do {                                                                  \
        row->field##_size = metadata->field##_size;                       \
        if (metadata->field##_size != 0u)                                 \
            memcpy(row->field, metadata->field, metadata->field##_size);  \
        row->field[metadata->field##_size] = '\0';                        \
    } while (0)
    SCXML_RETAIN_METADATA(send_id);
    SCXML_RETAIN_METADATA(origin);
    SCXML_RETAIN_METADATA(origin_type);
    SCXML_RETAIN_METADATA(invoke_id);
#undef SCXML_RETAIN_METADATA
    turbo_mutex_unlock(&session->registry_lock);
    *out_token = token;
    return row;
}

void scxml_runtime_release_event_metadata(void *user) {
    scxml_external_event_metadata_row *row =
        (scxml_external_event_metadata_row *)user;
    scxml_session_impl *session = row != NULL ? row->session : NULL;
    if (session == NULL) return;
    bool data_live;
    const cmeta_data_desc *schema;
    turbo_mutex_lock(&session->registry_lock);
    if (!row->in_use) {
        turbo_mutex_unlock(&session->registry_lock);
        return;
    }
    data_live = row->data_object_live;
    if (!data_live) {
        memset(row, 0, sizeof(*row));
        turbo_mutex_unlock(&session->registry_lock);
        return;
    }
    row->in_use = false;
    schema = row->data_schema;
    turbo_mutex_unlock(&session->registry_lock);
    scxml_runtime_destroy_event_data_object(schema, row->data_object.bytes);
    turbo_mutex_lock(&session->registry_lock);
    memset(row, 0, sizeof(*row));
    turbo_mutex_unlock(&session->registry_lock);
}

static void commit_event_metadata(void *user) {
    (void)user;
}

static scxml_execute_outcome raise_block_execution_error(
    const scxml_block *block,
    const cflow_statechart_executable_context *context,
    const char **out_error);

static bool evaluate_effect_value(
    const scxml_expr_program *program,
    const cflow_statechart_executable_context *context,
    const scxml_expr_system_values *system_values,
    scxml_expr_value *out) {
    scxml_expr_diagnostic diagnostic = {0};
    return scxml_expr_evaluate_value_with_system(
               program, context->out_state,
               evaluate_cmeta_executable_active, (void *)context,
               system_values, out, &diagnostic) ==
           SCXML_EXPR_OK;
}

bool scxml_runtime_payload_value_from_cmeta(
    const scxml_expr_value *source,
    scxml_payload_value *destination) {
    if (source == NULL || destination == NULL) return false;
    memset(destination, 0, sizeof(*destination));
    switch (source->kind) {
        case SCXML_EXPR_VALUE_BOOL:
            destination->kind = SCXML_PAYLOAD_VALUE_BOOL;
            destination->data.boolean = source->data.boolean;
            return true;
        case SCXML_EXPR_VALUE_SINT:
            destination->kind = SCXML_PAYLOAD_VALUE_SINT;
            destination->data.sint = source->data.sint;
            return true;
        case SCXML_EXPR_VALUE_UINT:
            destination->kind = SCXML_PAYLOAD_VALUE_UINT;
            destination->data.uint = source->data.uint;
            return true;
        case SCXML_EXPR_VALUE_FLOAT:
            destination->kind = SCXML_PAYLOAD_VALUE_FLOAT;
            destination->data.number = source->data.number;
            return true;
        case SCXML_EXPR_VALUE_STRING:
            destination->kind = SCXML_PAYLOAD_VALUE_STRING;
            destination->data.string.data = source->data.string.data;
            destination->data.string.size = source->data.string.size;
            return source->data.string.data != NULL ||
                   source->data.string.size == 0u;
        default: return false;
    }
}

static bool materialize_effect_named_payload(
    const scxml_block *block, scxml_session_impl *session,
    const scxml_effect_descriptor *effect,
    const cflow_statechart_executable_context *context,
    const scxml_expr_system_values *system_values,
    scxml_payload_view *out) {
    size_t index;
    if (block == NULL || session == NULL || effect == NULL ||
        context == NULL || out == NULL ||
        effect->payload_first > block->payload_storage_count ||
        effect->payload_count >
            block->payload_storage_count - effect->payload_first ||
        effect->payload_count > session->payload_scratch_capacity ||
        (effect->payload_count != 0u &&
         (block->payloads == NULL || session->payload_scratch == NULL)))
        return false;
    *out = (scxml_payload_view){0};
    if (effect->payload_count == 0u) return true;
    for (index = 0u; index < effect->payload_count; ++index) {
        const scxml_payload_descriptor *descriptor =
            &block->payloads[effect->payload_first + index];
        scxml_expr_value value = {0};
        scxml_payload_value scalar = {0};
        scxml_payload_entry *entry =
            &session->payload_scratch[index];
        if (!evaluate_effect_value(
                &descriptor->expression, context, system_values, &value) ||
            !scxml_runtime_payload_value_from_cmeta(&value, &scalar))
            return false;
        entry->name = descriptor->name;
        entry->name_size = descriptor->name_size;
        entry->value = (scxml_content_view){
            .kind = SCXML_CONTENT_SCALAR,
            .scalar = scalar};
    }
    out->kind = SCXML_PAYLOAD_NAMED;
    out->entries = session->payload_scratch;
    out->entry_count = effect->payload_count;
    return true;
}

bool scxml_runtime_materialize_content_descriptor(
    const scxml_content_descriptor *descriptor, const void *state,
    const scxml_expr_system_values *system_values,
    scxml_content_view *out) {
    if (descriptor == NULL || out == NULL) return false;
    *out = (scxml_content_view){0};
    if (descriptor->kind == SCXML_CONTENT_TEXT_UTF8 ||
        descriptor->kind == SCXML_CONTENT_XML_UTF8) {
        if (descriptor->byte_count != 0u && descriptor->bytes == NULL)
            return false;
        out->kind = descriptor->kind;
        out->bytes = descriptor->bytes;
        out->byte_count = descriptor->byte_count;
        return true;
    }
    if (descriptor->kind == SCXML_CONTENT_CMETA) {
        if (state == NULL || descriptor->location.value == NULL)
            return false;
        if (scxml_expr_require_data_bound(
                system_values, descriptor->location.offset,
                descriptor->location.storage_size, NULL) != SCXML_EXPR_OK)
            return false;
        out->kind = SCXML_CONTENT_CMETA;
        out->schema = descriptor->location.value;
        out->object = (const unsigned char *)state +
                      descriptor->location.offset;
        return true;
    }
    return false;
}

bool scxml_runtime_scalar_value_to_text(
    const scxml_expr_value *value, char *storage,
    size_t capacity, const char **out_data, size_t *out_size) {
    int written;
    if (value == NULL || storage == NULL || capacity == 0u ||
        out_data == NULL || out_size == NULL)
        return false;
    if (value->kind == SCXML_EXPR_VALUE_STRING) {
        if (value->data.string.size > SCXML_EVENT_METADATA_CAPACITY)
            return false;
        *out_data = value->data.string.data;
        *out_size = value->data.string.size;
        return true;
    }
    if (value->kind == SCXML_EXPR_VALUE_BOOL)
        written = snprintf(storage, capacity, "%s",
                           value->data.boolean ? "true" : "false");
    else if (value->kind == SCXML_EXPR_VALUE_SINT)
        written = snprintf(storage, capacity, "%lld",
                           (long long)value->data.sint);
    else if (value->kind == SCXML_EXPR_VALUE_UINT)
        written = snprintf(storage, capacity, "%llu",
                           (unsigned long long)value->data.uint);
    else if (value->kind == SCXML_EXPR_VALUE_FLOAT)
        written = snprintf(storage, capacity, "%.17g", value->data.number);
    else
        return false;
    if (written < 0 || (size_t)written >= capacity ||
        (size_t)written > SCXML_EVENT_METADATA_CAPACITY)
        return false;
    *out_data = storage;
    *out_size = (size_t)written;
    return true;
}

static bool materialize_send_id_location(
    scxml_session_impl *session,
    const scxml_effect_descriptor *descriptor, void *staged_state,
    const scxml_expr_system_values *system_values,
    scxml_send_request *request, char *storage, size_t capacity) {
    scxml_expr_diagnostic diagnostic = {0};
    uint64_t token;
    int written;
    if (!descriptor->has_id_location) return true;
    if (session == NULL || staged_state == NULL || request == NULL ||
        storage == NULL || capacity == 0u)
        return false;
    if (scxml_expr_require_data_bound(
            system_values, descriptor->id_location.offset,
            descriptor->id_location.storage_size, &diagnostic) !=
        SCXML_EXPR_OK)
        return false;
    token = session->next_send_token;
    if (token == 0u) return false;
    written = snprintf(
        storage, capacity, "send.%s.%" PRIu64, session->session_id, token);
    if (written < 0 || (size_t)written >= capacity) return false;
    session->next_send_token = token == UINT64_MAX ? 0u : token + 1u;
    if (scxml_location_assign_owned_string(
            &descriptor->id_location, staged_state, storage,
            (size_t)written, SCXML_EVENT_METADATA_CAPACITY,
            &diagnostic) != SCXML_EXPR_OK)
        return false;
    request->id = storage;
    request->id_size = (size_t)written;
    return true;
}

static scxml_execute_outcome execute_send(
    const scxml_block *block,
    scxml_session_impl *session,
    const scxml_effect_descriptor *descriptor,
    const cflow_statechart_executable_context *context,
    const scxml_expr_system_values *system_values,
    const char **out_error) {
    static const char default_type[] =
        "http://www.w3.org/TR/scxml/#SCXMLEventProcessor";
    const bool null_value = false;
    scxml_send_request materialized = descriptor->request.send;
    const scxml_send_request *request = &materialized;
    scxml_payload_view payload = {0};
    scxml_content_view content = {0};
    scxml_expr_value value = {0};
    scxml_event_metadata metadata = {
        .abi_version = SCXML_EVENT_METADATA_ABI,
        .struct_size = sizeof(metadata)};
    char id_storage[SCXML_EVENT_METADATA_CAPACITY + 1u];
    const scxml_program_name *dynamic_event = NULL;
    bool internal_target = descriptor->internal_target;
    cflow_statechart_effect_ticket adapter_ticket = {0};
    cflow_statechart_effect_ticket runtime_ticket;
    scxml_prepared_effect *prepared;
    scxml_delayed_send *delayed = NULL;
    const char *adapter_error = NULL;
    scxml_adapter_status status;
    size_t registry_index = SIZE_MAX;
    bool duplicate = false;
    if (!descriptor->has_type_expr && materialized.type_size == 0u) {
        materialized.type = default_type;
        materialized.type_size = sizeof(default_type) - 1u;
    }
    if (descriptor->has_event_expr) {
        if (session == NULL || !evaluate_effect_value(
                &descriptor->event_expr, context, system_values, &value) ||
            value.kind != SCXML_EXPR_VALUE_STRING ||
            !scxml_analyze_is_xml_nmtoken((turbo_xml_string_view){
                value.data.string.data, value.data.string.size})) {
            return raise_block_execution_error(block, context, out_error);
        }
        dynamic_event = scxml_program_find_name(
            session->program->event_names, session->program->event_name_count,
            value.data.string.data, value.data.string.size);
        if (dynamic_event == NULL)
            return raise_block_execution_error(block, context, out_error);
        materialized.event = value.data.string.data;
        materialized.event_size = value.data.string.size;
    }
    if (descriptor->has_target_expr) {
        if (!evaluate_effect_value(
                &descriptor->target_expr, context, system_values, &value) ||
            value.kind != SCXML_EXPR_VALUE_STRING)
            return raise_block_execution_error(block, context, out_error);
        materialized.target = value.data.string.data;
        materialized.target_size = value.data.string.size;
        internal_target =
            (value.data.string.size == sizeof("#_internal") - 1u &&
             memcmp(value.data.string.data, "#_internal",
                    sizeof("#_internal") - 1u) == 0) ||
            (value.data.string.size == sizeof("_internal") - 1u &&
             memcmp(value.data.string.data, "_internal",
                    sizeof("_internal") - 1u) == 0);
    }
    if (descriptor->has_type_expr) {
        if (!evaluate_effect_value(
                &descriptor->type_expr, context, system_values, &value) ||
            value.kind != SCXML_EXPR_VALUE_STRING ||
            value.data.string.size == 0u)
            return raise_block_execution_error(block, context, out_error);
        materialized.type = value.data.string.data;
        materialized.type_size = value.data.string.size;
    }
    if (descriptor->has_delay_expr) {
        if (!evaluate_effect_value(
                &descriptor->delay_expr, context, system_values, &value) ||
            (value.kind == SCXML_EXPR_VALUE_SINT &&
             value.data.sint < 0))
            return raise_block_execution_error(block, context, out_error);
        materialized.delay_ms =
            value.kind == SCXML_EXPR_VALUE_UINT
                ? value.data.uint : (uint64_t)value.data.sint;
    }
    if (descriptor->content.kind == SCXML_CONTENT_SCALAR) {
        scxml_payload_value scalar = {0};
        if (!evaluate_effect_value(
                &descriptor->data_expr, context, system_values, &value) ||
            !scxml_runtime_payload_value_from_cmeta(&value, &scalar))
            return raise_block_execution_error(block, context, out_error);
        content = (scxml_content_view){
            .kind = SCXML_CONTENT_SCALAR,
            .scalar = scalar};
    } else if (descriptor->content.kind != SCXML_CONTENT_INVALID) {
        if (!scxml_runtime_materialize_content_descriptor(
                &descriptor->content, context->out_state,
                system_values, &content))
            return raise_block_execution_error(block, context, out_error);
    }
    if (content.kind != SCXML_CONTENT_INVALID) {
        if (internal_target)
            metadata.data = content;
        else {
            payload.kind = SCXML_PAYLOAD_CONTENT;
            payload.content = content;
        }
    }
    if (descriptor->payload_count != 0u) {
        if (internal_target ||
            !materialize_effect_named_payload(
                block, session, descriptor, context, system_values,
                &payload))
            return raise_block_execution_error(block, context, out_error);
    }
    if (descriptor->content.kind != SCXML_CONTENT_INVALID &&
        internal_target &&
        materialized.delay_ms != 0u)
        return raise_block_execution_error(block, context, out_error);
    if (!materialize_send_id_location(
            session, descriptor, context->out_state, system_values,
            &materialized,
            id_storage, sizeof(id_storage)))
        return raise_block_execution_error(block, context, out_error);
    if (internal_target && request->delay_ms == 0u) {
        const cflow_event_id event_id = dynamic_event != NULL
            ? (cflow_event_id)dynamic_event->id : descriptor->event_id;
        const cflow_event_view raised = {
            event_id, &cmeta_type_bool, &null_value};
        if (descriptor->content.kind == SCXML_CONTENT_INVALID)
            return context->raise_internal(
                       context->raise_user, &raised, out_error)
                ? SCXML_EXECUTE_CONTINUE : SCXML_EXECUTE_FATAL;
        if (session == NULL || context->raise_internal_tagged == NULL ||
            context->stage_effect == NULL) {
            *out_error = "SCXML payload send requires tagged internal Events";
            return SCXML_EXECUTE_FATAL;
        }
        {
            uint64_t token = 0u;
            scxml_external_event_metadata_row *metadata_row =
                scxml_runtime_reserve_event_metadata(session, &metadata, &token);
            cflow_statechart_effect_ticket metadata_ticket;
            if (metadata_row == NULL)
                return raise_block_execution_error(block, context, out_error);
            if (metadata.data.kind != SCXML_CONTENT_INVALID &&
                !scxml_analyze_attach_event_content(
                    session, metadata_row, &metadata.data)) {
                scxml_runtime_release_event_metadata(metadata_row);
                return raise_block_execution_error(
                    block, context, out_error);
            }
            if (!context->raise_internal_tagged(
                    context->raise_user, &raised, token, out_error)) {
                scxml_runtime_release_event_metadata(metadata_row);
                return SCXML_EXECUTE_FATAL;
            }
            metadata_ticket = (cflow_statechart_effect_ticket){
                commit_event_metadata, scxml_runtime_release_event_metadata, metadata_row};
            if (!context->stage_effect(
                    context->effect_user, &metadata_ticket, out_error)) {
                scxml_runtime_release_event_metadata(metadata_row);
                return SCXML_EXECUTE_FATAL;
            }
        }
        return SCXML_EXECUTE_CONTINUE;
    }
    if (session == NULL || !session->has_event_io ||
        context->stage_effect == NULL) {
        *out_error = "SCXML send requires an owning Event I/O session";
        return SCXML_EXECUTE_FATAL;
    }
    materialized.payload = payload;
    turbo_mutex_lock(&session->registry_lock);
    prepared = acquire_prepared_effect_locked(session);
    if (prepared != NULL && request->delay_ms != 0u) {
        delayed = reserve_delayed_send_locked(
            session, request->id, request->id_size,
            descriptor->has_id_location, &registry_index, &duplicate);
    }
    if (prepared == NULL || (request->delay_ms != 0u && delayed == NULL)) {
        if (prepared != NULL) prepared->in_use = false;
        turbo_mutex_unlock(&session->registry_lock);
        return raise_adapter_error(
            session, context,
            duplicate ? SCXML_ADAPTER_ERROR_KIND_EXECUTION
                      : SCXML_ADAPTER_ERROR_KIND_COMMUNICATION,
            out_error);
    }
    prepared->kind = request->delay_ms != 0u
        ? SCXML_PREPARED_DELAYED_SEND : SCXML_PREPARED_SEND;
    prepared->registry_index = registry_index;
    turbo_mutex_unlock(&session->registry_lock);

    status = session->event_io.prepare_send(
        session->adapter_user, request, &adapter_ticket, &adapter_error);
    if (status != SCXML_ADAPTER_ACCEPTED) {
        turbo_mutex_lock(&session->registry_lock);
        rollback_prepared_effect_locked(prepared);
        turbo_mutex_unlock(&session->registry_lock);
        return send_failure_outcome(
            session, descriptor, request, context, status, adapter_error,
            out_error);
    }
    if (adapter_ticket.commit == NULL || adapter_ticket.discard == NULL) {
        turbo_mutex_lock(&session->registry_lock);
        rollback_prepared_effect_locked(prepared);
        turbo_mutex_unlock(&session->registry_lock);
        *out_error = "SCXML Event I/O adapter returned an invalid ticket";
        return SCXML_EXECUTE_FATAL;
    }
    prepared->adapter_ticket = adapter_ticket;
    runtime_ticket = (cflow_statechart_effect_ticket){
        commit_prepared_effect, discard_prepared_effect, prepared};
    if (!context->stage_effect(
            context->effect_user, &runtime_ticket, out_error)) {
        discard_prepared_effect(prepared);
        return SCXML_EXECUTE_FATAL;
    }
    return SCXML_EXECUTE_CONTINUE;
}

static scxml_execute_outcome execute_cancel(
    const scxml_block *block,
    scxml_session_impl *session,
    const scxml_effect_descriptor *descriptor,
    const cflow_statechart_executable_context *context,
    const scxml_expr_system_values *system_values,
    const char **out_error) {
    scxml_cancel_request materialized = descriptor->request.cancel;
    const scxml_cancel_request *request = &materialized;
    scxml_expr_value value = {0};
    cflow_statechart_effect_ticket adapter_ticket = {0};
    cflow_statechart_effect_ticket runtime_ticket;
    scxml_prepared_effect *prepared;
    scxml_delayed_send *delayed;
    const char *adapter_error = NULL;
    scxml_adapter_status status;
    size_t registry_index = SIZE_MAX;
    if (descriptor->has_send_id_expr) {
        if (!evaluate_effect_value(
                &descriptor->send_id_expr, context, system_values, &value) ||
            value.kind != SCXML_EXPR_VALUE_STRING ||
            value.data.string.size == 0u)
            return raise_block_execution_error(block, context, out_error);
        materialized.send_id = value.data.string.data;
        materialized.send_id_size = value.data.string.size;
    }
    if (session == NULL || !session->has_event_io ||
        context->stage_effect == NULL) {
        *out_error = "SCXML cancel requires an owning Event I/O session";
        return SCXML_EXECUTE_FATAL;
    }
    turbo_mutex_lock(&session->registry_lock);
    delayed = scxml_runtime_find_delayed_send_locked(
        session, request->send_id, request->send_id_size, &registry_index);
    if (delayed == NULL) {
        turbo_mutex_unlock(&session->registry_lock);
        return SCXML_EXECUTE_CONTINUE;
    }
    prepared = acquire_prepared_effect_locked(session);
    if (prepared == NULL) {
        turbo_mutex_unlock(&session->registry_lock);
        return raise_adapter_error(
            session, context, SCXML_ADAPTER_ERROR_KIND_COMMUNICATION,
            out_error);
    }
    prepared->kind = SCXML_PREPARED_CANCEL;
    prepared->registry_index = registry_index;
    delayed->previous_state = delayed->state;
    delayed->state = SCXML_DELAYED_CANCEL_RESERVED;
    turbo_mutex_unlock(&session->registry_lock);

    status = session->event_io.prepare_cancel(
        session->adapter_user, request, &adapter_ticket, &adapter_error);
    if (status != SCXML_ADAPTER_ACCEPTED) {
        turbo_mutex_lock(&session->registry_lock);
        rollback_prepared_effect_locked(prepared);
        turbo_mutex_unlock(&session->registry_lock);
        return adapter_failure_outcome(
            session, context, status, adapter_error, out_error);
    }
    if (adapter_ticket.commit == NULL || adapter_ticket.discard == NULL) {
        turbo_mutex_lock(&session->registry_lock);
        rollback_prepared_effect_locked(prepared);
        turbo_mutex_unlock(&session->registry_lock);
        *out_error = "SCXML Event I/O adapter returned an invalid ticket";
        return SCXML_EXECUTE_FATAL;
    }
    prepared->adapter_ticket = adapter_ticket;
    runtime_ticket = (cflow_statechart_effect_ticket){
        commit_prepared_effect, discard_prepared_effect, prepared};
    if (!context->stage_effect(
            context->effect_user, &runtime_ticket, out_error)) {
        discard_prepared_effect(prepared);
        return SCXML_EXECUTE_FATAL;
    }
    return SCXML_EXECUTE_CONTINUE;
}

static bool evaluate_cmeta_executable_active(
    void *user, cflow_machine_state_id state, bool *out_active) {
    const cflow_statechart_executable_context *context =
        (const cflow_statechart_executable_context *)user;
    if (context == NULL || context->is_active == NULL ||
        context->configuration_user == NULL || out_active == NULL)
        return false;
    *out_active = context->is_active(context->configuration_user, state);
    return true;
}

static bool evaluate_cmeta_initializer_active(
    void *user, cflow_machine_state_id state, bool *out_active) {
    (void)user;
    (void)state;
    if (out_active == NULL) return false;
    *out_active = false;
    return true;
}

static scxml_execute_outcome raise_block_execution_error(
    const scxml_block *block,
    const cflow_statechart_executable_context *context,
    const char **out_error) {
    const bool null_value = false;
    const cflow_event_view raised = {
        block->execution_error_event, &cmeta_type_bool, &null_value};
    if (block->execution_error_event == 0u ||
        !context->raise_internal(context->raise_user, &raised, out_error)) {
        if (block->execution_error_event == 0u)
            *out_error = "SCXML execution error event is unavailable";
        return SCXML_EXECUTE_FATAL;
    }
    return SCXML_EXECUTE_BLOCK_ABORTED;
}

static scxml_expr_status apply_external_data_initializer(
    const scxml_assign_program *assignment,
    scxml_session_impl *session, void *state,
    scxml_expr_diagnostic *diagnostic) {
    const cmeta_data_desc *destination = NULL;
    const char *uri = NULL;
    size_t uri_size = 0u;
    scxml_data_resource resource = {0};
    scxml_resource_status resource_status;
    scxml_expr_status status;
    if (assignment == NULL || session == NULL || state == NULL ||
        !session->has_data_resources ||
        !scxml_assign_external_source(
            assignment, &uri, &uri_size, &destination) ||
        uri == NULL || uri_size == 0u || destination == NULL ||
        session->data_resources.open == NULL ||
        session->data_resources.close == NULL ||
        session->data_decode_storage == NULL ||
        session->data_decode_storage_size == 0u)
        return SCXML_EXPR_INVALID_ARGUMENT;
    resource_status = session->data_resources.open(
        session->data_resource_user, uri, uri_size, destination, &resource);
    if (resource_status != SCXML_RESOURCE_OK)
        return resource_status == SCXML_RESOURCE_LIMIT_EXCEEDED
            ? SCXML_EXPR_LIMIT_EXCEEDED : SCXML_EXPR_EVALUATION_ERROR;
    if (resource.reader.state != CSERDE_READER_READY ||
        resource.reader.ops == NULL) {
        session->data_resources.close(
            session->data_resource_user, &resource);
        return SCXML_EXPR_EVALUATION_ERROR;
    }
    status = scxml_assign_apply_external(
        assignment, &resource.reader, &session->cbind,
        session->data_decode_storage, session->data_decode_storage_size,
        state, diagnostic);
    session->data_resources.close(
        session->data_resource_user, &resource);
    return status;
}

static bool apply_data_initializers(
    const scxml_block *block, scxml_session_impl *session,
    const cflow_statechart_executable_context *context,
    void *state, const scxml_expr_system_values *system_values,
    scxml_expr_is_active_fn is_active, void *active_user,
    size_t first, size_t count, const char **out_error) {
    const cmeta_type_desc *state_type =
        block != NULL ? block->state_type : NULL;
    const bool trivial_state =
        state_type != NULL &&
        cmeta_type_require_traits(
            state_type,
            CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY) ==
            CMETA_OK;
    void *snapshot;
    size_t assignment;
    if (count == 0u) return true;
    if (block == NULL || state == NULL ||
        system_values == NULL || out_error == NULL ||
        block->assignments == NULL || state_type == NULL ||
        state_type->size == 0u || first > block->assignment_storage_count ||
        count > block->assignment_storage_count - first ||
        (!trivial_state &&
         cmeta_type_require_traits(
             state_type, CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                             CMETA_TRAIT_DESTROY) != CMETA_OK)) {
        if (out_error != NULL)
            *out_error = "SCXML data initializer context is invalid";
        return false;
    }
    snapshot = malloc(state_type->size);
    if (snapshot == NULL) {
        *out_error = "SCXML data initializer snapshot allocation failed";
        return false;
    }
    for (assignment = 0u; assignment < count; ++assignment) {
        const size_t index = first + assignment;
        scxml_expr_diagnostic diagnostic = {0};
        scxml_expr_status status;
        const cmeta_data_desc *external_destination = NULL;
        const char *external_uri = NULL;
        size_t external_uri_size = 0u;
        if (scxml_session_data_initializer_is_overridden(session, index))
            continue;
        if (trivial_state) {
            memcpy(snapshot, state, state_type->size);
        } else if (!state_type->traits->copy_construct(snapshot, state)) {
            *out_error = "SCXML data initializer snapshot copy failed";
            free(snapshot);
            return false;
        }
        if (scxml_assign_external_source(
                &block->assignments[index], &external_uri,
                &external_uri_size, &external_destination)) {
            status = apply_external_data_initializer(
                &block->assignments[index], session, state, &diagnostic);
        } else {
            status = scxml_assign_apply_with_system(
                &block->assignments[index], state, is_active, active_user,
                system_values, &diagnostic);
        }
        if (status != SCXML_EXPR_OK) {
            if (trivial_state) {
                memcpy(state, snapshot, state_type->size);
            } else {
                state_type->traits->destroy(state);
                state_type->traits->move_construct(state, snapshot);
            }
        }
        if (!trivial_state) state_type->traits->destroy(snapshot);
        if (status != SCXML_EXPR_OK) {
            if (context == NULL) {
                *out_error = "SCXML startup data initializer failed";
                free(snapshot);
                return false;
            }
            if (raise_block_execution_error(block, context, out_error) !=
                SCXML_EXECUTE_BLOCK_ABORTED) {
                free(snapshot);
                return false;
            }
        }
    }
    free(snapshot);
    return true;
}

static scxml_execute_outcome raise_done_data_execution_error(
    const scxml_block *block,
    const cflow_statechart_executable_context *context,
    const char **out_error) {
    const scxml_execute_outcome outcome =
        raise_block_execution_error(block, context, out_error);
    return outcome == SCXML_EXECUTE_BLOCK_ABORTED
        ? SCXML_EXECUTE_CONTINUE : outcome;
}

typedef struct scxml_done_data_materialization {
    const scxml_block *block;
    scxml_session_impl *session;
    const scxml_done_data_descriptor *descriptor;
    const cflow_statechart_executable_context *context;
    const void *state;
    const scxml_expr_system_values *system_values;
    scxml_completion_data_slot *slot;
    const char **out_error;
} scxml_done_data_materialization;

static scxml_execute_outcome fail_done_data_expression(
    scxml_done_data_materialization *operation) {
    completion_data_publish_empty(operation->slot);
    return raise_done_data_execution_error(
        operation->block, operation->context, operation->out_error);
}

static bool initialize_done_data_projection(
    scxml_done_data_materialization *operation,
    size_t *out_marker_offset) {
    const scxml_done_data_descriptor *descriptor = operation->descriptor;
    scxml_completion_data_slot *slot = operation->slot;
    size_t stable_id_size;
    size_t required;
    if (descriptor->fields == NULL ||
        descriptor->shape.field_count != descriptor->assignment_count ||
        slot->projection_fields == NULL ||
        slot->projection_field_capacity < descriptor->assignment_count ||
        slot->projection_stable_id == NULL ||
        out_marker_offset == NULL)
        return false;
    stable_id_size = strlen(descriptor->schema.stable_id);
    if (!scxml_analyze_checked_add(
            stable_id_size, SCXML_COMPLETION_DATA_SUBSET_SUFFIX_SIZE,
            out_marker_offset) ||
        !scxml_analyze_checked_add(
            *out_marker_offset, descriptor->assignment_count, &required) ||
        !scxml_analyze_checked_add(required, 1u, &required) ||
        required > slot->projection_stable_id_capacity)
        return false;
    memcpy(slot->projection_stable_id,
           descriptor->schema.stable_id, stable_id_size);
    memcpy(slot->projection_stable_id + stable_id_size,
           SCXML_COMPLETION_DATA_SUBSET_SUFFIX,
           SCXML_COMPLETION_DATA_SUBSET_SUFFIX_SIZE);
    memset(slot->projection_stable_id + *out_marker_offset, '0',
           descriptor->assignment_count);
    slot->projection_stable_id[required - 1u] = '\0';
    slot->projection_shape = (cmeta_data_struct_shape){
        .layout = descriptor->shape.layout,
        .fields = slot->projection_fields,
        .field_count = 0u};
    slot->projection_schema = descriptor->schema;
    slot->projection_schema.stable_id = slot->projection_stable_id;
    slot->projection_schema.shape = &slot->projection_shape;
    return true;
}

static scxml_execute_outcome materialize_done_data_object(
    scxml_done_data_materialization *operation) {
    const scxml_done_data_descriptor *descriptor = operation->descriptor;
    size_t marker_offset = 0u;
    size_t successful_fields = 0u;
    size_t assignment;
    if (operation->session->program->cmeta_root == NULL ||
        operation->block->assignments == NULL ||
        descriptor->assignment_first >
            operation->block->assignment_storage_count ||
        descriptor->assignment_count >
            operation->block->assignment_storage_count -
                descriptor->assignment_first ||
        !cmeta_data_desc_valid(&descriptor->schema) ||
        descriptor->fields == NULL ||
        descriptor->shape.field_count != descriptor->assignment_count) {
        completion_data_release(operation->slot);
        *operation->out_error =
            "SCXML donedata object descriptor is invalid";
        return SCXML_EXECUTE_FATAL;
    }
    if (!scxml_runtime_copy_event_data_object(
            operation->session->program->cmeta_root,
            operation->slot->data_object.bytes, operation->state))
        return fail_done_data_expression(operation);
    operation->slot->data_schema = &descriptor->schema;
    operation->slot->data_object_live = true;
    if (!initialize_done_data_projection(operation, &marker_offset)) {
        completion_data_release(operation->slot);
        *operation->out_error =
            "SCXML donedata projection storage is invalid";
        return SCXML_EXECUTE_FATAL;
    }
    for (assignment = 0u;
         assignment < descriptor->assignment_count; ++assignment) {
        scxml_expr_diagnostic diagnostic = {0};
        if (scxml_assign_apply_from_with_system(
                &operation->block->assignments[
                    descriptor->assignment_first + assignment],
                operation->state, operation->slot->data_object.bytes,
                evaluate_cmeta_executable_active,
                (void *)operation->context,
                operation->system_values, &diagnostic) == SCXML_EXPR_OK) {
            operation->slot->projection_fields[successful_fields++] =
                descriptor->fields[assignment];
            operation->slot->projection_stable_id[
                marker_offset + assignment] = '1';
        } else {
            const scxml_execute_outcome outcome =
                raise_done_data_execution_error(
                    operation->block, operation->context,
                    operation->out_error);
            if (outcome != SCXML_EXECUTE_CONTINUE) {
                completion_data_release(operation->slot);
                return outcome;
            }
        }
    }
    if (successful_fields == 0u) {
        completion_data_publish_empty(operation->slot);
        return SCXML_EXECUTE_CONTINUE;
    }
    if (successful_fields != descriptor->assignment_count) {
        operation->slot->projection_shape.field_count = successful_fields;
        if (!cmeta_data_desc_valid(&operation->slot->projection_schema)) {
            completion_data_release(operation->slot);
            *operation->out_error =
                "SCXML donedata projected schema is invalid";
            return SCXML_EXECUTE_FATAL;
        }
        operation->slot->data_schema =
            &operation->slot->projection_schema;
    }
    operation->slot->state = SCXML_COMPLETION_DATA_READY;
    return SCXML_EXECUTE_CONTINUE;
}

static scxml_execute_outcome materialize_done_data_scalar(
    scxml_done_data_materialization *operation) {
    scxml_expr_diagnostic diagnostic = {0};
    scxml_expr_value value = {0};
    const char *data = NULL;
    size_t data_size = 0u;
    if (scxml_expr_evaluate_value_with_system(
            &operation->descriptor->expression, operation->state,
            evaluate_cmeta_executable_active,
            (void *)operation->context, operation->system_values,
            &value, &diagnostic) != SCXML_EXPR_OK ||
        !scxml_runtime_scalar_value_to_text(
            &value, operation->slot->data,
            sizeof(operation->slot->data), &data, &data_size))
        return fail_done_data_expression(operation);
    if (data_size > SCXML_EVENT_METADATA_CAPACITY) {
        completion_data_release(operation->slot);
        *operation->out_error =
            "SCXML donedata scalar exceeds its admitted bound";
        return SCXML_EXECUTE_FATAL;
    }
    if (data != operation->slot->data && data_size != 0u)
        memmove(operation->slot->data, data, data_size);
    operation->slot->data[data_size] = '\0';
    operation->slot->data_size = data_size;
    operation->slot->state = SCXML_COMPLETION_DATA_READY;
    return SCXML_EXECUTE_CONTINUE;
}

static scxml_execute_outcome materialize_done_data_inline(
    scxml_done_data_materialization *operation) {
    const size_t byte_count = operation->descriptor->content.byte_count;
    if (byte_count > SCXML_EVENT_METADATA_CAPACITY) {
        completion_data_release(operation->slot);
        *operation->out_error =
            "SCXML inline donedata exceeds its admitted bound";
        return SCXML_EXECUTE_FATAL;
    }
    if (byte_count != 0u)
        memcpy(operation->slot->data,
               operation->descriptor->content.bytes, byte_count);
    operation->slot->data[byte_count] = '\0';
    operation->slot->data_size = byte_count;
    operation->slot->state = SCXML_COMPLETION_DATA_READY;
    return SCXML_EXECUTE_CONTINUE;
}

static scxml_execute_outcome materialize_done_data(
    const scxml_block *block, scxml_session_impl *session,
    const scxml_step *step,
    const cflow_statechart_executable_context *context,
    const void *state,
    const scxml_expr_system_values *system_values,
    const char **out_error) {
    scxml_done_data_materialization operation = {0};
    if (session == NULL) return SCXML_EXECUTE_CONTINUE;
    if (block->done_data == NULL ||
        step->done_data >= block->done_data_storage_count ||
        context == NULL || state == NULL || system_values == NULL) {
        *out_error = "SCXML donedata execution context is invalid";
        return SCXML_EXECUTE_FATAL;
    }
    operation = (scxml_done_data_materialization){
        .block = block,
        .session = session,
        .descriptor = &block->done_data[step->done_data],
        .context = context,
        .state = state,
        .system_values = system_values,
        .out_error = out_error};
    operation.slot = completion_data_reserve(
        session, operation.descriptor->parent, out_error);
    if (operation.slot == NULL) return SCXML_EXECUTE_FATAL;
    if (operation.descriptor->assignment_count != 0u)
        return materialize_done_data_object(&operation);
    if (operation.descriptor->content.kind == SCXML_CONTENT_SCALAR)
        return materialize_done_data_scalar(&operation);
    if (operation.descriptor->content.kind == SCXML_CONTENT_TEXT_UTF8 ||
        operation.descriptor->content.kind == SCXML_CONTENT_XML_UTF8)
        return materialize_done_data_inline(&operation);
    completion_data_release(operation.slot);
    *out_error = "SCXML donedata descriptor has no materializable content";
    return SCXML_EXECUTE_FATAL;
}

static bool enqueue_condition_execution_error(
    const scxml_block *block,
    const cflow_statechart_executable_context *context,
    const char **out_error) {
    const bool null_value = false;
    const cflow_event_view raised = {
        block->execution_error_event, &cmeta_type_bool, &null_value};
    if (block->execution_error_event == 0u) {
        *out_error = "SCXML conditional error event is unavailable";
        return false;
    }
    return context->raise_internal(context->raise_user, &raised, out_error);
}

/* All first entries in one microstep share this ticket so their data and
   markers have the same publication boundary. */
static void commit_late_initializer(void *user) {
    scxml_session_impl *session =
        (scxml_session_impl *)user;
    size_t index;
    if (session == NULL || !session->late_initializer_ticket_pending) return;
    for (index = 0u; index < session->late_initializer_count; ++index) {
        if (session->late_initializers[index].phase ==
            SCXML_LATE_INITIALIZER_PENDING)
            session->late_initializers[index].phase =
                SCXML_LATE_INITIALIZER_DONE;
    }
    session->late_initializer_ticket_pending = false;
}

static void discard_late_initializer(void *user) {
    scxml_session_impl *session =
        (scxml_session_impl *)user;
    size_t index;
    if (session == NULL || !session->late_initializer_ticket_pending) return;
    for (index = 0u; index < session->late_initializer_count; ++index) {
        if (session->late_initializers[index].phase ==
            SCXML_LATE_INITIALIZER_PENDING)
            session->late_initializers[index].phase =
                SCXML_LATE_INITIALIZER_NEVER;
    }
    session->late_initializer_ticket_pending = false;
}

static void swap_scope_storage(
    scxml_scope_view *left, scxml_scope_view *right) {
    unsigned char *storage = left->storage;
    unsigned char *bound = left->bound;
    left->storage = right->storage;
    left->bound = right->bound;
    right->storage = storage;
    right->bound = bound;
}

static void commit_supplemental_scope(void *user) {
    scxml_session_impl *session = (scxml_session_impl *)user;
    if (session == NULL || !session->supplemental_transaction_pending)
        return;
    swap_scope_storage(
        &session->supplemental_committed,
        &session->supplemental_staged);
    scxml_scope_view_clear(&session->supplemental_staged);
    session->supplemental_transaction_pending = false;
}

static void discard_supplemental_scope(void *user) {
    scxml_session_impl *session = (scxml_session_impl *)user;
    if (session == NULL || !session->supplemental_transaction_pending)
        return;
    scxml_scope_view_clear(&session->supplemental_staged);
    session->supplemental_transaction_pending = false;
}

static bool begin_supplemental_block(
    scxml_session_impl *session,
    const cflow_statechart_executable_context *context,
    scxml_expr_system_values *system_values,
    const char **out_error) {
    cflow_statechart_effect_ticket ticket;
    if (session == NULL || session->program == NULL ||
        session->program->supplemental_scope.slot_count == 0u)
        return true;
    if (context == NULL || context->stage_effect == NULL ||
        system_values == NULL || out_error == NULL ||
        session->supplemental_checkpoint_live) {
        if (out_error != NULL)
            *out_error = "SCXML supplemental scope context is invalid";
        return false;
    }
    if (!session->supplemental_transaction_pending) {
        if (!scxml_scope_view_copy(
                &session->supplemental_staged,
                &session->supplemental_committed)) {
            *out_error = "SCXML supplemental scope staging failed";
            return false;
        }
        session->supplemental_transaction_pending = true;
        ticket = (cflow_statechart_effect_ticket){
            commit_supplemental_scope,
            discard_supplemental_scope, session};
        if (!context->stage_effect(
                context->effect_user, &ticket, out_error)) {
            scxml_scope_view_clear(&session->supplemental_staged);
            session->supplemental_transaction_pending = false;
            return false;
        }
    }
    if (!scxml_scope_view_copy(
            &session->supplemental_checkpoint,
            &session->supplemental_staged)) {
        *out_error = "SCXML supplemental block checkpoint failed";
        return false;
    }
    session->supplemental_checkpoint_live = true;
    system_values->supplemental = &session->supplemental_staged;
    return true;
}

static void settle_supplemental_block(
    scxml_session_impl *session, bool succeeded) {
    if (session == NULL || !session->supplemental_checkpoint_live) return;
    if (succeeded) {
        scxml_scope_view_clear(&session->supplemental_checkpoint);
    } else {
        scxml_scope_view_clear(&session->supplemental_staged);
        swap_scope_storage(
            &session->supplemental_staged,
            &session->supplemental_checkpoint);
    }
    session->supplemental_checkpoint_live = false;
}

static scxml_execute_outcome execute_scxml_range(
    const scxml_block *block,
    scxml_session_impl *session,
    const cflow_statechart_executable_context *context,
    scxml_mutable_state *mutable_state,
    const scxml_expr_system_values *system_values,
    size_t begin, size_t end, size_t depth,
    bool abort_condition_error, const char **out_error) {
    const bool null_value = false;
    size_t index = begin;
    if (depth > block->max_conditional_depth || begin > end ||
        end > block->step_storage_count) {
        *out_error = "SCXML executable range is invalid";
        return SCXML_EXECUTE_FATAL;
    }
    while (index < end) {
        const scxml_step *step = &block->steps[index];
        if (step->next <= index || step->next > end) {
            *out_error = "SCXML executable step span is invalid";
            return SCXML_EXECUTE_FATAL;
        }
        if (step->kind == SCXML_STEP_RAISE) {
            const cflow_event_view raised = {
                step->event, &cmeta_type_bool, &null_value};
            if (!context->raise_internal(
                    context->raise_user, &raised, out_error))
                return SCXML_EXECUTE_FATAL;
        } else if (step->kind == SCXML_STEP_SEND ||
                   step->kind == SCXML_STEP_CANCEL) {
            scxml_execute_outcome outcome;
            if (block->effects == NULL ||
                step->effect >= block->effect_storage_count) {
                *out_error = "SCXML effect descriptor is invalid";
                return SCXML_EXECUTE_FATAL;
            }
            outcome = step->kind == SCXML_STEP_SEND
                ? execute_send(
                      block, session, &block->effects[step->effect], context,
                      system_values, out_error)
                : execute_cancel(
                      block, session, &block->effects[step->effect], context,
                      system_values, out_error);
            if (outcome != SCXML_EXECUTE_CONTINUE) return outcome;
        } else if (step->kind == SCXML_STEP_INVOKE_ENTER ||
                   step->kind == SCXML_STEP_INVOKE_EXIT) {
            const scxml_execute_outcome outcome =
                execute_invocation_lifecycle(
                    block, session, step, context, out_error);
            if (outcome != SCXML_EXECUTE_CONTINUE) return outcome;
        } else if (step->kind == SCXML_STEP_LOG) {
            if (step->label == NULL) {
                *out_error = "SCXML log label storage is invalid";
                return SCXML_EXECUTE_FATAL;
            }
            TURBO_LOG_DEBUG(
                tlog_peek_default(), "cflow.scxml", step->label);
        } else if (step->kind == SCXML_STEP_SCRIPT) {
            void *state;
            if (session == NULL || session->program == NULL ||
                step->script >= session->program->script_count) {
                *out_error = "SCXML script descriptor is invalid";
                return SCXML_EXECUTE_FATAL;
            }
            state = mutable_state_get(mutable_state, out_error);
            if (state == NULL) return SCXML_EXECUTE_FATAL;
            if (!scxml_quickjs_execute_script(
                    session, &session->program->scripts[step->script], state,
                    &session->supplemental_staged,
                    evaluate_cmeta_executable_active, (void *)context,
                    system_values, out_error))
                return raise_block_execution_error(block, context, out_error);
        } else if (step->kind == SCXML_STEP_ASSIGN) {
            scxml_expr_diagnostic diagnostic = {0};
            void *state;
            if (block->assignments == NULL ||
                step->assignment >= block->assignment_storage_count) {
                *out_error = "SCXML assignment descriptor is invalid";
                return SCXML_EXECUTE_FATAL;
            }
            state = mutable_state_get(mutable_state, out_error);
            if (state == NULL) return SCXML_EXECUTE_FATAL;
            if (scxml_assign_apply_with_system(
                    &block->assignments[step->assignment],
                    state, evaluate_cmeta_executable_active,
                    (void *)context, system_values,
                    &diagnostic) !=
                SCXML_EXPR_OK)
                return raise_block_execution_error(block, context, out_error);
        } else if (step->kind == SCXML_STEP_DONEDATA) {
            const scxml_execute_outcome outcome = materialize_done_data(
                block, session, step, context,
                mutable_state_read(mutable_state, context),
                system_values, out_error);
            if (outcome != SCXML_EXECUTE_CONTINUE) return outcome;
        } else if (step->kind == SCXML_STEP_EARLY_INITIALIZE) {
            void *state = mutable_state_get(mutable_state, out_error);
            if (state == NULL ||
                !apply_data_initializers(
                    block, session, context, state, system_values,
                    evaluate_cmeta_initializer_active, NULL,
                    step->assignment, step->assignment_count, out_error))
                return SCXML_EXECUTE_FATAL;
        } else if (step->kind == SCXML_STEP_LATE_INITIALIZE) {
            scxml_late_initializer_state *initializer;
            cflow_statechart_effect_ticket ticket;
            void *state;
            if (session == NULL || context->stage_effect == NULL ||
                block->assignments == NULL ||
                step->late_initializer >= session->late_initializer_count ||
                step->assignment > block->assignment_storage_count ||
                step->assignment_count >
                    block->assignment_storage_count - step->assignment) {
                *out_error = "SCXML late initializer context is invalid";
                return SCXML_EXECUTE_FATAL;
            }
            initializer =
                &session->late_initializers[step->late_initializer];
            if (initializer->phase == SCXML_LATE_INITIALIZER_DONE) {
                index = step->next;
                continue;
            }
            if (initializer->phase != SCXML_LATE_INITIALIZER_NEVER) {
                *out_error =
                    "SCXML late initializer transaction is already pending";
                return SCXML_EXECUTE_FATAL;
            }
            initializer->phase = SCXML_LATE_INITIALIZER_PENDING;
            if (!session->late_initializer_ticket_pending) {
                session->late_initializer_ticket_pending = true;
                ticket = (cflow_statechart_effect_ticket){
                    commit_late_initializer, discard_late_initializer,
                    session};
                if (!context->stage_effect(
                        context->effect_user, &ticket, out_error)) {
                    initializer->phase = SCXML_LATE_INITIALIZER_NEVER;
                    session->late_initializer_ticket_pending = false;
                    return SCXML_EXECUTE_FATAL;
                }
            }
            state = mutable_state_get(mutable_state, out_error);
            if (state == NULL ||
                !apply_data_initializers(
                    block, session, context, state, system_values,
                    evaluate_cmeta_executable_active, (void *)context,
                    step->assignment, step->assignment_count, out_error))
                return SCXML_EXECUTE_FATAL;
        } else if (step->kind == SCXML_STEP_FOREACH) {
            const scxml_foreach_descriptor *descriptor;
            scxml_expr_diagnostic diagnostic = {0};
            scxml_foreach_snapshot snapshot = {0};
            scxml_foreach_value value = {0};
            size_t iteration;
            size_t next_depth;
            void *state;
            if (block->foreach_descriptors == NULL ||
                step->foreach_descriptor >= block->foreach_storage_count) {
                *out_error = "SCXML foreach descriptor is invalid";
                return SCXML_EXECUTE_FATAL;
            }
            descriptor =
                &block->foreach_descriptors[step->foreach_descriptor];
            if (descriptor->step_begin != index + 1u ||
                descriptor->step_begin > descriptor->step_end ||
                descriptor->step_end > step->next ||
                !scxml_analyze_checked_add(depth, 1u, &next_depth)) {
                *out_error = "SCXML foreach step span is invalid";
                return SCXML_EXECUTE_FATAL;
            }
            state = mutable_state_get(mutable_state, out_error);
            if (state == NULL) return SCXML_EXECUTE_FATAL;
            if (scxml_foreach_open_with_system(
                    &descriptor->program, state, system_values,
                    &snapshot, &diagnostic) !=
                SCXML_EXPR_OK)
                return raise_block_execution_error(block, context, out_error);
            if (snapshot.length != 0u &&
                scxml_foreach_value_init(
                    &descriptor->program, &value, &diagnostic) !=
                    SCXML_EXPR_OK) {
                scxml_foreach_snapshot_destroy(
                    &descriptor->program, &snapshot);
                return raise_block_execution_error(block, context, out_error);
            }
            for (iteration = 0u; iteration < snapshot.length; ++iteration) {
                scxml_execute_outcome outcome;
                if (scxml_foreach_next_with_system(
                        &descriptor->program, state, &snapshot,
                        &value, iteration, system_values, &diagnostic) !=
                    SCXML_EXPR_OK) {
                    scxml_foreach_value_destroy(
                        &descriptor->program, &value);
                    scxml_foreach_snapshot_destroy(
                        &descriptor->program, &snapshot);
                    return raise_block_execution_error(
                        block, context, out_error);
                }
                outcome = execute_scxml_range(
                    block, session, context, mutable_state, system_values,
                    descriptor->step_begin,
                    descriptor->step_end, next_depth, true, out_error);
                if (outcome != SCXML_EXECUTE_CONTINUE) {
                    scxml_foreach_value_destroy(
                        &descriptor->program, &value);
                    scxml_foreach_snapshot_destroy(
                        &descriptor->program, &snapshot);
                    return outcome;
                }
            }
            scxml_foreach_value_destroy(
                &descriptor->program, &value);
            scxml_foreach_snapshot_destroy(
                &descriptor->program, &snapshot);
        } else if (step->kind == SCXML_STEP_IF) {
            size_t branch;
            const scxml_branch *selected = NULL;
            if (step->branch_first > block->branch_storage_count ||
                step->branch_count >
                    block->branch_storage_count - step->branch_first) {
                *out_error = "SCXML conditional branch span is invalid";
                return SCXML_EXECUTE_FATAL;
            }
            for (branch = 0u; branch < step->branch_count; ++branch) {
                const scxml_branch *candidate =
                    &block->branches[step->branch_first + branch];
                const bool has_cmeta_condition =
                    candidate->condition.impl != NULL;
                if (candidate->step_begin < index + 1u ||
                    candidate->step_begin > candidate->step_end ||
                    candidate->step_end > step->next ||
                    (candidate->unconditional &&
                     (candidate->state != 0u || has_cmeta_condition)) ||
                    (!candidate->unconditional &&
                     ((candidate->state == 0u) ==
                      (candidate->condition.impl == NULL)))) {
                    *out_error = "SCXML conditional branch is invalid";
                    return SCXML_EXECUTE_FATAL;
                }
                if (candidate->unconditional) {
                    selected = candidate;
                    break;
                }
                if (has_cmeta_condition) {
                    scxml_expr_diagnostic diagnostic = {0};
                    bool enabled = false;
                    if (scxml_expr_evaluate_with_system(
                            &candidate->condition,
                            mutable_state_read(mutable_state, context),
                            evaluate_cmeta_executable_active, (void *)context,
                            system_values,
                            &enabled, &diagnostic) !=
                        SCXML_EXPR_OK) {
                        if (!enqueue_condition_execution_error(
                                block, context, out_error))
                            return SCXML_EXECUTE_FATAL;
                        if (abort_condition_error)
                            return SCXML_EXECUTE_BLOCK_ABORTED;
                        continue;
                    }
                    if (enabled) {
                        selected = candidate;
                        break;
                    }
                } else if (context->is_active(
                               context->configuration_user,
                               candidate->state)) {
                    selected = candidate;
                    break;
                }
            }
            if (selected != NULL) {
                size_t next_depth;
                scxml_execute_outcome outcome;
                if (!scxml_analyze_checked_add(depth, 1u, &next_depth))
                    return SCXML_EXECUTE_FATAL;
                outcome = execute_scxml_range(
                    block, session, context, mutable_state, system_values,
                    selected->step_begin,
                    selected->step_end, next_depth,
                    abort_condition_error, out_error);
                if (outcome != SCXML_EXECUTE_CONTINUE) return outcome;
            }
        } else {
            *out_error = "SCXML executable step is invalid";
            return SCXML_EXECUTE_FATAL;
        }
        index = step->next;
    }
    return SCXML_EXECUTE_CONTINUE;
}

static bool execute_scxml_block_impl(
    const scxml_block *block,
    scxml_session_impl *session,
    const cflow_statechart_executable_context *context,
    const char **out_error) {
    scxml_execute_outcome outcome;
    scxml_expr_system_values system_values;
    scxml_mutable_state mutable_state = {0};
    const bool trivial_state =
        cmeta_type_require_traits(
            block != NULL ? block->state_type : NULL,
            CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY) ==
        CMETA_OK;
    if (out_error != NULL) *out_error = NULL;
    if (block == NULL || block->steps == NULL ||
        (block->branch_storage_count != 0u && block->branches == NULL) ||
        (block->foreach_storage_count != 0u &&
         block->foreach_descriptors == NULL) ||
        block->step_begin >= block->step_end ||
        block->step_end > block->step_storage_count || context == NULL ||
        context->state == NULL || context->out_state == NULL ||
        context->raise_internal == NULL || context->is_active == NULL ||
        context->configuration_user == NULL ||
        out_error == NULL) {
        if (out_error != NULL)
            *out_error = "SCXML executable block context is invalid";
        return false;
    }
    if (!scxml_analyze_bind_current_event_system_values(
            session != NULL ? &session->system_values
                            : &block->system_values,
            block->event_names_by_id, block->event_name_count,
            context->event, &system_values)) {
        *out_error = "SCXML executable Event is not in the program map";
        return false;
    }
    if (session != NULL)
        system_values.event_name = session->system_values.event_name;
    if (session != NULL) session->failed_send_id_restore_live = false;
    if (trivial_state) {
        memcpy(context->out_state, context->state, block->state_type->size);
    } else if (cmeta_type_require_traits(
                   block->state_type,
                   CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                       CMETA_TRAIT_DESTROY) != CMETA_OK ||
               !block->state_type->traits->copy_construct(
                   context->out_state, context->state)) {
        *out_error = "SCXML state copy construction failed";
        return false;
    }
    if (!begin_supplemental_block(
            session, context, &system_values, out_error)) {
        if (!trivial_state)
            block->state_type->traits->destroy(context->out_state);
        return false;
    }
    mutable_state.value = context->out_state;
    outcome = execute_scxml_range(
        block, session, context, &mutable_state, &system_values,
        block->step_begin, block->step_end,
        0u, false, out_error);
    if (outcome == SCXML_EXECUTE_FATAL) {
        settle_supplemental_block(session, false);
        if (session != NULL) session->failed_send_id_restore_live = false;
        if (!trivial_state) {
            block->state_type->traits->destroy(context->out_state);
        }
        return false;
    }
    if (outcome == SCXML_EXECUTE_BLOCK_ABORTED) {
        scxml_expr_diagnostic diagnostic = {0};
        settle_supplemental_block(session, false);
        if (trivial_state) {
            memcpy(context->out_state, context->state,
                   block->state_type->size);
        } else {
            block->state_type->traits->destroy(context->out_state);
            if (!block->state_type->traits->copy_construct(
                    context->out_state, context->state)) {
                *out_error = "SCXML state rollback copy construction failed";
                return false;
            }
        }
        if (session != NULL && session->failed_send_id_restore_live &&
            scxml_location_assign_owned_string(
                &session->failed_send_id_location, context->out_state,
                session->failed_send_id, session->failed_send_id_size,
                SCXML_EVENT_METADATA_CAPACITY, &diagnostic) != SCXML_EXPR_OK) {
            session->failed_send_id_restore_live = false;
            *out_error = "SCXML failed send idlocation restore failed";
            return false;
        }
    } else {
        settle_supplemental_block(session, true);
    }
    if (session != NULL) session->failed_send_id_restore_live = false;
    return true;
}

bool scxml_runtime_execute_block(
    void *user, const cflow_statechart_executable_context *context,
    const char **out_error) {
    return execute_scxml_block_impl(
        (const scxml_block *)user, NULL, context, out_error);
}

bool scxml_runtime_execute_session_block(
    void *user, const cflow_statechart_executable_context *context,
    const char **out_error) {
    const scxml_session_binding_user *binding =
        (const scxml_session_binding_user *)user;
    if (binding == NULL || binding->session == NULL) {
        if (out_error != NULL)
            *out_error = "SCXML session binding is invalid";
        return false;
    }
    return execute_scxml_block_impl(
        binding->block, binding->session, context, out_error);
}
