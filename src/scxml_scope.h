#ifndef TURBO_SCXML_SCOPE_H
#define TURBO_SCXML_SCOPE_H

#include <cmeta/data.h>

#include <stdbool.h>
#include <stddef.h>

typedef struct scxml_scope_slot {
    char *name;
    size_t name_size;
    const cmeta_data_desc *value;
    size_t offset;
    bool managed;
} scxml_scope_slot;

typedef struct scxml_scope_schema {
    scxml_scope_slot *slots;
    size_t slot_count;
    size_t slot_capacity;
    size_t storage_size;
    size_t storage_align;
} scxml_scope_schema;

typedef struct scxml_scope_view {
    const scxml_scope_schema *schema;
    unsigned char *storage;
    unsigned char *bound;
} scxml_scope_view;

bool scxml_scope_schema_init(
    scxml_scope_schema *schema, size_t slot_capacity);
void scxml_scope_schema_destroy(scxml_scope_schema *schema);

const scxml_scope_slot *scxml_scope_find(
    const scxml_scope_schema *schema,
    const char *name, size_t name_size,
    size_t *out_slot);

bool scxml_scope_register(
    scxml_scope_schema *schema,
    const char *name, size_t name_size,
    const cmeta_data_desc *value,
    size_t *out_slot, bool *out_conflict);

const cmeta_data_desc *scxml_scope_find_data_for_type(
    const cmeta_data_desc *root, const cmeta_type_desc *type,
    size_t max_depth);

bool scxml_scope_view_valid(const scxml_scope_view *view);
bool scxml_scope_view_copy(
    scxml_scope_view *destination, const scxml_scope_view *source);
bool scxml_scope_view_move_replace(
    scxml_scope_view *destination, scxml_scope_view *source);
void scxml_scope_view_clear(scxml_scope_view *view);
bool scxml_scope_view_assign(
    scxml_scope_view *view, size_t slot, const void *source);
bool scxml_scope_view_read(
    const scxml_scope_view *view, size_t slot,
    const cmeta_data_desc **out_value, const void **out_object);

#endif
