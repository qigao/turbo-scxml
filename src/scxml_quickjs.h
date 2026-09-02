#ifndef TURBO_SCXML_QUICKJS_H
#define TURBO_SCXML_QUICKJS_H

#include "scxml_impl.h"

typedef enum scxml_quickjs_status {
    SCXML_QUICKJS_OK = 0,
    SCXML_QUICKJS_INVALID_ARGUMENT,
    SCXML_QUICKJS_ALLOCATION_FAILED,
    SCXML_QUICKJS_LIMIT_EXCEEDED,
    SCXML_QUICKJS_EXCEPTION
} scxml_quickjs_status;

typedef struct scxml_quickjs_runtime {
    void *runtime;
    void *context;
    scxml_quickjs_compile_options_v1 options;
    char *result_string;
    size_t result_string_capacity;
    uint64_t deadline_ms;
    size_t max_diagnostic_bytes;
    bool interrupted;
} scxml_quickjs_runtime;

bool scxml_quickjs_limits_valid(
    const scxml_quickjs_compile_options_v1 *options);
bool scxml_quickjs_static_property_budget_valid(
    const scxml_quickjs_compile_options_v1 *options,
    size_t supplemental_properties);
scxml_quickjs_status scxml_quickjs_runtime_init(
    scxml_quickjs_runtime *runtime,
    const scxml_quickjs_compile_options_v1 *options,
    char *diagnostic, size_t diagnostic_capacity);
scxml_quickjs_status scxml_quickjs_runtime_eval(
    scxml_quickjs_runtime *runtime,
    const char *source, size_t source_size,
    const char *filename,
    uint64_t max_eval_milliseconds,
    char *diagnostic, size_t diagnostic_capacity);
void scxml_quickjs_runtime_destroy(scxml_quickjs_runtime *runtime);
scxml_quickjs_status scxml_quickjs_validate_source(
    const scxml_quickjs_compile_options_v1 *options,
    const char *source, size_t source_size,
    char *diagnostic, size_t diagnostic_capacity);
scxml_quickjs_status scxml_quickjs_validate_expression(
    const scxml_quickjs_compile_options_v1 *options,
    const char *source, size_t source_size,
    char *diagnostic, size_t diagnostic_capacity);
scxml_expr_status scxml_quickjs_evaluate_expression(
    const char *source, size_t source_size,
    scxml_expr_value_kind expected_kind,
    const void *root_object,
    scxml_expr_is_active_fn is_active, void *active_user,
    const scxml_expr_system_values *system_values,
    scxml_expr_value *out_value,
    scxml_expr_diagnostic *diagnostic);
scxml_quickjs_status scxml_quickjs_collect_script_variables(
    scxml_scope_schema *scope, const cmeta_data_desc *root,
    const char *source, size_t source_size,
    size_t max_variables, size_t *variable_count,
    char *diagnostic, size_t diagnostic_capacity);
bool scxml_quickjs_session_runtime_init(
    scxml_session_impl *session, const char **out_error);
void scxml_quickjs_session_runtime_destroy(scxml_session_impl *session);
bool scxml_quickjs_execute_script(
    scxml_session_impl *session,
    const scxml_script_descriptor *script,
    void *state,
    scxml_scope_view *supplemental,
    scxml_expr_is_active_fn is_active, void *active_user,
    const scxml_expr_system_values *system_values,
    const char **out_error);

scxml_quickjs_compile_options_v1
scxml_quickjs_default_compile_options_impl(const cmeta_data_desc *root);

scxml_status scxml_quickjs_compile(
    scxml_program *out, const char *input, size_t input_size,
    const scxml_limits *limits,
    const scxml_quickjs_compile_options_v1 *options,
    scxml_diagnostic *diagnostic);

cflow_statechart_instance_status scxml_quickjs_session_init(
    scxml_session *session, const scxml_session_config *config,
    const scxml_quickjs_session_options_v1 *options);

#endif
