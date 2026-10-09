#include <scxml/cnet_frame_ingress.h>

#include <salts/error_codes.h>

#include <stdlib.h>
#include <string.h>

typedef struct scxml_cnet_frame_ingress_impl {
    cnet_client *client;
    scxml_host_router *router;
    scxml_host_session_ref target;
    cnet_connection connection;
    unsigned char *chunk;
    unsigned char *frame;
    size_t max_receive_bytes;
    size_t max_frame_bytes;
    size_t chunk_size;
    size_t chunk_pos;
    size_t frame_size;
    size_t frame_expected;
    uint8_t header[2];
    size_t header_size;
    char event_name[SCXML_EVENT_METADATA_CAPACITY + 1u];
    size_t event_name_size;
    bool bound;
    bool connected;
    bool armed;
    bool terminal;
    bool closed;
    bool failed;
    int first_error;
    uint64_t received_chunks;
    uint64_t accepted_frames;
    uint64_t rejected_frames;
    uint64_t host_full;
    uint64_t cancelled_bytes;
    uint64_t stale_callbacks;
} scxml_cnet_frame_ingress_impl;

static scxml_cnet_frame_ingress_impl *frame_impl(
    scxml_cnet_frame_ingress *ingress) {
    return ingress != NULL
        ? (scxml_cnet_frame_ingress_impl *)ingress->impl : NULL;
}

static bool matches(const scxml_cnet_frame_ingress_impl *impl,
                    cnet_connection connection) {
    return impl->bound &&
        impl->connection.slot == connection.slot &&
        impl->connection.generation == connection.generation;
}

static void mark_error(scxml_cnet_frame_ingress_impl *impl, int status) {
    if (!impl->failed) {
        impl->first_error = status;
        impl->failed = true;
        ++impl->rejected_frames;
    }
}

static void on_state(void *user, cnet_connection connection,
                     cnet_connection_state state, const cnet_error *error) {
    scxml_cnet_frame_ingress_impl *impl =
        (scxml_cnet_frame_ingress_impl *)user;
    if (impl == NULL) return;
    if (!matches(impl, connection)) {
        ++impl->stale_callbacks;
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
        impl->armed = false;
        if (state == CNET_CONNECTION_FAILED)
            mark_error(impl,
                error != NULL && error->status != SALTS_OK
                    ? error->status : SALTS_EIO);
    }
}

static void on_receive(void *user, cnet_connection connection,
                       const cnet_receive_view *view) {
    scxml_cnet_frame_ingress_impl *impl =
        (scxml_cnet_frame_ingress_impl *)user;
    if (impl == NULL) return;
    if (!matches(impl, connection)) {
        ++impl->stale_callbacks;
        return;
    }
    if (!impl->armed) {
        mark_error(impl, SALTS_EPROTO);
        return;
    }
    impl->armed = false;
    if (impl->closed) {
        if (view != NULL) impl->cancelled_bytes += view->size;
        return;
    }
    if (view == NULL || view->kind != CNET_MESSAGE_BYTES ||
        view->size == 0u || view->data == NULL) {
        mark_error(impl, SALTS_EPROTO);
        return;
    }
    if (view->size > impl->max_receive_bytes) {
        mark_error(impl, SALTS_EMSGSIZE);
        return;
    }
    if (impl->chunk_pos != impl->chunk_size ||
        impl->chunk_size != 0u) {
        mark_error(impl, SALTS_EBUSY);
        return;
    }
    memcpy(impl->chunk, view->data, view->size);
    impl->chunk_size = view->size;
    impl->chunk_pos = 0u;
    ++impl->received_chunks;
}

int scxml_cnet_frame_ingress_init(
    scxml_cnet_frame_ingress *ingress,
    const scxml_cnet_frame_ingress_config *config) {
    scxml_cnet_frame_ingress_impl *impl;
    if (ingress == NULL || ingress->impl != NULL || config == NULL ||
        config->client == NULL || config->router == NULL ||
        config->target.generation == 0u ||
        config->event_name == NULL || config->event_name_size == 0u ||
        config->event_name_size > SCXML_EVENT_METADATA_CAPACITY ||
        memchr(config->event_name, '\0', config->event_name_size) != NULL ||
        config->max_frame_bytes == 0u ||
        config->max_frame_bytes > SCXML_EVENT_METADATA_CAPACITY / 2u ||
        config->max_receive_bytes == 0u ||
        config->max_receive_bytes > SCXML_EVENT_DATA_CAPACITY)
        return SALTS_EINVAL;
    impl = (scxml_cnet_frame_ingress_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return SALTS_ENOMEM;
    impl->chunk = (unsigned char *)malloc(config->max_receive_bytes);
    impl->frame = (unsigned char *)malloc(config->max_frame_bytes);
    if (impl->chunk == NULL || impl->frame == NULL) {
        free(impl->chunk);
        free(impl->frame);
        free(impl);
        return SALTS_ENOMEM;
    }
    impl->client = config->client;
    impl->router = config->router;
    impl->target = config->target;
    impl->max_frame_bytes = config->max_frame_bytes;
    impl->max_receive_bytes = config->max_receive_bytes;
    impl->event_name_size = config->event_name_size;
    memcpy(impl->event_name, config->event_name, config->event_name_size);
    impl->event_name[config->event_name_size] = '\0';
    ingress->impl = impl;
    return SALTS_OK;
}

cnet_observer scxml_cnet_frame_ingress_observer(
    scxml_cnet_frame_ingress *ingress) {
    scxml_cnet_frame_ingress_impl *impl = frame_impl(ingress);
    cnet_observer observer = {0};
    if (impl != NULL) {
        observer.on_state = on_state;
        observer.on_receive = on_receive;
        observer.user = impl;
    }
    return observer;
}

int scxml_cnet_frame_ingress_bind(
    scxml_cnet_frame_ingress *ingress, cnet_connection connection) {
    scxml_cnet_frame_ingress_impl *impl = frame_impl(ingress);
    if (impl == NULL || connection.generation == 0u) return SALTS_EINVAL;
    if (impl->bound || impl->closed) return SALTS_EBUSY;
    impl->bound = true;
    impl->connection = connection;
    return SALTS_OK;
}

int scxml_cnet_frame_ingress_arm(scxml_cnet_frame_ingress *ingress) {
    scxml_cnet_frame_ingress_impl *impl = frame_impl(ingress);
    int status;
    if (impl == NULL || !impl->bound) return SALTS_EINVAL;
    if (impl->closed || impl->terminal) return SALTS_ESHUTDOWN;
    if (impl->failed) return impl->first_error;
    if (!impl->connected) return SALTS_ENOTCONN;
    if (impl->armed) return SALTS_EBUSY;
    /* Do not receive more data while a copied chunk/frame is outstanding. */
    if (impl->chunk_size != 0u ||
        (impl->frame_expected != 0u &&
         impl->frame_size == impl->frame_expected))
        return SALTS_ENOBUFS;
    impl->armed = true;
    status = cnet_receive(impl->client, impl->connection, 1u);
    if (status != SALTS_OK) impl->armed = false;
    return status;
}

static int publish_frame(scxml_cnet_frame_ingress_impl *impl) {
    static const char digits[] = "0123456789abcdef";
    char hex[SCXML_EVENT_METADATA_CAPACITY + 1u];
    size_t i;
    int status;
    for (i = 0u; i < impl->frame_expected; ++i) {
        const unsigned char c = impl->frame[i];
        hex[i * 2u] = digits[c >> 4u];
        hex[i * 2u + 1u] = digits[c & 0x0fu];
    }
    hex[impl->frame_expected * 2u] = '\0';
    status = scxml_host_router_enqueue(
        impl->router, impl->target, impl->event_name, impl->event_name_size,
        hex, impl->frame_expected * 2u);
    if (status == SALTS_OK) {
        ++impl->accepted_frames;
        memset(impl->frame, 0, impl->max_frame_bytes);
        impl->header_size = 0u;
        impl->frame_expected = 0u;
        impl->frame_size = 0u;
    } else if (status == SALTS_ENOBUFS) {
        ++impl->host_full;
    } else {
        mark_error(impl, status);
    }
    return status;
}

int scxml_cnet_frame_ingress_process(
    scxml_cnet_frame_ingress *ingress,
    size_t max_frames, size_t *out_accepted) {
    scxml_cnet_frame_ingress_impl *impl = frame_impl(ingress);
    size_t accepted = 0u;
    if (out_accepted != NULL) *out_accepted = 0u;
    if (impl == NULL || max_frames == 0u || out_accepted == NULL)
        return SALTS_EINVAL;
    if (impl->closed) return SALTS_ESHUTDOWN;
    if (impl->failed) return impl->first_error;

    while (accepted < max_frames) {
        if (impl->frame_expected != 0u &&
            impl->frame_size == impl->frame_expected) {
            const int status = publish_frame(impl);
            if (status != SALTS_OK) {
                *out_accepted = accepted;
                return status;
            }
            ++accepted;
            continue;
        }
        if (impl->chunk_pos == impl->chunk_size) {
            impl->chunk_size = 0u;
            impl->chunk_pos = 0u;
            break;
        }
        if (impl->header_size != 2u) {
            impl->header[impl->header_size++] =
                impl->chunk[impl->chunk_pos++];
            if (impl->header_size == 2u) {
                impl->frame_expected =
                    (size_t)impl->header[0] * 256u + impl->header[1];
                if (impl->frame_expected == 0u ||
                    impl->frame_expected > impl->max_frame_bytes) {
                    mark_error(impl, impl->frame_expected == 0u
                        ? SALTS_EPROTO : SALTS_EMSGSIZE);
                    *out_accepted = accepted;
                    return impl->first_error;
                }
            }
        } else {
            impl->frame[impl->frame_size++] =
                impl->chunk[impl->chunk_pos++];
        }
    }
    if (impl->chunk_pos == impl->chunk_size) {
        impl->chunk_size = 0u;
        impl->chunk_pos = 0u;
    }
    if (impl->terminal &&
        (impl->header_size != 0u || impl->frame_expected != 0u)) {
        mark_error(impl, SALTS_EPROTO);
        *out_accepted = accepted;
        return impl->first_error;
    }
    *out_accepted = accepted;
    return SALTS_OK;
}

bool scxml_cnet_frame_ingress_get_stats(
    const scxml_cnet_frame_ingress *ingress,
    scxml_cnet_frame_ingress_stats *out) {
    const scxml_cnet_frame_ingress_impl *impl = ingress != NULL
        ? (const scxml_cnet_frame_ingress_impl *)ingress->impl : NULL;
    if (impl == NULL || out == NULL) return false;
    *out = (scxml_cnet_frame_ingress_stats){
        .connection = impl->connection,
        .buffered_chunk_bytes = impl->chunk_size - impl->chunk_pos,
        .frame_bytes = impl->frame_size,
        .required_frame_bytes = impl->frame_expected,
        .bound = impl->bound,
        .connected = impl->connected,
        .armed = impl->armed,
        .terminal = impl->terminal,
        .closed = impl->closed,
        .failed = impl->failed,
        .first_error = impl->first_error,
        .received_chunks = impl->received_chunks,
        .accepted_frames = impl->accepted_frames,
        .rejected_frames = impl->rejected_frames,
        .host_full = impl->host_full,
        .cancelled_bytes = impl->cancelled_bytes,
        .stale_callbacks = impl->stale_callbacks
    };
    return true;
}

int scxml_cnet_frame_ingress_close(scxml_cnet_frame_ingress *ingress) {
    scxml_cnet_frame_ingress_impl *impl = frame_impl(ingress);
    if (impl == NULL) return SALTS_EINVAL;
    if (impl->closed) return SALTS_EALREADY;
    impl->closed = true;
    impl->cancelled_bytes += impl->chunk_size - impl->chunk_pos +
                             impl->frame_size + impl->header_size;
    memset(impl->chunk, 0, impl->max_receive_bytes);
    memset(impl->frame, 0, impl->max_frame_bytes);
    impl->chunk_size = 0u;
    impl->chunk_pos = 0u;
    impl->frame_size = 0u;
    impl->frame_expected = 0u;
    impl->header_size = 0u;
    return SALTS_OK;
}

bool scxml_cnet_frame_ingress_is_quiescent(
    const scxml_cnet_frame_ingress *ingress) {
    const scxml_cnet_frame_ingress_impl *impl = ingress != NULL
        ? (const scxml_cnet_frame_ingress_impl *)ingress->impl : NULL;
    return impl != NULL && impl->closed && !impl->armed &&
        (!impl->bound || impl->terminal);
}

int scxml_cnet_frame_ingress_destroy(scxml_cnet_frame_ingress *ingress) {
    scxml_cnet_frame_ingress_impl *impl = frame_impl(ingress);
    if (ingress == NULL) return SALTS_EINVAL;
    if (impl == NULL) return SALTS_OK;
    if (!scxml_cnet_frame_ingress_is_quiescent(ingress)) return SALTS_EBUSY;
    free(impl->chunk);
    free(impl->frame);
    free(impl);
    ingress->impl = NULL;
    return SALTS_OK;
}
