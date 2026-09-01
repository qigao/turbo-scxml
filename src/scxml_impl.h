#ifndef SCXML_IMPL_H
#define SCXML_IMPL_H

#include <scxml/scxml.h>
#include <tlog.h>
#include <turbo/thread.h>
#include <turbo_uuid.h>

#include "scxml_expr.h"
#include "scxml_assign.h"
#include "scxml_ast.h"
#include "scxml_syntax.h"
#include "scxml_foreach.h"
#include "scxml_location.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCXML_NAMESPACE "http://www.w3.org/2005/07/scxml"
#define SCXML_DEFAULT_MAX_STATES 65536u
#define SCXML_DEFAULT_MAX_EVENTS 65536u
#define SCXML_DEFAULT_MAX_TRANSITIONS 1048576u
#define SCXML_DEFAULT_MAX_NAME_BYTES (16u * 1024u * 1024u)

static const char SCXML_ERROR_EXECUTION_EVENT[] = "error.execution";
static const char SCXML_ERROR_COMMUNICATION_EVENT[] = "error.communication";
static const uint64_t SCXML_EXTERNAL_METADATA_TOKEN_BIT =
    UINT64_C(1) << 63u;

typedef enum scxml_data_model {
    SCXML_DATA_MODEL_NULL = 0,
    SCXML_DATA_MODEL_CMETA
} scxml_data_model;

typedef struct scxml_name_ref {
    turbo_xml_string_view name;
    turbo_xml_location location;
    uint64_t id;
    size_t order;
} scxml_name_ref;

typedef struct scxml_program_name {
    const char *name;
    size_t size;
    uint64_t id;
} scxml_program_name;

typedef struct scxml_node_ref {
    scxml_ast_node_id node_id;
    cflow_machine_state_id id;
} scxml_node_ref;

typedef struct scxml_synthetic_initial {
    cflow_machine_state_id parent;
    cflow_machine_state_id state;
    turbo_xml_string_view target;
    turbo_xml_location location;
} scxml_synthetic_initial;

typedef enum scxml_step_kind {
    SCXML_STEP_RAISE = 1,
    SCXML_STEP_IF,
    SCXML_STEP_LOG,
    SCXML_STEP_ASSIGN,
    SCXML_STEP_FOREACH,
    SCXML_STEP_SEND,
    SCXML_STEP_CANCEL,
    SCXML_STEP_INVOKE_ENTER,
    SCXML_STEP_INVOKE_EXIT,
    SCXML_STEP_LATE_INITIALIZE,
    SCXML_STEP_DONEDATA
} scxml_step_kind;

typedef enum scxml_effect_kind {
    SCXML_EFFECT_SEND = 1,
    SCXML_EFFECT_CANCEL
} scxml_effect_kind;

typedef struct scxml_payload_descriptor {
    const char *name;
    size_t name_size;
    scxml_expr_program expression;
} scxml_payload_descriptor;

typedef struct scxml_content_descriptor {
    scxml_content_kind kind;
    const char *bytes;
    size_t byte_count;
    scxml_location location;
} scxml_content_descriptor;

typedef struct scxml_effect_descriptor {
    scxml_effect_kind kind;
    cflow_event_id event_id;
    bool internal_target;
    bool has_event_expr;
    bool has_target_expr;
    bool has_type_expr;
    bool has_delay_expr;
    bool has_send_id_expr;
    bool has_id_location;
    size_t payload_first;
    size_t payload_count;
    scxml_expr_program event_expr;
    scxml_expr_program target_expr;
    scxml_expr_program type_expr;
    scxml_expr_program delay_expr;
    scxml_expr_program send_id_expr;
    scxml_expr_program data_expr;
    scxml_location id_location;
    scxml_content_descriptor content;
    union {
        scxml_send_request send;
        scxml_cancel_request cancel;
    } request;
} scxml_effect_descriptor;

typedef struct scxml_step {
    scxml_step_kind kind;
    size_t next;
    cflow_event_id event;
    size_t branch_first;
    size_t branch_count;
    const char *label;
    size_t effect;
    size_t invocation;
    size_t assignment;
    size_t assignment_count;
    size_t late_initializer;
    size_t foreach_descriptor;
    size_t done_data;
} scxml_step;

typedef struct scxml_foreach_descriptor {
    scxml_foreach_program program;
    size_t step_begin;
    size_t step_end;
} scxml_foreach_descriptor;

typedef struct scxml_branch {
    cflow_machine_state_id state;
    scxml_expr_program condition;
    size_t step_begin;
    size_t step_end;
    bool unconditional;
} scxml_branch;

typedef struct scxml_block {
    const cmeta_type_desc *state_type;
    const scxml_step *steps;
    const scxml_branch *branches;
    const scxml_effect_descriptor *effects;
    const scxml_assign_program *assignments;
    const scxml_payload_descriptor *payloads;
    const scxml_foreach_descriptor *foreach_descriptors;
    const struct scxml_invocation_descriptor *invocations;
    const struct scxml_done_data_descriptor *done_data;
    size_t step_begin;
    size_t step_end;
    size_t step_storage_count;
    size_t branch_storage_count;
    size_t effect_storage_count;
    size_t assignment_storage_count;
    size_t payload_storage_count;
    size_t foreach_storage_count;
    size_t invocation_storage_count;
    size_t done_data_storage_count;
    size_t max_conditional_depth;
    cflow_event_id execution_error_event;
    const scxml_program_name *const *event_names_by_id;
    size_t event_name_count;
    scxml_expr_system_values system_values;
} scxml_block;

typedef struct scxml_invocation_descriptor {
    cflow_machine_state_id owner;
    const char *id;
    size_t id_size;
    size_t owner_id_size;
    size_t dynamic_id_max_size;
    size_t dynamic_done_name_max_size;
    const char *type;
    size_t type_size;
    const char *src;
    size_t src_size;
    const char *done_name;
    size_t done_name_size;
    cflow_event_id done_event;
    const scxml_block *finalize;
    scxml_ast_node_id source_node_id;
    bool autoforward;
    bool has_type_expr;
    bool has_src_expr;
    bool has_id_location;
    size_t payload_first;
    size_t payload_count;
    scxml_expr_program type_expr;
    scxml_expr_program src_expr;
    scxml_expr_program data_expr;
    scxml_location id_location;
    scxml_content_descriptor content;
} scxml_invocation_descriptor;

typedef struct scxml_done_data_descriptor {
    cflow_machine_state_id parent;
    cflow_machine_state_id final_state;
    size_t assignment_first;
    size_t assignment_count;
    scxml_expr_program expression;
    scxml_content_descriptor content;
    cmeta_data_field_desc *fields;
    cmeta_data_struct_shape shape;
    cmeta_data_desc schema;
    char *schema_stable_id;
} scxml_done_data_descriptor;

typedef struct scxml_guard_user {
    scxml_data_model data_model;
    union {
        cflow_machine_state_id state;
        scxml_expr_program expression;
    } value;
    const scxml_program_name *const *event_names_by_id;
    size_t event_name_count;
    cflow_event_id execution_error_event;
    scxml_expr_system_values system_values;
} scxml_guard_user;

typedef struct scxml_counts {
    size_t state_rows;
    size_t node_refs;
    size_t state_names;
    size_t synthetic_initials;
    size_t transition_rows;
    size_t transition_target_rows;
    size_t event_descriptor_rows;
    size_t event_descriptor_guard_rows;
    size_t event_descriptor_action_rows;
    size_t event_descriptor_target_rows;
    size_t guard_rows;
    size_t event_occurrences;
    size_t executable_blocks;
    size_t block_rows;
    size_t executable_steps;
    size_t log_label_bytes;
    size_t effect_rows;
    size_t payload_rows;
    size_t max_payload_entries;
    size_t dynamic_expression_rows;
    size_t assignment_rows;
    size_t data_initializer_rows;
    size_t late_initializer_rows;
    size_t done_data_rows;
    size_t foreach_rows;
    size_t effect_string_bytes;
    size_t conditional_branches;
    size_t max_conditional_depth;
    size_t state_action_rows;
    size_t transition_action_rows;
    size_t invocation_rows;
    size_t invocation_string_bytes;
    uint32_t requirements;
} scxml_counts;

typedef struct scxml_build {
    scxml_limits limits;
    scxml_diagnostic *diagnostic;
    scxml_data_model data_model;
    const cmeta_data_desc *cmeta_root;
    scxml_expr_limits expression_limits;
    cflow_statechart_state *states;
    cflow_statechart_transition *transitions;
    cflow_statechart_transition_target *transition_targets;
    cflow_statechart_guard *guards;
    cflow_event_type *events;
    cflow_statechart_executable *executables;
    cflow_statechart_state_action *state_actions;
    cflow_statechart_transition_action *transition_actions;
    cflow_statechart_executable_binding *bindings;
    cflow_statechart_guard_binding *guard_bindings;
    scxml_guard_user *guard_users;
    scxml_block *blocks;
    scxml_step *steps;
    scxml_branch *branches;
    scxml_effect_descriptor *effects;
    scxml_payload_descriptor *payloads;
    scxml_assign_program *assignments;
    scxml_foreach_descriptor *foreach_descriptors;
    scxml_invocation_descriptor *invocations;
    scxml_done_data_descriptor *done_data;
    scxml_name_ref *invocation_names;
    char *log_storage;
    char *effect_storage;
    char *invocation_storage;
    scxml_name_ref *state_names;
    scxml_name_ref *event_names;
    scxml_name_ref *event_occurrences;
    scxml_node_ref *node_refs;
    scxml_synthetic_initial *synthetic_initials;
    size_t state_index;
    size_t node_ref_index;
    size_t state_name_index;
    size_t synthetic_index;
    size_t transition_index;
    size_t transition_target_index;
    size_t guard_index;
    size_t event_occurrence_index;
    size_t event_name_count;
    size_t executable_index;
    size_t block_index;
    size_t step_index;
    size_t branch_index;
    size_t effect_index;
    size_t payload_index;
    size_t assignment_index;
    size_t foreach_index;
    size_t log_storage_index;
    size_t effect_storage_index;
    size_t step_capacity;
    size_t branch_capacity;
    size_t effect_capacity;
    size_t payload_capacity;
    size_t assignment_capacity;
    size_t foreach_capacity;
    size_t max_iterations;
    size_t log_storage_capacity;
    size_t effect_storage_capacity;
    size_t guard_capacity;
    size_t transition_target_capacity;
    size_t max_conditional_depth;
    size_t state_action_index;
    size_t transition_action_index;
    size_t invocation_index;
    size_t invocation_emit_index;
    size_t invocation_storage_index;
    size_t invocation_storage_capacity;
    size_t invocation_capacity;
    size_t done_data_index;
    size_t done_data_capacity;
    size_t late_initializer_index;
    uint32_t requirements;
    bool late_binding;
    cflow_event_id execution_error_event;
} scxml_build;

typedef struct scxml_program_impl {
    cflow_statechart statechart;
    scxml_data_model data_model;
    const cmeta_data_desc *cmeta_root;
    scxml_program_name *state_names;
    size_t state_name_count;
    scxml_program_name *event_names;
    const scxml_program_name **event_names_by_id;
    size_t event_name_count;
    cflow_statechart_executable_binding *bindings;
    size_t binding_count;
    cflow_statechart_guard_binding *guard_bindings;
    scxml_guard_user *guard_users;
    size_t guard_binding_count;
    scxml_block *blocks;
    scxml_step *steps;
    scxml_branch *branches;
    size_t branch_count;
    scxml_effect_descriptor *effects;
    size_t effect_count;
    scxml_payload_descriptor *payloads;
    size_t payload_count;
    size_t max_payload_entries;
    scxml_assign_program *assignments;
    size_t assignment_count;
    size_t data_initializer_count;
    size_t late_initializer_count;
    scxml_foreach_descriptor *foreach_descriptors;
    size_t foreach_count;
    scxml_invocation_descriptor *invocations;
    size_t invocation_count;
    scxml_done_data_descriptor *done_data;
    size_t done_data_count;
    const char *document_name;
    size_t document_name_size;
    char *name_storage;
    char *log_storage;
    char *effect_storage;
    char *invocation_storage;
    cflow_event_id execution_error_event;
    cflow_event_id communication_error_event;
    uint32_t requirements;
    bool null_value;
} scxml_program_impl;

typedef struct scxml_session_impl scxml_session_impl;

typedef struct scxml_session_binding_user {
    const scxml_block *block;
    scxml_session_impl *session;
} scxml_session_binding_user;

typedef struct scxml_session_guard_user {
    const scxml_guard_user *guard;
    scxml_session_impl *session;
} scxml_session_guard_user;

typedef enum scxml_late_initializer_phase {
    SCXML_LATE_INITIALIZER_NEVER = 0,
    SCXML_LATE_INITIALIZER_PENDING,
    SCXML_LATE_INITIALIZER_DONE
} scxml_late_initializer_phase;

typedef struct scxml_late_initializer_state {
    scxml_late_initializer_phase phase;
} scxml_late_initializer_state;

typedef enum scxml_delayed_state {
    SCXML_DELAYED_FREE = 0,
    SCXML_DELAYED_RESERVED,
    SCXML_DELAYED_ACTIVE,
    SCXML_DELAYED_CANCEL_RESERVED
} scxml_delayed_state;

typedef struct scxml_delayed_send {
    const char *id;
    size_t id_size;
    char generated_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    scxml_delayed_state state;
    scxml_delayed_state previous_state;
} scxml_delayed_send;

typedef enum scxml_prepared_kind {
    SCXML_PREPARED_SEND = 1,
    SCXML_PREPARED_DELAYED_SEND,
    SCXML_PREPARED_CANCEL
} scxml_prepared_kind;

typedef struct scxml_prepared_effect {
    scxml_session_impl *session;
    cflow_statechart_effect_ticket adapter_ticket;
    size_t registry_index;
    scxml_prepared_kind kind;
    bool in_use;
} scxml_prepared_effect;

typedef enum scxml_invocation_state {
    SCXML_INVOCATION_INACTIVE = 0,
    SCXML_INVOCATION_PENDING,
    SCXML_INVOCATION_START_RESERVED,
    SCXML_INVOCATION_FAIL_RESERVED,
    SCXML_INVOCATION_ACTIVE,
    SCXML_INVOCATION_FAILED
} scxml_invocation_state;

typedef struct scxml_invocation_row {
    uint64_t token;
    const char *id;
    size_t id_size;
    char owned_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    bool owns_id;
    scxml_invocation_state state;
} scxml_invocation_row;

typedef enum scxml_invocation_effect_kind {
    SCXML_INVOCATION_EFFECT_ENTER = 1,
    SCXML_INVOCATION_EFFECT_EXIT,
    SCXML_INVOCATION_EFFECT_START,
    SCXML_INVOCATION_EFFECT_FAIL,
    SCXML_INVOCATION_EFFECT_COMPLETE,
    SCXML_INVOCATION_EFFECT_FORWARD
} scxml_invocation_effect_kind;

typedef struct scxml_invocation_lifecycle_effect {
    scxml_session_impl *session;
    cflow_statechart_effect_ticket adapter_ticket;
    uint64_t token;
    size_t invocation;
    scxml_invocation_effect_kind kind;
    bool in_use;
} scxml_invocation_lifecycle_effect;

typedef union scxml_event_data_storage {
    long double floating_alignment;
    void *pointer_alignment;
    uint64_t integer_alignment;
    unsigned char bytes[SCXML_EVENT_DATA_CAPACITY];
} scxml_event_data_storage;

typedef enum scxml_completion_data_state {
    SCXML_COMPLETION_DATA_FREE = 0,
    SCXML_COMPLETION_DATA_BUILDING,
    SCXML_COMPLETION_DATA_READY,
    SCXML_COMPLETION_DATA_BOUND
} scxml_completion_data_state;

#define SCXML_COMPLETION_DATA_SUBSET_SUFFIX "#runtime-subset:"
#define SCXML_COMPLETION_DATA_SUBSET_SUFFIX_SIZE \
    (sizeof(SCXML_COMPLETION_DATA_SUBSET_SUFFIX) - 1u)

typedef struct scxml_completion_data_slot {
    scxml_completion_data_state state;
    cflow_machine_state_id parent;
    uint64_t sequence;
    size_t data_size;
    const cmeta_data_desc *data_schema;
    bool data_object_live;
    cmeta_data_field_desc *projection_fields;
    size_t projection_field_capacity;
    char *projection_stable_id;
    size_t projection_stable_id_capacity;
    cmeta_data_struct_shape projection_shape;
    cmeta_data_desc projection_schema;
    char data[SCXML_EVENT_METADATA_CAPACITY + 1u];
    scxml_event_data_storage data_object;
} scxml_completion_data_slot;

typedef struct scxml_external_event_metadata_row {
    scxml_session_impl *session;
    uint64_t token;
    bool in_use;
    size_t send_id_size;
    size_t origin_size;
    size_t origin_type_size;
    size_t invoke_id_size;
    size_t data_size;
    const cmeta_data_desc *data_schema;
    bool data_object_live;
    char send_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char origin[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char origin_type[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char invoke_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char data[SCXML_EVENT_METADATA_CAPACITY + 1u];
    scxml_event_data_storage data_object;
} scxml_external_event_metadata_row;

struct scxml_session_impl {
    const scxml_program_impl *program;
    cflow_statechart_instance instance;
    cflow_statechart_executable_binding *bindings;
    scxml_session_binding_user *binding_users;
    size_t binding_count;
    cflow_statechart_guard_binding *guard_bindings;
    scxml_session_guard_user *guard_users;
    size_t guard_binding_count;
    scxml_late_initializer_state *late_initializers;
    size_t late_initializer_count;
    bool late_initializer_ticket_pending;
    char *system_name;
    char session_id[TURBO_UUID_STRING_SIZE];
    char scxml_location[sizeof("#_scxml_") - 1u +
                        TURBO_UUID_STRING_SIZE];
    char current_event_name[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char current_event_send_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char current_event_origin[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char current_event_origin_type[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char current_event_invoke_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    char current_event_data[SCXML_EVENT_METADATA_CAPACITY + 1u];
    const cmeta_data_desc *current_event_data_schema;
    bool current_event_data_object_live;
    scxml_event_data_storage current_event_data_object;
    scxml_completion_data_slot *completion_data_slots;
    size_t completion_data_capacity;
    cmeta_data_field_desc *completion_projection_fields;
    char *completion_projection_stable_ids;
    size_t completion_projection_field_capacity;
    size_t completion_projection_stable_id_capacity;
    size_t current_completion_data_slot;
    uint64_t next_completion_data_sequence;
    scxml_expr_system_values system_values;
    scxml_event_io_adapter event_io;
    void *adapter_user;
    turbo_mutex_t registry_lock;
    scxml_delayed_send *delayed_sends;
    size_t delayed_send_capacity;
    scxml_prepared_effect *prepared_effects;
    size_t prepared_effect_capacity;
    scxml_invoke_adapter invoke;
    void *invoke_user;
    scxml_invocation_row *invocation_rows;
    size_t invocation_capacity;
    scxml_invocation_lifecycle_effect *invocation_effects;
    size_t invocation_effect_capacity;
    scxml_invoke_stats invoke_stats;
    uint64_t next_send_token;
    uint64_t next_invocation_token;
    bool failed_send_id_restore_live;
    scxml_location failed_send_id_location;
    size_t failed_send_id_size;
    char failed_send_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    scxml_external_event_metadata_row *external_metadata_rows;
    size_t external_metadata_capacity;
    scxml_payload_entry *payload_scratch;
    size_t payload_scratch_capacity;
    uint64_t next_external_metadata_token;
    bool has_event_io;
    bool has_invoke;
    atomic_bool adapter_close_called;
    atomic_bool invoke_close_called;
};

#endif
