#include "cmeta_location.h"

#include <stdint.h>
#include <string.h>

static cmeta_location_status location_result(
    cmeta_location_status status, size_t byte_offset,
    cmeta_location_failure failure,
    cmeta_location_diagnostic *diagnostic) {
    if (diagnostic != NULL) {
        diagnostic->byte_offset = byte_offset;
        diagnostic->failure = failure;
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

static bool location_path_valid_detailed(
    const char *path, size_t path_size, size_t *out_error_offset) {
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

bool cmeta_location_path_valid(
    const char *path, size_t path_size, size_t max_depth) {
    size_t error_offset = 0u;
    size_t depth = 1u;
    size_t index;
    if (path == NULL || path_size == 0u || max_depth == 0u ||
        !location_path_valid_detailed(path, path_size, &error_offset))
        return false;
    for (index = 0u; index < path_size; ++index)
        if (path[index] == '.' && ++depth > max_depth) return false;
    return true;
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

cmeta_location_status cmeta_location_compile_detailed(
    cmeta_location *out,
    const char *path, size_t path_size,
    const cmeta_data_desc *root, size_t max_depth,
    cmeta_location_diagnostic *diagnostic) {
    const cmeta_data_desc *current = root;
    size_t absolute_offset = 0u;
    size_t segment_start = 0u;
    size_t depth = 0u;
    size_t index;
    size_t lexical_error = 0u;
    cmeta_location compiled = {0};
    if (out == NULL || out->root != NULL || path == NULL ||
        path_size == 0u || max_depth == 0u ||
        !cmeta_data_desc_valid(root) || root->kind != CMETA_DATA_STRUCT ||
        root->storage_type == NULL)
        return location_result(
            CMETA_LOCATION_INVALID_ARGUMENT, 0u,
            CMETA_LOCATION_FAILURE_INVALID_ARGUMENT, diagnostic);
    if (!location_path_valid_detailed(path, path_size, &lexical_error))
        return location_result(
            CMETA_LOCATION_SYNTAX_ERROR, lexical_error,
            CMETA_LOCATION_FAILURE_SYNTAX, diagnostic);
    for (index = 0u; index <= path_size; ++index) {
        const bool at_end = index == path_size;
        const cmeta_data_struct_shape *shape;
        const cmeta_data_field_desc *field;
        if (!at_end && path[index] != '.') continue;
        if (++depth > max_depth)
            return location_result(
                CMETA_LOCATION_LIMIT_EXCEEDED, index,
                CMETA_LOCATION_FAILURE_DEPTH, diagnostic);
        if (!cmeta_data_desc_valid(current))
            return location_result(
                CMETA_LOCATION_INVALID_ARGUMENT,
                segment_start, CMETA_LOCATION_FAILURE_INVALID_SCHEMA,
                diagnostic);
        if (current->kind != CMETA_DATA_STRUCT)
            return location_result(
                CMETA_LOCATION_UNKNOWN, segment_start,
                CMETA_LOCATION_FAILURE_NON_STRUCT, diagnostic);
        if (current->shape == NULL || current->storage_type == NULL)
            return location_result(
                CMETA_LOCATION_INVALID_ARGUMENT,
                segment_start, CMETA_LOCATION_FAILURE_INVALID_SCHEMA,
                diagnostic);
        shape = (const cmeta_data_struct_shape *)current->shape;
        field = location_find_field(
            shape, path + segment_start, index - segment_start);
        if (field == NULL)
            return location_result(
                CMETA_LOCATION_UNKNOWN, segment_start,
                CMETA_LOCATION_FAILURE_UNRESOLVED, diagnostic);
        if (!cmeta_data_desc_valid(field->value) ||
            field->offset > current->storage_type->size ||
            (field->value->storage_type != NULL &&
             field->value->storage_type->size >
                 current->storage_type->size - field->offset) ||
            absolute_offset > SIZE_MAX - field->offset)
            return location_result(
                CMETA_LOCATION_INVALID_ARGUMENT,
                segment_start, CMETA_LOCATION_FAILURE_INVALID_SCHEMA,
                diagnostic);
        if (field->value->storage_type == NULL)
            return location_result(
                CMETA_LOCATION_UNKNOWN, segment_start,
                CMETA_LOCATION_FAILURE_UNADDRESSABLE, diagnostic);
        absolute_offset += field->offset;
        current = field->value;
        if (at_end) break;
        segment_start = index + 1u;
    }
    if (absolute_offset > root->storage_type->size ||
        current->storage_type->size >
            root->storage_type->size - absolute_offset)
        return location_result(
            CMETA_LOCATION_INVALID_ARGUMENT, 0u,
            CMETA_LOCATION_FAILURE_ROOT_BOUNDS, diagnostic);
    compiled.root = root;
    compiled.value = current;
    compiled.offset = absolute_offset;
    compiled.storage_size = current->storage_type->size;
    compiled.slot = SIZE_MAX;
    compiled.kind = CMETA_LOCATION_ROOT;
    *out = compiled;
    return location_result(
        CMETA_LOCATION_OK, 0u, CMETA_LOCATION_FAILURE_NONE, diagnostic);
}

cmeta_location_status cmeta_location_compile(
    cmeta_location *out,
    const char *path, size_t path_size,
    const cmeta_data_desc *root, size_t max_depth,
    size_t *out_error_offset) {
    cmeta_location_diagnostic diagnostic = {0};
    const cmeta_location_status status = cmeta_location_compile_detailed(
        out, path, path_size, root, max_depth, &diagnostic);
    if (out_error_offset != NULL)
        *out_error_offset = diagnostic.byte_offset;
    return status;
}

cmeta_location_status cmeta_location_compile_with_scope_detailed(
    cmeta_location *out,
    const char *path, size_t path_size,
    const cmeta_data_desc *root,
    const cmeta_scope_schema *scope,
    size_t max_depth, cmeta_location_diagnostic *diagnostic) {
    cmeta_location_status status;
    const cmeta_scope_slot *slot;
    const cmeta_data_desc *current;
    size_t slot_index = SIZE_MAX;
    size_t absolute_offset = 0u;
    size_t segment_start;
    size_t first_end = 0u;
    size_t depth = 1u;
    size_t index;
    size_t lexical_error = 0u;
    cmeta_location_diagnostic local_diagnostic = {0};
    cmeta_location_diagnostic *detail =
        diagnostic == NULL ? &local_diagnostic : diagnostic;
    cmeta_location compiled = {0};
    status = cmeta_location_compile_detailed(
        out, path, path_size, root, max_depth, detail);
    if (status != CMETA_LOCATION_UNKNOWN || scope == NULL) return status;
    if (detail->failure != CMETA_LOCATION_FAILURE_UNRESOLVED)
        return status;
    if (!location_path_valid_detailed(path, path_size, &lexical_error))
        return location_result(
            CMETA_LOCATION_SYNTAX_ERROR, lexical_error,
            CMETA_LOCATION_FAILURE_SYNTAX, diagnostic);
    while (first_end < path_size && path[first_end] != '.') ++first_end;
    slot = cmeta_scope_find(scope, path, first_end, &slot_index);
    if (slot == NULL)
        return location_result(
            CMETA_LOCATION_UNKNOWN, 0u,
            CMETA_LOCATION_FAILURE_UNRESOLVED, diagnostic);
    current = slot->value;
    segment_start = first_end + (first_end < path_size ? 1u : 0u);
    for (index = segment_start; index <= path_size && first_end < path_size;
         ++index) {
        const bool at_end = index == path_size;
        const cmeta_data_struct_shape *shape;
        const cmeta_data_field_desc *field;
        if (!at_end && path[index] != '.') continue;
        if (++depth > max_depth)
            return location_result(
                CMETA_LOCATION_LIMIT_EXCEEDED, index,
                CMETA_LOCATION_FAILURE_DEPTH, diagnostic);
        if (!cmeta_data_desc_valid(current) ||
            current->kind != CMETA_DATA_STRUCT || current->shape == NULL ||
            current->storage_type == NULL)
            return location_result(
                CMETA_LOCATION_UNKNOWN, segment_start,
                CMETA_LOCATION_FAILURE_NON_STRUCT, diagnostic);
        shape = (const cmeta_data_struct_shape *)current->shape;
        field = location_find_field(
            shape, path + segment_start, index - segment_start);
        if (field == NULL || !cmeta_data_desc_valid(field->value) ||
            field->offset > current->storage_type->size ||
            absolute_offset > SIZE_MAX - field->offset)
            return location_result(
                CMETA_LOCATION_UNKNOWN, segment_start,
                CMETA_LOCATION_FAILURE_UNRESOLVED, diagnostic);
        if (field->value->storage_type == NULL)
            return location_result(
                CMETA_LOCATION_UNKNOWN, segment_start,
                CMETA_LOCATION_FAILURE_UNADDRESSABLE, diagnostic);
        if (field->value->storage_type->size >
                current->storage_type->size - field->offset)
            return location_result(
                CMETA_LOCATION_UNKNOWN, segment_start,
                CMETA_LOCATION_FAILURE_UNRESOLVED, diagnostic);
        absolute_offset += field->offset;
        if (absolute_offset > slot->value->storage_type->size ||
            field->value->storage_type->size >
                slot->value->storage_type->size - absolute_offset)
            return location_result(
                CMETA_LOCATION_INVALID_ARGUMENT,
                segment_start, CMETA_LOCATION_FAILURE_SCOPE_BOUNDS,
                diagnostic);
        current = field->value;
        segment_start = index + 1u;
    }
    compiled.root = root;
    compiled.value = current;
    compiled.offset = absolute_offset;
    compiled.storage_size = current->storage_type->size;
    compiled.slot = slot_index;
    compiled.kind = CMETA_LOCATION_SCOPE;
    *out = compiled;
    return location_result(
        CMETA_LOCATION_OK, 0u, CMETA_LOCATION_FAILURE_NONE, diagnostic);
}

cmeta_location_status cmeta_location_compile_with_scope(
    cmeta_location *out,
    const char *path, size_t path_size,
    const cmeta_data_desc *root,
    const cmeta_scope_schema *scope,
    size_t max_depth, size_t *out_error_offset) {
    cmeta_location_diagnostic diagnostic = {0};
    const cmeta_location_status status =
        cmeta_location_compile_with_scope_detailed(
            out, path, path_size, root, scope, max_depth, &diagnostic);
    if (out_error_offset != NULL)
        *out_error_offset = diagnostic.byte_offset;
    return status;
}

cmeta_location_status cmeta_location_assign_owned_string(
    const cmeta_location *location, void *root,
    const char *data, size_t size, size_t max_bytes) {
    const cmeta_data_buffer_ops *ops;
    unsigned char *destination;
    cmeta_status status;
    if (location == NULL || root == NULL ||
        location->kind != CMETA_LOCATION_ROOT ||
        (size != 0u && data == NULL) ||
        !cmeta_data_desc_valid(location->root) ||
        !cmeta_data_desc_valid(location->value) ||
        location->value->kind != CMETA_DATA_STRING ||
        location->offset > location->root->storage_type->size ||
        location->storage_size >
            location->root->storage_type->size - location->offset)
        return CMETA_LOCATION_INVALID_ARGUMENT;
    ops = cmeta_data_buffer_ops_of(location->value);
    if (ops == NULL || ops->ownership != CMETA_DATA_BUFFER_OWNED)
        return CMETA_LOCATION_TYPE_MISMATCH;
    destination = (unsigned char *)root + location->offset;
    status = cmeta_data_buffer_restore_zero(location->value, destination);
    if (status == CMETA_OK)
        status = cmeta_data_buffer_assign(
            location->value, destination, (const unsigned char *)data,
            size, max_bytes);
    return status == CMETA_OK
        ? CMETA_LOCATION_OK : CMETA_LOCATION_EVALUATION_ERROR;
}
