#include "scxml_location.h"

#include <stdio.h>
#include <string.h>

static scxml_expr_status location_report(
    scxml_expr_diagnostic *diagnostic,
    scxml_expr_status status, size_t byte_offset,
    const char *message) {
    if (diagnostic != NULL) {
        memset(diagnostic, 0, sizeof(*diagnostic));
        diagnostic->status = status;
        diagnostic->byte_offset = byte_offset;
        if (message != NULL)
            (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                           "%s", message);
    }
    return status;
}

static scxml_expr_status location_map_status(
    cmeta_location_status status, size_t byte_offset,
    scxml_expr_diagnostic *diagnostic) {
    switch (status) {
        case CMETA_LOCATION_OK:
            return location_report(
                diagnostic, SCXML_EXPR_OK, byte_offset, NULL);
        case CMETA_LOCATION_INVALID_ARGUMENT:
            return location_report(
                diagnostic, SCXML_EXPR_INVALID_ARGUMENT, byte_offset,
                "CMeta location schema is invalid");
        case CMETA_LOCATION_SYNTAX_ERROR:
            return location_report(
                diagnostic, SCXML_EXPR_SYNTAX_ERROR, byte_offset,
                "SCXML location is not a dotted NCName path");
        case CMETA_LOCATION_UNKNOWN:
            return location_report(
                diagnostic, SCXML_EXPR_UNKNOWN_LOCATION, byte_offset,
                "SCXML location is unresolved");
        case CMETA_LOCATION_TYPE_MISMATCH:
            return location_report(
                diagnostic, SCXML_EXPR_TYPE_MISMATCH, byte_offset,
                "SCXML location is not an owned string");
        case CMETA_LOCATION_LIMIT_EXCEEDED:
            return location_report(
                diagnostic, SCXML_EXPR_LIMIT_EXCEEDED, byte_offset,
                "SCXML location path depth limit exceeded");
        case CMETA_LOCATION_EVALUATION_ERROR:
            return location_report(
                diagnostic, SCXML_EXPR_EVALUATION_ERROR, byte_offset,
                "CMeta owned-string location assignment failed");
    }
    return location_report(
        diagnostic, SCXML_EXPR_INVALID_ARGUMENT, byte_offset,
        "invalid CMeta location status");
}

static bool location_path_has_root(
    const char *path, size_t path_size, const char *root) {
    const size_t root_size = strlen(root);
    return path_size == root_size && memcmp(path, root, root_size) == 0;
}

static bool location_path_has_object_root(
    const char *path, size_t path_size, const char *root) {
    const size_t root_size = strlen(root);
    return path_size >= root_size && memcmp(path, root, root_size) == 0 &&
           (path_size == root_size || path[root_size] == '.');
}

bool scxml_location_is_read_only_system(
    const char *path, size_t path_size, size_t max_depth) {
    if (!cmeta_location_path_valid(path, path_size, max_depth)) return false;
    return location_path_has_root(path, path_size, "_sessionid") ||
           location_path_has_root(path, path_size, "_name") ||
           location_path_has_object_root(path, path_size, "_event") ||
           location_path_has_object_root(path, path_size, "_ioprocessors");
}

static bool location_compile_arguments_valid(
    const scxml_location *out,
    const char *path, size_t path_size,
    const cmeta_data_desc *root, size_t max_depth) {
    return out != NULL && out->root == NULL && path != NULL &&
           path_size != 0u && max_depth != 0u &&
           cmeta_data_desc_valid(root) && root->kind == CMETA_DATA_STRUCT &&
           root->storage_type != NULL;
}

scxml_expr_status scxml_location_compile(
    scxml_location *out,
    const char *path, size_t path_size,
    const cmeta_data_desc *root, size_t max_depth,
    bool writable,
    scxml_expr_diagnostic *diagnostic) {
    cmeta_location_status status;
    size_t byte_offset = 0u;
    if (!location_compile_arguments_valid(
            out, path, path_size, root, max_depth))
        return location_report(
            diagnostic, SCXML_EXPR_INVALID_ARGUMENT, 0u,
            "invalid SCXML location compile arguments");
    if (writable && path[0] == '_')
        return location_report(
            diagnostic, SCXML_EXPR_UNKNOWN_LOCATION, 0u,
            "CMeta system locations are read-only");
    status = cmeta_location_compile(
        out, path, path_size, root, max_depth, &byte_offset);
    return location_map_status(status, byte_offset, diagnostic);
}

scxml_expr_status scxml_location_compile_with_scope(
    scxml_location *out,
    const char *path, size_t path_size,
    const cmeta_data_desc *root,
    const scxml_scope_schema *supplemental,
    size_t max_depth, bool writable,
    scxml_expr_diagnostic *diagnostic) {
    cmeta_location_status status;
    size_t byte_offset = 0u;
    if (!location_compile_arguments_valid(
            out, path, path_size, root, max_depth))
        return location_report(
            diagnostic, SCXML_EXPR_INVALID_ARGUMENT, 0u,
            "invalid SCXML location compile arguments");
    if (writable && path[0] == '_')
        return location_report(
            diagnostic, SCXML_EXPR_UNKNOWN_LOCATION, 0u,
            "CMeta system locations are read-only");
    status = cmeta_location_compile_with_scope(
        out, path, path_size, root, supplemental,
        max_depth, &byte_offset);
    return location_map_status(status, byte_offset, diagnostic);
}

scxml_expr_status scxml_location_assign_owned_string(
    const scxml_location *location, void *root,
    const char *data, size_t size, size_t max_bytes,
    scxml_expr_diagnostic *diagnostic) {
    const cmeta_location_status status =
        cmeta_location_assign_owned_string(
            location, root, data, size, max_bytes);
    if (status == CMETA_LOCATION_INVALID_ARGUMENT)
        return location_report(
            diagnostic, SCXML_EXPR_INVALID_ARGUMENT, 0u,
            "invalid CMeta owned-string location assignment arguments");
    return location_map_status(status, 0u, diagnostic);
}
