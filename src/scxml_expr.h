#ifndef SCXML_EXPR_H
#define SCXML_EXPR_H

#include <cflow/machine.h>
#include <cmeta/data.h>

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

typedef struct scxml_expr_string_view {
    const char *data;
    size_t size;
} scxml_expr_string_view;

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
    /** Optional owned event object; schema is the root or a compatible subset. */
    const cmeta_data_desc *event_data_schema;
    const void *event_data_object;
    scxml_expr_string_view scxml_location;
} scxml_expr_system_values;

typedef enum scxml_expr_value_kind {
    SCXML_EXPR_VALUE_INVALID = 0,
    SCXML_EXPR_VALUE_BOOL,
    SCXML_EXPR_VALUE_SINT,
    SCXML_EXPR_VALUE_UINT,
    SCXML_EXPR_VALUE_FLOAT,
    SCXML_EXPR_VALUE_STRING
} scxml_expr_value_kind;

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

scxml_expr_status scxml_expr_compile_value(
    scxml_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
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
