#include <scxml/host_router.h>

#include <salts/thread.h>

#include <stdlib.h>
#include <string.h>

typedef struct scxml_host_endpoint {
    scxml_session *session;
    uint32_t generation;
    bool live;
} scxml_host_endpoint;

typedef enum scxml_host_row_state {
    SCXML_HOST_ROW_FREE = 0,
    SCXML_HOST_ROW_RESERVED,
    SCXML_HOST_ROW_READY
} scxml_host_row_state;

typedef struct scxml_host_router_impl scxml_host_router_impl;

typedef struct scxml_host_row {
    scxml_host_router_impl *owner;
    scxml_host_row_state state;
    scxml_host_session_ref target;
    uint64_t sequence;
    size_t name_size;
    size_t text_size;
    char name[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char text[SCXML_EVENT_METADATA_CAPACITY + 1u];
} scxml_host_row;

struct scxml_host_router_impl {
    cmeta_mutex_t lock;
    scxml_host_endpoint *endpoints;
    scxml_host_row *rows;
    size_t endpoint_capacity;
    size_t event_capacity;
    size_t max_text_bytes;
    size_t endpoint_count;
    size_t pending;
    size_t reserved;
    size_t high_water;
    uint64_t next_sequence;
    uint64_t prepared;
    uint64_t committed;
    uint64_t discarded;
    uint64_t delivered;
    uint64_t cancelled;
    uint64_t rejected_full;
    uint64_t stale_refs;
    uint64_t delivery_errors;
    uint64_t invariant_failures;
    bool closed;
};

static scxml_host_router_impl *router_impl(scxml_host_router *router) {
    return router != NULL ? (scxml_host_router_impl *)router->impl : NULL;
}

static bool matches(const scxml_host_router_impl *impl,
                    scxml_host_session_ref target) {
    return target.generation != 0u &&
        (size_t)target.slot < impl->endpoint_capacity &&
        impl->endpoints[target.slot].live &&
        impl->endpoints[target.slot].generation == target.generation;
}

static bool valid_fields(const scxml_host_router_impl *impl,
                         const char *name, size_t name_size,
                         const char *text, size_t text_size) {
    return name != NULL && name_size != 0u &&
        name_size <= SCXML_EVENT_METADATA_CAPACITY &&
        memchr(name, '\0', name_size) == NULL &&
        text_size <= impl->max_text_bytes &&
        (text_size == 0u || text != NULL);
}

static void release_locked(scxml_host_router_impl *impl, scxml_host_row *row) {
    if (row->state == SCXML_HOST_ROW_FREE || impl->pending == 0u) {
        ++impl->invariant_failures;
        return;
    }
    if (row->state == SCXML_HOST_ROW_RESERVED) {
        if (impl->reserved != 0u) --impl->reserved;
        else ++impl->invariant_failures;
    }
    memset(row->name, 0, sizeof(row->name));
    memset(row->text, 0, sizeof(row->text));
    row->target = (scxml_host_session_ref){0};
    row->sequence = 0u;
    row->name_size = 0u;
    row->text_size = 0u;
    row->state = SCXML_HOST_ROW_FREE;
    --impl->pending;
}

static scxml_host_row *reserve_locked(scxml_host_router_impl *impl,
                                     scxml_host_session_ref target,
                                     const char *name, size_t name_size,
                                     const char *text, size_t text_size) {
    scxml_host_row *row = NULL;
    size_t i;
    if (impl->pending == impl->event_capacity) {
        ++impl->rejected_full;
        return NULL;
    }
    for (i = 0u; i < impl->event_capacity; ++i) {
        if (impl->rows[i].state == SCXML_HOST_ROW_FREE) {
            row = &impl->rows[i];
            break;
        }
    }
    if (row == NULL) {
        ++impl->invariant_failures;
        return NULL;
    }
    row->state = SCXML_HOST_ROW_RESERVED;
    row->target = target;
    row->name_size = name_size;
    row->text_size = text_size;
    memcpy(row->name, name, name_size);
    row->name[name_size] = '\0';
    if (text_size != 0u) memcpy(row->text, text, text_size);
    row->text[text_size] = '\0';
    ++impl->pending;
    ++impl->reserved;
    if (impl->pending > impl->high_water) impl->high_water = impl->pending;
    return row;
}

static void commit_locked(scxml_host_router_impl *impl, scxml_host_row *row) {
    if (row->state != SCXML_HOST_ROW_RESERVED) {
        ++impl->invariant_failures;
    } else if (impl->closed || !matches(impl, row->target) ||
               impl->next_sequence == UINT64_MAX) {
        ++impl->cancelled;
        release_locked(impl, row);
    } else {
        --impl->reserved;
        row->state = SCXML_HOST_ROW_READY;
        row->sequence = ++impl->next_sequence;
        ++impl->committed;
    }
}

static void ticket_commit(void *user) {
    scxml_host_row *row = (scxml_host_row *)user;
    scxml_host_router_impl *impl;
    if (row == NULL || row->owner == NULL) return;
    impl = row->owner;
    cmeta_mutex_lock(&impl->lock);
    commit_locked(impl, row);
    cmeta_mutex_unlock(&impl->lock);
}

static void ticket_discard(void *user) {
    scxml_host_row *row = (scxml_host_row *)user;
    scxml_host_router_impl *impl;
    if (row == NULL || row->owner == NULL) return;
    impl = row->owner;
    cmeta_mutex_lock(&impl->lock);
    if (row->state != SCXML_HOST_ROW_RESERVED) {
        ++impl->invariant_failures;
    } else {
        ++impl->discarded;
        release_locked(impl, row);
    }
    cmeta_mutex_unlock(&impl->lock);
}

int scxml_host_router_init(scxml_host_router *router,
                           const scxml_host_router_config *config) {
    scxml_host_router_impl *impl;
    size_t i;
    if (router == NULL || router->impl != NULL || config == NULL ||
        config->endpoint_capacity == 0u ||
        config->endpoint_capacity > UINT32_MAX ||
        config->endpoint_capacity > SIZE_MAX / sizeof(scxml_host_endpoint) ||
        config->event_capacity == 0u ||
        config->event_capacity > SIZE_MAX / sizeof(scxml_host_row) ||
        config->max_text_bytes == 0u ||
        config->max_text_bytes > SCXML_EVENT_METADATA_CAPACITY)
        return SALTS_EINVAL;
    impl = (scxml_host_router_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return SALTS_ENOMEM;
    impl->endpoints = (scxml_host_endpoint *)calloc(
        config->endpoint_capacity, sizeof(*impl->endpoints));
    impl->rows = (scxml_host_row *)calloc(
        config->event_capacity, sizeof(*impl->rows));
    if (impl->endpoints == NULL || impl->rows == NULL) {
        free(impl->endpoints);
        free(impl->rows);
        free(impl);
        return SALTS_ENOMEM;
    }
    cmeta_mutex_init(&impl->lock);
    if (impl->lock == NULL) {
        free(impl->endpoints);
        free(impl->rows);
        free(impl);
        return SALTS_ENOMEM;
    }
    impl->endpoint_capacity = config->endpoint_capacity;
    impl->event_capacity = config->event_capacity;
    impl->max_text_bytes = config->max_text_bytes;
    for (i = 0u; i < config->event_capacity; ++i) impl->rows[i].owner = impl;
    router->impl = impl;
    return SALTS_OK;
}

int scxml_host_router_attach(scxml_host_router *router,
                             scxml_session *session,
                             scxml_host_session_ref *out_ref) {
    scxml_host_router_impl *impl = router_impl(router);
    size_t i;
    if (out_ref != NULL) *out_ref = (scxml_host_session_ref){0};
    if (impl == NULL || session == NULL || session->impl == NULL ||
        out_ref == NULL)
        return SALTS_EINVAL;
    cmeta_mutex_lock(&impl->lock);
    if (impl->closed) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ESHUTDOWN;
    }
    for (i = 0u; i < impl->endpoint_capacity; ++i) {
        scxml_host_endpoint *endpoint = &impl->endpoints[i];
        if (!endpoint->live && endpoint->generation != UINT32_MAX) {
            /* Session identity is not a raw CFlow statechart pointer. */
            endpoint->live = true;
            endpoint->session = session;
            ++endpoint->generation;
            ++impl->endpoint_count;
            *out_ref = (scxml_host_session_ref){
                (uint32_t)i, endpoint->generation
            };
            cmeta_mutex_unlock(&impl->lock);
            return SALTS_OK;
        }
    }
    cmeta_mutex_unlock(&impl->lock);
    return SALTS_ENOBUFS;
}

int scxml_host_router_detach(scxml_host_router *router,
                             scxml_host_session_ref target) {
    scxml_host_router_impl *impl = router_impl(router);
    size_t i;
    if (impl == NULL) return SALTS_EINVAL;
    cmeta_mutex_lock(&impl->lock);
    if (!matches(impl, target)) {
        ++impl->stale_refs;
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ENOENT;
    }
    for (i = 0u; i < impl->event_capacity; ++i) {
        const scxml_host_row *row = &impl->rows[i];
        if (row->state != SCXML_HOST_ROW_FREE &&
            row->target.slot == target.slot &&
            row->target.generation == target.generation) {
            cmeta_mutex_unlock(&impl->lock);
            return SALTS_EBUSY;
        }
    }
    impl->endpoints[target.slot].live = false;
    impl->endpoints[target.slot].session = NULL;
    --impl->endpoint_count;
    cmeta_mutex_unlock(&impl->lock);
    return SALTS_OK;
}

int scxml_host_router_prepare(scxml_host_router *router,
                              scxml_host_session_ref target,
                              const char *name, size_t name_size,
                              const char *text, size_t text_size,
                              cflow_statechart_effect_ticket *out_ticket) {
    scxml_host_router_impl *impl = router_impl(router);
    scxml_host_row *row;
    if (out_ticket != NULL) *out_ticket = (cflow_statechart_effect_ticket){0};
    if (impl == NULL || out_ticket == NULL ||
        !valid_fields(impl, name, name_size, text, text_size))
        return SALTS_EINVAL;
    cmeta_mutex_lock(&impl->lock);
    if (impl->closed) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ESHUTDOWN;
    }
    if (!matches(impl, target)) {
        ++impl->stale_refs;
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ENOENT;
    }
    row = reserve_locked(impl, target, name, name_size, text, text_size);
    if (row == NULL) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ENOBUFS;
    }
    ++impl->prepared;
    *out_ticket = (cflow_statechart_effect_ticket){
        ticket_commit, ticket_discard, row
    };
    cmeta_mutex_unlock(&impl->lock);
    return SALTS_OK;
}

int scxml_host_router_enqueue(scxml_host_router *router,
                              scxml_host_session_ref target,
                              const char *name, size_t name_size,
                              const char *text, size_t text_size) {
    scxml_host_router_impl *impl = router_impl(router);
    scxml_host_row *row;
    if (impl == NULL || !valid_fields(impl, name, name_size, text, text_size))
        return SALTS_EINVAL;
    cmeta_mutex_lock(&impl->lock);
    if (impl->closed) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ESHUTDOWN;
    }
    if (!matches(impl, target)) {
        ++impl->stale_refs;
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ENOENT;
    }
    if (impl->next_sequence == UINT64_MAX) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ERANGE;
    }
    row = reserve_locked(impl, target, name, name_size, text, text_size);
    if (row == NULL) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ENOBUFS;
    }
    ++impl->prepared;
    commit_locked(impl, row);
    cmeta_mutex_unlock(&impl->lock);
    return SALTS_OK;
}

cflow_mailbox_status scxml_host_router_drain(
    scxml_host_router *router, size_t max_events, size_t *out_delivered) {
    scxml_host_router_impl *impl = router_impl(router);
    size_t count = 0u;
    if (out_delivered != NULL) *out_delivered = 0u;
    if (impl == NULL || out_delivered == NULL || max_events == 0u)
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    cmeta_mutex_lock(&impl->lock);
    while (count < max_events) {
        scxml_host_row *row = NULL;
        scxml_event_metadata metadata;
        cflow_mailbox_status status;
        size_t i;
        for (i = 0u; i < impl->event_capacity; ++i) {
            scxml_host_row *candidate = &impl->rows[i];
            if (candidate->state == SCXML_HOST_ROW_READY &&
                (row == NULL || candidate->sequence < row->sequence))
                row = candidate;
        }
        if (row == NULL) break;
        if (!matches(impl, row->target)) {
            ++impl->stale_refs;
            ++impl->delivery_errors;
            *out_delivered = count;
            cmeta_mutex_unlock(&impl->lock);
            return CFLOW_MAILBOX_INVALID_ARGUMENT;
        }
        metadata = (scxml_event_metadata){
            .abi_version = SCXML_EVENT_METADATA_ABI,
            .struct_size = sizeof(scxml_event_metadata),
            .data = {
                .kind = SCXML_CONTENT_TEXT_UTF8,
                .bytes = row->text,
                .byte_count = row->text_size
            }
        };
        /* Finite try_send, not Statechart execution: keep one semantic owner. */
        status = scxml_session_try_send_named_with_metadata(
            impl->endpoints[row->target.slot].session, row->name,
            row->name_size, &metadata);
        if (status != CFLOW_MAILBOX_OK) {
            if (status != CFLOW_MAILBOX_FULL) ++impl->delivery_errors;
            *out_delivered = count;
            cmeta_mutex_unlock(&impl->lock);
            return status;
        }
        release_locked(impl, row);
        ++impl->delivered;
        ++count;
    }
    *out_delivered = count;
    cmeta_mutex_unlock(&impl->lock);
    return count != 0u ? CFLOW_MAILBOX_OK : CFLOW_MAILBOX_EMPTY;
}

int scxml_host_router_cancel(scxml_host_router *router,
                             scxml_host_session_ref target,
                             size_t *out_cancelled) {
    scxml_host_router_impl *impl = router_impl(router);
    size_t count = 0u, i;
    if (out_cancelled != NULL) *out_cancelled = 0u;
    if (impl == NULL || out_cancelled == NULL) return SALTS_EINVAL;
    cmeta_mutex_lock(&impl->lock);
    if (!matches(impl, target)) {
        ++impl->stale_refs;
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ENOENT;
    }
    for (i = 0u; i < impl->event_capacity; ++i) {
        scxml_host_row *row = &impl->rows[i];
        if (row->state == SCXML_HOST_ROW_READY &&
            row->target.slot == target.slot &&
            row->target.generation == target.generation) {
            release_locked(impl, row);
            ++impl->cancelled;
            ++count;
        }
    }
    *out_cancelled = count;
    cmeta_mutex_unlock(&impl->lock);
    return SALTS_OK;
}

bool scxml_host_router_get_stats(
    const scxml_host_router *router, scxml_host_router_stats *out) {
    scxml_host_router_impl *impl = router != NULL
        ? (scxml_host_router_impl *)router->impl : NULL;
    if (impl == NULL || out == NULL) return false;
    cmeta_mutex_lock(&impl->lock);
    *out = (scxml_host_router_stats){
        .endpoints = impl->endpoint_count,
        .pending = impl->pending,
        .reserved = impl->reserved,
        .high_water = impl->high_water,
        .closed = impl->closed,
        .prepared = impl->prepared,
        .committed = impl->committed,
        .discarded = impl->discarded,
        .delivered = impl->delivered,
        .cancelled = impl->cancelled,
        .rejected_full = impl->rejected_full,
        .stale_refs = impl->stale_refs,
        .delivery_errors = impl->delivery_errors,
        .invariant_failures = impl->invariant_failures
    };
    cmeta_mutex_unlock(&impl->lock);
    return true;
}

int scxml_host_router_close(scxml_host_router *router) {
    scxml_host_router_impl *impl = router_impl(router);
    size_t i;
    if (impl == NULL) return SALTS_EINVAL;
    cmeta_mutex_lock(&impl->lock);
    if (impl->closed) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_EALREADY;
    }
    impl->closed = true;
    for (i = 0u; i < impl->event_capacity; ++i) {
        if (impl->rows[i].state == SCXML_HOST_ROW_READY) {
            release_locked(impl, &impl->rows[i]);
            ++impl->cancelled;
        }
    }
    cmeta_mutex_unlock(&impl->lock);
    return SALTS_OK;
}

bool scxml_host_router_is_quiescent(const scxml_host_router *router) {
    scxml_host_router_impl *impl = router != NULL
        ? (scxml_host_router_impl *)router->impl : NULL;
    bool ok;
    if (impl == NULL) return false;
    cmeta_mutex_lock(&impl->lock);
    ok = impl->closed && impl->pending == 0u && impl->endpoint_count == 0u;
    cmeta_mutex_unlock(&impl->lock);
    return ok;
}

int scxml_host_router_destroy(scxml_host_router *router) {
    scxml_host_router_impl *impl = router_impl(router);
    if (router == NULL) return SALTS_EINVAL;
    if (impl == NULL) return SALTS_OK;
    if (!scxml_host_router_is_quiescent(router)) return SALTS_EBUSY;
    cmeta_mutex_destroy(&impl->lock);
    free(impl->rows);
    free(impl->endpoints);
    free(impl);
    router->impl = NULL;
    return SALTS_OK;
}
