#include <cflow/executor.h>
#include <cmeta/struct.h>
#include <scxml/scxml.h>

#include "tinytest.h"

#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <salts/thread.h>

#define HOST_ENDPOINT_CAPACITY 4u
#define HOST_MESSAGE_CAPACITY 4u
#define HOST_LOCATION_CAPACITY 64u
#define HOST_EVENT_CAPACITY 64u
#define HOST_SEND_ID_CAPACITY 64u
#define HOST_TARGET_CAPACITY 64u

static const char HOST_SCXML_PROCESSOR[] =
    "http://www.w3.org/TR/scxml/#SCXMLEventProcessor";
static const char HOST_ORIGIN_TYPE[] = "scxml";

Struct(host_cmeta_state,
    (int, unused)
);

static const cmeta_type_traits HOST_CMETA_STATE_TRAITS = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY};
static const cmeta_type_desc HOST_CMETA_STATE_TYPE = {
    .name = "host_cmeta_state",
    .size = sizeof(host_cmeta_state),
    .align = _Alignof(host_cmeta_state),
    .kind = CMETA_T_OBJECT,
    .traits = &HOST_CMETA_STATE_TRAITS};
static const cmeta_data_field_desc HOST_CMETA_STATE_FIELDS[] = {
    {"test.scxml.host.unused", "unused", offsetof(host_cmeta_state, unused),
     &cmeta_data_int}};
static const cmeta_data_struct_shape HOST_CMETA_STATE_SHAPE = {
    .layout = StructMeta(host_cmeta_state),
    .fields = HOST_CMETA_STATE_FIELDS,
    .field_count = sizeof(HOST_CMETA_STATE_FIELDS) /
                   sizeof(HOST_CMETA_STATE_FIELDS[0])};
static const cmeta_data_desc HOST_CMETA_STATE_DESC = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.host.state",
    .display_name = "SCXML host state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &HOST_CMETA_STATE_TYPE,
    .shape = &HOST_CMETA_STATE_SHAPE};

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
    salts_mutex_t lock;
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
    salts_mutex_init(&router->lock);
    return router->lock != NULL;
}

static void host_router_destroy(host_router *router) {
    if (router == NULL) return;
    if (router->lock != NULL) salts_mutex_destroy(&router->lock);
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
    salts_mutex_lock(&router->lock);
    if (adapter->router != router || adapter->endpoint != SIZE_MAX ||
        adapter->closed) {
        salts_mutex_unlock(&router->lock);
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
        salts_mutex_unlock(&router->lock);
        return true;
    }
    salts_mutex_unlock(&router->lock);
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
    salts_mutex_lock(&router->lock);
    endpoint = &router->endpoints[endpoint_index];
    if (!endpoint->in_use || endpoint->active) {
        salts_mutex_unlock(&router->lock);
        return false;
    }
    for (index = 0u; index < HOST_ENDPOINT_CAPACITY; ++index) {
        if (index != endpoint_index && router->endpoints[index].active &&
            strcmp(router->endpoints[index].location, location) == 0) {
            salts_mutex_unlock(&router->lock);
            return false;
        }
    }
    endpoint->session = session;
    endpoint->program = program;
    memcpy(endpoint->location, location, required);
    endpoint->active = true;
    salts_mutex_unlock(&router->lock);
    return true;
}

static bool host_router_unregister(host_router *router, size_t endpoint);
static size_t host_router_discard_pending(host_router *router);

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
        salts_mutex_lock(&router->lock);
        router->endpoints[endpoint].adapter = NULL;
        salts_mutex_unlock(&router->lock);
    }
    *out_endpoint = endpoint;
    return true;
}

static bool host_router_unregister(host_router *router, size_t endpoint) {
    size_t index;
    if (router == NULL || endpoint >= HOST_ENDPOINT_CAPACITY) return false;
    salts_mutex_lock(&router->lock);
    if (!router->endpoints[endpoint].in_use) {
        salts_mutex_unlock(&router->lock);
        return false;
    }
    for (index = 0u; index < router->message_capacity; ++index) {
        const host_message *message = &router->messages[index];
        if (message->state != HOST_MESSAGE_FREE &&
            (message->source == endpoint || message->target == endpoint)) {
            salts_mutex_unlock(&router->lock);
            return false;
        }
    }
    if (router->endpoints[endpoint].adapter != NULL)
        router->endpoints[endpoint].adapter->endpoint = SIZE_MAX;
    router->endpoints[endpoint] = (host_endpoint){
        .parent = SIZE_MAX, .invoke_target = SIZE_MAX};
    salts_mutex_unlock(&router->lock);
    return true;
}

static bool host_router_set_parent(host_router *router, size_t child,
                                   size_t parent) {
    bool valid;
    if (router == NULL || child >= HOST_ENDPOINT_CAPACITY ||
        parent >= HOST_ENDPOINT_CAPACITY)
        return false;
    salts_mutex_lock(&router->lock);
    valid = router->endpoints[child].in_use &&
        router->endpoints[parent].in_use;
    if (valid) router->endpoints[child].parent = parent;
    salts_mutex_unlock(&router->lock);
    return valid;
}

static bool host_router_set_invoke_alias(
    host_router *router, size_t owner, const char *alias, size_t target) {
    bool valid;
    if (router == NULL || owner >= HOST_ENDPOINT_CAPACITY ||
        target >= HOST_ENDPOINT_CAPACITY || alias == NULL)
        return false;
    salts_mutex_lock(&router->lock);
    valid = router->endpoints[owner].in_use &&
        router->endpoints[target].in_use &&
        host_copy(router->endpoints[owner].invoke_alias,
                  sizeof(router->endpoints[owner].invoke_alias),
                  alias, strlen(alias));
    if (valid) router->endpoints[owner].invoke_target = target;
    salts_mutex_unlock(&router->lock);
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

static size_t host_router_discard_pending(host_router *router) {
    size_t discarded = 0u;
    size_t index;
    if (router == NULL) return SIZE_MAX;
    salts_mutex_lock(&router->lock);
    for (index = 0u; index < router->message_capacity; ++index) {
        if (router->messages[index].state == HOST_MESSAGE_INFLIGHT) {
            salts_mutex_unlock(&router->lock);
            return SIZE_MAX;
        }
    }
    for (index = 0u; index < router->message_capacity; ++index) {
        if (router->messages[index].state == HOST_MESSAGE_RESERVED ||
            router->messages[index].state == HOST_MESSAGE_READY) {
            host_release_message_locked(&router->messages[index]);
            ++discarded;
        }
    }
    salts_mutex_unlock(&router->lock);
    return discarded;
}

static void host_ticket_commit(void *user) {
    host_message *message = (host_message *)user;
    host_router *router = message != NULL ? message->router : NULL;
    if (router == NULL) return;
    salts_mutex_lock(&router->lock);
    if (message->state == HOST_MESSAGE_RESERVED) {
        message->state = HOST_MESSAGE_READY;
        message->sequence = router->next_sequence++;
    }
    salts_mutex_unlock(&router->lock);
}

static void host_ticket_discard(void *user) {
    host_message *message = (host_message *)user;
    host_router *router = message != NULL ? message->router : NULL;
    if (router == NULL) return;
    salts_mutex_lock(&router->lock);
    if (message->state == HOST_MESSAGE_RESERVED)
        host_release_message_locked(message);
    salts_mutex_unlock(&router->lock);
}

static scxml_adapter_status host_prepare_send(
    void *user, const scxml_send_request *request,
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
    if (request->type_size != 0u &&
        !host_text_equal(request->type, request->type_size,
                         HOST_SCXML_PROCESSOR)) {
        *out_error = "unsupported Event I/O processor type";
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    salts_mutex_lock(&router->lock);
    if (adapter->closed) {
        salts_mutex_unlock(&router->lock);
        return SCXML_ADAPTER_CLOSED;
    }
    source_relative_target = request->target_size == 0u ||
        host_text_equal(request->target, request->target_size,
                        "#_parent") ||
        (router->endpoints[adapter->endpoint].invoke_target != SIZE_MAX &&
         host_text_equal(
             request->target, request->target_size,
             router->endpoints[adapter->endpoint].invoke_alias));
    target = host_resolve_target_locked(
        router, adapter->endpoint, request->target,
        request->target_size);
    if (target == SIZE_MAX || !router->endpoints[target].in_use ||
        (!source_relative_target &&
         !router->endpoints[target].accessible)) {
        salts_mutex_unlock(&router->lock);
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
        salts_mutex_unlock(&router->lock);
        return SCXML_ADAPTER_FULL;
    }
    if (!host_copy(message->event, sizeof(message->event),
                   request->event, request->event_size) ||
        !host_copy(message->target_text, sizeof(message->target_text),
                   request->target, request->target_size) ||
        !host_copy(message->send_id, sizeof(message->send_id),
                   request->id, request->id_size)) {
        memset(message, 0, sizeof(*message));
        salts_mutex_unlock(&router->lock);
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
    salts_mutex_unlock(&router->lock);
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
    salts_mutex_lock(&adapter->router->lock);
    adapter->closed = true;
    salts_mutex_unlock(&adapter->router->lock);
}

static bool host_adapter_is_quiescent(void *user) {
    host_adapter_context *adapter = (host_adapter_context *)user;
    bool quiescent;
    if (adapter == NULL || adapter->router == NULL) return false;
    salts_mutex_lock(&adapter->router->lock);
    quiescent = adapter->closed && adapter->outstanding == 0u;
    salts_mutex_unlock(&adapter->router->lock);
    return quiescent;
}

static const scxml_event_io_adapter HOST_ADAPTER = {
    .abi_version = SCXML_ADAPTER_ABI,
    .struct_size = sizeof(scxml_event_io_adapter),
    .capabilities = SCXML_EVENT_IO_CAP_SEND,
    .prepare_send = host_prepare_send,
    .prepare_cancel = host_prepare_cancel,
    .close = host_adapter_close,
    .is_quiescent = host_adapter_is_quiescent};

static size_t host_router_ready_count(host_router *router) {
    size_t count = 0u;
    size_t index;
    salts_mutex_lock(&router->lock);
    for (index = 0u; index < router->message_capacity; ++index) {
        if (router->messages[index].state == HOST_MESSAGE_READY) ++count;
    }
    salts_mutex_unlock(&router->lock);
    return count;
}

static host_pump_status host_router_pump(host_router *router) {
    host_message snapshot = {0};
    host_message *selected = NULL;
    host_endpoint target = {0};
    host_endpoint source = {0};
    scxml_event_metadata metadata = {0};
    cflow_mailbox_status mailbox_status = CFLOW_MAILBOX_INVALID_ARGUMENT;
    size_t index;
    if (router == NULL) return HOST_PUMP_EMPTY;
    salts_mutex_lock(&router->lock);
    for (index = 0u; index < router->message_capacity; ++index) {
        host_message *candidate = &router->messages[index];
        if (candidate->state == HOST_MESSAGE_READY &&
            (selected == NULL || candidate->sequence < selected->sequence))
            selected = candidate;
    }
    if (selected == NULL) {
        salts_mutex_unlock(&router->lock);
        return HOST_PUMP_EMPTY;
    }
    selected->state = HOST_MESSAGE_INFLIGHT;
    snapshot = *selected;
    source = router->endpoints[snapshot.source];
    target = router->endpoints[snapshot.target];
    salts_mutex_unlock(&router->lock);

    metadata = (scxml_event_metadata){
        .abi_version = SCXML_EVENT_METADATA_ABI,
        .struct_size = sizeof(metadata),
        .send_id = snapshot.send_id,
        .send_id_size = strlen(snapshot.send_id),
        .origin = source.location,
        .origin_size = strlen(source.location),
        .origin_type = HOST_ORIGIN_TYPE,
        .origin_type_size = sizeof(HOST_ORIGIN_TYPE) - 1u};
    if (source.active && target.active && target.accessible)
        mailbox_status = scxml_session_try_send_named_with_metadata(
            target.session, snapshot.event, strlen(snapshot.event),
            &metadata);

    salts_mutex_lock(&router->lock);
    if (mailbox_status == CFLOW_MAILBOX_FULL) {
        selected->state = HOST_MESSAGE_READY;
        salts_mutex_unlock(&router->lock);
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
    salts_mutex_unlock(&router->lock);
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

static bool host_read_fixture(
    const char *fixture_name, char **out_source, size_t *out_size) {
    char path[512];
    int path_size;
    if (fixture_name == NULL || out_source == NULL || out_size == NULL)
        return false;
    *out_source = NULL;
    *out_size = 0u;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path))
        return false;
    *out_source = tt_read_file(path, out_size);
    return *out_source != NULL;
}

static scxml_status host_compile_fixture(
    const char *fixture_name, scxml_program *program,
    scxml_diagnostic *diagnostic) {
    char *source = NULL;
    size_t source_size = 0u;
    scxml_status status;
    if (program == NULL || diagnostic == NULL)
        return SCXML_INVALID_ARGUMENT;
    if (!host_read_fixture(fixture_name, &source, &source_size))
        return SCXML_XML_ERROR;
    status = scxml_compile(
        program, source, source_size, NULL, diagnostic);
    free(source);
    return status;
}

static scxml_status host_compile_cmeta_fixture(
    const char *fixture_name, scxml_program *program,
    scxml_diagnostic *diagnostic) {
    const scxml_cmeta_compile_options_v1 options =
        scxml_cmeta_default_compile_options(&HOST_CMETA_STATE_DESC);
    char *source = NULL;
    size_t source_size = 0u;
    scxml_status status;
    if (program == NULL || diagnostic == NULL)
        return SCXML_INVALID_ARGUMENT;
    if (!host_read_fixture(fixture_name, &source, &source_size))
        return SCXML_XML_ERROR;
    status = scxml_compile_cmeta(
        program, source, source_size, NULL, &options, diagnostic);
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
    while (!atomic_load(&blocker->release)) salts_thread_yield();
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
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND,
        .prepare_send = host_order_prepare_send,
        .close = host_order_close,
        .is_quiescent = host_order_is_quiescent};
    const scxml_invoke_adapter invoke = {
        .abi_version = SCXML_ADAPTER_ABI,
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
    while (!atomic_load(&blocker.entered)) salts_thread_yield();
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

typedef enum host_invoke_ticket_kind {
    HOST_INVOKE_TICKET_NONE = 0,
    HOST_INVOKE_TICKET_START,
    HOST_INVOKE_TICKET_CANCEL
} host_invoke_ticket_kind;

typedef struct host_invoke_probe host_invoke_probe;

typedef struct host_invoke_ticket_row {
    host_invoke_probe *probe;
    host_invoke_ticket_kind kind;
    bool live;
    bool terminalized;
} host_invoke_ticket_row;

struct host_invoke_probe {
    uint64_t token;
    char id[HOST_SEND_ID_CAPACITY];
    size_t start_prepares;
    size_t start_commits;
    size_t start_discards;
    size_t cancel_prepares;
    size_t cancel_commits;
    size_t cancel_discards;
    size_t terminal_violations;
    host_invoke_ticket_row start_ticket;
    host_invoke_ticket_row cancel_ticket;
    bool duplicate_terminal_on_close;
    bool closed;
};

static bool host_invoke_terminal(
    host_invoke_ticket_row *ticket, bool committed) {
    host_invoke_probe *probe = ticket != NULL ? ticket->probe : NULL;
    if (probe == NULL) return false;
    if (!ticket->live || ticket->terminalized) {
        ++probe->terminal_violations;
        return false;
    }
    if (ticket->kind == HOST_INVOKE_TICKET_START) {
        if (committed)
            ++probe->start_commits;
        else
            ++probe->start_discards;
    } else if (ticket->kind == HOST_INVOKE_TICKET_CANCEL) {
        if (committed)
            ++probe->cancel_commits;
        else
            ++probe->cancel_discards;
    } else {
        ++probe->terminal_violations;
        return false;
    }
    ticket->live = false;
    ticket->terminalized = true;
    return true;
}

static void host_invoke_commit(void *user) {
    (void)host_invoke_terminal((host_invoke_ticket_row *)user, true);
}

static void host_invoke_discard(void *user) {
    (void)host_invoke_terminal((host_invoke_ticket_row *)user, false);
}

static bool host_invoke_prepare_ticket(
    host_invoke_probe *probe, host_invoke_ticket_kind kind,
    cflow_statechart_effect_ticket *out_ticket) {
    host_invoke_ticket_row *ticket;
    if (probe == NULL || out_ticket == NULL ||
        (kind != HOST_INVOKE_TICKET_START &&
         kind != HOST_INVOKE_TICKET_CANCEL))
        return false;
    ticket = kind == HOST_INVOKE_TICKET_START
        ? &probe->start_ticket : &probe->cancel_ticket;
    if (ticket->live || ticket->terminalized) return false;
    *ticket = (host_invoke_ticket_row){
        .probe = probe, .kind = kind, .live = true};
    *out_ticket = (cflow_statechart_effect_ticket){
        host_invoke_commit, host_invoke_discard, ticket};
    return true;
}

static scxml_adapter_status host_invoke_prepare_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    static const char expected_id[] = "foo";
    static const char expected_type[] = "http://www.w3.org/TR/scxml/";
    static const char expected_src[] = "test253-child.scxml";
    host_invoke_probe *probe = (host_invoke_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || probe->closed || probe->start_ticket.live ||
        request->token == 0u || request->id == NULL ||
        request->type == NULL || request->src == NULL ||
        request->autoforward || request->payload.kind != SCXML_PAYLOAD_NONE ||
        !host_text_equal(request->id, request->id_size, expected_id) ||
        !host_text_equal(request->type, request->type_size, expected_type) ||
        !host_text_equal(request->src, request->src_size, expected_src) ||
        probe->start_prepares != 0u ||
        !host_copy(probe->id, sizeof(probe->id),
                   request->id, request->id_size) ||
        !host_invoke_prepare_ticket(
            probe, HOST_INVOKE_TICKET_START, out_ticket)) {
        if (out_error != NULL)
            *out_error = "unexpected invoked SCXML start request";
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    probe->token = request->token;
    ++probe->start_prepares;
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status host_invoke_prepare_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    host_invoke_probe *probe = (host_invoke_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || probe->closed || probe->cancel_ticket.live ||
        request->token == 0u || request->id == NULL ||
        request->token != probe->token || probe->start_commits != 1u ||
        probe->cancel_prepares != 0u ||
        !host_text_equal(request->id, request->id_size, probe->id) ||
        !host_invoke_prepare_ticket(
            probe, HOST_INVOKE_TICKET_CANCEL, out_ticket)) {
        if (out_error != NULL)
            *out_error = "unexpected invoked SCXML cancel request";
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    ++probe->cancel_prepares;
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void host_invoke_close(void *user) {
    host_invoke_probe *probe = (host_invoke_probe *)user;
    if (probe == NULL) return;
    probe->closed = true;
    if (probe->duplicate_terminal_on_close)
        host_invoke_terminal(&probe->cancel_ticket, true);
}

static bool host_invoke_is_quiescent(void *user) {
    const host_invoke_probe *probe = (const host_invoke_probe *)user;
    return probe != NULL && probe->closed &&
        !probe->start_ticket.live && !probe->cancel_ticket.live;
}

static const scxml_invoke_adapter HOST_INVOKE_ADAPTER = {
    .abi_version = SCXML_ADAPTER_ABI,
    .struct_size = sizeof(scxml_invoke_adapter),
    .capabilities = SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
    .prepare_start = host_invoke_prepare_start,
    .prepare_cancel = host_invoke_prepare_cancel,
    .close = host_invoke_close,
    .is_quiescent = host_invoke_is_quiescent};

typedef enum host_w3c_route_kind {
    HOST_W3C_INVOKE_TARGET = 0,
    HOST_W3C_BIDIRECTIONAL,
    HOST_W3C_INVOKED_EVENT_IO
} host_w3c_route_kind;

typedef struct host_w3c_route_control {
    bool fail_after_child_ready;
    bool duplicate_terminal_on_close;
    bool cleanup_completed;
} host_w3c_route_control;

static bool host_run_w3c_route_fixture(
    const char *fixture_name, host_w3c_route_kind kind,
    host_w3c_route_control *control) {
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
    const bool invoked_event_io = kind == HOST_W3C_INVOKED_EVENT_IO;
    const char *child_source = kind == HOST_W3C_INVOKE_TARGET
        ? invoke_child_source : roundtrip_child_source;
    const char *alias = kind == HOST_W3C_INVOKE_TARGET
        ? "#_invokedChild" : invoked_event_io ? "#_foo" : "#_child";
    const size_t expected_deliveries = kind == HOST_W3C_INVOKE_TARGET
        ? 2u : 3u;
    const char *expected_last_event = invoked_event_io
        ? "success" : "eventReceived";
    const host_cmeta_state initial_data = {0};
    const scxml_cmeta_session_options_v1 cmeta_data = {
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(cmeta_data),
        .initial_state = &initial_data};
    host_router router;
    host_adapter_context parent_adapter;
    host_adapter_context child_adapter;
    host_invoke_probe invoke_probe = {0};
    scxml_program parent_program = {0};
    scxml_program child_program = {0};
    scxml_diagnostic diagnostic = {0};
    scxml_session parent = {0};
    scxml_session child = {0};
    cflow_executor parent_executor = {0};
    cflow_executor child_executor = {0};
    scxml_session_config parent_config;
    scxml_session_config child_config;
    cflow_statechart_instance_stats parent_stats = {0};
    cflow_statechart_instance_stats child_stats = {0};
    scxml_invoke_stats invoke_stats = {0};
    cflow_statechart_instance_status init_status;
    size_t parent_endpoint = SIZE_MAX;
    size_t child_endpoint = SIZE_MAX;
    size_t delivery;
    bool router_initialized = false;
    bool parent_executor_initialized = false;
    bool child_executor_initialized = false;
    bool parent_initialized = false;
    bool child_initialized = false;
    bool child_destroyed = false;
    bool parent_destroyed = false;
    bool child_unregistered = false;
    bool parent_unregistered = false;
    bool succeeded = false;

    if (fixture_name == NULL ||
        (kind != HOST_W3C_INVOKE_TARGET &&
         kind != HOST_W3C_BIDIRECTIONAL &&
         kind != HOST_W3C_INVOKED_EVENT_IO))
        return false;
    if (!host_router_init(&router, HOST_MESSAGE_CAPACITY)) goto cleanup;
    router_initialized = true;
    host_adapter_init(&parent_adapter, &router);
    host_adapter_init(&child_adapter, &router);
    if (invoked_event_io) {
        if (host_compile_cmeta_fixture(
                fixture_name, &parent_program, &diagnostic) != SCXML_OK ||
            host_compile_cmeta_fixture(
                "test253-child.scxml", &child_program,
                &diagnostic) != SCXML_OK)
            goto cleanup;
    } else if (host_compile_fixture(
                   fixture_name, &parent_program, &diagnostic) != SCXML_OK ||
               host_compile(
                   child_source, &child_program, &diagnostic) != SCXML_OK) {
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&parent_executor)) goto cleanup;
    parent_executor_initialized = true;
    if (!cflow_executor_serial_init(&child_executor)) goto cleanup;
    child_executor_initialized = true;
    parent_config = host_session_config(&parent_program, &parent_executor);
    child_config = host_session_config(&child_program, &child_executor);
    parent_config.event_io = &HOST_ADAPTER;
    parent_config.adapter_user = &parent_adapter;
    child_config.event_io = &HOST_ADAPTER;
    child_config.adapter_user = &child_adapter;
    if (invoked_event_io) {
        invoke_probe.duplicate_terminal_on_close =
            control != NULL && control->duplicate_terminal_on_close;
        parent_config.invocation_capacity = 1u;
        parent_config.invoke = &HOST_INVOKE_ADAPTER;
        parent_config.invoke_user = &invoke_probe;
    }
    if (!host_router_reserve(
            &router, true, &parent_adapter, &parent_endpoint) ||
        !host_router_reserve(
            &router, true, &child_adapter, &child_endpoint) ||
        !host_router_set_parent(
            &router, child_endpoint, parent_endpoint) ||
        !host_router_set_invoke_alias(
            &router, parent_endpoint, alias, child_endpoint))
        goto cleanup;
    init_status = invoked_event_io
        ? scxml_session_init_cmeta(&parent, &parent_config, &cmeta_data)
        : scxml_session_init(&parent, &parent_config);
    if (init_status != CFLOW_STATECHART_INSTANCE_OK) goto cleanup;
    parent_initialized = true;
    if (!host_router_activate(
            &router, parent_endpoint, &parent, &parent_program) ||
        !cflow_executor_wait_idle(&parent_executor))
        goto cleanup;
    if (invoked_event_io &&
        (invoke_probe.start_prepares != 1u ||
         invoke_probe.start_commits != 1u ||
         invoke_probe.start_discards != 0u || invoke_probe.token == 0u))
        goto cleanup;
    init_status = invoked_event_io
        ? scxml_session_init_cmeta(&child, &child_config, &cmeta_data)
        : scxml_session_init(&child, &child_config);
    if (init_status != CFLOW_STATECHART_INSTANCE_OK) goto cleanup;
    child_initialized = true;
    if (!host_router_activate(
            &router, child_endpoint, &child, &child_program) ||
        !cflow_executor_wait_idle(&child_executor))
        goto cleanup;
    if (control != NULL && control->fail_after_child_ready)
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
        strcmp(router.last_delivery.event, expected_last_event) != 0 ||
        !scxml_session_get_stats(&parent, &parent_stats) ||
        !scxml_session_get_stats(&child, &child_stats))
        goto cleanup;
    succeeded = parent_stats.done && !parent_stats.errored &&
        child_stats.done && !child_stats.errored;
    if (invoked_event_io) {
        if (!scxml_session_get_invoke_stats(&parent, &invoke_stats))
            goto cleanup;
        succeeded = succeeded && invoke_probe.start_prepares == 1u &&
            invoke_probe.start_commits == 1u &&
            invoke_probe.start_discards == 0u &&
            invoke_probe.cancel_prepares == 1u &&
            invoke_probe.cancel_commits == 1u &&
            invoke_probe.cancel_discards == 0u &&
            invoke_probe.terminal_violations == 0u &&
            invoke_probe.start_ticket.terminalized &&
            invoke_probe.cancel_ticket.terminalized &&
            invoke_stats.started == 1u &&
            invoke_stats.start_failed == 0u &&
            invoke_stats.cancelled == 1u &&
            invoke_stats.cancel_failed == 0u &&
            invoke_stats.active == 0u;
    }

cleanup:
    if (child_initialized) scxml_session_close(&child);
    if (parent_initialized) scxml_session_close(&parent);
    child_destroyed = !child_initialized;
    parent_destroyed = !parent_initialized;
    child_unregistered = !router_initialized || child_endpoint == SIZE_MAX;
    parent_unregistered = !router_initialized || parent_endpoint == SIZE_MAX;
    {
        bool cleanup_safe = true;
        if (child_executor_initialized &&
            !cflow_executor_wait_idle(&child_executor))
            cleanup_safe = false;
        if (parent_executor_initialized &&
            !cflow_executor_wait_idle(&parent_executor))
            cleanup_safe = false;
        if (cleanup_safe && router_initialized &&
            host_router_discard_pending(&router) == SIZE_MAX)
            cleanup_safe = false;
        if (cleanup_safe) {
            child_destroyed = !child_initialized ||
                scxml_session_destroy(&child) ==
                    CFLOW_STATECHART_INSTANCE_OK;
            parent_destroyed = !parent_initialized ||
                scxml_session_destroy(&parent) ==
                    CFLOW_STATECHART_INSTANCE_OK;
        }
        if (cleanup_safe && child_destroyed && parent_destroyed) {
            if (!child_unregistered)
                child_unregistered = host_router_unregister(
                    &router, child_endpoint);
            if (!parent_unregistered)
                parent_unregistered = host_router_unregister(
                    &router, parent_endpoint);
        }
        cleanup_safe = cleanup_safe && child_destroyed &&
            parent_destroyed && child_unregistered && parent_unregistered;
        if (!cleanup_safe) succeeded = false;
        if (cleanup_safe) {
            if (child_executor_initialized)
                cflow_executor_destroy(&child_executor);
            if (parent_executor_initialized)
                cflow_executor_destroy(&parent_executor);
            scxml_program_destroy(&child_program);
            scxml_program_destroy(&parent_program);
            if (router_initialized) host_router_destroy(&router);
        }
    }
    if (invoked_event_io && parent_initialized &&
        (!invoke_probe.closed || invoke_probe.terminal_violations != 0u ||
         !invoke_probe.start_ticket.terminalized ||
         invoke_probe.start_ticket.live ||
         !invoke_probe.cancel_ticket.terminalized ||
         invoke_probe.cancel_ticket.live))
        succeeded = false;
    if (control != NULL)
        control->cleanup_completed = child_destroyed && parent_destroyed &&
            child_unregistered && parent_unregistered;
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
        child_config.event_io = &HOST_ADAPTER;
        child_config.adapter_user = &child_adapter;
        check_equal(scxml_session_init(&parent, &parent_config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(host_router_register(
            &router, &parent, &parent_program, true, NULL,
            &parent_endpoint));
        check_true(host_router_reserve(
            &router, true, &child_adapter, &child_endpoint));
        check_true(host_router_set_parent(
            &router, child_endpoint, parent_endpoint));
        check_equal(scxml_session_init(&child, &child_config),
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

    it("preserves the full incoming Event name while routing a compiled prefix") {
        static const char receiver_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='cmeta'><state id='waiting'><transition "
            "event='alarm.system' "
            "cond='_event.name == &quot;alarm.system.disk.full&quot;' "
            "target='done'/></state><final id='done'/></scxml>";
        const scxml_cmeta_compile_options_v1 compile_options =
            scxml_cmeta_default_compile_options(&HOST_CMETA_STATE_DESC);
        const host_cmeta_state initial_data = {0};
        const scxml_cmeta_session_options_v1 cmeta_data = {
            .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
            .struct_size = sizeof(cmeta_data),
            .initial_state = &initial_data};
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
        cflow_statechart_instance_stats stats = {0};
        cflow_event_view go = {0};
        char receiver_location[HOST_LOCATION_CAPACITY];
        char sender_source[512];
        size_t required = 0u;
        size_t receiver_endpoint = SIZE_MAX;
        size_t sender_endpoint = SIZE_MAX;

        check_true(host_router_init(&router, HOST_MESSAGE_CAPACITY));
        host_adapter_init(&sender_adapter, &router);
        check_equal(scxml_compile_cmeta(
                        &receiver_program, receiver_source,
                        sizeof(receiver_source) - 1u, NULL,
                        &compile_options, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&receiver_executor));
        receiver_config = host_session_config(
            &receiver_program, &receiver_executor);
        check_equal(scxml_session_init_cmeta(
                        &receiver, &receiver_config, &cmeta_data),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(scxml_session_copy_location(
                        &receiver, receiver_location,
                        sizeof(receiver_location), &required),
                    SCXML_LOCATION_OK);
        check_true(host_router_register(
            &router, &receiver, &receiver_program, true, NULL,
            &receiver_endpoint));

        check_true(snprintf(
            sender_source, sizeof(sender_source),
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='waiting'><transition event='go' target='sent'>"
            "<send event='alarm.system.disk.full' target='%s'/>"
            "</transition></state><state id='sent'/></scxml>",
            receiver_location) > 0);
        check_equal(host_compile(
                        sender_source, &sender_program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&sender_executor));
        sender_config = host_session_config(&sender_program, &sender_executor);
        sender_config.event_io = &HOST_ADAPTER;
        sender_config.adapter_user = &sender_adapter;
        check_equal(scxml_session_init(&sender, &sender_config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(host_router_register(
            &router, &sender, &sender_program, true, &sender_adapter,
            &sender_endpoint));
        check_true(scxml_program_event(&sender_program, "go", 2u, &go));
        check_equal(scxml_session_try_send(&sender, &go), CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&sender_executor));
        check_equal(host_router_pump(&router), HOST_PUMP_DELIVERED);
        check_true(cflow_executor_wait_idle(&receiver_executor));
        check_true(scxml_session_get_stats(&receiver, &stats));
        check_true(stats.done);
        check_false(stats.errored);

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

    it("routes a parent send through #_invokeid") {
        check_true(host_run_w3c_route_fixture(
            "test192.scxml", HOST_W3C_INVOKE_TARGET, NULL));
    }

    it("exchanges SCXML Events between parent and child sessions") {
        check_true(host_run_w3c_route_fixture(
            "test347.scxml", HOST_W3C_BIDIRECTIONAL, NULL));
    }

    it("test 253 uses SCXML Event I/O in both invoke directions") {
        check_true(host_run_w3c_route_fixture(
            "test253.scxml", HOST_W3C_INVOKED_EVENT_IO, NULL));
    }

    it("cleans a committed child route after injected failure") {
        host_w3c_route_control control = {
            .fail_after_child_ready = true};

        check_false(host_run_w3c_route_fixture(
            "test253.scxml", HOST_W3C_INVOKED_EVENT_IO, &control));
        check_true(control.cleanup_completed);
    }

    it("rejects a duplicate invoke terminal during shutdown") {
        host_w3c_route_control control = {
            .duplicate_terminal_on_close = true};

        check_false(host_run_w3c_route_fixture(
            "test253.scxml", HOST_W3C_INVOKED_EVENT_IO, &control));
        check_true(control.cleanup_completed);
    }

    it("detects a duplicate invoke ticket terminal call") {
        host_invoke_probe probe = {0};
        scxml_invoke_start_request request = {
            .token = 1u,
            .id = "foo",
            .id_size = sizeof("foo") - 1u,
            .type = "http://www.w3.org/TR/scxml/",
            .type_size = sizeof("http://www.w3.org/TR/scxml/") - 1u,
            .src = "test253-child.scxml",
            .src_size = sizeof("test253-child.scxml") - 1u};
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;

        check_equal(host_invoke_prepare_start(
                        &probe, &request, &ticket, &error),
                    SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        ticket.commit(ticket.user);
        check_equal(probe.start_commits, (size_t)1u);
        check_equal(probe.terminal_violations, (size_t)1u);
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
        sender_config.event_io = &HOST_ADAPTER;
        sender_config.adapter_user = &sender_adapter;
        check_equal(scxml_session_init(&sender, &sender_config),
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
        cflow_statechart_instance_stats stats = {0};
        cflow_event_view go = {0};
        size_t endpoint = SIZE_MAX;

        check_true(host_router_init(&router, 1u));
        host_adapter_init(&adapter, &router);
        check_equal(host_compile(source, &program, &diagnostic),
                    SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        config = host_session_config(&program, &executor);
        config.event_io = &HOST_ADAPTER;
        config.adapter_user = &adapter;
        check_equal(scxml_session_init(&session, &config),
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
        scxml_send_request request = {0};
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
        owner_config.event_io = &HOST_ADAPTER;
        owner_config.adapter_user = &adapter;
        check_equal(scxml_session_init(&owner, &owner_config),
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
        request.event = "ping";
        request.event_size = 4u;
        request.type = HOST_SCXML_PROCESSOR;
        request.type_size = sizeof(HOST_SCXML_PROCESSOR) - 1u;

        request.target = "#_parent";
        request.target_size = 8u;
        check_equal(host_prepare_send(
                        &adapter, &request, &first, &error),
                    SCXML_ADAPTER_ACCEPTED);
        check_equal(host_prepare_send(
                        &adapter, &request, &second, &error),
                    SCXML_ADAPTER_FULL);
        first.discard(first.user);

        request.target = "#_child";
        request.target_size = 7u;
        check_equal(host_prepare_send(
                        &adapter, &request, &first, &error),
                    SCXML_ADAPTER_ACCEPTED);
        first.discard(first.user);
        request.type = "urn:unsupported";
        request.type_size = sizeof("urn:unsupported") - 1u;
        check_equal(host_prepare_send(
                        &adapter, &request, &second, &error),
                    SCXML_ADAPTER_ERROR_EXECUTION);
        check_not_null(error);
        request.type = NULL;
        request.type_size = 0u;
        request.target = NULL;
        request.target_size = 0u;
        check_equal(host_prepare_send(
                        &adapter, &request, &first, &error),
                    SCXML_ADAPTER_ACCEPTED);
        first.commit(first.user);
        scxml_session_close(&owner);
        check_false(host_adapter_is_quiescent(&adapter));
        check_equal(host_prepare_send(
                        &adapter, &request, &second, &error),
                    SCXML_ADAPTER_CLOSED);
        check_equal(host_router_ready_count(&router), (size_t)1u);
        check_equal(host_router_discard_pending(&router), (size_t)1u);
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
