#include <cflow/executor.h>
#include <cmeta/struct.h>
#include <scxml/scxml.h>

#include "tinytest.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CHILD_START_CAPACITY = 2u,
    CHILD_CANCEL_CAPACITY = 2u,
    CHILD_NAMED_CAPACITY = 2u,
    CHILD_NAME_CAPACITY = 64u,
    CHILD_TEXT_CAPACITY = 128u,
    CHILD_XML_CAPACITY = 4096u,
    CHILD_PATH_CAPACITY = 512u,
    CHILD_EVENT_CAPACITY = 64u,
    CHILD_MICROSTEP_LIMIT = 128u
};

static const char CHILD_SCXML_INVOKE_PROCESSOR[] =
    "http://www.w3.org/TR/scxml/";
static const char CHILD_SCXML_EVENT_PROCESSOR[] =
    "http://www.w3.org/TR/scxml/#SCXMLEventProcessor";
static const char CHILD_PARENT_TARGET[] = "#_parent";
static const char CHILD_VALUE_NAME[] = "child_value";

typedef enum child_case_kind {
    CHILD_CASE_239 = 239,
    CHILD_CASE_240,
    CHILD_CASE_241,
    CHILD_CASE_242,
    CHILD_CASE_243,
    CHILD_CASE_244,
    CHILD_CASE_245
} child_case_kind;

Struct(parent_state,
    (int, child_value),
    (int, unknown_value)
);

Struct(child_state,
    (int, child_value),
    (int, expected_value)
);

#define DEFINE_TRIVIAL_STATE_SCHEMA(prefix, type_name, stable_name, ...)       \
    static const cmeta_type_traits prefix##_traits = {                        \
        .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY};     \
    static const cmeta_type_desc prefix##_type = {                            \
        .name = #type_name,                                                    \
        .size = sizeof(type_name),                                             \
        .align = _Alignof(type_name),                                          \
        .kind = CMETA_T_OBJECT,                                                \
        .traits = &prefix##_traits};                                           \
    static const cmeta_data_field_desc prefix##_fields[] = {__VA_ARGS__};      \
    static const cmeta_data_struct_shape prefix##_shape = {                   \
        .layout = StructMeta(type_name),                                       \
        .fields = prefix##_fields,                                             \
        .field_count = sizeof(prefix##_fields) / sizeof(prefix##_fields[0])};  \
    static const cmeta_data_desc prefix##_desc = {                            \
        .struct_size = sizeof(cmeta_data_desc),                                \
        .abi_version = CMETA_DATA_DESC_ABI_VERSION,                            \
        .stable_id = stable_name,                                              \
        .display_name = #type_name,                                            \
        .kind = CMETA_DATA_STRUCT,                                             \
        .storage_type = &prefix##_type,                                        \
        .shape = &prefix##_shape}

DEFINE_TRIVIAL_STATE_SCHEMA(
    parent_state_schema, parent_state, "test.scxml.invoke.parent-state",
    {"test.scxml.invoke.parent.child-value", "child_value",
     offsetof(parent_state, child_value), &cmeta_data_int},
    {"test.scxml.invoke.parent.unknown-value", "unknown_value",
     offsetof(parent_state, unknown_value), &cmeta_data_int});

DEFINE_TRIVIAL_STATE_SCHEMA(
    child_state_schema, child_state, "test.scxml.invoke.child-state",
    {"test.scxml.invoke.child.child-value", "child_value",
     offsetof(child_state, child_value), &cmeta_data_int},
    {"test.scxml.invoke.child.expected-value", "expected_value",
     offsetof(child_state, expected_value), &cmeta_data_int});

#undef DEFINE_TRIVIAL_STATE_SCHEMA

typedef enum child_row_state {
    CHILD_ROW_FREE = 0,
    CHILD_ROW_RESERVED,
    CHILD_ROW_READY,
    CHILD_ROW_CONSUMED,
    CHILD_ROW_DISCARDED
} child_row_state;

typedef enum child_ticket_kind {
    CHILD_TICKET_INVALID = 0,
    CHILD_TICKET_START,
    CHILD_TICKET_CANCEL,
    CHILD_TICKET_SEND
} child_ticket_kind;

typedef struct child_ticket {
    child_ticket_kind kind;
    child_row_state state;
    size_t *commits;
    size_t *discards;
    size_t *violations;
} child_ticket;

typedef struct child_named_value {
    char name[CHILD_NAME_CAPACITY];
    scxml_payload_value value;
    char string[CHILD_TEXT_CAPACITY];
} child_named_value;

typedef struct child_start_row {
    child_ticket ticket;
    uint64_t token;
    char id[CHILD_NAME_CAPACITY];
    char type[CHILD_TEXT_CAPACITY];
    char src[CHILD_TEXT_CAPACITY];
    scxml_payload_kind payload_kind;
    char xml[CHILD_XML_CAPACITY + 1u];
    size_t xml_size;
    child_named_value named[CHILD_NAMED_CAPACITY];
    size_t named_count;
} child_start_row;

typedef struct child_cancel_row {
    child_ticket ticket;
    uint64_t token;
    char id[CHILD_NAME_CAPACITY];
} child_cancel_row;

typedef struct adapter_lifecycle {
    bool closed;
    size_t closes;
} adapter_lifecycle;

typedef struct child_host {
    adapter_lifecycle lifecycle;
    child_start_row starts[CHILD_START_CAPACITY];
    child_cancel_row cancels[CHILD_CANCEL_CAPACITY];
    size_t start_prepares;
    size_t start_commits;
    size_t start_discards;
    size_t cancel_prepares;
    size_t cancel_commits;
    size_t cancel_discards;
    size_t ticket_violations;
    size_t src_children;
    size_t content_children;
    size_t recognized_values;
    size_t ignored_values;
    size_t child_events;
} child_host;

typedef struct send_probe {
    adapter_lifecycle lifecycle;
    child_ticket ticket;
    char event[CHILD_EVENT_CAPACITY];
    size_t prepares;
    size_t commits;
    size_t discards;
    size_t violations;
    bool require_parent_target;
} send_probe;

typedef struct child_expectations {
    size_t starts;
    size_t src_children;
    size_t content_children;
    size_t recognized_values;
    size_t ignored_values;
    int parent_child_value;
    int parent_unknown_value;
    int child_expected_value;
} child_expectations;

static bool copy_text(char *destination, size_t capacity,
                      const char *source, size_t size) {
    if (destination == NULL || capacity == 0u || size >= capacity ||
        (source == NULL && size != 0u))
        return false;
    if (size != 0u) memcpy(destination, source, size);
    destination[size] = '\0';
    return true;
}

static bool text_equals(const char *value, size_t value_size,
                        const char *literal) {
    return value != NULL && literal != NULL &&
        value_size == strlen(literal) &&
        memcmp(value, literal, value_size) == 0;
}

static void child_ticket_commit(void *user) {
    child_ticket *ticket = (child_ticket *)user;
    if (ticket == NULL || ticket->kind == CHILD_TICKET_INVALID ||
        ticket->state != CHILD_ROW_RESERVED) {
        if (ticket != NULL && ticket->violations != NULL)
            ++*ticket->violations;
        return;
    }
    ticket->state = CHILD_ROW_READY;
    if (ticket->commits != NULL) ++*ticket->commits;
}

static void child_ticket_discard(void *user) {
    child_ticket *ticket = (child_ticket *)user;
    if (ticket == NULL || ticket->kind == CHILD_TICKET_INVALID ||
        ticket->state != CHILD_ROW_RESERVED) {
        if (ticket != NULL && ticket->violations != NULL)
            ++*ticket->violations;
        return;
    }
    ticket->state = CHILD_ROW_DISCARDED;
    if (ticket->discards != NULL) ++*ticket->discards;
}

static cflow_statechart_effect_ticket make_effect_ticket(
    child_ticket *ticket) {
    return (cflow_statechart_effect_ticket){
        child_ticket_commit, child_ticket_discard, ticket};
}

static void adapter_close(void *user) {
    adapter_lifecycle *lifecycle = (adapter_lifecycle *)user;
    if (lifecycle == NULL) return;
    lifecycle->closed = true;
    ++lifecycle->closes;
}

static bool adapter_is_quiescent(void *user) {
    const adapter_lifecycle *lifecycle =
        (const adapter_lifecycle *)user;
    return lifecycle != NULL && lifecycle->closed;
}

static bool copy_scalar_value(child_named_value *destination,
                              const scxml_content_view *source) {
    if (destination == NULL || source == NULL ||
        source->kind != SCXML_CONTENT_SCALAR)
        return false;
    destination->value = source->scalar;
    switch (source->scalar.kind) {
        case SCXML_PAYLOAD_VALUE_BOOL:
        case SCXML_PAYLOAD_VALUE_SINT:
        case SCXML_PAYLOAD_VALUE_UINT:
        case SCXML_PAYLOAD_VALUE_FLOAT:
            return true;
        case SCXML_PAYLOAD_VALUE_STRING:
            if (!copy_text(
                    destination->string, sizeof(destination->string),
                    source->scalar.data.string.data,
                    source->scalar.data.string.size))
                return false;
            destination->value.data.string.data = destination->string;
            return true;
        default:
            return false;
    }
}

static bool copy_named_payload(child_start_row *row,
                               const scxml_payload_view *payload) {
    size_t index;
    if (row == NULL || payload == NULL ||
        payload->kind != SCXML_PAYLOAD_NAMED ||
        payload->entry_count == 0u ||
        payload->entry_count > CHILD_NAMED_CAPACITY ||
        payload->entries == NULL)
        return false;
    for (index = 0u; index < payload->entry_count; ++index) {
        const scxml_payload_entry *entry = &payload->entries[index];
        child_named_value *named = &row->named[index];
        if (entry->name == NULL || entry->name_size == 0u ||
            !copy_text(named->name, sizeof(named->name),
                       entry->name, entry->name_size) ||
            !copy_scalar_value(named, &entry->value))
            return false;
    }
    row->named_count = payload->entry_count;
    return true;
}

static scxml_adapter_status child_prepare_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    static const char invalid_request[] =
        "invalid invoked child materialization request";
    child_host *host = (child_host *)user;
    child_start_row *row;
    bool source_form;

    if (host == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    *out_ticket = (cflow_statechart_effect_ticket){0};
    *out_error = NULL;
    if (host->lifecycle.closed) return SCXML_ADAPTER_CLOSED;
    if (host->start_prepares >= CHILD_START_CAPACITY)
        return SCXML_ADAPTER_FULL;
    if (request->token == 0u || request->id == NULL ||
        request->id_size == 0u || request->autoforward ||
        !text_equals(request->type, request->type_size,
                     CHILD_SCXML_INVOKE_PROCESSOR)) {
        *out_error = invalid_request;
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }

    row = &host->starts[host->start_prepares];
    memset(row, 0, sizeof(*row));
    if (!copy_text(row->id, sizeof(row->id),
                   request->id, request->id_size) ||
        !copy_text(row->type, sizeof(row->type),
                   request->type, request->type_size) ||
        !copy_text(row->src, sizeof(row->src),
                   request->src, request->src_size)) {
        *out_error = invalid_request;
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }

    source_form = request->src_size != 0u;
    if (source_form) {
        if (request->payload.kind == SCXML_PAYLOAD_NAMED) {
            if (!copy_named_payload(row, &request->payload)) {
                *out_error = invalid_request;
                return SCXML_ADAPTER_INVALID_CONTRACT;
            }
        } else if (request->payload.kind != SCXML_PAYLOAD_NONE) {
            *out_error = invalid_request;
            return SCXML_ADAPTER_INVALID_CONTRACT;
        }
    } else {
        const scxml_content_view *content = &request->payload.content;
        if (request->payload.kind != SCXML_PAYLOAD_CONTENT ||
            content->kind != SCXML_CONTENT_XML_UTF8 ||
            content->bytes == NULL || content->byte_count == 0u ||
            content->byte_count > CHILD_XML_CAPACITY) {
            *out_error = invalid_request;
            return SCXML_ADAPTER_INVALID_CONTRACT;
        }
        memcpy(row->xml, content->bytes, content->byte_count);
        row->xml[content->byte_count] = '\0';
        row->xml_size = content->byte_count;
    }

    row->token = request->token;
    row->payload_kind = request->payload.kind;
    row->ticket = (child_ticket){
        .kind = CHILD_TICKET_START,
        .state = CHILD_ROW_RESERVED,
        .commits = &host->start_commits,
        .discards = &host->start_discards,
        .violations = &host->ticket_violations};
    ++host->start_prepares;
    *out_ticket = make_effect_ticket(&row->ticket);
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status child_prepare_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    static const char invalid_cancel[] =
        "unknown invoked child cancellation";
    child_host *host = (child_host *)user;
    child_cancel_row *row;
    size_t index;
    bool matched = false;

    if (host == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    *out_ticket = (cflow_statechart_effect_ticket){0};
    *out_error = NULL;
    if (host->lifecycle.closed) return SCXML_ADAPTER_CLOSED;
    if (host->cancel_prepares >= CHILD_CANCEL_CAPACITY)
        return SCXML_ADAPTER_FULL;
    for (index = 0u; index < host->cancel_prepares; ++index) {
        if (host->cancels[index].token == request->token) {
            *out_error = invalid_cancel;
            return SCXML_ADAPTER_INVALID_CONTRACT;
        }
    }
    for (index = 0u; index < host->start_prepares; ++index) {
        const child_start_row *start = &host->starts[index];
        if ((start->ticket.state == CHILD_ROW_READY ||
             start->ticket.state == CHILD_ROW_CONSUMED) &&
            start->token == request->token &&
            text_equals(request->id, request->id_size, start->id)) {
            matched = true;
            break;
        }
    }
    if (!matched) {
        *out_error = invalid_cancel;
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    row = &host->cancels[host->cancel_prepares];
    memset(row, 0, sizeof(*row));
    if (!copy_text(row->id, sizeof(row->id),
                   request->id, request->id_size)) {
        *out_error = invalid_cancel;
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    row->token = request->token;
    row->ticket = (child_ticket){
        .kind = CHILD_TICKET_CANCEL,
        .state = CHILD_ROW_RESERVED,
        .commits = &host->cancel_commits,
        .discards = &host->cancel_discards,
        .violations = &host->ticket_violations};
    ++host->cancel_prepares;
    *out_ticket = make_effect_ticket(&row->ticket);
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status capture_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    send_probe *probe = (send_probe *)user;
    static const char invalid_send[] =
        "invalid child Event relay request";
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    *out_ticket = (cflow_statechart_effect_ticket){0};
    *out_error = NULL;
    if (probe->lifecycle.closed) return SCXML_ADAPTER_CLOSED;
    if (probe->prepares != 0u || request->event == NULL ||
        request->event_size == 0u ||
        !copy_text(probe->event, sizeof(probe->event),
                   request->event, request->event_size) ||
        request->delay_ms != 0u ||
        request->payload.kind != SCXML_PAYLOAD_NONE ||
        !text_equals(request->type, request->type_size,
                     CHILD_SCXML_EVENT_PROCESSOR) ||
        (probe->require_parent_target &&
         !text_equals(request->target, request->target_size,
                      CHILD_PARENT_TARGET))) {
        *out_error = invalid_send;
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    probe->ticket = (child_ticket){
        .kind = CHILD_TICKET_SEND,
        .state = CHILD_ROW_RESERVED,
        .commits = &probe->commits,
        .discards = &probe->discards,
        .violations = &probe->violations};
    ++probe->prepares;
    *out_ticket = make_effect_ticket(&probe->ticket);
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static bool read_fixture(const char *fixture_name,
                         char **out_source, size_t *out_size) {
    char path[CHILD_PATH_CAPACITY];
    int path_size;
    if (fixture_name == NULL || out_source == NULL || out_size == NULL)
        return false;
    *out_source = NULL;
    *out_size = 0u;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    *out_source = tt_read_file(path, out_size);
    return *out_source != NULL && *out_size != 0u;
}

static bool source_is_allowlisted(const char *source) {
    static const char *const allowed[] = {
        "test239-child.scxml",
        "test240-child.scxml",
        "test242-child.scxml",
        "test245-child.scxml"};
    size_t index;
    if (source == NULL) return false;
    for (index = 0u; index < sizeof(allowed) / sizeof(allowed[0]); ++index) {
        if (strcmp(source, allowed[index]) == 0) return true;
    }
    return false;
}

static bool child_expectations_for(child_case_kind kind,
                                   child_expectations *out) {
    if (out == NULL) return false;
    *out = (child_expectations){0};
    switch (kind) {
        case CHILD_CASE_239:
        case CHILD_CASE_242:
            out->starts = 2u;
            out->src_children = 1u;
            out->content_children = 1u;
            return true;
        case CHILD_CASE_240:
            out->starts = 2u;
            out->src_children = 2u;
            out->recognized_values = 2u;
            out->parent_child_value = 1;
            out->child_expected_value = 1;
            return true;
        case CHILD_CASE_241:
            out->starts = 2u;
            out->src_children = 2u;
            out->recognized_values = 2u;
            out->parent_child_value = 7;
            out->child_expected_value = 7;
            return true;
        case CHILD_CASE_243:
        case CHILD_CASE_244:
            out->starts = 1u;
            out->src_children = 1u;
            out->recognized_values = 1u;
            out->parent_child_value = 1;
            out->child_expected_value = 1;
            return true;
        case CHILD_CASE_245:
            out->starts = 1u;
            out->src_children = 1u;
            out->ignored_values = 1u;
            out->parent_unknown_value = 3;
            return true;
        default:
            return false;
    }
}

static bool materialize_child_initial_state(
    const child_start_row *row, int expected_value,
    child_state *out_state, size_t *out_recognized,
    size_t *out_ignored) {
    size_t index;
    if (row == NULL || out_state == NULL || out_recognized == NULL ||
        out_ignored == NULL)
        return false;
    *out_state = (child_state){.expected_value = expected_value};
    *out_recognized = 0u;
    *out_ignored = 0u;
    for (index = 0u; index < row->named_count; ++index) {
        const child_named_value *named = &row->named[index];
        if (strcmp(named->name, CHILD_VALUE_NAME) != 0) {
            ++*out_ignored;
            continue;
        }
        if (named->value.kind != SCXML_PAYLOAD_VALUE_SINT ||
            named->value.data.sint < INT_MIN ||
            named->value.data.sint > INT_MAX)
            return false;
        out_state->child_value = (int)named->value.data.sint;
        ++*out_recognized;
    }
    return true;
}

static bool run_one_child(
    child_host *host, child_start_row *row, int expected_value,
    scxml_session *parent, const scxml_program *parent_program) {
    char *owned_source = NULL;
    const char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    send_probe send = {.require_parent_target = true};
    child_state initial = {0};
    size_t recognized = 0u;
    size_t ignored = 0u;
    const scxml_cmeta_compile_options_v1 compile_options =
        scxml_cmeta_default_compile_options(&child_state_schema_desc);
    scxml_cmeta_session_options_v1 data = {0};
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND,
        .prepare_send = capture_send,
        .close = adapter_close,
        .is_quiescent = adapter_is_quiescent};
    scxml_session_config config = {0};
    cflow_event_view parent_event = {0};
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;

    if (host == NULL || row == NULL || parent == NULL ||
        parent_program == NULL || row->ticket.state != CHILD_ROW_READY)
        return false;
    row->ticket.state = CHILD_ROW_CONSUMED;
    if (row->src[0] != '\0') {
        if (!source_is_allowlisted(row->src) ||
            !read_fixture(row->src, &owned_source, &source_size))
            goto cleanup;
        source = owned_source;
        ++host->src_children;
    } else {
        if (row->payload_kind != SCXML_PAYLOAD_CONTENT ||
            row->xml_size == 0u)
            goto cleanup;
        source = row->xml;
        source_size = row->xml_size;
        ++host->content_children;
    }
    if (!materialize_child_initial_state(
            row, expected_value, &initial, &recognized, &ignored))
        goto cleanup;
    host->recognized_values += recognized;
    host->ignored_values += ignored;
    if (scxml_compile_cmeta(
            &program, source, source_size, NULL, &compile_options,
            &diagnostic) != SCXML_OK) {
        info("child source=%s compile_status=%d diagnostic=%s",
             row->src[0] != '\0' ? row->src : "<inline>",
             (int)diagnostic.status, diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) goto cleanup;
    executor_initialized = true;
    config = (scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = 1u,
        .internal_event_capacity = 1u,
        .completion_capacity = 1u,
        .microstep_limit = CHILD_MICROSTEP_LIMIT,
        .effect_capacity = 2u,
        .adapter_internal_event_capacity = 1u,
        .event_io = &event_io,
        .adapter_user = &send};
    data = (scxml_cmeta_session_options_v1){
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(data),
        .initial_state = &initial};
    if (scxml_session_init_cmeta(&session, &config, &data) !=
        CFLOW_STATECHART_INSTANCE_OK) {
        info("child source=%s init_error=%s",
             row->src[0] != '\0' ? row->src : "<inline>",
             scxml_session_error(&session));
        goto cleanup;
    }
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor) ||
        !scxml_session_get_stats(&session, &stats) || !stats.done ||
        stats.errored || send.prepares != 1u || send.commits != 1u ||
        send.discards != 0u || send.violations != 0u ||
        send.ticket.state != CHILD_ROW_READY)
        goto cleanup;
    send.ticket.state = CHILD_ROW_CONSUMED;
    if (!scxml_program_event(
            parent_program, send.event, strlen(send.event),
            &parent_event))
        goto cleanup;
    if (scxml_session_destroy(&session) != CFLOW_STATECHART_INSTANCE_OK)
        goto cleanup;
    session_initialized = false;
    if (!send.lifecycle.closed || send.lifecycle.closes != 1u ||
        scxml_session_report_invoke_event(
            parent, row->token, &parent_event) != CFLOW_MAILBOX_OK)
        goto cleanup;
    ++host->child_events;
    succeeded = true;

cleanup:
    if (session_initialized &&
        scxml_session_destroy(&session) != CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(owned_source);
    return succeeded;
}

static bool host_matches_expectations(
    const child_host *host, const child_expectations *expected,
    const scxml_invoke_stats *invoke_stats) {
    return host != NULL && expected != NULL && invoke_stats != NULL &&
        host->start_prepares == expected->starts &&
        host->start_commits == expected->starts &&
        host->start_discards == 0u &&
        host->cancel_prepares == expected->starts &&
        host->cancel_commits == expected->starts &&
        host->cancel_discards == 0u &&
        host->ticket_violations == 0u &&
        host->src_children == expected->src_children &&
        host->content_children == expected->content_children &&
        host->recognized_values == expected->recognized_values &&
        host->ignored_values == expected->ignored_values &&
        host->child_events == expected->starts &&
        invoke_stats->started == expected->starts &&
        invoke_stats->start_failed == 0u &&
        invoke_stats->cancelled == expected->starts &&
        invoke_stats->cancel_failed == 0u &&
        invoke_stats->completed == 0u &&
        invoke_stats->returned_accepted == expected->starts &&
        invoke_stats->returned_rejected == 0u &&
        invoke_stats->active == 0u;
}

static bool run_child_materialization_fixture(
    const char *fixture_name, child_case_kind kind) {
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    scxml_invoke_stats invoke_stats = {0};
    child_host host = {0};
    send_probe result = {0};
    child_expectations expected = {0};
    parent_state initial = {0};
    const scxml_cmeta_compile_options_v1 compile_options =
        scxml_cmeta_default_compile_options(&parent_state_schema_desc);
    scxml_cmeta_session_options_v1 data = {0};
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND,
        .prepare_send = capture_send,
        .close = adapter_close,
        .is_quiescent = adapter_is_quiescent};
    const scxml_invoke_adapter invoke = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(invoke),
        .capabilities = SCXML_INVOKE_CAP_START |
            SCXML_INVOKE_CAP_CANCEL | SCXML_INVOKE_CAP_PAYLOAD |
            SCXML_INVOKE_CAP_CONTENT,
        .prepare_start = child_prepare_start,
        .prepare_cancel = child_prepare_cancel,
        .close = adapter_close,
        .is_quiescent = adapter_is_quiescent};
    scxml_session_config config = {0};
    size_t index;
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;

    if (fixture_name == NULL ||
        !child_expectations_for(kind, &expected) ||
        !read_fixture(fixture_name, &source, &source_size))
        goto cleanup;
    initial.child_value = expected.parent_child_value;
    initial.unknown_value = expected.parent_unknown_value;
    if (scxml_compile_cmeta(
            &program, source, source_size, NULL, &compile_options,
            &diagnostic) != SCXML_OK) {
        info("fixture=%s compile_status=%d diagnostic=%s", fixture_name,
             (int)diagnostic.status, diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) goto cleanup;
    executor_initialized = true;
    config = (scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = 2u,
        .internal_event_capacity = 2u,
        .completion_capacity = 2u,
        .microstep_limit = CHILD_MICROSTEP_LIMIT,
        .effect_capacity = 4u,
        .adapter_internal_event_capacity = 2u,
        .invocation_capacity = CHILD_START_CAPACITY,
        .event_io = &event_io,
        .adapter_user = &result,
        .invoke = &invoke,
        .invoke_user = &host};
    data = (scxml_cmeta_session_options_v1){
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(data),
        .initial_state = &initial};
    if (scxml_session_init_cmeta(&session, &config, &data) !=
        CFLOW_STATECHART_INSTANCE_OK) {
        info("fixture=%s init_error=%s", fixture_name,
             scxml_session_error(&session));
        goto cleanup;
    }
    session_initialized = true;
    for (index = 0u; index < expected.starts; ++index) {
        if (!cflow_executor_wait_idle(&executor) ||
            host.start_prepares != index + 1u ||
            host.starts[index].ticket.state != CHILD_ROW_READY ||
            !run_one_child(
                &host, &host.starts[index],
                expected.child_expected_value, &session, &program))
            goto cleanup;
    }
    if (!cflow_executor_wait_idle(&executor) ||
        !scxml_session_get_stats(&session, &stats) ||
        !scxml_session_get_invoke_stats(&session, &invoke_stats) ||
        !stats.done || stats.errored || result.prepares != 1u ||
        result.commits != 1u || result.discards != 0u ||
        result.violations != 0u ||
        strcmp(result.event, "result.pass") != 0 ||
        !host_matches_expectations(&host, &expected, &invoke_stats)) {
        info("fixture=%s done=%d errored=%d result=%s start=%zu/%zu/%zu cancel=%zu/%zu/%zu src=%zu content=%zu recognized=%zu ignored=%zu child_events=%zu accepted=%llu cancelled=%llu active=%zu error=%s",
             fixture_name, stats.done ? 1 : 0, stats.errored ? 1 : 0,
             result.event, host.start_prepares, host.start_commits,
             host.start_discards, host.cancel_prepares,
             host.cancel_commits, host.cancel_discards,
             host.src_children, host.content_children,
             host.recognized_values, host.ignored_values,
             host.child_events,
             (unsigned long long)invoke_stats.returned_accepted,
             (unsigned long long)invoke_stats.cancelled,
             invoke_stats.active, scxml_session_error(&session));
        goto cleanup;
    }
    succeeded = true;

cleanup:
    if (session_initialized &&
        scxml_session_destroy(&session) != CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (session_initialized &&
        (!host.lifecycle.closed || host.lifecycle.closes != 1u ||
         !result.lifecycle.closed || result.lifecycle.closes != 1u))
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

spec("SCXML invoked child materialization") {
    it("executes invoked src and content documents for W3C 239") {
        check_true(run_child_materialization_fixture(
            "test239.scxml", CHILD_CASE_239));
    }

    it("injects namelist and param values for W3C 240") {
        check_true(run_child_materialization_fixture(
            "test240.scxml", CHILD_CASE_240));
    }

    it("treats namelist and param identically for W3C 241") {
        check_true(run_child_materialization_fixture(
            "test241.scxml", CHILD_CASE_241));
    }

    it("treats src and content documents identically for W3C 242") {
        check_true(run_child_materialization_fixture(
            "test242.scxml", CHILD_CASE_242));
    }

    it("initializes matching child data from param for W3C 243") {
        check_true(run_child_materialization_fixture(
            "test243.scxml", CHILD_CASE_243));
    }

    it("initializes matching child data from namelist for W3C 244") {
        check_true(run_child_materialization_fixture(
            "test244.scxml", CHILD_CASE_244));
    }

    it("does not add unknown named data to the child model for W3C 245") {
        check_true(run_child_materialization_fixture(
            "test245.scxml", CHILD_CASE_245));
    }
}
