#include <scxml/host_router.h>

#include <salts/thread.h>

#include <stdlib.h>
#include <string.h>

typedef struct scxml_host_endpoint {
    scxml_session *session;
    uint32_t generation;
    scxml_host_session_ref parent;
    char location[SCXML_EVENT_METADATA_CAPACITY + 1u];
    size_t location_size;
    bool live;
    bool accessible;
} scxml_host_endpoint;

/* Invoke aliases are generation-pinned and drawn from one finite table. */
typedef struct scxml_host_invoke_route {
    scxml_host_session_ref owner;
    scxml_host_session_ref target;
    char id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    size_t id_size;
    bool live;
} scxml_host_invoke_route;

typedef enum scxml_host_row_state {
    SCXML_HOST_ROW_FREE = 0,
    SCXML_HOST_ROW_RESERVED,
    SCXML_HOST_ROW_READY,
    SCXML_HOST_ROW_INFLIGHT
} scxml_host_row_state;

typedef struct scxml_host_router_impl scxml_host_router_impl;

typedef struct scxml_host_row {
    scxml_host_router_impl *owner;
    scxml_host_row_state state;
    scxml_host_session_ref target;
    scxml_host_session_ref source;
    uint64_t sequence;
    size_t name_size;
    size_t text_size;
    size_t send_id_size;
    char name[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char text[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char send_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
} scxml_host_row;

struct scxml_host_router_impl {
    cmeta_mutex_t lock;
    scxml_host_endpoint *endpoints;
    scxml_host_row *rows;
    scxml_host_invoke_route *invoke_routes;
    size_t endpoint_capacity;
    size_t event_capacity;
    size_t invoke_capacity;
    size_t invoke_count;
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
    /* One host drain lane; Session admission must not run under lock. */
    bool draining;
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

static bool refs_equal(scxml_host_session_ref a, scxml_host_session_ref b) {
    return a.generation != 0u && a.slot == b.slot &&
           a.generation == b.generation;
}

static bool valid_invoke_id(const char *id, size_t size) {
    return id != NULL && size != 0u &&
        size <= SCXML_EVENT_METADATA_CAPACITY - 2u &&
        memchr(id, '\0', size) == NULL && memchr(id, '#', size) == NULL;
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
    memset(row->send_id, 0, sizeof(row->send_id));
    row->target = (scxml_host_session_ref){0};
    row->source = (scxml_host_session_ref){0};
    row->sequence = 0u;
    row->name_size = 0u;
    row->text_size = 0u;
    row->send_id_size = 0u;
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
        config->invoke_capacity > SIZE_MAX / sizeof(scxml_host_invoke_route) ||
        config->max_text_bytes == 0u ||
        config->max_text_bytes > SCXML_EVENT_METADATA_CAPACITY)
        return SALTS_EINVAL;
    impl = (scxml_host_router_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return SALTS_ENOMEM;
    impl->endpoints = (scxml_host_endpoint *)calloc(
        config->endpoint_capacity, sizeof(*impl->endpoints));
    impl->rows = (scxml_host_row *)calloc(
        config->event_capacity, sizeof(*impl->rows));
    if (config->invoke_capacity != 0u) {
        impl->invoke_routes = (scxml_host_invoke_route *)calloc(
            config->invoke_capacity, sizeof(*impl->invoke_routes));
    }
    if (impl->endpoints == NULL || impl->rows == NULL ||
        (config->invoke_capacity != 0u && impl->invoke_routes == NULL)) {
        free(impl->endpoints);
        free(impl->rows);
        free(impl->invoke_routes);
        free(impl);
        return SALTS_ENOMEM;
    }
    cmeta_mutex_init(&impl->lock);
    if (impl->lock == NULL) {
        free(impl->endpoints);
        free(impl->rows);
        free(impl->invoke_routes);
        free(impl);
        return SALTS_ENOMEM;
    }
    impl->endpoint_capacity = config->endpoint_capacity;
    impl->event_capacity = config->event_capacity;
    impl->invoke_capacity = config->invoke_capacity;
    impl->max_text_bytes = config->max_text_bytes;
    for (i = 0u; i < config->event_capacity; ++i) impl->rows[i].owner = impl;
    router->impl = impl;
    return SALTS_OK;
}

int scxml_host_router_attach(scxml_host_router *router,
                             scxml_session *session,
                             scxml_host_session_ref *out_ref) {
    scxml_host_router_impl *impl = router_impl(router);
    char location[SCXML_EVENT_METADATA_CAPACITY + 1u];
    size_t required = 0u;
    size_t location_size;
    size_t i;
    if (out_ref != NULL) *out_ref = (scxml_host_session_ref){0};
    if (impl == NULL || session == NULL || session->impl == NULL ||
        out_ref == NULL)
        return SALTS_EINVAL;
    /* Extract the Session's actual built-in SCXML location before acquiring
       the Host lock. It is the ONLY public #_scxml_<sessionid> identity. */
    if (scxml_session_copy_location(session, location, sizeof(location),
                                    &required) != SCXML_LOCATION_OK ||
        required <= 1u || required > sizeof(location))
        return SALTS_EINVAL;
    location_size = required - 1u;

    cmeta_mutex_lock(&impl->lock);
    if (impl->closed) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ESHUTDOWN;
    }
    for (i = 0u; i < impl->endpoint_capacity; ++i) {
        const scxml_host_endpoint *p = &impl->endpoints[i];
        if (p->live &&
            (p->session == session ||
             (p->location_size == location_size &&
              memcmp(p->location, location, location_size) == 0))) {
            cmeta_mutex_unlock(&impl->lock);
            return SALTS_EALREADY;
        }
    }
    for (i = 0u; i < impl->endpoint_capacity; ++i) {
        scxml_host_endpoint *endpoint = &impl->endpoints[i];
        if (!endpoint->live && endpoint->generation != UINT32_MAX) {
            /* Keep generation monotonically increasing, never wrap/reuse a
               retired generation's Session identity. */
            endpoint->live = true;
            endpoint->session = session;
            endpoint->parent = (scxml_host_session_ref){0};
            endpoint->accessible = false;
            memset(endpoint->location, 0, sizeof(endpoint->location));
            memcpy(endpoint->location, location, required);
            endpoint->location_size = location_size;
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
    scxml_host_endpoint *endpoint;
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
            (refs_equal(row->target, target) ||
             refs_equal(row->source, target))) {
            cmeta_mutex_unlock(&impl->lock);
            return SALTS_EBUSY;
        }
    }
    for (i = 0u; i < impl->endpoint_capacity; ++i) {
        const scxml_host_endpoint *p = &impl->endpoints[i];
        if (p->live && refs_equal(p->parent, target)) {
            cmeta_mutex_unlock(&impl->lock);
            return SALTS_EBUSY;
        }
    }
    for (i = 0u; i < impl->invoke_capacity; ++i) {
        const scxml_host_invoke_route *route = &impl->invoke_routes[i];
        if (route->live &&
            (refs_equal(route->owner, target) ||
             refs_equal(route->target, target))) {
            cmeta_mutex_unlock(&impl->lock);
            return SALTS_EBUSY;
        }
    }
    endpoint = &impl->endpoints[target.slot];
    endpoint->live = false;
    endpoint->session = NULL;
    endpoint->parent = (scxml_host_session_ref){0};
    endpoint->accessible = false;
    endpoint->location_size = 0u;
    memset(endpoint->location, 0, sizeof(endpoint->location));
    --impl->endpoint_count;
    cmeta_mutex_unlock(&impl->lock);
    return SALTS_OK;
}

int scxml_host_router_set_parent(scxml_host_router *router,
                                 scxml_host_session_ref child,
                                 scxml_host_session_ref parent) {
    scxml_host_router_impl *impl = router_impl(router);
    scxml_host_session_ref ancestor;
    size_t steps;
    const bool clear = parent.slot == 0u && parent.generation == 0u;
    if (impl == NULL || (!clear && refs_equal(child, parent)))
        return SALTS_EINVAL;
    cmeta_mutex_lock(&impl->lock);
    if (impl->closed) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ESHUTDOWN;
    }
    if (!matches(impl, child) || (!clear && !matches(impl, parent))) {
        ++impl->stale_refs;
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ENOENT;
    }
    if (!clear) {
        /* No parent cycles, and no dependence on retired generations. */
        ancestor = parent;
        for (steps = 0u; ancestor.generation != 0u &&
                         steps < impl->endpoint_capacity; ++steps) {
            if (refs_equal(ancestor, child)) {
                cmeta_mutex_unlock(&impl->lock);
                return SALTS_EINVAL;
            }
            if (!matches(impl, ancestor)) {
                ++impl->stale_refs;
                cmeta_mutex_unlock(&impl->lock);
                return SALTS_ENOENT;
            }
            ancestor = impl->endpoints[ancestor.slot].parent;
        }
        if (ancestor.generation != 0u) {
            cmeta_mutex_unlock(&impl->lock);
            return SALTS_EINVAL;
        }
    }
    impl->endpoints[child.slot].parent =
        clear ? (scxml_host_session_ref){0} : parent;
    cmeta_mutex_unlock(&impl->lock);
    return SALTS_OK;
}

int scxml_host_router_set_external_access(
    scxml_host_router *router, scxml_host_session_ref ref, bool enabled) {
    scxml_host_router_impl *impl = router_impl(router);
    if (impl == NULL) return SALTS_EINVAL;
    cmeta_mutex_lock(&impl->lock);
    if (impl->closed) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ESHUTDOWN;
    }
    if (!matches(impl, ref)) {
        ++impl->stale_refs;
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ENOENT;
    }
    impl->endpoints[ref.slot].accessible = enabled;
    cmeta_mutex_unlock(&impl->lock);
    return SALTS_OK;
}

int scxml_host_router_bind_invoke(
    scxml_host_router *router, scxml_host_session_ref source,
    const char *invoke_id, size_t invoke_id_size,
    scxml_host_session_ref target) {
    scxml_host_router_impl *impl = router_impl(router);
    scxml_host_invoke_route *free_route = NULL;
    size_t i;
    if (impl == NULL || !valid_invoke_id(invoke_id, invoke_id_size) ||
        refs_equal(source, target))
        return SALTS_EINVAL;
    cmeta_mutex_lock(&impl->lock);
    if (impl->closed) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ESHUTDOWN;
    }
    if (!matches(impl, source) || !matches(impl, target)) {
        ++impl->stale_refs;
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ENOENT;
    }
    for (i = 0u; i < impl->invoke_capacity; ++i) {
        scxml_host_invoke_route *route = &impl->invoke_routes[i];
        if (!route->live) {
            if (free_route == NULL) free_route = route;
        } else if (refs_equal(route->owner, source) &&
                   route->id_size == invoke_id_size &&
                   memcmp(route->id, invoke_id, invoke_id_size) == 0) {
            cmeta_mutex_unlock(&impl->lock);
            return SALTS_EALREADY;
        }
    }
    if (free_route == NULL) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ENOBUFS;
    }
    *free_route = (scxml_host_invoke_route){
        .owner = source, .target = target,
        .id_size = invoke_id_size, .live = true
    };
    memcpy(free_route->id, invoke_id, invoke_id_size);
    free_route->id[invoke_id_size] = '\0';
    ++impl->invoke_count;
    cmeta_mutex_unlock(&impl->lock);
    return SALTS_OK;
}

int scxml_host_router_unbind_invoke(
    scxml_host_router *router, scxml_host_session_ref source,
    const char *invoke_id, size_t invoke_id_size) {
    scxml_host_router_impl *impl = router_impl(router);
    size_t i;
    if (impl == NULL || !valid_invoke_id(invoke_id, invoke_id_size))
        return SALTS_EINVAL;
    cmeta_mutex_lock(&impl->lock);
    if (!matches(impl, source)) {
        ++impl->stale_refs;
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ENOENT;
    }
    for (i = 0u; i < impl->invoke_capacity; ++i) {
        scxml_host_invoke_route *route = &impl->invoke_routes[i];
        if (route->live && refs_equal(route->owner, source) &&
            route->id_size == invoke_id_size &&
            memcmp(route->id, invoke_id, invoke_id_size) == 0) {
            memset(route, 0, sizeof(*route));
            --impl->invoke_count;
            cmeta_mutex_unlock(&impl->lock);
            return SALTS_OK;
        }
    }
    cmeta_mutex_unlock(&impl->lock);
    return SALTS_ENOENT;
}

static bool host_target_equal(const char *target, size_t size,
                              const char *literal) {
    const size_t literal_size = strlen(literal);
    return size == literal_size &&
        memcmp(target, literal, size) == 0;
}

static int resolve_locked(scxml_host_router_impl *impl,
                          scxml_host_session_ref source,
                          const char *target, size_t target_size,
                          scxml_host_session_ref *out_target) {
    size_t i;
    scxml_host_session_ref resolved = {0};
    if (!matches(impl, source)) return SALTS_ENOENT;
    if (target_size == 0u) {
        resolved = source;
    } else if (host_target_equal(target, target_size, "#_parent")) {
        resolved = impl->endpoints[source.slot].parent;
    } else if (target_size > 2u && target[0] == '#' && target[1] == '_') {
        /* Invocation aliases have precedence over globally exposed Session
           locations and are visible only from their owning Session. */
        for (i = 0u; i < impl->invoke_capacity; ++i) {
            const scxml_host_invoke_route *route = &impl->invoke_routes[i];
            if (route->live && refs_equal(route->owner, source) &&
                route->id_size == target_size - 2u &&
                memcmp(route->id, target + 2u, route->id_size) == 0) {
                resolved = route->target;
                break;
            }
        }
    }
    if (resolved.generation == 0u && target_size != 0u) {
        /* Global addresses are the exact generated SCXML location strings,
           not an ambiguous slot, URI substring or prefix match. */
        for (i = 0u; i < impl->endpoint_capacity; ++i) {
            const scxml_host_endpoint *candidate = &impl->endpoints[i];
            if (candidate->live && candidate->accessible &&
                candidate->location_size == target_size &&
                memcmp(candidate->location, target, target_size) == 0) {
                resolved = (scxml_host_session_ref){
                    (uint32_t)i, candidate->generation
                };
                break;
            }
        }
    }
    if (!matches(impl, resolved)) return SALTS_ENOENT;
    *out_target = resolved;
    return SALTS_OK;
}

int scxml_host_router_resolve(
    scxml_host_router *router, scxml_host_session_ref source,
    const char *target, size_t target_size,
    scxml_host_session_ref *out_target) {
    scxml_host_router_impl *impl = router_impl(router);
    int status;
    if (out_target != NULL) *out_target = (scxml_host_session_ref){0};
    if (impl == NULL || out_target == NULL ||
        (target_size != 0u && target == NULL))
        return SALTS_EINVAL;
    cmeta_mutex_lock(&impl->lock);
    if (impl->closed) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ESHUTDOWN;
    }
    status = resolve_locked(impl, source, target, target_size, out_target);
    if (status == SALTS_ENOENT && !matches(impl, source))
        ++impl->stale_refs;
    cmeta_mutex_unlock(&impl->lock);
    return status;
}

int scxml_host_router_prepare_target(
    scxml_host_router *router, scxml_host_session_ref source,
    const char *target, size_t target_size,
    const char *name, size_t name_size,
    const char *send_id, size_t send_id_size,
    const char *text, size_t text_size,
    cflow_statechart_effect_ticket *out_ticket) {
    scxml_host_router_impl *impl = router_impl(router);
    scxml_host_session_ref resolved = {0};
    scxml_host_row *row;
    int status;
    if (out_ticket != NULL) *out_ticket = (cflow_statechart_effect_ticket){0};
    if (impl == NULL || out_ticket == NULL ||
        (target_size != 0u && target == NULL) ||
        send_id_size > SCXML_EVENT_METADATA_CAPACITY ||
        (send_id_size != 0u &&
         (send_id == NULL || memchr(send_id, '\0', send_id_size) != NULL)) ||
        !valid_fields(impl, name, name_size, text, text_size))
        return SALTS_EINVAL;

    cmeta_mutex_lock(&impl->lock);
    if (impl->closed) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ESHUTDOWN;
    }
    status = resolve_locked(impl, source, target, target_size, &resolved);
    if (status != SALTS_OK) {
        if (!matches(impl, source)) ++impl->stale_refs;
        cmeta_mutex_unlock(&impl->lock);
        return status;
    }
    row = reserve_locked(impl, resolved, name, name_size, text, text_size);
    if (row == NULL) {
        cmeta_mutex_unlock(&impl->lock);
        return SALTS_ENOBUFS;
    }
    row->source = source;
    if (send_id_size != 0u) memcpy(row->send_id, send_id, send_id_size);
    row->send_id[send_id_size] = '\0';
    row->send_id_size = send_id_size;
    ++impl->prepared;
    *out_ticket = (cflow_statechart_effect_ticket){
        ticket_commit, ticket_discard, row
    };
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
    cflow_mailbox_status result = CFLOW_MAILBOX_EMPTY;
    if (out_delivered != NULL) *out_delivered = 0u;
    if (impl == NULL || out_delivered == NULL || max_events == 0u)
        return CFLOW_MAILBOX_INVALID_ARGUMENT;

    cmeta_mutex_lock(&impl->lock);
    /* A second drainer cannot reinterpret READY/INFLIGHT identity or reorder
       already-accepted external Events; this is a Host-lane contract error. */
    if (impl->draining) {
        cmeta_mutex_unlock(&impl->lock);
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    }
    if (impl->closed) {
        cmeta_mutex_unlock(&impl->lock);
        return CFLOW_MAILBOX_CLOSED;
    }
    impl->draining = true;
    cmeta_mutex_unlock(&impl->lock);

    while (count < max_events) {
        scxml_host_row *row = NULL;
        scxml_session *session;
        scxml_event_metadata metadata;
        const char *origin = NULL;
        size_t origin_size = 0u;
        cflow_mailbox_status status;
        size_t i;

        cmeta_mutex_lock(&impl->lock);
        if (impl->closed) {
            result = CFLOW_MAILBOX_CLOSED;
            cmeta_mutex_unlock(&impl->lock);
            break;
        }
        for (i = 0u; i < impl->event_capacity; ++i) {
            scxml_host_row *candidate = &impl->rows[i];
            if (candidate->state == SCXML_HOST_ROW_READY &&
                (row == NULL || candidate->sequence < row->sequence))
                row = candidate;
        }
        if (row == NULL) {
            cmeta_mutex_unlock(&impl->lock);
            break;
        }
        if (!matches(impl, row->target)) {
            ++impl->stale_refs;
            ++impl->delivery_errors;
            result = CFLOW_MAILBOX_INVALID_ARGUMENT;
            cmeta_mutex_unlock(&impl->lock);
            break;
        }
        if (row->source.generation != 0u) {
            if (!matches(impl, row->source)) {
                ++impl->stale_refs;
                ++impl->delivery_errors;
                result = CFLOW_MAILBOX_INVALID_ARGUMENT;
                cmeta_mutex_unlock(&impl->lock);
                break;
            }
            origin = impl->endpoints[row->source.slot].location;
            origin_size = impl->endpoints[row->source.slot].location_size;
        }
        /* The row and matching Session/source generation stay borrowed while
           INFLIGHT. Detach, close and cancel may not recycle this row. */
        session = impl->endpoints[row->target.slot].session;
        row->state = SCXML_HOST_ROW_INFLIGHT;
        cmeta_mutex_unlock(&impl->lock);

        metadata = (scxml_event_metadata){
            .abi_version = SCXML_EVENT_METADATA_ABI,
            .struct_size = sizeof(scxml_event_metadata),
            .send_id = row->send_id_size ? row->send_id : NULL,
            .send_id_size = row->send_id_size,
            .origin = origin,
            .origin_size = origin_size,
            .origin_type = origin != NULL ? "scxml" : NULL,
            .origin_type_size = origin != NULL ? 5u : 0u,
            .data = {
                .kind = SCXML_CONTENT_TEXT_UTF8,
                .bytes = row->text,
                .byte_count = row->text_size
            }
        };
        /* May take CFlow's own locks; NEVER hold the Host router mutex here. */
        status = scxml_session_try_send_named_with_metadata(
            session, row->name, row->name_size, &metadata);

        cmeta_mutex_lock(&impl->lock);
        if (row->state != SCXML_HOST_ROW_INFLIGHT) {
            ++impl->invariant_failures;
            result = CFLOW_MAILBOX_INVALID_ARGUMENT;
        } else if (status == CFLOW_MAILBOX_OK) {
            release_locked(impl, row);
            ++impl->delivered;
            ++count;
            result = CFLOW_MAILBOX_OK;
        } else if (impl->closed) {
            /* Close may cancel READY rows while a foreign try_send executes.
               This INFLIGHT row completes once and is never re-published. */
            ++impl->cancelled;
            release_locked(impl, row);
            result = CFLOW_MAILBOX_CLOSED;
        } else {
            row->state = SCXML_HOST_ROW_READY;
            if (status != CFLOW_MAILBOX_FULL) ++impl->delivery_errors;
            result = status;
        }
        cmeta_mutex_unlock(&impl->lock);
        if (status != CFLOW_MAILBOX_OK || result != CFLOW_MAILBOX_OK) break;
    }

    cmeta_mutex_lock(&impl->lock);
    impl->draining = false;
    cmeta_mutex_unlock(&impl->lock);
    *out_delivered = count;
    return result;
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
        .invoke_bindings = impl->invoke_count,
        .closed = impl->closed,
        .draining = impl->draining,
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
    free(impl->invoke_routes);
    free(impl);
    router->impl = NULL;
    return SALTS_OK;
}
