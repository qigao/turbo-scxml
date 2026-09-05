#ifndef SCXML_EXPR_H
#define SCXML_EXPR_H

#include <scxml/scxml.h>
#include <cflow/machine.h>
#include <cmeta/data.h>

#include "scxml_scope.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCXML_EXPR_DIAGNOSTIC_CAPACITY 192u

typedef enum scxml_expr_status {
    SCXML_EXPR_OK = 0,
    SCXML_EXPR_INVALID_ARGUMENT,
    SCXML_EXPR_SYNTAX_ERROR,
    SCXML_EXPR_UNKNOWN_LOCATION,
    SCXML_EXPR_TYPE_MISMATCH,
    SCXML_EXPR_LIMIT_EXCEEDED,
    SCXML_EXPR_ALLOCATION_FAILED,
    SCXML_EXPR_EVALUATION_ERROR
} scxml_expr_status;

typedef struct scxml_expr_limits {
    size_t max_source_bytes;
    size_t max_instructions;
    size_t max_operands;
    size_t max_expression_depth;
    size_t max_path_depth;
    size_t max_literal_bytes;
    size_t max_string_bytes;
} scxml_expr_limits;

typedef struct scxml_expr_diagnostic {
    scxml_expr_status status;
    size_t byte_offset;
    char message[SCXML_EXPR_DIAGNOSTIC_CAPACITY];
} scxml_expr_diagnostic;

typedef struct scxml_expr_program {
    void *impl;
} scxml_expr_program;

enum {
    SCXML_EXPR_SYSTEM_NAME = UINT32_C(1) << 0u,
    SCXML_EXPR_SYSTEM_SESSION_ID = UINT32_C(1) << 1u,
    SCXML_EXPR_SYSTEM_EVENT = UINT32_C(1) << 2u,
    SCXML_EXPR_SYSTEM_EVENT_NAME = UINT32_C(1) << 3u,
    SCXML_EXPR_SYSTEM_EVENT_TYPE = UINT32_C(1) << 4u,
    SCXML_EXPR_SYSTEM_EVENT_SEND_ID = UINT32_C(1) << 5u,
    SCXML_EXPR_SYSTEM_EVENT_ORIGIN = UINT32_C(1) << 6u,
    SCXML_EXPR_SYSTEM_EVENT_ORIGIN_TYPE = UINT32_C(1) << 7u,
    SCXML_EXPR_SYSTEM_EVENT_INVOKE_ID = UINT32_C(1) << 8u,
    SCXML_EXPR_SYSTEM_EVENT_DATA = UINT32_C(1) << 9u,
    SCXML_EXPR_SYSTEM_IOPROCESSORS = UINT32_C(1) << 10u,
    SCXML_EXPR_SYSTEM_ALL = (UINT32_C(1) << 11u) - UINT32_C(1)
};

/** Private compile-time allowlist for SCXML-only expression constructs. */
typedef struct scxml_expr_compile_policy {
    uint32_t allowed_system_operands;
    bool allow_in;
    bool allow_is_bound;
} scxml_expr_compile_policy;

typedef struct scxml_expr_string_view {
    const char *data;
    size_t size;
} scxml_expr_string_view;

typedef bool (*scxml_expr_is_data_bound_fn)(
    void *user, size_t offset, size_t storage_size, bool *out_bound);

/** Call-scoped immutable SCXML system strings; no member may be retained. */
typedef struct scxml_expr_system_values {
    scxml_expr_string_view name;
    scxml_expr_string_view session_id;
    scxml_expr_string_view event_name;
    scxml_expr_string_view event_type;
    scxml_expr_string_view event_send_id;
    scxml_expr_string_view event_origin;
    scxml_expr_string_view event_origin_type;
    scxml_expr_string_view event_invoke_id;
    scxml_expr_string_view event_data;
    /** Optional owned event object described by its event-specific root schema. */
    const cmeta_data_desc *event_data_schema;
    const void *event_data_object;
    const scxml_ioprocessor_descriptor *ioprocessors;
    size_t ioprocessor_count;
    scxml_expr_is_data_bound_fn is_data_bound;
    void *data_bound_user;
    scxml_scope_view *supplemental;
    /** Private per-session data-model execution context; call-scoped. */
    void *datamodel_user;
} scxml_expr_system_values;

/* Private runtime boundary shared by every direct CMeta location access. */
scxml_expr_status scxml_expr_require_data_bound(
    const scxml_expr_system_values *values,
    size_t offset, size_t storage_size,
    scxml_expr_diagnostic *diagnostic);

typedef enum scxml_expr_value_kind {
    SCXML_EXPR_VALUE_INVALID = 0,
    SCXML_EXPR_VALUE_BOOL,
    SCXML_EXPR_VALUE_SINT,
    SCXML_EXPR_VALUE_UINT,
    SCXML_EXPR_VALUE_FLOAT,
    SCXML_EXPR_VALUE_STRING
} scxml_expr_value_kind;

typedef enum scxml_expr_path_policy {
    SCXML_EXPR_PATH_STRICT = 0,
    SCXML_EXPR_PATH_RUNTIME_MISSING
} scxml_expr_path_policy;

/** One scalar result; string bytes remain borrowed only until state mutation. */
typedef struct scxml_expr_value {
    scxml_expr_value_kind kind;
    union {
        bool boolean;
        int64_t sint;
        uint64_t uint;
        double number;
        struct {
            const char *data;
            size_t size;
        } string;
    } data;
} scxml_expr_value;

typedef bool (*scxml_expr_resolve_state_fn)(
    void *user, const char *name, size_t name_size,
    cflow_machine_state_id *out_state);

typedef bool (*scxml_expr_is_active_fn)(
    void *user, cflow_machine_state_id state, bool *out_active);

typedef scxml_expr_status (*scxml_expr_external_evaluate_fn)(
    const char *source, size_t source_size,
    scxml_expr_value_kind expected_kind,
    const void *root_object,
    scxml_expr_is_active_fn is_active, void *active_user,
    const scxml_expr_system_values *system_values,
    scxml_expr_value *out_value,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_limits scxml_expr_default_limits(void);
bool scxml_expr_limits_valid(
    const scxml_expr_limits *limits);

/*
 * Private CFlowScxml foundation API. The root descriptor and every descriptor
 * reachable from a compiled path remain borrowed until program destruction.
 */
scxml_expr_status scxml_expr_compile(
    scxml_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_limits *limits,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status scxml_expr_compile_with_policy(
    scxml_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_compile_policy *policy,
    const scxml_expr_limits *limits,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status scxml_expr_compile_with_scope_policy(
    scxml_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    const scxml_scope_schema *supplemental,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_compile_policy *policy,
    const scxml_expr_limits *limits,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status scxml_expr_compile_value(
    scxml_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_limits *limits,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status scxml_expr_compile_value_with_scope(
    scxml_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    const scxml_scope_schema *supplemental,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_limits *limits,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status scxml_expr_compile_value_with_scope_policy(
    scxml_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    const scxml_scope_schema *supplemental,
    scxml_expr_path_policy path_policy,
    scxml_expr_value_kind unresolved_kind,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_limits *limits,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status scxml_expr_compile_external(
    scxml_expr_program *out,
    const char *source, size_t source_size,
    scxml_expr_value_kind expected_kind,
    scxml_expr_external_evaluate_fn evaluate,
    const scxml_expr_limits *limits,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_value_kind
scxml_expr_program_value_kind(
    const scxml_expr_program *program);

scxml_expr_status scxml_expr_evaluate(
    const scxml_expr_program *program,
    const void *root_object,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    bool *out_value,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status scxml_expr_evaluate_with_system(
    const scxml_expr_program *program,
    const void *root_object,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    const scxml_expr_system_values *system_values,
    bool *out_value,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status scxml_expr_evaluate_value(
    const scxml_expr_program *program,
    const void *root_object,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    scxml_expr_value *out_value,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status
scxml_expr_evaluate_value_with_system(
    const scxml_expr_program *program,
    const void *root_object,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    const scxml_expr_system_values *system_values,
    scxml_expr_value *out_value,
    scxml_expr_diagnostic *diagnostic);

void scxml_expr_program_destroy(
    scxml_expr_program *program);

#endif /* SCXML_EXPR_H */
