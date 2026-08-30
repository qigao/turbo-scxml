#ifndef SCXML_SEQUENCE_H
#define SCXML_SEQUENCE_H

#include "scxml_expr.h"

#include <cmeta/range.h>

typedef struct scxml_sequence_program {
    const cmeta_data_desc *root;
    const cmeta_type_desc *container_type;
    const cmeta_type_desc *element_type;
    size_t offset;
    size_t storage_size;
} scxml_sequence_program;

scxml_expr_status scxml_sequence_compile(
    scxml_sequence_program *out,
    const char *location, size_t location_size,
    const cmeta_data_desc *root, size_t max_path_depth,
    scxml_expr_diagnostic *diagnostic);

/**
 * Open one borrowed sequence Range and snapshot its current length.
 *
 * The Range and every value produced by it remain governed by the provider's
 * source lifetime and version contract. Failure leaves both outputs unchanged.
 */
scxml_expr_status scxml_sequence_open(
    const scxml_sequence_program *program,
    const void *root_object, cmeta_range *out_range, size_t *out_length,
    scxml_expr_diagnostic *diagnostic);

#endif /* SCXML_SEQUENCE_H */
