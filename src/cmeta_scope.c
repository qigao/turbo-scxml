#include "cmeta_scope.h"

#include <cmeta/cmeta.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void *scope_default_allocate(void *user, size_t size) {
    (void)user;
    return malloc(size);
}

static void *scope_default_allocate_zero(
    void *user, size_t count, size_t size) {
    (void)user;
    return calloc(count, size);
}

static void scope_default_deallocate(void *user, void *pointer) {
    (void)user;
    free(pointer);
}

static const cmeta_scope_allocator scope_default_allocator = {
    NULL,
    scope_default_allocate,
    scope_default_allocate_zero,
    scope_default_deallocate};

static bool scope_allocator_valid(const cmeta_scope_allocator *allocator) {
    return allocator != NULL && allocator->allocate != NULL &&
           allocator->allocate_zero != NULL && allocator->deallocate != NULL;
}

static const cmeta_scope_allocator *scope_select_allocator(
    const cmeta_scope_allocator *allocator) {
    return allocator == NULL ? &scope_default_allocator : allocator;
}

static bool scope_type_supported(
    const cmeta_type_desc *type, bool *out_managed) {
    if (!cmeta_type_desc_valid(type) || type->size == 0u ||
        type->align == 0u ||
        (type->align & (type->align - 1u)) != 0u)
        return false;
    if (cmeta_type_require_traits(
            type, CMETA_TRAIT_TRIVIAL_COPY |
                      CMETA_TRAIT_TRIVIAL_DESTROY) == CMETA_OK) {
        *out_managed = false;
        return true;
    }
    if (cmeta_type_require_traits(
            type, CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                      CMETA_TRAIT_DESTROY) != CMETA_OK)
        return false;
    *out_managed = true;
    return true;
}

bool cmeta_scope_schema_init(
    cmeta_scope_schema *schema, size_t slot_capacity,
    size_t max_storage_bytes, const cmeta_scope_allocator *allocator) {
    const cmeta_scope_allocator *selected =
        scope_select_allocator(allocator);
    if (schema == NULL || schema->slots != NULL ||
        schema->slot_count != 0u || schema->slot_capacity != 0u ||
        schema->storage_size != 0u || schema->storage_align != 0u ||
        schema->max_storage_bytes != 0u ||
        schema->frozen ||
        schema->allocator.user != NULL ||
        schema->allocator.allocate != NULL ||
        schema->allocator.allocate_zero != NULL ||
        schema->allocator.deallocate != NULL ||
        !scope_allocator_valid(selected) ||
        (slot_capacity != 0u && max_storage_bytes == 0u) ||
        slot_capacity > SIZE_MAX / sizeof(*schema->slots))
        return false;
    if (slot_capacity == 0u) return true;
    schema->slots = (cmeta_scope_slot *)selected->allocate_zero(
        selected->user, slot_capacity, sizeof(*schema->slots));
    if (schema->slots == NULL) return false;
    schema->storage_align = 1u;
    schema->slot_capacity = slot_capacity;
    schema->max_storage_bytes = max_storage_bytes;
    schema->allocator = *selected;
    return true;
}

void cmeta_scope_schema_destroy(cmeta_scope_schema *schema) {
    size_t index;
    if (schema == NULL) return;
    if (scope_allocator_valid(&schema->allocator)) {
        for (index = 0u; index < schema->slot_count; ++index)
            schema->allocator.deallocate(
                schema->allocator.user, schema->slots[index].name);
        schema->allocator.deallocate(
            schema->allocator.user, schema->slots);
    }
    memset(schema, 0, sizeof(*schema));
}

const cmeta_scope_slot *cmeta_scope_find(
    const cmeta_scope_schema *schema,
    const char *name, size_t name_size,
    size_t *out_slot) {
    size_t index;
    if (out_slot != NULL) *out_slot = SIZE_MAX;
    if (schema == NULL || (name_size != 0u && name == NULL)) return NULL;
    for (index = 0u; index < schema->slot_count; ++index) {
        const cmeta_scope_slot *slot = &schema->slots[index];
        if (slot->name_size == name_size &&
            memcmp(slot->name, name, name_size) == 0) {
            if (out_slot != NULL) *out_slot = index;
            return slot;
        }
    }
    return NULL;
}

bool cmeta_scope_register(
    cmeta_scope_schema *schema,
    const char *name, size_t name_size,
    const cmeta_data_desc *value,
    size_t *out_slot, bool *out_conflict) {
    const cmeta_scope_slot *existing;
    const cmeta_type_desc *type;
    cmeta_scope_slot *slot;
    size_t aligned;
    bool managed;
    if (out_slot != NULL) *out_slot = SIZE_MAX;
    if (out_conflict != NULL) *out_conflict = false;
    if (schema == NULL || name == NULL || name_size == 0u ||
        out_slot == NULL || out_conflict == NULL ||
        schema->frozen ||
        !scope_allocator_valid(&schema->allocator) ||
        !cmeta_data_desc_valid(value) || value->storage_type == NULL)
        return false;
    existing = cmeta_scope_find(schema, name, name_size, out_slot);
    if (existing != NULL) {
        if (!cmeta_type_equal(
                existing->value->storage_type, value->storage_type))
            *out_conflict = true;
        return !*out_conflict;
    }
    if (schema->slot_count >= schema->slot_capacity ||
        !scope_type_supported(value->storage_type, &managed))
        return false;
    type = value->storage_type;
    if (schema->storage_size > SIZE_MAX - (type->align - 1u)) return false;
    aligned = (schema->storage_size + type->align - 1u) &
              ~(type->align - 1u);
    if (aligned > SIZE_MAX - type->size ||
        aligned + type->size > schema->max_storage_bytes)
        return false;
    slot = &schema->slots[schema->slot_count];
    slot->name = (char *)schema->allocator.allocate(
        schema->allocator.user, name_size);
    if (slot->name == NULL) return false;
    memcpy(slot->name, name, name_size);
    slot->name_size = name_size;
    slot->value = value;
    slot->offset = aligned;
    slot->managed = managed;
    schema->storage_size = aligned + type->size;
    if (type->align > schema->storage_align)
        schema->storage_align = type->align;
    *out_slot = schema->slot_count++;
    return true;
}

static const cmeta_data_desc *scope_builtin_data(
    const cmeta_type_desc *type) {
    if (cmeta_type_equal(type, &cmeta_type_bool)) return &cmeta_data_bool;
    if (cmeta_type_equal(type, &cmeta_type_int)) return &cmeta_data_int;
    if (cmeta_type_equal(type, &cmeta_type_long)) return &cmeta_data_long;
    if (cmeta_type_equal(type, &cmeta_type_size)) return &cmeta_data_size;
    if (cmeta_type_equal(type, &cmeta_type_float)) return &cmeta_data_float;
    if (cmeta_type_equal(type, &cmeta_type_double)) return &cmeta_data_double;
    return NULL;
}

static const cmeta_data_desc *scope_find_data_recursive(
    const cmeta_data_desc *current, const cmeta_type_desc *type,
    size_t depth, size_t max_depth) {
    const cmeta_data_struct_shape *shape;
    size_t index;
    if (!cmeta_data_desc_valid(current)) return NULL;
    if (current->storage_type != NULL &&
        cmeta_type_equal(current->storage_type, type))
        return current;
    if (depth >= max_depth || current->kind != CMETA_DATA_STRUCT ||
        current->shape == NULL)
        return NULL;
    shape = (const cmeta_data_struct_shape *)current->shape;
    for (index = 0u; index < shape->field_count; ++index) {
        const cmeta_data_desc *found = scope_find_data_recursive(
            shape->fields[index].value, type, depth + 1u, max_depth);
        if (found != NULL) return found;
    }
    return NULL;
}

const cmeta_data_desc *cmeta_scope_find_data_for_type(
    const cmeta_data_desc *root, const cmeta_type_desc *type,
    size_t max_depth) {
    const cmeta_data_desc *builtin;
    if (!cmeta_type_desc_valid(type) || max_depth == 0u) return NULL;
    builtin = scope_builtin_data(type);
    return builtin != NULL
        ? builtin : scope_find_data_recursive(root, type, 0u, max_depth);
}

static bool scope_storage_init(
    cmeta_scope_storage *storage, const cmeta_scope_schema *schema,
    const cmeta_scope_allocator *allocator) {
    const cmeta_scope_allocator *selected =
        scope_select_allocator(allocator);
    size_t allocation_size;
    uintptr_t address;
    uintptr_t aligned;
    if (storage == NULL || schema == NULL ||
        storage->view.schema != NULL || storage->allocation != NULL ||
        storage->view.storage != NULL || storage->view.bound != NULL ||
        storage->allocator.user != NULL ||
        storage->allocator.allocate != NULL ||
        storage->allocator.allocate_zero != NULL ||
        storage->allocator.deallocate != NULL ||
        !scope_allocator_valid(selected))
        return false;
    if (schema->slot_count == 0u) {
        storage->view.schema = schema;
        storage->allocator = *selected;
        return true;
    }
    if (schema->slots == NULL || schema->storage_size == 0u ||
        schema->storage_align == 0u ||
        schema->storage_size > SIZE_MAX - (schema->storage_align - 1u) ||
        schema->storage_size + schema->storage_align - 1u >
            SIZE_MAX - schema->slot_count)
        return false;
    allocation_size = schema->storage_size + schema->storage_align - 1u +
                      schema->slot_count;
    storage->allocation = selected->allocate_zero(
        selected->user, 1u, allocation_size);
    if (storage->allocation == NULL) return false;
    address = (uintptr_t)storage->allocation;
    if (address > UINTPTR_MAX - (schema->storage_align - 1u)) {
        selected->deallocate(selected->user, storage->allocation);
        storage->allocation = NULL;
        return false;
    }
    aligned = (address + schema->storage_align - 1u) &
              ~((uintptr_t)schema->storage_align - 1u);
    storage->view.schema = schema;
    storage->view.storage = (unsigned char *)aligned;
    storage->view.bound = storage->view.storage + schema->storage_size;
    storage->allocator = *selected;
    return true;
}

bool cmeta_scope_storage_init(
    cmeta_scope_storage *storage, cmeta_scope_schema *schema,
    const cmeta_scope_allocator *allocator) {
    if (!scope_storage_init(storage, schema, allocator)) return false;
    schema->frozen = true;
    return true;
}

void cmeta_scope_storage_destroy(cmeta_scope_storage *storage) {
    if (storage == NULL) return;
    cmeta_scope_view_clear(&storage->view);
    if (scope_allocator_valid(&storage->allocator))
        storage->allocator.deallocate(
            storage->allocator.user, storage->allocation);
    memset(storage, 0, sizeof(*storage));
}

bool cmeta_scope_view_valid(const cmeta_scope_view *view) {
    return view != NULL && view->schema != NULL &&
           (view->schema->slot_count == 0u ||
            (view->storage != NULL && view->bound != NULL));
}

static bool scope_view_copy_into_empty(
    cmeta_scope_view *destination, const cmeta_scope_view *source) {
    size_t index;
    for (index = 0u; index < source->schema->slot_count; ++index) {
        const cmeta_scope_slot *slot = &source->schema->slots[index];
        if (source->bound[index] == 0u) continue;
        if (slot->managed) {
            if (!slot->value->storage_type->traits->copy_construct(
                    destination->storage + slot->offset,
                    source->storage + slot->offset)) {
                cmeta_scope_view_clear(destination);
                return false;
            }
        } else {
            memcpy(destination->storage + slot->offset,
                   source->storage + slot->offset,
                   slot->value->storage_type->size);
        }
        destination->bound[index] = 1u;
    }
    return true;
}

bool cmeta_scope_view_copy(
    cmeta_scope_view *destination, const cmeta_scope_view *source) {
    cmeta_scope_storage staging = {0};
    bool copied;
    if (!cmeta_scope_view_valid(destination) ||
        !cmeta_scope_view_valid(source) ||
        destination->schema != source->schema)
        return false;
    if (source->schema->slot_count == 0u) return true;
    if (destination->storage == source->storage ||
        destination->bound == source->bound)
        return false;
    if (!scope_storage_init(
            &staging, source->schema, &source->schema->allocator))
        return false;
    copied = scope_view_copy_into_empty(&staging.view, source);
    if (copied)
        copied = cmeta_scope_view_move_replace(
            destination, &staging.view);
    cmeta_scope_storage_destroy(&staging);
    return copied;
}

bool cmeta_scope_view_move_replace(
    cmeta_scope_view *destination, cmeta_scope_view *source) {
    size_t index;
    if (!cmeta_scope_view_valid(destination) ||
        !cmeta_scope_view_valid(source) ||
        destination->schema != source->schema)
        return false;
    if (source->schema->slot_count == 0u) return true;
    if (destination->storage == source->storage ||
        destination->bound == source->bound)
        return false;
    cmeta_scope_view_clear(destination);
    for (index = 0u; index < source->schema->slot_count; ++index) {
        const cmeta_scope_slot *slot = &source->schema->slots[index];
        if (source->bound[index] == 0u) continue;
        if (slot->managed) {
            slot->value->storage_type->traits->move_construct(
                destination->storage + slot->offset,
                source->storage + slot->offset);
        } else {
            memcpy(destination->storage + slot->offset,
                   source->storage + slot->offset,
                   slot->value->storage_type->size);
        }
        destination->bound[index] = 1u;
        source->bound[index] = 0u;
    }
    memset(source->storage, 0, source->schema->storage_size);
    return true;
}

void cmeta_scope_view_clear_slot(cmeta_scope_view *view, size_t slot) {
    const cmeta_scope_slot *entry;
    if (!cmeta_scope_view_valid(view) ||
        slot >= view->schema->slot_count)
        return;
    entry = &view->schema->slots[slot];
    if (view->bound[slot] != 0u && entry->managed)
        entry->value->storage_type->traits->destroy(
            view->storage + entry->offset);
    memset(view->storage + entry->offset, 0,
           entry->value->storage_type->size);
    view->bound[slot] = 0u;
}

void cmeta_scope_view_clear(cmeta_scope_view *view) {
    size_t index;
    if (!cmeta_scope_view_valid(view)) return;
    for (index = 0u; index < view->schema->slot_count; ++index) {
        const cmeta_scope_slot *slot = &view->schema->slots[index];
        if (view->bound[index] != 0u && slot->managed)
            slot->value->storage_type->traits->destroy(
                view->storage + slot->offset);
    }
    if (view->schema->storage_size != 0u)
        memset(view->storage, 0, view->schema->storage_size);
    if (view->schema->slot_count != 0u)
        memset(view->bound, 0, view->schema->slot_count);
}

bool cmeta_scope_view_assign(
    cmeta_scope_view *view, size_t slot, const void *source) {
    const cmeta_scope_slot *entry;
    void *destination;
    if (!cmeta_scope_view_valid(view) || source == NULL ||
        slot >= view->schema->slot_count)
        return false;
    entry = &view->schema->slots[slot];
    destination = view->storage + entry->offset;
    if (entry->managed) {
        const cmeta_type_desc *type = entry->value->storage_type;
        void *allocation;
        void *temporary;
        uintptr_t address;
        uintptr_t aligned;
        size_t allocation_size;
        if (type->size > SIZE_MAX - (type->align - 1u)) return false;
        allocation_size = type->size + type->align - 1u;
        allocation = view->schema->allocator.allocate(
            view->schema->allocator.user, allocation_size);
        if (allocation == NULL) return false;
        address = (uintptr_t)allocation;
        if (address > UINTPTR_MAX - (type->align - 1u)) {
            view->schema->allocator.deallocate(
                view->schema->allocator.user, allocation);
            return false;
        }
        aligned = (address + type->align - 1u) &
                  ~((uintptr_t)type->align - 1u);
        temporary = (void *)aligned;
        if (!entry->value->storage_type->traits->copy_construct(
                temporary, source)) {
            view->schema->allocator.deallocate(
                view->schema->allocator.user, allocation);
            return false;
        }
        if (view->bound[slot] != 0u)
            type->traits->destroy(destination);
        type->traits->move_construct(destination, temporary);
        view->schema->allocator.deallocate(
            view->schema->allocator.user, allocation);
    } else {
        memmove(destination, source, entry->value->storage_type->size);
    }
    view->bound[slot] = 1u;
    return true;
}

bool cmeta_scope_view_read(
    const cmeta_scope_view *view, size_t slot,
    const cmeta_data_desc **out_value, const void **out_object) {
    const cmeta_scope_slot *entry;
    if (out_value != NULL) *out_value = NULL;
    if (out_object != NULL) *out_object = NULL;
    if (!cmeta_scope_view_valid(view) || out_value == NULL ||
        out_object == NULL || slot >= view->schema->slot_count ||
        view->bound[slot] == 0u)
        return false;
    entry = &view->schema->slots[slot];
    *out_value = entry->value;
    *out_object = view->storage + entry->offset;
    return true;
}
