#ifndef TURBO_SCXML_CMETA_SCOPE_H
#define TURBO_SCXML_CMETA_SCOPE_H

#include <cmeta/data.h>

#include <stdbool.h>
#include <stddef.h>

typedef struct cmeta_scope_allocator {
    void *user;
    void *(*allocate)(void *user, size_t size);
    void *(*allocate_zero)(void *user, size_t count, size_t size);
    void (*deallocate)(void *user, void *pointer);
} cmeta_scope_allocator;

typedef struct cmeta_scope_slot {
    char *name;
    size_t name_size;
    const cmeta_data_desc *value;
    size_t offset;
    bool managed;
} cmeta_scope_slot;

typedef struct cmeta_scope_schema {
    cmeta_scope_slot *slots;
    size_t slot_count;
    size_t slot_capacity;
    size_t storage_size;
    size_t storage_align;
    size_t max_storage_bytes;
    cmeta_scope_allocator allocator;
} cmeta_scope_schema;

typedef struct cmeta_scope_view {
    const cmeta_scope_schema *schema;
    unsigned char *storage;
    unsigned char *bound;
} cmeta_scope_view;

typedef struct cmeta_scope_storage {
    cmeta_scope_view view;
    void *allocation;
    cmeta_scope_allocator allocator;
} cmeta_scope_storage;

/* Allocator operations are copied. Their user context must remain valid until
 * the matching schema or storage owner is destroyed. */
bool cmeta_scope_schema_init(
    cmeta_scope_schema *schema, size_t slot_capacity,
    size_t max_storage_bytes, const cmeta_scope_allocator *allocator);
void cmeta_scope_schema_destroy(cmeta_scope_schema *schema);

const cmeta_scope_slot *cmeta_scope_find(
    const cmeta_scope_schema *schema,
    const char *name, size_t name_size,
    size_t *out_slot);

bool cmeta_scope_register(
    cmeta_scope_schema *schema,
    const char *name, size_t name_size,
    const cmeta_data_desc *value,
    size_t *out_slot, bool *out_conflict);

const cmeta_data_desc *cmeta_scope_find_data_for_type(
    const cmeta_data_desc *root, const cmeta_type_desc *type,
    size_t max_depth);

bool cmeta_scope_storage_init(
    cmeta_scope_storage *storage, const cmeta_scope_schema *schema,
    const cmeta_scope_allocator *allocator);
/* The schema and every borrowed descriptor must outlive this storage owner. */
void cmeta_scope_storage_destroy(cmeta_scope_storage *storage);

bool cmeta_scope_view_valid(const cmeta_scope_view *view);
bool cmeta_scope_view_copy(
    cmeta_scope_view *destination, const cmeta_scope_view *source);
bool cmeta_scope_view_move_replace(
    cmeta_scope_view *destination, cmeta_scope_view *source);
void cmeta_scope_view_clear_slot(cmeta_scope_view *view, size_t slot);
void cmeta_scope_view_clear(cmeta_scope_view *view);
bool cmeta_scope_view_assign(
    cmeta_scope_view *view, size_t slot, const void *source);
bool cmeta_scope_view_read(
    const cmeta_scope_view *view, size_t slot,
    const cmeta_data_desc **out_value, const void **out_object);

#endif /* TURBO_SCXML_CMETA_SCOPE_H */
