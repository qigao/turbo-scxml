#include "scxml_location.h"

#include <stdint.h>
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

static bool location_decode_utf8(const char *data, size_t size,
                                 size_t *cursor, uint32_t *out_codepoint) {
    const size_t start = *cursor;
    const unsigned char lead = (unsigned char)data[start];
    size_t width;
    size_t index;
    uint32_t value;
    if (lead <= 0x7fu) {
        *out_codepoint = lead;
        *cursor = start + 1u;
        return true;
    }
    if (lead >= 0xc2u && lead <= 0xdfu) {
        width = 2u;
        value = lead & 0x1fu;
    } else if (lead >= 0xe0u && lead <= 0xefu) {
        width = 3u;
        value = lead & 0x0fu;
    } else if (lead >= 0xf0u && lead <= 0xf4u) {
        width = 4u;
        value = lead & 0x07u;
    } else {
        return false;
    }
    if (width > size - start) return false;
    for (index = 1u; index < width; ++index) {
        const unsigned char continuation = (unsigned char)data[start + index];
        if ((continuation & 0xc0u) != 0x80u) return false;
        value = (value << 6u) | (continuation & 0x3fu);
    }
    if ((width == 3u && value < 0x800u) ||
        (width == 4u && value < 0x10000u) ||
        (value >= 0xd800u && value <= 0xdfffu) || value > 0x10ffffu)
        return false;
    *out_codepoint = value;
    *cursor = start + width;
    return true;
}

static bool location_ncname_start(uint32_t codepoint) {
    return codepoint == '_' || (codepoint >= 'A' && codepoint <= 'Z') ||
           (codepoint >= 'a' && codepoint <= 'z') ||
           (codepoint >= 0xc0u && codepoint <= 0xd6u) ||
           (codepoint >= 0xd8u && codepoint <= 0xf6u) ||
           (codepoint >= 0xf8u && codepoint <= 0x2ffu) ||
           (codepoint >= 0x370u && codepoint <= 0x37du) ||
           (codepoint >= 0x37fu && codepoint <= 0x1fffu) ||
           (codepoint >= 0x200cu && codepoint <= 0x200du) ||
           (codepoint >= 0x2070u && codepoint <= 0x218fu) ||
           (codepoint >= 0x2c00u && codepoint <= 0x2fefu) ||
           (codepoint >= 0x3001u && codepoint <= 0xd7ffu) ||
           (codepoint >= 0xf900u && codepoint <= 0xfdcfu) ||
           (codepoint >= 0xfdf0u && codepoint <= 0xfffdu) ||
           (codepoint >= 0x10000u && codepoint <= 0xeffffu);
}

static bool location_ncname_continue(uint32_t codepoint) {
    return location_ncname_start(codepoint) || codepoint == '-' ||
           (codepoint >= '0' && codepoint <= '9') || codepoint == 0xb7u ||
           (codepoint >= 0x300u && codepoint <= 0x36fu) ||
           (codepoint >= 0x203fu && codepoint <= 0x2040u);
}

static bool location_path_valid(const char *path, size_t path_size,
                                size_t *out_error_offset) {
    size_t cursor = 0u;
    bool segment_start = true;
    while (cursor < path_size) {
        uint32_t codepoint;
        const size_t offset = cursor;
        if (path[cursor] == '.') {
            if (segment_start) {
                *out_error_offset = cursor;
                return false;
            }
            segment_start = true;
            ++cursor;
            continue;
        }
        if (!location_decode_utf8(path, path_size, &cursor, &codepoint) ||
            (segment_start ? !location_ncname_start(codepoint)
                           : !location_ncname_continue(codepoint))) {
            *out_error_offset = offset;
            return false;
        }
        segment_start = false;
    }
    if (segment_start) {
        *out_error_offset = path_size;
        return false;
    }
    return true;
}

static bool location_path_has_root(
    const char *path, size_t path_size, const char *root) {
    const size_t root_size = strlen(root);
    return path_size == root_size &&
           memcmp(path, root, root_size) == 0;
}

static bool location_path_has_object_root(
    const char *path, size_t path_size, const char *root) {
    const size_t root_size = strlen(root);
    return path_size >= root_size &&
           memcmp(path, root, root_size) == 0 &&
           (path_size == root_size || path[root_size] == '.');
}

bool scxml_location_is_read_only_system(
    const char *path, size_t path_size, size_t max_depth) {
    size_t lexical_error = 0u;
    size_t depth = 1u;
    size_t index;
    if (path == NULL || path_size == 0u || max_depth == 0u ||
        !location_path_valid(path, path_size, &lexical_error))
        return false;
    for (index = 0u; index < path_size; ++index) {
        if (path[index] == '.' && ++depth > max_depth) return false;
    }
    return location_path_has_root(path, path_size, "_sessionid") ||
           location_path_has_root(path, path_size, "_name") ||
           location_path_has_object_root(path, path_size, "_event") ||
           location_path_has_object_root(
               path, path_size, "_ioprocessors");
}

static const cmeta_data_field_desc *location_find_field(
    const cmeta_data_struct_shape *shape,
    const char *name, size_t name_size) {
    size_t index;
    if (shape == NULL || name == NULL) return NULL;
    for (index = 0u; index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &shape->fields[index];
        if (field->name != NULL && strlen(field->name) == name_size &&
            memcmp(field->name, name, name_size) == 0)
            return field;
    }
    return NULL;
}

static scxml_expr_status location_compile_root(
    scxml_location *out,
    const char *path, size_t path_size,
    const cmeta_data_desc *root, size_t max_depth,
    bool writable,
    scxml_expr_diagnostic *diagnostic) {
    const cmeta_data_desc *current = root;
    size_t absolute_offset = 0u;
    size_t segment_start = 0u;
    size_t depth = 0u;
    size_t index;
    size_t lexical_error = 0u;
    scxml_location compiled = {0};
    if (out == NULL || out->root != NULL || path == NULL ||
        path_size == 0u || max_depth == 0u ||
        !cmeta_data_desc_valid(root) || root->kind != CMETA_DATA_STRUCT ||
        root->storage_type == NULL)
        return location_report(diagnostic,
                               SCXML_EXPR_INVALID_ARGUMENT, 0u,
                               "invalid SCXML location compile arguments");
    if (writable && path[0] == '_')
        return location_report(diagnostic,
                               SCXML_EXPR_UNKNOWN_LOCATION, 0u,
                               "CMeta system locations are read-only");
    if (!location_path_valid(path, path_size, &lexical_error))
        return location_report(
            diagnostic, SCXML_EXPR_SYNTAX_ERROR, lexical_error,
            "SCXML location is not a dotted NCName path");
    for (index = 0u; index <= path_size; ++index) {
        const bool at_end = index == path_size;
        const cmeta_data_struct_shape *shape;
        const cmeta_data_field_desc *field;
        if (!at_end && path[index] != '.') continue;
        if (++depth > max_depth)
            return location_report(
                diagnostic, SCXML_EXPR_LIMIT_EXCEEDED, index,
                "SCXML location path depth limit exceeded");
        if (!cmeta_data_desc_valid(current))
            return location_report(
                diagnostic, SCXML_EXPR_INVALID_ARGUMENT,
                segment_start, "CMeta location schema is invalid");
        if (current->kind != CMETA_DATA_STRUCT)
            return location_report(
                diagnostic, SCXML_EXPR_UNKNOWN_LOCATION,
                segment_start, "SCXML location traverses a non-struct value");
        if (current->shape == NULL || current->storage_type == NULL)
            return location_report(
                diagnostic, SCXML_EXPR_INVALID_ARGUMENT,
                segment_start, "CMeta location schema is invalid");
        shape = (const cmeta_data_struct_shape *)current->shape;
        field = location_find_field(
            shape, path + segment_start, index - segment_start);
        if (field == NULL)
            return location_report(
                diagnostic, SCXML_EXPR_UNKNOWN_LOCATION,
                segment_start, "SCXML location is unresolved");
        if (!cmeta_data_desc_valid(field->value) ||
            field->offset > current->storage_type->size ||
            (field->value->storage_type != NULL &&
             field->value->storage_type->size >
                 current->storage_type->size - field->offset) ||
            absolute_offset > SIZE_MAX - field->offset)
            return location_report(
                diagnostic, SCXML_EXPR_INVALID_ARGUMENT,
                segment_start, "CMeta location schema is invalid");
        if (field->value->storage_type == NULL)
            return location_report(
                diagnostic, SCXML_EXPR_UNKNOWN_LOCATION,
                segment_start,
                "SCXML location does not expose addressable storage");
        absolute_offset += field->offset;
        current = field->value;
        if (at_end) break;
        segment_start = index + 1u;
    }
    if (absolute_offset > root->storage_type->size ||
        current->storage_type->size >
            root->storage_type->size - absolute_offset)
        return location_report(
            diagnostic, SCXML_EXPR_INVALID_ARGUMENT, 0u,
            "CMeta location schema exceeds root storage");
    compiled.root = root;
    compiled.value = current;
    compiled.offset = absolute_offset;
    compiled.storage_size = current->storage_type->size;
    compiled.slot = SIZE_MAX;
    compiled.kind = SCXML_LOCATION_CMETA;
    *out = compiled;
    return location_report(diagnostic, SCXML_EXPR_OK, 0u, NULL);
}

scxml_expr_status scxml_location_compile(
    scxml_location *out,
    const char *path, size_t path_size,
    const cmeta_data_desc *root, size_t max_depth,
    bool writable,
    scxml_expr_diagnostic *diagnostic) {
    return location_compile_root(
        out, path, path_size, root, max_depth, writable, diagnostic);
}

scxml_expr_status scxml_location_compile_with_scope(
    scxml_location *out,
    const char *path, size_t path_size,
    const cmeta_data_desc *root,
    const scxml_scope_schema *supplemental,
    size_t max_depth, bool writable,
    scxml_expr_diagnostic *diagnostic) {
    scxml_expr_status status;
    const scxml_scope_slot *slot;
    const cmeta_data_desc *current;
    size_t slot_index = SIZE_MAX;
    size_t absolute_offset = 0u;
    size_t segment_start = 0u;
    size_t first_end = 0u;
    size_t depth = 0u;
    size_t index;
    size_t lexical_error = 0u;
    scxml_location compiled = {0};
    status = location_compile_root(
        out, path, path_size, root, max_depth, writable, diagnostic);
    if (status != SCXML_EXPR_UNKNOWN_LOCATION || supplemental == NULL)
        return status;
    if (!location_path_valid(path, path_size, &lexical_error))
        return location_report(
            diagnostic, SCXML_EXPR_SYNTAX_ERROR, lexical_error,
            "SCXML location is not a dotted NCName path");
    while (first_end < path_size && path[first_end] != '.') ++first_end;
    slot = scxml_scope_find(
        supplemental, path, first_end, &slot_index);
    if (slot == NULL)
        return location_report(
            diagnostic, SCXML_EXPR_UNKNOWN_LOCATION, 0u,
            "SCXML location is unresolved");
    current = slot->value;
    segment_start = first_end + (first_end < path_size ? 1u : 0u);
    depth = 1u;
    for (index = segment_start; index <= path_size && first_end < path_size;
         ++index) {
        const bool at_end = index == path_size;
        const cmeta_data_struct_shape *shape;
        const cmeta_data_field_desc *field;
        if (!at_end && path[index] != '.') continue;
        if (++depth > max_depth)
            return location_report(
                diagnostic, SCXML_EXPR_LIMIT_EXCEEDED, index,
                "SCXML location path depth limit exceeded");
        if (!cmeta_data_desc_valid(current) ||
            current->kind != CMETA_DATA_STRUCT || current->shape == NULL ||
            current->storage_type == NULL)
            return location_report(
                diagnostic, SCXML_EXPR_UNKNOWN_LOCATION, segment_start,
                "SCXML location traverses a non-struct value");
        shape = (const cmeta_data_struct_shape *)current->shape;
        field = location_find_field(
            shape, path + segment_start, index - segment_start);
        if (field == NULL || !cmeta_data_desc_valid(field->value) ||
            field->value->storage_type == NULL ||
            field->offset > current->storage_type->size ||
            field->value->storage_type->size >
                current->storage_type->size - field->offset ||
            absolute_offset > SIZE_MAX - field->offset)
            return location_report(
                diagnostic, SCXML_EXPR_UNKNOWN_LOCATION, segment_start,
                "SCXML location is unresolved");
        absolute_offset += field->offset;
        if (absolute_offset > slot->value->storage_type->size ||
            field->value->storage_type->size >
                slot->value->storage_type->size - absolute_offset)
            return location_report(
                diagnostic, SCXML_EXPR_INVALID_ARGUMENT, segment_start,
                "supplemental location exceeds slot storage");
        current = field->value;
        segment_start = index + 1u;
    }
    compiled.root = root;
    compiled.value = current;
    compiled.offset = absolute_offset;
    compiled.storage_size = current->storage_type->size;
    compiled.slot = slot_index;
    compiled.kind = SCXML_LOCATION_SUPPLEMENTAL;
    *out = compiled;
    return location_report(diagnostic, SCXML_EXPR_OK, 0u, NULL);
}

scxml_expr_status
scxml_location_assign_owned_string(
    const scxml_location *location, void *root,
    const char *data, size_t size, size_t max_bytes,
    scxml_expr_diagnostic *diagnostic) {
    const cmeta_data_buffer_ops *ops;
    unsigned char *destination;
    cmeta_status status;
    if (location == NULL || root == NULL ||
        location->kind != SCXML_LOCATION_CMETA ||
        (size != 0u && data == NULL) ||
        !cmeta_data_desc_valid(location->root) ||
        !cmeta_data_desc_valid(location->value) ||
        location->value->kind != CMETA_DATA_STRING ||
        location->offset > location->root->storage_type->size ||
        location->storage_size >
            location->root->storage_type->size - location->offset)
        return location_report(
            diagnostic, SCXML_EXPR_INVALID_ARGUMENT, 0u,
            "invalid CMeta owned-string location assignment arguments");
    ops = cmeta_data_buffer_ops_of(location->value);
    if (ops == NULL || ops->ownership != CMETA_DATA_BUFFER_OWNED)
        return location_report(
            diagnostic, SCXML_EXPR_TYPE_MISMATCH, 0u,
            "SCXML location is not an owned string");
    destination = (unsigned char *)root + location->offset;
    status = cmeta_data_buffer_restore_zero(location->value, destination);
    if (status == CMETA_OK)
        status = cmeta_data_buffer_assign(
            location->value, destination, (const unsigned char *)data,
            size, max_bytes);
    return status == CMETA_OK
        ? location_report(
              diagnostic, SCXML_EXPR_OK, 0u, NULL)
        : location_report(
              diagnostic, SCXML_EXPR_EVALUATION_ERROR, 0u,
              "CMeta owned-string location assignment failed");
}
