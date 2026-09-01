#ifndef SCXML_ASSIGN_H
#define SCXML_ASSIGN_H

#include "scxml_expr.h"

typedef struct scxml_assign_program {
    void *impl;
} scxml_assign_program;

scxml_expr_status scxml_assign_compile(
    scxml_assign_program *out,
    const char *location, size_t location_size,
    const char *expression, size_t expression_size,
    const cmeta_data_desc *root,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_limits *limits,
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

void scxml_assign_program_destroy(
    scxml_assign_program *program);

#endif /* SCXML_ASSIGN_H */
