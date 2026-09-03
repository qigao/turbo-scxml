#include "chttp_event_io_internal.h"

#include <salts/error_codes.h>
#include <salts/platform.h>

#include <stdatomic.h>
#include <string.h>

static atomic_bool FAIL_NEXT_CANCEL_ADMISSION;
static atomic_uint DELAY_NEXT_COMPLETION_RELEASE_MS;

typedef enum scxml_chttp_egress_outcome {
    SCXML_CHTTP_EGRESS_COMPLETED = 0,
    SCXML_CHTTP_EGRESS_FAILED,
    SCXML_CHTTP_EGRESS_CANCELLED
} scxml_chttp_egress_outcome;

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
    char *send_id = row->send_id;
    char *body = row->body;
    const uint32_t generation = row->generation;
    if (binding != NULL && binding->outbound_references != 0u)
        --binding->outbound_references;
    memset(row, 0, sizeof(*row));
    row->processor = processor;
    row->connection_uri = connection_uri;
    row->authority = authority;
    row->target = target;
    row->send_id = send_id;
    row->body = body;
    row->generation = generation;
}

static void abort_reserved(scxml_chttp_egress_row *row) {
    scxml_chttp_processor_impl *processor = row->processor;
    salts_mutex_lock(&processor->lock);
    if (row->state == SCXML_CHTTP_EGRESS_RESERVED) {
        if (processor->queued_egress != 0u) --processor->queued_egress;
        reset_row_locked(row);
    } else {
        ++processor->invariant_failures;
    }
    salts_mutex_unlock(&processor->lock);
}

static void egress_commit(void *user) {
    scxml_chttp_egress_row *row = (scxml_chttp_egress_row *)user;
    scxml_chttp_processor_impl *processor;
    uint64_t now;
    if (row == NULL || row->processor == NULL) return;
    processor = row->processor;
    now = salts_monotonic_ms();
    salts_mutex_lock(&processor->lock);
    if (row->state != SCXML_CHTTP_EGRESS_RESERVED || row->binding == NULL) {
        ++processor->invariant_failures;
        salts_mutex_unlock(&processor->lock);
        return;
    }
    if (processor->state != SCXML_CHTTP_PROCESSOR_RUNNING ||
        (row->binding->state != SCXML_CHTTP_BINDING_RESERVED &&
         row->binding->state != SCXML_CHTTP_BINDING_ACTIVE)) {
        if (processor->queued_egress != 0u) --processor->queued_egress;
        reset_row_locked(row);
        salts_mutex_unlock(&processor->lock);
        return;
    }
    row->state = SCXML_CHTTP_EGRESS_READY;
    row->commit_sequence = row->binding->next_commit_sequence++;
    if (row->binding->next_commit_sequence == 0u)
        row->binding->next_commit_sequence = 1u;
    row->due_ms = row->delay_ms > UINT64_MAX - now
        ? UINT64_MAX : now + row->delay_ms;
    salts_cond_signal(&processor->wake);
    salts_mutex_unlock(&processor->lock);
}

static void egress_discard(void *user) {
    scxml_chttp_egress_row *row = (scxml_chttp_egress_row *)user;
    scxml_chttp_processor_impl *processor;
    if (row == NULL || row->processor == NULL) return;
    processor = row->processor;
    salts_mutex_lock(&processor->lock);
    if (row->state == SCXML_CHTTP_EGRESS_RESERVED) {
        if (processor->queued_egress != 0u) --processor->queued_egress;
        reset_row_locked(row);
    } else {
        ++processor->invariant_failures;
    }
    salts_mutex_unlock(&processor->lock);
}

scxml_adapter_status scxml_chttp_egress_prepare_send(
    scxml_chttp_binding_impl *binding,
    const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    scxml_chttp_processor_impl *processor;
    scxml_chttp_egress_row *row = NULL;
    scxml_chttp_resolved_target resolved = {0};
    scxml_chttp_codec_limits limits;
    scxml_chttp_encoded_body encoded = {0};
    size_t required_body_size = 0u;
    size_t connection_size;
    size_t authority_size;
    size_t target_size;
    size_t index;
    int resolve_status;
    scxml_adapter_status encode_status;
    if (out_ticket != NULL) memset(out_ticket, 0, sizeof(*out_ticket));
    if (out_error != NULL) *out_error = NULL;
    if (binding == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    if (request->target_size == 0u) {
        *out_error = "BasicHTTP target is empty";
        return SCXML_ADAPTER_ERROR_COMMUNICATION;
    }
    if (request->target == NULL ||
        (request->id == NULL && request->id_size != 0u) ||
        request->id_size > SCXML_EVENT_METADATA_CAPACITY)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    processor = binding->processor;
    salts_mutex_lock(&processor->lock);
    if (processor->state != SCXML_CHTTP_PROCESSOR_RUNNING ||
        (binding->state != SCXML_CHTTP_BINDING_RESERVED &&
         binding->state != SCXML_CHTTP_BINDING_ACTIVE)) {
        salts_mutex_unlock(&processor->lock);
        return SCXML_ADAPTER_CLOSED;
    }
    for (index = 0u; index < processor->config.egress_capacity; ++index) {
        if (processor->egress[index].state == SCXML_CHTTP_EGRESS_FREE) {
            row = &processor->egress[index];
            break;
        }
    }
    if (row == NULL) {
        salts_mutex_unlock(&processor->lock);
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
    ++binding->outbound_references;
    ++processor->queued_egress;
    salts_mutex_unlock(&processor->lock);

    resolve_status = processor->config.resolve(
        processor->config.resolve_user,
        request->target, request->target_size, &resolved);
    if (resolve_status != SALTS_OK ||
        !bounded_string_size(
            resolved.connection_uri,
            processor->config.max_access_uri_bytes, &connection_size) ||
        !bounded_string_size(
            resolved.authority,
            processor->config.max_access_uri_bytes, &authority_size) ||
        !bounded_string_size(
            resolved.target,
            processor->config.max_access_uri_bytes, &target_size) ||
        !valid_connection_uri(resolved.connection_uri, connection_size) ||
        resolved.target[0] != '/') {
        abort_reserved(row);
        *out_error = "BasicHTTP target was denied";
        return SCXML_ADAPTER_ERROR_COMMUNICATION;
    }
    memcpy(row->connection_uri, resolved.connection_uri, connection_size + 1u);
    memcpy(row->authority, resolved.authority, authority_size + 1u);
    memcpy(row->target, resolved.target, target_size + 1u);
    limits = (scxml_chttp_codec_limits){
        .max_event_name_bytes = processor->config.max_event_name_bytes,
        .max_form_entry_count = processor->config.max_form_entry_count,
        .max_form_name_bytes = processor->config.max_form_name_bytes,
        .max_form_value_bytes = processor->config.max_form_value_bytes,
        .max_encoded_body_bytes = processor->config.max_encoded_body_bytes};
    encode_status = scxml_chttp_codec_encode(
        request, &limits, row->body,
        processor->config.max_encoded_body_bytes,
        &required_body_size, &encoded);
    if (encode_status != SCXML_ADAPTER_ACCEPTED) {
        abort_reserved(row);
        return encode_status;
    }
    row->body_size = encoded.body_size;
    row->media_type = encoded.media_type;
    row->media_type_size = encoded.media_type_size;
    salts_mutex_lock(&processor->lock);
    if (row->state != SCXML_CHTTP_EGRESS_RESERVED ||
        (binding->state != SCXML_CHTTP_BINDING_RESERVED &&
         binding->state != SCXML_CHTTP_BINDING_ACTIVE)) {
        if (row->state == SCXML_CHTTP_EGRESS_RESERVED) {
            if (processor->queued_egress != 0u) --processor->queued_egress;
            reset_row_locked(row);
        }
        salts_mutex_unlock(&processor->lock);
        return SCXML_ADAPTER_CLOSED;
    }
    ++processor->egress_accepted;
    *out_ticket = (cflow_statechart_effect_ticket){
        egress_commit, egress_discard, row};
    salts_mutex_unlock(&processor->lock);
    return SCXML_ADAPTER_ACCEPTED;
}

static void cancel_discard(void *user) {
    scxml_chttp_cancel_ticket *ticket =
        (scxml_chttp_cancel_ticket *)user;
    if (ticket == NULL || ticket->processor == NULL) return;
    salts_mutex_lock(&ticket->processor->lock);
    if (ticket->reserved)
        ticket->reserved = false;
    else
        ++ticket->processor->invariant_failures;
    salts_mutex_unlock(&ticket->processor->lock);
}

static void cancel_commit(void *user) {
    scxml_chttp_cancel_ticket *ticket =
        (scxml_chttp_cancel_ticket *)user;
    scxml_chttp_processor_impl *processor;
    scxml_chttp_egress_row *row;
    if (ticket == NULL || ticket->processor == NULL) return;
    processor = ticket->processor;
    salts_mutex_lock(&processor->lock);
    if (!ticket->reserved ||
        ticket->target_index >= processor->config.egress_capacity) {
        ++processor->invariant_failures;
        salts_mutex_unlock(&processor->lock);
        return;
    }
    ticket->reserved = false;
    row = &processor->egress[ticket->target_index];
    if (row->generation != ticket->target_generation) {
        salts_mutex_unlock(&processor->lock);
        return;
    }
    if (row->state == SCXML_CHTTP_EGRESS_READY) {
        if (processor->queued_egress != 0u) --processor->queued_egress;
        ++processor->egress_cancelled;
        reset_row_locked(row);
    } else if (row->state == SCXML_CHTTP_EGRESS_SUBMITTING ||
               row->state == SCXML_CHTTP_EGRESS_SUBMITTED) {
        row->cancel_requested = true;
        salts_cond_signal(&processor->wake);
    }
    salts_mutex_unlock(&processor->lock);
}

scxml_adapter_status scxml_chttp_egress_prepare_cancel(
    scxml_chttp_binding_impl *binding,
    const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error,
    bool *out_handled) {
    scxml_chttp_processor_impl *processor;
    scxml_chttp_egress_row *target = NULL;
    scxml_chttp_cancel_ticket *ticket = NULL;
    size_t target_index = 0u;
    size_t index;
    if (out_ticket != NULL) memset(out_ticket, 0, sizeof(*out_ticket));
    if (out_error != NULL) *out_error = NULL;
    if (out_handled != NULL) *out_handled = false;
    if (binding == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || out_handled == NULL ||
        (request->send_id == NULL && request->send_id_size != 0u))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    processor = binding->processor;
    salts_mutex_lock(&processor->lock);
    if (processor->state != SCXML_CHTTP_PROCESSOR_RUNNING ||
        (binding->state != SCXML_CHTTP_BINDING_RESERVED &&
         binding->state != SCXML_CHTTP_BINDING_ACTIVE)) {
        salts_mutex_unlock(&processor->lock);
        return SCXML_ADAPTER_CLOSED;
    }
    for (index = 0u; index < processor->config.egress_capacity; ++index) {
        scxml_chttp_egress_row *candidate = &processor->egress[index];
        if (candidate->binding == binding && request->send_id != NULL &&
            candidate->send_id_size == request->send_id_size &&
            memcmp(candidate->send_id, request->send_id,
                   request->send_id_size) == 0 &&
            (candidate->state == SCXML_CHTTP_EGRESS_RESERVED ||
             candidate->state == SCXML_CHTTP_EGRESS_READY ||
             candidate->state == SCXML_CHTTP_EGRESS_SUBMITTING ||
             candidate->state == SCXML_CHTTP_EGRESS_SUBMITTED ||
             candidate->state == SCXML_CHTTP_EGRESS_COMPLETING)) {
            target = candidate;
            target_index = index;
            break;
        }
    }
    if (target == NULL) {
        salts_mutex_unlock(&processor->lock);
        return SCXML_ADAPTER_ACCEPTED;
    }
    *out_handled = true;
    for (index = 0u; index < processor->config.egress_capacity; ++index) {
        if (!processor->cancel_tickets[index].reserved) {
            ticket = &processor->cancel_tickets[index];
            break;
        }
    }
    if (ticket == NULL) {
        salts_mutex_unlock(&processor->lock);
        return SCXML_ADAPTER_FULL;
    }
    ticket->target_index = target_index;
    ticket->target_generation = target->generation;
    ticket->reserved = true;
    *out_ticket = (cflow_statechart_effect_ticket){
        cancel_commit, cancel_discard, ticket};
    salts_mutex_unlock(&processor->lock);
    return SCXML_ADAPTER_ACCEPTED;
}

void scxml_chttp_egress_close_binding_locked(
    scxml_chttp_binding_impl *binding) {
    scxml_chttp_processor_impl *processor;
    size_t index;
    if (binding == NULL || binding->processor == NULL) return;
    processor = binding->processor;
    for (index = 0u; index < processor->config.egress_capacity; ++index) {
        scxml_chttp_egress_row *row = &processor->egress[index];
        if (row->binding != binding) continue;
        if (row->state == SCXML_CHTTP_EGRESS_READY) {
            if (processor->queued_egress != 0u) --processor->queued_egress;
            ++processor->egress_cancelled;
            reset_row_locked(row);
        } else if (row->state == SCXML_CHTTP_EGRESS_SUBMITTING ||
                   row->state == SCXML_CHTTP_EGRESS_SUBMITTED) {
            row->cancel_requested = true;
        }
    }
    salts_cond_signal(&processor->wake);
}

static bool earlier_row_blocks_locked(
    scxml_chttp_processor_impl *processor,
    const scxml_chttp_egress_row *candidate) {
    size_t index;
    for (index = 0u; index < processor->config.egress_capacity; ++index) {
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
    for (index = 0u; index < processor->config.egress_capacity; ++index) {
        scxml_chttp_egress_row *row = &processor->egress[index];
        if (row->state == SCXML_CHTTP_EGRESS_READY &&
            row->binding->state == SCXML_CHTTP_BINDING_ACTIVE &&
            row->due_ms <= now &&
            !earlier_row_blocks_locked(processor, row) &&
            (selected == NULL || row->due_ms < selected->due_ms))
            selected = row;
    }
    return selected;
}

static void complete_row(
    scxml_chttp_egress_row *row,
    scxml_chttp_egress_outcome outcome) {
    scxml_chttp_processor_impl *processor = row->processor;
    scxml_chttp_binding_impl *binding;
    scxml_session *session;
    bool report_error;
    bool report_send_done;
    char send_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    size_t send_id_size;
    salts_mutex_lock(&processor->lock);
    if ((row->state != SCXML_CHTTP_EGRESS_SUBMITTED &&
         row->state != SCXML_CHTTP_EGRESS_SUBMITTING) ||
        row->binding == NULL) {
        ++processor->invariant_failures;
        salts_mutex_unlock(&processor->lock);
        return;
    }
    row->state = SCXML_CHTTP_EGRESS_COMPLETING;
    if (processor->in_flight_egress != 0u) --processor->in_flight_egress;
    if (outcome == SCXML_CHTTP_EGRESS_COMPLETED)
        ++processor->egress_completed;
    else if (outcome == SCXML_CHTTP_EGRESS_CANCELLED)
        ++processor->egress_cancelled;
    else
        ++processor->egress_failed;
    binding = row->binding;
    session = binding->session;
    report_error = outcome == SCXML_CHTTP_EGRESS_FAILED && session != NULL &&
        binding->state == SCXML_CHTTP_BINDING_ACTIVE;
    report_send_done = row->delay_ms != 0u && row->send_id_size != 0u &&
        session != NULL;
    send_id_size = row->send_id_size;
    if (send_id_size != 0u)
        memcpy(send_id, row->send_id, send_id_size);
    send_id[send_id_size] = '\0';
    salts_mutex_unlock(&processor->lock);
    {
        const uint32_t delay_ms = atomic_exchange_explicit(
            &DELAY_NEXT_COMPLETION_RELEASE_MS, 0u, memory_order_acq_rel);
        if (delay_ms != 0u) salts_sleep_ms(delay_ms);
    }
    if (report_error)
        (void)scxml_session_report_adapter_error(
            session, SCXML_ADAPTER_ERROR_KIND_COMMUNICATION);
    if (report_send_done)
        (void)scxml_session_report_send_done(
            session, send_id, send_id_size);
    salts_mutex_lock(&processor->lock);
    if (row->state == SCXML_CHTTP_EGRESS_COMPLETING)
        reset_row_locked(row);
    else
        ++processor->invariant_failures;
    salts_mutex_unlock(&processor->lock);
}

static void request_complete(
    void *user, chttp_request request,
    const chttp_response_view *response,
    const chttp_error *error) {
    scxml_chttp_egress_row *row = (scxml_chttp_egress_row *)user;
    scxml_chttp_egress_outcome outcome;
    (void)request;
    if (error != NULL && error->status == SALTS_ECANCELED)
        outcome = SCXML_CHTTP_EGRESS_CANCELLED;
    else if (error != NULL || response == NULL ||
             response->status_code < 200u || response->status_code >= 300u)
        outcome = SCXML_CHTTP_EGRESS_FAILED;
    else
        outcome = SCXML_CHTTP_EGRESS_COMPLETED;
    complete_row(row, outcome);
}

bool scxml_chttp_egress_cancel_one(scxml_chttp_processor_impl *processor) {
    scxml_chttp_egress_row *row = NULL;
    chttp_request request = {0};
    int status;
    size_t index;
    if (processor == NULL) return false;
    salts_mutex_lock(&processor->lock);
    for (index = 0u; index < processor->config.egress_capacity; ++index) {
        if (processor->egress[index].state ==
                SCXML_CHTTP_EGRESS_SUBMITTED &&
            processor->egress[index].cancel_requested) {
            row = &processor->egress[index];
            row->cancel_requested = false;
            request = row->request;
            break;
        }
    }
    salts_mutex_unlock(&processor->lock);
    if (row == NULL) return false;
    status = atomic_exchange_explicit(
                 &FAIL_NEXT_CANCEL_ADMISSION, false, memory_order_acq_rel)
        ? SALTS_ENOBUFS
        : chttp_async_request_cancel(&processor->client, request);
    if (status == SALTS_OK || status == SALTS_EALREADY) return true;
    if (status == SALTS_ENOENT) {
        salts_mutex_lock(&processor->lock);
        if (row->state == SCXML_CHTTP_EGRESS_SUBMITTED &&
            row->request.slot == request.slot &&
            row->request.generation == request.generation)
            ++processor->invariant_failures;
        salts_mutex_unlock(&processor->lock);
        complete_row(row, SCXML_CHTTP_EGRESS_CANCELLED);
        return true;
    }
    {
        salts_mutex_lock(&processor->lock);
        if (row->state == SCXML_CHTTP_EGRESS_SUBMITTED &&
            row->request.slot == request.slot &&
            row->request.generation == request.generation) {
            row->cancel_requested = true;
            if (status != SALTS_ENOBUFS && status != SALTS_EBUSY)
                ++processor->invariant_failures;
        }
        salts_mutex_unlock(&processor->lock);
        /* Let the client make progress before retrying a rejected close. */
        return false;
    }
}

void scxml_chttp_test_fail_next_cancel_admission(void) {
    atomic_store_explicit(
        &FAIL_NEXT_CANCEL_ADMISSION, true, memory_order_release);
}

void scxml_chttp_test_delay_next_completion_release(uint32_t delay_ms) {
    atomic_store_explicit(
        &DELAY_NEXT_COMPLETION_RELEASE_MS, delay_ms, memory_order_release);
}

bool scxml_chttp_egress_submit_one(scxml_chttp_processor_impl *processor) {
    scxml_chttp_egress_row *row;
    chttp_header header;
    chttp_request_options options;
    chttp_request request = {0};
    int status;
    if (processor == NULL) return false;
    salts_mutex_lock(&processor->lock);
    row = select_ready_locked(processor, salts_monotonic_ms());
    if (row == NULL) {
        salts_mutex_unlock(&processor->lock);
        return false;
    }
    row->state = SCXML_CHTTP_EGRESS_SUBMITTING;
    if (processor->queued_egress != 0u) --processor->queued_egress;
    ++processor->in_flight_egress;
    header = (chttp_header){"Content-Type", row->media_type};
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
    salts_mutex_unlock(&processor->lock);
    status = chttp_async_client_submit(&processor->client, &options, &request);
    salts_mutex_lock(&processor->lock);
    if (status == SALTS_OK &&
        row->state == SCXML_CHTTP_EGRESS_SUBMITTING) {
        row->request = request;
        row->state = SCXML_CHTTP_EGRESS_SUBMITTED;
        salts_mutex_unlock(&processor->lock);
        return true;
    }
    if (status == SALTS_ENOBUFS &&
        row->state == SCXML_CHTTP_EGRESS_SUBMITTING) {
        row->state = SCXML_CHTTP_EGRESS_READY;
        ++processor->queued_egress;
        if (processor->in_flight_egress != 0u)
            --processor->in_flight_egress;
        salts_mutex_unlock(&processor->lock);
        return false;
    }
    salts_mutex_unlock(&processor->lock);
    complete_row(row, SCXML_CHTTP_EGRESS_FAILED);
    return true;
}
