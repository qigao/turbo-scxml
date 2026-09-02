#include "scxml_scope.h"

#include <cmeta/cmeta.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

bool scxml_scope_schema_init(
    scxml_scope_schema *schema, size_t slot_capacity) {
    if (schema == NULL || schema->slots != NULL ||
        schema->slot_count != 0u || schema->slot_capacity != 0u ||
        schema->storage_size != 0u || schema->storage_align != 0u)
        return false;
    if (slot_capacity == 0u) return true;
    if (slot_capacity > SIZE_MAX / sizeof(*schema->slots)) return false;
    schema->slots = (scxml_scope_slot *)calloc(
        slot_capacity, sizeof(*schema->slots));
    if (schema->slots == NULL) return false;
    schema->slot_capacity = slot_capacity;
    schema->storage_align = 1u;
    return true;
}

void scxml_scope_schema_destroy(scxml_scope_schema *schema) {
    size_t index;
    if (schema == NULL) return;
    for (index = 0u; index < schema->slot_count; ++index)
        free(schema->slots[index].name);
    free(schema->slots);
    memset(schema, 0, sizeof(*schema));
}

const scxml_scope_slot *scxml_scope_find(
    const scxml_scope_schema *schema,
    const char *name, size_t name_size,
    size_t *out_slot) {
    size_t index;
    if (out_slot != NULL) *out_slot = SIZE_MAX;
    if (schema == NULL || (name_size != 0u && name == NULL)) return NULL;
    for (index = 0u; index < schema->slot_count; ++index) {
        const scxml_scope_slot *slot = &schema->slots[index];
        if (slot->name_size == name_size &&
            memcmp(slot->name, name, name_size) == 0) {
            if (out_slot != NULL) *out_slot = index;
            return slot;
        }
    }
    return NULL;
}

bool scxml_scope_register(
    scxml_scope_schema *schema,
    const char *name, size_t name_size,
    const cmeta_data_desc *value,
    size_t *out_slot, bool *out_conflict) {
    const scxml_scope_slot *existing;
    const cmeta_type_desc *type;
    scxml_scope_slot *slot;
    size_t aligned;
    bool managed;
    if (out_slot != NULL) *out_slot = SIZE_MAX;
    if (out_conflict != NULL) *out_conflict = false;
    if (schema == NULL || name == NULL || name_size == 0u ||
        out_slot == NULL || out_conflict == NULL ||
        !cmeta_data_desc_valid(value) || value->storage_type == NULL)
        return false;
    existing = scxml_scope_find(schema, name, name_size, out_slot);
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
    if (aligned > SIZE_MAX - type->size) return false;
    slot = &schema->slots[schema->slot_count];
    slot->name = (char *)malloc(name_size);
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

const cmeta_data_desc *scxml_scope_find_data_for_type(
    const cmeta_data_desc *root, const cmeta_type_desc *type,
    size_t max_depth) {
    const cmeta_data_desc *builtin;
    if (!cmeta_type_desc_valid(type) || max_depth == 0u) return NULL;
    builtin = scope_builtin_data(type);
    return builtin != NULL
        ? builtin : scope_find_data_recursive(root, type, 0u, max_depth);
}

bool scxml_scope_view_valid(const scxml_scope_view *view) {
    return view != NULL && view->schema != NULL &&
           (view->schema->slot_count == 0u ||
            (view->storage != NULL && view->bound != NULL));
}

void scxml_scope_view_clear(scxml_scope_view *view) {
    size_t index;
    if (!scxml_scope_view_valid(view)) return;
    for (index = 0u; index < view->schema->slot_count; ++index) {
        const scxml_scope_slot *slot = &view->schema->slots[index];
        if (view->bound[index] != 0u && slot->managed)
            slot->value->storage_type->traits->destroy(
                view->storage + slot->offset);
    }
    if (view->schema->storage_size != 0u)
        memset(view->storage, 0, view->schema->storage_size);
    if (view->schema->slot_count != 0u)
        memset(view->bound, 0, view->schema->slot_count);
}

bool scxml_scope_view_copy(
    scxml_scope_view *destination, const scxml_scope_view *source) {
    size_t index;
    if (!scxml_scope_view_valid(destination) ||
        !scxml_scope_view_valid(source) ||
        destination->schema != source->schema)
        return false;
    if (source->schema->slot_count == 0u) return true;
    if (
        destination->storage == source->storage ||
        destination->bound == source->bound)
        return false;
    scxml_scope_view_clear(destination);
    for (index = 0u; index < source->schema->slot_count; ++index) {
        const scxml_scope_slot *slot = &source->schema->slots[index];
        if (source->bound[index] == 0u) continue;
        if (slot->managed) {
            if (!slot->value->storage_type->traits->copy_construct(
                    destination->storage + slot->offset,
                    source->storage + slot->offset)) {
                scxml_scope_view_clear(destination);
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

bool scxml_scope_view_move_replace(
    scxml_scope_view *destination, scxml_scope_view *source) {
    size_t index;
    if (!scxml_scope_view_valid(destination) ||
        !scxml_scope_view_valid(source) ||
        destination->schema != source->schema)
        return false;
    if (source->schema->slot_count == 0u) return true;
    if (
        destination->storage == source->storage ||
        destination->bound == source->bound)
        return false;
    scxml_scope_view_clear(destination);
    for (index = 0u; index < source->schema->slot_count; ++index) {
        const scxml_scope_slot *slot = &source->schema->slots[index];
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
    if (source->schema->storage_size != 0u)
        memset(source->storage, 0, source->schema->storage_size);
    return true;
}

bool scxml_scope_view_assign(
    scxml_scope_view *view, size_t slot_index, const void *source) {
    const scxml_scope_slot *slot;
    void *destination;
    if (!scxml_scope_view_valid(view) || source == NULL ||
        slot_index >= view->schema->slot_count)
        return false;
    slot = &view->schema->slots[slot_index];
    destination = view->storage + slot->offset;
    if (slot->managed) {
        if (view->bound[slot_index] != 0u)
            slot->value->storage_type->traits->destroy(destination);
        if (!slot->value->storage_type->traits->copy_construct(
                destination, source)) {
            memset(destination, 0, slot->value->storage_type->size);
            view->bound[slot_index] = 0u;
            return false;
        }
    } else {
        memcpy(destination, source, slot->value->storage_type->size);
    }
    view->bound[slot_index] = 1u;
    return true;
}

bool scxml_scope_view_read(
    const scxml_scope_view *view, size_t slot_index,
    const cmeta_data_desc **out_value, const void **out_object) {
    const scxml_scope_slot *slot;
    if (out_value != NULL) *out_value = NULL;
    if (out_object != NULL) *out_object = NULL;
    if (!scxml_scope_view_valid(view) || out_value == NULL ||
        out_object == NULL || slot_index >= view->schema->slot_count ||
        view->bound[slot_index] == 0u)
        return false;
    slot = &view->schema->slots[slot_index];
    *out_value = slot->value;
    *out_object = view->storage + slot->offset;
    return true;
}
