#include "chttp_event_io_internal.h"

#include <turbo/clock.h>
#include <turbo/error_codes.h>

#include <string.h>

typedef enum egress_outcome {
    EGRESS_OUTCOME_COMPLETED = 0,
    EGRESS_OUTCOME_FAILED,
    EGRESS_OUTCOME_CANCELLED
} egress_outcome;

static bool bounded_string_size(
    const char *text, size_t maximum, size_t *out_size) {
    size_t size;
    if (text == NULL || out_size == NULL) return false;
    for (size = 0u; size <= maximum; ++size) {
        if (text[size] == '\0') {
            if (size == 0u) return false;
            *out_size = size;
            return true;
        }
    }
    return false;
}

static bool valid_connection_uri(const char *uri, size_t size) {
    static const char tcp[] = "tcp://";
    static const char pipe[] = "pipe://";
    return (size > sizeof(tcp) - 1u &&
            memcmp(uri, tcp, sizeof(tcp) - 1u) == 0) ||
           (size > sizeof(pipe) - 1u &&
            memcmp(uri, pipe, sizeof(pipe) - 1u) == 0);
}

static void reset_row_locked(scxml_chttp_egress_row *row) {
    scxml_chttp_processor_impl *processor = row->processor;
    scxml_chttp_binding_impl *binding = row->binding;
    char *connection_uri = row->connection_uri;
    char *authority = row->authority;
    char *target = row->target;
    char *body = row->body;
    uint32_t generation = row->generation;
    if (binding != NULL && binding->outbound_refs != 0u)
        --binding->outbound_refs;
    memset(row, 0, sizeof(*row));
    row->processor = processor;
    row->connection_uri = connection_uri;
    row->authority = authority;
    row->target = target;
    row->body = body;
    row->generation = generation;
}

static void abort_reserved(scxml_chttp_egress_row *row) {
    scxml_chttp_processor_impl *processor = row->binding->processor;
    turbo_mutex_lock(&processor->mutex);
    if (row->state == SCXML_CHTTP_EGRESS_RESERVED) {
        if (processor->stats.queued_egress != 0u)
            --processor->stats.queued_egress;
        reset_row_locked(row);
    } else {
        ++processor->stats.invariant_failures;
    }
    turbo_mutex_unlock(&processor->mutex);
}

static void egress_commit(void *user) {
    scxml_chttp_egress_row *row = (scxml_chttp_egress_row *)user;
    scxml_chttp_processor_impl *processor;
    uint64_t now;
    if (row == NULL || row->processor == NULL) return;
    processor = row->processor;
    now = turbo_monotonic_ms();
    turbo_mutex_lock(&processor->mutex);
    if (row->state != SCXML_CHTTP_EGRESS_RESERVED) {
        ++processor->stats.invariant_failures;
        turbo_mutex_unlock(&processor->mutex);
        return;
    }
    row->state = SCXML_CHTTP_EGRESS_READY;
    row->commit_sequence = row->binding->next_commit_sequence++;
    row->due_ms = row->delay_ms > UINT64_MAX - now
        ? UINT64_MAX : now + row->delay_ms;
    turbo_cond_signal(&processor->condition);
    turbo_mutex_unlock(&processor->mutex);
}

static void egress_discard(void *user) {
    scxml_chttp_egress_row *row = (scxml_chttp_egress_row *)user;
    scxml_chttp_processor_impl *processor;
    if (row == NULL || row->processor == NULL) return;
    processor = row->processor;
    turbo_mutex_lock(&processor->mutex);
    if (row->state == SCXML_CHTTP_EGRESS_RESERVED) {
        if (processor->stats.queued_egress != 0u)
            --processor->stats.queued_egress;
        reset_row_locked(row);
    } else {
        ++processor->stats.invariant_failures;
    }
    turbo_mutex_unlock(&processor->mutex);
}

scxml_adapter_status scxml_chttp_egress_prepare_send(
    scxml_chttp_binding_impl *binding, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    scxml_chttp_processor_impl *processor;
    scxml_chttp_egress_row *row = NULL;
    scxml_chttp_resolved_target resolved = {0};
    const char *content_type = NULL;
    size_t connection_size;
    size_t authority_size;
    size_t target_size;
    size_t content_type_size = 0u;
    size_t body_size = 0u;
    size_t index;
    scxml_adapter_status encode_status;
    if (out_error != NULL) *out_error = NULL;
    if (binding == NULL || request == NULL || out_ticket == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    if (request->target_size == 0u) {
        if (out_error != NULL) *out_error = "BasicHTTP target is empty";
        return SCXML_ADAPTER_ERROR_COMMUNICATION;
    }
    if (request->target == NULL ||
        (request->id == NULL && request->id_size != 0u) ||
        request->id_size > SCXML_EVENT_METADATA_CAPACITY)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    processor = binding->processor;
    turbo_mutex_lock(&processor->mutex);
    if (processor->state != SCXML_CHTTP_PROCESSOR_RUNNING ||
        (binding->state != SCXML_CHTTP_BINDING_RESERVED &&
         binding->state != SCXML_CHTTP_BINDING_ACTIVE)) {
        turbo_mutex_unlock(&processor->mutex);
        return SCXML_ADAPTER_CLOSED;
    }
    for (index = 0u; index < processor->egress_capacity; ++index) {
        if (processor->egress[index].state == SCXML_CHTTP_EGRESS_FREE) {
            row = &processor->egress[index];
            break;
        }
    }
    if (row == NULL) {
        turbo_mutex_unlock(&processor->mutex);
        return SCXML_ADAPTER_FULL;
    }
    ++row->generation;
    if (row->generation == 0u) ++row->generation;
    row->state = SCXML_CHTTP_EGRESS_RESERVED;
    row->binding = binding;
    row->delay_ms = request->delay_ms;
    if (request->id_size != 0u)
        memcpy(row->send_id, request->id, request->id_size);
    row->send_id[request->id_size] = '\0';
    row->send_id_size = request->id_size;
    ++binding->outbound_refs;
    ++processor->stats.queued_egress;
    turbo_mutex_unlock(&processor->mutex);

    if (!processor->resolve(
            processor->resolve_user, request->target, request->target_size,
            &resolved) ||
        !bounded_string_size(
            resolved.connection_uri, processor->max_access_uri_bytes,
            &connection_size) ||
        !bounded_string_size(
            resolved.authority, processor->max_access_uri_bytes,
            &authority_size) ||
        !bounded_string_size(
            resolved.target, processor->max_access_uri_bytes, &target_size) ||
        !valid_connection_uri(resolved.connection_uri, connection_size) ||
        resolved.target[0] != '/') {
        abort_reserved(row);
        if (out_error != NULL) *out_error = "BasicHTTP target was denied";
        return SCXML_ADAPTER_ERROR_COMMUNICATION;
    }
    memcpy(row->connection_uri, resolved.connection_uri, connection_size + 1u);
    memcpy(row->authority, resolved.authority, authority_size + 1u);
    memcpy(row->target, resolved.target, target_size + 1u);
    encode_status = scxml_chttp_encode_send_body(
        request, row->body, processor->max_encoded_body_bytes,
        &body_size, &content_type, &content_type_size);
    if (encode_status != SCXML_ADAPTER_ACCEPTED) {
        abort_reserved(row);
        return encode_status;
    }
    row->body[body_size] = '\0';
    row->body_size = body_size;
    row->content_type = content_type;
    row->content_type_size = content_type_size;
    turbo_mutex_lock(&processor->mutex);
    if (row->state != SCXML_CHTTP_EGRESS_RESERVED ||
        (binding->state != SCXML_CHTTP_BINDING_RESERVED &&
         binding->state != SCXML_CHTTP_BINDING_ACTIVE)) {
        if (row->state == SCXML_CHTTP_EGRESS_RESERVED) {
            if (processor->stats.queued_egress != 0u)
                --processor->stats.queued_egress;
            reset_row_locked(row);
        }
        turbo_mutex_unlock(&processor->mutex);
        return SCXML_ADAPTER_CLOSED;
    }
    ++processor->stats.egress_accepted;
    *out_ticket = (cflow_statechart_effect_ticket){
        egress_commit, egress_discard, row};
    turbo_mutex_unlock(&processor->mutex);
    return SCXML_ADAPTER_ACCEPTED;
}

static void cancel_ticket_discard(void *user) {
    scxml_chttp_cancel_ticket *ticket =
        (scxml_chttp_cancel_ticket *)user;
    if (ticket == NULL || ticket->processor == NULL) return;
    turbo_mutex_lock(&ticket->processor->mutex);
    if (ticket->reserved) {
        ticket->reserved = false;
    } else {
        ++ticket->processor->stats.invariant_failures;
    }
    turbo_mutex_unlock(&ticket->processor->mutex);
}

static void cancel_ticket_commit(void *user) {
    scxml_chttp_cancel_ticket *ticket =
        (scxml_chttp_cancel_ticket *)user;
    scxml_chttp_processor_impl *processor;
    scxml_chttp_egress_row *row;
    if (ticket == NULL || ticket->processor == NULL) return;
    processor = ticket->processor;
    turbo_mutex_lock(&processor->mutex);
    if (!ticket->reserved || ticket->target_slot >= processor->egress_capacity) {
        ++processor->stats.invariant_failures;
        turbo_mutex_unlock(&processor->mutex);
        return;
    }
    ticket->reserved = false;
    row = &processor->egress[ticket->target_slot];
    if (row->generation != ticket->target_generation) {
        turbo_mutex_unlock(&processor->mutex);
        return;
    }
    if (row->state == SCXML_CHTTP_EGRESS_READY) {
        if (processor->stats.queued_egress != 0u)
            --processor->stats.queued_egress;
        ++processor->stats.egress_cancelled;
        reset_row_locked(row);
    } else if (row->state == SCXML_CHTTP_EGRESS_SUBMITTING ||
               row->state == SCXML_CHTTP_EGRESS_SUBMITTED) {
        row->cancel_requested = true;
        turbo_cond_signal(&processor->condition);
    }
    turbo_mutex_unlock(&processor->mutex);
}

scxml_adapter_status scxml_chttp_egress_prepare_cancel(
    scxml_chttp_binding_impl *binding, const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error,
    bool *out_handled) {
    scxml_chttp_processor_impl *processor;
    scxml_chttp_egress_row *target = NULL;
    scxml_chttp_cancel_ticket *ticket = NULL;
    size_t target_index = 0u;
    size_t index;
    if (out_handled != NULL) *out_handled = false;
    if (out_error != NULL) *out_error = NULL;
    if (binding == NULL || request == NULL || out_ticket == NULL ||
        out_handled == NULL ||
        (request->send_id == NULL && request->send_id_size != 0u))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    processor = binding->processor;
    turbo_mutex_lock(&processor->mutex);
    for (index = 0u; index < processor->egress_capacity; ++index) {
        scxml_chttp_egress_row *candidate = &processor->egress[index];
        if (candidate->binding == binding &&
            candidate->send_id_size == request->send_id_size &&
            request->send_id != NULL &&
            memcmp(candidate->send_id, request->send_id,
                   request->send_id_size) == 0 &&
            (candidate->state == SCXML_CHTTP_EGRESS_READY ||
             candidate->state == SCXML_CHTTP_EGRESS_SUBMITTING ||
             candidate->state == SCXML_CHTTP_EGRESS_SUBMITTED)) {
            target = candidate;
            target_index = index;
            break;
        }
    }
    if (target == NULL) {
        turbo_mutex_unlock(&processor->mutex);
        return SCXML_ADAPTER_ACCEPTED;
    }
    *out_handled = true;
    for (index = 0u; index < processor->egress_capacity; ++index) {
        if (!processor->cancel_tickets[index].reserved) {
            ticket = &processor->cancel_tickets[index];
            break;
        }
    }
    if (ticket == NULL) {
        turbo_mutex_unlock(&processor->mutex);
        return SCXML_ADAPTER_FULL;
    }
    ticket->target_slot = target_index;
    ticket->target_generation = target->generation;
    ticket->reserved = true;
    *out_ticket = (cflow_statechart_effect_ticket){
        cancel_ticket_commit, cancel_ticket_discard, ticket};
    turbo_mutex_unlock(&processor->mutex);
    return SCXML_ADAPTER_ACCEPTED;
}

void scxml_chttp_egress_close_binding_locked(
    scxml_chttp_binding_impl *binding) {
    scxml_chttp_processor_impl *processor = binding->processor;
    size_t index;
    for (index = 0u; index < processor->egress_capacity; ++index) {
        scxml_chttp_egress_row *row = &processor->egress[index];
        if (row->binding != binding) continue;
        if (row->state == SCXML_CHTTP_EGRESS_READY) {
            if (processor->stats.queued_egress != 0u)
                --processor->stats.queued_egress;
            ++processor->stats.egress_cancelled;
            reset_row_locked(row);
        } else if (row->state == SCXML_CHTTP_EGRESS_SUBMITTING ||
                   row->state == SCXML_CHTTP_EGRESS_SUBMITTED) {
            row->cancel_requested = true;
        }
    }
    turbo_cond_signal(&processor->condition);
}

static bool earlier_row_blocks_locked(
    scxml_chttp_processor_impl *processor,
    const scxml_chttp_egress_row *candidate) {
    size_t index;
    for (index = 0u; index < processor->egress_capacity; ++index) {
        const scxml_chttp_egress_row *row = &processor->egress[index];
        if (row != candidate && row->binding == candidate->binding &&
            row->state != SCXML_CHTTP_EGRESS_FREE &&
            row->state != SCXML_CHTTP_EGRESS_RESERVED &&
            row->commit_sequence < candidate->commit_sequence)
            return true;
    }
    return false;
}

static scxml_chttp_egress_row *select_ready_locked(
    scxml_chttp_processor_impl *processor, uint64_t now) {
    scxml_chttp_egress_row *selected = NULL;
    size_t index;
    for (index = 0u; index < processor->egress_capacity; ++index) {
        scxml_chttp_egress_row *row = &processor->egress[index];
        if (row->state == SCXML_CHTTP_EGRESS_READY &&
            row->binding->state == SCXML_CHTTP_BINDING_ACTIVE &&
            row->due_ms <= now && !earlier_row_blocks_locked(processor, row) &&
            (selected == NULL || row->due_ms < selected->due_ms))
            selected = row;
    }
    return selected;
}

static void complete_row(
    scxml_chttp_egress_row *row, egress_outcome outcome) {
    scxml_chttp_binding_impl *binding = row->binding;
    scxml_chttp_processor_impl *processor = binding->processor;
    scxml_session *session;
    bool report_error;
    bool report_send_done;
    char send_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    size_t send_id_size;
    turbo_mutex_lock(&processor->mutex);
    if (row->state != SCXML_CHTTP_EGRESS_SUBMITTED &&
        row->state != SCXML_CHTTP_EGRESS_SUBMITTING) {
        ++processor->stats.invariant_failures;
        turbo_mutex_unlock(&processor->mutex);
        return;
    }
    row->state = SCXML_CHTTP_EGRESS_COMPLETING;
    if (processor->stats.in_flight_egress != 0u)
        --processor->stats.in_flight_egress;
    if (outcome == EGRESS_OUTCOME_COMPLETED)
        ++processor->stats.egress_completed;
    else if (outcome == EGRESS_OUTCOME_CANCELLED)
        ++processor->stats.egress_cancelled;
    else
        ++processor->stats.egress_failed;
    session = binding->session;
    report_error = outcome == EGRESS_OUTCOME_FAILED && session != NULL;
    report_send_done = row->delay_ms != 0u && row->send_id_size != 0u &&
        session != NULL;
    send_id_size = row->send_id_size;
    if (send_id_size != 0u)
        memcpy(send_id, row->send_id, send_id_size);
    send_id[send_id_size] = '\0';
    turbo_mutex_unlock(&processor->mutex);
    if (report_error)
        (void)scxml_session_report_adapter_error(
            session, SCXML_ADAPTER_ERROR_KIND_COMMUNICATION);
    if (report_send_done)
        (void)scxml_session_report_send_done(
            session, send_id, send_id_size);
    turbo_mutex_lock(&processor->mutex);
    if (row->state == SCXML_CHTTP_EGRESS_COMPLETING)
        reset_row_locked(row);
    else
        ++processor->stats.invariant_failures;
    turbo_mutex_unlock(&processor->mutex);
}

static void request_complete(
    void *user, chttp_request request,
    const chttp_response_view *response, const chttp_error *error) {
    scxml_chttp_egress_row *row = (scxml_chttp_egress_row *)user;
    egress_outcome outcome;
    (void)request;
    if (error != NULL && error->status == TURBO_ECANCELED)
        outcome = EGRESS_OUTCOME_CANCELLED;
    else if (error != NULL || response == NULL ||
             response->status_code < 200u || response->status_code >= 300u)
        outcome = EGRESS_OUTCOME_FAILED;
    else
        outcome = EGRESS_OUTCOME_COMPLETED;
    complete_row(row, outcome);
}

static bool cancel_one(scxml_chttp_processor_impl *processor) {
    scxml_chttp_egress_row *row = NULL;
    chttp_request request = {0};
    size_t index;
    turbo_mutex_lock(&processor->mutex);
    for (index = 0u; index < processor->egress_capacity; ++index) {
        if (processor->egress[index].state == SCXML_CHTTP_EGRESS_SUBMITTED &&
            processor->egress[index].cancel_requested) {
            row = &processor->egress[index];
            row->cancel_requested = false;
            request = row->request;
            break;
        }
    }
    turbo_mutex_unlock(&processor->mutex);
    if (row == NULL) return false;
    (void)chttp_async_request_cancel(&processor->client, request);
    return true;
}

static bool submit_one(scxml_chttp_processor_impl *processor) {
    scxml_chttp_egress_row *row;
    chttp_header header;
    chttp_request_options options;
    chttp_request request = {0};
    int status;
    turbo_mutex_lock(&processor->mutex);
    row = select_ready_locked(processor, turbo_monotonic_ms());
    if (row == NULL) {
        turbo_mutex_unlock(&processor->mutex);
        return false;
    }
    row->state = SCXML_CHTTP_EGRESS_SUBMITTING;
    if (processor->stats.queued_egress != 0u)
        --processor->stats.queued_egress;
    ++processor->stats.in_flight_egress;
    header = (chttp_header){"Content-Type", row->content_type};
    options = (chttp_request_options){
        .connection_uri = row->connection_uri,
        .authority = row->authority,
        .target = row->target,
        .method = CHTTP_METHOD_POST,
        .headers = &header,
        .header_count = 1u,
        .body = row->body,
        .body_size = row->body_size,
        .on_complete = request_complete,
        .user = row};
    turbo_mutex_unlock(&processor->mutex);
    status = chttp_async_client_submit(&processor->client, &options, &request);
    turbo_mutex_lock(&processor->mutex);
    if (status == TURBO_OK && row->state == SCXML_CHTTP_EGRESS_SUBMITTING) {
        row->request = request;
        row->state = SCXML_CHTTP_EGRESS_SUBMITTED;
        turbo_mutex_unlock(&processor->mutex);
        return true;
    }
    if (status == TURBO_ENOBUFS &&
        row->state == SCXML_CHTTP_EGRESS_SUBMITTING) {
        row->state = SCXML_CHTTP_EGRESS_READY;
        ++processor->stats.queued_egress;
        if (processor->stats.in_flight_egress != 0u)
            --processor->stats.in_flight_egress;
        turbo_mutex_unlock(&processor->mutex);
        return false;
    }
    turbo_mutex_unlock(&processor->mutex);
    complete_row(row, EGRESS_OUTCOME_FAILED);
    return true;
}

void scxml_chttp_egress_worker(void *user) {
    scxml_chttp_processor_impl *processor =
        (scxml_chttp_processor_impl *)user;
    int status = TURBO_OK;
    for (;;) {
        bool stop;
        size_t completions = 0u;
        turbo_mutex_lock(&processor->mutex);
        stop = processor->stop_requested;
        turbo_mutex_unlock(&processor->mutex);
        if (stop) break;
        while (cancel_one(processor)) {}
        (void)submit_one(processor);
        status = chttp_async_client_poll(
            &processor->client, processor->worker_poll_ms, &completions);
        if (status != TURBO_OK && status != TURBO_ESHUTDOWN) break;
    }
    if (status == TURBO_OK || status == TURBO_ESHUTDOWN)
        status = chttp_async_client_stop(
            &processor->client, processor->stop_timeout_ms);
    if (status == TURBO_OK) {
        status = chttp_async_client_destroy(&processor->client);
        if (status == TURBO_OK) processor->client_destroyed = true;
    }
    turbo_mutex_lock(&processor->mutex);
    processor->worker_status = status;
    turbo_cond_broadcast(&processor->condition);
    turbo_mutex_unlock(&processor->mutex);
}
