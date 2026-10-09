#include <scxml/cnet_egress.h>

#include <salts/thread.h>

#include <stdlib.h>
#include <string.h>

typedef enum scxml_cnet_egress_row_state {
    SCXML_CNET_ROW_FREE = 0,
    SCXML_CNET_ROW_RESERVED,
    SCXML_CNET_ROW_READY,
    SCXML_CNET_ROW_SUBMITTING,
    SCXML_CNET_ROW_SUBMITTED
} scxml_cnet_egress_row_state;

typedef struct scxml_cnet_egress_impl scxml_cnet_egress_impl;

typedef struct scxml_cnet_egress_row {
    scxml_cnet_egress_impl *owner;
    scxml_cnet_egress_row_state state;
    uint64_t sequence;
    size_t size;
    unsigned char bytes[SCXML_EVENT_METADATA_CAPACITY];
} scxml_cnet_egress_row;

struct scxml_cnet_egress_impl {
    cmeta_mutex_t lock;
    cnet_client *client;
    cnet_connection connection;
    scxml_cnet_egress_row *rows;
    size_t capacity;
    size_t max_payload_bytes;
    size_t pending;
    size_t high_water;
    uint64_t next_sequence;
    uint64_t prepared;
    uint64_t committed;
    uint64_t discarded;
    uint64_t submitted;
    uint64_t completed;
    uint64_t cancelled;
    uint64_t rejected_full;
    uint64_t transient_full;
    uint64_t stale_callbacks;
    uint64_t invariant_failures;
    bool bound;
    bool connected;
    bool terminal;
    bool closed;
    bool failed;
    bool pumping;
    int first_error;
};

static scxml_cnet_egress_impl *impl_of(scxml_cnet_egress *egress) {
    return egress != NULL ? (scxml_cnet_egress_impl *)egress->impl : NULL;
}

static bool matches(const scxml_cnet_egress_impl *impl,
                    cnet_connection connection) {
    return impl->bound &&
           impl->connection.slot == connection.slot &&
           impl->connection.generation == connection.generation;
}

static void set_failure(scxml_cnet_egress_impl *impl, int status) {
    if (!impl->failed) {
        impl->failed = true;
        impl->first_error = status;
    }
}

static void release_row(scxml_cnet_egress_impl *impl,
                        scxml_cnet_egress_row *row) {
    if (row->state == SCXML_CNET_ROW_FREE || impl->pending == 0u) {
        ++impl->invariant_failures;
        return;
    }
    memset(row->bytes, 0, sizeof(row->bytes));
    row->state = SCXML_CNET_ROW_FREE;
    row->size = 0u;
    row->sequence = 0u;
    --impl->pending;
}

static scxml_cnet_egress_row *earliest(scxml_cnet_egress_impl *impl,
                                      scxml_cnet_egress_row_state state) {
    scxml_cnet_egress_row *selected = NULL;
    size_t i;
    for (i = 0u; i < impl->capacity; ++i) {
        scxml_cnet_egress_row *row = &impl->rows[i];
        if (row->state == state &&
            (selected == NULL || row->sequence < selected->sequence))
            selected = row;
    }
    return selected;
}

/*
 * commit/discard run on the CFlow SerialExecutor. No I/O is attempted here:
 * CNet calls must occur on the one owner lane, via the explicit pump.
 */
static void ticket_commit(void *user) {
    scxml_cnet_egress_row *row = (scxml_cnet_egress_row *)user;
    scxml_cnet_egress_impl *impl;
    if (row == NULL || row->owner == NULL) return;
    impl = row->owner;
    cmeta_mutex_lock(&impl->lock);
    if (row->state != SCXML_CNET_ROW_RESERVED) {
        ++impl->invariant_failures;
    } else if (impl->closed || impl->terminal) {
        ++impl->cancelled;
        release_row(impl, row);
    } else {
        row->state = SCXML_CNET_ROW_READY;
        row->sequence = ++impl->next_sequence;
        ++impl->committed;
    }
    cmeta_mutex_unlock(&impl->lock);
}

static void ticket_discard(void *user) {
    scxml_cnet_egress_row *row = (scxml_cnet_egress_row *)user;
    scxml_cnet_egress_impl *impl;
    if (row == NULL || row->owner == NULL) return;
    impl = row->owner;
    cmeta_mutex_lock(&impl->lock);
    if (row->state != SCXML_CNET_ROW_RESERVED) {
        ++impl->invariant_failures;
    } else {
        ++impl->discarded;
        release_row(impl, row);
    }
    cmeta_mutex_unlock(&impl->lock);
}

static scxml_adapter_status prepare_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    static const char unsupported_type[] =
        "CNet raw-byte profile requires explicit urn:turboscxml:cnet-raw:1 type";
    static const char unsupported_target[] =
        "CNet raw-byte profile requires cnet://bound target";
    static const char unsupported_payload[] =
        "CNet raw-byte profile accepts only event-name or TEXT_UTF8 content bytes";
    scxml_cnet_egress_impl *impl = (scxml_cnet_egress_impl *)user;
    const unsigned char *bytes = NULL;
    size_t size = 0u;
    scxml_cnet_egress_row *row = NULL;
    size_t i;
    if (out_ticket != NULL) memset(out_ticket, 0, sizeof(*out_ticket));
    if (out_error != NULL) *out_error = NULL;
    if (impl == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL ||
        (request->event_size != 0u && request->event == NULL) ||
        (request->target_size != 0u && request->target == NULL) ||
        (request->type_size != 0u && request->type == NULL) ||
        (request->id_size != 0u && request->id == NULL))
        return SCXML_ADAPTER_INVALID_CONTRACT;

    if (request->type_size != sizeof(SCXML_CNET_RAW_PROCESSOR_URI) - 1u ||
        memcmp(request->type, SCXML_CNET_RAW_PROCESSOR_URI,
               sizeof(SCXML_CNET_RAW_PROCESSOR_URI) - 1u) != 0) {
        *out_error = unsupported_type;
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    if (request->target_size != sizeof(SCXML_CNET_BOUND_TARGET) - 1u ||
        memcmp(request->target, SCXML_CNET_BOUND_TARGET,
               sizeof(SCXML_CNET_BOUND_TARGET) - 1u) != 0) {
        *out_error = unsupported_target;
        return SCXML_ADAPTER_ERROR_COMMUNICATION;
    }
    if (request->delay_ms != 0u) {
        *out_error = "CNet raw-byte profile does not implement delayed sends";
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    if (request->payload.kind == SCXML_PAYLOAD_NONE) {
        bytes = (const unsigned char *)request->event;
        size = request->event_size;
    } else if (request->payload.kind == SCXML_PAYLOAD_CONTENT &&
               request->payload.content.kind == SCXML_CONTENT_TEXT_UTF8) {
        bytes = (const unsigned char *)request->payload.content.bytes;
        size = request->payload.content.byte_count;
    } else {
        *out_error = unsupported_payload;
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    if (bytes == NULL || size == 0u || size > impl->max_payload_bytes) {
        *out_error = unsupported_payload;
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }

    cmeta_mutex_lock(&impl->lock);
    if (impl->closed || impl->terminal || impl->failed) {
        cmeta_mutex_unlock(&impl->lock);
        return SCXML_ADAPTER_CLOSED;
    }
    if (impl->pending == impl->capacity) {
        ++impl->rejected_full;
        cmeta_mutex_unlock(&impl->lock);
        return SCXML_ADAPTER_FULL;
    }
    for (i = 0u; i < impl->capacity; ++i) {
        if (impl->rows[i].state == SCXML_CNET_ROW_FREE) {
            row = &impl->rows[i];
            break;
        }
    }
    if (row == NULL) {
        ++impl->invariant_failures;
        cmeta_mutex_unlock(&impl->lock);
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    row->size = size;
    memcpy(row->bytes, bytes, size);
    row->state = SCXML_CNET_ROW_RESERVED;
    row->sequence = 0u;
    ++impl->prepared;
    ++impl->pending;
    if (impl->pending > impl->high_water)
        impl->high_water = impl->pending;
    *out_ticket = (cflow_statechart_effect_ticket){
        ticket_commit, ticket_discard, row
    };
    cmeta_mutex_unlock(&impl->lock);
    return SCXML_ADAPTER_ACCEPTED;
}

static void observe_state(void *user, cnet_connection connection,
                          cnet_connection_state state, const cnet_error *error) {
    scxml_cnet_egress_impl *impl = (scxml_cnet_egress_impl *)user;
    size_t i;
    if (impl == NULL) return;
    cmeta_mutex_lock(&impl->lock);
    if (!matches(impl, connection)) {
        ++impl->stale_callbacks;
        cmeta_mutex_unlock(&impl->lock);
        return;
    }
    if (state == CNET_CONNECTION_CONNECTED) {
        impl->connected = true;
    } else if (state == CNET_CONNECTION_CLOSING) {
        impl->connected = false;
    } else if (state == CNET_CONNECTION_CLOSED ||
               state == CNET_CONNECTION_FAILED) {
        impl->connected = false;
        impl->terminal = true;
        if (state == CNET_CONNECTION_FAILED)
            set_failure(impl,
                error != NULL && error->status != SALTS_OK
                    ? error->status : SALTS_EIO);
        for (i = 0u; i < impl->capacity; ++i) {
            scxml_cnet_egress_row *row = &impl->rows[i];
            if (row->state == SCXML_CNET_ROW_READY ||
                row->state == SCXML_CNET_ROW_SUBMITTED) {
                ++impl->cancelled;
                release_row(impl, row);
            }
            /* A concurrent pumping CNet owner is forbidden by CNet itself.
               RESERVED tickets and SUBMITTING command work remain borrowed. */
        }
    }
    cmeta_mutex_unlock(&impl->lock);
}

static void observe_send(void *user, cnet_connection connection, size_t size) {
    scxml_cnet_egress_impl *impl = (scxml_cnet_egress_impl *)user;
    scxml_cnet_egress_row *row;
    if (impl == NULL) return;
    cmeta_mutex_lock(&impl->lock);
    if (!matches(impl, connection) || impl->terminal) {
        ++impl->stale_callbacks;
        cmeta_mutex_unlock(&impl->lock);
        return;
    }
    row = earliest(impl, SCXML_CNET_ROW_SUBMITTED);
    if (row == NULL) {
        ++impl->invariant_failures;
        set_failure(impl, SALTS_EPROTO);
    } else {
        if (row->size == size) {
            ++impl->completed;
        } else {
            set_failure(impl, SALTS_EPROTO);
        }
        release_row(impl, row);
    }
    cmeta_mutex_unlock(&impl->lock);
}

static void adapter_close(void *user) {
    scxml_cnet_egress_impl *impl = (scxml_cnet_egress_impl *)user;
    size_t i;
    if (impl == NULL) return;
    cmeta_mutex_lock(&impl->lock);
    if (!impl->closed) {
        impl->closed = true;
        for (i = 0u; i < impl->capacity; ++i) {
            if (impl->rows[i].state == SCXML_CNET_ROW_READY) {
                ++impl->cancelled;
                release_row(impl, &impl->rows[i]);
            }
        }
    }
    cmeta_mutex_unlock(&impl->lock);
}

static bool adapter_quiescent(void *user) {
    scxml_cnet_egress_impl *impl = (scxml_cnet_egress_impl *)user;
    bool ok;
    if (impl == NULL) return false;
    cmeta_mutex_lock(&impl->lock);
    ok = impl->closed && impl->pending == 0u && !impl->pumping &&
         (!impl->bound || impl->terminal);
    cmeta_mutex_unlock(&impl->lock);
    return ok;
}

static const scxml_event_io_adapter EGRESS_ADAPTER = {
    .abi_version = SCXML_ADAPTER_ABI,
    .struct_size = sizeof(scxml_event_io_adapter),
    .capabilities = SCXML_EVENT_IO_CAP_SEND | SCXML_EVENT_IO_CAP_CONTENT,
    .prepare_send = prepare_send,
    .prepare_cancel = NULL,
    .close = adapter_close,
    .is_quiescent = adapter_quiescent
};

int scxml_cnet_egress_init(scxml_cnet_egress *egress,
                           const scxml_cnet_egress_config *config) {
    scxml_cnet_egress_impl *impl;
    size_t i;
    if (egress == NULL || egress->impl != NULL || config == NULL ||
        config->client == NULL || config->capacity == 0u ||
        config->capacity > SIZE_MAX / sizeof(scxml_cnet_egress_row) ||
        config->max_payload_bytes == 0u ||
        config->max_payload_bytes > SCXML_EVENT_METADATA_CAPACITY)
        return SALTS_EINVAL;
    impl = (scxml_cnet_egress_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return SALTS_ENOMEM;
    impl->rows = (scxml_cnet_egress_row *)calloc(
        config->capacity, sizeof(*impl->rows));
    if (impl->rows == NULL) {
        free(impl);
        return SALTS_ENOMEM;
    }
    cmeta_mutex_init(&impl->lock);
    impl->client = config->client;
    impl->capacity = config->capacity;
    impl->max_payload_bytes = config->max_payload_bytes;
    for (i = 0u; i < config->capacity; ++i)
        impl->rows[i].owner = impl;
    egress->impl = impl;
    return SALTS_OK;
}

cnet_observer scxml_cnet_egress_observer(scxml_cnet_egress *egress) {
    scxml_cnet_egress_impl *impl = impl_of(egress);
    cnet_observer observer = {0};
    if (impl != NULL) {
        observer.on_state = observe_state;
        observer.on_send = observe_send;
        observer.user = impl;
    }
    return observer;
}

int scxml_cnet_egress_bind(scxml_cnet_egress *egress,
                           cnet_connection connection) {
    scxml_cnet_egress_impl *impl = impl_of(egress);
    if (impl == NULL || connection.generation == 0u) return SALTS_EINVAL;
    cmeta_mutex_lock(&impl->lock);
    if (impl->bound || impl->closed) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_EBUSY;
    }
    impl->connection = connection;
    impl->bound = true;
    cmeta_mutex_unlock(&impl->lock);
    return SALTS_OK;
}

const scxml_event_io_adapter *scxml_cnet_egress_adapter(void) {
    return &EGRESS_ADAPTER;
}

void *scxml_cnet_egress_adapter_user(scxml_cnet_egress *egress) {
    return impl_of(egress);
}

/*
 * The CNet owner drives progress. An owned copy is made while the row is
 * protected, then CNet retains one buffer reference through its own native
 * terminal. The row is retained until on_send; transient command admission
 * returns READY without an implicit retry loop.
 */
int scxml_cnet_egress_pump(
    scxml_cnet_egress *egress, size_t max_writes, size_t *out_submitted) {
    scxml_cnet_egress_impl *impl = impl_of(egress);
    size_t completed = 0u;
    int result = SALTS_OK;
    if (out_submitted != NULL) *out_submitted = 0u;
    if (impl == NULL || max_writes == 0u || out_submitted == NULL)
        return SALTS_EINVAL;
    cmeta_mutex_lock(&impl->lock);
    if (impl->pumping) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_EBUSY;
    }
    if (impl->closed || impl->terminal || impl->failed) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ESHUTDOWN;
    }
    if (!impl->bound || !impl->connected) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ENOTCONN;
    }
    impl->pumping = true;
    cmeta_mutex_unlock(&impl->lock);

    while (completed < max_writes) {
        scxml_cnet_egress_row *row;
        unsigned char copy[SCXML_EVENT_METADATA_CAPACITY];
        size_t size;
        mem_buffer_t *buffer;
        int status;

        cmeta_mutex_lock(&impl->lock);
        row = earliest(impl, SCXML_CNET_ROW_READY);
        if (row == NULL || impl->closed || impl->terminal) {
            cmeta_mutex_unlock(&impl->lock);
            break;
        }
        size = row->size;
        memcpy(copy, row->bytes, size);
        row->state = SCXML_CNET_ROW_SUBMITTING;
        cmeta_mutex_unlock(&impl->lock);

        buffer = mem_get_buffer(mem_global(), size);
        if (buffer == NULL) {
            cmeta_mutex_lock(&impl->lock);
            if (impl->closed) {
                ++impl->cancelled;
                release_row(impl, row);
            } else {
                row->state = SCXML_CNET_ROW_READY;
            }
            cmeta_mutex_unlock(&impl->lock);
            result = SALTS_ENOMEM;
            break;
        }
        memcpy(mem_buffer_data(buffer), copy, size);
        mem_set_used(buffer, size);

        cmeta_mutex_lock(&impl->lock);
        if (impl->closed || impl->terminal) {
            ++impl->cancelled;
            release_row(impl, row);
            cmeta_mutex_unlock(&impl->lock);
            mem_buffer_release(buffer);
            break;
        }
        cmeta_mutex_unlock(&impl->lock);

        status = cnet_send_buffer(impl->client, impl->connection, buffer);
        mem_buffer_release(buffer);
        cmeta_mutex_lock(&impl->lock);
        if (row->state != SCXML_CNET_ROW_SUBMITTING) {
            ++impl->invariant_failures;
            set_failure(impl, SALTS_EPROTO);
            result = SALTS_EPROTO;
        } else if (status == SALTS_OK) {
            row->state = SCXML_CNET_ROW_SUBMITTED;
            ++impl->submitted;
            ++completed;
        } else if (impl->closed || impl->terminal) {
            ++impl->cancelled;
            release_row(impl, row);
            result = status;
        } else if (status == SALTS_ENOBUFS || status == SALTS_EBUSY) {
            ++impl->transient_full;
            row->state = SCXML_CNET_ROW_READY;
            result = status;
        } else {
            set_failure(impl, status);
            release_row(impl, row);
            result = status;
        }
        cmeta_mutex_unlock(&impl->lock);
        if (status != SALTS_OK) break;
    }

    cmeta_mutex_lock(&impl->lock);
    impl->pumping = false;
    cmeta_mutex_unlock(&impl->lock);
    *out_submitted = completed;
    return result;
}

bool scxml_cnet_egress_get_stats(
    const scxml_cnet_egress *egress, scxml_cnet_egress_stats *out) {
    scxml_cnet_egress_impl *impl = egress != NULL
        ? (scxml_cnet_egress_impl *)egress->impl : NULL;
    if (impl == NULL || out == NULL) return false;
    cmeta_mutex_lock(&impl->lock);
    *out = (scxml_cnet_egress_stats){
        .connection = impl->connection,
        .capacity = impl->capacity,
        .pending = impl->pending,
        .high_water = impl->high_water,
        .bound = impl->bound,
        .connected = impl->connected,
        .terminal = impl->terminal,
        .closed = impl->closed,
        .failed = impl->failed,
        .first_error = impl->first_error,
        .prepared = impl->prepared,
        .committed = impl->committed,
        .discarded = impl->discarded,
        .submitted = impl->submitted,
        .completed = impl->completed,
        .cancelled = impl->cancelled,
        .rejected_full = impl->rejected_full,
        .transient_full = impl->transient_full,
        .stale_callbacks = impl->stale_callbacks,
        .invariant_failures = impl->invariant_failures
    };
    cmeta_mutex_unlock(&impl->lock);
    return true;
}

int scxml_cnet_egress_close(scxml_cnet_egress *egress) {
    scxml_cnet_egress_impl *impl = impl_of(egress);
    bool already_closed;
    if (impl == NULL) return SALTS_EINVAL;
    cmeta_mutex_lock(&impl->lock);
    already_closed = impl->closed;
    cmeta_mutex_unlock(&impl->lock);
    if (already_closed) return SALTS_EALREADY;
    adapter_close(impl);
    return SALTS_OK;
}

bool scxml_cnet_egress_is_quiescent(const scxml_cnet_egress *egress) {
    scxml_cnet_egress_impl *impl = egress != NULL
        ? (scxml_cnet_egress_impl *)egress->impl : NULL;
    return adapter_quiescent(impl);
}

int scxml_cnet_egress_destroy(scxml_cnet_egress *egress) {
    scxml_cnet_egress_impl *impl = impl_of(egress);
    if (egress == NULL) return SALTS_EINVAL;
    if (impl == NULL) return SALTS_OK;
    if (!adapter_quiescent(impl)) return SALTS_EBUSY;
    cmeta_mutex_destroy(&impl->lock);
    free(impl->rows);
    free(impl);
    egress->impl = NULL;
    return SALTS_OK;
}
