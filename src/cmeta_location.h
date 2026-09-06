#ifndef TURBO_SCXML_CMETA_LOCATION_H
#define TURBO_SCXML_CMETA_LOCATION_H

#include "cmeta_scope.h"

typedef enum cmeta_location_status {
    CMETA_LOCATION_OK = 0,
    CMETA_LOCATION_INVALID_ARGUMENT,
    CMETA_LOCATION_SYNTAX_ERROR,
    CMETA_LOCATION_UNKNOWN,
    CMETA_LOCATION_TYPE_MISMATCH,
    CMETA_LOCATION_LIMIT_EXCEEDED,
    CMETA_LOCATION_EVALUATION_ERROR
} cmeta_location_status;

typedef enum cmeta_location_kind {
    CMETA_LOCATION_ROOT = 0,
    CMETA_LOCATION_SCOPE
} cmeta_location_kind;

typedef struct cmeta_location {
    const cmeta_data_desc *root;
    const cmeta_data_desc *value;
    size_t offset;
    size_t storage_size;
    size_t slot;
    cmeta_location_kind kind;
} cmeta_location;

bool cmeta_location_path_valid(
    const char *path, size_t path_size, size_t max_depth);

cmeta_location_status cmeta_location_compile(
    cmeta_location *out,
    const char *path, size_t path_size,
    const cmeta_data_desc *root, size_t max_depth,
    size_t *out_error_offset);

cmeta_location_status cmeta_location_compile_with_scope(
    cmeta_location *out,
    const char *path, size_t path_size,
    const cmeta_data_desc *root,
    const cmeta_scope_schema *scope,
    size_t max_depth, size_t *out_error_offset);

cmeta_location_status cmeta_location_assign_owned_string(
    const cmeta_location *location, void *root,
    const char *data, size_t size, size_t max_bytes);

#endif /* TURBO_SCXML_CMETA_LOCATION_H */
