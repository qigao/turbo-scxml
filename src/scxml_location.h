#ifndef SCXML_LOCATION_H
#define SCXML_LOCATION_H

#include "cmeta_location.h"
#include "scxml_expr.h"

typedef cmeta_location_kind scxml_location_kind;
#define SCXML_LOCATION_CMETA CMETA_LOCATION_ROOT
#define SCXML_LOCATION_SUPPLEMENTAL CMETA_LOCATION_SCOPE
typedef cmeta_location scxml_location;

bool scxml_location_is_read_only_system(
    const char *path, size_t path_size, size_t max_depth);

scxml_expr_status scxml_location_compile(
    scxml_location *out,
    const char *path, size_t path_size,
    const cmeta_data_desc *root, size_t max_depth,
    bool writable,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status scxml_location_compile_with_scope(
    scxml_location *out,
    const char *path, size_t path_size,
    const cmeta_data_desc *root,
    const scxml_scope_schema *supplemental,
    size_t max_depth, bool writable,
    scxml_expr_diagnostic *diagnostic);

scxml_expr_status
scxml_location_assign_owned_string(
    const scxml_location *location, void *root,
    const char *data, size_t size, size_t max_bytes,
    scxml_expr_diagnostic *diagnostic);

#endif
