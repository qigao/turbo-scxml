#include <cflow/executor.h>
#include <scxml/scxml.h>

#include "tinytest.h"

#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <turbo/thread.h>

#define HOST_ENDPOINT_CAPACITY 4u
#define HOST_MESSAGE_CAPACITY 4u
#define HOST_LOCATION_CAPACITY 64u
#define HOST_EVENT_CAPACITY 64u
#define HOST_SEND_ID_CAPACITY 64u
#define HOST_TARGET_CAPACITY 64u

static const char HOST_SCXML_PROCESSOR[] =
    "http://www.w3.org/TR/scxml/#SCXMLEventProcessor";
static const char HOST_ORIGIN_TYPE[] = "scxml";

typedef enum host_message_state {
    HOST_MESSAGE_FREE = 0,
    HOST_MESSAGE_RESERVED,
    HOST_MESSAGE_READY,
    HOST_MESSAGE_INFLIGHT
} host_message_state;

typedef struct host_router host_router;
typedef struct host_adapter_context host_adapter_context;

typedef struct host_endpoint {
    bool in_use;
    bool active;
    bool accessible;
    scxml_session *session;
    const scxml_program *program;
    host_adapter_context *adapter;
    size_t parent;
    size_t invoke_target;
    char invoke_alias[HOST_TARGET_CAPACITY];
    char location[HOST_LOCATION_CAPACITY];
} host_endpoint;

typedef struct host_message {
    host_router *router;
    host_adapter_context *source_adapter;
    host_message_state state;
    size_t source;
    size_t target;
    uint64_t sequence;
    char event[HOST_EVENT_CAPACITY];
    char target_text[HOST_TARGET_CAPACITY];
    char send_id[HOST_SEND_ID_CAPACITY];
} host_message;

typedef struct host_delivery {
    size_t count;
    char event[HOST_EVENT_CAPACITY];
    char target[HOST_TARGET_CAPACITY];
    char send_id[HOST_SEND_ID_CAPACITY];
    char origin[HOST_LOCATION_CAPACITY];
    char origin_type[sizeof(HOST_ORIGIN_TYPE)];
} host_delivery;

struct host_router {
    turbo_mutex_t lock;
    size_t message_capacity;
    uint64_t next_sequence;
    host_endpoint endpoints[HOST_ENDPOINT_CAPACITY];
    host_message messages[HOST_MESSAGE_CAPACITY];
    host_delivery last_delivery;
};

struct host_adapter_context {
    host_router *router;
    size_t endpoint;
    size_t outstanding;
    bool closed;
};

typedef enum host_pump_status {
    HOST_PUMP_EMPTY = 0,
    HOST_PUMP_DELIVERED,
    HOST_PUMP_WOULD_BLOCK,
    HOST_PUMP_DROPPED
} host_pump_status;

static bool host_copy(char *destination, size_t capacity,
                      const char *source, size_t source_size) {
    if (destination == NULL || capacity == 0u || source_size >= capacity ||
        (source == NULL && source_size != 0u))
        return false;
    if (source_size != 0u) memcpy(destination, source, source_size);
    destination[source_size] = '\0';
    return true;
}

static bool host_text_equal(const char *left, size_t left_size,
                            const char *right) {
    const size_t right_size = strlen(right);
    return left_size == right_size &&
        (left_size == 0u || memcmp(left, right, left_size) == 0);
}

static bool host_router_init(host_router *router, size_t message_capacity) {
    size_t index;
    if (router == NULL || message_capacity == 0u ||
        message_capacity > HOST_MESSAGE_CAPACITY)
        return false;
    memset(router, 0, sizeof(*router));
    for (index = 0u; index < HOST_ENDPOINT_CAPACITY; ++index) {
        router->endpoints[index].parent = SIZE_MAX;
        router->endpoints[index].invoke_target = SIZE_MAX;
    }
    router->message_capacity = message_capacity;
    router->next_sequence = UINT64_C(1);
    turbo_mutex_init(&router->lock);
    return router->lock != NULL;
}

static void host_router_destroy(host_router *router) {
    if (router == NULL) return;
    if (router->lock != NULL) turbo_mutex_destroy(&router->lock);
    memset(router, 0, sizeof(*router));
}

static void host_adapter_init(host_adapter_context *adapter,
                              host_router *router) {
    if (adapter == NULL) return;
    *adapter = (host_adapter_context){
        .router = router, .endpoint = SIZE_MAX};
}

static bool host_router_reserve(
    host_router *router, bool accessible,
    host_adapter_context *adapter, size_t *out_endpoint) {
    size_t index;
    if (router == NULL || adapter == NULL || out_endpoint == NULL)
        return false;
    turbo_mutex_lock(&router->lock);
    if (adapter->router != router || adapter->endpoint != SIZE_MAX ||
        adapter->closed) {
        turbo_mutex_unlock(&router->lock);
        return false;
    }
    for (index = 0u; index < HOST_ENDPOINT_CAPACITY; ++index) {
        host_endpoint *endpoint = &router->endpoints[index];
        if (endpoint->in_use) continue;
        *endpoint = (host_endpoint){
            .in_use = true,
            .accessible = accessible,
            .adapter = adapter,
            .parent = SIZE_MAX,
            .invoke_target = SIZE_MAX};
        adapter->endpoint = index;
        *out_endpoint = index;
        turbo_mutex_unlock(&router->lock);
        return true;
    }
    turbo_mutex_unlock(&router->lock);
    return false;
}

static bool host_router_activate(
    host_router *router, size_t endpoint_index,
    scxml_session *session, const scxml_program *program) {
    char location[HOST_LOCATION_CAPACITY];
    size_t required = 0u;
    size_t index;
    host_endpoint *endpoint;
    if (router == NULL || endpoint_index >= HOST_ENDPOINT_CAPACITY ||
        session == NULL || program == NULL ||
        scxml_session_copy_location(
            session, location, sizeof(location), &required) !=
            SCXML_LOCATION_OK)
        return false;
    turbo_mutex_lock(&router->lock);
    endpoint = &router->endpoints[endpoint_index];
    if (!endpoint->in_use || endpoint->active) {
        turbo_mutex_unlock(&router->lock);
        return false;
    }
    for (index = 0u; index < HOST_ENDPOINT_CAPACITY; ++index) {
        if (index != endpoint_index && router->endpoints[index].active &&
            strcmp(router->endpoints[index].location, location) == 0) {
            turbo_mutex_unlock(&router->lock);
            return false;
        }
    }
    endpoint->session = session;
    endpoint->program = program;
    memcpy(endpoint->location, location, required);
    endpoint->active = true;
    turbo_mutex_unlock(&router->lock);
    return true;
}

static bool host_router_unregister(host_router *router, size_t endpoint);

static bool host_router_register(
    host_router *router, scxml_session *session,
    const scxml_program *program, bool accessible,
    host_adapter_context *adapter, size_t *out_endpoint) {
    host_adapter_context local_adapter;
    host_adapter_context *endpoint_adapter = adapter;
    size_t endpoint;
    if (router == NULL || session == NULL || program == NULL ||
        out_endpoint == NULL)
        return false;
    if (endpoint_adapter == NULL) {
        host_adapter_init(&local_adapter, router);
        endpoint_adapter = &local_adapter;
    }
    if (!host_router_reserve(
            router, accessible, endpoint_adapter, &endpoint))
        return false;
    if (!host_router_activate(router, endpoint, session, program)) {
        (void)host_router_unregister(router, endpoint);
        return false;
    }
    if (adapter == NULL) {
        turbo_mutex_lock(&router->lock);
        router->endpoints[endpoint].adapter = NULL;
        turbo_mutex_unlock(&router->lock);
    }
    *out_endpoint = endpoint;
    return true;
}

static bool host_router_unregister(host_router *router, size_t endpoint) {
    size_t index;
    if (router == NULL || endpoint >= HOST_ENDPOINT_CAPACITY) return false;
    turbo_mutex_lock(&router->lock);
    if (!router->endpoints[endpoint].in_use) {
        turbo_mutex_unlock(&router->lock);
        return false;
    }
    for (index = 0u; index < router->message_capacity; ++index) {
        const host_message *message = &router->messages[index];
        if (message->state != HOST_MESSAGE_FREE &&
            (message->source == endpoint || message->target == endpoint)) {
            turbo_mutex_unlock(&router->lock);
            return false;
        }
    }
    if (router->endpoints[endpoint].adapter != NULL)
        router->endpoints[endpoint].adapter->endpoint = SIZE_MAX;
    router->endpoints[endpoint] = (host_endpoint){
        .parent = SIZE_MAX, .invoke_target = SIZE_MAX};
    turbo_mutex_unlock(&router->lock);
    return true;
}

static bool host_router_set_parent(host_router *router, size_t child,
                                   size_t parent) {
    bool valid;
    if (router == NULL || child >= HOST_ENDPOINT_CAPACITY ||
        parent >= HOST_ENDPOINT_CAPACITY)
        return false;
    turbo_mutex_lock(&router->lock);
    valid = router->endpoints[child].in_use &&
        router->endpoints[parent].in_use;
    if (valid) router->endpoints[child].parent = parent;
    turbo_mutex_unlock(&router->lock);
    return valid;
}

static bool host_router_set_invoke_alias(
    host_router *router, size_t owner, const char *alias, size_t target) {
    bool valid;
    if (router == NULL || owner >= HOST_ENDPOINT_CAPACITY ||
        target >= HOST_ENDPOINT_CAPACITY || alias == NULL)
        return false;
    turbo_mutex_lock(&router->lock);
    valid = router->endpoints[owner].in_use &&
        router->endpoints[target].in_use &&
        host_copy(router->endpoints[owner].invoke_alias,
                  sizeof(router->endpoints[owner].invoke_alias),
                  alias, strlen(alias));
    if (valid) router->endpoints[owner].invoke_target = target;
    turbo_mutex_unlock(&router->lock);
    return valid;
}

static size_t host_resolve_target_locked(
    const host_router *router, size_t source,
    const char *target, size_t target_size) {
    const host_endpoint *owner;
    size_t index;
    if (source >= HOST_ENDPOINT_CAPACITY ||
        !router->endpoints[source].in_use)
        return SIZE_MAX;
    owner = &router->endpoints[source];
    if (target_size == 0u) return source;
    if (host_text_equal(target, target_size, "#_parent"))
        return owner->parent;
    if (owner->invoke_target != SIZE_MAX &&
        host_text_equal(target, target_size, owner->invoke_alias))
        return owner->invoke_target;
    for (index = 0u; index < HOST_ENDPOINT_CAPACITY; ++index) {
        const host_endpoint *candidate = &router->endpoints[index];
        if (candidate->in_use && candidate->accessible &&
            host_text_equal(target, target_size, candidate->location))
            return index;
    }
    return SIZE_MAX;
}

static void host_release_message_locked(host_message *message) {
    host_adapter_context *adapter = message->source_adapter;
    if (adapter != NULL && adapter->outstanding != 0u)
        --adapter->outstanding;
    memset(message, 0, sizeof(*message));
}

static void host_ticket_commit(void *user) {
    host_message *message = (host_message *)user;
    host_router *router = message != NULL ? message->router : NULL;
    if (router == NULL) return;
    turbo_mutex_lock(&router->lock);
    if (message->state == HOST_MESSAGE_RESERVED) {
        message->state = HOST_MESSAGE_READY;
        message->sequence = router->next_sequence++;
    }
    turbo_mutex_unlock(&router->lock);
}

static void host_ticket_discard(void *user) {
    host_message *message = (host_message *)user;
    host_router *router = message != NULL ? message->router : NULL;
    if (router == NULL) return;
    turbo_mutex_lock(&router->lock);
    if (message->state == HOST_MESSAGE_RESERVED)
        host_release_message_locked(message);
    turbo_mutex_unlock(&router->lock);
}

static scxml_adapter_status host_prepare_send(
    void *user, const scxml_send_request_v3 *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    host_adapter_context *adapter = (host_adapter_context *)user;
    host_router *router = adapter != NULL ? adapter->router : NULL;
    host_message *message = NULL;
    bool source_relative_target;
    size_t target;
    size_t index;
    if (router == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || adapter->endpoint == SIZE_MAX)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    *out_error = NULL;
    if (request->payload.kind != SCXML_PAYLOAD_NONE)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    if (request->base.type_size != 0u &&
        !host_text_equal(request->base.type, request->base.type_size,
                         HOST_SCXML_PROCESSOR)) {
        *out_error = "unsupported Event I/O processor type";
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    turbo_mutex_lock(&router->lock);
    if (adapter->closed) {
        turbo_mutex_unlock(&router->lock);
        return SCXML_ADAPTER_CLOSED;
    }
    source_relative_target = request->base.target_size == 0u ||
        host_text_equal(request->base.target, request->base.target_size,
                        "#_parent") ||
        (router->endpoints[adapter->endpoint].invoke_target != SIZE_MAX &&
         host_text_equal(
             request->base.target, request->base.target_size,
             router->endpoints[adapter->endpoint].invoke_alias));
    target = host_resolve_target_locked(
        router, adapter->endpoint, request->base.target,
        request->base.target_size);
    if (target == SIZE_MAX || !router->endpoints[target].in_use ||
        (!source_relative_target &&
         !router->endpoints[target].accessible)) {
        turbo_mutex_unlock(&router->lock);
        *out_error = "target session is missing or inaccessible";
        return SCXML_ADAPTER_ERROR_COMMUNICATION;
    }
    for (index = 0u; index < router->message_capacity; ++index) {
        if (router->messages[index].state == HOST_MESSAGE_FREE) {
            message = &router->messages[index];
            break;
        }
    }
    if (message == NULL) {
        turbo_mutex_unlock(&router->lock);
        return SCXML_ADAPTER_FULL;
    }
    if (!host_copy(message->event, sizeof(message->event),
                   request->base.event, request->base.event_size) ||
        !host_copy(message->target_text, sizeof(message->target_text),
                   request->base.target, request->base.target_size) ||
        !host_copy(message->send_id, sizeof(message->send_id),
                   request->base.id, request->base.id_size)) {
        memset(message, 0, sizeof(*message));
        turbo_mutex_unlock(&router->lock);
        return SCXML_ADAPTER_FULL;
    }
    message->router = router;
    message->source_adapter = adapter;
    message->state = HOST_MESSAGE_RESERVED;
    message->source = adapter->endpoint;
    message->target = target;
    ++adapter->outstanding;
    *out_ticket = (cflow_statechart_effect_ticket){
        host_ticket_commit, host_ticket_discard, message};
    turbo_mutex_unlock(&router->lock);
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status host_prepare_cancel(
    void *user, const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    (void)user;
    (void)request;
    (void)out_ticket;
    if (out_error != NULL) *out_error = "cancel capability is not advertised";
    return SCXML_ADAPTER_INVALID_CONTRACT;
}

static void host_adapter_close(void *user) {
    host_adapter_context *adapter = (host_adapter_context *)user;
    if (adapter == NULL || adapter->router == NULL) return;
    turbo_mutex_lock(&adapter->router->lock);
    adapter->closed = true;
    turbo_mutex_unlock(&adapter->router->lock);
}

static bool host_adapter_is_quiescent(void *user) {
    host_adapter_context *adapter = (host_adapter_context *)user;
    bool quiescent;
    if (adapter == NULL || adapter->router == NULL) return false;
    turbo_mutex_lock(&adapter->router->lock);
    quiescent = adapter->closed && adapter->outstanding == 0u;
    turbo_mutex_unlock(&adapter->router->lock);
    return quiescent;
}

static const scxml_event_io_adapter_v3 HOST_ADAPTER = {
    .abi_version = SCXML_EVENT_IO_ADAPTER_ABI_V3,
    .struct_size = sizeof(scxml_event_io_adapter_v3),
    .capabilities = SCXML_EVENT_IO_CAP_SEND,
    .prepare_send = host_prepare_send,
    .prepare_cancel = host_prepare_cancel,
    .close = host_adapter_close,
    .is_quiescent = host_adapter_is_quiescent};

static size_t host_router_ready_count(host_router *router) {
    size_t count = 0u;
    size_t index;
    turbo_mutex_lock(&router->lock);
    for (index = 0u; index < router->message_capacity; ++index) {
        if (router->messages[index].state == HOST_MESSAGE_READY) ++count;
    }
    turbo_mutex_unlock(&router->lock);
    return count;
}

static host_pump_status host_router_pump(host_router *router) {
    host_message snapshot = {0};
    host_message *selected = NULL;
    host_endpoint target = {0};
    host_endpoint source = {0};
    cflow_event_view event = {0};
    scxml_event_metadata metadata = {0};
    cflow_mailbox_status mailbox_status = CFLOW_MAILBOX_INVALID_ARGUMENT;
    size_t index;
    if (router == NULL) return HOST_PUMP_EMPTY;
    turbo_mutex_lock(&router->lock);
    for (index = 0u; index < router->message_capacity; ++index) {
        host_message *candidate = &router->messages[index];
        if (candidate->state == HOST_MESSAGE_READY &&
            (selected == NULL || candidate->sequence < selected->sequence))
            selected = candidate;
    }
    if (selected == NULL) {
        turbo_mutex_unlock(&router->lock);
        return HOST_PUMP_EMPTY;
    }
    selected->state = HOST_MESSAGE_INFLIGHT;
    snapshot = *selected;
    source = router->endpoints[snapshot.source];
    target = router->endpoints[snapshot.target];
    turbo_mutex_unlock(&router->lock);

    metadata = (scxml_event_metadata){
        .send_id = snapshot.send_id,
        .send_id_size = strlen(snapshot.send_id),
        .origin = source.location,
        .origin_size = strlen(source.location),
        .origin_type = HOST_ORIGIN_TYPE,
        .origin_type_size = sizeof(HOST_ORIGIN_TYPE) - 1u};
    if (source.active && target.active && target.accessible &&
        scxml_program_event(
            target.program, snapshot.event, strlen(snapshot.event), &event)) {
        mailbox_status = scxml_session_try_send_v2(
            target.session, &event, &metadata);
    }

    turbo_mutex_lock(&router->lock);
    if (mailbox_status == CFLOW_MAILBOX_FULL) {
        selected->state = HOST_MESSAGE_READY;
        turbo_mutex_unlock(&router->lock);
        return HOST_PUMP_WOULD_BLOCK;
    }
    if (mailbox_status == CFLOW_MAILBOX_OK) {
        ++router->last_delivery.count;
        (void)host_copy(router->last_delivery.event,
                        sizeof(router->last_delivery.event), snapshot.event,
                        strlen(snapshot.event));
        (void)host_copy(router->last_delivery.target,
                        sizeof(router->last_delivery.target),
                        snapshot.target_text, strlen(snapshot.target_text));
        (void)host_copy(router->last_delivery.send_id,
                        sizeof(router->last_delivery.send_id),
                        snapshot.send_id, strlen(snapshot.send_id));
        (void)host_copy(router->last_delivery.origin,
                        sizeof(router->last_delivery.origin), source.location,
                        strlen(source.location));
        (void)host_copy(router->last_delivery.origin_type,
                        sizeof(router->last_delivery.origin_type),
                        HOST_ORIGIN_TYPE, sizeof(HOST_ORIGIN_TYPE) - 1u);
    }
    host_release_message_locked(selected);
    turbo_mutex_unlock(&router->lock);
    if (mailbox_status == CFLOW_MAILBOX_OK) return HOST_PUMP_DELIVERED;
    if (target.in_use)
        (void)scxml_session_report_adapter_error(
            target.session, SCXML_ADAPTER_ERROR_KIND_COMMUNICATION);
    if (source.in_use)
        (void)scxml_session_report_adapter_error(
            source.session, SCXML_ADAPTER_ERROR_KIND_COMMUNICATION);
    return HOST_PUMP_DROPPED;
}

static scxml_status host_compile(
    const char *source, scxml_program *program,
    scxml_diagnostic *diagnostic) {
    return scxml_compile(
        program, source, strlen(source), NULL, diagnostic);
}

static scxml_status host_compile_fixture(
    const char *fixture_name, scxml_program *program,
    scxml_diagnostic *diagnostic) {
    char path[512];
    char *source = NULL;
    size_t source_size = 0u;
    int path_size;
    scxml_status status;
    if (fixture_name == NULL || program == NULL || diagnostic == NULL)
        return SCXML_INVALID_ARGUMENT;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path))
        return SCXML_LIMIT_EXCEEDED;
    source = tt_read_file(path, &source_size);
    if (source == NULL) return SCXML_XML_ERROR;
    status = scxml_compile(
        program, source, source_size, NULL, diagnostic);
    free(source);
    return status;
}

static scxml_session_config host_session_config(
    const scxml_program *program, cflow_executor *executor) {
    return (scxml_session_config){
        .program = program,
        .executor = executor,
        .external_event_capacity = 4u,
        .internal_event_capacity = 4u,
        .completion_capacity = 4u,
        .microstep_limit = 32u,
        .effect_capacity = 4u,
        .adapter_internal_event_capacity = 4u};
}

enum {
    HOST_ORDER_INVOKE_CAPACITY = 3,
    HOST_ORDER_TICKET_CAPACITY = 8,
    HOST_ORDER_TRACE_CAPACITY = 16
};

typedef struct host_order_probe host_order_probe;

typedef struct host_order_ticket {
    host_order_probe *probe;
    char commit_mark;
    bool in_use;
} host_order_ticket;

struct host_order_probe {
    char trace[HOST_ORDER_TRACE_CAPACITY];
    size_t trace_size;
    char starts[HOST_ORDER_INVOKE_CAPACITY][HOST_SEND_ID_CAPACITY];
    size_t start_count;
    size_t start_commits;
    size_t discards;
    size_t close_calls;
    host_order_ticket tickets[HOST_ORDER_TICKET_CAPACITY];
};

typedef struct host_order_blocker {
    atomic_bool entered;
    atomic_bool release;
} host_order_blocker;

static bool host_order_append(host_order_probe *probe, char mark) {
    if (probe == NULL || probe->trace_size + 1u >= sizeof(probe->trace))
        return false;
    probe->trace[probe->trace_size++] = mark;
    probe->trace[probe->trace_size] = '\0';
    return true;
}

static host_order_ticket *host_order_acquire_ticket(
    host_order_probe *probe, char commit_mark) {
    size_t index;
    if (probe == NULL) return NULL;
    for (index = 0u; index < HOST_ORDER_TICKET_CAPACITY; ++index) {
        host_order_ticket *ticket = &probe->tickets[index];
        if (ticket->in_use) continue;
        *ticket = (host_order_ticket){
            .probe = probe, .commit_mark = commit_mark, .in_use = true};
        return ticket;
    }
    return NULL;
}

static void host_order_commit(void *user) {
    host_order_ticket *ticket = (host_order_ticket *)user;
    if (ticket == NULL || !ticket->in_use || ticket->probe == NULL) return;
    if (ticket->commit_mark != '\0') {
        (void)host_order_append(ticket->probe, ticket->commit_mark);
        if (ticket->commit_mark == 'A' || ticket->commit_mark == 'B' ||
            ticket->commit_mark == 'X')
            ++ticket->probe->start_commits;
    }
    ticket->in_use = false;
}

static void host_order_discard(void *user) {
    host_order_ticket *ticket = (host_order_ticket *)user;
    if (ticket == NULL || !ticket->in_use || ticket->probe == NULL) return;
    ++ticket->probe->discards;
    ticket->in_use = false;
}

static bool host_order_make_ticket(
    host_order_probe *probe, char commit_mark,
    cflow_statechart_effect_ticket *out_ticket) {
    host_order_ticket *ticket =
        host_order_acquire_ticket(probe, commit_mark);
    if (ticket == NULL || out_ticket == NULL) return false;
    *out_ticket = (cflow_statechart_effect_ticket){
        host_order_commit, host_order_discard, ticket};
    return true;
}

static scxml_adapter_status host_order_prepare_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    host_order_probe *probe = (host_order_probe *)user;
    char prepare_mark = '\0';
    char commit_mark = '\0';
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->id == NULL || request->id_size == 0u ||
        probe->start_count >= HOST_ORDER_INVOKE_CAPACITY ||
        request->id_size >= sizeof(probe->starts[0]))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    if (host_text_equal(request->id, request->id_size, "outerInvoke")) {
        prepare_mark = 'a';
        commit_mark = 'A';
    } else if (host_text_equal(
                   request->id, request->id_size, "liveInvoke")) {
        prepare_mark = 'b';
        commit_mark = 'B';
    } else if (host_text_equal(
                   request->id, request->id_size, "transientInvoke")) {
        prepare_mark = 'x';
        commit_mark = 'X';
    } else {
        *out_error = "unexpected invocation ID";
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    memcpy(probe->starts[probe->start_count], request->id, request->id_size);
    probe->starts[probe->start_count][request->id_size] = '\0';
    ++probe->start_count;
    if (!host_order_append(probe, prepare_mark) ||
        !host_order_make_ticket(probe, commit_mark, out_ticket))
        return SCXML_ADAPTER_FULL;
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status host_order_prepare_invoke_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    host_order_probe *probe = (host_order_probe *)user;
    if (probe == NULL || request == NULL || request->token == 0u ||
        out_ticket == NULL || out_error == NULL ||
        !host_order_make_ticket(probe, '\0', out_ticket))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status host_order_prepare_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    host_order_probe *probe = (host_order_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL ||
        !host_text_equal(request->event, request->event_size, "selected") ||
        !host_order_append(probe, 's') ||
        !host_order_make_ticket(probe, 'S', out_ticket))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void host_order_close(void *user) {
    host_order_probe *probe = (host_order_probe *)user;
    if (probe != NULL) ++probe->close_calls;
}

static bool host_order_is_quiescent(void *user) {
    const host_order_probe *probe = (const host_order_probe *)user;
    return probe != NULL && probe->close_calls == 2u;
}

static void host_order_block_executor(void *user) {
    host_order_blocker *blocker = (host_order_blocker *)user;
    if (blocker == NULL) return;
    atomic_store(&blocker->entered, true);
    while (!atomic_load(&blocker->release)) turbo_thread_yield();
}

static bool host_admit_named_event(
    scxml_session *session, const scxml_program *program,
    const char *event_name) {
    cflow_event_view event = {0};
    const size_t event_size = event_name != NULL ? strlen(event_name) : 0u;
    return event_size != 0u &&
        scxml_program_event(program, event_name, event_size, &event) &&
        scxml_session_try_send(session, &event) == CFLOW_MAILBOX_OK;
}

static bool host_characterize_macrostep_invoke_order(void) {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='waiting'>"
        "<state id='waiting'><transition event='enter' target='outer'/>"
        "</state>"
        "<state id='outer' initial='transient'>"
        "<invoke id='outerInvoke' type='urn:test' src='outer'/>"
        "<state id='transient'>"
        "<invoke id='transientInvoke' type='urn:test' src='transient'/>"
        "<transition target='live'/></state>"
        "<state id='live'>"
        "<invoke id='liveInvoke' type='urn:test' src='live'/>"
        "<transition event='go' target='pass'>"
        "<send event='selected'/></transition></state></state>"
        "<state id='never'><transition event='noise' target='fail'/>"
        "</state><final id='pass'/><final id='fail'/></scxml>";
    const scxml_event_io_adapter_v1 event_io = {
        .abi_version = SCXML_EVENT_IO_ADAPTER_ABI_V1,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND,
        .prepare_send = host_order_prepare_send,
        .close = host_order_close,
        .is_quiescent = host_order_is_quiescent};
    const scxml_invoke_adapter_v1 invoke = {
        .abi_version = SCXML_INVOKE_ADAPTER_ABI_V1,
        .struct_size = sizeof(invoke),
        .capabilities = SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
        .prepare_start = host_order_prepare_start,
        .prepare_cancel = host_order_prepare_invoke_cancel,
        .close = host_order_close,
        .is_quiescent = host_order_is_quiescent};
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    scxml_session session = {0};
    cflow_executor executor = {0};
    cflow_statechart_instance_stats stats = {0};
    host_order_probe probe = {0};
    host_order_blocker blocker;
    scxml_session_config config;
    bool executor_initialized = false;
    bool session_initialized = false;
    bool blocker_posted = false;
    bool succeeded = false;

    atomic_init(&blocker.entered, false);
    atomic_init(&blocker.release, false);
    if (host_compile(source, &program, &diagnostic) != SCXML_OK ||
        !cflow_executor_serial_init(&executor))
        goto cleanup;
    executor_initialized = true;
    config = host_session_config(&program, &executor);
    config.event_io = &event_io;
    config.adapter_user = &probe;
    config.invoke = &invoke;
    config.invoke_user = &probe;
    config.invocation_capacity = HOST_ORDER_INVOKE_CAPACITY;
    if (scxml_session_init(&session, &config) !=
        CFLOW_STATECHART_INSTANCE_OK)
        goto cleanup;
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor) ||
        cflow_executor_try_post(
            &executor, host_order_block_executor, &blocker) !=
            CFLOW_ADMISSION_ACCEPTED)
        goto cleanup;
    blocker_posted = true;
    while (!atomic_load(&blocker.entered)) turbo_thread_yield();
    if (!host_admit_named_event(&session, &program, "enter") ||
        !host_admit_named_event(&session, &program, "noise") ||
        !host_admit_named_event(&session, &program, "go"))
        goto cleanup;
    atomic_store(&blocker.release, true);
    blocker_posted = false;
    if (!cflow_executor_wait_idle(&executor) ||
        !scxml_session_get_stats(&session, &stats))
        goto cleanup;
    succeeded = stats.done && !stats.errored &&
        probe.start_count == 2u && probe.start_commits == 2u &&
        probe.discards == 0u &&
        strcmp(probe.starts[0], "outerInvoke") == 0 &&
        strcmp(probe.starts[1], "liveInvoke") == 0 &&
        strcmp(probe.trace, "abABsS") == 0;

cleanup:
    if (blocker_posted) atomic_store(&blocker.release, true);
    if (session_initialized &&
        scxml_session_destroy(&session) != CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    return succeeded;
}

typedef enum host_w3c_route_kind {
    HOST_W3C_INVOKE_TARGET = 0,
    HOST_W3C_BIDIRECTIONAL
} host_w3c_route_kind;

static bool host_run_w3c_route_fixture(
    const char *fixture_name, host_w3c_route_kind kind) {
    static const char invoke_child_source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
        "<state id='waiting'><transition event='parentToChild' "
        "target='done'><send target='#_parent' event='eventReceived'/>"
        "</transition></state><final id='done'/></scxml>";
    static const char roundtrip_child_source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
        "<state id='waiting'><onentry><send target='#_parent' "
        "event='childToParent'/></onentry><transition "
        "event='parentToChild' target='done'><send target='#_parent' "
        "event='eventReceived'/></transition></state>"
        "<final id='done'/></scxml>";
    const char *child_source = kind == HOST_W3C_INVOKE_TARGET
        ? invoke_child_source : roundtrip_child_source;
    const char *alias = kind == HOST_W3C_INVOKE_TARGET
        ? "#_invokedChild" : "#_child";
    const size_t expected_deliveries = kind == HOST_W3C_INVOKE_TARGET
        ? 2u : 3u;
    host_router router;
    host_adapter_context parent_adapter;
    host_adapter_context child_adapter;
    scxml_program parent_program = {0};
    scxml_program child_program = {0};
    scxml_diagnostic diagnostic = {0};
    scxml_session parent = {0};
    scxml_session child = {0};
    cflow_executor parent_executor = {0};
    cflow_executor child_executor = {0};
    scxml_session_config parent_config;
    scxml_session_config child_config;
    scxml_session_adapters_v3 parent_adapters;
    scxml_session_adapters_v3 child_adapters;
    cflow_statechart_instance_stats parent_stats = {0};
    cflow_statechart_instance_stats child_stats = {0};
    size_t parent_endpoint = SIZE_MAX;
    size_t child_endpoint = SIZE_MAX;
    size_t delivery;
    bool router_initialized = false;
    bool parent_executor_initialized = false;
    bool child_executor_initialized = false;
    bool parent_initialized = false;
    bool child_initialized = false;
    bool succeeded = false;

    if (fixture_name == NULL ||
        (kind != HOST_W3C_INVOKE_TARGET &&
         kind != HOST_W3C_BIDIRECTIONAL))
        return false;
    if (!host_router_init(&router, HOST_MESSAGE_CAPACITY)) goto cleanup;
    router_initialized = true;
    host_adapter_init(&parent_adapter, &router);
    host_adapter_init(&child_adapter, &router);
    if (host_compile_fixture(
            fixture_name, &parent_program, &diagnostic) != SCXML_OK ||
        host_compile(child_source, &child_program, &diagnostic) != SCXML_OK ||
        !cflow_executor_serial_init(&parent_executor))
        goto cleanup;
    parent_executor_initialized = true;
    if (!cflow_executor_serial_init(&child_executor)) goto cleanup;
    child_executor_initialized = true;
    parent_config = host_session_config(&parent_program, &parent_executor);
    child_config = host_session_config(&child_program, &child_executor);
    parent_adapters = (scxml_session_adapters_v3){
        .abi_version = SCXML_SESSION_ADAPTERS_ABI_V3,
        .struct_size = sizeof(parent_adapters),
        .event_io = &HOST_ADAPTER,
        .event_io_user = &parent_adapter};
    child_adapters = (scxml_session_adapters_v3){
        .abi_version = SCXML_SESSION_ADAPTERS_ABI_V3,
        .struct_size = sizeof(child_adapters),
        .event_io = &HOST_ADAPTER,
        .event_io_user = &child_adapter};
    if (!host_router_reserve(
            &router, true, &parent_adapter, &parent_endpoint) ||
        !host_router_reserve(
            &router, true, &child_adapter, &child_endpoint) ||
        !host_router_set_parent(
            &router, child_endpoint, parent_endpoint) ||
        !host_router_set_invoke_alias(
            &router, parent_endpoint, alias, child_endpoint) ||
        scxml_session_init_v3(
            &parent, &parent_config, &parent_adapters) !=
            CFLOW_STATECHART_INSTANCE_OK)
        goto cleanup;
    parent_initialized = true;
    if (!host_router_activate(
            &router, parent_endpoint, &parent, &parent_program) ||
        scxml_session_init_v3(
            &child, &child_config, &child_adapters) !=
            CFLOW_STATECHART_INSTANCE_OK)
        goto cleanup;
    child_initialized = true;
    if (!host_router_activate(
            &router, child_endpoint, &child, &child_program) ||
        !cflow_executor_wait_idle(&parent_executor) ||
        !cflow_executor_wait_idle(&child_executor))
        goto cleanup;
    for (delivery = 0u; delivery < expected_deliveries; ++delivery) {
        if (host_router_ready_count(&router) != 1u ||
            host_router_pump(&router) != HOST_PUMP_DELIVERED ||
            !cflow_executor_wait_idle(&parent_executor) ||
            !cflow_executor_wait_idle(&child_executor))
            goto cleanup;
    }
    if (host_router_ready_count(&router) != 0u ||
        router.last_delivery.count != expected_deliveries ||
        strcmp(router.last_delivery.event, "eventReceived") != 0 ||
        !scxml_session_get_stats(&parent, &parent_stats) ||
        !scxml_session_get_stats(&child, &child_stats))
        goto cleanup;
    succeeded = parent_stats.done && !parent_stats.errored &&
        child_stats.done && !child_stats.errored;

cleanup:
    if (child_initialized &&
        scxml_session_destroy(&child) != CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (parent_initialized &&
        scxml_session_destroy(&parent) != CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (router_initialized && child_endpoint != SIZE_MAX)
        (void)host_router_unregister(&router, child_endpoint);
    if (router_initialized && parent_endpoint != SIZE_MAX)
        (void)host_router_unregister(&router, parent_endpoint);
    if (child_executor_initialized)
        cflow_executor_destroy(&child_executor);
    if (parent_executor_initialized)
        cflow_executor_destroy(&parent_executor);
    scxml_program_destroy(&child_program);
    scxml_program_destroy(&parent_program);
    if (router_initialized) host_router_destroy(&router);
    return succeeded;
}

spec("SCXML host Event I/O adapter contract") {
    it("commits live invocations in document order before external selection") {
        check_true(host_characterize_macrostep_invoke_order());
    }

    it("routes an initial child send through #_parent") {
        static const char parent_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='waiting'><transition event='childToParent' "
            "target='pass'/></state><final id='pass'/></scxml>";
        host_router router;
        host_adapter_context child_adapter;
        scxml_program parent_program = {0};
        scxml_program child_program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session parent = {0};
        scxml_session child = {0};
        cflow_executor parent_executor = {0};
        cflow_executor child_executor = {0};
        scxml_session_config parent_config;
        scxml_session_config child_config;
        scxml_session_adapters_v3 child_adapters;
        cflow_statechart_instance_stats stats = {0};
        size_t parent_endpoint = SIZE_MAX;
        size_t child_endpoint = SIZE_MAX;

        check_true(host_router_init(&router, HOST_MESSAGE_CAPACITY));
        host_adapter_init(&child_adapter, &router);
        check_equal(host_compile(parent_source, &parent_program,
                                 &diagnostic), SCXML_OK);
        check_equal(host_compile_fixture(
                        "test191.scxml", &child_program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&parent_executor));
        check_true(cflow_executor_serial_init(&child_executor));
        parent_config = host_session_config(
            &parent_program, &parent_executor);
        child_config = host_session_config(
            &child_program, &child_executor);
        child_adapters = (scxml_session_adapters_v3){
            .abi_version = SCXML_SESSION_ADAPTERS_ABI_V3,
            .struct_size = sizeof(child_adapters),
            .event_io = &HOST_ADAPTER,
            .event_io_user = &child_adapter};
        check_equal(scxml_session_init(&parent, &parent_config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(host_router_register(
            &router, &parent, &parent_program, true, NULL,
            &parent_endpoint));
        check_true(host_router_reserve(
            &router, true, &child_adapter, &child_endpoint));
        check_true(host_router_set_parent(
            &router, child_endpoint, parent_endpoint));
        check_equal(scxml_session_init_v3(
                        &child, &child_config, &child_adapters),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(host_router_activate(
            &router, child_endpoint, &child, &child_program));
        check_true(cflow_executor_wait_idle(&child_executor));
        check_equal(host_router_pump(&router), HOST_PUMP_DELIVERED);
        check_true(cflow_executor_wait_idle(&parent_executor));
        check_true(scxml_session_get_stats(&parent, &stats));
        check_true(stats.done);

        check_equal(scxml_session_destroy(&child),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(host_router_unregister(&router, child_endpoint));
        check_true(host_router_unregister(&router, parent_endpoint));
        check_equal(scxml_session_destroy(&parent),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&child_executor);
        cflow_executor_destroy(&parent_executor);
        scxml_program_destroy(&child_program);
        scxml_program_destroy(&parent_program);
        host_router_destroy(&router);
    }

    it("routes a parent send through #_invokeid") {
        check_true(host_run_w3c_route_fixture(
            "test192.scxml", HOST_W3C_INVOKE_TARGET));
    }

    it("exchanges SCXML Events between parent and child sessions") {
        check_true(host_run_w3c_route_fixture(
            "test347.scxml", HOST_W3C_BIDIRECTIONAL));
    }

    it("publishes committed cross-session sends with SCXML field mapping") {
        static const char receiver_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='waiting'><transition event='ping' target='done'/>"
            "</state><final id='done'/></scxml>";
        host_router router;
        host_adapter_context sender_adapter;
        scxml_program receiver_program = {0};
        scxml_program sender_program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session receiver = {0};
        scxml_session sender = {0};
        cflow_executor receiver_executor = {0};
        cflow_executor sender_executor = {0};
        scxml_session_config receiver_config;
        scxml_session_config sender_config;
        scxml_session_adapters_v3 adapters;
        cflow_statechart_instance_stats stats = {0};
        cflow_event_view go = {0};
        char receiver_location[HOST_LOCATION_CAPACITY];
        char sender_location[HOST_LOCATION_CAPACITY];
        char sender_source[512];
        size_t receiver_required = 0u;
        size_t sender_required = 0u;
        size_t receiver_endpoint = SIZE_MAX;
        size_t sender_endpoint = SIZE_MAX;

        check_true(host_router_init(&router, HOST_MESSAGE_CAPACITY));
        host_adapter_init(&sender_adapter, &router);
        check_equal(host_compile(receiver_source, &receiver_program,
                                 &diagnostic), SCXML_OK);
        check_true(cflow_executor_serial_init(&receiver_executor));
        receiver_config = host_session_config(
            &receiver_program, &receiver_executor);
        check_equal(scxml_session_init(&receiver, &receiver_config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_session_copy_location(
                        &receiver, receiver_location,
                        sizeof(receiver_location), &receiver_required),
                    SCXML_LOCATION_OK);
        check_true(host_router_register(
            &router, &receiver, &receiver_program, true, NULL,
            &receiver_endpoint));

        check_true(snprintf(
            sender_source, sizeof(sender_source),
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='waiting'><transition event='go' target='sent'>"
            "<send event='ping' target='%s' id='send-1'/></transition>"
            "</state><state id='sent'/></scxml>",
            receiver_location) > 0);
        check_equal(host_compile(sender_source, &sender_program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&sender_executor));
        sender_config = host_session_config(&sender_program, &sender_executor);
        adapters = (scxml_session_adapters_v3){
            .abi_version = SCXML_SESSION_ADAPTERS_ABI_V3,
            .struct_size = sizeof(adapters),
            .event_io = &HOST_ADAPTER,
            .event_io_user = &sender_adapter};
        check_equal(scxml_session_init_v3(
                        &sender, &sender_config, &adapters),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(host_router_register(
            &router, &sender, &sender_program, true, &sender_adapter,
            &sender_endpoint));
        check_equal(scxml_session_copy_location(
                        &sender, sender_location, sizeof(sender_location),
                        &sender_required),
                    SCXML_LOCATION_OK);

        check_true(scxml_program_event(
            &sender_program, "go", 2u, &go));
        check_equal(scxml_session_try_send(&sender, &go),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&sender_executor));
        check_equal(host_router_ready_count(&router), (size_t)1u);
        check_true(scxml_session_get_stats(&receiver, &stats));
        check_false(stats.done);
        check_equal(host_router_pump(&router), HOST_PUMP_DELIVERED);
        check_true(cflow_executor_wait_idle(&receiver_executor));
        check_true(scxml_session_get_stats(&receiver, &stats));
        check_true(stats.done);
        check_equal(router.last_delivery.count, (size_t)1u);
        check_equal(router.last_delivery.event, "ping");
        check_equal(router.last_delivery.target, receiver_location);
        check_equal(router.last_delivery.send_id, "send-1");
        check_equal(router.last_delivery.origin, sender_location);
        check_equal(router.last_delivery.origin_type, HOST_ORIGIN_TYPE);

        check_equal(scxml_session_destroy(&sender),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(host_router_unregister(&router, sender_endpoint));
        check_true(host_router_unregister(&router, receiver_endpoint));
        check_equal(scxml_session_destroy(&receiver),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&sender_executor);
        cflow_executor_destroy(&receiver_executor);
        scxml_program_destroy(&sender_program);
        scxml_program_destroy(&receiver_program);
        host_router_destroy(&router);
    }

    it("maps inaccessible sessions to error.communication") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='waiting'><transition event='go'>"
            "<send event='ping' target='#_scxml_missing'/></transition>"
            "<transition event='error.communication' target='done'/>"
            "</state><final id='done'/></scxml>";
        host_router router;
        host_adapter_context adapter;
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        scxml_session_config config;
        scxml_session_adapters_v3 adapters;
        cflow_statechart_instance_stats stats = {0};
        cflow_event_view go = {0};
        size_t endpoint = SIZE_MAX;

        check_true(host_router_init(&router, 1u));
        host_adapter_init(&adapter, &router);
        check_equal(host_compile(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        config = host_session_config(&program, &executor);
        adapters = (scxml_session_adapters_v3){
            .abi_version = SCXML_SESSION_ADAPTERS_ABI_V3,
            .struct_size = sizeof(adapters),
            .event_io = &HOST_ADAPTER,
            .event_io_user = &adapter};
        check_equal(scxml_session_init_v3(
                        &session, &config, &adapters),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(host_router_register(
            &router, &session, &program, true, &adapter, &endpoint));
        check_true(scxml_program_event(&program, "go", 2u, &go));
        check_equal(scxml_session_try_send(&session, &go),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(host_router_ready_count(&router), (size_t)0u);
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);

        check_equal(scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(host_router_unregister(&router, endpoint));
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
        host_router_destroy(&router);
    }

    it("enforces bounded reservations special routes and close quiescence") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='waiting'><transition event='ping'/></state></scxml>";
        host_router router;
        host_adapter_context adapter;
        scxml_program owner_program = {0};
        scxml_program peer_program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session owner = {0};
        scxml_session peer = {0};
        cflow_executor owner_executor = {0};
        cflow_executor peer_executor = {0};
        scxml_session_config owner_config;
        scxml_session_config peer_config;
        scxml_session_adapters_v3 adapters;
        scxml_send_request_v3 request = {0};
        cflow_statechart_effect_ticket first = {0};
        cflow_statechart_effect_ticket second = {0};
        const char *error = NULL;
        size_t owner_endpoint = SIZE_MAX;
        size_t peer_endpoint = SIZE_MAX;

        check_true(host_router_init(&router, 1u));
        host_adapter_init(&adapter, &router);
        check_equal(host_compile(source, &owner_program, &diagnostic),
                    SCXML_OK);
        check_equal(host_compile(source, &peer_program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&owner_executor));
        check_true(cflow_executor_serial_init(&peer_executor));
        owner_config = host_session_config(&owner_program, &owner_executor);
        peer_config = host_session_config(&peer_program, &peer_executor);
        adapters = (scxml_session_adapters_v3){
            .abi_version = SCXML_SESSION_ADAPTERS_ABI_V3,
            .struct_size = sizeof(adapters),
            .event_io = &HOST_ADAPTER,
            .event_io_user = &adapter};
        check_equal(scxml_session_init_v3(
                        &owner, &owner_config, &adapters),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_session_init(&peer, &peer_config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(host_router_register(
            &router, &owner, &owner_program, false, &adapter,
            &owner_endpoint));
        check_true(host_router_register(
            &router, &peer, &peer_program, true, NULL, &peer_endpoint));
        check_true(host_router_set_parent(
            &router, owner_endpoint, peer_endpoint));
        check_true(host_router_set_invoke_alias(
            &router, owner_endpoint, "#_child", peer_endpoint));
        request.base.event = "ping";
        request.base.event_size = 4u;
        request.base.type = HOST_SCXML_PROCESSOR;
        request.base.type_size = sizeof(HOST_SCXML_PROCESSOR) - 1u;

        request.base.target = "#_parent";
        request.base.target_size = 8u;
        check_equal(host_prepare_send(
                        &adapter, &request, &first, &error),
                    SCXML_ADAPTER_ACCEPTED);
        check_equal(host_prepare_send(
                        &adapter, &request, &second, &error),
                    SCXML_ADAPTER_FULL);
        first.discard(first.user);

        request.base.target = "#_child";
        request.base.target_size = 7u;
        check_equal(host_prepare_send(
                        &adapter, &request, &first, &error),
                    SCXML_ADAPTER_ACCEPTED);
        first.discard(first.user);
        request.base.type = "urn:unsupported";
        request.base.type_size = sizeof("urn:unsupported") - 1u;
        check_equal(host_prepare_send(
                        &adapter, &request, &second, &error),
                    SCXML_ADAPTER_ERROR_EXECUTION);
        check_not_null(error);
        request.base.type = NULL;
        request.base.type_size = 0u;
        request.base.target = NULL;
        request.base.target_size = 0u;
        check_equal(host_prepare_send(
                        &adapter, &request, &first, &error),
                    SCXML_ADAPTER_ACCEPTED);
        scxml_session_close(&owner);
        check_false(host_adapter_is_quiescent(&adapter));
        check_equal(host_prepare_send(
                        &adapter, &request, &second, &error),
                    SCXML_ADAPTER_CLOSED);
        first.discard(first.user);
        check_true(host_adapter_is_quiescent(&adapter));
        check_equal(host_router_ready_count(&router), (size_t)0u);

        check_equal(scxml_session_destroy(&owner),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(host_router_unregister(&router, owner_endpoint));
        check_true(host_router_unregister(&router, peer_endpoint));
        check_equal(scxml_session_destroy(&peer),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&owner_executor);
        cflow_executor_destroy(&peer_executor);
        scxml_program_destroy(&owner_program);
        scxml_program_destroy(&peer_program);
        host_router_destroy(&router);
    }
}
