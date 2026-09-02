#ifndef SCXML_ASSIGN_H
#define SCXML_ASSIGN_H

#include "scxml_expr.h"
#include "scxml_location.h"

#include <cbind/cbind.h>

typedef struct scxml_quickjs_compile_options_v1
    scxml_quickjs_compile_options_v1;

typedef struct scxml_assign_program {
    void *impl;
} scxml_assign_program;

typedef enum scxml_assign_location_policy {
    SCXML_ASSIGN_LOCATION_STRICT = 0,
    SCXML_ASSIGN_LOCATION_RUNTIME = 1
} scxml_assign_location_policy;

scxml_expr_status scxml_assign_compile(
    scxml_assign_program *out,
    const char *location, size_t location_size,
    const char *expression, size_t expression_size,
    const cmeta_data_desc *root,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_limits *limits,
    scxml_assign_location_policy location_policy,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status scxml_assign_compile_with_scope(
    scxml_assign_program *out,
    const char *location, size_t location_size,
    const char *expression, size_t expression_size,
    const cmeta_data_desc *root,
    const scxml_scope_schema *supplemental,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_limits *limits,
    scxml_assign_location_policy location_policy,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status scxml_assign_compile_quickjs_with_scope(
    scxml_assign_program *out,
    const char *location, size_t location_size,
    const char *expression, size_t expression_size,
    const cmeta_data_desc *root,
    const scxml_scope_schema *supplemental,
    const scxml_quickjs_compile_options_v1 *quickjs_options,
    const scxml_expr_limits *limits,
    scxml_assign_location_policy location_policy,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status scxml_assign_compile_string_literal(
    scxml_assign_program *out,
    const char *location, size_t location_size,
    const char *bytes, size_t byte_count,
    const cmeta_data_desc *root,
    const scxml_expr_limits *limits,
    scxml_assign_location_policy location_policy,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status scxml_assign_compile_external(
    scxml_assign_program *out,
    const char *location, size_t location_size,
    const char *uri, size_t uri_size,
    const cmeta_data_desc *root,
    const scxml_expr_limits *limits,
    scxml_expr_diagnostic *diagnostic);

bool scxml_assign_external_source(
    const scxml_assign_program *program,
    const char **out_uri, size_t *out_uri_size,
    const cmeta_data_desc **out_destination);

scxml_expr_status scxml_assign_apply_external(
    const scxml_assign_program *program,
    cserde_reader *reader,
    const cbind_context *context,
    void *decode_storage, size_t decode_storage_size,
    void *staged_root,
    scxml_expr_diagnostic *diagnostic);

/* Mutates only staged_root. Callers discard the whole staged object on error. */
scxml_expr_status scxml_assign_apply(
    const scxml_assign_program *program,
    void *staged_root,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status scxml_assign_apply_with_system(
    const scxml_assign_program *program,
    void *staged_root,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    const scxml_expr_system_values *system_values,
    scxml_expr_diagnostic *diagnostic);

/* Evaluates against source_root and writes only to destination_root. */
scxml_expr_status scxml_assign_apply_from_with_system(
    const scxml_assign_program *program,
    const void *source_root,
    void *destination_root,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    const scxml_expr_system_values *system_values,
    scxml_expr_diagnostic *diagnostic);

bool scxml_assign_destination_matches(
    const scxml_assign_program *program,
    const scxml_location *location);

bool scxml_assign_destination_range(
    const scxml_assign_program *program,
    size_t *out_offset, size_t *out_storage_size);

bool scxml_assign_destination_is_read_only_system(
    const scxml_assign_program *program);

void scxml_assign_program_destroy(
    scxml_assign_program *program);

#endif /* SCXML_ASSIGN_H */
