#include "scxml_scope.h"

#include <stdint.h>

bool scxml_scope_schema_init(
    scxml_scope_schema *schema, size_t slot_capacity) {
    return cmeta_scope_schema_init(
        schema, slot_capacity, SIZE_MAX, NULL);
}

void scxml_scope_schema_destroy(scxml_scope_schema *schema) {
    cmeta_scope_schema_destroy(schema);
}

const scxml_scope_slot *scxml_scope_find(
    const scxml_scope_schema *schema,
    const char *name, size_t name_size,
    size_t *out_slot) {
    return cmeta_scope_find(schema, name, name_size, out_slot);
}

bool scxml_scope_register(
    scxml_scope_schema *schema,
    const char *name, size_t name_size,
    const cmeta_data_desc *value,
    size_t *out_slot, bool *out_conflict) {
    return cmeta_scope_register(
        schema, name, name_size, value, out_slot, out_conflict);
}

const cmeta_data_desc *scxml_scope_find_data_for_type(
    const cmeta_data_desc *root, const cmeta_type_desc *type,
    size_t max_depth) {
    return cmeta_scope_find_data_for_type(root, type, max_depth);
}

bool scxml_scope_view_valid(const scxml_scope_view *view) {
    return cmeta_scope_view_valid(view);
}

bool scxml_scope_view_copy(
    scxml_scope_view *destination, const scxml_scope_view *source) {
    return cmeta_scope_view_copy(destination, source);
}

bool scxml_scope_view_move_replace(
    scxml_scope_view *destination, scxml_scope_view *source) {
    return cmeta_scope_view_move_replace(destination, source);
}

void scxml_scope_view_clear(scxml_scope_view *view) {
    cmeta_scope_view_clear(view);
}

bool scxml_scope_view_assign(
    scxml_scope_view *view, size_t slot, const void *source) {
    return cmeta_scope_view_assign(view, slot, source);
}

bool scxml_scope_view_read(
    const scxml_scope_view *view, size_t slot,
    const cmeta_data_desc **out_value, const void **out_object) {
    return cmeta_scope_view_read(view, slot, out_value, out_object);
}
