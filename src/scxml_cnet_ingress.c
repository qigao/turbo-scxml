#include <scxml/cnet_ingress.h>

#include <salts/error_codes.h>

#include <stdlib.h>
#include <string.h>

typedef struct scxml_cnet_chunk {
    size_t hex_size;
    char hex[SCXML_EVENT_METADATA_CAPACITY + 1u];
} scxml_cnet_chunk;

typedef struct scxml_cnet_ingress_impl {
    cnet_client *client;
    scxml_session *session;
    cnet_connection connection;
    scxml_cnet_chunk *rows;
    size_t capacity;
    size_t max_chunk_bytes;
    size_t head;
    size_t pending;
    size_t peak_pending;
    size_t event_name_size;
    char event_name[SCXML_EVENT_METADATA_CAPACITY + 1u];
    bool bound;
    bool connected;
    bool armed;
    bool closed;
    bool terminal;
    bool failed;
    int first_error;
    uint64_t received;
    uint64_t delivered;
    uint64_t cancelled;
    uint64_t rejected_full;
    uint64_t rejected_oversize;
    uint64_t stale_callbacks;
} scxml_cnet_ingress_impl;

static scxml_cnet_ingress_impl *ingress_impl(scxml_cnet_ingress *value) {
    return value != NULL ? (scxml_cnet_ingress_impl *)value->impl : NULL;
}

static bool connection_matches(
    const scxml_cnet_ingress_impl *impl, cnet_connection connection) {
    return impl->bound &&
        impl->connection.slot == connection.slot &&
        impl->connection.generation == connection.generation;
}

static void mark_failure(scxml_cnet_ingress_impl *impl, int error) {
    if (!impl->failed) {
        impl->failed = true;
        impl->first_error = error;
    }
}

static void observe_state(void *user, cnet_connection connection,
                          cnet_connection_state state, const cnet_error *error) {
    scxml_cnet_ingress_impl *impl = (scxml_cnet_ingress_impl *)user;
    if (impl == NULL) return;
    if (!connection_matches(impl, connection)) {
        ++impl->stale_callbacks;
        return;
    }
    if (state == CNET_CONNECTION_CONNECTED) {
        impl->connected = true;
    } else if (state == CNET_CONNECTION_CLOSING) {
        impl->connected = false;
    } else if (state == CNET_CONNECTION_CLOSED ||
               state == CNET_CONNECTION_FAILED) {
        /* Only CNet may establish the transport-terminal fact. */
        impl->connected = false;
        impl->armed = false;
        impl->terminal = true;
        if (state == CNET_CONNECTION_FAILED)
            mark_failure(impl,
                error != NULL && error->status != SALTS_OK
                    ? error->status : SALTS_EIO);
    }
}

static void observe_receive(void *user, cnet_connection connection,
                            const cnet_receive_view *view) {
    static const char digits[] = "0123456789abcdef";
    scxml_cnet_ingress_impl *impl = (scxml_cnet_ingress_impl *)user;
    scxml_cnet_chunk *row;
    size_t i;
    size_t index;

    if (impl == NULL) return;
    if (!connection_matches(impl, connection)) {
        ++impl->stale_callbacks;
        return;
    }
    if (!impl->armed) {
        mark_failure(impl, SALTS_EPROTO);
        return;
    }
    impl->armed = false;
    if (impl->closed) {
        /* Explicit cancellation, not a successfully admitted SCXML Event. */
        ++impl->cancelled;
        return;
    }
    if (view == NULL || view->kind != CNET_MESSAGE_BYTES ||
        (view->size != 0u && view->data == NULL)) {
        mark_failure(impl, SALTS_EPROTO);
        return;
    }
    if (view->size > impl->max_chunk_bytes) {
        ++impl->rejected_oversize;
        mark_failure(impl, SALTS_EMSGSIZE);
        return;
    }
    if (impl->pending >= impl->capacity) {
        ++impl->rejected_full;
        mark_failure(impl, SALTS_ENOBUFS);
        return;
    }

    index = (impl->head + impl->pending) % impl->capacity;
    row = &impl->rows[index];
    for (i = 0u; i < view->size; ++i) {
        const unsigned char byte = ((const unsigned char *)view->data)[i];
        row->hex[2u * i] = digits[byte >> 4u];
        row->hex[2u * i + 1u] = digits[byte & 0x0fu];
    }
    row->hex_size = view->size * 2u;
    row->hex[row->hex_size] = '\0';
    ++impl->pending;
    ++impl->received;
    if (impl->pending > impl->peak_pending)
        impl->peak_pending = impl->pending;
}

int scxml_cnet_ingress_init(scxml_cnet_ingress *ingress,
                            const scxml_cnet_ingress_config *config) {
    scxml_cnet_ingress_impl *impl;
    if (ingress == NULL || ingress->impl != NULL || config == NULL ||
        config->client == NULL || config->session == NULL ||
        config->session->impl == NULL || config->event_name == NULL ||
        config->event_name_size == 0u ||
        config->event_name_size > SCXML_EVENT_METADATA_CAPACITY ||
        memchr(config->event_name, '\0', config->event_name_size) != NULL ||
        config->capacity == 0u ||
        config->capacity > SIZE_MAX / sizeof(scxml_cnet_chunk) ||
        config->max_chunk_bytes == 0u ||
        config->max_chunk_bytes > SCXML_EVENT_METADATA_CAPACITY / 2u)
        return SALTS_EINVAL;

    impl = (scxml_cnet_ingress_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return SALTS_ENOMEM;
    impl->rows = (scxml_cnet_chunk *)calloc(
        config->capacity, sizeof(*impl->rows));
    if (impl->rows == NULL) {
        free(impl);
        return SALTS_ENOMEM;
    }
    impl->client = config->client;
    impl->session = config->session;
    impl->capacity = config->capacity;
    impl->max_chunk_bytes = config->max_chunk_bytes;
    impl->event_name_size = config->event_name_size;
    memcpy(impl->event_name, config->event_name, config->event_name_size);
    impl->event_name[config->event_name_size] = '\0';
    ingress->impl = impl;
    return SALTS_OK;
}

cnet_observer scxml_cnet_ingress_observer(scxml_cnet_ingress *ingress) {
    cnet_observer observer = {0};
    scxml_cnet_ingress_impl *impl = ingress_impl(ingress);
    if (impl != NULL) {
        observer.on_state = observe_state;
        observer.on_receive = observe_receive;
        observer.user = impl;
    }
    return observer;
}

int scxml_cnet_ingress_bind(scxml_cnet_ingress *ingress,
                            cnet_connection connection) {
    scxml_cnet_ingress_impl *impl = ingress_impl(ingress);
    if (impl == NULL || connection.generation == 0u) return SALTS_EINVAL;
    if (impl->bound || impl->closed) return SALTS_EBUSY;
    impl->connection = connection;
    impl->bound = true;
    return SALTS_OK;
}

int scxml_cnet_ingress_arm(scxml_cnet_ingress *ingress) {
    scxml_cnet_ingress_impl *impl = ingress_impl(ingress);
    int status;
    if (impl == NULL || !impl->bound) return SALTS_EINVAL;
    if (impl->closed || impl->terminal) return SALTS_ESHUTDOWN;
    if (impl->failed) return impl->first_error;
    if (!impl->connected) return SALTS_ENOTCONN;
    if (impl->armed) return SALTS_EBUSY;
    if (impl->pending == impl->capacity) {
        ++impl->rejected_full;
        return SALTS_ENOBUFS;
    }
    impl->armed = true;
    status = cnet_receive(impl->client, impl->connection, 1u);
    if (status != SALTS_OK) impl->armed = false;
    return status;
}

cflow_mailbox_status scxml_cnet_ingress_drain(
    scxml_cnet_ingress *ingress, size_t max_events, size_t *out_delivered) {
    scxml_cnet_ingress_impl *impl = ingress_impl(ingress);
    size_t completed = 0u;
    if (out_delivered != NULL) *out_delivered = 0u;
    if (impl == NULL || max_events == 0u || out_delivered == NULL)
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    if (impl->closed) return CFLOW_MAILBOX_CLOSED;
    if (impl->failed) return CFLOW_MAILBOX_INVALID_ARGUMENT;

    while (impl->pending != 0u && completed < max_events) {
        scxml_cnet_chunk *row = &impl->rows[impl->head];
        const scxml_event_metadata metadata = {
            .abi_version = SCXML_EVENT_METADATA_ABI,
            .struct_size = sizeof(scxml_event_metadata),
            .data = {
                .kind = SCXML_CONTENT_TEXT_UTF8,
                .bytes = row->hex,
                .byte_count = row->hex_size
            }
        };
        const cflow_mailbox_status status =
            scxml_session_try_send_named_with_metadata(
                impl->session, impl->event_name, impl->event_name_size,
                &metadata);
        if (status != CFLOW_MAILBOX_OK) {
            if (status != CFLOW_MAILBOX_FULL)
                mark_failure(impl, SALTS_EPROTO);
            *out_delivered = completed;
            return status;
        }
        /* Success copied metadata and Event before the row is recycled. */
        memset(row, 0, sizeof(*row));
        impl->head = (impl->head + 1u) % impl->capacity;
        --impl->pending;
        ++impl->delivered;
        ++completed;
    }
    *out_delivered = completed;
    return completed != 0u ? CFLOW_MAILBOX_OK : CFLOW_MAILBOX_EMPTY;
}

bool scxml_cnet_ingress_get_stats(
    const scxml_cnet_ingress *ingress, scxml_cnet_ingress_stats *out) {
    const scxml_cnet_ingress_impl *impl = ingress != NULL
        ? (const scxml_cnet_ingress_impl *)ingress->impl : NULL;
    if (impl == NULL || out == NULL) return false;
    *out = (scxml_cnet_ingress_stats){
        .connection = impl->connection,
        .capacity = impl->capacity,
        .pending = impl->pending,
        .peak_pending = impl->peak_pending,
        .bound = impl->bound,
        .connected = impl->connected,
        .receive_armed = impl->armed,
        .closed = impl->closed,
        .terminal = impl->terminal,
        .failed = impl->failed,
        .first_error = impl->first_error,
        .received = impl->received,
        .delivered = impl->delivered,
        .cancelled = impl->cancelled,
        .rejected_full = impl->rejected_full,
        .rejected_oversize = impl->rejected_oversize,
        .stale_callbacks = impl->stale_callbacks
    };
    return true;
}

int scxml_cnet_ingress_close(scxml_cnet_ingress *ingress) {
    scxml_cnet_ingress_impl *impl = ingress_impl(ingress);
    if (impl == NULL) return SALTS_EINVAL;
    if (impl->closed) return SALTS_EALREADY;
    impl->closed = true;
    impl->cancelled += impl->pending;
    memset(impl->rows, 0, impl->capacity * sizeof(*impl->rows));
    impl->pending = 0u;
    impl->head = 0u;
    return SALTS_OK;
}

bool scxml_cnet_ingress_is_quiescent(const scxml_cnet_ingress *ingress) {
    const scxml_cnet_ingress_impl *impl = ingress != NULL
        ? (const scxml_cnet_ingress_impl *)ingress->impl : NULL;
    return impl != NULL && impl->closed && !impl->armed &&
           impl->pending == 0u && (!impl->bound || impl->terminal);
}

int scxml_cnet_ingress_destroy(scxml_cnet_ingress *ingress) {
    scxml_cnet_ingress_impl *impl = ingress_impl(ingress);
    if (ingress == NULL) return SALTS_EINVAL;
    if (impl == NULL) return SALTS_OK;
    if (!scxml_cnet_ingress_is_quiescent(ingress)) return SALTS_EBUSY;
    free(impl->rows);
    free(impl);
    ingress->impl = NULL;
    return SALTS_OK;
}
