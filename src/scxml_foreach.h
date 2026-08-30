#ifndef SCXML_FOREACH_H
#define SCXML_FOREACH_H

#include "scxml_location.h"
#include "scxml_sequence.h"

typedef struct scxml_foreach_program {
    scxml_sequence_program sequence;
    scxml_location item;
    scxml_location index;
    size_t max_iterations;
    bool has_index;
    bool managed_item;
} scxml_foreach_program;

typedef struct scxml_foreach_value {
    void *allocation;
    void *storage;
    bool live;
} scxml_foreach_value;

scxml_expr_status scxml_foreach_compile(
    scxml_foreach_program *out,
    const char *array, size_t array_size,
    const char *item, size_t item_size,
    const char *index_or_null, size_t index_size,
    const cmeta_data_desc *root, size_t max_path_depth,
    size_t max_iterations,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status scxml_foreach_open(
    const scxml_foreach_program *program,
    void *staged_root, cmeta_range *out_range, size_t *out_length,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status scxml_foreach_value_init(
    const scxml_foreach_program *program,
    scxml_foreach_value *value,
    scxml_expr_diagnostic *diagnostic);

void scxml_foreach_value_destroy(
    const scxml_foreach_program *program,
    scxml_foreach_value *value);

scxml_expr_status scxml_foreach_next(
    const scxml_foreach_program *program,
    void *staged_root, const cmeta_range *range,
    cmeta_range_cursor *cursor, scxml_foreach_value *value,
    size_t iteration, size_t length,
    scxml_expr_diagnostic *diagnostic);

#endif
