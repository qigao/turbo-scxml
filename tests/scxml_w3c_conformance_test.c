#include <cflow/executor.h>
#include <scxml/scxml.h>
#include <cflow/statechart_instance.h>
#include <turbo_cmeta_data.h>
#include <turbostl/typed.h>
#include <tlog.h>

#include "tinytest.h"

#include <stddef.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <turbo/thread.h>

#define W3C_FIXTURE_PATH_CAPACITY 512u
#define W3C_MANIFEST_ROW_CAPACITY 256u
#define W3C_MANIFEST_BYTE_CAPACITY 131072u
#define W3C_MANIFEST_COLUMN_COUNT 9u
#define W3C_UPSTREAM_PREFIX "https://www.w3.org/Voice/2013/scxml-irp/"

enum {
    W3C_EXTERNAL_EVENT_CAPACITY = 2,
    W3C_MACROSTEP_EXTERNAL_EVENT_CAPACITY = 3,
    W3C_INTERNAL_EVENT_CAPACITY = 8,
    /* Test 417 completes two regions, their parallel, its parent, and root. */
    W3C_COMPLETION_CAPACITY = 5,
    W3C_MICROSTEP_LIMIT = 32,
    W3C_UPSTREAM_TEST_DOCUMENT_COUNT = 202,
    W3C_UPSTREAM_MANDATORY_DOCUMENT_COUNT = 168,
    W3C_UPSTREAM_OPTIONAL_DOCUMENT_COUNT = 34,
    W3C_PASS_DOCUMENT_COUNT = 158,
    W3C_UNSUPPORTED_DOCUMENT_COUNT = 10,
    W3C_LOOPBACK_CAPACITY = 2,
    W3C_DELAYED_MESSAGE_CAPACITY = 2,
    W3C_NAMED_PAYLOAD_CAPACITY = 2,
    W3C_EVENT_TEXT_CAPACITY = 32,
    W3C_MACROSTEP_INVOKE_CAPACITY = 3,
    W3C_MATERIALIZATION_INVOKE_CAPACITY = 2,
    W3C_INVOKE_COMPLETION_EXTERNAL_CAPACITY = 3,
    W3C_FINALIZE_INVOKE_CAPACITY = 2,
    W3C_AUTOFORWARD_EVENT_CAPACITY = 2,
    W3C_CANCELLATION_LOG_CAPACITY = 2,
    W3C_CANCELLATION_LOG_MESSAGE_CAPACITY = 32
};

static const char W3C_SCXML_EVENT_PROCESSOR[] =
    "http://www.w3.org/TR/scxml/#SCXMLEventProcessor";
static const char W3C_SCXML_INVOKE_PROCESSOR[] =
    "http://www.w3.org/TR/scxml/";
static const char W3C_AUTOFORWARD_INPUT_EVENT[] = "event230";
static const char W3C_AUTOFORWARD_SEND_ID[] = "send-230";
static const char W3C_AUTOFORWARD_ORIGIN[] = "scxml://parent/session";
static const char W3C_AUTOFORWARD_INVOKE_ID[] = "source-invoke-230";
static const char W3C_AUTOFORWARD_DATA[] = "payload-230";

typedef enum w3c_invoke_materialization_kind {
    W3C_INVOKE_TYPE_EXPR = 0,
    W3C_INVOKE_SRC_EXPR,
    W3C_INVOKE_CANONICAL_TYPE,
    W3C_INVOKE_UNIQUE_IDS,
    W3C_INVOKE_NAMED_PAYLOAD,
    W3C_INVOKE_CONTENT,
    W3C_INVOKE_ARGUMENT_ERROR
} w3c_invoke_materialization_kind;

typedef enum w3c_invoke_completion_case {
    W3C_INVOKE_RETURN_PROVENANCE = 0,
    W3C_INVOKE_MULTIPLE_RETURN,
    W3C_INVOKE_EXACT_COMPLETION,
    W3C_INVOKE_TERMINAL_COMPLETION,
    W3C_INVOKE_CHILD_FINAL_COMPLETION
} w3c_invoke_completion_case;

typedef enum w3c_invoke_cancellation_case {
    W3C_INVOKE_CANCEL_STOPS_CHILD = 0,
    W3C_INVOKE_CANCEL_REJECTS_RETURN,
    W3C_INVOKE_CANCEL_RUNS_CHILD_ONEXIT
} w3c_invoke_cancellation_case;

typedef enum w3c_invoke_finalize_case {
    W3C_INVOKE_FINALIZE_BEFORE_SELECTION = 0,
    W3C_INVOKE_FINALIZE_MATCHING_ONLY
} w3c_invoke_finalize_case;

typedef enum w3c_manifest_column {
    W3C_MANIFEST_ID = 0,
    W3C_MANIFEST_FIXTURE,
    W3C_MANIFEST_APPLICABILITY,
    W3C_MANIFEST_STATUS,
    W3C_MANIFEST_FEATURE,
    W3C_MANIFEST_UPSTREAM,
    W3C_MANIFEST_EXPECTED,
    W3C_MANIFEST_TRANSFORMATION,
    W3C_MANIFEST_RATIONALE
} w3c_manifest_column;

typedef struct w3c_run_result {
    cflow_machine_state_id current_state;
    cflow_machine_state_id pass_state;
    cflow_machine_state_id fail_state;
    bool done;
    bool errored;
} w3c_run_result;

typedef struct w3c_adapter_probe {
    size_t prepare_send_calls;
    scxml_adapter_status send_status;
    const char *expected_target;
    const char *expected_type;
} w3c_adapter_probe;

typedef struct w3c_content_probe {
    size_t prepare_send_calls;
    size_t commits;
    size_t discards;
    scxml_content_kind kind;
    char bytes[32];
} w3c_content_probe;

typedef struct w3c_result_probe {
    size_t prepare_send_calls;
    size_t commits;
    size_t discards;
    char event[32];
} w3c_result_probe;

typedef struct w3c_delayed_message {
    char event[W3C_EVENT_TEXT_CAPACITY];
    char id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    uint64_t delay_ms;
} w3c_delayed_message;

typedef struct w3c_delayed_probe {
    w3c_delayed_message messages[W3C_DELAYED_MESSAGE_CAPACITY];
    size_t send_count;
    size_t cancel_count;
    size_t commits;
    size_t discards;
    char cancel_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    bool payload_seen;
    int64_t payload_sint;
} w3c_delayed_probe;

typedef struct w3c_cancel_isolation_probe {
    w3c_delayed_probe delayed;
    w3c_result_probe result;
    size_t cancel_prepare_calls;
} w3c_cancel_isolation_probe;

typedef struct w3c_named_payload_probe {
    const char *expected_event;
    const char *expected_names[W3C_NAMED_PAYLOAD_CAPACITY];
    int64_t expected_values[W3C_NAMED_PAYLOAD_CAPACITY];
    size_t expected_count;
    size_t sends;
    size_t commits;
    size_t discards;
} w3c_named_payload_probe;

typedef struct w3c_termination_probe {
    size_t sends;
    size_t commits;
    size_t discards;
    size_t closes;
    size_t cancellations;
    size_t deliveries;
    bool pending;
} w3c_termination_probe;

typedef enum w3c_delayed_fixture_kind {
    W3C_DELAY_ORDER = 0,
    W3C_DYNAMIC_DELAY,
    W3C_LITERAL_CANCEL,
    W3C_DYNAMIC_CANCEL
} w3c_delayed_fixture_kind;

typedef scxml_adapter_status (*w3c_send_extension_fn)(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error);

typedef struct w3c_cmeta_probe {
    w3c_result_probe result;
    bool routable_loopback;
    bool require_scxml_type;
    bool require_route_target;
    bool hold_loopback_delivery;
    size_t loopback_limit;
    size_t loopback_ready_count;
    size_t loopback_delivered_count;
    size_t loopback_prepare_calls;
    size_t loopback_commits;
    size_t loopback_discards;
    scxml_adapter_status send_rejection;
    size_t rejected_sends;
    w3c_send_extension_fn send_extension;
    void *send_extension_user;
    char route_origin[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char initial_route_target[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char loopback_events[W3C_LOOPBACK_CAPACITY][32];
    char loopback_send_ids[W3C_LOOPBACK_CAPACITY]
                          [SCXML_EVENT_METADATA_CAPACITY + 1u];
} w3c_cmeta_probe;

typedef struct w3c_cmeta_fixture_options {
    const char *external_event;
    const char *following_external_event;
    const char *third_external_event;
    size_t external_event_capacity;
    bool hold_executor_during_external_admission;
    bool require_three_external_events_drained;
    size_t loopback_count;
    bool routable_loopback;
    bool require_scxml_type;
    bool require_route_target;
    bool hold_loopback_delivery;
    scxml_adapter_status send_rejection;
    w3c_send_extension_fn send_extension;
    void *send_extension_user;
    size_t max_iterations;
    const scxml_cmeta_environment_override *environment_overrides;
    size_t environment_override_count;
} w3c_cmeta_fixture_options;

typedef struct w3c_executor_blocker {
    atomic_bool entered;
    atomic_bool release;
} w3c_executor_blocker;

typedef struct w3c_event_probe {
    cflow_event_id expected_internal;
    size_t selected_internal;
} w3c_event_probe;

Struct(w3c_cmeta_state,
    (tstr, invoke_id),
    (tstr, send_id),
    (int, sequence)
);

static bool w3c_cmeta_state_copy(void *destination, const void *source) {
    if (destination == NULL || source == NULL) return false;
    memcpy(destination, source, sizeof(w3c_cmeta_state));
    return true;
}

static void w3c_cmeta_state_move(void *destination, void *source) {
    if (destination == NULL || source == NULL) return;
    memcpy(destination, source, sizeof(w3c_cmeta_state));
    memset(source, 0, sizeof(w3c_cmeta_state));
}

static void w3c_cmeta_state_destroy(void *value) {
    (void)value;
}

static const cmeta_type_identity w3c_cmeta_state_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.w3c.state");
static const cmeta_type_traits w3c_cmeta_state_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = w3c_cmeta_state_copy,
    .move_construct = w3c_cmeta_state_move,
    .destroy = w3c_cmeta_state_destroy};
static const cmeta_type_desc w3c_cmeta_state_type = {
    .name = "w3c_cmeta_state",
    .size = sizeof(w3c_cmeta_state),
    .align = _Alignof(w3c_cmeta_state),
    .kind = CMETA_T_OBJECT,
    .traits = &w3c_cmeta_state_traits,
    .identity = &w3c_cmeta_state_identity};
static const cmeta_data_buffer_shape w3c_owned_string_shape = {
    .ownership = CMETA_DATA_BUFFER_OWNED};
static const cmeta_data_desc w3c_owned_string_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.w3c.owned-string",
    .display_name = "W3C owned string",
    .kind = CMETA_DATA_STRING,
    .storage_type = &turbo_tstr_cmeta_type,
    .shape = &w3c_owned_string_shape,
    .buffer_ops = &turbo_tstr_cmeta_buffer_ops};
static const cmeta_data_field_desc w3c_cmeta_state_fields[] = {
    {"test.scxml.w3c.state.invoke-id", "invoke_id",
     offsetof(w3c_cmeta_state, invoke_id), &w3c_owned_string_desc},
    {"test.scxml.w3c.state.send-id", "send_id",
     offsetof(w3c_cmeta_state, send_id), &w3c_owned_string_desc},
    {"test.scxml.w3c.state.sequence", "sequence",
     offsetof(w3c_cmeta_state, sequence), &cmeta_data_int}};
static const cmeta_data_struct_shape w3c_cmeta_state_shape = {
    .layout = StructMeta(w3c_cmeta_state),
    .fields = w3c_cmeta_state_fields,
    .field_count = sizeof(w3c_cmeta_state_fields) /
                   sizeof(w3c_cmeta_state_fields[0])};
static const cmeta_data_desc w3c_cmeta_state_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.w3c.state.schema",
    .display_name = "W3C CMeta state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &w3c_cmeta_state_type,
    .shape = &w3c_cmeta_state_shape};

Struct(w3c_copy_payload,
    (int, first),
    (int, second)
);

static bool w3c_copy_payload_copy(void *destination, const void *source) {
    if (destination == NULL || source == NULL) return false;
    memcpy(destination, source, sizeof(w3c_copy_payload));
    return true;
}

static void w3c_copy_payload_move(void *destination, void *source) {
    if (destination == NULL || source == NULL) return;
    memcpy(destination, source, sizeof(w3c_copy_payload));
    memset(source, 0, sizeof(w3c_copy_payload));
}

static void w3c_copy_payload_destroy(void *value) {
    (void)value;
}

static const cmeta_type_traits w3c_copy_payload_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = w3c_copy_payload_copy,
    .move_construct = w3c_copy_payload_move,
    .destroy = w3c_copy_payload_destroy};
static const cmeta_type_desc w3c_copy_payload_type = {
    .name = "w3c_copy_payload",
    .size = sizeof(w3c_copy_payload),
    .align = _Alignof(w3c_copy_payload),
    .kind = CMETA_T_OBJECT,
    .traits = &w3c_copy_payload_traits};
static const cmeta_data_field_desc w3c_copy_payload_fields[] = {
    {"test.scxml.w3c.copy.first", "first",
     offsetof(w3c_copy_payload, first), &cmeta_data_int},
    {"test.scxml.w3c.copy.second", "second",
     offsetof(w3c_copy_payload, second), &cmeta_data_int}};
static const cmeta_data_struct_shape w3c_copy_payload_shape = {
    .layout = StructMeta(w3c_copy_payload),
    .fields = w3c_copy_payload_fields,
    .field_count = sizeof(w3c_copy_payload_fields) /
                   sizeof(w3c_copy_payload_fields[0])};
static const cmeta_data_desc w3c_copy_payload_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.w3c.copy.payload",
    .display_name = "W3C copied payload",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &w3c_copy_payload_type,
    .shape = &w3c_copy_payload_shape};

Struct(w3c_copy_state,
    (w3c_copy_payload, payload)
);

static bool w3c_copy_state_copy(void *destination, const void *source) {
    if (destination == NULL || source == NULL) return false;
    memcpy(destination, source, sizeof(w3c_copy_state));
    return true;
}

static void w3c_copy_state_move(void *destination, void *source) {
    if (destination == NULL || source == NULL) return;
    memcpy(destination, source, sizeof(w3c_copy_state));
    memset(source, 0, sizeof(w3c_copy_state));
}

static void w3c_copy_state_destroy(void *value) {
    (void)value;
}

static const cmeta_type_traits w3c_copy_state_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = w3c_copy_state_copy,
    .move_construct = w3c_copy_state_move,
    .destroy = w3c_copy_state_destroy};
static const cmeta_type_desc w3c_copy_state_type = {
    .name = "w3c_copy_state",
    .size = sizeof(w3c_copy_state),
    .align = _Alignof(w3c_copy_state),
    .kind = CMETA_T_OBJECT,
    .traits = &w3c_copy_state_traits};
static const cmeta_data_field_desc w3c_copy_state_fields[] = {
    {"test.scxml.w3c.copy.state.payload", "payload",
     offsetof(w3c_copy_state, payload), &w3c_copy_payload_desc}};
static const cmeta_data_struct_shape w3c_copy_state_shape = {
    .layout = StructMeta(w3c_copy_state),
    .fields = w3c_copy_state_fields,
    .field_count = sizeof(w3c_copy_state_fields) /
                   sizeof(w3c_copy_state_fields[0])};
static const cmeta_data_desc w3c_copy_state_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.w3c.copy.state",
    .display_name = "W3C copy sender state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &w3c_copy_state_type,
    .shape = &w3c_copy_state_shape};

typedef struct w3c_copy_probe {
    w3c_copy_payload message;
    size_t sends;
    size_t commits;
    size_t discards;
} w3c_copy_probe;

Struct(w3c_foreach_state,
    (TYPE(Vec, int), values),
    (int, item),
    (size_t, index),
    (int, previous),
    (int, stage),
    (int, total),
    (int, valid)
);

static _Atomic(w3c_foreach_state *) w3c_foreach_staged_state;

static bool w3c_foreach_state_copy(void *destination_, const void *source_) {
    w3c_foreach_state *destination = (w3c_foreach_state *)destination_;
    const w3c_foreach_state *source =
        (const w3c_foreach_state *)source_;
    size_t index;
    if (destination == NULL || source == NULL) return false;
    memset(destination, 0, sizeof(*destination));
    destination->values = VecOf(int);
    if (vec_init(&destination->values, source->values.element_limit) != STL_OK)
        return false;
    for (index = 0u; index < vec_size(&source->values); ++index) {
        const int *value = (const int *)vec_at_const(&source->values, index);
        if (value == NULL || vec_push(&destination->values, value) != STL_OK) {
            vec_destroy(&destination->values);
            memset(destination, 0, sizeof(*destination));
            return false;
        }
    }
    destination->item = source->item;
    destination->index = source->index;
    destination->previous = source->previous;
    destination->stage = source->stage;
    destination->total = source->total;
    destination->valid = source->valid;
    atomic_store(&w3c_foreach_staged_state, destination);
    return true;
}

static void w3c_foreach_state_move(void *destination_, void *source_) {
    w3c_foreach_state *destination = (w3c_foreach_state *)destination_;
    w3c_foreach_state *source = (w3c_foreach_state *)source_;
    if (destination == NULL || source == NULL) return;
    *destination = *source;
    memset(source, 0, sizeof(*source));
}

static void w3c_foreach_state_destroy(void *value_) {
    w3c_foreach_state *value = (w3c_foreach_state *)value_;
    if (value != NULL && value->values.initialized)
        vec_destroy(&value->values);
}

static const cmeta_type_traits w3c_foreach_state_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = w3c_foreach_state_copy,
    .move_construct = w3c_foreach_state_move,
    .destroy = w3c_foreach_state_destroy};
static const cmeta_type_desc w3c_foreach_state_type = {
    .name = "w3c_foreach_state",
    .size = sizeof(w3c_foreach_state),
    .align = _Alignof(w3c_foreach_state),
    .kind = CMETA_T_OBJECT,
    .traits = &w3c_foreach_state_traits};
static const cmeta_data_field_desc w3c_foreach_state_fields[] = {
    {"test.scxml.w3c.foreach.values", "values",
     offsetof(w3c_foreach_state, values), &cmeta_data_sequence},
    {"test.scxml.w3c.foreach.item", "item",
     offsetof(w3c_foreach_state, item), &cmeta_data_int},
    {"test.scxml.w3c.foreach.index", "index",
     offsetof(w3c_foreach_state, index), &cmeta_data_size},
    {"test.scxml.w3c.foreach.previous", "previous",
     offsetof(w3c_foreach_state, previous), &cmeta_data_int},
    {"test.scxml.w3c.foreach.stage", "stage",
     offsetof(w3c_foreach_state, stage), &cmeta_data_int},
    {"test.scxml.w3c.foreach.total", "total",
     offsetof(w3c_foreach_state, total), &cmeta_data_int},
    {"test.scxml.w3c.foreach.valid", "valid",
     offsetof(w3c_foreach_state, valid), &cmeta_data_int}};
static const cmeta_data_struct_shape w3c_foreach_state_shape = {
    .layout = StructMeta(w3c_foreach_state),
    .fields = w3c_foreach_state_fields,
    .field_count = sizeof(w3c_foreach_state_fields) /
                   sizeof(w3c_foreach_state_fields[0])};
static const cmeta_data_desc w3c_foreach_state_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.w3c.foreach.state",
    .display_name = "W3C CMeta foreach state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &w3c_foreach_state_type,
    .shape = &w3c_foreach_state_shape};

typedef struct w3c_invoke_probe {
    size_t starts;
    size_t commits;
    size_t discards;
    uint64_t token;
    char id[SCXML_EVENT_METADATA_CAPACITY + 1u];
} w3c_invoke_probe;

typedef struct w3c_invoke_autoforward_probe
    w3c_invoke_autoforward_probe;

typedef struct w3c_invoke_autoforward_ticket {
    w3c_invoke_autoforward_probe *probe;
    bool response_required;
    bool fields_equal;
    char name[W3C_EVENT_TEXT_CAPACITY];
    char type[W3C_EVENT_TEXT_CAPACITY];
    char send_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char origin[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char origin_type[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char invoke_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char data[SCXML_EVENT_METADATA_CAPACITY + 1u];
} w3c_invoke_autoforward_ticket;

struct w3c_invoke_autoforward_probe {
    bool require_all_fields;
    bool deliverable;
    uint64_t token;
    char id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    size_t start_prepares;
    size_t start_commits;
    size_t start_discards;
    size_t forward_prepares;
    size_t forward_commits;
    size_t forward_discards;
    size_t cancel_prepares;
    size_t cancel_commits;
    size_t cancel_discards;
    w3c_invoke_autoforward_ticket
        forwards[W3C_AUTOFORWARD_EVENT_CAPACITY];
};

typedef struct w3c_invoke_completion_probe {
    size_t start_prepares;
    size_t start_commits;
    size_t start_discards;
    size_t cancel_prepares;
    size_t cancel_commits;
    size_t cancel_discards;
    uint64_t token;
    char id[SCXML_EVENT_METADATA_CAPACITY + 1u];
} w3c_invoke_completion_probe;

typedef struct w3c_invoke_cancellation_probe {
    scxml_session *child;
    size_t start_prepares;
    size_t start_commits;
    size_t start_discards;
    size_t cancel_prepares;
    size_t cancel_commits;
    size_t cancel_discards;
    uint64_t token;
    char id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    bool child_cancelled;
} w3c_invoke_cancellation_probe;

typedef struct w3c_invoke_cancellation_log {
    size_t count;
    char messages[W3C_CANCELLATION_LOG_CAPACITY]
                 [W3C_CANCELLATION_LOG_MESSAGE_CAPACITY];
} w3c_invoke_cancellation_log;

typedef struct w3c_invoke_finalize_start {
    uint64_t token;
    char id[SCXML_EVENT_METADATA_CAPACITY + 1u];
} w3c_invoke_finalize_start;

typedef struct w3c_invoke_finalize_probe {
    w3c_invoke_finalize_start starts[W3C_FINALIZE_INVOKE_CAPACITY];
    const char *expected_ids[W3C_FINALIZE_INVOKE_CAPACITY];
    size_t expected_count;
    size_t start_count;
    size_t start_commits;
    size_t start_discards;
    size_t cancel_prepares;
    size_t cancel_commits;
    size_t cancel_discards;
} w3c_invoke_finalize_probe;

typedef struct w3c_materialized_invoke_start {
    uint64_t token;
    char id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char type[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char src[SCXML_EVENT_METADATA_CAPACITY + 1u];
    scxml_payload_kind payload_kind;
    size_t payload_entry_count;
    char payload_name[W3C_EVENT_TEXT_CAPACITY];
    scxml_payload_value payload_value;
} w3c_materialized_invoke_start;

typedef struct w3c_invoke_materialization_probe {
    w3c_materialized_invoke_start
        starts[W3C_MATERIALIZATION_INVOKE_CAPACITY];
    size_t start_count;
    size_t start_commits;
    size_t start_discards;
    size_t cancel_commits;
    size_t cancel_discards;
} w3c_invoke_materialization_probe;

typedef struct w3c_macrostep_invoke_start {
    uint64_t token;
    char id[SCXML_EVENT_METADATA_CAPACITY + 1u];
} w3c_macrostep_invoke_start;

typedef struct w3c_macrostep_invoke_probe {
    w3c_macrostep_invoke_start starts[W3C_MACROSTEP_INVOKE_CAPACITY];
    size_t start_count;
    size_t start_commits;
    size_t start_discards;
    size_t cancel_commits;
    size_t cancel_discards;
} w3c_macrostep_invoke_probe;

typedef struct w3c_manifest_row {
    char *columns[W3C_MANIFEST_COLUMN_COUNT];
} w3c_manifest_row;

typedef struct w3c_manifest_stats {
    size_t rows;
    size_t mandatory;
    size_t optional;
    size_t passed;
    size_t unsupported;
    size_t not_applicable;
} w3c_manifest_stats;

static bool split_manifest_row(char *line, w3c_manifest_row *out_row) {
    size_t column = 0u;
    char *cursor;
    if (line == NULL || out_row == NULL || line[0] == '\0') return false;
    memset(out_row, 0, sizeof(*out_row));
    out_row->columns[column++] = line;
    for (cursor = line; *cursor != '\0'; ++cursor) {
        if (*cursor != '\t') continue;
        if (column >= W3C_MANIFEST_COLUMN_COUNT) return false;
        *cursor = '\0';
        out_row->columns[column++] = cursor + 1;
    }
    if (column != W3C_MANIFEST_COLUMN_COUNT) return false;
    for (column = 0u; column < W3C_MANIFEST_COLUMN_COUNT; ++column) {
        if (out_row->columns[column][0] == '\0') return false;
    }
    return true;
}

static bool manifest_value_is(const char *value, const char *expected) {
    return value != NULL && expected != NULL && strcmp(value, expected) == 0;
}

static bool manifest_upstream_is_documented(const char *id,
                                            const char *upstream) {
    char expected[W3C_FIXTURE_PATH_CAPACITY];
    size_t numeric_length = 0u;
    int written;
    if (id == NULL || upstream == NULL) return false;
    while (id[numeric_length] >= '0' && id[numeric_length] <= '9') {
        ++numeric_length;
    }
    if (numeric_length == 0u) return false;
    written = snprintf(expected, sizeof(expected), "%s%.*s/test%s.",
                       W3C_UPSTREAM_PREFIX, (int)numeric_length, id, id);
    if (written < 0 || (size_t)written >= sizeof(expected) ||
        strncmp(upstream, expected, (size_t)written) != 0)
        return false;
    upstream += (size_t)written;
    return strcmp(upstream, "txml") == 0 || strcmp(upstream, "txt") == 0;
}

static bool validate_w3c_manifest_source(char *source,
                                         const char *fixture_directory,
                                         bool verify_fixtures,
                                         const char *documentation,
                                         w3c_manifest_stats *out_stats) {
    static const char header[] =
        "id\tfixture\tapplicability\tstatus\tfeature\tupstream\texpected\t"
        "transformation\trationale";
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char expected_fixture[64];
    char *cursor;
    size_t row_count = 0u;
    w3c_manifest_row rows[W3C_MANIFEST_ROW_CAPACITY];
    w3c_manifest_stats stats = {0};
    int written;
    if (source == NULL || fixture_directory == NULL || out_stats == NULL)
        return false;
    memset(out_stats, 0, sizeof(*out_stats));
    cursor = source;
    {
        char *newline = strchr(cursor, '\n');
        if (newline == NULL) goto cleanup;
        *newline = '\0';
        if (newline != cursor && newline[-1] == '\r') newline[-1] = '\0';
        if (strcmp(cursor, header) != 0) goto cleanup;
        cursor = newline + 1;
    }
    while (*cursor != '\0') {
        char *newline = strchr(cursor, '\n');
        char *line_end = newline != NULL ? newline : cursor + strlen(cursor);
        w3c_manifest_row *row;
        size_t previous;
        char *fixture_source = NULL;
        size_t fixture_size = 0u;
        if (line_end != cursor && line_end[-1] == '\r') line_end[-1] = '\0';
        if (newline != NULL) *newline = '\0';
        if (cursor[0] == '\0' || row_count == W3C_MANIFEST_ROW_CAPACITY)
            goto cleanup;
        row = &rows[row_count];
        if (!split_manifest_row(cursor, row)) goto cleanup;
        if ((!manifest_value_is(
                  row->columns[W3C_MANIFEST_APPLICABILITY], "MANDATORY") &&
             !manifest_value_is(
                 row->columns[W3C_MANIFEST_APPLICABILITY], "OPTIONAL")) ||
            (!manifest_value_is(row->columns[W3C_MANIFEST_STATUS], "PASS") &&
             !manifest_value_is(row->columns[W3C_MANIFEST_STATUS],
                                "UNSUPPORTED") &&
             !manifest_value_is(row->columns[W3C_MANIFEST_STATUS], "N/A")) ||
            !manifest_upstream_is_documented(
                row->columns[W3C_MANIFEST_ID],
                row->columns[W3C_MANIFEST_UPSTREAM]))
            goto cleanup;
        for (previous = 0u; previous < row_count; ++previous) {
            if (strcmp(rows[previous].columns[W3C_MANIFEST_ID],
                       row->columns[W3C_MANIFEST_ID]) == 0 ||
                strcmp(rows[previous].columns[W3C_MANIFEST_FIXTURE],
                       row->columns[W3C_MANIFEST_FIXTURE]) == 0 ||
                strcmp(rows[previous].columns[W3C_MANIFEST_UPSTREAM],
                       row->columns[W3C_MANIFEST_UPSTREAM]) == 0)
                goto cleanup;
        }
        written = snprintf(expected_fixture, sizeof(expected_fixture),
                           "test%s.scxml",
                           row->columns[W3C_MANIFEST_ID]);
        if (written < 0 || (size_t)written >= sizeof(expected_fixture) ||
            strcmp(expected_fixture,
                   row->columns[W3C_MANIFEST_FIXTURE]) != 0)
            goto cleanup;
        if (manifest_value_is(row->columns[W3C_MANIFEST_APPLICABILITY],
                              "MANDATORY")) {
            ++stats.mandatory;
        } else {
            ++stats.optional;
        }
        if (manifest_value_is(row->columns[W3C_MANIFEST_STATUS], "PASS")) {
            if ((!manifest_value_is(row->columns[W3C_MANIFEST_EXPECTED],
                                    "TERMINAL_PASS") &&
                 !manifest_value_is(row->columns[W3C_MANIFEST_EXPECTED],
                                    "COMPILE_REJECT")) ||
                manifest_value_is(row->columns[W3C_MANIFEST_TRANSFORMATION],
                                  "NONE"))
                goto cleanup;
            if (documentation != NULL &&
                (strstr(documentation,
                        row->columns[W3C_MANIFEST_FIXTURE]) == NULL ||
                 strstr(documentation,
                        row->columns[W3C_MANIFEST_UPSTREAM]) == NULL))
                goto cleanup;
            ++stats.passed;
        } else if (manifest_value_is(
                       row->columns[W3C_MANIFEST_STATUS], "UNSUPPORTED")) {
            if (!manifest_value_is(
                    row->columns[W3C_MANIFEST_APPLICABILITY], "MANDATORY") ||
                !manifest_value_is(row->columns[W3C_MANIFEST_EXPECTED],
                                   "NOT_RUN") ||
                !manifest_value_is(row->columns[W3C_MANIFEST_TRANSFORMATION],
                                   "NONE"))
                goto cleanup;
            ++stats.unsupported;
        } else {
            if (!manifest_value_is(
                    row->columns[W3C_MANIFEST_APPLICABILITY], "OPTIONAL") ||
                !manifest_value_is(row->columns[W3C_MANIFEST_EXPECTED],
                                   "NOT_RUN") ||
                !manifest_value_is(row->columns[W3C_MANIFEST_TRANSFORMATION],
                                   "NONE"))
                goto cleanup;
            ++stats.not_applicable;
        }
        if (verify_fixtures &&
            manifest_value_is(row->columns[W3C_MANIFEST_STATUS], "PASS")) {
            written = snprintf(path, sizeof(path), "%s/%s",
                               fixture_directory,
                               row->columns[W3C_MANIFEST_FIXTURE]);
            if (written < 0 || (size_t)written >= sizeof(path)) goto cleanup;
            fixture_source = tt_read_file(path, &fixture_size);
            if (fixture_source == NULL || fixture_size == 0u) {
                free(fixture_source);
                goto cleanup;
            }
            free(fixture_source);
        }
        ++row_count;
        if (newline == NULL) break;
        cursor = newline + 1;
    }
    stats.rows = row_count;
    if (row_count == 0u) goto cleanup;
    *out_stats = stats;
    return true;

cleanup:
    memset(out_stats, 0, sizeof(*out_stats));
    return false;
}

static bool validate_w3c_manifest(w3c_manifest_stats *out_stats) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    char *documentation = NULL;
    size_t source_size = 0u;
    size_t documentation_size = 0u;
    bool valid = false;
    int written;
    if (out_stats == NULL) return false;
    memset(out_stats, 0, sizeof(*out_stats));
    written = snprintf(path, sizeof(path), "%s/manifest.tsv",
                       SCXML_W3C_FIXTURE_DIR);
    if (written < 0 || (size_t)written >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL || source_size == 0u ||
        source_size > W3C_MANIFEST_BYTE_CAPACITY)
        goto cleanup;
    written = snprintf(path, sizeof(path), "%s/README.md",
                       SCXML_W3C_FIXTURE_DIR);
    if (written < 0 || (size_t)written >= sizeof(path)) goto cleanup;
    documentation = tt_read_file(path, &documentation_size);
    if (documentation == NULL || documentation_size == 0u)
        goto cleanup;
    valid = validate_w3c_manifest_source(
        source, SCXML_W3C_FIXTURE_DIR, true, documentation, out_stats);
    if (!valid || out_stats->rows != W3C_UPSTREAM_TEST_DOCUMENT_COUNT ||
        out_stats->mandatory != W3C_UPSTREAM_MANDATORY_DOCUMENT_COUNT ||
        out_stats->optional != W3C_UPSTREAM_OPTIONAL_DOCUMENT_COUNT) {
        valid = false;
        memset(out_stats, 0, sizeof(*out_stats));
    }

cleanup:
    free(documentation);
    free(source);
    return valid;
}

static scxml_adapter_status w3c_reject_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    w3c_adapter_probe *probe = (w3c_adapter_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL ||
        (probe->send_status != SCXML_ADAPTER_ERROR_EXECUTION &&
         probe->send_status != SCXML_ADAPTER_ERROR_COMMUNICATION) ||
        (probe->expected_target != NULL &&
         (request->target == NULL ||
          request->target_size != strlen(probe->expected_target) ||
          memcmp(request->target, probe->expected_target,
                 request->target_size) != 0)) ||
        (probe->expected_type != NULL &&
         (request->type == NULL ||
          request->type_size != strlen(probe->expected_type) ||
          memcmp(request->type, probe->expected_type,
                 request->type_size) != 0))) {
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    ++probe->prepare_send_calls;
    *out_ticket = (cflow_statechart_effect_ticket){0};
    *out_error = probe->send_status == SCXML_ADAPTER_ERROR_EXECUTION
        ? "injected W3C send execution error"
        : "injected W3C send communication error";
    return probe->send_status;
}

static void w3c_adapter_close(void *user) {
    (void)user;
}

static bool w3c_adapter_is_quiescent(void *user) {
    return user != NULL;
}

static bool w3c_copy_request_text(char *destination, size_t capacity,
                                  const char *source, size_t size) {
    if (destination == NULL || capacity == 0u || size >= capacity ||
        (source == NULL && size != 0u))
        return false;
    if (size != 0u) memcpy(destination, source, size);
    destination[size] = '\0';
    return true;
}

static void w3c_delayed_commit(void *user) {
    w3c_delayed_probe *probe = (w3c_delayed_probe *)user;
    if (probe != NULL) ++probe->commits;
}

static void w3c_delayed_discard(void *user) {
    w3c_delayed_probe *probe = (w3c_delayed_probe *)user;
    if (probe != NULL) ++probe->discards;
}

static scxml_adapter_status w3c_capture_delayed_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_delayed_probe *probe = (w3c_delayed_probe *)user;
    w3c_delayed_message *message;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->event == NULL ||
        request->event_size == 0u ||
        probe->send_count >= W3C_DELAYED_MESSAGE_CAPACITY)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    message = &probe->messages[probe->send_count];
    if (!w3c_copy_request_text(
            message->event, sizeof(message->event), request->event,
            request->event_size) ||
        !w3c_copy_request_text(
            message->id, sizeof(message->id), request->id,
            request->id_size) ||
        request->type == NULL ||
        request->type_size != sizeof(W3C_SCXML_EVENT_PROCESSOR) - 1u ||
        memcmp(request->type, W3C_SCXML_EVENT_PROCESSOR,
               request->type_size) != 0)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    message->delay_ms = request->delay_ms;
    ++probe->send_count;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_delayed_commit, w3c_delayed_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status w3c_capture_delayed_payload_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_delayed_probe *probe = (w3c_delayed_probe *)user;
    scxml_adapter_status status;
    if (probe == NULL || request == NULL ||
        request->payload.kind != SCXML_PAYLOAD_CONTENT ||
        request->payload.content.kind != SCXML_CONTENT_SCALAR ||
        request->payload.content.scalar.kind != SCXML_PAYLOAD_VALUE_SINT)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    status = w3c_capture_delayed_send(
        user, request, out_ticket, out_error);
    if (status == SCXML_ADAPTER_ACCEPTED) {
        probe->payload_seen = true;
        probe->payload_sint = request->payload.content.scalar.data.sint;
    }
    return status;
}

static scxml_adapter_status w3c_capture_delayed_cancel(
    void *user, const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_delayed_probe *probe = (w3c_delayed_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || probe->cancel_count != 0u ||
        request->send_id == NULL || request->send_id_size == 0u ||
        !w3c_copy_request_text(
            probe->cancel_id, sizeof(probe->cancel_id), request->send_id,
            request->send_id_size))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->cancel_count;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_delayed_commit, w3c_delayed_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void w3c_named_payload_commit(void *user) {
    w3c_named_payload_probe *probe = (w3c_named_payload_probe *)user;
    if (probe != NULL) ++probe->commits;
}

static void w3c_named_payload_discard(void *user) {
    w3c_named_payload_probe *probe = (w3c_named_payload_probe *)user;
    if (probe != NULL) ++probe->discards;
}

static scxml_adapter_status w3c_capture_named_payload_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_named_payload_probe *probe = (w3c_named_payload_probe *)user;
    size_t index;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || probe->expected_event == NULL ||
        probe->expected_count == 0u ||
        probe->expected_count > W3C_NAMED_PAYLOAD_CAPACITY ||
        probe->sends != 0u || request->event == NULL ||
        request->event_size != strlen(probe->expected_event) ||
        memcmp(request->event, probe->expected_event,
               request->event_size) != 0 ||
        request->type == NULL ||
        request->type_size != sizeof(W3C_SCXML_EVENT_PROCESSOR) - 1u ||
        memcmp(request->type, W3C_SCXML_EVENT_PROCESSOR,
               request->type_size) != 0 ||
        request->payload.kind != SCXML_PAYLOAD_NAMED ||
        request->payload.entry_count != probe->expected_count)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    for (index = 0u; index < probe->expected_count; ++index) {
        const scxml_payload_entry *entry = &request->payload.entries[index];
        if (probe->expected_names[index] == NULL || entry->name == NULL ||
            entry->name_size != strlen(probe->expected_names[index]) ||
            memcmp(entry->name, probe->expected_names[index],
                   entry->name_size) != 0 ||
            entry->value.kind != SCXML_CONTENT_SCALAR ||
            entry->value.scalar.kind != SCXML_PAYLOAD_VALUE_SINT ||
            entry->value.scalar.data.sint != probe->expected_values[index])
            return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    ++probe->sends;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_named_payload_commit, w3c_named_payload_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void w3c_termination_commit(void *user) {
    w3c_termination_probe *probe = (w3c_termination_probe *)user;
    if (probe == NULL) return;
    ++probe->commits;
    probe->pending = true;
}

static void w3c_termination_discard(void *user) {
    w3c_termination_probe *probe = (w3c_termination_probe *)user;
    if (probe == NULL) return;
    ++probe->discards;
    probe->pending = false;
}

static scxml_adapter_status w3c_capture_terminating_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    static const char event[] = "childToParent";
    static const char target[] = "#_parent";
    static const char id[] = "child.delayed";
    w3c_termination_probe *probe = (w3c_termination_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || probe->sends != 0u || request->event == NULL ||
        request->event_size != sizeof(event) - 1u ||
        memcmp(request->event, event, sizeof(event) - 1u) != 0 ||
        request->target == NULL || request->target_size != sizeof(target) - 1u ||
        memcmp(request->target, target, sizeof(target) - 1u) != 0 ||
        request->type == NULL ||
        request->type_size != sizeof(W3C_SCXML_EVENT_PROCESSOR) - 1u ||
        memcmp(request->type, W3C_SCXML_EVENT_PROCESSOR,
               request->type_size) != 0 || request->id == NULL ||
        request->id_size != sizeof(id) - 1u ||
        memcmp(request->id, id, sizeof(id) - 1u) != 0 ||
        request->delay_ms != UINT64_C(500))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->sends;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_termination_commit, w3c_termination_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void w3c_termination_close(void *user) {
    w3c_termination_probe *probe = (w3c_termination_probe *)user;
    if (probe == NULL) return;
    ++probe->closes;
    if (probe->pending) {
        probe->pending = false;
        ++probe->cancellations;
    }
}

static bool w3c_termination_is_quiescent(void *user) {
    const w3c_termination_probe *probe =
        (const w3c_termination_probe *)user;
    return probe != NULL && probe->closes == 1u && !probe->pending;
}

static bool w3c_termination_try_deliver(w3c_termination_probe *probe) {
    if (probe == NULL || !probe->pending) return false;
    probe->pending = false;
    ++probe->deliveries;
    return true;
}

static void w3c_content_commit(void *user) {
    w3c_content_probe *probe = (w3c_content_probe *)user;
    if (probe != NULL) ++probe->commits;
}

static void w3c_content_discard(void *user) {
    w3c_content_probe *probe = (w3c_content_probe *)user;
    if (probe != NULL) ++probe->discards;
}

static scxml_adapter_status w3c_capture_content_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_content_probe *probe = (w3c_content_probe *)user;
    const scxml_content_view *content;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL ||
        request->payload.kind != SCXML_PAYLOAD_CONTENT)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    content = &request->payload.content;
    if ((content->kind != SCXML_CONTENT_TEXT_UTF8 &&
         content->kind != SCXML_CONTENT_XML_UTF8) ||
        content->byte_count >= sizeof(probe->bytes) ||
        (content->byte_count != 0u && content->bytes == NULL))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    if (content->byte_count != 0u)
        memcpy(probe->bytes, content->bytes, content->byte_count);
    probe->bytes[content->byte_count] = '\0';
    probe->kind = content->kind;
    ++probe->prepare_send_calls;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_content_commit, w3c_content_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void w3c_copy_commit(void *user) {
    w3c_copy_probe *probe = (w3c_copy_probe *)user;
    if (probe != NULL) ++probe->commits;
}

static void w3c_copy_discard(void *user) {
    w3c_copy_probe *probe = (w3c_copy_probe *)user;
    if (probe != NULL) ++probe->discards;
}

static scxml_adapter_status w3c_capture_copy_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    static const char event[] = "event1";
    w3c_copy_probe *probe = (w3c_copy_probe *)user;
    const scxml_content_view *content;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || probe->sends != 0u ||
        request->event == NULL ||
        request->event_size != sizeof(event) - 1u ||
        memcmp(request->event, event, sizeof(event) - 1u) != 0 ||
        request->payload.kind != SCXML_PAYLOAD_CONTENT)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    content = &request->payload.content;
    if (content->kind != SCXML_CONTENT_CMETA ||
        content->schema != &w3c_copy_payload_desc ||
        content->object == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    probe->message = *(const w3c_copy_payload *)content->object;
    ++probe->sends;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_copy_commit, w3c_copy_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void w3c_result_commit(void *user) {
    w3c_result_probe *probe = (w3c_result_probe *)user;
    if (probe != NULL) ++probe->commits;
}

static void w3c_result_discard(void *user) {
    w3c_result_probe *probe = (w3c_result_probe *)user;
    if (probe != NULL) ++probe->discards;
}

static scxml_adapter_status w3c_capture_result_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_result_probe *probe = (w3c_result_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->event == NULL ||
        request->event_size == 0u ||
        request->event_size >= sizeof(probe->event) ||
        probe->prepare_send_calls != 0u)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    memcpy(probe->event, request->event, request->event_size);
    probe->event[request->event_size] = '\0';
    ++probe->prepare_send_calls;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_result_commit, w3c_result_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static bool w3c_send_is_result(const scxml_send_request *request) {
    static const char pass[] = "result.pass";
    static const char fail[] = "result.fail";
    if (request == NULL || request->event == NULL) return false;
    return (request->event_size == sizeof(pass) - 1u &&
            memcmp(request->event, pass, sizeof(pass) - 1u) == 0) ||
           (request->event_size == sizeof(fail) - 1u &&
            memcmp(request->event, fail, sizeof(fail) - 1u) == 0);
}

static scxml_adapter_status w3c_capture_isolated_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_cancel_isolation_probe *probe =
        (w3c_cancel_isolation_probe *)user;
    if (probe == NULL) return SCXML_ADAPTER_INVALID_CONTRACT;
    return w3c_send_is_result(request)
        ? w3c_capture_result_send(
              &probe->result, request, out_ticket, out_error)
        : w3c_capture_delayed_send(
              &probe->delayed, request, out_ticket, out_error);
}

static scxml_adapter_status w3c_capture_isolated_cancel(
    void *user, const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_cancel_isolation_probe *probe =
        (w3c_cancel_isolation_probe *)user;
    if (probe == NULL) return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->cancel_prepare_calls;
    return w3c_capture_delayed_cancel(
        &probe->delayed, request, out_ticket, out_error);
}

static void w3c_loopback_commit(void *user) {
    w3c_cmeta_probe *probe = (w3c_cmeta_probe *)user;
    if (probe == NULL) return;
    ++probe->loopback_commits;
    ++probe->loopback_ready_count;
}

static void w3c_loopback_discard(void *user) {
    w3c_cmeta_probe *probe = (w3c_cmeta_probe *)user;
    if (probe == NULL) return;
    ++probe->loopback_discards;
}

static scxml_adapter_status w3c_capture_cmeta_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_cmeta_probe *probe = (w3c_cmeta_probe *)user;
    size_t index;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    if (w3c_send_is_result(request))
        return w3c_capture_result_send(
            &probe->result, request, out_ticket, out_error);
    if (probe->send_extension != NULL)
        return probe->send_extension(
            probe->send_extension_user, request, out_ticket, out_error);
    if (probe->send_rejection != SCXML_ADAPTER_ACCEPTED &&
        probe->rejected_sends == 0u) {
        ++probe->rejected_sends;
        *out_ticket = (cflow_statechart_effect_ticket){0};
        *out_error = "configured W3C send rejection";
        return probe->send_rejection;
    }
    index = probe->loopback_prepare_calls;
    if (request->event == NULL || request->event_size == 0u ||
        index >= probe->loopback_limit || index >= W3C_LOOPBACK_CAPACITY ||
        request->event_size >= sizeof(probe->loopback_events[index]) ||
        request->id_size > SCXML_EVENT_METADATA_CAPACITY ||
        (request->id_size != 0u && request->id == NULL))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    if (probe->require_scxml_type &&
        (request->type == NULL ||
         request->type_size != sizeof(W3C_SCXML_EVENT_PROCESSOR) - 1u ||
         memcmp(request->type, W3C_SCXML_EVENT_PROCESSOR,
                request->type_size) != 0))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    if (probe->require_route_target) {
        if (request->target == NULL || request->target_size == 0u ||
            request->target_size > SCXML_EVENT_METADATA_CAPACITY)
            return SCXML_ADAPTER_INVALID_CONTRACT;
        memcpy(probe->initial_route_target, request->target,
               request->target_size);
        probe->initial_route_target[request->target_size] = '\0';
    }
    if (probe->routable_loopback && index == 1u &&
        (request->target == NULL ||
         request->target_size != strlen(probe->route_origin) ||
         memcmp(request->target, probe->route_origin,
                request->target_size) != 0 ||
         request->type == NULL ||
         request->type_size != sizeof(W3C_SCXML_EVENT_PROCESSOR) - 1u ||
         memcmp(request->type, W3C_SCXML_EVENT_PROCESSOR,
                request->type_size) != 0))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    memcpy(probe->loopback_events[index], request->event,
           request->event_size);
    probe->loopback_events[index][request->event_size] = '\0';
    if (request->id_size != 0u)
        memcpy(probe->loopback_send_ids[index], request->id,
               request->id_size);
    probe->loopback_send_ids[index][request->id_size] = '\0';
    ++probe->loopback_prepare_calls;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_loopback_commit, w3c_loopback_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void w3c_block_executor(void *user) {
    w3c_executor_blocker *blocker = (w3c_executor_blocker *)user;
    atomic_store(&blocker->entered, true);
    while (!atomic_load(&blocker->release)) turbo_thread_yield();
}

static bool w3c_admit_external_event(
    scxml_session *session, const scxml_program *program,
    const char *event_name) {
    cflow_event_view event = {0};
    scxml_event_metadata metadata = {
        .abi_version = SCXML_EVENT_METADATA_ABI,
        .struct_size = sizeof(metadata)};
    const size_t event_size = event_name != NULL ? strlen(event_name) : 0u;
    return event_size != 0u &&
        scxml_program_event(program, event_name, event_size, &event) &&
        scxml_session_try_send_with_metadata(session, &event, &metadata) ==
            CFLOW_MAILBOX_OK;
}

static void w3c_invoke_commit(void *user) {
    w3c_invoke_probe *probe = (w3c_invoke_probe *)user;
    if (probe != NULL) ++probe->commits;
}

static void w3c_invoke_discard(void *user) {
    w3c_invoke_probe *probe = (w3c_invoke_probe *)user;
    if (probe != NULL) ++probe->discards;
}

static scxml_adapter_status w3c_capture_invoke_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_invoke_probe *probe = (w3c_invoke_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->id == NULL || request->id_size == 0u ||
        request->id_size >= sizeof(probe->id))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    memcpy(probe->id, request->id, request->id_size);
    probe->id[request->id_size] = '\0';
    probe->token = request->token;
    ++probe->starts;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_invoke_commit, w3c_invoke_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status w3c_accept_invoke_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    if (user == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_invoke_commit, w3c_invoke_discard, user};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void w3c_invoke_completion_start_commit(void *user) {
    w3c_invoke_completion_probe *probe =
        (w3c_invoke_completion_probe *)user;
    if (probe != NULL) ++probe->start_commits;
}

static void w3c_invoke_completion_start_discard(void *user) {
    w3c_invoke_completion_probe *probe =
        (w3c_invoke_completion_probe *)user;
    if (probe != NULL) ++probe->start_discards;
}

static void w3c_invoke_completion_cancel_commit(void *user) {
    w3c_invoke_completion_probe *probe =
        (w3c_invoke_completion_probe *)user;
    if (probe != NULL) ++probe->cancel_commits;
}

static void w3c_invoke_completion_cancel_discard(void *user) {
    w3c_invoke_completion_probe *probe =
        (w3c_invoke_completion_probe *)user;
    if (probe != NULL) ++probe->cancel_discards;
}

static scxml_adapter_status w3c_capture_invoke_completion_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_invoke_completion_probe *probe =
        (w3c_invoke_completion_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || probe->start_prepares != 0u ||
        request->token == 0u || request->id == NULL ||
        request->id_size == 0u || request->id_size >= sizeof(probe->id))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    memcpy(probe->id, request->id, request->id_size);
    probe->id[request->id_size] = '\0';
    probe->token = request->token;
    ++probe->start_prepares;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_invoke_completion_start_commit,
        w3c_invoke_completion_start_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status w3c_capture_invoke_completion_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_invoke_completion_probe *probe =
        (w3c_invoke_completion_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->token != probe->token ||
        request->id == NULL || request->id_size != strlen(probe->id) ||
        memcmp(request->id, probe->id, request->id_size) != 0)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->cancel_prepares;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_invoke_completion_cancel_commit,
        w3c_invoke_completion_cancel_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void w3c_capture_invoke_cancellation_log(
    const turbo_log_entry_t *entry, void *user_data) {
    w3c_invoke_cancellation_log *capture =
        (w3c_invoke_cancellation_log *)user_data;
    size_t message_size;
    if (entry == NULL || capture == NULL || entry->component == NULL ||
        strcmp(entry->component, "cflow.scxml") != 0)
        return;
    if (capture->count >= W3C_CANCELLATION_LOG_CAPACITY) {
        if (capture->count != SIZE_MAX) ++capture->count;
        return;
    }
    message_size = entry->message_len;
    if (message_size >= W3C_CANCELLATION_LOG_MESSAGE_CAPACITY)
        message_size = W3C_CANCELLATION_LOG_MESSAGE_CAPACITY - 1u;
    if (message_size != 0u)
        memcpy(capture->messages[capture->count], entry->message,
               message_size);
    capture->messages[capture->count][message_size] = '\0';
    ++capture->count;
}

static void w3c_invoke_cancellation_start_commit(void *user) {
    w3c_invoke_cancellation_probe *probe =
        (w3c_invoke_cancellation_probe *)user;
    if (probe != NULL) ++probe->start_commits;
}

static void w3c_invoke_cancellation_start_discard(void *user) {
    w3c_invoke_cancellation_probe *probe =
        (w3c_invoke_cancellation_probe *)user;
    if (probe != NULL) ++probe->start_discards;
}

static void w3c_invoke_cancellation_cancel_commit(void *user) {
    w3c_invoke_cancellation_probe *probe =
        (w3c_invoke_cancellation_probe *)user;
    if (probe == NULL || probe->child == NULL) return;
    ++probe->cancel_commits;
    scxml_session_cancel(probe->child);
    probe->child_cancelled = true;
}

static void w3c_invoke_cancellation_cancel_discard(void *user) {
    w3c_invoke_cancellation_probe *probe =
        (w3c_invoke_cancellation_probe *)user;
    if (probe != NULL) ++probe->cancel_discards;
}

static scxml_adapter_status w3c_capture_invoke_cancellation_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_invoke_cancellation_probe *probe =
        (w3c_invoke_cancellation_probe *)user;
    if (probe == NULL || probe->child == NULL || request == NULL ||
        out_ticket == NULL || out_error == NULL ||
        probe->start_prepares != 0u || request->token == 0u ||
        request->id == NULL || request->id_size == 0u ||
        request->id_size >= sizeof(probe->id))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    memcpy(probe->id, request->id, request->id_size);
    probe->id[request->id_size] = '\0';
    probe->token = request->token;
    ++probe->start_prepares;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_invoke_cancellation_start_commit,
        w3c_invoke_cancellation_start_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status w3c_capture_invoke_cancellation_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_invoke_cancellation_probe *probe =
        (w3c_invoke_cancellation_probe *)user;
    if (probe == NULL || probe->child == NULL || request == NULL ||
        out_ticket == NULL || out_error == NULL ||
        probe->cancel_prepares != 0u || request->token != probe->token ||
        request->id == NULL || request->id_size != strlen(probe->id) ||
        memcmp(request->id, probe->id, request->id_size) != 0)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->cancel_prepares;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_invoke_cancellation_cancel_commit,
        w3c_invoke_cancellation_cancel_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static bool w3c_invoke_text_is(
    const char *text, size_t text_size, const char *expected) {
    const size_t expected_size = expected != NULL ? strlen(expected) : 0u;
    return text != NULL && expected != NULL && text_size == expected_size &&
        memcmp(text, expected, expected_size) == 0;
}

static void w3c_autoforward_start_commit(void *user) {
    w3c_invoke_autoforward_probe *probe =
        (w3c_invoke_autoforward_probe *)user;
    if (probe != NULL) ++probe->start_commits;
}

static void w3c_autoforward_start_discard(void *user) {
    w3c_invoke_autoforward_probe *probe =
        (w3c_invoke_autoforward_probe *)user;
    if (probe != NULL) ++probe->start_discards;
}

static void w3c_autoforward_forward_commit(void *user) {
    w3c_invoke_autoforward_ticket *ticket =
        (w3c_invoke_autoforward_ticket *)user;
    if (ticket == NULL || ticket->probe == NULL) return;
    ++ticket->probe->forward_commits;
    if (ticket->response_required && ticket->fields_equal)
        ticket->probe->deliverable = true;
}

static void w3c_autoforward_forward_discard(void *user) {
    w3c_invoke_autoforward_ticket *ticket =
        (w3c_invoke_autoforward_ticket *)user;
    if (ticket != NULL && ticket->probe != NULL)
        ++ticket->probe->forward_discards;
}

static void w3c_autoforward_cancel_commit(void *user) {
    w3c_invoke_autoforward_probe *probe =
        (w3c_invoke_autoforward_probe *)user;
    if (probe != NULL) ++probe->cancel_commits;
}

static void w3c_autoforward_cancel_discard(void *user) {
    w3c_invoke_autoforward_probe *probe =
        (w3c_invoke_autoforward_probe *)user;
    if (probe != NULL) ++probe->cancel_discards;
}

static scxml_adapter_status w3c_capture_autoforward_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_invoke_autoforward_probe *probe =
        (w3c_invoke_autoforward_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || probe->start_prepares != 0u ||
        request->token == 0u || !request->autoforward ||
        !w3c_invoke_text_is(
            request->type, request->type_size,
            W3C_SCXML_INVOKE_PROCESSOR) ||
        request->id == NULL || request->id_size == 0u ||
        request->id_size >= sizeof(probe->id))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    probe->token = request->token;
    memcpy(probe->id, request->id, request->id_size);
    probe->id[request->id_size] = '\0';
    ++probe->start_prepares;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_autoforward_start_commit,
        w3c_autoforward_start_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static bool w3c_copy_autoforward_envelope(
    w3c_invoke_autoforward_ticket *ticket,
    const scxml_event_envelope_view *envelope) {
    return ticket != NULL && envelope != NULL &&
        envelope->abi_version == SCXML_EVENT_ENVELOPE_ABI &&
        envelope->struct_size == sizeof(scxml_event_envelope_view) &&
        w3c_copy_request_text(
            ticket->name, sizeof(ticket->name),
            envelope->name, envelope->name_size) &&
        w3c_copy_request_text(
            ticket->type, sizeof(ticket->type),
            envelope->type, envelope->type_size) &&
        w3c_copy_request_text(
            ticket->send_id, sizeof(ticket->send_id),
            envelope->send_id, envelope->send_id_size) &&
        w3c_copy_request_text(
            ticket->origin, sizeof(ticket->origin),
            envelope->origin, envelope->origin_size) &&
        w3c_copy_request_text(
            ticket->origin_type, sizeof(ticket->origin_type),
            envelope->origin_type, envelope->origin_type_size) &&
        w3c_copy_request_text(
            ticket->invoke_id, sizeof(ticket->invoke_id),
            envelope->invoke_id, envelope->invoke_id_size) &&
        envelope->data.kind == SCXML_CONTENT_TEXT_UTF8 &&
        w3c_copy_request_text(
            ticket->data, sizeof(ticket->data),
            envelope->data.bytes, envelope->data.byte_count);
}

static bool w3c_autoforward_fields_equal(
    const w3c_invoke_autoforward_probe *probe,
    const w3c_invoke_autoforward_ticket *ticket) {
    if (probe == NULL || ticket == NULL ||
        strcmp(ticket->type, "external") != 0)
        return false;
    if (!probe->require_all_fields)
        return strcmp(ticket->name, "childToParent") == 0;
    return strcmp(ticket->name, W3C_AUTOFORWARD_INPUT_EVENT) == 0 &&
        strcmp(ticket->send_id, W3C_AUTOFORWARD_SEND_ID) == 0 &&
        strcmp(ticket->origin, W3C_AUTOFORWARD_ORIGIN) == 0 &&
        strcmp(ticket->origin_type, W3C_SCXML_EVENT_PROCESSOR) == 0 &&
        strcmp(ticket->invoke_id, W3C_AUTOFORWARD_INVOKE_ID) == 0 &&
        strcmp(ticket->data, W3C_AUTOFORWARD_DATA) == 0;
}

static scxml_adapter_status w3c_capture_autoforward_event(
    void *user, const scxml_invoke_forward_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_invoke_autoforward_probe *probe =
        (w3c_invoke_autoforward_probe *)user;
    w3c_invoke_autoforward_ticket *ticket;
    const char *expected_input;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->token != probe->token ||
        !w3c_invoke_text_is(request->id, request->id_size, probe->id) ||
        request->event == NULL ||
        probe->forward_prepares >= W3C_AUTOFORWARD_EVENT_CAPACITY)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ticket = &probe->forwards[probe->forward_prepares];
    ticket->probe = probe;
    if (!w3c_copy_autoforward_envelope(ticket, request->envelope)) {
        *out_error = "invalid W3C autoforward Event envelope";
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    expected_input = probe->require_all_fields
        ? W3C_AUTOFORWARD_INPUT_EVENT : "childToParent";
    ticket->response_required =
        probe->forward_prepares == 0u &&
        strcmp(ticket->name, expected_input) == 0;
    ticket->fields_equal = w3c_autoforward_fields_equal(probe, ticket);
    ++probe->forward_prepares;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_autoforward_forward_commit,
        w3c_autoforward_forward_discard, ticket};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status w3c_capture_autoforward_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_invoke_autoforward_probe *probe =
        (w3c_invoke_autoforward_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->token != probe->token ||
        !w3c_invoke_text_is(request->id, request->id_size, probe->id))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->cancel_prepares;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_autoforward_cancel_commit,
        w3c_autoforward_cancel_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void w3c_invoke_finalize_start_commit(void *user) {
    w3c_invoke_finalize_probe *probe =
        (w3c_invoke_finalize_probe *)user;
    if (probe != NULL) ++probe->start_commits;
}

static void w3c_invoke_finalize_start_discard(void *user) {
    w3c_invoke_finalize_probe *probe =
        (w3c_invoke_finalize_probe *)user;
    if (probe != NULL) ++probe->start_discards;
}

static void w3c_invoke_finalize_cancel_commit(void *user) {
    w3c_invoke_finalize_probe *probe =
        (w3c_invoke_finalize_probe *)user;
    if (probe != NULL) ++probe->cancel_commits;
}

static void w3c_invoke_finalize_cancel_discard(void *user) {
    w3c_invoke_finalize_probe *probe =
        (w3c_invoke_finalize_probe *)user;
    if (probe != NULL) ++probe->cancel_discards;
}

static scxml_adapter_status w3c_capture_invoke_finalize_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_invoke_finalize_probe *probe =
        (w3c_invoke_finalize_probe *)user;
    w3c_invoke_finalize_start *start;
    const char *expected_id;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->token == 0u ||
        probe->start_count >= probe->expected_count ||
        probe->start_count >= W3C_FINALIZE_INVOKE_CAPACITY ||
        !w3c_invoke_text_is(
            request->type, request->type_size, W3C_SCXML_INVOKE_PROCESSOR))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    expected_id = probe->expected_ids[probe->start_count];
    if (!w3c_invoke_text_is(request->id, request->id_size, expected_id) ||
        request->id_size >= sizeof(probe->starts[0].id)) {
        *out_error = "unexpected W3C invoke finalize descriptor";
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    start = &probe->starts[probe->start_count++];
    start->token = request->token;
    memcpy(start->id, request->id, request->id_size);
    start->id[request->id_size] = '\0';
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_invoke_finalize_start_commit,
        w3c_invoke_finalize_start_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status w3c_capture_invoke_finalize_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_invoke_finalize_probe *probe =
        (w3c_invoke_finalize_probe *)user;
    size_t index;
    bool found = false;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->token == 0u)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    for (index = 0u; index < probe->start_count; ++index) {
        if (probe->starts[index].token == request->token &&
            w3c_invoke_text_is(
                request->id, request->id_size, probe->starts[index].id)) {
            found = true;
            break;
        }
    }
    if (!found) {
        *out_error = "unexpected W3C invoke finalize cancellation";
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    ++probe->cancel_prepares;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_invoke_finalize_cancel_commit,
        w3c_invoke_finalize_cancel_discard, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void w3c_materialized_invoke_start_commit(void *user) {
    w3c_invoke_materialization_probe *probe =
        (w3c_invoke_materialization_probe *)user;
    if (probe != NULL) ++probe->start_commits;
}

static void w3c_materialized_invoke_start_discard(void *user) {
    w3c_invoke_materialization_probe *probe =
        (w3c_invoke_materialization_probe *)user;
    if (probe != NULL) ++probe->start_discards;
}

static void w3c_materialized_invoke_cancel_commit(void *user) {
    w3c_invoke_materialization_probe *probe =
        (w3c_invoke_materialization_probe *)user;
    if (probe != NULL) ++probe->cancel_commits;
}

static void w3c_materialized_invoke_cancel_discard(void *user) {
    w3c_invoke_materialization_probe *probe =
        (w3c_invoke_materialization_probe *)user;
    if (probe != NULL) ++probe->cancel_discards;
}

static scxml_adapter_status w3c_capture_materialized_invoke_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_invoke_materialization_probe *probe =
        (w3c_invoke_materialization_probe *)user;
    w3c_materialized_invoke_start *start;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->token == 0u ||
        probe->start_count >= W3C_MATERIALIZATION_INVOKE_CAPACITY)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    start = &probe->starts[probe->start_count];
    if (!w3c_copy_request_text(
            start->id, sizeof(start->id), request->id,
            request->id_size) ||
        !w3c_copy_request_text(
            start->type, sizeof(start->type), request->type,
            request->type_size) ||
        !w3c_copy_request_text(
            start->src, sizeof(start->src), request->src,
            request->src_size))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    start->token = request->token;
    start->payload_kind = request->payload.kind;
    start->payload_entry_count = request->payload.entry_count;
    if (request->payload.kind == SCXML_PAYLOAD_CONTENT) {
        if (request->payload.content.kind != SCXML_CONTENT_SCALAR)
            return SCXML_ADAPTER_INVALID_CONTRACT;
        start->payload_value = request->payload.content.scalar;
    } else if (request->payload.kind == SCXML_PAYLOAD_NAMED) {
        const scxml_payload_entry *entry;
        if (request->payload.entry_count != 1u ||
            request->payload.entries == NULL)
            return SCXML_ADAPTER_INVALID_CONTRACT;
        entry = &request->payload.entries[0];
        if (!w3c_copy_request_text(
                start->payload_name, sizeof(start->payload_name),
                entry->name, entry->name_size))
            return SCXML_ADAPTER_INVALID_CONTRACT;
        if (entry->value.kind != SCXML_CONTENT_SCALAR)
            return SCXML_ADAPTER_INVALID_CONTRACT;
        start->payload_value = entry->value.scalar;
    } else if (request->payload.kind != SCXML_PAYLOAD_NONE) {
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    ++probe->start_count;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_materialized_invoke_start_commit,
        w3c_materialized_invoke_start_discard,
        probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status w3c_accept_materialized_invoke_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_invoke_materialization_probe *probe =
        (w3c_invoke_materialization_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->token == 0u || request->id == NULL ||
        request->id_size == 0u)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_materialized_invoke_cancel_commit,
        w3c_materialized_invoke_cancel_discard,
        probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static bool w3c_materialized_invoke_start_is(
    const w3c_materialized_invoke_start *start,
    const char *type, const char *src, scxml_payload_kind payload_kind) {
    return start != NULL && start->token != 0u && start->id[0] != '\0' &&
        strcmp(start->type, type) == 0 && strcmp(start->src, src) == 0 &&
        start->payload_kind == payload_kind;
}

static bool w3c_materialized_invokes_match(
    const w3c_invoke_materialization_probe *probe,
    w3c_invoke_materialization_kind kind) {
    const w3c_materialized_invoke_start *first;
    if (probe == NULL) return false;
    if (kind == W3C_INVOKE_ARGUMENT_ERROR)
        return probe->start_count == 0u && probe->start_commits == 0u &&
            probe->start_discards == 0u;
    if (probe->start_count == 0u ||
        probe->start_commits != probe->start_count ||
        probe->start_discards != 0u)
        return false;
    first = &probe->starts[0];
    switch (kind) {
        case W3C_INVOKE_TYPE_EXPR:
        case W3C_INVOKE_CANONICAL_TYPE:
            return probe->start_count == 1u &&
                w3c_materialized_invoke_start_is(
                    first, W3C_SCXML_INVOKE_PROCESSOR, "",
                    SCXML_PAYLOAD_NONE);
        case W3C_INVOKE_SRC_EXPR:
            return probe->start_count == 1u &&
                w3c_materialized_invoke_start_is(
                    first, W3C_SCXML_INVOKE_PROCESSOR,
                    "file:test216sub1.scxml", SCXML_PAYLOAD_NONE);
        case W3C_INVOKE_UNIQUE_IDS:
            return probe->start_count == 2u &&
                w3c_materialized_invoke_start_is(
                    first, W3C_SCXML_INVOKE_PROCESSOR, "",
                    SCXML_PAYLOAD_NONE) &&
                w3c_materialized_invoke_start_is(
                    &probe->starts[1], W3C_SCXML_INVOKE_PROCESSOR, "",
                    SCXML_PAYLOAD_NONE) &&
                first->token < probe->starts[1].token &&
                strcmp(first->id, "s0.1") == 0 &&
                strcmp(probe->starts[1].id, "s0.2") == 0;
        case W3C_INVOKE_NAMED_PAYLOAD:
            return probe->start_count == 1u &&
                w3c_materialized_invoke_start_is(
                    first, W3C_SCXML_INVOKE_PROCESSOR,
                    "file:test226sub1.scxml", SCXML_PAYLOAD_NAMED) &&
                first->payload_entry_count == 1u &&
                strcmp(first->payload_name, "aParam") == 0 &&
                first->payload_value.kind == SCXML_PAYLOAD_VALUE_SINT &&
                first->payload_value.data.sint == INT64_C(1);
        case W3C_INVOKE_CONTENT:
            return probe->start_count == 1u &&
                w3c_materialized_invoke_start_is(
                    first, W3C_SCXML_INVOKE_PROCESSOR, "",
                    SCXML_PAYLOAD_CONTENT) &&
                first->payload_value.kind == SCXML_PAYLOAD_VALUE_SINT &&
                first->payload_value.data.sint == INT64_C(7);
        case W3C_INVOKE_ARGUMENT_ERROR:
            break;
    }
    return false;
}

static void w3c_macrostep_invoke_commit(void *user) {
    w3c_macrostep_invoke_probe *probe =
        (w3c_macrostep_invoke_probe *)user;
    if (probe != NULL) ++probe->start_commits;
}

static void w3c_macrostep_invoke_discard(void *user) {
    w3c_macrostep_invoke_probe *probe =
        (w3c_macrostep_invoke_probe *)user;
    if (probe != NULL) ++probe->start_discards;
}

static void w3c_macrostep_cancel_commit(void *user) {
    w3c_macrostep_invoke_probe *probe =
        (w3c_macrostep_invoke_probe *)user;
    if (probe != NULL) ++probe->cancel_commits;
}

static void w3c_macrostep_cancel_discard(void *user) {
    w3c_macrostep_invoke_probe *probe =
        (w3c_macrostep_invoke_probe *)user;
    if (probe != NULL) ++probe->cancel_discards;
}

static scxml_adapter_status w3c_capture_macrostep_invoke_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_macrostep_invoke_probe *probe =
        (w3c_macrostep_invoke_probe *)user;
    const char *expected_src = NULL;
    w3c_macrostep_invoke_start *start;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->token == 0u || request->id == NULL ||
        request->id_size == 0u ||
        request->id_size >= sizeof(probe->starts[0].id) ||
        probe->start_count >= W3C_MACROSTEP_INVOKE_CAPACITY ||
        !w3c_invoke_text_is(
            request->type, request->type_size, "urn:w3c:test"))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    if (w3c_invoke_text_is(request->id, request->id_size, "invokeS1"))
        expected_src = "ancestor";
    else if (w3c_invoke_text_is(
                 request->id, request->id_size, "invokeS11"))
        expected_src = "transient";
    else if (w3c_invoke_text_is(
                 request->id, request->id_size, "invokeS12"))
        expected_src = "descendant";
    if (expected_src == NULL ||
        !w3c_invoke_text_is(request->src, request->src_size, expected_src)) {
        *out_error = "unexpected W3C 422 invocation descriptor";
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    start = &probe->starts[probe->start_count++];
    start->token = request->token;
    memcpy(start->id, request->id, request->id_size);
    start->id[request->id_size] = '\0';
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_macrostep_invoke_commit,
        w3c_macrostep_invoke_discard,
        probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status w3c_accept_macrostep_invoke_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_macrostep_invoke_probe *probe =
        (w3c_macrostep_invoke_probe *)user;
    if (probe == NULL || request == NULL || request->token == 0u ||
        out_ticket == NULL || out_error == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_macrostep_cancel_commit,
        w3c_macrostep_cancel_discard,
        probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static bool run_w3c_fixture(const char *fixture_name,
                            w3c_run_result *out_result) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    const cflow_statechart_executable_binding *executables = NULL;
    const cflow_statechart_guard_binding *guards = NULL;
    size_t executable_count = 0u;
    size_t guard_count = 0u;
    cflow_executor executor = {0};
    cflow_statechart_instance instance = {0};
    cflow_statechart_instance_config config = {0};
    cflow_statechart_instance_stats stats = {0};
    bool executor_initialized = false;
    bool instance_initialized = false;
    bool succeeded = false;
    int path_size;
    scxml_status compile_status;
    cflow_statechart_instance_status runtime_status;

    if (fixture_name == NULL || out_result == NULL) {
        return false;
    }
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    memset(out_result, 0, sizeof(*out_result));
    source = tt_read_file(path, &source_size);
    if (source == NULL) {
        info("fixture=%s read failed", fixture_name);
        goto cleanup;
    }
    compile_status =
        scxml_compile(&program, source, source_size, NULL, &diagnostic);
    if (compile_status != SCXML_OK) {
        info("fixture=%s compile_status=%d diagnostic=%s", fixture_name,
             (int)compile_status, diagnostic.message);
        goto cleanup;
    }
    if (!scxml_program_state_id(&program, "pass", 4u,
                                      &out_result->pass_state) ||
        !scxml_program_state_id(&program, "fail", 4u,
                                      &out_result->fail_state)) {
        info("fixture=%s result states missing", fixture_name);
        goto cleanup;
    }
    if (!scxml_program_instance_bindings(
            &program, &executables, &executable_count) ||
        !scxml_program_guard_bindings(&program, &guards, &guard_count)) {
        info("fixture=%s runtime bindings missing", fixture_name);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) {
        info("fixture=%s executor initialization failed", fixture_name);
        goto cleanup;
    }
    executor_initialized = true;
    config = (cflow_statechart_instance_config){
        .statechart = scxml_program_statechart(&program),
        .initial_state = scxml_program_initial_state(&program),
        .guards = guards,
        .guard_count = guard_count,
        .executables = executables,
        .executable_count = executable_count,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .executor = &executor};
    runtime_status = cflow_statechart_instance_init(&instance, &config);
    if (runtime_status != CFLOW_STATECHART_INSTANCE_OK) {
        info("fixture=%s runtime_status=%d", fixture_name,
             (int)runtime_status);
        goto cleanup;
    }
    instance_initialized = true;
    if (!cflow_executor_wait_idle(&executor) ||
        !cflow_statechart_instance_get_stats(&instance, &stats)) {
        info("fixture=%s executor wait or stats failed", fixture_name);
        goto cleanup;
    }
    out_result->current_state =
        cflow_statechart_instance_current_state(&instance);
    out_result->done = stats.done;
    out_result->errored = stats.errored;
    succeeded = true;

cleanup:
    if (instance_initialized)
        (void)cflow_statechart_instance_destroy(&instance);
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static bool run_w3c_compile_reject_fixture(
    const char *fixture_name, scxml_status expected_status,
    const char *expected_diagnostic) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    const scxml_cmeta_compile_options_v1 compile_options =
        scxml_cmeta_default_compile_options(&w3c_cmeta_state_desc);
    scxml_status status;
    bool succeeded = false;
    int written;

    if (fixture_name == NULL || expected_status == SCXML_OK ||
        expected_diagnostic == NULL || expected_diagnostic[0] == '\0')
        return false;
    written = snprintf(path, sizeof(path), "%s/%s",
                       SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (written < 0 || (size_t)written >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL || source_size == 0u) {
        info("fixture=%s read failed", fixture_name);
        goto cleanup;
    }
    status = scxml_compile_cmeta(
        &program, source, source_size, NULL, &compile_options, &diagnostic);
    if (status != expected_status || diagnostic.status != expected_status ||
        strstr(diagnostic.message, expected_diagnostic) == NULL ||
        program.impl != NULL) {
        info("fixture=%s compile_status=%d diagnostic_status=%d diagnostic=%s",
             fixture_name, (int)status, (int)diagnostic.status,
             diagnostic.message);
        goto cleanup;
    }
    succeeded = true;

cleanup:
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static void check_w3c_fixture(const char *fixture_name) {
    w3c_run_result result = {0};

    check_true(run_w3c_fixture(fixture_name, &result));
    check_true(result.done);
    check_false(result.errored);
    check_equal(result.current_state, result.pass_state);
    check_not_equal(result.current_state, result.fail_state);
}

static bool run_w3c_content_fixture(const char *fixture_name,
                                    const char *expected_content) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    cflow_machine_state_id pass_state = 0u;
    w3c_content_probe probe = {0};
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND |
            SCXML_EVENT_IO_CAP_CONTENT,
        .prepare_send = w3c_capture_content_send,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    scxml_session_config config = {0};
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;
    int path_size;

    if (fixture_name == NULL || expected_content == NULL) return false;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) goto cleanup;
    if (scxml_compile(
            &program, source, source_size, NULL, &diagnostic) !=
            SCXML_OK ||
        !scxml_program_state_id(
            &program, "pass", sizeof("pass") - 1u, &pass_state) ||
        !cflow_executor_serial_init(&executor))
        goto cleanup;
    executor_initialized = true;
    config = (scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 1u,
        .adapter_internal_event_capacity = 1u};
    config.event_io = &event_io;
    config.adapter_user = &probe;
    if (scxml_session_init(&session, &config) !=
        CFLOW_STATECHART_INSTANCE_OK)
        goto cleanup;
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor) ||
        !scxml_session_get_stats(&session, &stats))
        goto cleanup;
    succeeded = stats.done && !stats.errored &&
        probe.prepare_send_calls == 1u && probe.commits == 1u &&
        probe.discards == 0u &&
        probe.kind == SCXML_CONTENT_TEXT_UTF8 &&
        strcmp(probe.bytes, expected_content) == 0;

cleanup:
    if (session_initialized &&
        scxml_session_destroy(&session) !=
            CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static bool run_w3c_copy_fixture(const char *fixture_name) {
    static const char receiver_source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "datamodel='cmeta'><state id='waiting'>"
        "<transition event='event1' cond='_event.data.first == 1 &amp;&amp; "
        "_event.data.second == 2' target='pass'/>"
        "<transition event='*' target='fail'/></state>"
        "<final id='pass'/><state id='fail'/></scxml>";
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program sender_program = {0};
    scxml_program receiver_program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor sender_executor = {0};
    cflow_executor receiver_executor = {0};
    scxml_session sender = {0};
    scxml_session receiver = {0};
    cflow_statechart_instance_stats sender_stats = {0};
    cflow_statechart_instance_stats receiver_stats = {0};
    cflow_event_view event = {0};
    w3c_copy_probe probe = {0};
    w3c_executor_blocker blocker;
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND |
            SCXML_EVENT_IO_CAP_CONTENT,
        .prepare_send = w3c_capture_copy_send,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const w3c_copy_state sender_initial = {
        .payload = {.first = 1, .second = 2}};
    const w3c_copy_payload receiver_initial = {0};
    const scxml_cmeta_session_options_v1 sender_data = {
        SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        sizeof(sender_data), &sender_initial};
    const scxml_cmeta_session_options_v1 receiver_data = {
        SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        sizeof(receiver_data), &receiver_initial};
    const scxml_cmeta_compile_options_v1 sender_compile_options =
        scxml_cmeta_default_compile_options(&w3c_copy_state_desc);
    const scxml_cmeta_compile_options_v1 receiver_compile_options =
        scxml_cmeta_default_compile_options(&w3c_copy_payload_desc);
    scxml_session_config sender_config = {0};
    scxml_session_config receiver_config = {0};
    scxml_event_metadata metadata = {
        .abi_version = SCXML_EVENT_METADATA_ABI,
        .struct_size = sizeof(metadata)};
    bool sender_executor_initialized = false;
    bool receiver_executor_initialized = false;
    bool sender_initialized = false;
    bool receiver_initialized = false;
    bool blocker_initialized = false;
    bool succeeded = false;
    int path_size;

    if (fixture_name == NULL) return false;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) goto cleanup;
    if (scxml_compile_cmeta(
            &sender_program, source, source_size, NULL,
            &sender_compile_options, &diagnostic) != SCXML_OK ||
        scxml_compile_cmeta(
            &receiver_program, receiver_source,
            sizeof(receiver_source) - 1u, NULL,
            &receiver_compile_options, &diagnostic) != SCXML_OK ||
        !cflow_executor_serial_init(&sender_executor))
        goto cleanup;
    sender_executor_initialized = true;
    if (!cflow_executor_serial_init(&receiver_executor)) goto cleanup;
    receiver_executor_initialized = true;
    sender_config = (scxml_session_config){
        .program = &sender_program,
        .executor = &sender_executor,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 2u,
        .adapter_internal_event_capacity = 1u};
    receiver_config = sender_config;
    receiver_config.program = &receiver_program;
    receiver_config.executor = &receiver_executor;
    receiver_config.effect_capacity = 0u;
    receiver_config.adapter_internal_event_capacity = 0u;
    if (scxml_session_init_cmeta(
            &receiver, &receiver_config, &receiver_data) !=
            CFLOW_STATECHART_INSTANCE_OK)
        goto cleanup;
    receiver_initialized = true;
    sender_config.event_io = &event_io;
    sender_config.adapter_user = &probe;
    if (scxml_session_init_cmeta(
            &sender, &sender_config, &sender_data) !=
            CFLOW_STATECHART_INSTANCE_OK)
        goto cleanup;
    sender_initialized = true;
    if (!cflow_executor_wait_idle(&sender_executor) ||
        !scxml_session_get_stats(&sender, &sender_stats) ||
        !sender_stats.done || sender_stats.errored || probe.sends != 1u ||
        probe.commits != 1u || probe.discards != 0u ||
        probe.message.first != 1 || probe.message.second != 2 ||
        !scxml_program_event(
            &receiver_program, "event1", sizeof("event1") - 1u, &event))
        goto cleanup;
    atomic_init(&blocker.entered, false);
    atomic_init(&blocker.release, false);
    blocker_initialized = true;
    if (cflow_executor_try_post(
            &receiver_executor, w3c_block_executor, &blocker) !=
        CFLOW_ADMISSION_ACCEPTED)
        goto cleanup;
    while (!atomic_load(&blocker.entered)) turbo_thread_yield();
    metadata.data = (scxml_content_view){
        .kind = SCXML_CONTENT_CMETA,
        .schema = &w3c_copy_payload_desc,
        .object = &probe.message};
    if (scxml_session_try_send_with_metadata(&receiver, &event, &metadata) !=
        CFLOW_MAILBOX_OK)
        goto cleanup;
    probe.message.first = 7;
    probe.message.second = 8;
    atomic_store(&blocker.release, true);
    if (!cflow_executor_wait_idle(&receiver_executor) ||
        !scxml_session_get_stats(&receiver, &receiver_stats))
        goto cleanup;
    succeeded = receiver_stats.done && !receiver_stats.errored;

cleanup:
    if (blocker_initialized) atomic_store(&blocker.release, true);
    if (sender_initialized &&
        scxml_session_destroy(&sender) != CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (receiver_initialized &&
        scxml_session_destroy(&receiver) != CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (receiver_executor_initialized)
        cflow_executor_destroy(&receiver_executor);
    if (sender_executor_initialized)
        cflow_executor_destroy(&sender_executor);
    scxml_program_destroy(&receiver_program);
    scxml_program_destroy(&sender_program);
    free(source);
    return succeeded;
}

static bool run_w3c_invoke_idlocation_fixture(
    const char *fixture_name, const char *expected_id,
    const char *returned_event) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    cflow_machine_state_id pass_state = 0u;
    w3c_invoke_probe probe = {0};
    w3c_result_probe result = {0};
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND,
        .prepare_send = w3c_capture_result_send,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const scxml_invoke_adapter invoke = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(invoke),
        .capabilities = SCXML_INVOKE_CAP_START |
            SCXML_INVOKE_CAP_CANCEL,
        .prepare_start = w3c_capture_invoke_start,
        .prepare_cancel = w3c_accept_invoke_cancel,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const w3c_cmeta_state initial = {0};
    const scxml_cmeta_session_options_v1 data = {
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(data),
        .initial_state = &initial};
    const scxml_cmeta_compile_options_v1 compile_options =
        scxml_cmeta_default_compile_options(&w3c_cmeta_state_desc);
    scxml_session_config config = {0};
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;
    int path_size;

    if (fixture_name == NULL) return false;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) goto cleanup;
    if (scxml_compile_cmeta(
            &program, source, source_size, NULL, &compile_options,
            &diagnostic) != SCXML_OK) {
        info("fixture=%s compile diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (!scxml_program_state_id(
            &program, "pass", sizeof("pass") - 1u, &pass_state)) {
        info("fixture=%s pass state missing", fixture_name);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) {
        info("fixture=%s executor initialization failed", fixture_name);
        goto cleanup;
    }
    executor_initialized = true;
    config = (scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 2u,
        .adapter_internal_event_capacity = 2u,
        .invocation_capacity = 1u,
        .event_io = &event_io,
        .adapter_user = &result,
        .invoke = &invoke,
        .invoke_user = &probe};
    {
        const cflow_statechart_instance_status init_status =
            scxml_session_init_cmeta(&session, &config, &data);
        if (init_status != CFLOW_STATECHART_INSTANCE_OK) {
            info("fixture=%s session init status=%d error=%s", fixture_name,
                 (int)init_status, scxml_session_error(&session));
            goto cleanup;
        }
    }
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor)) {
        info("fixture=%s initial wait failed", fixture_name);
        goto cleanup;
    }
    if (probe.starts != 1u || probe.commits != 1u ||
        probe.discards != 0u || probe.token == 0u || probe.id[0] == '\0' ||
        (expected_id != NULL && strcmp(probe.id, expected_id) != 0)) {
        info("fixture=%s starts=%zu commits=%zu discards=%zu token=%llu id=%s",
             fixture_name, probe.starts, probe.commits, probe.discards,
             (unsigned long long)probe.token, probe.id);
        goto cleanup;
    }
    if (returned_event == NULL) {
        if (scxml_session_report_invoke_done(&session, probe.token) !=
            CFLOW_MAILBOX_OK) {
            info("fixture=%s done report rejected", fixture_name);
            goto cleanup;
        }
    } else {
        cflow_event_view event = {0};
        const size_t event_size = strlen(returned_event);
        if (!scxml_program_event(
                &program, returned_event, event_size, &event) ||
            scxml_session_report_invoke_event(
                &session, probe.token, &event) != CFLOW_MAILBOX_OK) {
            info("fixture=%s child event report rejected", fixture_name);
            goto cleanup;
        }
    }
    if (!cflow_executor_wait_idle(&executor) ||
        !scxml_session_get_stats(&session, &stats)) {
        info("fixture=%s final wait or stats failed", fixture_name);
        goto cleanup;
    }
    succeeded = stats.done && !stats.errored &&
        (returned_event == NULL ||
         (result.prepare_send_calls == 1u && result.commits == 1u &&
          result.discards == 0u &&
          strcmp(result.event, "result.pass") == 0));
    if (!succeeded)
        info("fixture=%s done=%d errored=%d error=%s", fixture_name,
             stats.done ? 1 : 0, stats.errored ? 1 : 0,
             scxml_session_error(&session));

cleanup:
    if (session_initialized &&
        scxml_session_destroy(&session) !=
            CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static bool run_w3c_top_level_final_child(void) {
    static const char child_fixture[] = "test247-child.scxml";
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    scxml_session_config config = {0};
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;
    int path_size;

    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, child_fixture);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) {
        info("fixture=%s could not be read", child_fixture);
        goto cleanup;
    }
    if (scxml_compile(
            &program, source, source_size, NULL, &diagnostic) != SCXML_OK) {
        info("fixture=%s compile diagnostic=%s", child_fixture,
             diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) goto cleanup;
    executor_initialized = true;
    config = (scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = 1u,
        .internal_event_capacity = 1u,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 0u,
        .adapter_internal_event_capacity = 0u};
    if (scxml_session_init(&session, &config) !=
        CFLOW_STATECHART_INSTANCE_OK) {
        info("fixture=%s child init error=%s", child_fixture,
             scxml_session_error(&session));
        goto cleanup;
    }
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor) ||
        !scxml_session_get_stats(&session, &stats)) {
        info("fixture=%s child wait or stats failed", child_fixture);
        goto cleanup;
    }
    succeeded = stats.done && !stats.errored;
    if (!succeeded)
        info("fixture=%s child done=%d errored=%d error=%s", child_fixture,
             stats.done ? 1 : 0, stats.errored ? 1 : 0,
             scxml_session_error(&session));

cleanup:
    if (session_initialized &&
        scxml_session_destroy(&session) != CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static bool run_w3c_invoke_completion_fixture(
    const char *fixture_name, w3c_invoke_completion_case test_case) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    scxml_invoke_stats invoke_stats = {0};
    w3c_invoke_completion_probe probe = {0};
    w3c_result_probe result = {0};
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND,
        .prepare_send = w3c_capture_result_send,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const scxml_invoke_adapter invoke = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(invoke),
        .capabilities = SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
        .prepare_start = w3c_capture_invoke_completion_start,
        .prepare_cancel = w3c_capture_invoke_completion_cancel,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const w3c_cmeta_state initial = {0};
    const scxml_cmeta_session_options_v1 data = {
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(data),
        .initial_state = &initial};
    const scxml_cmeta_compile_options_v1 compile_options =
        scxml_cmeta_default_compile_options(&w3c_cmeta_state_desc);
    scxml_session_config config = {0};
    const char *expected_id = NULL;
    uint64_t expected_returned_accepted = 0u;
    uint64_t expected_returned_rejected = 0u;
    uint64_t expected_completed = 0u;
    uint64_t expected_cancelled = 0u;
    size_t expected_cancel_prepares = 0u;
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;
    int path_size;

    switch (test_case) {
        case W3C_INVOKE_RETURN_PROVENANCE:
            expected_id = "invoke228";
            expected_returned_accepted = 1u;
            expected_completed = 1u;
            break;
        case W3C_INVOKE_MULTIPLE_RETURN:
            expected_id = "invoke232";
            expected_returned_accepted = 3u;
            expected_completed = 1u;
            break;
        case W3C_INVOKE_EXACT_COMPLETION:
            expected_id = "foo";
            expected_returned_accepted = 1u;
            expected_completed = 1u;
            break;
        case W3C_INVOKE_TERMINAL_COMPLETION:
            expected_id = "invoke236";
            expected_returned_accepted = 2u;
            expected_returned_rejected = 1u;
            expected_completed = 1u;
            break;
        case W3C_INVOKE_CHILD_FINAL_COMPLETION:
            expected_id = "invoke247";
            expected_returned_accepted = 1u;
            expected_completed = 1u;
            break;
        default:
            return false;
    }
    if (fixture_name == NULL) return false;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) {
        info("fixture=%s could not be read", fixture_name);
        goto cleanup;
    }
    if (scxml_compile_cmeta(
            &program, source, source_size, NULL, &compile_options,
            &diagnostic) != SCXML_OK) {
        info("fixture=%s compile diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) goto cleanup;
    executor_initialized = true;
    config = (scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = W3C_INVOKE_COMPLETION_EXTERNAL_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 2u,
        .adapter_internal_event_capacity = 2u,
        .invocation_capacity = 1u,
        .event_io = &event_io,
        .adapter_user = &result,
        .invoke = &invoke,
        .invoke_user = &probe};
    if (scxml_session_init_cmeta(&session, &config, &data) !=
        CFLOW_STATECHART_INSTANCE_OK) {
        info("fixture=%s session init error=%s", fixture_name,
             scxml_session_error(&session));
        goto cleanup;
    }
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor) || probe.start_prepares != 1u ||
        probe.start_commits != 1u || probe.start_discards != 0u ||
        probe.token == 0u || strcmp(probe.id, expected_id) != 0) {
        info("fixture=%s starts=%zu commits=%zu discards=%zu token=%llu id=%s",
             fixture_name, probe.start_prepares, probe.start_commits,
             probe.start_discards, (unsigned long long)probe.token, probe.id);
        goto cleanup;
    }

    if (test_case == W3C_INVOKE_RETURN_PROVENANCE) {
        if (scxml_session_report_invoke_done(&session, probe.token) !=
            CFLOW_MAILBOX_OK)
            goto cleanup;
    } else if (test_case == W3C_INVOKE_MULTIPLE_RETURN) {
        static const char *const event_names[] = {
            "childToParent1", "childToParent2"};
        size_t event_index;
        for (event_index = 0u;
             event_index < sizeof(event_names) / sizeof(event_names[0]);
             ++event_index) {
            cflow_event_view event = {0};
            if (!scxml_program_event(
                    &program, event_names[event_index],
                    strlen(event_names[event_index]), &event) ||
                scxml_session_report_invoke_event(
                    &session, probe.token, &event) != CFLOW_MAILBOX_OK)
                goto cleanup;
        }
        if (scxml_session_report_invoke_done(&session, probe.token) !=
            CFLOW_MAILBOX_OK)
            goto cleanup;
    } else if (test_case == W3C_INVOKE_EXACT_COMPLETION) {
        if (scxml_session_report_invoke_done(&session, probe.token) !=
            CFLOW_MAILBOX_OK)
            goto cleanup;
    } else if (test_case == W3C_INVOKE_TERMINAL_COMPLETION) {
        cflow_event_view event = {0};
        if (!scxml_program_event(
                &program, "childToParent", sizeof("childToParent") - 1u,
                &event) ||
            scxml_session_report_invoke_event(
                &session, probe.token, &event) != CFLOW_MAILBOX_OK ||
            scxml_session_report_invoke_done(
                &session, probe.token) != CFLOW_MAILBOX_OK ||
            !cflow_executor_wait_idle(&executor) ||
            scxml_session_report_invoke_event(
                &session, probe.token, &event) !=
                CFLOW_MAILBOX_INVALID_ARGUMENT ||
            !w3c_admit_external_event(&session, &program, "confirm"))
            goto cleanup;
    } else {
        if (!run_w3c_top_level_final_child() ||
            scxml_session_report_invoke_done(&session, probe.token) !=
                CFLOW_MAILBOX_OK)
            goto cleanup;
    }

    if (!cflow_executor_wait_idle(&executor) ||
        !scxml_session_get_stats(&session, &stats) ||
        !scxml_session_get_invoke_stats(&session, &invoke_stats))
        goto cleanup;
    succeeded = stats.done && !stats.errored &&
        probe.start_prepares == 1u && probe.start_commits == 1u &&
        probe.start_discards == 0u &&
        probe.cancel_prepares == expected_cancel_prepares &&
        probe.cancel_commits == expected_cancel_prepares &&
        probe.cancel_discards == 0u &&
        result.prepare_send_calls == 1u && result.commits == 1u &&
        result.discards == 0u && strcmp(result.event, "result.pass") == 0 &&
        invoke_stats.started == 1u && invoke_stats.start_failed == 0u &&
        invoke_stats.cancelled == expected_cancelled &&
        invoke_stats.cancel_failed == 0u &&
        invoke_stats.completed == expected_completed &&
        invoke_stats.returned_accepted == expected_returned_accepted &&
        invoke_stats.returned_rejected == expected_returned_rejected &&
        invoke_stats.active == 0u;
    if (!succeeded)
        info("fixture=%s done=%d errored=%d result=%s start=%zu/%zu/%zu cancel=%zu/%zu/%zu accepted=%llu rejected=%llu completed=%llu active=%zu error=%s",
             fixture_name, stats.done ? 1 : 0, stats.errored ? 1 : 0,
             result.event, probe.start_prepares, probe.start_commits,
             probe.start_discards, probe.cancel_prepares,
             probe.cancel_commits, probe.cancel_discards,
             (unsigned long long)invoke_stats.returned_accepted,
             (unsigned long long)invoke_stats.returned_rejected,
             (unsigned long long)invoke_stats.completed,
             invoke_stats.active, scxml_session_error(&session));

cleanup:
    if (session_initialized &&
        scxml_session_destroy(&session) != CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static bool run_w3c_invoke_cancellation_fixture(
    const char *fixture_name, w3c_invoke_cancellation_case test_case) {
    char parent_path[W3C_FIXTURE_PATH_CAPACITY];
    char child_path[W3C_FIXTURE_PATH_CAPACITY];
    char *parent_source = NULL;
    char *child_source = NULL;
    size_t parent_source_size = 0u;
    size_t child_source_size = 0u;
    scxml_program parent_program = {0};
    scxml_program child_program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor parent_executor = {0};
    cflow_executor child_executor = {0};
    scxml_session parent = {0};
    scxml_session child = {0};
    cflow_statechart_instance_stats parent_stats = {0};
    cflow_statechart_instance_stats child_stats = {0};
    scxml_invoke_stats invoke_stats = {0};
    w3c_invoke_cancellation_probe probe = {.child = &child};
    w3c_invoke_cancellation_log cancellation_log = {0};
    w3c_result_probe result = {0};
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND,
        .prepare_send = w3c_capture_result_send,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const scxml_invoke_adapter invoke = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(invoke),
        .capabilities = SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
        .prepare_start = w3c_capture_invoke_cancellation_start,
        .prepare_cancel = w3c_capture_invoke_cancellation_cancel,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    scxml_session_config parent_config = {0};
    scxml_session_config child_config = {0};
    cflow_event_view child_finish = {0};
    cflow_event_view child_return = {0};
    const char *child_fixture = NULL;
    const char *expected_id = NULL;
    uint64_t expected_returned_rejected = 0u;
    tlog_t *previous_logger = tlog_peek_default();
    tlog_t *logger = NULL;
    turbo_log_sink_t *sink = NULL;
    bool require_exit_logs = false;
    bool parent_executor_initialized = false;
    bool child_executor_initialized = false;
    bool parent_initialized = false;
    bool child_initialized = false;
    bool succeeded = false;
    bool reached_verification = false;
    cflow_statechart_instance_status parent_destroy_status =
        CFLOW_STATECHART_INSTANCE_OK;
    cflow_statechart_instance_status child_destroy_status =
        CFLOW_STATECHART_INSTANCE_OK;
    int path_size;

    if (fixture_name == NULL) return false;
    if (test_case == W3C_INVOKE_CANCEL_STOPS_CHILD) {
        child_fixture = "test237-child.scxml";
        expected_id = "invoke237";
        expected_returned_rejected = 1u;
    } else if (test_case == W3C_INVOKE_CANCEL_REJECTS_RETURN) {
        child_fixture = "test252-child.scxml";
        expected_id = "invoke252";
        expected_returned_rejected = 2u;
    } else if (test_case == W3C_INVOKE_CANCEL_RUNS_CHILD_ONEXIT) {
        const tlog_config_t log_config = {
            .min_level = TURBO_LOG_LEVEL_DEBUG, .buffer_size = 0u};
        child_fixture = "test250-child.scxml";
        expected_id = "invoke250";
        expected_returned_rejected = 1u;
        require_exit_logs = true;
        logger = tlog_create(&log_config);
        if (logger == NULL) goto cleanup;
        sink = turbo_sink_callback_create(
            w3c_capture_invoke_cancellation_log, &cancellation_log);
        if (sink == NULL || tlog_add_sink(logger, sink) != 0) goto cleanup;
        sink = NULL;
        tlog_set_default(logger);
    } else {
        return false;
    }
    path_size = snprintf(parent_path, sizeof(parent_path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(parent_path))
        return false;
    path_size = snprintf(child_path, sizeof(child_path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, child_fixture);
    if (path_size < 0 || (size_t)path_size >= sizeof(child_path))
        return false;
    parent_source = tt_read_file(parent_path, &parent_source_size);
    child_source = tt_read_file(child_path, &child_source_size);
    if (parent_source == NULL || child_source == NULL) {
        info("fixture=%s child=%s could not be read", fixture_name,
             child_fixture);
        goto cleanup;
    }
    if (scxml_compile(
            &child_program, child_source, child_source_size, NULL,
            &diagnostic) != SCXML_OK) {
        info("fixture=%s child compile diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (scxml_compile(
            &parent_program, parent_source, parent_source_size, NULL,
            &diagnostic) != SCXML_OK) {
        info("fixture=%s parent compile diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&child_executor)) goto cleanup;
    child_executor_initialized = true;
    child_config = (scxml_session_config){
        .program = &child_program,
        .executor = &child_executor,
        .external_event_capacity = 1u,
        .internal_event_capacity = 1u,
        .completion_capacity = 1u,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 0u,
        .adapter_internal_event_capacity = 0u};
    if (scxml_session_init(&child, &child_config) !=
        CFLOW_STATECHART_INSTANCE_OK) {
        info("fixture=%s child init error=%s", fixture_name,
             scxml_session_error(&child));
        goto cleanup;
    }
    child_initialized = true;
    if (!cflow_executor_wait_idle(&child_executor) ||
        !scxml_session_get_stats(&child, &child_stats) || child_stats.done ||
        child_stats.errored || child_stats.cancelled)
        goto cleanup;

    if (!cflow_executor_serial_init(&parent_executor)) goto cleanup;
    parent_executor_initialized = true;
    parent_config = (scxml_session_config){
        .program = &parent_program,
        .executor = &parent_executor,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 2u,
        .adapter_internal_event_capacity = 2u,
        .invocation_capacity = 1u,
        .event_io = &event_io,
        .adapter_user = &result,
        .invoke = &invoke,
        .invoke_user = &probe};
    if (scxml_session_init(&parent, &parent_config) !=
        CFLOW_STATECHART_INSTANCE_OK) {
        info("fixture=%s parent init error=%s", fixture_name,
             scxml_session_error(&parent));
        goto cleanup;
    }
    parent_initialized = true;
    if (!cflow_executor_wait_idle(&parent_executor) ||
        probe.start_prepares != 1u || probe.start_commits != 1u ||
        probe.start_discards != 0u || probe.token == 0u ||
        strcmp(probe.id, expected_id) != 0 ||
        !w3c_admit_external_event(&parent, &parent_program, "leave") ||
        !cflow_executor_wait_idle(&parent_executor) ||
        probe.cancel_prepares != 1u || probe.cancel_commits != 1u ||
        probe.cancel_discards != 0u || !probe.child_cancelled ||
        !cflow_executor_wait_idle(&child_executor) ||
        !scxml_session_get_stats(&child, &child_stats))
        goto cleanup;
    if (!child_stats.done || child_stats.errored || !child_stats.cancelled ||
        !scxml_program_event(
            &child_program, "finish", sizeof("finish") - 1u,
            &child_finish) ||
        scxml_session_try_send(&child, &child_finish) !=
            CFLOW_MAILBOX_CANCELLED)
        goto cleanup;

    if (test_case == W3C_INVOKE_CANCEL_REJECTS_RETURN) {
        if (!scxml_program_event(
                &parent_program, "childToParent",
                sizeof("childToParent") - 1u, &child_return) ||
            scxml_session_report_invoke_event(
                &parent, probe.token, &child_return) !=
                CFLOW_MAILBOX_INVALID_ARGUMENT)
            goto cleanup;
    }
    if (scxml_session_report_invoke_done(&parent, probe.token) !=
            CFLOW_MAILBOX_INVALID_ARGUMENT ||
        !w3c_admit_external_event(&parent, &parent_program, "timeout") ||
        !cflow_executor_wait_idle(&parent_executor) ||
        !scxml_session_get_stats(&parent, &parent_stats) ||
        !scxml_session_get_invoke_stats(&parent, &invoke_stats))
        goto cleanup;
    if (logger != NULL) tlog_flush(logger);

    reached_verification = true;
    succeeded = parent_stats.done && !parent_stats.errored &&
        child_stats.done && !child_stats.errored && child_stats.cancelled &&
        child_stats.active_state_count == 0u &&
        result.prepare_send_calls == 1u && result.commits == 1u &&
        result.discards == 0u && strcmp(result.event, "result.pass") == 0 &&
        probe.start_prepares == 1u && probe.start_commits == 1u &&
        probe.start_discards == 0u && probe.cancel_prepares == 1u &&
        probe.cancel_commits == 1u && probe.cancel_discards == 0u &&
        invoke_stats.started == 1u && invoke_stats.start_failed == 0u &&
        invoke_stats.cancelled == 1u && invoke_stats.cancel_failed == 0u &&
        invoke_stats.completed == 0u &&
        invoke_stats.returned_accepted == 0u &&
        invoke_stats.returned_rejected == expected_returned_rejected &&
        invoke_stats.active == 0u &&
        (!require_exit_logs ||
         (cancellation_log.count == W3C_CANCELLATION_LOG_CAPACITY &&
          strcmp(cancellation_log.messages[0], "Exiting sub01") == 0 &&
          strcmp(cancellation_log.messages[1], "Exiting sub0") == 0));
    if (!succeeded)
        info("fixture=%s parent_done=%d child_cancelled=%d result=%s start=%zu/%zu/%zu cancel=%zu/%zu/%zu accepted=%llu rejected=%llu completed=%llu active=%zu exit_logs=%zu/%s/%s parent_error=%s child_error=%s",
             fixture_name, parent_stats.done ? 1 : 0,
             child_stats.cancelled ? 1 : 0, result.event,
             probe.start_prepares, probe.start_commits,
             probe.start_discards, probe.cancel_prepares,
             probe.cancel_commits, probe.cancel_discards,
             (unsigned long long)invoke_stats.returned_accepted,
             (unsigned long long)invoke_stats.returned_rejected,
             (unsigned long long)invoke_stats.completed,
             invoke_stats.active, cancellation_log.count,
             cancellation_log.messages[0], cancellation_log.messages[1],
             scxml_session_error(&parent),
             scxml_session_error(&child));

cleanup:
    if (parent_initialized) {
        parent_destroy_status = scxml_session_destroy(&parent);
        if (parent_destroy_status != CFLOW_STATECHART_INSTANCE_OK)
            succeeded = false;
    }
    if (child_initialized) {
        child_destroy_status = scxml_session_destroy(&child);
        if (child_destroy_status != CFLOW_STATECHART_INSTANCE_OK)
            succeeded = false;
    }
    if (parent_executor_initialized)
        cflow_executor_destroy(&parent_executor);
    if (child_executor_initialized)
        cflow_executor_destroy(&child_executor);
    if (logger != NULL) tlog_flush(logger);
    tlog_set_default(previous_logger);
    if (sink != NULL) turbo_sink_destroy(sink);
    if (logger != NULL) tlog_destroy(logger);
    scxml_program_destroy(&parent_program);
    scxml_program_destroy(&child_program);
    free(parent_source);
    free(child_source);
    if (!succeeded &&
        test_case == W3C_INVOKE_CANCEL_RUNS_CHILD_ONEXIT)
        info("test250 reached_verification=%d parent_destroy=%d child_destroy=%d logs=%zu/%s/%s child_done=%d child_cancelled=%d child_errored=%d",
             reached_verification ? 1 : 0, (int)parent_destroy_status,
             (int)child_destroy_status, cancellation_log.count,
             cancellation_log.messages[0], cancellation_log.messages[1],
             child_stats.done ? 1 : 0, child_stats.cancelled ? 1 : 0,
             child_stats.errored ? 1 : 0);
    return succeeded;
}

static bool run_w3c_invoke_autoforward_fixture(
    const char *fixture_name, bool require_all_fields) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    scxml_invoke_stats invoke_stats = {0};
    w3c_invoke_autoforward_probe probe = {
        .require_all_fields = require_all_fields};
    w3c_result_probe result = {0};
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND,
        .prepare_send = w3c_capture_result_send,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const scxml_invoke_adapter invoke = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(invoke),
        .capabilities = SCXML_INVOKE_CAP_START |
            SCXML_INVOKE_CAP_CANCEL | SCXML_INVOKE_CAP_FORWARD,
        .prepare_start = w3c_capture_autoforward_start,
        .prepare_cancel = w3c_capture_autoforward_cancel,
        .prepare_forward = w3c_capture_autoforward_event,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    scxml_session_config config = {0};
    cflow_event_view input_event = {0};
    cflow_event_view response_event = {0};
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;
    int path_size;

    if (fixture_name == NULL) return false;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) {
        info("fixture=%s could not be read", fixture_name);
        goto cleanup;
    }
    if (scxml_compile(
            &program, source, source_size, NULL, &diagnostic) != SCXML_OK) {
        info("fixture=%s compile diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) goto cleanup;
    executor_initialized = true;
    config = (scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 4u,
        .adapter_internal_event_capacity = 2u,
        .invocation_capacity = 1u,
        .event_io = &event_io,
        .adapter_user = &result,
        .invoke = &invoke,
        .invoke_user = &probe};
    if (scxml_session_init(&session, &config) !=
        CFLOW_STATECHART_INSTANCE_OK) {
        info("fixture=%s session init error=%s", fixture_name,
             scxml_session_error(&session));
        goto cleanup;
    }
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor) ||
        probe.start_prepares != 1u || probe.start_commits != 1u ||
        probe.start_discards != 0u)
        goto cleanup;

    if (require_all_fields) {
        const scxml_event_metadata metadata = {
            .abi_version = SCXML_EVENT_METADATA_ABI,
            .struct_size = sizeof(scxml_event_metadata),
            .send_id = W3C_AUTOFORWARD_SEND_ID,
            .send_id_size = sizeof(W3C_AUTOFORWARD_SEND_ID) - 1u,
            .origin = W3C_AUTOFORWARD_ORIGIN,
            .origin_size = sizeof(W3C_AUTOFORWARD_ORIGIN) - 1u,
            .origin_type = W3C_SCXML_EVENT_PROCESSOR,
            .origin_type_size = sizeof(W3C_SCXML_EVENT_PROCESSOR) - 1u,
            .invoke_id = W3C_AUTOFORWARD_INVOKE_ID,
            .invoke_id_size = sizeof(W3C_AUTOFORWARD_INVOKE_ID) - 1u,
            .data = {
                .kind = SCXML_CONTENT_TEXT_UTF8,
                .bytes = W3C_AUTOFORWARD_DATA,
                .byte_count = sizeof(W3C_AUTOFORWARD_DATA) - 1u}};
        if (!scxml_program_event(
                &program, W3C_AUTOFORWARD_INPUT_EVENT,
                sizeof(W3C_AUTOFORWARD_INPUT_EVENT) - 1u, &input_event) ||
            scxml_session_try_send_with_metadata(
                &session, &input_event, &metadata) != CFLOW_MAILBOX_OK)
            goto cleanup;
    } else {
        if (!scxml_program_event(
                &program, "childToParent",
                sizeof("childToParent") - 1u, &input_event) ||
            scxml_session_report_invoke_event(
                &session, probe.token, &input_event) != CFLOW_MAILBOX_OK)
            goto cleanup;
    }
    if (!cflow_executor_wait_idle(&executor) || !probe.deliverable)
        goto cleanup;

    if (require_all_fields) {
        if (!scxml_program_event(
                &program, "fieldsEqual", sizeof("fieldsEqual") - 1u,
                &response_event))
            goto cleanup;
    } else if (!scxml_program_event(
                   &program, "eventReceived",
                   sizeof("eventReceived") - 1u, &response_event)) {
        goto cleanup;
    }
    if (scxml_session_report_invoke_event(
            &session, probe.token, &response_event) != CFLOW_MAILBOX_OK ||
        !cflow_executor_wait_idle(&executor) ||
        !scxml_session_get_stats(&session, &stats) ||
        !scxml_session_get_invoke_stats(&session, &invoke_stats))
        goto cleanup;
    succeeded = stats.done && !stats.errored &&
        result.prepare_send_calls == 1u && result.commits == 1u &&
        result.discards == 0u && strcmp(result.event, "result.pass") == 0 &&
        probe.forward_prepares == W3C_AUTOFORWARD_EVENT_CAPACITY &&
        probe.forward_commits == W3C_AUTOFORWARD_EVENT_CAPACITY &&
        probe.forward_discards == 0u &&
        probe.cancel_prepares == 1u && probe.cancel_commits == 1u &&
        probe.cancel_discards == 0u && invoke_stats.started == 1u &&
        invoke_stats.start_failed == 0u && invoke_stats.cancelled == 1u &&
        invoke_stats.cancel_failed == 0u && invoke_stats.forwarded == 2u &&
        invoke_stats.forward_failed == 0u &&
        invoke_stats.returned_accepted == (require_all_fields ? 1u : 2u) &&
        invoke_stats.returned_rejected == 0u && invoke_stats.active == 0u;
    if (!succeeded)
        info("fixture=%s done=%d errored=%d result=%s start=%zu/%zu/%zu forward=%zu/%zu/%zu cancel=%zu/%zu/%zu accepted=%llu forwarded=%llu active=%zu error=%s",
             fixture_name, stats.done ? 1 : 0, stats.errored ? 1 : 0,
             result.event, probe.start_prepares, probe.start_commits,
             probe.start_discards, probe.forward_prepares,
             probe.forward_commits, probe.forward_discards,
             probe.cancel_prepares, probe.cancel_commits,
             probe.cancel_discards,
             (unsigned long long)invoke_stats.returned_accepted,
             (unsigned long long)invoke_stats.forwarded,
             invoke_stats.active, scxml_session_error(&session));

cleanup:
    if (session_initialized &&
        scxml_session_destroy(&session) != CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static bool run_w3c_invoke_finalize_fixture(
    const char *fixture_name, w3c_invoke_finalize_case test_case) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    scxml_invoke_stats invoke_stats = {0};
    w3c_invoke_finalize_probe probe = {0};
    w3c_result_probe result = {0};
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND,
        .prepare_send = w3c_capture_result_send,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const scxml_invoke_adapter invoke = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(invoke),
        .capabilities = SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
        .prepare_start = w3c_capture_invoke_finalize_start,
        .prepare_cancel = w3c_capture_invoke_finalize_cancel,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const w3c_cmeta_state initial = {0};
    const scxml_cmeta_session_options_v1 data = {
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(data),
        .initial_state = &initial};
    const scxml_cmeta_compile_options_v1 compile_options =
        scxml_cmeta_default_compile_options(&w3c_cmeta_state_desc);
    scxml_session_config config = {0};
    cflow_event_view returned_event = {0};
    size_t expected_count;
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;
    int path_size;

    if (fixture_name == NULL) return false;
    if (test_case == W3C_INVOKE_FINALIZE_BEFORE_SELECTION) {
        probe.expected_ids[0] = "invoke233";
        expected_count = 1u;
    } else if (test_case == W3C_INVOKE_FINALIZE_MATCHING_ONLY) {
        probe.expected_ids[0] = "first";
        probe.expected_ids[1] = "second";
        expected_count = 2u;
    } else {
        return false;
    }
    probe.expected_count = expected_count;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) {
        info("fixture=%s could not be read", fixture_name);
        goto cleanup;
    }
    if (scxml_compile_cmeta(
            &program, source, source_size, NULL, &compile_options,
            &diagnostic) != SCXML_OK) {
        info("fixture=%s compile diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) goto cleanup;
    executor_initialized = true;
    config = (scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = 1u,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 4u,
        .adapter_internal_event_capacity = 2u,
        .invocation_capacity = expected_count,
        .event_io = &event_io,
        .adapter_user = &result,
        .invoke = &invoke,
        .invoke_user = &probe};
    if (scxml_session_init_cmeta(&session, &config, &data) !=
        CFLOW_STATECHART_INSTANCE_OK) {
        info("fixture=%s session init error=%s", fixture_name,
             scxml_session_error(&session));
        goto cleanup;
    }
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor) ||
        probe.start_count != expected_count ||
        probe.start_commits != expected_count ||
        probe.start_discards != 0u ||
        !scxml_program_event(
            &program, "childToParent", sizeof("childToParent") - 1u,
            &returned_event) ||
        scxml_session_report_invoke_event(
            &session, probe.starts[0].token, &returned_event) !=
            CFLOW_MAILBOX_OK ||
        !cflow_executor_wait_idle(&executor) ||
        !scxml_session_get_stats(&session, &stats) ||
        !scxml_session_get_invoke_stats(&session, &invoke_stats))
        goto cleanup;
    succeeded = stats.done && !stats.errored &&
        result.prepare_send_calls == 1u && result.commits == 1u &&
        result.discards == 0u && strcmp(result.event, "result.pass") == 0 &&
        probe.cancel_prepares == expected_count &&
        probe.cancel_commits == expected_count &&
        probe.cancel_discards == 0u &&
        invoke_stats.started == expected_count &&
        invoke_stats.start_failed == 0u &&
        invoke_stats.cancelled == expected_count &&
        invoke_stats.cancel_failed == 0u &&
        invoke_stats.completed == 0u &&
        invoke_stats.returned_accepted == 1u &&
        invoke_stats.returned_rejected == 0u &&
        invoke_stats.active == 0u;
    if (!succeeded)
        info("fixture=%s done=%d errored=%d result=%s start=%zu/%zu/%zu cancel=%zu/%zu/%zu accepted=%llu completed=%llu active=%zu error=%s",
             fixture_name, stats.done ? 1 : 0, stats.errored ? 1 : 0,
             result.event, probe.start_count, probe.start_commits,
             probe.start_discards, probe.cancel_prepares,
             probe.cancel_commits, probe.cancel_discards,
             (unsigned long long)invoke_stats.returned_accepted,
             (unsigned long long)invoke_stats.completed,
             invoke_stats.active, scxml_session_error(&session));

cleanup:
    if (session_initialized &&
        scxml_session_destroy(&session) != CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static bool run_w3c_invoke_materialization_fixture(
    const char *fixture_name, w3c_invoke_materialization_kind kind) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    w3c_invoke_materialization_probe probe = {0};
    w3c_result_probe result = {0};
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND,
        .prepare_send = w3c_capture_result_send,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const scxml_invoke_adapter invoke = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(invoke),
        .capabilities = SCXML_INVOKE_CAP_START |
            SCXML_INVOKE_CAP_CANCEL | SCXML_INVOKE_CAP_PAYLOAD,
        .prepare_start = w3c_capture_materialized_invoke_start,
        .prepare_cancel = w3c_accept_materialized_invoke_cancel,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const w3c_cmeta_state initial = {0};
    const scxml_cmeta_session_options_v1 data = {
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(data),
        .initial_state = &initial};
    const scxml_cmeta_compile_options_v1 compile_options =
        scxml_cmeta_default_compile_options(&w3c_cmeta_state_desc);
    scxml_session_config config = {0};
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;
    int path_size;

    if (fixture_name == NULL) return false;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) goto cleanup;
    if (scxml_compile_cmeta(
            &program, source, source_size, NULL, &compile_options,
            &diagnostic) != SCXML_OK) {
        info("fixture=%s compile diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) goto cleanup;
    executor_initialized = true;
    config = (scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 4u,
        .adapter_internal_event_capacity = 2u,
        .invocation_capacity = W3C_MATERIALIZATION_INVOKE_CAPACITY,
        .event_io = &event_io,
        .adapter_user = &result};
    config.invoke = &invoke;
    config.invoke_user = &probe;
    if (scxml_session_init_cmeta(
            &session, &config, &data) !=
        CFLOW_STATECHART_INSTANCE_OK) {
        info("fixture=%s session init error=%s", fixture_name,
             scxml_session_error(&session));
        goto cleanup;
    }
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor) ||
        !w3c_materialized_invokes_match(&probe, kind)) {
        info("fixture=%s kind=%d starts=%zu commits=%zu discards=%zu",
             fixture_name, (int)kind, probe.start_count,
             probe.start_commits, probe.start_discards);
        goto cleanup;
    }
    if (kind != W3C_INVOKE_ARGUMENT_ERROR) {
        if (kind == W3C_INVOKE_NAMED_PAYLOAD) {
            cflow_event_view event = {0};
            if (!scxml_program_event(
                    &program, "varBound", sizeof("varBound") - 1u,
                    &event) ||
                scxml_session_report_invoke_event(
                    &session, probe.starts[0].token, &event) !=
                    CFLOW_MAILBOX_OK)
                goto cleanup;
        } else if (scxml_session_report_invoke_done(
                       &session, probe.starts[0].token) !=
                   CFLOW_MAILBOX_OK) {
            goto cleanup;
        }
    }
    if (!cflow_executor_wait_idle(&executor) ||
        !scxml_session_get_stats(&session, &stats))
        goto cleanup;
    succeeded = stats.done && !stats.errored &&
        result.prepare_send_calls == 1u && result.commits == 1u &&
        result.discards == 0u && strcmp(result.event, "result.pass") == 0;
    if (!succeeded)
        info("fixture=%s done=%d errored=%d result=%s error=%s",
             fixture_name, stats.done ? 1 : 0, stats.errored ? 1 : 0,
             result.event, scxml_session_error(&session));

cleanup:
    if (session_initialized &&
        scxml_session_destroy(&session) !=
            CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static bool run_w3c_macrostep_invoke_fixture(const char *fixture_name) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    w3c_macrostep_invoke_probe probe = {0};
    w3c_result_probe result = {0};
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND,
        .prepare_send = w3c_capture_result_send,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const scxml_invoke_adapter invoke = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(invoke),
        .capabilities = SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
        .prepare_start = w3c_capture_macrostep_invoke_start,
        .prepare_cancel = w3c_accept_macrostep_invoke_cancel,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const w3c_cmeta_state initial = {0};
    const scxml_cmeta_session_options_v1 data = {
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(data),
        .initial_state = &initial};
    const scxml_cmeta_compile_options_v1 compile_options =
        scxml_cmeta_default_compile_options(&w3c_cmeta_state_desc);
    scxml_session_config config = {0};
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;
    size_t event_index;
    int path_size;

    if (fixture_name == NULL) return false;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) goto cleanup;
    if (scxml_compile_cmeta(
            &program, source, source_size, NULL, &compile_options,
            &diagnostic) != SCXML_OK) {
        info("fixture=%s compile diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) goto cleanup;
    executor_initialized = true;
    config = (scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 4u,
        .adapter_internal_event_capacity = 2u,
        .invocation_capacity = W3C_MACROSTEP_INVOKE_CAPACITY,
        .event_io = &event_io,
        .adapter_user = &result,
        .invoke = &invoke,
        .invoke_user = &probe};
    {
        const cflow_statechart_instance_status init_status =
            scxml_session_init_cmeta(&session, &config, &data);
        if (init_status != CFLOW_STATECHART_INSTANCE_OK) {
            info("fixture=%s session init status=%d error=%s", fixture_name,
                 (int)init_status, scxml_session_error(&session));
            goto cleanup;
        }
    }
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor) || probe.start_count != 2u ||
        probe.start_commits != 2u || probe.start_discards != 0u ||
        probe.starts[0].token == 0u || probe.starts[1].token == 0u ||
        strcmp(probe.starts[0].id, "invokeS1") != 0 ||
        strcmp(probe.starts[1].id, "invokeS12") != 0) {
        info("fixture=%s starts=%zu commits=%zu discards=%zu ids=%s,%s",
             fixture_name, probe.start_count, probe.start_commits,
             probe.start_discards, probe.starts[0].id, probe.starts[1].id);
        goto cleanup;
    }
    for (event_index = 0u; event_index < probe.start_count; ++event_index) {
        cflow_event_view event = {0};
        const char *event_name = event_index == 0u
            ? "invokeS1" : "invokeS12";
        const size_t event_size = strlen(event_name);
        if (!scxml_program_event(
                &program, event_name, event_size, &event) ||
            scxml_session_report_invoke_event(
                &session, probe.starts[event_index].token, &event) !=
                CFLOW_MAILBOX_OK ||
            !cflow_executor_wait_idle(&executor))
            goto cleanup;
    }
    if (!scxml_session_get_stats(&session, &stats)) goto cleanup;
    succeeded = stats.done && !stats.errored &&
        result.prepare_send_calls == 1u && result.commits == 1u &&
        result.discards == 0u && strcmp(result.event, "result.pass") == 0;
    if (!succeeded)
        info("fixture=%s done=%d errored=%d result=%s error=%s",
             fixture_name, stats.done ? 1 : 0, stats.errored ? 1 : 0,
             result.event, scxml_session_error(&session));

cleanup:
    if (session_initialized &&
        scxml_session_destroy(&session) != CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static bool run_w3c_cmeta_fixture_with_schema(
    const char *fixture_name, const w3c_cmeta_fixture_options *options,
    const cmeta_data_desc *root, const void *initial_state) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    w3c_cmeta_probe probe = {
        .routable_loopback =
            options != NULL && options->routable_loopback,
        .require_scxml_type =
            options != NULL && options->require_scxml_type,
        .require_route_target =
            options != NULL && options->require_route_target,
        .hold_loopback_delivery =
            options != NULL && options->hold_loopback_delivery,
        .loopback_limit =
            options != NULL ? options->loopback_count : 0u,
        .send_rejection = options != NULL
            ? options->send_rejection : SCXML_ADAPTER_ACCEPTED,
        .send_extension = options != NULL
            ? options->send_extension : NULL,
        .send_extension_user = options != NULL
            ? options->send_extension_user : NULL};
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND,
        .prepare_send = w3c_capture_cmeta_send,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const scxml_cmeta_session_options_v1 data = {
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(data),
        .initial_state = initial_state};
    const scxml_cmeta_session_options_v2 environment_data = {
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V2,
        .struct_size = sizeof(environment_data),
        .initial_state = initial_state,
        .environment_overrides = options != NULL
            ? options->environment_overrides : NULL,
        .environment_override_count = options != NULL
            ? options->environment_override_count : 0u};
    scxml_cmeta_compile_options_v1 compile_options =
        scxml_cmeta_default_compile_options(root);
    scxml_session_config config = {0};
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;
    bool blocker_initialized = false;
    w3c_executor_blocker blocker;
    cflow_event_view admitted_event = {0};
    int path_size;

    if (fixture_name == NULL || !cmeta_data_desc_valid(root) ||
        initial_state == NULL)
        return false;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) goto cleanup;
    if (options != NULL && options->max_iterations != 0u)
        compile_options.max_iterations = options->max_iterations;
    if (scxml_compile_cmeta(
            &program, source, source_size, NULL, &compile_options,
            &diagnostic) != SCXML_OK) {
        info("fixture=%s compile diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) goto cleanup;
    executor_initialized = true;
    config = (scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity =
            options != NULL && options->external_event_capacity != 0u
                ? options->external_event_capacity
                : W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 2u,
        .adapter_internal_event_capacity = 1u,
        .event_io = &event_io,
        .adapter_user = &probe};
    {
        const cflow_statechart_instance_status init_status =
            options != NULL && options->environment_override_count != 0u
                ? scxml_session_init_cmeta_v2(
                      &session, &config, &environment_data)
                : scxml_session_init_cmeta(&session, &config, &data);
        if (init_status != CFLOW_STATECHART_INSTANCE_OK) {
            info("fixture=%s session init status=%d error=%s", fixture_name,
                 (int)init_status, scxml_session_error(&session));
            goto cleanup;
        }
    }
    session_initialized = true;
    if (probe.routable_loopback) {
        size_t required = 0u;
        if (scxml_session_copy_location(
                &session, probe.route_origin, sizeof(probe.route_origin),
                &required) != SCXML_LOCATION_OK)
            goto cleanup;
        if (probe.require_route_target &&
            strcmp(probe.initial_route_target, probe.route_origin) != 0)
            goto cleanup;
    }
    if (options != NULL &&
        options->hold_executor_during_external_admission) {
        atomic_init(&blocker.entered, false);
        atomic_init(&blocker.release, false);
        blocker_initialized = true;
        if (cflow_executor_try_post(
                &executor, w3c_block_executor, &blocker) !=
            CFLOW_ADMISSION_ACCEPTED)
            goto cleanup;
        while (!atomic_load(&blocker.entered)) turbo_thread_yield();
    }
    if (options != NULL && options->external_event != NULL) {
        if (!w3c_admit_external_event(
                &session, &program, options->external_event))
            goto cleanup;
    }
    if (options != NULL && options->following_external_event != NULL &&
        !w3c_admit_external_event(
            &session, &program, options->following_external_event))
        goto cleanup;
    if (options != NULL && options->third_external_event != NULL &&
        !w3c_admit_external_event(
            &session, &program, options->third_external_event))
        goto cleanup;
    if (blocker_initialized) atomic_store(&blocker.release, true);
    if (!cflow_executor_wait_idle(&executor)) goto cleanup;
    while (!probe.hold_loopback_delivery &&
           probe.loopback_delivered_count < probe.loopback_limit) {
        const size_t index = probe.loopback_delivered_count;
        const size_t event_size = strlen(probe.loopback_events[index]);
        scxml_event_metadata metadata = {
            .abi_version = SCXML_EVENT_METADATA_ABI,
            .struct_size = sizeof(metadata)};
        metadata.send_id = probe.loopback_send_ids[index];
        metadata.send_id_size = strlen(probe.loopback_send_ids[index]);
        if (probe.loopback_ready_count <= index) goto cleanup;
        if (probe.routable_loopback) {
            metadata.origin = probe.route_origin;
            metadata.origin_size = strlen(probe.route_origin);
            metadata.origin_type = W3C_SCXML_EVENT_PROCESSOR;
            metadata.origin_type_size =
                sizeof(W3C_SCXML_EVENT_PROCESSOR) - 1u;
        }
        if (!scxml_program_event(
                &program, probe.loopback_events[index], event_size,
                &admitted_event) ||
            scxml_session_try_send_with_metadata(
                &session, &admitted_event, &metadata) !=
                CFLOW_MAILBOX_OK)
            goto cleanup;
        ++probe.loopback_delivered_count;
        if (!cflow_executor_wait_idle(&executor)) goto cleanup;
    }
    if (!scxml_session_get_stats(&session, &stats)) goto cleanup;
    succeeded = stats.done && !stats.errored &&
        probe.result.prepare_send_calls == 1u &&
        probe.result.commits == 1u && probe.result.discards == 0u &&
        strcmp(probe.result.event, "result.pass") == 0 &&
        probe.loopback_prepare_calls == probe.loopback_limit &&
        probe.loopback_commits == probe.loopback_limit &&
        probe.loopback_discards == 0u &&
        probe.loopback_delivered_count ==
            (probe.hold_loopback_delivery ? 0u : probe.loopback_limit) &&
        probe.loopback_ready_count == probe.loopback_limit &&
        ((probe.send_rejection == SCXML_ADAPTER_ACCEPTED &&
          probe.rejected_sends == 0u) ||
         (probe.send_rejection != SCXML_ADAPTER_ACCEPTED &&
          probe.rejected_sends == 1u)) &&
        (options == NULL ||
         !options->require_three_external_events_drained ||
         (stats.external_accepted == W3C_MACROSTEP_EXTERNAL_EVENT_CAPACITY &&
          stats.external_completed == W3C_MACROSTEP_EXTERNAL_EVENT_CAPACITY &&
          stats.external_pending == 0u &&
          stats.external_in_flight == 0u));
    if (!succeeded)
        info("fixture=%s done=%d errored=%d active=%zu leaves=%zu "
             "macrosteps=%llu microsteps=%llu actions=%llu result_sends=%zu "
             "result_commits=%zu result_discards=%zu result=%s "
             "loopback_sends=%zu loopback_commits=%zu "
             "loopback_discards=%zu delivered=%zu rejected=%zu "
             "external_accepted=%llu external_completed=%llu "
             "external_pending=%zu external_in_flight=%zu error=%s",
             fixture_name, stats.done ? 1 : 0, stats.errored ? 1 : 0,
             stats.active_state_count, stats.active_leaf_count,
             (unsigned long long)stats.macrosteps,
             (unsigned long long)stats.microsteps,
             (unsigned long long)stats.actions,
             probe.result.prepare_send_calls, probe.result.commits,
             probe.result.discards, probe.result.event,
             probe.loopback_prepare_calls, probe.loopback_commits,
             probe.loopback_discards, probe.loopback_delivered_count,
             probe.rejected_sends,
             (unsigned long long)stats.external_accepted,
             (unsigned long long)stats.external_completed,
             stats.external_pending, stats.external_in_flight,
             scxml_session_error(&session));

cleanup:
    if (blocker_initialized) atomic_store(&blocker.release, true);
    if (session_initialized &&
        scxml_session_destroy(&session) !=
            CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static bool run_w3c_cmeta_fixture_with_options(
    const char *fixture_name, const w3c_cmeta_fixture_options *options) {
    const w3c_cmeta_state initial = {0};
    return run_w3c_cmeta_fixture_with_schema(
        fixture_name, options, &w3c_cmeta_state_desc, &initial);
}

static bool run_w3c_cmeta_fixture(const char *fixture_name) {
    return run_w3c_cmeta_fixture_with_options(fixture_name, NULL);
}

typedef struct w3c_foreach_mutation_probe {
    size_t prepares;
    size_t commits;
    size_t discards;
} w3c_foreach_mutation_probe;

static void w3c_foreach_mutation_commit(void *user) {
    w3c_foreach_mutation_probe *probe =
        (w3c_foreach_mutation_probe *)user;
    if (probe != NULL) ++probe->commits;
}

static void w3c_foreach_mutation_discard(void *user) {
    w3c_foreach_mutation_probe *probe =
        (w3c_foreach_mutation_probe *)user;
    if (probe != NULL) ++probe->discards;
}

static scxml_adapter_status w3c_mutate_foreach_collection(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    static const char event[] = "mutate.collection";
    const int appended = 4;
    w3c_foreach_mutation_probe *probe =
        (w3c_foreach_mutation_probe *)user;
    w3c_foreach_state *staged =
        atomic_load(&w3c_foreach_staged_state);
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || staged == NULL || request->event == NULL ||
        request->event_size != sizeof(event) - 1u ||
        memcmp(request->event, event, sizeof(event) - 1u) != 0 ||
        probe->prepares != 0u || vec_size(&staged->values) != 3u ||
        vec_push(&staged->values, &appended) != STL_OK)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->prepares;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_foreach_mutation_commit,
        w3c_foreach_mutation_discard,
        probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static bool run_w3c_foreach_fixture_with_options(
    const char *fixture_name, const w3c_cmeta_fixture_options *options,
    size_t element_limit) {
    static const int values[] = {1, 2, 3};
    w3c_foreach_state initial = {
        .values = VecOf(int),
        .valid = 1};
    bool succeeded = false;
    size_t index;

    if (fixture_name == NULL ||
        element_limit < sizeof(values) / sizeof(values[0]) ||
        vec_init(&initial.values, element_limit) != STL_OK)
        return false;
    for (index = 0u; index < sizeof(values) / sizeof(values[0]); ++index) {
        if (vec_push(&initial.values, &values[index]) != STL_OK)
            goto cleanup;
    }
    succeeded = run_w3c_cmeta_fixture_with_schema(
        fixture_name, options, &w3c_foreach_state_desc, &initial);

cleanup:
    vec_destroy(&initial.values);
    return succeeded;
}

static bool run_w3c_foreach_fixture(const char *fixture_name) {
    return run_w3c_foreach_fixture_with_options(
        fixture_name, NULL, 3u);
}

static bool run_w3c_foreach_snapshot_fixture(const char *fixture_name) {
    w3c_foreach_mutation_probe mutation = {0};
    const w3c_cmeta_fixture_options options = {
        .send_extension = w3c_mutate_foreach_collection,
        .send_extension_user = &mutation};
    bool succeeded;

    atomic_store(&w3c_foreach_staged_state, NULL);
    succeeded = run_w3c_foreach_fixture_with_options(
        fixture_name, &options, 4u);
    atomic_store(&w3c_foreach_staged_state, NULL);
    return succeeded && mutation.prepares == 1u &&
           mutation.commits == 1u && mutation.discards == 0u;
}

static bool run_w3c_invalid_foreach_fixture(const char *fixture_name) {
    const w3c_cmeta_fixture_options options = {
        .max_iterations = 2u};
    return run_w3c_foreach_fixture_with_options(
        fixture_name, &options, 3u);
}

static bool run_w3c_cmeta_external_fixture(
    const char *fixture_name, const char *external_event) {
    const w3c_cmeta_fixture_options options = {
        .external_event = external_event};
    return run_w3c_cmeta_fixture_with_options(fixture_name, &options);
}

static bool run_w3c_cmeta_ordered_external_fixture(
    const char *fixture_name, const char *first_event,
    const char *second_event) {
    const w3c_cmeta_fixture_options options = {
        .external_event = first_event,
        .following_external_event = second_event,
        .hold_executor_during_external_admission = true};
    return run_w3c_cmeta_fixture_with_options(fixture_name, &options);
}

static bool run_w3c_cmeta_three_external_fixture(
    const char *fixture_name, const char *first_event,
    const char *second_event, const char *third_event) {
    const w3c_cmeta_fixture_options options = {
        .external_event = first_event,
        .following_external_event = second_event,
        .third_external_event = third_event,
        .external_event_capacity = W3C_MACROSTEP_EXTERNAL_EVENT_CAPACITY,
        .hold_executor_during_external_admission = true,
        .require_three_external_events_drained = true};
    return run_w3c_cmeta_fixture_with_options(fixture_name, &options);
}

static bool run_w3c_cmeta_loopback_fixture(const char *fixture_name) {
    const w3c_cmeta_fixture_options options = {.loopback_count = 1u};
    return run_w3c_cmeta_fixture_with_options(fixture_name, &options);
}

static bool run_w3c_cmeta_held_loopback_fixture(
    const char *fixture_name) {
    const w3c_cmeta_fixture_options options = {
        .loopback_count = 1u,
        .hold_loopback_delivery = true};
    return run_w3c_cmeta_fixture_with_options(fixture_name, &options);
}

static bool run_w3c_cmeta_double_loopback_fixture(
    const char *fixture_name) {
    const w3c_cmeta_fixture_options options = {.loopback_count = 2u};
    return run_w3c_cmeta_fixture_with_options(fixture_name, &options);
}

static bool run_w3c_cmeta_scxml_loopback_fixture(
    const char *fixture_name) {
    const w3c_cmeta_fixture_options options = {
        .loopback_count = 1u,
        .routable_loopback = true,
        .require_scxml_type = true};
    return run_w3c_cmeta_fixture_with_options(fixture_name, &options);
}

static bool run_w3c_cmeta_routable_loopback_fixture(
    const char *fixture_name) {
    const w3c_cmeta_fixture_options options = {
        .loopback_count = 2u,
        .routable_loopback = true};
    return run_w3c_cmeta_fixture_with_options(fixture_name, &options);
}

static bool run_w3c_cmeta_location_loopback_fixture(
    const char *fixture_name) {
    const w3c_cmeta_fixture_options options = {
        .loopback_count = 1u,
        .routable_loopback = true,
        .require_route_target = true};
    return run_w3c_cmeta_fixture_with_options(fixture_name, &options);
}

static bool run_w3c_cmeta_rejected_send_fixture(const char *fixture_name) {
    const w3c_cmeta_fixture_options options = {
        .send_rejection = SCXML_ADAPTER_ERROR_COMMUNICATION};
    return run_w3c_cmeta_fixture_with_options(fixture_name, &options);
}

static cflow_statechart_host_result w3c_observe_event(
    void *user, cflow_statechart_host_context *context,
    const char **out_error) {
    w3c_event_probe *probe = (w3c_event_probe *)user;
    const cflow_statechart_observed_event *event;
    if (probe == NULL || context == NULL || out_error == NULL)
        return CFLOW_STATECHART_HOST_FATAL;
    *out_error = NULL;
    if (cflow_statechart_host_context_phase(context) !=
        CFLOW_STATECHART_HOST_PREPARE_TRIGGER)
        return CFLOW_STATECHART_HOST_CONTINUE;
    event = cflow_statechart_host_context_trigger(context);
    if (event == NULL) return CFLOW_STATECHART_HOST_FATAL;
    if (event->kind == CFLOW_STATECHART_OBSERVED_INTERNAL &&
        event->event != NULL &&
        event->event->id == probe->expected_internal)
        ++probe->selected_internal;
    return CFLOW_STATECHART_HOST_CONTINUE;
}

static bool run_w3c_top_level_final_fixture(const char *fixture_name) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    const cflow_statechart_executable_binding *executables = NULL;
    const cflow_statechart_guard_binding *guards = NULL;
    size_t executable_count = 0u;
    size_t guard_count = 0u;
    cflow_executor executor = {0};
    cflow_statechart_instance instance = {0};
    cflow_statechart_instance_stats stats = {0};
    w3c_event_probe probe = {0};
    const cflow_statechart_instance_hooks hooks = {
        .abi_version = CFLOW_STATECHART_INSTANCE_HOOKS_ABI_V4,
        .struct_size = sizeof(cflow_statechart_instance_hooks),
        .on_host_transaction = w3c_observe_event};
    cflow_statechart_instance_config config = {0};
    bool executor_initialized = false;
    bool instance_initialized = false;
    bool succeeded = false;
    int path_size;

    if (fixture_name == NULL) return false;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) goto cleanup;
    if (scxml_compile(
            &program, source, source_size, NULL, &diagnostic) != SCXML_OK ||
        !scxml_program_event_id(
            &program, "event1", sizeof("event1") - 1u,
            &probe.expected_internal) ||
        !scxml_program_instance_bindings(
            &program, &executables, &executable_count) ||
        !scxml_program_guard_bindings(&program, &guards, &guard_count)) {
        info("fixture=%s compile or binding diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) goto cleanup;
    executor_initialized = true;
    config = (cflow_statechart_instance_config){
        .statechart = scxml_program_statechart(&program),
        .initial_state = scxml_program_initial_state(&program),
        .guards = guards,
        .guard_count = guard_count,
        .executables = executables,
        .executable_count = executable_count,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .executor = &executor,
        .hooks = &hooks,
        .hook_user = &probe};
    if (cflow_statechart_instance_init(&instance, &config) !=
        CFLOW_STATECHART_INSTANCE_OK)
        goto cleanup;
    instance_initialized = true;
    if (!cflow_executor_wait_idle(&executor) ||
        !cflow_statechart_instance_get_stats(&instance, &stats))
        goto cleanup;
    succeeded = stats.done && !stats.errored && stats.actions == 1u &&
        stats.internal_pending == 0u && probe.selected_internal == 0u;
    if (!succeeded)
        info("fixture=%s done=%d errored=%d actions=%llu pending=%zu "
             "selected=%zu",
             fixture_name, stats.done ? 1 : 0, stats.errored ? 1 : 0,
             (unsigned long long)stats.actions, stats.internal_pending,
             probe.selected_internal);

cleanup:
    if (instance_initialized)
        (void)cflow_statechart_instance_destroy(&instance);
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static bool run_w3c_adapter_error_fixture(
    const char *fixture_name, scxml_adapter_status send_status,
    const char *expected_target, const char *expected_type,
    cflow_statechart_instance_stats *out_stats,
    size_t *out_prepare_send_calls) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    scxml_session session = {0};
    w3c_adapter_probe probe = {
        .send_status = send_status,
        .expected_target = expected_target,
        .expected_type = expected_type};
    scxml_event_io_adapter adapter = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(scxml_event_io_adapter),
        .capabilities = SCXML_EVENT_IO_CAP_SEND,
        .prepare_send = w3c_reject_send,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    scxml_session_config config = {0};
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;
    int path_size;

    if (fixture_name == NULL || out_stats == NULL ||
        out_prepare_send_calls == NULL) {
        return false;
    }
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    memset(out_stats, 0, sizeof(*out_stats));
    *out_prepare_send_calls = 0u;
    source = tt_read_file(path, &source_size);
    if (source == NULL) {
        info("fixture=%s read failed", fixture_name);
        goto cleanup;
    }
    if (scxml_compile(
            &program, source, source_size, NULL, &diagnostic) !=
        SCXML_OK) {
        info("fixture=%s compile diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) {
        info("fixture=%s executor initialization failed", fixture_name);
        goto cleanup;
    }
    executor_initialized = true;
    config = (scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 1u,
        .adapter_internal_event_capacity = 1u,
        .event_io = &adapter,
        .adapter_user = &probe};
    if (scxml_session_init(&session, &config) !=
        CFLOW_STATECHART_INSTANCE_OK) {
        info("fixture=%s session initialization failed", fixture_name);
        goto cleanup;
    }
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor) ||
        !scxml_session_get_stats(&session, out_stats)) {
        info("fixture=%s executor wait or stats failed", fixture_name);
        goto cleanup;
    }
    *out_prepare_send_calls = probe.prepare_send_calls;
    succeeded = true;

cleanup:
    if (session_initialized) {
        if (!out_stats->done) {
            scxml_session_cancel(&session);
            (void)cflow_executor_wait_idle(&executor);
        }
        if (scxml_session_destroy(&session) !=
            CFLOW_STATECHART_INSTANCE_OK) {
            succeeded = false;
        }
    }
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static void check_w3c_adapter_error_fixture(const char *fixture_name) {
    cflow_statechart_instance_stats stats = {0};
    size_t prepare_send_calls = 0u;

    check_true(run_w3c_adapter_error_fixture(
        fixture_name, SCXML_ADAPTER_ERROR_EXECUTION, "peer",
        W3C_SCXML_EVENT_PROCESSOR, &stats, &prepare_send_calls));
    check_true(stats.done);
    check_false(stats.errored);
    check_equal(prepare_send_calls, (size_t)1u);
}

static void check_w3c_send_error_fixture(
    const char *fixture_name, scxml_adapter_status send_status,
    const char *expected_target, const char *expected_type) {
    cflow_statechart_instance_stats stats = {0};
    size_t prepare_send_calls = 0u;

    check_true(run_w3c_adapter_error_fixture(
        fixture_name, send_status, expected_target, expected_type,
        &stats, &prepare_send_calls));
    check_true(stats.done);
    check_false(stats.errored);
    check_equal(prepare_send_calls, (size_t)1u);
}

static bool w3c_delayed_message_is(
    const w3c_delayed_message *message, const char *event,
    const char *id, uint64_t delay_ms) {
    return message != NULL && event != NULL &&
        strcmp(message->event, event) == 0 &&
        (id == NULL || strcmp(message->id, id) == 0) &&
        message->delay_ms == delay_ms;
}

static bool run_w3c_delayed_fixture(
    const char *fixture_name, w3c_delayed_fixture_kind kind) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    w3c_delayed_probe probe = {0};
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND |
            SCXML_EVENT_IO_CAP_DELAYED_SEND | SCXML_EVENT_IO_CAP_CANCEL,
        .prepare_send = w3c_capture_delayed_send,
        .prepare_cancel = w3c_capture_delayed_cancel,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const w3c_cmeta_state initial = {0};
    const scxml_cmeta_session_options_v1 data = {
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(data),
        .initial_state = &initial};
    const scxml_cmeta_compile_options_v1 compile_options =
        scxml_cmeta_default_compile_options(&w3c_cmeta_state_desc);
    scxml_session_config config = {0};
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;
    int path_size;

    if (fixture_name == NULL) return false;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) {
        info("fixture=%s read failed", fixture_name);
        goto cleanup;
    }
    if (kind == W3C_DYNAMIC_CANCEL || kind == W3C_DYNAMIC_DELAY) {
        if (scxml_compile_cmeta(
                &program, source, source_size, NULL, &compile_options,
                &diagnostic) != SCXML_OK) {
            info("fixture=%s compile diagnostic=%s", fixture_name,
                 diagnostic.message);
            goto cleanup;
        }
    } else if (scxml_compile(
                   &program, source, source_size, NULL, &diagnostic) !=
               SCXML_OK) {
        info("fixture=%s compile diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) goto cleanup;
    executor_initialized = true;
    config = (scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 3u,
        .adapter_internal_event_capacity = 2u,
        .delayed_send_capacity = W3C_DELAYED_MESSAGE_CAPACITY,
        .event_io = &event_io,
        .adapter_user = &probe};
    {
        const cflow_statechart_instance_status init_status =
            (kind == W3C_DYNAMIC_CANCEL || kind == W3C_DYNAMIC_DELAY)
                ? scxml_session_init_cmeta(&session, &config, &data)
                : scxml_session_init(&session, &config);
        if (init_status != CFLOW_STATECHART_INSTANCE_OK) {
            info("fixture=%s session init status=%d", fixture_name,
                 (int)init_status);
            goto cleanup;
        }
    }
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor) || probe.send_count != 2u ||
        probe.discards != 0u)
        goto cleanup;
    if (kind == W3C_DELAY_ORDER) {
        if (probe.cancel_count != 0u || probe.commits != 2u ||
            !w3c_delayed_message_is(
                &probe.messages[0], "event2", "later", UINT64_C(1000)) ||
            !w3c_delayed_message_is(
                &probe.messages[1], "event1", NULL, UINT64_C(0)) ||
            !w3c_admit_external_event(&session, &program, "event1") ||
            !cflow_executor_wait_idle(&executor) ||
            !w3c_admit_external_event(&session, &program, "event2") ||
            !cflow_executor_wait_idle(&executor) ||
            !scxml_session_report_send_done(
                &session, probe.messages[0].id,
                strlen(probe.messages[0].id)))
            goto cleanup;
    } else if (kind == W3C_DYNAMIC_DELAY) {
        if (probe.cancel_count != 0u || probe.commits != 2u ||
            !w3c_delayed_message_is(
                &probe.messages[0], "event2", "later", UINT64_C(1000)) ||
            !w3c_delayed_message_is(
                &probe.messages[1], "event1", "sooner", UINT64_C(500)) ||
            !w3c_admit_external_event(&session, &program, "event1") ||
            !cflow_executor_wait_idle(&executor) ||
            !scxml_session_report_send_done(
                &session, probe.messages[1].id,
                strlen(probe.messages[1].id)) ||
            !w3c_admit_external_event(&session, &program, "event2") ||
            !cflow_executor_wait_idle(&executor) ||
            !scxml_session_report_send_done(
                &session, probe.messages[0].id,
                strlen(probe.messages[0].id)))
            goto cleanup;
    } else {
        const char *expected_first_id = kind == W3C_LITERAL_CANCEL
            ? "foo" : probe.messages[0].id;
        if (probe.cancel_count != 1u || probe.commits != 3u ||
            probe.messages[0].id[0] == '\0' ||
            !w3c_delayed_message_is(
                &probe.messages[0], "event1", expected_first_id,
                UINT64_C(1000)) ||
            !w3c_delayed_message_is(
                &probe.messages[1], "event2", "bar", UINT64_C(1500)) ||
            strcmp(probe.cancel_id, probe.messages[0].id) != 0 ||
            scxml_session_report_send_done(
                &session, probe.messages[0].id,
                strlen(probe.messages[0].id)) ||
            !w3c_admit_external_event(&session, &program, "event2") ||
            !cflow_executor_wait_idle(&executor) ||
            !scxml_session_report_send_done(
                &session, probe.messages[1].id,
                strlen(probe.messages[1].id)))
            goto cleanup;
    }
    if (!scxml_session_get_stats(&session, &stats)) goto cleanup;
    succeeded = stats.done && !stats.errored;

cleanup:
    if (session_initialized &&
        scxml_session_destroy(&session) != CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static bool run_w3c_cancel_isolation_fixture(const char *fixture_name) {
    static const char delayed_id[] = "foo";
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor victim_executor = {0};
    cflow_executor attacker_executor = {0};
    scxml_session victim = {0};
    scxml_session attacker = {0};
    cflow_statechart_instance_stats victim_stats = {0};
    cflow_statechart_instance_stats attacker_stats = {0};
    w3c_cancel_isolation_probe victim_probe = {0};
    w3c_cancel_isolation_probe attacker_probe = {0};
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND |
            SCXML_EVENT_IO_CAP_DELAYED_SEND | SCXML_EVENT_IO_CAP_CANCEL,
        .prepare_send = w3c_capture_isolated_send,
        .prepare_cancel = w3c_capture_isolated_cancel,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const w3c_cmeta_state victim_initial = {.sequence = 1};
    const w3c_cmeta_state attacker_initial = {0};
    const scxml_cmeta_session_options_v1 victim_data = {
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(victim_data),
        .initial_state = &victim_initial};
    const scxml_cmeta_session_options_v1 attacker_data = {
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(attacker_data),
        .initial_state = &attacker_initial};
    const scxml_cmeta_compile_options_v1 compile_options =
        scxml_cmeta_default_compile_options(&w3c_cmeta_state_desc);
    scxml_session_config victim_config = {0};
    scxml_session_config attacker_config = {0};
    bool victim_executor_initialized = false;
    bool attacker_executor_initialized = false;
    bool victim_initialized = false;
    bool attacker_initialized = false;
    bool succeeded = false;
    int path_size;

    if (fixture_name == NULL) return false;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) goto cleanup;
    if (scxml_compile_cmeta(
            &program, source, source_size, NULL, &compile_options,
            &diagnostic) != SCXML_OK) {
        info("fixture=%s compile diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&victim_executor)) goto cleanup;
    victim_executor_initialized = true;
    if (!cflow_executor_serial_init(&attacker_executor)) goto cleanup;
    attacker_executor_initialized = true;
    victim_config = (scxml_session_config){
        .program = &program,
        .executor = &victim_executor,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 2u,
        .adapter_internal_event_capacity = 1u,
        .delayed_send_capacity = 1u,
        .event_io = &event_io,
        .adapter_user = &victim_probe};
    attacker_config = victim_config;
    attacker_config.executor = &attacker_executor;
    attacker_config.adapter_user = &attacker_probe;
    if (scxml_session_init_cmeta(
            &victim, &victim_config, &victim_data) !=
        CFLOW_STATECHART_INSTANCE_OK)
        goto cleanup;
    victim_initialized = true;
    if (!cflow_executor_wait_idle(&victim_executor) ||
        victim_probe.delayed.send_count != 1u ||
        victim_probe.delayed.commits != 1u ||
        victim_probe.delayed.discards != 0u ||
        !w3c_delayed_message_is(
            &victim_probe.delayed.messages[0], "event1", delayed_id,
            UINT64_C(1000)))
        goto cleanup;
    if (scxml_session_init_cmeta(
            &attacker, &attacker_config, &attacker_data) !=
        CFLOW_STATECHART_INSTANCE_OK)
        goto cleanup;
    attacker_initialized = true;
    if (!cflow_executor_wait_idle(&attacker_executor) ||
        !scxml_session_get_stats(&attacker, &attacker_stats) ||
        !attacker_stats.done || attacker_stats.errored ||
        attacker_probe.cancel_prepare_calls != 0u ||
        attacker_probe.delayed.send_count != 0u ||
        attacker_probe.result.prepare_send_calls != 1u ||
        attacker_probe.result.commits != 1u ||
        attacker_probe.result.discards != 0u ||
        strcmp(attacker_probe.result.event, "result.pass") != 0 ||
        scxml_session_report_send_done(
            &attacker, delayed_id, sizeof(delayed_id) - 1u) ||
        !scxml_session_report_send_done(
            &victim, delayed_id, sizeof(delayed_id) - 1u) ||
        scxml_session_report_send_done(
            &victim, delayed_id, sizeof(delayed_id) - 1u) ||
        !w3c_admit_external_event(&victim, &program, "event1") ||
        !cflow_executor_wait_idle(&victim_executor) ||
        !scxml_session_get_stats(&victim, &victim_stats))
        goto cleanup;
    succeeded = victim_stats.done && !victim_stats.errored &&
        victim_probe.cancel_prepare_calls == 0u &&
        victim_probe.result.prepare_send_calls == 1u &&
        victim_probe.result.commits == 1u &&
        victim_probe.result.discards == 0u &&
        strcmp(victim_probe.result.event, "result.pass") == 0;

cleanup:
    if (attacker_initialized &&
        scxml_session_destroy(&attacker) !=
            CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (victim_initialized &&
        scxml_session_destroy(&victim) !=
            CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (attacker_executor_initialized)
        cflow_executor_destroy(&attacker_executor);
    if (victim_executor_initialized)
        cflow_executor_destroy(&victim_executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static bool run_w3c_frozen_payload_fixture(const char *fixture_name) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    w3c_delayed_probe probe = {0};
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND |
            SCXML_EVENT_IO_CAP_DELAYED_SEND | SCXML_EVENT_IO_CAP_PAYLOAD,
        .prepare_send = w3c_capture_delayed_payload_send,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const w3c_cmeta_state initial = {.sequence = 1};
    const scxml_cmeta_session_options_v1 data = {
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(data),
        .initial_state = &initial};
    const scxml_cmeta_compile_options_v1 compile_options =
        scxml_cmeta_default_compile_options(&w3c_cmeta_state_desc);
    scxml_session_config config = {0};
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;
    int path_size;

    if (fixture_name == NULL) return false;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) {
        info("fixture=%s read failed", fixture_name);
        goto cleanup;
    }
    if (scxml_compile_cmeta(
            &program, source, source_size, NULL, &compile_options,
            &diagnostic) != SCXML_OK) {
        info("fixture=%s compile diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) goto cleanup;
    executor_initialized = true;
    config = (scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 2u,
        .adapter_internal_event_capacity = 1u,
        .delayed_send_capacity = 1u};
    config.event_io = &event_io;
    config.adapter_user = &probe;
    if (scxml_session_init_cmeta(
            &session, &config, &data) !=
        CFLOW_STATECHART_INSTANCE_OK)
        goto cleanup;
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor) ||
        !scxml_session_get_stats(&session, &stats) ||
        probe.send_count != 1u || probe.commits != 1u ||
        probe.discards != 0u || !probe.payload_seen ||
        probe.payload_sint != INT64_C(1) ||
        !w3c_delayed_message_is(
            &probe.messages[0], "event1", "frozen", UINT64_C(1000)) ||
        !scxml_session_report_send_done(
            &session, probe.messages[0].id,
            strlen(probe.messages[0].id)))
        goto cleanup;
    succeeded = stats.done && !stats.errored;

cleanup:
    if (session_initialized &&
        scxml_session_destroy(&session) != CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static bool run_w3c_named_payload_fixture(
    const char *fixture_name, const char *event,
    const char *first_name, int64_t first_value,
    const char *second_name, int64_t second_value) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    w3c_named_payload_probe probe = {
        .expected_event = event,
        .expected_names = {first_name, second_name},
        .expected_values = {first_value, second_value},
        .expected_count = second_name != NULL ? 2u : 1u};
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND |
            SCXML_EVENT_IO_CAP_PAYLOAD,
        .prepare_send = w3c_capture_named_payload_send,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const w3c_cmeta_state initial = {.sequence = 1};
    const scxml_cmeta_session_options_v1 data = {
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(data),
        .initial_state = &initial};
    const scxml_cmeta_compile_options_v1 compile_options =
        scxml_cmeta_default_compile_options(&w3c_cmeta_state_desc);
    scxml_session_config config = {0};
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;
    int path_size;

    if (fixture_name == NULL || event == NULL || first_name == NULL)
        return false;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) goto cleanup;
    if (scxml_compile_cmeta(
            &program, source, source_size, NULL, &compile_options,
            &diagnostic) != SCXML_OK) {
        info("fixture=%s compile diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) goto cleanup;
    executor_initialized = true;
    config = (scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 1u,
        .adapter_internal_event_capacity = 1u};
    config.event_io = &event_io;
    config.adapter_user = &probe;
    if (scxml_session_init_cmeta(
            &session, &config, &data) !=
        CFLOW_STATECHART_INSTANCE_OK)
        goto cleanup;
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor) || probe.sends != 1u ||
        probe.commits != 1u || probe.discards != 0u ||
        !w3c_admit_external_event(&session, &program, event) ||
        !cflow_executor_wait_idle(&executor) ||
        !scxml_session_get_stats(&session, &stats))
        goto cleanup;
    succeeded = stats.done && !stats.errored;

cleanup:
    if (session_initialized &&
        scxml_session_destroy(&session) != CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static bool run_w3c_terminating_send_fixture(const char *fixture_name) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    w3c_termination_probe probe = {0};
    const scxml_event_io_adapter event_io = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(event_io),
        .capabilities = SCXML_EVENT_IO_CAP_SEND |
            SCXML_EVENT_IO_CAP_DELAYED_SEND,
        .prepare_send = w3c_capture_terminating_send,
        .close = w3c_termination_close,
        .is_quiescent = w3c_termination_is_quiescent};
    scxml_session_config config = {0};
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;
    int path_size;

    if (fixture_name == NULL) return false;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) goto cleanup;
    if (scxml_compile(
            &program, source, source_size, NULL, &diagnostic) != SCXML_OK) {
        info("fixture=%s compile diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) goto cleanup;
    executor_initialized = true;
    config = (scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 1u,
        .adapter_internal_event_capacity = 1u,
        .delayed_send_capacity = 1u,
        .event_io = &event_io,
        .adapter_user = &probe};
    if (scxml_session_init(&session, &config) !=
        CFLOW_STATECHART_INSTANCE_OK)
        goto cleanup;
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor) ||
        !scxml_session_get_stats(&session, &stats) ||
        !stats.done || stats.errored || probe.sends != 1u ||
        probe.commits != 1u || probe.discards != 0u || !probe.pending)
        goto cleanup;
    if (scxml_session_destroy(&session) !=
        CFLOW_STATECHART_INSTANCE_OK)
        goto cleanup;
    session_initialized = false;
    succeeded = probe.closes == 1u && probe.cancellations == 1u &&
        !probe.pending && !w3c_termination_try_deliver(&probe) &&
        probe.deliveries == 0u;

cleanup:
    if (session_initialized &&
        scxml_session_destroy(&session) != CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

suite("SCXML W3C-derived conformance regression corpus") {
    it("validates the complete strict upstream inventory") {
        w3c_manifest_stats stats = {0};
        check_true(validate_w3c_manifest(&stats));
        check_equal(stats.rows,
                    (size_t)W3C_UPSTREAM_TEST_DOCUMENT_COUNT);
        check_equal(stats.mandatory,
                    (size_t)W3C_UPSTREAM_MANDATORY_DOCUMENT_COUNT);
        check_equal(stats.optional,
                    (size_t)W3C_UPSTREAM_OPTIONAL_DOCUMENT_COUNT);
        check_equal(stats.passed, (size_t)W3C_PASS_DOCUMENT_COUNT);
        check_equal(stats.unsupported,
                    (size_t)W3C_UNSUPPORTED_DOCUMENT_COUNT);
        check_equal(stats.not_applicable,
                    (size_t)W3C_UPSTREAM_OPTIONAL_DOCUMENT_COUNT);
    }

    it("rejects duplicate inventory IDs") {
        char source[] =
            "id\tfixture\tapplicability\tstatus\tfeature\tupstream\t"
            "expected\ttransformation\trationale\n"
            "144\ttest144.scxml\tMANDATORY\tUNSUPPORTED\traise\t"
            W3C_UPSTREAM_PREFIX "144/test144.txml\tNOT_RUN\tNONE\tone\n"
            "144\ttest144.scxml\tMANDATORY\tUNSUPPORTED\traise\t"
            W3C_UPSTREAM_PREFIX "144/test144.txml\tNOT_RUN\tNONE\ttwo\n";
        w3c_manifest_stats stats = {0};
        check_false(validate_w3c_manifest_source(
            source, SCXML_W3C_FIXTURE_DIR, false, NULL, &stats));
    }

    it("rejects PASS rows whose local fixture is missing") {
        char source[] =
            "id\tfixture\tapplicability\tstatus\tfeature\tupstream\t"
            "expected\ttransformation\trationale\n"
            "999999\ttest999999.scxml\tMANDATORY\tPASS\tstate\t"
            W3C_UPSTREAM_PREFIX
            "999999/test999999.txml\tTERMINAL_PASS\tlocal rewrite\twitness\n";
        w3c_manifest_stats stats = {0};
        check_false(validate_w3c_manifest_source(
            source, SCXML_W3C_FIXTURE_DIR, true, NULL, &stats));
    }

    it("accepts a documented PASS witnessed by compile rejection") {
        char source[] =
            "id\tfixture\tapplicability\tstatus\tfeature\tupstream\t"
            "expected\ttransformation\trationale\n"
            "313\ttest313.scxml\tMANDATORY\tPASS\tExpressions\t"
            W3C_UPSTREAM_PREFIX
            "313/test313.txml\tCOMPILE_REJECT\tlocal illegal expression\t"
            "load rejection is the selected upstream-permitted outcome\n";
        static const char documentation[] =
            "test313.scxml " W3C_UPSTREAM_PREFIX "313/test313.txml";
        w3c_manifest_stats stats = {0};

        check_true(validate_w3c_manifest_source(
            source, SCXML_W3C_FIXTURE_DIR, false, documentation, &stats));
        check_equal(stats.passed, (size_t)1u);
        check_equal(stats.unsupported, (size_t)0u);
    }

    it("rejects malformed inventory rows") {
        char source[] =
            "id\tfixture\tapplicability\tstatus\tfeature\tupstream\t"
            "expected\ttransformation\trationale\n"
            "144\ttest144.scxml\tMANDATORY\tUNSUPPORTED\traise\t"
            W3C_UPSTREAM_PREFIX "144/test144.txml\tNOT_RUN\tNONE\n";
        w3c_manifest_stats stats = {0};
        check_false(validate_w3c_manifest_source(
            source, SCXML_W3C_FIXTURE_DIR, false, NULL, &stats));
    }

    it("rejects inventory sources outside the documented W3C origin") {
        char source[] =
            "id\tfixture\tapplicability\tstatus\tfeature\tupstream\t"
            "expected\ttransformation\trationale\n"
            "144\ttest144.scxml\tMANDATORY\tUNSUPPORTED\traise\t"
            "https://example.invalid/144/test144.txml\tNOT_RUN\tNONE\t"
            "undocumented\n";
        w3c_manifest_stats stats = {0};
        check_false(validate_w3c_manifest_source(
            source, SCXML_W3C_FIXTURE_DIR, false, NULL, &stats));
    }

    it("rejects PASS rows missing synchronized provenance documentation") {
        char source[] =
            "id\tfixture\tapplicability\tstatus\tfeature\tupstream\t"
            "expected\ttransformation\trationale\n"
            "144\ttest144.scxml\tMANDATORY\tPASS\traise\t"
            W3C_UPSTREAM_PREFIX
            "144/test144.txml\tTERMINAL_PASS\tlocal rewrite\twitness\n";
        w3c_manifest_stats stats = {0};
        check_false(validate_w3c_manifest_source(
            source, SCXML_W3C_FIXTURE_DIR, false,
            "test144.scxml without its upstream URL", &stats));
    }

    it("test 144 preserves raised internal event order") {
        check_w3c_fixture("test144.scxml");
    }

    it("test 147 executes only the first true conditional partition") {
        check_w3c_fixture("test147.scxml");
    }

    it("test 148 executes else when every condition is false") {
        check_w3c_fixture("test148.scxml");
    }

    it("test 149 skips conditional content without a matching partition") {
        check_w3c_fixture("test149.scxml");
    }

    it("test 158 executes one content block in document order") {
        check_w3c_fixture("test158.scxml");
    }

    it("test 159 aborts the remainder of a failing content block") {
        check_w3c_adapter_error_fixture("test159.scxml");
    }

    it("test 153 assigns foreach items from first to last") {
        check_true(run_w3c_foreach_fixture("test153.scxml"));
    }

    it("test 152 aborts foreach blocks for invalid arrays and item locations") {
        check_true(run_w3c_invalid_foreach_fixture("test152.scxml"));
    }

    it("test 155 executes foreach content after each item assignment") {
        check_true(run_w3c_foreach_fixture("test155.scxml"));
    }

    it("test 156 stops foreach and its content block after a child error") {
        check_true(run_w3c_foreach_fixture("test156.scxml"));
    }

    it("test 525 isolates foreach iteration from source mutations") {
        check_true(run_w3c_foreach_snapshot_fixture("test525.scxml"));
    }

    it("test 179 delivers evaluated content bytes without alteration") {
        check_true(run_w3c_content_fixture("test179.scxml", "123"));
    }

    it("test 172 evaluates eventexpr when send executes") {
        check_true(run_w3c_cmeta_loopback_fixture("test172.scxml"));
    }

    it("test 173 evaluates targetexpr when send executes") {
        check_true(run_w3c_cmeta_fixture("test173.scxml"));
    }

    it("test 174 evaluates typeexpr when send executes") {
        check_true(run_w3c_cmeta_scxml_loopback_fixture("test174.scxml"));
    }

    it("test 175 evaluates delayexpr when send executes") {
        check_true(run_w3c_delayed_fixture(
            "test175.scxml", W3C_DYNAMIC_DELAY));
    }

    it("test 176 evaluates param when send executes") {
        check_true(run_w3c_named_payload_fixture(
            "test176.scxml", "event1", "aParam", INT64_C(2),
            NULL, INT64_C(0)));
    }

    it("test 178 preserves duplicate send payload names") {
        check_true(run_w3c_named_payload_fixture(
            "test178.scxml", "event1", "duplicate", INT64_C(2),
            "duplicate", INT64_C(3)));
    }

    it("test 183 stores the generated send ID in idlocation") {
        check_true(run_w3c_cmeta_loopback_fixture("test183.scxml"));
    }

    it("test 185 preserves delayed send ordering") {
        check_true(run_w3c_delayed_fixture(
            "test185.scxml", W3C_DELAY_ORDER));
    }

    it("test 186 freezes send arguments before delayed dispatch") {
        check_true(run_w3c_frozen_payload_fixture("test186.scxml"));
    }

    it("test 187 discards delayed sends when their session terminates") {
        check_true(run_w3c_terminating_send_fixture("test187.scxml"));
    }

    it("test 205 preserves the sent Event and payload") {
        check_true(run_w3c_named_payload_fixture(
            "test205.scxml", "event1", "aParam", INT64_C(1),
            NULL, INT64_C(0)));
    }

    it("test 207 cannot cancel a delayed send owned by another session") {
        check_true(run_w3c_cancel_isolation_fixture("test207.scxml"));
    }

    it("test 208 cancels a delayed send from the same session") {
        check_true(run_w3c_delayed_fixture(
            "test208.scxml", W3C_LITERAL_CANCEL));
    }

    it("test 210 evaluates sendidexpr when cancel executes") {
        check_true(run_w3c_delayed_fixture(
            "test210.scxml", W3C_DYNAMIC_CANCEL));
    }

    it("test 198 defaults send to the SCXML Event Processor") {
        check_true(run_w3c_cmeta_scxml_loopback_fixture("test198.scxml"));
    }

    it("test 200 supports the explicit SCXML Event Processor type") {
        check_true(run_w3c_cmeta_scxml_loopback_fixture("test200.scxml"));
    }

    it("test 194 maps an unsupported send target to error.execution") {
        check_w3c_send_error_fixture(
            "test194.scxml", SCXML_ADAPTER_ERROR_EXECUTION,
            "#_invalid", W3C_SCXML_EVENT_PROCESSOR);
    }

    it("test 199 maps an unsupported send type to error.execution") {
        check_w3c_send_error_fixture(
            "test199.scxml", SCXML_ADAPTER_ERROR_EXECUTION,
            NULL, "urn:unsupported:event-processor");
    }

    it("test 521 maps an unreachable target to error.communication") {
        check_w3c_send_error_fixture(
            "test521.scxml", SCXML_ADAPTER_ERROR_COMMUNICATION,
            "#_scxml_missing", W3C_SCXML_EVENT_PROCESSOR);
    }

    it("test 348 maps send event to the received Event name") {
        check_true(run_w3c_cmeta_scxml_loopback_fixture("test348.scxml"));
    }

    it("test 349 exposes a routable sender origin") {
        check_true(run_w3c_cmeta_routable_loopback_fixture("test349.scxml"));
    }

    it("test 352 maps the SCXML source type to Event origintype") {
        check_true(run_w3c_cmeta_scxml_loopback_fixture("test352.scxml"));
    }

    it("test 496 maps an inaccessible session to error.communication") {
        check_w3c_send_error_fixture(
            "test496.scxml", SCXML_ADAPTER_ERROR_COMMUNICATION,
            "#_scxml_missing", W3C_SCXML_EVENT_PROCESSOR);
    }

    it("test 500 exposes the SCXML Event Processor location") {
        check_true(run_w3c_cmeta_fixture("test500.scxml"));
    }

    it("test 189 prioritizes a #_internal send over an external send") {
        check_true(run_w3c_cmeta_held_loopback_fixture("test189.scxml"));
    }

    it("test 351 maps explicit and absent send IDs") {
        check_true(run_w3c_cmeta_double_loopback_fixture("test351.scxml"));
    }

    it("test 501 routes through the published SCXML location") {
        check_true(run_w3c_cmeta_location_loopback_fixture("test501.scxml"));
    }

    it("test 190 routes #_scxml_sessionid to its external queue") {
        check_true(run_w3c_cmeta_location_loopback_fixture("test190.scxml"));
    }

    it("test 350 uses the target to select the receiving session") {
        check_true(run_w3c_cmeta_location_loopback_fixture("test350.scxml"));
    }

    it("test 495 inserts internal and external sends into their queues") {
        check_true(run_w3c_cmeta_scxml_loopback_fixture("test495.scxml"));
    }

    it("test 354 copies structured Event data across sessions") {
        check_true(run_w3c_copy_fixture("test354.scxml"));
    }

    it("test 553 discards a send whose argument evaluation fails") {
        check_true(run_w3c_cmeta_fixture("test553.scxml"));
    }

    it("test 215 evaluates invoke typeexpr when invoke executes") {
        check_true(run_w3c_invoke_materialization_fixture(
            "test215.scxml", W3C_INVOKE_TYPE_EXPR));
    }

    it("test 216 evaluates invoke srcexpr when invoke executes") {
        check_true(run_w3c_invoke_materialization_fixture(
            "test216.scxml", W3C_INVOKE_SRC_EXPR));
    }

    it("test 220 routes the canonical SCXML invocation type") {
        check_true(run_w3c_invoke_materialization_fixture(
            "test220.scxml", W3C_INVOKE_CANONICAL_TYPE));
    }

    it("test 223 binds an automatically generated invoke ID") {
        check_true(run_w3c_invoke_idlocation_fixture(
            "test223.scxml", NULL, NULL));
    }

    it("test 224 generates invoke IDs in stateid.platformid form") {
        check_true(run_w3c_invoke_idlocation_fixture(
            "test224.scxml", "s0.1", NULL));
    }

    it("test 225 generates unique invoke IDs within one session") {
        check_true(run_w3c_invoke_materialization_fixture(
            "test225.scxml", W3C_INVOKE_UNIQUE_IDS));
    }

    it("test 226 starts a typed service with src and named data") {
        check_true(run_w3c_invoke_materialization_fixture(
            "test226.scxml", W3C_INVOKE_NAMED_PAYLOAD));
    }

    it("test 228 binds completion to its exact invocation ID") {
        check_true(run_w3c_invoke_completion_fixture(
            "test228.scxml", W3C_INVOKE_RETURN_PROVENANCE));
    }

    it("test 229 autoforwards every external Event to the child") {
        check_true(run_w3c_invoke_autoforward_fixture(
            "test229.scxml", false));
    }

    it("test 230 preserves all seven autoforward Event fields") {
        check_true(run_w3c_invoke_autoforward_fixture(
            "test230.scxml", true));
    }

    it("test 232 preserves multiple returned Events before completion") {
        check_true(run_w3c_invoke_completion_fixture(
            "test232.scxml", W3C_INVOKE_MULTIPLE_RETURN));
    }

    it("test 233 finalizes a returned Event before transition selection") {
        check_true(run_w3c_invoke_finalize_fixture(
            "test233.scxml", W3C_INVOKE_FINALIZE_BEFORE_SELECTION));
    }

    it("test 234 finalizes only the invocation that returned the Event") {
        check_true(run_w3c_invoke_finalize_fixture(
            "test234.scxml", W3C_INVOKE_FINALIZE_MATCHING_ONLY));
    }

    it("test 235 exposes completion with the exact invocation ID") {
        check_true(run_w3c_invoke_completion_fixture(
            "test235.scxml", W3C_INVOKE_EXACT_COMPLETION));
    }

    it("test 236 makes completion terminal for its invocation token") {
        check_true(run_w3c_invoke_completion_fixture(
            "test236.scxml", W3C_INVOKE_TERMINAL_COMPLETION));
    }

    it("test 237 cancels the child when the invoking state exits") {
        check_true(run_w3c_invoke_cancellation_fixture(
            "test237.scxml", W3C_INVOKE_CANCEL_STOPS_CHILD));
    }

    it("test 250 runs every active child onexit handler when cancelled") {
        check_true(run_w3c_invoke_cancellation_fixture(
            "test250.scxml", W3C_INVOKE_CANCEL_RUNS_CHILD_ONEXIT));
    }

    it("test 247 reports completion after a child reaches top-level final") {
        check_true(run_w3c_invoke_completion_fixture(
            "test247.scxml", W3C_INVOKE_CHILD_FINAL_COMPLETION));
    }

    it("test 252 rejects child Events received after cancellation") {
        check_true(run_w3c_invoke_cancellation_fixture(
            "test252.scxml", W3C_INVOKE_CANCEL_REJECTS_RETURN));
    }

    it("test 530 evaluates invoke content when invoke executes") {
        check_true(run_w3c_invoke_materialization_fixture(
            "test530.scxml", W3C_INVOKE_CONTENT));
    }

    it("test 554 does not start after invoke argument evaluation fails") {
        check_true(run_w3c_invoke_materialization_fixture(
            "test554.scxml", W3C_INVOKE_ARGUMENT_ERROR));
    }

    it("test 286 raises error.execution for an invalid assign location") {
        check_true(run_w3c_cmeta_fixture("test286.scxml"));
    }

    it("test 287 assigns a legal value to a valid location") {
        check_true(run_w3c_cmeta_fixture("test287.scxml"));
    }

    it("test 294 places param and content values in completion data") {
        check_true(run_w3c_cmeta_fixture("test294.scxml"));
    }

    it("test 298 raises error.execution for an invalid param location") {
        check_true(run_w3c_cmeta_fixture("test298.scxml"));
    }

    it("test 343 omits only a failed completion param") {
        check_true(run_w3c_cmeta_fixture("test343.scxml"));
    }

    it("test 488 orders a param expression error before empty completion data") {
        check_true(run_w3c_cmeta_fixture("test488.scxml"));
    }

    it("test 279 initializes all data before the initial state") {
        check_true(run_w3c_cmeta_fixture("test279.scxml"));
    }

    it("test 277 recovers from an illegal data initializer") {
        check_true(run_w3c_cmeta_fixture("test277.scxml"));
    }

    it("test 276 preserves a top-level value supplied at instantiation") {
        static const scxml_cmeta_environment_override overrides[] = {
            {"sequence", sizeof("sequence") - 1u}};
        const w3c_cmeta_fixture_options options = {
            .environment_overrides = overrides,
            .environment_override_count =
                sizeof(overrides) / sizeof(overrides[0])};
        const w3c_cmeta_state initial = {.sequence = 1};
        check_true(run_w3c_cmeta_fixture_with_schema(
            "test276.scxml", &options, &w3c_cmeta_state_desc, &initial));
    }

    it("test 550 evaluates a data expression at early binding time") {
        check_true(run_w3c_cmeta_fixture("test550.scxml"));
    }

    it("test 487 raises error.execution for an unrepresentable value") {
        check_true(run_w3c_cmeta_fixture("test487.scxml"));
    }

    it("test 309 treats a non-Boolean transition condition as false") {
        check_true(run_w3c_cmeta_fixture("test309.scxml"));
    }

    it("test 310 exposes In through the CMeta data model") {
        check_true(run_w3c_cmeta_fixture("test310.scxml"));
    }

    it("test 311 raises error.execution when a location cannot be evaluated") {
        check_true(run_w3c_cmeta_fixture("test311.scxml"));
    }

    it("test 312 raises error.execution for an illegal value expression") {
        check_true(run_w3c_cmeta_fixture("test312.scxml"));
    }

    it("test 313 rejects a syntactically ill-formed expression at load") {
        check_true(run_w3c_compile_reject_fixture(
            "test313.scxml", SCXML_INVALID_STRUCTURE,
            "CMeta assignment"));
    }

    it("test 314 raises a value-expression error only when evaluated") {
        check_true(run_w3c_cmeta_fixture("test314.scxml"));
    }

    it("test 344 queues a transition condition error before entry raises") {
        check_true(run_w3c_cmeta_fixture("test344.scxml"));
    }

    it("test 318 retains the selected event through state entry") {
        check_true(run_w3c_cmeta_fixture("test318.scxml"));
    }

    it("test 319 leaves the current event unbound during initialization") {
        check_true(run_w3c_cmeta_fixture("test319.scxml"));
    }

    it("test 321 binds the generated session ID during initialization") {
        check_true(run_w3c_cmeta_fixture("test321.scxml"));
    }

    it("test 322 preserves the generated session ID after a write attempt") {
        check_true(run_w3c_cmeta_fixture("test322.scxml"));
    }

    it("test 323 binds the document name during initialization") {
        check_true(run_w3c_cmeta_fixture("test323.scxml"));
    }

    it("test 324 preserves the document name after a write attempt") {
        check_true(run_w3c_cmeta_fixture("test324.scxml"));
    }

    it("test 325 binds the supported I/O processor set during initialization") {
        check_true(run_w3c_cmeta_fixture("test325.scxml"));
    }

    it("test 326 preserves the I/O processor set after a write attempt") {
        check_true(run_w3c_cmeta_fixture("test326.scxml"));
    }

    it("test 329 rejects writes to every system variable") {
        check_true(run_w3c_cmeta_fixture("test329.scxml"));
    }

    it("test 330 exposes every required field on internal and external Events") {
        check_true(run_w3c_cmeta_external_fixture(
            "test330.scxml", "external.check"));
    }

    it("test 331 classifies internal platform and external Events") {
        check_true(run_w3c_cmeta_external_fixture(
            "test331.scxml", "external.check"));
    }

    it("test 332 binds a failed send ID to the processor error Event") {
        check_true(run_w3c_cmeta_rejected_send_fixture("test332.scxml"));
    }

    it("test 333 leaves an unspecified external sendid empty") {
        check_true(run_w3c_cmeta_external_fixture(
            "test333.scxml", "external.check"));
    }

    it("test 335 leaves internal Event origin empty") {
        check_true(run_w3c_cmeta_fixture("test335.scxml"));
    }

    it("test 336 returns an external Event through origin and origintype") {
        check_true(run_w3c_cmeta_routable_loopback_fixture("test336.scxml"));
    }

    it("test 337 leaves internal and platform Event origintype empty") {
        check_true(run_w3c_cmeta_fixture("test337.scxml"));
    }

    it("test 338 binds a child Event to its live invocation ID") {
        check_true(run_w3c_invoke_idlocation_fixture(
            "test338.scxml", NULL, "child.event"));
    }

    it("test 339 leaves invokeid empty for a raised event") {
        check_true(run_w3c_cmeta_fixture("test339.scxml"));
    }

    it("test 342 binds an evaluated sent Event name") {
        check_true(run_w3c_cmeta_loopback_fixture("test342.scxml"));
    }

    it("test 346 raises one execution error for each system-variable write") {
        check_true(run_w3c_cmeta_fixture("test346.scxml"));
    }

    it("test 355 selects the first root child when initial is omitted") {
        check_w3c_fixture("test355.scxml");
    }
    it("test 364 enters every declared or document-order default") {
        check_w3c_fixture("test364.scxml");
    }

    it("test 372 raises parent completion after final onentry") {
        check_true(run_w3c_cmeta_fixture("test372.scxml"));
    }

    it("test 570 orders child completion before parallel completion") {
        check_true(run_w3c_cmeta_fixture("test570.scxml"));
    }

    it("test 375 executes onentry handlers in document order") {
        check_w3c_fixture("test375.scxml");
    }

    it("test 376 keeps onentry handlers as separate executable blocks") {
        check_w3c_adapter_error_fixture("test376.scxml");
    }

    it("test 377 executes onexit handlers in document order") {
        check_w3c_fixture("test377.scxml");
    }

    it("test 378 keeps onexit handlers as separate executable blocks") {
        check_w3c_adapter_error_fixture("test378.scxml");
    }

    it("test 387 enters the declared default history configuration") {
        check_w3c_fixture("test387.scxml");
    }
    it("test 388 restores stored shallow and deep configurations") {
        check_w3c_fixture("test388.scxml");
    }

    it("test 396 uses the selected event name for transition matching") {
        check_true(run_w3c_cmeta_fixture("test396.scxml"));
    }

    it("test 399 applies unions prefixes boundaries and wildcards") {
        check_w3c_fixture("test399.scxml");
    }

    it("test 401 prioritizes a processor error over queued external events") {
        check_true(run_w3c_cmeta_ordered_external_fixture(
            "test401.scxml", "start", "foo"));
    }

    it("test 402 processes processor errors as ordinary internal events") {
        check_true(run_w3c_cmeta_fixture("test402.scxml"));
    }

    it("test 422 starts only live invocations after macrostep settlement") {
        check_true(run_w3c_macrostep_invoke_fixture("test422.scxml"));
    }

    it("test 423 consumes unmatched external events before an enabling event") {
        check_true(run_w3c_cmeta_three_external_fixture(
            "test423.scxml", "start", "externalEvent1", "externalEvent2"));
    }

    it("test 579 orders initial and default history content") {
        check_w3c_fixture("test579.scxml");
    }

    it("test 580 keeps history pseudo states out of the configuration") {
        check_w3c_fixture("test580.scxml");
    }

    it("test 403a applies source priority, document order, and guards") {
        check_w3c_fixture("test403a.scxml");
    }

    it("test 403b de-duplicates an ancestor transition selected by parallel leaves") {
        check_true(run_w3c_cmeta_fixture("test403b.scxml"));
    }

    it("test 403c retains compatible transitions while preempting conflicts") {
        check_true(run_w3c_cmeta_fixture("test403c.scxml"));
    }

    it("test 403c keeps one wildcard transition across selection rounds") {
        static const char wildcard_transition[] = "<transition event=\"*\"";
        char path[W3C_FIXTURE_PATH_CAPACITY];
        char *source = NULL;
        size_t source_size = 0u;
        int path_size = snprintf(path, sizeof(path), "%s/%s",
                                 SCXML_W3C_FIXTURE_DIR, "test403c.scxml");

        check_true(path_size >= 0 && (size_t)path_size < sizeof(path));
        source = tt_read_file(path, &source_size);
        check_not_null(source);
        if (source != NULL) {
            const char *first = strstr(source, wildcard_transition);
            check_not_null(first);
            if (first != NULL)
                check_null(strstr(first + 1u, wildcard_transition));
        }
        free(source);
    }

    it("test 404 executes exits in exit order before transition content") {
        check_w3c_fixture("test404.scxml");
    }

    it("test 405 executes selected transition content in document order") {
        check_w3c_fixture("test405.scxml");
    }

    it("test 406 executes transition content before entry-order actions") {
        check_w3c_fixture("test406.scxml");
    }

    it("test 407 executes onexit content when a state exits") {
        check_w3c_fixture("test407.scxml");
    }

    it("test 409 removes exited descendants before ancestor onexit") {
        check_w3c_fixture("test409.scxml");
    }

    it("test 411 adds a state before its own onentry") {
        check_w3c_fixture("test411.scxml");
    }

    it("test 412 orders parent entry, initial transition, then child entry") {
        check_w3c_fixture("test412.scxml");
    }

    it("test 415 halts before selecting a final onentry event") {
        check_true(run_w3c_top_level_final_fixture("test415.scxml"));
    }

    it("test 416 raises compound state completion") {
        check_w3c_fixture("test416.scxml");
    }

    it("test 417 raises parallel state completion") {
        check_w3c_fixture("test417.scxml");
    }

    it("test 419 selects eventless transitions before queued events") {
        check_w3c_fixture("test419.scxml");
    }

    it("test 421 drains unmatched internal events before an enabled one") {
        check_w3c_fixture("test421.scxml");
    }

    it("test 503 gives targetless transitions an empty exit set") {
        check_w3c_fixture("test503.scxml");
    }

    it("test 504 exits every active descendant of the external LCCA") {
        check_w3c_fixture("test504.scxml");
    }

    it("test 505 retains a compound source for an internal descendant target") {
        check_w3c_fixture("test505.scxml");
    }

    it("test 506 treats a non-descendant internal target as external") {
        check_w3c_fixture("test506.scxml");
    }

    it("test 527 evaluates expression content for completion data") {
        check_true(run_w3c_cmeta_fixture("test527.scxml"));
    }

    it("test 528 orders content errors before empty completion data") {
        check_true(run_w3c_cmeta_fixture("test528.scxml"));
    }

    it("test 529 preserves inline completion content") {
        check_true(run_w3c_cmeta_fixture("test529.scxml"));
    }

    it("test 533 treats an internal transition from parallel as external") {
        check_w3c_fixture("test533.scxml");
    }

    it("test 436 exposes exact In membership in the null data model") {
        check_w3c_fixture("test436.scxml");
    }

    it("test 576 enters both non-default root initial targets") {
        check_w3c_fixture("test576.scxml");
    }
    it("test 413 starts in the root initial configuration") {
        check_w3c_fixture("test413.scxml");
    }
}
