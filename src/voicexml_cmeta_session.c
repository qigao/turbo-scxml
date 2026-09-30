#include <voicexml/cmeta.h>

#include "voicexml_allocator.h"
#include "voicexml_cmeta_internal.h"

#include <data_bind_csv_provider.h>
#include <data_bind_format_provider.h>
#include <data_bind_json_provider.h>
#include <data_bind_xml_provider.h>
#include <data_bind_yaml_provider.h>

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static vxml_status read_scalar_value(
    const cmeta_data_desc *data, const void *object,
    unsigned char *string_scratch, size_t string_capacity,
    vxml_cmeta_value_view *out_value);

static bool session_options_valid(
    const vxml_cmeta_session_options_v1 *options) {
    const size_t v1_prefix_size =
        offsetof(vxml_cmeta_session_options_v1, max_execution_steps) +
        sizeof(options->max_execution_steps);
    return options != NULL &&
        options->abi_version == VXML_CMETA_SESSION_OPTIONS_ABI_V1 &&
        options->struct_size >= v1_prefix_size &&
        options->max_transaction_bytes != 0u &&
        options->max_execution_steps != 0u &&
        ((options->initially_undefined == NULL) ==
         (options->initially_undefined_count == 0u));
}

static bool session_data_options_valid(
    const vxml_cmeta_session_options_v1 *options) {
    const size_t tail_size =
        offsetof(vxml_cmeta_session_options_v1, max_data_owned_bytes) +
        sizeof(options->max_data_owned_bytes);
    const vxml_cmeta_data_resource_adapter_v1 *adapter;
    if (options == NULL || options->struct_size < tail_size ||
        options->max_data_bytes == 0u ||
        options->max_data_owned_bytes == 0u)
        return false;
    adapter = options->data_resources;
    return adapter != NULL &&
        adapter->abi_version == VXML_CMETA_DATA_RESOURCE_ADAPTER_ABI_V1 &&
        adapter->struct_size >= sizeof(*adapter) &&
        adapter->open != NULL && adapter->close != NULL;
}

static bool session_event_options_valid(
    const vxml_cmeta_session_options_v1 *options) {
    const size_t tail_size =
        offsetof(vxml_cmeta_session_options_v1, max_event_dispatch_depth) +
        sizeof(options->max_event_dispatch_depth);
    return options != NULL &&
        options->struct_size >= tail_size &&
        options->max_event_counters != 0u &&
        options->max_event_name_bytes != 0u &&
        options->max_event_dispatch_depth != 0u &&
        options->max_event_name_bytes != SIZE_MAX;
}

static bool session_prompt_media_options_valid(
    const vxml_cmeta_session_options_v1 *options) {
    const size_t tail_size =
        offsetof(vxml_cmeta_session_options_v1, prompt_media_user) +
        sizeof(options->prompt_media_user);
    const vxml_cmeta_prompt_media_adapter_v1 *adapter;
    if (options == NULL || options->struct_size < tail_size)
        return false;
    adapter = options->prompt_media;
    return adapter != NULL &&
        adapter->abi_version == VXML_CMETA_PROMPT_MEDIA_ADAPTER_ABI_V1 &&
        adapter->struct_size >= sizeof(*adapter) &&
        adapter->prepare != NULL &&
        adapter->cancel != NULL;
}

static bool session_collect_options_valid(
    const vxml_cmeta_session_options_v1 *options) {
    const size_t tail_size =
        offsetof(vxml_cmeta_session_options_v1, collect_user) +
        sizeof(options->collect_user);
    const vxml_cmeta_collect_adapter_v1 *adapter;
    if (options == NULL || options->struct_size < tail_size)
        return false;
    adapter = options->collect;
    return adapter != NULL &&
        adapter->abi_version == VXML_CMETA_COLLECT_ADAPTER_ABI_V1 &&
        adapter->struct_size >= sizeof(*adapter) &&
        adapter->prepare != NULL &&
        adapter->cancel != NULL;
}

static const DataBindFormatProvider *data_format_provider(
    vxml_cmeta_data_format format) {
    switch (format) {
    case VXML_CMETA_DATA_JSON:
        return data_bind_json_format_provider();
    case VXML_CMETA_DATA_YAML:
        return data_bind_yaml_format_provider();
    case VXML_CMETA_DATA_CSV:
        return data_bind_csv_format_provider();
    case VXML_CMETA_DATA_XML:
        return data_bind_xml_format_provider();
    default:
        return NULL;
    }
}

static void *session_scope_allocate(void *user, size_t size) {
    (void)user;
    return vxml_malloc(size);
}

static void *session_scope_allocate_zero(
    void *user, size_t count, size_t size) {
    (void)user;
    return vxml_calloc(count, size);
}

static void session_scope_deallocate(void *user, void *pointer) {
    (void)user;
    vxml_free(pointer);
}

static const cmeta_scope_allocator session_scope_allocator = {
    NULL,
    session_scope_allocate,
    session_scope_allocate_zero,
    session_scope_deallocate
};

static bool checked_add(size_t *total, size_t amount) {
    if (*total > SIZE_MAX - amount) return false;
    *total += amount;
    return true;
}

static bool checked_multiply(size_t left, size_t right, size_t *out) {
    if (left != 0u && right > SIZE_MAX / left) return false;
    *out = left * right;
    return true;
}

static bool valid_alignment(size_t alignment) {
    return alignment != 0u &&
        (alignment & (alignment - 1u)) == 0u;
}

static bool storage_allocation_size(
    size_t object_size, size_t object_align, size_t bit_count,
    size_t *out_size) {
    size_t size;
    if (object_size == 0u || !valid_alignment(object_align) ||
        object_size > SIZE_MAX - (object_align - 1u))
        return false;
    size = object_size + object_align - 1u;
    if (!checked_add(&size, bit_count)) return false;
    *out_size = size;
    return true;
}

static bool session_scope_storage_init(
    cmeta_scope_storage *storage,
    const cmeta_scope_schema *schema) {
    size_t allocation_size;
    uintptr_t address;
    uintptr_t aligned;
    if (storage == NULL || schema == NULL || storage->allocation != NULL ||
        storage->view.schema != NULL || storage->view.storage != NULL ||
        storage->view.bound != NULL)
        return false;
    if (schema->slot_count == 0u) {
        storage->view.schema = schema;
        storage->allocator = session_scope_allocator;
        return true;
    }
    if (schema->slots == NULL || !storage_allocation_size(
            schema->storage_size, schema->storage_align,
            schema->slot_count, &allocation_size))
        return false;
    storage->allocation = vxml_calloc(1u, allocation_size);
    if (storage->allocation == NULL) return false;
    address = (uintptr_t)storage->allocation;
    if (address > UINTPTR_MAX - (schema->storage_align - 1u)) {
        vxml_free(storage->allocation);
        memset(storage, 0, sizeof(*storage));
        return false;
    }
    aligned = (address + schema->storage_align - 1u) &
        ~((uintptr_t)schema->storage_align - 1u);
    storage->view.schema = schema;
    storage->view.storage = (unsigned char *)aligned;
    storage->view.bound = storage->view.storage + schema->storage_size;
    storage->allocator = session_scope_allocator;
    return true;
}

static bool field_type_supported(
    const cmeta_data_desc *field, bool *out_managed) {
    const cmeta_type_desc *type;
    if (!cmeta_data_desc_valid(field) || field->storage_type == NULL)
        return false;
    type = field->storage_type;
    if (!cmeta_type_desc_valid(type) || type->size == 0u ||
        !valid_alignment(type->align))
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

static const cmeta_data_struct_shape *session_root_shape(
    const vxml_cmeta_program_data *program) {
    if (program == NULL || !cmeta_data_desc_valid(program->root) ||
        program->root->kind != CMETA_DATA_STRUCT ||
        program->root->storage_type == NULL || program->root->shape == NULL)
        return NULL;
    return (const cmeta_data_struct_shape *)program->root->shape;
}

static bool session_root_fields_valid(
    const vxml_cmeta_program_data *program) {
    const cmeta_data_struct_shape *shape = session_root_shape(program);
    const cmeta_type_desc *root_type;
    size_t index;
    if (shape == NULL || shape->layout == NULL ||
        shape->field_count != shape->layout->field_count ||
        (shape->field_count != 0u && shape->fields == NULL))
        return false;
    root_type = program->root->storage_type;
    if (!cmeta_type_desc_valid(root_type) || root_type->size == 0u ||
        !valid_alignment(root_type->align) ||
        shape->layout->size != root_type->size ||
        shape->layout->align != root_type->align)
        return false;
    for (index = 0u; index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &shape->fields[index];
        const cmeta_field_desc *layout_field =
            cmeta_struct_field(shape->layout, index);
        const cmeta_type_desc *field_type;
        size_t prior;
        bool managed;
        if (field->name == NULL || field->stable_id == NULL ||
            layout_field == NULL || layout_field->name == NULL ||
            layout_field->type == NULL ||
            strcmp(field->name, layout_field->name) != 0 ||
            !field_type_supported(field->value, &managed) ||
            !cmeta_type_equal(
                layout_field->type, field->value->storage_type) ||
            field->offset != layout_field->offset ||
            field->offset > root_type->size ||
            field->value->storage_type->size >
                root_type->size - field->offset ||
            layout_field->size != field->value->storage_type->size ||
            layout_field->align != field->value->storage_type->align)
            return false;
        field_type = field->value->storage_type;
        if (field->offset % field_type->align != 0u ||
            root_type->align < field_type->align)
            return false;
        for (prior = 0u; prior < index; ++prior) {
            const cmeta_data_field_desc *previous = &shape->fields[prior];
            const size_t previous_size =
                previous->value->storage_type->size;
            if (field->offset >= previous->offset) {
                if (field->offset - previous->offset < previous_size)
                    return false;
            } else if (previous->offset - field->offset < field_type->size) {
                return false;
            }
        }
    }
    return true;
}

static bool session_root_contract_valid(
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_session_options_v1 *options) {
    const cmeta_data_struct_shape *shape = session_root_shape(program);
    return session_root_fields_valid(program) && shape != NULL &&
        (shape->field_count == 0u || options->initial_root != NULL);
}

static bool name_view_equal(
    vxml_cmeta_name_view name, const char *field_name) {
    const size_t field_size = strlen(field_name);
    return name.size == field_size &&
        memcmp(name.data, field_name, field_size) == 0;
}

static bool initial_undefined_map(
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_session_options_v1 *options,
    unsigned char *undefined) {
    const cmeta_data_struct_shape *shape = session_root_shape(program);
    size_t name_index;
    if (shape == NULL || options->initially_undefined_count > shape->field_count)
        return false;
    for (name_index = 0u;
         name_index < options->initially_undefined_count; ++name_index) {
        const vxml_cmeta_name_view name =
            options->initially_undefined[name_index];
        size_t field_index;
        if (name.data == NULL || name.size == 0u ||
            !cmeta_location_path_valid(name.data, name.size, 1u))
            return false;
        for (field_index = 0u; field_index < shape->field_count;
             ++field_index)
            if (name_view_equal(name, shape->fields[field_index].name))
                break;
        if (field_index == shape->field_count || undefined[field_index] != 0u)
            return false;
        undefined[field_index] = 1u;
    }
    return true;
}

static bool root_storage_init(
    vxml_cmeta_root_storage *storage,
    const vxml_cmeta_program_data *program) {
    const cmeta_data_struct_shape *shape = session_root_shape(program);
    const cmeta_type_desc *type = program->root->storage_type;
    size_t allocation_size;
    uintptr_t address;
    uintptr_t aligned;
    if (shape == NULL || !storage_allocation_size(
            type->size, type->align, shape->field_count, &allocation_size))
        return false;
    storage->allocation = vxml_calloc(1u, allocation_size);
    if (storage->allocation == NULL) return false;
    address = (uintptr_t)storage->allocation;
    if (address > UINTPTR_MAX - (type->align - 1u)) {
        vxml_free(storage->allocation);
        memset(storage, 0, sizeof(*storage));
        return false;
    }
    aligned = (address + type->align - 1u) &
        ~((uintptr_t)type->align - 1u);
    storage->storage = (unsigned char *)aligned;
    storage->bound = storage->storage + type->size;
    return true;
}

static void root_storage_clear(
    vxml_cmeta_root_storage *storage,
    const vxml_cmeta_program_data *program) {
    const cmeta_data_struct_shape *shape = session_root_shape(program);
    size_t index;
    if (storage == NULL || storage->storage == NULL || shape == NULL) return;
    for (index = 0u; index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &shape->fields[index];
        bool managed = false;
        if (storage->bound[index] != 0u &&
            field_type_supported(field->value, &managed) && managed)
            field->value->storage_type->traits->destroy(
                storage->storage + field->offset);
    }
    memset(storage->storage, 0, program->root->storage_type->size);
    if (shape->field_count != 0u)
        memset(storage->bound, 0, shape->field_count);
}

static void root_storage_destroy(
    vxml_cmeta_root_storage *storage,
    const vxml_cmeta_program_data *program) {
    if (storage == NULL) return;
    root_storage_clear(storage, program);
    vxml_free(storage->allocation);
    memset(storage, 0, sizeof(*storage));
}

static vxml_status root_storage_copy_initial(
    vxml_cmeta_root_storage *destination,
    const vxml_cmeta_program_data *program, const void *source,
    const unsigned char *undefined) {
    const cmeta_data_struct_shape *shape = session_root_shape(program);
    size_t index;
    for (index = 0u; index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &shape->fields[index];
        const cmeta_type_desc *type = field->value->storage_type;
        bool managed = false;
        if (undefined[index] != 0u) continue;
        if (!field_type_supported(field->value, &managed))
            return VXML_INVALID_CONTRACT;
        if (managed) {
            if (!type->traits->copy_construct(
                    destination->storage + field->offset,
                    (const unsigned char *)source + field->offset))
                return VXML_ALLOCATION_FAILED;
        } else {
            memcpy(destination->storage + field->offset,
                   (const unsigned char *)source + field->offset,
                   type->size);
        }
        destination->bound[index] = 1u;
    }
    return VXML_OK;
}

static bool root_storage_copy(
    vxml_cmeta_root_storage *destination,
    const vxml_cmeta_root_storage *source,
    const vxml_cmeta_program_data *program) {
    const cmeta_data_struct_shape *shape = session_root_shape(program);
    size_t index;
    root_storage_clear(destination, program);
    for (index = 0u; index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &shape->fields[index];
        const cmeta_type_desc *type = field->value->storage_type;
        bool managed = false;
        if (source->bound[index] == 0u) continue;
        if (!field_type_supported(field->value, &managed)) goto failure;
        if (managed) {
            if (!type->traits->copy_construct(
                    destination->storage + field->offset,
                    source->storage + field->offset))
                goto failure;
        } else {
            memcpy(destination->storage + field->offset,
                   source->storage + field->offset, type->size);
        }
        destination->bound[index] = 1u;
    }
    return true;

failure:
    root_storage_clear(destination, program);
    return false;
}

static bool scope_storage_copy(
    cmeta_scope_storage *destination,
    const cmeta_scope_storage *source) {
    const cmeta_scope_schema *schema;
    size_t index;
    if (!cmeta_scope_view_valid(&destination->view) ||
        !cmeta_scope_view_valid(&source->view) ||
        destination->view.schema != source->view.schema)
        return false;
    schema = source->view.schema;
    cmeta_scope_view_clear(&destination->view);
    for (index = 0u; index < schema->slot_count; ++index) {
        const cmeta_scope_slot *slot = &schema->slots[index];
        if (source->view.bound[index] == 0u) continue;
        if (slot->managed) {
            if (!slot->value->storage_type->traits->copy_construct(
                    destination->view.storage + slot->offset,
                    source->view.storage + slot->offset))
                goto failure;
        } else {
            memcpy(destination->view.storage + slot->offset,
                   source->view.storage + slot->offset,
                   slot->value->storage_type->size);
        }
        destination->view.bound[index] = 1u;
    }
    return true;

failure:
    cmeta_scope_view_clear(&destination->view);
    return false;
}

static bool range_valid(size_t first, size_t count, size_t total) {
    return first <= total && count <= total - first;
}

static bool exit_action_capacity(
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_action_row *action,
    size_t *out_entries, size_t *out_names, size_t *out_strings) {
    size_t entries = 0u;
    size_t names = 0u;
    size_t strings = 0u;
    size_t index;
    if (action->exit_kind == VXML_CMETA_EXIT_EXPRESSION) {
        if (action->expression >= program->expression_count ||
            program->expressions == NULL)
            return false;
        entries = 1u;
        if (vxml_cmeta_expr_program_value_kind(
                &program->expressions[action->expression].program) ==
            VXML_CMETA_VALUE_STRING)
            strings = program->max_string_bytes;
    } else if (action->exit_kind == VXML_CMETA_EXIT_NAMELIST) {
        if (!range_valid(
                action->first_location, action->location_count,
                program->location_count) ||
            (action->location_count != 0u && program->locations == NULL))
            return false;
        entries = action->location_count;
        for (index = 0u; index < action->location_count; ++index) {
            const vxml_cmeta_location_row *location =
                &program->locations[action->first_location + index];
            if ((location->name_size != 0u && location->name == NULL) ||
                !checked_add(&names, location->name_size))
                return false;
            if (location->value != NULL &&
                location->value->kind == CMETA_DATA_STRING &&
                !checked_add(&strings, program->max_string_bytes))
                return false;
        }
    } else if (action->exit_kind != VXML_CMETA_EXIT_EMPTY) {
        return false;
    }
    *out_entries = entries;
    *out_names = names;
    *out_strings = strings;
    return true;
}

static bool exit_capacity_measure(
    const vxml_cmeta_program_data *program,
    size_t first_action, size_t action_end,
    size_t *out_entries, size_t *out_names, size_t *out_strings) {
    size_t entries = 0u;
    size_t names = 0u;
    size_t strings = 0u;
    size_t index;
    if (first_action > action_end ||
        !range_valid(first_action, action_end - first_action,
                     program->action_count) ||
        (first_action != action_end && program->actions == NULL))
        return false;
    for (index = first_action; index < action_end; ++index) {
        const vxml_cmeta_action_row *action = &program->actions[index];
        size_t action_entries;
        size_t action_names;
        size_t action_strings;
        if (action->kind != VXML_CMETA_ACTION_EXIT) continue;
        if (!exit_action_capacity(
                program, action, &action_entries,
                &action_names, &action_strings))
            return false;
        if (action_entries > entries) entries = action_entries;
        if (action_names > names) names = action_names;
        if (action_strings > strings) strings = action_strings;
    }
    *out_entries = entries;
    *out_names = names;
    *out_strings = strings;
    return true;
}

static bool transaction_bytes_measure(
    const vxml_cmeta_program_data *program,
    size_t declared_count, size_t *out_bytes,
    size_t *out_exec_frame_capacity) {
    const cmeta_data_struct_shape *shape = session_root_shape(program);
    size_t total = 0u;
    size_t amount;
    size_t scope_index;
    size_t frame_capacity;
    size_t exit_entries = 0u;
    size_t exit_names = 0u;
    size_t exit_strings = 0u;
    if (shape == NULL || !storage_allocation_size(
            program->root->storage_type->size,
            program->root->storage_type->align,
            shape->field_count, &amount) || !checked_add(&total, amount))
        return false;
    for (scope_index = 0u; scope_index < program->scope_count;
         ++scope_index) {
        const cmeta_scope_schema *schema =
            &program->scopes[scope_index].schema;
        if (schema->slot_count == 0u) continue;
        if (!storage_allocation_size(
                schema->storage_size, schema->storage_align,
                schema->slot_count, &amount) || !checked_add(&total, amount))
            return false;
    }
    if (!checked_add(&total, declared_count) ||
        !checked_add(&total, program->expression_scratch_bytes))
        return false;
    if (!exit_capacity_measure(
            program, 0u, program->action_count,
            &exit_entries, &exit_names, &exit_strings) ||
        !checked_multiply(
            exit_entries, sizeof(vxml_cmeta_exit_entry), &amount) ||
        !checked_add(&total, amount) ||
        !checked_add(&total, exit_names) ||
        !checked_add(&total, exit_strings))
        return false;
    if (program->max_conditional_depth == SIZE_MAX) return false;
    frame_capacity = program->max_conditional_depth + 1u;
    if (!checked_multiply(
            frame_capacity, sizeof(vxml_cmeta_exec_frame), &amount) ||
        !checked_add(&total, amount) ||
        !checked_multiply(
            3u, sizeof(vxml_cmeta_expr_runtime_scope), &amount) ||
        !checked_add(&total, amount))
        return false;
    *out_bytes = total;
    *out_exec_frame_capacity = frame_capacity;
    return true;
}

static void exit_snapshot_destroy(vxml_cmeta_exit_snapshot *snapshot) {
    if (snapshot == NULL) return;
    vxml_free(snapshot->strings);
    vxml_free(snapshot->names);
    vxml_free(snapshot->entries);
    memset(snapshot, 0, sizeof(*snapshot));
}

static vxml_status exit_snapshot_prepare_range(
    vxml_cmeta_exit_snapshot *snapshot,
    const vxml_cmeta_program_data *program,
    size_t first_action, size_t action_end) {
    size_t entry_capacity;
    size_t name_capacity;
    size_t string_capacity;
    exit_snapshot_destroy(snapshot);
    if (!exit_capacity_measure(
            program, first_action, action_end,
            &entry_capacity, &name_capacity, &string_capacity))
        return VXML_INVALID_STRUCTURE;
    if (entry_capacity != 0u) {
        snapshot->entries = (vxml_cmeta_exit_entry *)vxml_calloc(
            entry_capacity, sizeof(*snapshot->entries));
        if (snapshot->entries == NULL) goto allocation_failure;
    }
    if (name_capacity != 0u) {
        snapshot->names = (char *)vxml_malloc(name_capacity);
        if (snapshot->names == NULL) goto allocation_failure;
    }
    if (string_capacity != 0u) {
        snapshot->strings = (char *)vxml_malloc(string_capacity);
        if (snapshot->strings == NULL) goto allocation_failure;
    }
    snapshot->entry_capacity = entry_capacity;
    snapshot->name_capacity = name_capacity;
    snapshot->string_capacity = string_capacity;
    return VXML_OK;

allocation_failure:
    exit_snapshot_destroy(snapshot);
    return VXML_ALLOCATION_FAILED;
}

static vxml_status exit_snapshot_append(
    vxml_cmeta_exit_snapshot *snapshot,
    const char *name, size_t name_size,
    const vxml_cmeta_value_view *value) {
    vxml_cmeta_exit_entry *entry;
    if (snapshot == NULL || value == NULL ||
        snapshot->count >= snapshot->entry_capacity ||
        snapshot->name_size > snapshot->name_capacity ||
        name_size > snapshot->name_capacity - snapshot->name_size)
        return VXML_LIMIT_EXCEEDED;
    entry = &snapshot->entries[snapshot->count];
    if (name_size != 0u) {
        if (name == NULL || snapshot->names == NULL)
            return VXML_INVALID_STRUCTURE;
        memcpy(snapshot->names + snapshot->name_size, name, name_size);
        entry->name.data = snapshot->names + snapshot->name_size;
        entry->name.size = name_size;
        snapshot->name_size += name_size;
    }
    entry->value = *value;
    if (value->kind == VXML_CMETA_VALUE_STRING) {
        const size_t string_size = value->data.string.size;
        if ((string_size != 0u && value->data.string.data == NULL) ||
            snapshot->string_size > snapshot->string_capacity ||
            string_size >
                snapshot->string_capacity - snapshot->string_size)
            return VXML_LIMIT_EXCEEDED;
        if (string_size != 0u) {
            if (snapshot->strings == NULL) return VXML_INVALID_STRUCTURE;
            memmove(snapshot->strings + snapshot->string_size,
                    value->data.string.data, string_size);
        }
        entry->value.data.string.data =
            snapshot->strings + snapshot->string_size;
        snapshot->string_size += string_size;
    }
    ++snapshot->count;
    return VXML_OK;
}

static void exit_snapshot_publish(vxml_cmeta_session_data *session) {
    exit_snapshot_destroy(&session->terminal_exit);
    session->terminal_exit = session->pending_exit;
    memset(&session->pending_exit, 0, sizeof(session->pending_exit));
}

static void settle_prompt_media(
    vxml_cmeta_session_data *session) {
    if (session == NULL) return;
    if (session->prompt_media_prepared) {
        vxml_cmeta_prompt_media_ticket_v1 ticket =
            session->prompt_media_ticket;
        session->prompt_media_prepared = false;
        session->prompt_media_ticket =
            (vxml_cmeta_prompt_media_ticket_v1){0};
        if (ticket.discard != NULL)
            ticket.discard(ticket.user);
    } else if (session->prompt_media_in_flight &&
               session->prompt_media_adapter != NULL) {
        const uint64_t generation =
            session->prompt_media_generation;
        session->prompt_media_in_flight = false;
        session->prompt_media_adapter->cancel(
            session->prompt_media_user, generation);
    }
    session->prompt_media_generation = 0u;
}

static void session_data_destroy(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program) {
    size_t index;
    if (session == NULL) return;
    settle_prompt_media(session);
    atomic_store_explicit(
        &session->collect_mailbox.state,
        VXML_CMETA_COLLECT_MAILBOX_CLOSED,
        memory_order_release);
    if (session->collect_prepared) {
        vxml_cmeta_collect_ticket_v1 ticket = session->collect_ticket;
        session->collect_prepared = false;
        session->collect_ticket = (vxml_cmeta_collect_ticket_v1){0};
        if (ticket.discard != NULL)
            ticket.discard(ticket.user);
    } else if (session->collect_in_flight &&
               session->collect_adapter != NULL) {
        const uint64_t generation = session->collect_generation;
        session->collect_in_flight = false;
        session->collect_adapter->cancel(
            session->collect_user, generation);
    }
    exit_snapshot_destroy(&session->terminal_exit);
    exit_snapshot_destroy(&session->pending_exit);
    root_storage_destroy(&session->staged_root, program);
    root_storage_destroy(&session->committed_root, program);
    if (session->staged_scopes != NULL)
        for (index = 0u; index < program->scope_count; ++index)
            cmeta_scope_storage_destroy(&session->staged_scopes[index]);
    if (session->committed_scopes != NULL)
        for (index = 0u; index < program->scope_count; ++index)
            cmeta_scope_storage_destroy(&session->committed_scopes[index]);
    vxml_free(session->retry_reset_pending);
    vxml_free(session->event_counter_names);
    vxml_free(session->event_counters);
    vxml_free(session->collect_mailbox.root_fields);
    vxml_free(session->collect_mailbox.allocation);
    vxml_free(session->data_value_allocation);
    vxml_free(session->data_workspace_allocation);
    vxml_free(session->exec_frames);
    vxml_free(session->runtime_scopes);
    vxml_free(session->read_scratch);
    vxml_free(session->expression_scratch);
    vxml_free(session->staged_declared);
    vxml_free(session->committed_declared);
    vxml_free(session->declared_offsets);
    vxml_free(session->staged_scopes);
    vxml_free(session->committed_scopes);
    vxml_free(session);
}

static unsigned char *session_declared(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    bool staged, size_t scope) {
    unsigned char *base = staged
        ? session->staged_declared : session->committed_declared;
    if (session == NULL || program == NULL || scope >= program->scope_count ||
        session->declared_offsets == NULL)
        return NULL;
    if (session->declared_offsets[scope] == session->declared_count)
        return base;
    return base != NULL ? base + session->declared_offsets[scope] : NULL;
}

static bool recovery_event_name(
    const vxml_cmeta_event_counter *counter) {
    return counter != NULL && counter->event != NULL &&
        ((counter->event_size == sizeof("noinput") - 1u &&
          memcmp(counter->event, "noinput", sizeof("noinput") - 1u) == 0) ||
         (counter->event_size == sizeof("nomatch") - 1u &&
          memcmp(counter->event, "nomatch", sizeof("nomatch") - 1u) == 0));
}

static void reset_field_retry_counters(
    vxml_cmeta_session_data *session, size_t field_index) {
    size_t index;
    if (session == NULL) return;
    for (index = 0u; index < session->event_counter_count; ++index) {
        vxml_cmeta_event_counter *counter =
            &session->event_counters[index];
        if (counter->scope_kind == VXML_CMETA_EVENT_FIELD &&
            counter->owner == field_index &&
            recovery_event_name(counter))
            counter->count = 0u;
    }
}

static void reset_form_retry_counters(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form) {
    size_t offset;
    if (session == NULL || program == NULL || form == NULL ||
        !range_valid(form->first_field, form->field_count,
                     program->field_count))
        return;
    for (offset = 0u; offset < form->field_count; ++offset)
        reset_field_retry_counters(
            session, form->first_field + offset);
}

static void mark_field_retry_reset(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    size_t field_index) {
    if (session == NULL || program == NULL ||
        session->retry_reset_pending == NULL ||
        field_index >= program->field_count)
        return;
    session->retry_reset_pending[field_index] = 1u;
}

static void apply_retry_resets(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program) {
    size_t index;
    if (session == NULL || program == NULL ||
        session->retry_reset_pending == NULL)
        return;
    for (index = 0u; index < program->field_count; ++index)
        if (session->retry_reset_pending[index] != 0u)
            reset_field_retry_counters(session, index);
}

static void transaction_reset(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program) {
    size_t index;
    root_storage_clear(&session->staged_root, program);
    for (index = 0u; index < program->scope_count; ++index)
        cmeta_scope_view_clear(&session->staged_scopes[index].view);
    if (session->declared_count != 0u)
        memset(session->staged_declared, 0, session->declared_count);
    if (program->field_count != 0u &&
        session->retry_reset_pending != NULL)
        memset(session->retry_reset_pending, 0, program->field_count);
}

static bool transaction_begin(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program) {
    size_t index;
    transaction_reset(session, program);
    if (!root_storage_copy(
            &session->staged_root, &session->committed_root, program))
        return false;
    for (index = 0u; index < program->scope_count; ++index) {
        if (!scope_storage_copy(
                &session->staged_scopes[index],
                &session->committed_scopes[index])) {
            transaction_reset(session, program);
            return false;
        }
    }
    if (session->declared_count != 0u)
        memcpy(session->staged_declared, session->committed_declared,
               session->declared_count);
    return true;
}

static void transaction_commit(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program) {
    vxml_cmeta_root_storage root_swap = session->committed_root;
    unsigned char *declared_swap = session->committed_declared;
    size_t index;
    session->committed_root = session->staged_root;
    session->staged_root = root_swap;
    for (index = 0u; index < program->scope_count; ++index) {
        cmeta_scope_storage scope_swap = session->committed_scopes[index];
        session->committed_scopes[index] = session->staged_scopes[index];
        session->staged_scopes[index] = scope_swap;
    }
    session->committed_declared = session->staged_declared;
    session->staged_declared = declared_swap;
    apply_retry_resets(session, program);
    transaction_reset(session, program);
}

static vxml_status session_fail(
    vxml_session_impl *session, vxml_status status) {
    if (session == NULL) return status;
    session->state = VXML_SESSION_FAILED;
    session->error = status;
    return status;
}

static bool consume_step(vxml_cmeta_session_data *session) {
    if (session->execution_steps >= session->max_execution_steps)
        return false;
    ++session->execution_steps;
    return true;
}

static vxml_status evaluate_expression(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    bool staged, size_t expression,
    const size_t *scopes, size_t scope_count,
    vxml_cmeta_value_view *out_value) {
    const vxml_cmeta_root_storage *root;
    vxml_cmeta_expr_runtime runtime;
    vxml_cmeta_expr_scratch scratch;
    size_t index;
    if (session == NULL || program == NULL || out_value == NULL ||
        expression >= program->expression_count ||
        program->expressions == NULL ||
        scope_count > session->runtime_scope_capacity ||
        (scope_count != 0u && scopes == NULL))
        return VXML_INVALID_STRUCTURE;
    for (index = 0u; index < scope_count; ++index) {
        const size_t scope = scopes[index];
        unsigned char *declared;
        size_t earlier;
        if (scope >= program->scope_count) return VXML_INVALID_STRUCTURE;
        for (earlier = 0u; earlier < index; ++earlier)
            if (scopes[earlier] == scope) return VXML_INVALID_STRUCTURE;
        declared = session_declared(session, program, staged, scope);
        if (declared == NULL &&
            program->scopes[scope].schema.slot_count != 0u)
            return VXML_INVALID_STRUCTURE;
        session->runtime_scopes[index] =
            (vxml_cmeta_expr_runtime_scope){
                scope,
                staged ? &session->staged_scopes[scope].view
                       : &session->committed_scopes[scope].view,
                declared,
                program->scopes[scope].schema.slot_count};
    }
    root = staged ? &session->staged_root : &session->committed_root;
    runtime = (vxml_cmeta_expr_runtime){
        root->storage,
        root->bound,
        session_root_shape(program)->field_count,
        session->runtime_scopes,
        scope_count};
    scratch = (vxml_cmeta_expr_scratch){
        session->expression_scratch,
        session->expression_scratch_bytes,
        0u};
    return vxml_cmeta_expr_evaluate(
        &program->expressions[expression].program,
        &runtime, &scratch, out_value, NULL);
}

static vxml_status evaluate_condition(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    bool staged, size_t expression,
    const size_t *scopes, size_t scope_count,
    bool *out_value) {
    vxml_cmeta_value_view value;
    const vxml_status status = evaluate_expression(
        session, program, staged, expression,
        scopes, scope_count, &value);
    if (status != VXML_OK) return status;
    if (value.kind != VXML_CMETA_VALUE_BOOL)
        return VXML_INVALID_STRUCTURE;
    *out_value = value.data.boolean;
    return VXML_OK;
}

static bool store_sint(
    const cmeta_data_desc *data, void *object, int64_t value) {
    const cmeta_data_integer_shape *shape =
        (const cmeta_data_integer_shape *)data->shape;
    if (shape == NULL) return false;
    switch (shape->bits) {
        case 8u: {
            const int8_t converted = (int8_t)value;
            if ((int64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 16u: {
            const int16_t converted = (int16_t)value;
            if ((int64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 32u: {
            const int32_t converted = (int32_t)value;
            if ((int64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 64u:
            memcpy(object, &value, sizeof(value));
            return true;
        default:
            return false;
    }
}

static bool store_uint(
    const cmeta_data_desc *data, void *object, uint64_t value) {
    const cmeta_data_integer_shape *shape =
        (const cmeta_data_integer_shape *)data->shape;
    if (shape == NULL) return false;
    switch (shape->bits) {
        case 8u: {
            const uint8_t converted = (uint8_t)value;
            if ((uint64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 16u: {
            const uint16_t converted = (uint16_t)value;
            if ((uint64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 32u: {
            const uint32_t converted = (uint32_t)value;
            if ((uint64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 64u:
            memcpy(object, &value, sizeof(value));
            return true;
        default:
            return false;
    }
}

static bool store_float(
    const cmeta_data_desc *data, void *object, double value) {
    const cmeta_data_float_shape *shape =
        (const cmeta_data_float_shape *)data->shape;
    if (shape == NULL || !isfinite(value)) return false;
    if (shape->bits == 32u) {
        const float converted = (float)value;
        if (!isfinite(converted) || (double)converted != value) return false;
        memcpy(object, &converted, sizeof(converted));
        return true;
    }
    if (shape->bits == 64u) {
        memcpy(object, &value, sizeof(value));
        return true;
    }
    return false;
}

static vxml_status buffer_status(cmeta_status status) {
    if (status == CMETA_OK) return VXML_OK;
    if (status == CMETA_OUT_OF_MEMORY) return VXML_ALLOCATION_FAILED;
    if (status == CMETA_CAPACITY_EXCEEDED) return VXML_LIMIT_EXCEEDED;
    return VXML_SEMANTIC_ERROR;
}

static vxml_status assign_scalar_object(
    const cmeta_data_desc *data, void *object,
    const vxml_cmeta_value_view *value, size_t max_string_bytes) {
    cmeta_status status;
    if (!cmeta_data_desc_valid(data) || data->storage_type == NULL ||
        object == NULL || value == NULL ||
        value->kind == VXML_CMETA_VALUE_UNDEFINED)
        return VXML_INVALID_STRUCTURE;
    switch (data->kind) {
        case CMETA_DATA_BOOL:
            if (value->kind != VXML_CMETA_VALUE_BOOL ||
                data->storage_type->size != sizeof(bool))
                return VXML_SEMANTIC_ERROR;
            memcpy(object, &value->data.boolean, sizeof(bool));
            return VXML_OK;
        case CMETA_DATA_SINT:
            return value->kind == VXML_CMETA_VALUE_SINT &&
                    store_sint(data, object, value->data.sint)
                ? VXML_OK : VXML_SEMANTIC_ERROR;
        case CMETA_DATA_UINT:
            return value->kind == VXML_CMETA_VALUE_UINT &&
                    store_uint(data, object, value->data.uint_value)
                ? VXML_OK : VXML_SEMANTIC_ERROR;
        case CMETA_DATA_FLOAT:
            return value->kind == VXML_CMETA_VALUE_FLOAT &&
                    store_float(data, object, value->data.number)
                ? VXML_OK : VXML_SEMANTIC_ERROR;
        case CMETA_DATA_STRING: {
            const cmeta_data_buffer_ops *ops = cmeta_data_buffer_ops_of(data);
            if (value->kind != VXML_CMETA_VALUE_STRING || ops == NULL ||
                ops->ownership != CMETA_DATA_BUFFER_OWNED ||
                (value->data.string.size != 0u &&
                 value->data.string.data == NULL))
                return VXML_SEMANTIC_ERROR;
            status = cmeta_data_buffer_restore_zero(data, object);
            if (status == CMETA_OK)
                status = cmeta_data_buffer_assign(
                    data, object,
                    (const unsigned char *)value->data.string.data,
                    value->data.string.size, max_string_bytes);
            return buffer_status(status);
        }
        default:
            return VXML_SEMANTIC_ERROR;
    }
}

static vxml_status assign_scope_slot(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    bool staged, size_t scope, size_t slot,
    const vxml_cmeta_value_view *value) {
    cmeta_scope_view *view;
    const cmeta_scope_schema *schema;
    const cmeta_scope_slot *target;
    vxml_status status;
    if (session == NULL || program == NULL || value == NULL ||
        scope >= program->scope_count)
        return VXML_INVALID_STRUCTURE;
    view = staged ? &session->staged_scopes[scope].view
                  : &session->committed_scopes[scope].view;
    schema = &program->scopes[scope].schema;
    if (!cmeta_scope_view_valid(view) || view->schema != schema ||
        slot >= schema->slot_count)
        return VXML_INVALID_STRUCTURE;
    if (value->kind == VXML_CMETA_VALUE_UNDEFINED) {
        cmeta_scope_view_clear_slot(view, slot);
        return VXML_OK;
    }
    target = &schema->slots[slot];
    if (target->offset > schema->storage_size ||
        target->value == NULL || target->value->storage_type == NULL ||
        target->value->storage_type->size >
            schema->storage_size - target->offset)
        return VXML_INVALID_STRUCTURE;
    status = assign_scalar_object(
        target->value, view->storage + target->offset,
        value, program->max_string_bytes);
    if (status == VXML_OK) view->bound[slot] = 1u;
    return status;
}

static vxml_status initialize_declarations(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    size_t first, size_t count,
    const size_t *scopes, size_t scope_count) {
    unsigned char *declared;
    size_t index;
    if (scope_count == 0u || scopes == NULL ||
        scopes[0] >= program->scope_count ||
        !range_valid(first, count, program->declaration_count) ||
        (count != 0u && program->declarations == NULL))
        return VXML_INVALID_STRUCTURE;
    declared = session_declared(session, program, true, scopes[0]);
    if (declared == NULL &&
        program->scopes[scopes[0]].schema.slot_count != 0u)
        return VXML_INVALID_STRUCTURE;
    for (index = 0u; index < count; ++index) {
        const vxml_cmeta_declaration_row *row =
            &program->declarations[first + index];
        vxml_cmeta_value_view value;
        vxml_status status;
        if (row->scope != scopes[0] ||
            row->slot >= program->scopes[row->scope].schema.slot_count)
            return VXML_INVALID_STRUCTURE;
        declared[row->slot] = 1u;
        if (row->expression == VXML_CMETA_NO_INDEX) continue;
        status = evaluate_expression(
            session, program, true, row->expression,
            scopes, scope_count, &value);
        if (status != VXML_OK) return status;
        status = assign_scope_slot(
            session, program, true, row->scope, row->slot, &value);
        if (status != VXML_OK) return status;
    }
    return VXML_OK;
}

static vxml_status initialize_form(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    size_t form_index) {
    const size_t document_scopes[1] = {program->document_scope};
    const size_t form_scopes[2] = {form->scope, program->document_scope};
    size_t block_offset;
    vxml_status status = initialize_declarations(
        session, program,
        program->first_document_declaration,
        program->document_declaration_count,
        document_scopes, 1u);
    if (status != VXML_OK) return status;
    status = initialize_declarations(
        session, program, form->first_declaration, form->declaration_count,
        form_scopes, 2u);
    if (status != VXML_OK) return status;
    for (block_offset = 0u; block_offset < form->block_count; ++block_offset) {
        const size_t block_index = form->first_block + block_offset;
        const vxml_cmeta_block_row *block = &program->blocks[block_index];
        const size_t block_scopes[3] = {
            block->scope, form->scope, program->document_scope};
        unsigned char *form_declared;
        if (block->form != form_index ||
            block->scope >= program->scope_count ||
            block->form_item_slot >=
                program->scopes[form->scope].schema.slot_count)
            return VXML_INVALID_STRUCTURE;
        form_declared = session_declared(
            session, program, true, form->scope);
        if (form_declared == NULL) return VXML_INVALID_STRUCTURE;
        form_declared[block->form_item_slot] = 1u;
        if (block->initial_expression != VXML_CMETA_NO_INDEX) {
            vxml_cmeta_value_view value;
            status = evaluate_expression(
                session, program, true, block->initial_expression,
                block_scopes, 3u, &value);
            if (status != VXML_OK) return status;
            status = assign_scope_slot(
                session, program, true, form->scope,
                block->form_item_slot, &value);
            if (status != VXML_OK) return status;
        }
    }
    return VXML_OK;
}

typedef struct resolved_location {
    const vxml_cmeta_location_candidate_row *candidate;
    cmeta_scope_view *scope;
    vxml_cmeta_root_storage *root;
} resolved_location;

static vxml_status resolve_staged_location(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_location_row *location,
    resolved_location *out) {
    size_t index;
    memset(out, 0, sizeof(*out));
    if (location == NULL ||
        !range_valid(location->first_candidate, location->candidate_count,
                     program->location_candidate_count))
        return VXML_INVALID_STRUCTURE;
    for (index = 0u; index < location->candidate_count; ++index) {
        const vxml_cmeta_location_candidate_row *candidate =
            &program->location_candidates[
                location->first_candidate + index];
        if (candidate->scope != VXML_CMETA_NO_INDEX) {
            unsigned char *declared;
            const cmeta_scope_schema *schema;
            if (candidate->scope >= program->scope_count)
                return VXML_INVALID_STRUCTURE;
            schema = &program->scopes[candidate->scope].schema;
            declared = session_declared(
                session, program, true, candidate->scope);
            if (candidate->schema != schema ||
                candidate->location.slot >= schema->slot_count ||
                declared == NULL)
                return VXML_INVALID_STRUCTURE;
            if (declared[candidate->location.slot] == 0u) continue;
            out->candidate = candidate;
            out->scope = &session->staged_scopes[candidate->scope].view;
            return VXML_OK;
        }
        if (candidate->root_field >=
            session_root_shape(program)->field_count)
            return VXML_INVALID_STRUCTURE;
        out->candidate = candidate;
        out->root = &session->staged_root;
        return VXML_OK;
    }
    return VXML_SEMANTIC_ERROR;
}

static vxml_status read_staged_location_value(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_location_row *location,
    vxml_cmeta_value_view *out_value) {
    const cmeta_data_struct_shape *root_shape = session_root_shape(program);
    resolved_location resolved;
    const void *object = NULL;
    unsigned char *string_scratch = NULL;
    bool bound;
    vxml_status status;
    *out_value = (vxml_cmeta_value_view){0};
    status = resolve_staged_location(session, program, location, &resolved);
    if (status != VXML_OK) return status;
    if (resolved.candidate->location.value != location->value)
        return VXML_INVALID_STRUCTURE;
    if (resolved.scope != NULL) {
        const cmeta_scope_slot *slot = &resolved.scope->schema->slots[
            resolved.candidate->location.slot];
        const size_t offset = resolved.candidate->location.offset;
        bound = resolved.scope->bound[
            resolved.candidate->location.slot] != 0u;
        if (offset > slot->value->storage_type->size ||
            location->value->storage_type->size >
                slot->value->storage_type->size - offset)
            return VXML_INVALID_STRUCTURE;
        if (bound) object = resolved.scope->storage + slot->offset + offset;
    } else {
        const size_t root_field = resolved.candidate->root_field;
        const size_t offset = resolved.candidate->location.offset;
        if (root_shape == NULL || root_field >= root_shape->field_count ||
            offset > program->root->storage_type->size ||
            location->value->storage_type->size >
                program->root->storage_type->size - offset)
            return VXML_INVALID_STRUCTURE;
        bound = resolved.root->bound[root_field] != 0u;
        if (bound) object = resolved.root->storage + offset;
    }
    if (!bound) return VXML_OK;
    if (session->pending_exit.string_size >
        session->pending_exit.string_capacity)
        return VXML_INVALID_STRUCTURE;
    if (session->pending_exit.string_size <
        session->pending_exit.string_capacity) {
        if (session->pending_exit.strings == NULL)
            return VXML_INVALID_STRUCTURE;
        string_scratch = (unsigned char *)session->pending_exit.strings +
            session->pending_exit.string_size;
    }
    return read_scalar_value(
        location->value, object, string_scratch,
        session->pending_exit.string_capacity -
            session->pending_exit.string_size,
        out_value);
}

static void root_storage_clear_field(
    vxml_cmeta_root_storage *storage,
    const vxml_cmeta_program_data *program, size_t field_index) {
    const cmeta_data_struct_shape *shape = session_root_shape(program);
    const cmeta_data_field_desc *field = &shape->fields[field_index];
    bool managed = false;
    if (storage->bound[field_index] != 0u &&
        field_type_supported(field->value, &managed) && managed)
        field->value->storage_type->traits->destroy(
            storage->storage + field->offset);
    memset(storage->storage + field->offset, 0,
           field->value->storage_type->size);
    storage->bound[field_index] = 0u;
}

static vxml_status assign_resolved_location(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_location_row *location,
    const vxml_cmeta_value_view *value) {
    const cmeta_data_struct_shape *root_shape = session_root_shape(program);
    resolved_location resolved;
    const cmeta_data_desc *target;
    unsigned char *object;
    unsigned char *bound;
    bool direct;
    vxml_status status = resolve_staged_location(
        session, program, location, &resolved);
    if (status != VXML_OK) return status;
    target = resolved.candidate->location.value;
    if (target == NULL || target != location->value ||
        target->storage_type == NULL)
        return VXML_INVALID_STRUCTURE;
    if (resolved.scope != NULL) {
        const cmeta_scope_slot *slot =
            &resolved.scope->schema->slots[
                resolved.candidate->location.slot];
        const size_t offset = resolved.candidate->location.offset;
        direct = target == slot->value && offset == 0u;
        if (value->kind == VXML_CMETA_VALUE_UNDEFINED) {
            cmeta_scope_view_clear_slot(
                resolved.scope, resolved.candidate->location.slot);
            return VXML_OK;
        }
        bound = &resolved.scope->bound[
            resolved.candidate->location.slot];
        if (*bound == 0u && !direct) return VXML_SEMANTIC_ERROR;
        if (offset > slot->value->storage_type->size ||
            target->storage_type->size >
                slot->value->storage_type->size - offset)
            return VXML_INVALID_STRUCTURE;
        object = resolved.scope->storage + slot->offset + offset;
    } else {
        const cmeta_data_field_desc *field =
            &root_shape->fields[resolved.candidate->root_field];
        const size_t offset = resolved.candidate->location.offset;
        direct = target == field->value && offset == field->offset;
        if (value->kind == VXML_CMETA_VALUE_UNDEFINED) {
            root_storage_clear_field(
                resolved.root, program, resolved.candidate->root_field);
            return VXML_OK;
        }
        bound = &resolved.root->bound[resolved.candidate->root_field];
        if (*bound == 0u && !direct) return VXML_SEMANTIC_ERROR;
        if (offset > program->root->storage_type->size ||
            target->storage_type->size >
                program->root->storage_type->size - offset)
            return VXML_INVALID_STRUCTURE;
        object = resolved.root->storage + offset;
    }
    status = assign_scalar_object(
        target, object, value, program->max_string_bytes);
    if (status == VXML_OK) *bound = 1u;
    return status;
}

static void mark_form_retry_reset_by_root(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    size_t root_field) {
    size_t offset;
    if (session == NULL || program == NULL || form == NULL ||
        !range_valid(form->first_field, form->field_count,
                     program->field_count) ||
        program->fields == NULL)
        return;
    for (offset = 0u; offset < form->field_count; ++offset) {
        const size_t field_index = form->first_field + offset;
        if (program->fields[field_index].root_field == root_field)
            mark_field_retry_reset(session, program, field_index);
    }
}

static vxml_status execute_clear(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    const vxml_cmeta_action_row *action) {
    size_t index;
    if (action->clear_all_form_items) {
        if (form == NULL ||
            !range_valid(form->first_block, form->block_count,
                         program->block_count) ||
            !range_valid(form->first_field, form->field_count,
                         program->field_count))
            return VXML_INVALID_STRUCTURE;
        for (index = 0u; index < form->block_count; ++index) {
            const vxml_cmeta_block_row *block =
                &program->blocks[form->first_block + index];
            if (block->form_item_slot >=
                program->scopes[form->scope].schema.slot_count)
                return VXML_INVALID_STRUCTURE;
            cmeta_scope_view_clear_slot(
                &session->staged_scopes[form->scope].view,
                block->form_item_slot);
        }
        for (index = 0u; index < form->field_count; ++index) {
            const vxml_cmeta_field_row *field =
                &program->fields[form->first_field + index];
            root_storage_clear_field(
                &session->staged_root, program, field->root_field);
            mark_field_retry_reset(
                session, program, form->first_field + index);
        }
        return VXML_OK;
    }
    if (!range_valid(action->first_location, action->location_count,
                     program->location_count))
        return VXML_INVALID_STRUCTURE;
    for (index = 0u; index < action->location_count; ++index) {
        resolved_location resolved;
        vxml_status status = resolve_staged_location(
            session, program,
            &program->locations[action->first_location + index], &resolved);
        if (status != VXML_OK) return status;
        if (resolved.scope != NULL) {
            cmeta_scope_view_clear_slot(
                resolved.scope, resolved.candidate->location.slot);
        } else {
            root_storage_clear_field(
                resolved.root, program, resolved.candidate->root_field);
            mark_form_retry_reset_by_root(
                session, program, form,
                resolved.candidate->root_field);
        }
    }
    return VXML_OK;
}

static vxml_status execute_var(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    size_t execution_scope,
    const size_t *scopes, size_t scope_count,
    const vxml_cmeta_action_row *action) {
    unsigned char *declared;
    vxml_cmeta_value_view value;
    vxml_status status;
    if (action->scope != execution_scope ||
        action->scope >= program->scope_count ||
        action->slot >= program->scopes[action->scope].schema.slot_count)
        return VXML_INVALID_STRUCTURE;
    declared = session_declared(
        session, program, true, action->scope);
    if (declared == NULL) return VXML_INVALID_STRUCTURE;
    declared[action->slot] = 1u;
    if (action->expression == VXML_CMETA_NO_INDEX) return VXML_OK;
    status = evaluate_expression(
        session, program, true, action->expression,
        scopes, scope_count, &value);
    if (status != VXML_OK) return status;
    return assign_scope_slot(
        session, program, true, action->scope, action->slot, &value);
}

static vxml_status execute_assign(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    size_t execution_scope,
    const size_t *scopes, size_t scope_count,
    const vxml_cmeta_action_row *action) {
    vxml_cmeta_value_view value;
    vxml_status status;
    if (action->scope != VXML_CMETA_NO_INDEX) {
        unsigned char *declared;
        if (action->scope >= program->scope_count ||
            action->scope != execution_scope ||
            action->slot >=
                program->scopes[action->scope].schema.slot_count)
            return VXML_INVALID_STRUCTURE;
        declared = session_declared(
            session, program, true, action->scope);
        if (declared == NULL) return VXML_INVALID_STRUCTURE;
        declared[action->slot] = 1u;
    }
    if (action->expression == VXML_CMETA_NO_INDEX)
        return VXML_OK;
    if (action->target >= program->location_count ||
        program->locations == NULL)
        return VXML_INVALID_STRUCTURE;
    status = evaluate_expression(
        session, program, true, action->expression,
        scopes, scope_count, &value);
    if (status != VXML_OK) return status;
    return assign_resolved_location(
        session, program, &program->locations[action->target], &value);
}

static vxml_status execute_exit(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const size_t *scopes, size_t scope_count,
    const vxml_cmeta_action_row *action) {
    size_t index;
    if (action->exit_kind == VXML_CMETA_EXIT_EXPRESSION) {
        vxml_cmeta_value_view value;
        vxml_status status = evaluate_expression(
            session, program, true, action->expression,
            scopes, scope_count, &value);
        if (status != VXML_OK) return status;
        status = exit_snapshot_append(
            &session->pending_exit, NULL, 0u, &value);
        if (status != VXML_OK) return status;
    } else if (action->exit_kind == VXML_CMETA_EXIT_NAMELIST) {
        if (!range_valid(
                action->first_location, action->location_count,
                program->location_count) ||
            (action->location_count != 0u && program->locations == NULL))
            return VXML_INVALID_STRUCTURE;
        for (index = 0u; index < action->location_count; ++index) {
            const vxml_cmeta_location_row *location =
                &program->locations[action->first_location + index];
            vxml_cmeta_value_view value;
            vxml_status status = read_staged_location_value(
                session, program, location, &value);
            if (status != VXML_OK) return status;
            status = exit_snapshot_append(
                &session->pending_exit,
                location->name, location->name_size, &value);
            if (status != VXML_OK) return status;
        }
    } else if (action->exit_kind != VXML_CMETA_EXIT_EMPTY) {
        return VXML_INVALID_STRUCTURE;
    }
    session->pending_exit.kind = action->exit_kind;
    session->exit_requested = true;
    return VXML_OK;
}

static vxml_status execute_throw(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_action_row *action) {
    if (session == NULL || action == NULL ||
        action->event_name == NULL ||
        action->event_name_size == 0u)
        return VXML_INVALID_STRUCTURE;
    session->thrown_event = action->event_name;
    session->thrown_event_size = action->event_name_size;
    session->throw_requested = true;
    return VXML_OK;
}

static vxml_status execute_rethrow(
    vxml_cmeta_session_data *session) {
    if (session == NULL || !session->event_dispatch_active)
        return VXML_INVALID_STATE;
    session->rethrow_requested = true;
    return VXML_OK;
}

static vxml_status execute_reprompt(
    vxml_cmeta_session_data *session) {
    if (session == NULL || !session->event_dispatch_active)
        return VXML_INVALID_STATE;
    session->handler_reprompt_requested = true;
    return VXML_OK;
}

static vxml_status select_if_branch(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const size_t *scopes, size_t scope_count,
    const vxml_cmeta_action_row *action,
    size_t action_index,
    bool *out_selected, size_t *out_first, size_t *out_end) {
    size_t branch_offset;
    *out_selected = false;
    *out_first = 0u;
    *out_end = 0u;
    if (action->branch_count == 0u ||
        !range_valid(action->first_branch, action->branch_count,
                     program->branch_count) ||
        program->branches == NULL)
        return VXML_INVALID_STRUCTURE;
    for (branch_offset = 0u; branch_offset < action->branch_count;
         ++branch_offset) {
        const vxml_cmeta_branch_row *branch =
            &program->branches[action->first_branch + branch_offset];
        bool matches = true;
        vxml_status status;
        if (branch->first_action > branch->action_end ||
            branch->first_action < action_index + 1u ||
            branch->action_end > action->next_action ||
            branch->action_end > program->action_count)
            return VXML_INVALID_STRUCTURE;
        if (branch->condition != VXML_CMETA_NO_INDEX) {
            status = evaluate_condition(
                session, program, true, branch->condition,
                scopes, scope_count, &matches);
            if (status != VXML_OK) return status;
        }
        if (!matches) continue;
        *out_selected = true;
        *out_first = branch->first_action;
        *out_end = branch->action_end;
        return VXML_OK;
    }
    return VXML_OK;
}

static vxml_status execute_action_range(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    size_t execution_scope,
    const size_t *scopes, size_t scope_count,
    size_t first_action, size_t action_end) {
    size_t frame_count = 1u;
    if (first_action > action_end ||
        !range_valid(first_action,
                     action_end - first_action,
                     program->action_count) ||
        session->exec_frame_capacity == 0u ||
        session->exec_frames == NULL ||
        (program->action_count != 0u && program->actions == NULL))
        return VXML_INVALID_STRUCTURE;
    session->exec_frames[0] = (vxml_cmeta_exec_frame){
        first_action, action_end};
    while (frame_count != 0u) {
        vxml_cmeta_exec_frame *frame =
            &session->exec_frames[frame_count - 1u];
        size_t action_index;
        const vxml_cmeta_action_row *action;
        if (frame->next_action == frame->action_end) {
            --frame_count;
            continue;
        }
        if (frame->next_action > frame->action_end ||
            frame->next_action >= program->action_count)
            return VXML_INVALID_STRUCTURE;
        action_index = frame->next_action;
        action = &program->actions[action_index];
        if (action->next_action <= action_index ||
            action->next_action > frame->action_end)
            return VXML_INVALID_STRUCTURE;
        frame->next_action = action->next_action;
        if (!consume_step(session)) return VXML_LIMIT_EXCEEDED;
        switch (action->kind) {
            case VXML_CMETA_ACTION_VAR: {
                const vxml_status status = execute_var(
                    session, program, execution_scope,
                    scopes, scope_count, action);
                if (status != VXML_OK) return status;
                break;
            }
            case VXML_CMETA_ACTION_IF: {
                bool selected;
                size_t first;
                size_t end;
                const vxml_status status = select_if_branch(
                    session, program, scopes, scope_count, action,
                    action_index,
                    &selected, &first, &end);
                if (status != VXML_OK) return status;
                if (selected && first != end) {
                    if (frame_count >= session->exec_frame_capacity)
                        return VXML_LIMIT_EXCEEDED;
                    session->exec_frames[frame_count++] =
                        (vxml_cmeta_exec_frame){first, end};
                }
                break;
            }
            case VXML_CMETA_ACTION_ASSIGN: {
                const vxml_status status = execute_assign(
                    session, program, execution_scope,
                    scopes, scope_count, action);
                if (status != VXML_OK) return status;
                break;
            }
            case VXML_CMETA_ACTION_CLEAR: {
                const vxml_status status = execute_clear(
                    session, program, form, action);
                if (status != VXML_OK) return status;
                break;
            }
            case VXML_CMETA_ACTION_EXIT: {
                const vxml_status status = execute_exit(
                    session, program, scopes, scope_count, action);
                if (status != VXML_OK) return status;
                return VXML_OK;
            }
            case VXML_CMETA_ACTION_THROW: {
                const vxml_status status =
                    execute_throw(session, action);
                if (status != VXML_OK) return status;
                return VXML_OK;
            }
            case VXML_CMETA_ACTION_RETHROW: {
                const vxml_status status =
                    execute_rethrow(session);
                if (status != VXML_OK) return status;
                return VXML_OK;
            }
            case VXML_CMETA_ACTION_REPROMPT: {
                const vxml_status status =
                    execute_reprompt(session);
                if (status != VXML_OK) return status;
                break;
            }
            default:
                return VXML_INVALID_STRUCTURE;
        }
    }
    return VXML_OK;
}

static vxml_status execute_actions(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    const vxml_cmeta_block_row *block) {
    const size_t scopes[3] = {
        block->scope, form->scope, program->document_scope};
    return execute_action_range(
        session, program, form, block->scope,
        scopes, 3u, block->first_action, block->action_end);
}

static vxml_status map_databind_runtime_status(
    DataBindStatus status) {
    switch (status) {
    case DATA_BIND_OK:
        return VXML_OK;
    case DATA_BIND_ERR_OOM:
        return VXML_ALLOCATION_FAILED;
    case DATA_BIND_ERR_LIMIT:
    case DATA_BIND_ERR_BUFFER_TOO_SMALL:
        return VXML_LIMIT_EXCEEDED;
    case DATA_BIND_ERR_PARSE:
    case DATA_BIND_ERR_TYPE_MISMATCH:
    case DATA_BIND_ERR_VALIDATION:
        return VXML_SEMANTIC_ERROR;
    case DATA_BIND_ERR_INVALID_ARG:
    case DATA_BIND_ERR_SCHEMA:
    case DATA_BIND_ERR_TYPE_NOT_FOUND:
        return VXML_INVALID_CONTRACT;
    case DATA_BIND_ERR_CANCELED:
    case DATA_BIND_ERR_IO:
    case DATA_BIND_ERR_RUNTIME:
    default:
        return VXML_INVALID_STATE;
    }
}

static bool align_buffer(
    unsigned char *allocation, size_t allocation_bytes,
    size_t alignment, size_t required_bytes,
    unsigned char **out) {
    uintptr_t address;
    uintptr_t aligned;
    size_t adjustment;
    if (out == NULL || !valid_alignment(alignment))
        return false;
    *out = NULL;
    if (required_bytes == 0u) return true;
    if (allocation == NULL || allocation_bytes < required_bytes)
        return false;
    address = (uintptr_t)allocation;
    if (address > UINTPTR_MAX - (alignment - 1u))
        return false;
    aligned = (address + alignment - 1u) &
        ~((uintptr_t)alignment - 1u);
    adjustment = (size_t)(aligned - address);
    if (adjustment > allocation_bytes ||
        required_bytes > allocation_bytes - adjustment)
        return false;
    *out = (unsigned char *)aligned;
    return true;
}

static bool collect_fixed_scalar_data(
    const cmeta_data_desc *data) {
    if (!cmeta_data_desc_valid(data) ||
        data->storage_type == NULL ||
        data->storage_type->size == 0u ||
        !valid_alignment(data->storage_type->align))
        return false;
    switch (data->kind) {
    case CMETA_DATA_BOOL:
    case CMETA_DATA_SINT:
    case CMETA_DATA_UINT:
    case CMETA_DATA_FLOAT:
        return true;
    default:
        return false;
    }
}

static bool measure_collect_mailbox(
    const vxml_cmeta_program_data *program,
    size_t *out_bytes, size_t *out_alignment) {
    size_t bytes = 0u;
    size_t alignment = 1u;
    size_t index;
    if (program == NULL || out_bytes == NULL ||
        out_alignment == NULL)
        return false;
    for (index = 0u; index < program->field_count; ++index) {
        const vxml_cmeta_field_row *field =
            &program->fields[index];
        const cmeta_type_desc *type =
            field->field_data != NULL
                ? field->field_data->storage_type : NULL;
        if (!collect_fixed_scalar_data(field->field_data))
            continue;
        if (type->size > bytes) bytes = type->size;
        if (type->align > alignment) alignment = type->align;
    }
    *out_bytes = bytes;
    *out_alignment = alignment;
    return true;
}

static bool measure_external_data_scratch(
    const vxml_cmeta_program_data *program,
    size_t *out_workspace_bytes,
    size_t *out_workspace_alignment,
    size_t *out_value_bytes,
    size_t *out_value_alignment) {
    size_t workspace_bytes = 0u;
    size_t workspace_alignment = 1u;
    size_t value_bytes = 0u;
    size_t value_alignment = 1u;
    size_t index;
    if (program == NULL || out_workspace_bytes == NULL ||
        out_workspace_alignment == NULL ||
        out_value_bytes == NULL || out_value_alignment == NULL)
        return false;
    for (index = 0u; index < program->external_data_count; ++index) {
        const vxml_cmeta_external_data_row *row =
            &program->external_data[index];
        const cmeta_type_desc *type =
            row->field_data != NULL ? row->field_data->storage_type : NULL;
        if (row->plan == NULL || type == NULL ||
            !valid_alignment(row->workspace_alignment) ||
            !valid_alignment(type->align) ||
            type->size == 0u)
            return false;
        if (row->decode_workspace_bytes > workspace_bytes)
            workspace_bytes = row->decode_workspace_bytes;
        if (row->workspace_alignment > workspace_alignment)
            workspace_alignment = row->workspace_alignment;
        if (type->size > value_bytes)
            value_bytes = type->size;
        if (type->align > value_alignment)
            value_alignment = type->align;
    }
    *out_workspace_bytes = workspace_bytes;
    *out_workspace_alignment = workspace_alignment;
    *out_value_bytes = value_bytes;
    *out_value_alignment = value_alignment;
    return true;
}

static vxml_status load_external_data(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_session_options_v1 *options) {
    const cmeta_data_struct_shape *root_shape =
        session_root_shape(program);
    size_t index;
    if (program->external_data_count == 0u)
        return VXML_OK;
    if (!session_data_options_valid(options) ||
        root_shape == NULL || program->external_data == NULL)
        return VXML_INVALID_CONTRACT;

    for (index = 0u; index < program->external_data_count; ++index) {
        const vxml_cmeta_external_data_row *row =
            &program->external_data[index];
        vxml_cmeta_data_resource_v1 resource = {0};
        DataBindFormatReader format_reader =
            DATA_BIND_FORMAT_READER_INIT;
        DataBindError format_error = DATA_BIND_ERROR_INIT;
        DataBindNativeDiagnostic native_diagnostic =
            DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
        DataBindNativeOptions native_options =
            DATA_BIND_NATIVE_OPTIONS_INIT;
        const DataBindFormatProvider *provider;
        cmeta_status meta_status;
        DataBindStatus bind_status;
        vxml_status status;
        bool resource_open = false;
        bool reader_open = false;
        unsigned char *destination;

        if (row->field_index >= root_shape->field_count ||
            row->field_data == NULL ||
            root_shape->fields[row->field_index].value != row->field_data ||
            root_shape->fields[row->field_index].offset != row->field_offset)
            return VXML_INVALID_STRUCTURE;

        status = options->data_resources->open(
            options->data_resource_user,
            row->uri, row->uri_size,
            options->max_data_bytes, &resource);
        if (status != VXML_OK)
            return status;
        resource_open = true;
        if (resource.lease == NULL ||
            resource.size > options->max_data_bytes ||
            (resource.size != 0u && resource.data == NULL)) {
            status = resource.size > options->max_data_bytes
                ? VXML_LIMIT_EXCEEDED : VXML_INVALID_CONTRACT;
            goto settle;
        }

        provider = data_format_provider(resource.format);
        if (provider == NULL) {
            status = VXML_UNSUPPORTED_FEATURE;
            goto settle;
        }
        bind_status = data_bind_format_reader_open(
            provider,
            (const char *)resource.data, resource.size,
            program->max_data_bind_depth,
            &format_reader, &format_error);
        if (bind_status != DATA_BIND_OK) {
            status = map_databind_runtime_status(bind_status);
            goto settle;
        }
        reader_open = true;

        memset(session->data_value, 0, session->data_value_bytes);
        meta_status = cmeta_data_value_init_zero(
            row->field_data, session->data_value);
        if (meta_status != CMETA_OK) {
            status = VXML_INVALID_CONTRACT;
            goto settle;
        }

        native_options.workspace = session->data_workspace;
        native_options.workspace_bytes = session->data_workspace_bytes;
        native_options.max_depth = program->max_data_bind_depth;
        native_options.max_items = program->max_data_bind_items;
        native_options.max_owned_bytes = options->max_data_owned_bytes;
        bind_status = data_bind_native_plan_decode(
            row->plan, &native_options,
            format_reader.reader,
            session->data_value,
            row->field_data->storage_type->size,
            &native_diagnostic);
        if (bind_status != DATA_BIND_OK) {
            (void)cmeta_data_value_restore_zero(
                row->field_data, session->data_value);
            status = map_databind_runtime_status(bind_status);
            goto settle;
        }

        destination =
            session->committed_root.storage + row->field_offset;
        meta_status = session->committed_root.bound[row->field_index] != 0u
            ? cmeta_data_value_restore_zero(row->field_data, destination)
            : cmeta_data_value_init_zero(row->field_data, destination);
        if (meta_status != CMETA_OK) {
            (void)cmeta_data_value_restore_zero(
                row->field_data, session->data_value);
            status = VXML_INVALID_CONTRACT;
            goto settle;
        }
        meta_status = cmeta_data_value_move(
            row->field_data, destination, session->data_value);
        if (meta_status != CMETA_OK) {
            (void)cmeta_data_value_restore_zero(
                row->field_data, destination);
            (void)cmeta_data_value_restore_zero(
                row->field_data, session->data_value);
            session->committed_root.bound[row->field_index] = 0u;
            status = VXML_INVALID_CONTRACT;
            goto settle;
        }
        session->committed_root.bound[row->field_index] = 1u;
        status = VXML_OK;

settle:
        if (reader_open)
            (void)data_bind_format_reader_close(&format_reader);
        if (resource_open)
            options->data_resources->close(
                options->data_resource_user, &resource);
        if (status != VXML_OK)
            return status;
    }
    return VXML_OK;
}

vxml_status vxml_cmeta_session_init_profile(
    vxml_session_impl *session, const void *options_pointer) {
    const vxml_cmeta_session_options_v1 *options =
        (const vxml_cmeta_session_options_v1 *)options_pointer;
    const vxml_cmeta_program_data *program;
    const cmeta_data_struct_shape *root_shape;
    vxml_cmeta_session_data *profile = NULL;
    unsigned char *undefined = NULL;
    size_t scope_index;
    vxml_status status = VXML_INVALID_CONTRACT;
    if (session == NULL || session->profile_data != NULL ||
        !session_options_valid(options))
        return VXML_INVALID_CONTRACT;
    if (session->program == NULL || session->program->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    program = (const vxml_cmeta_program_data *)session->program->profile_data;
    root_shape = session_root_shape(program);
    if (!session_root_contract_valid(program, options))
        return VXML_INVALID_CONTRACT;
    profile = (vxml_cmeta_session_data *)vxml_calloc(1u, sizeof(*profile));
    if (profile == NULL) return VXML_ALLOCATION_FAILED;
    profile->max_transaction_bytes = options->max_transaction_bytes;
    profile->max_execution_steps = options->max_execution_steps;
    atomic_init(
        &profile->collect_mailbox.state,
        VXML_CMETA_COLLECT_MAILBOX_DISARMED);
    atomic_init(&profile->collect_mailbox.generation, UINT64_C(0));
    profile->active_form = VXML_CMETA_NO_INDEX;
    profile->active_field = VXML_CMETA_NO_INDEX;
    profile->active_block = VXML_CMETA_NO_INDEX;
    {
        const size_t prompt_tail =
            offsetof(vxml_cmeta_session_options_v1, prompt_media_user) +
            sizeof(options->prompt_media_user);
        if (options->struct_size >= prompt_tail &&
            options->prompt_media != NULL) {
            if (!session_prompt_media_options_valid(options)) {
                status = VXML_INVALID_CONTRACT;
                goto failure;
            }
            profile->prompt_media_adapter = options->prompt_media;
            profile->prompt_media_user = options->prompt_media_user;
        }
    }
    if (program->event_handler_count != 0u) {
        size_t name_bytes;
        size_t index;
        if (program->event_handlers == NULL ||
            !session_event_options_valid(options) ||
            !checked_multiply(
                options->max_event_counters,
                options->max_event_name_bytes + 1u,
                &name_bytes)) {
            status = VXML_INVALID_CONTRACT;
            goto failure;
        }
        profile->event_counter_capacity =
            options->max_event_counters;
        profile->event_name_stride =
            options->max_event_name_bytes + 1u;
        profile->max_event_dispatch_depth =
            options->max_event_dispatch_depth;
        profile->event_counters =
            (vxml_cmeta_event_counter *)vxml_calloc(
                profile->event_counter_capacity,
                sizeof(*profile->event_counters));
        profile->event_counter_names =
            (char *)vxml_calloc(name_bytes, 1u);
        if (profile->event_counters == NULL ||
            profile->event_counter_names == NULL) {
            status = VXML_ALLOCATION_FAILED;
            goto failure;
        }
        for (index = 0u;
             index < profile->event_counter_capacity;
             ++index)
            profile->event_counters[index].event =
                profile->event_counter_names +
                index * profile->event_name_stride;
    }
    if (program->field_count != 0u) {
        if (program->fields == NULL ||
            !session_collect_options_valid(options)) {
            status = VXML_INVALID_CONTRACT;
            goto failure;
        }
        profile->collect_adapter = options->collect;
        profile->collect_user = options->collect_user;
        profile->retry_reset_pending =
            (unsigned char *)vxml_calloc(
                program->field_count, sizeof(unsigned char));
        if (profile->retry_reset_pending == NULL) {
            status = VXML_ALLOCATION_FAILED;
            goto failure;
        }
    }
    if (root_shape->field_count != 0u) {
        undefined = (unsigned char *)vxml_calloc(
            root_shape->field_count, sizeof(*undefined));
        if (undefined == NULL) {
            status = VXML_ALLOCATION_FAILED;
            goto failure;
        }
    }
    if (!initial_undefined_map(program, options, undefined)) goto failure;
    profile->declared_offsets = (size_t *)vxml_calloc(
        program->scope_count + 1u, sizeof(*profile->declared_offsets));
    profile->committed_scopes = (cmeta_scope_storage *)vxml_calloc(
        program->scope_count, sizeof(*profile->committed_scopes));
    profile->staged_scopes = (cmeta_scope_storage *)vxml_calloc(
        program->scope_count, sizeof(*profile->staged_scopes));
    if (profile->declared_offsets == NULL ||
        (program->scope_count != 0u &&
         (profile->committed_scopes == NULL ||
          profile->staged_scopes == NULL))) {
        status = VXML_ALLOCATION_FAILED;
        goto failure;
    }
    for (scope_index = 0u; scope_index < program->scope_count;
         ++scope_index) {
        const size_t slot_count = program->scopes[scope_index].schema.slot_count;
        profile->declared_offsets[scope_index] = profile->declared_count;
        if (!checked_add(&profile->declared_count, slot_count)) goto failure;
    }
    profile->declared_offsets[program->scope_count] = profile->declared_count;
    if (!transaction_bytes_measure(
            program, profile->declared_count,
            &profile->transaction_bytes_required,
            &profile->exec_frame_capacity) ||
        profile->transaction_bytes_required > options->max_transaction_bytes) {
        status = VXML_LIMIT_EXCEEDED;
        goto failure;
    }
    if (profile->declared_count != 0u) {
        profile->committed_declared = (unsigned char *)vxml_calloc(
            profile->declared_count, sizeof(*profile->committed_declared));
        profile->staged_declared = (unsigned char *)vxml_calloc(
            profile->declared_count, sizeof(*profile->staged_declared));
        if (profile->committed_declared == NULL ||
            profile->staged_declared == NULL) {
            status = VXML_ALLOCATION_FAILED;
            goto failure;
        }
    }
    profile->expression_scratch_bytes = program->expression_scratch_bytes;
    if (profile->expression_scratch_bytes != 0u) {
        profile->expression_scratch = (unsigned char *)vxml_malloc(
            profile->expression_scratch_bytes);
        if (profile->expression_scratch == NULL) {
            status = VXML_ALLOCATION_FAILED;
            goto failure;
        }
    }
    profile->read_scratch_bytes = program->max_string_bytes;
    profile->read_scratch = (unsigned char *)vxml_malloc(
        profile->read_scratch_bytes);
    if (profile->read_scratch == NULL) {
        status = VXML_ALLOCATION_FAILED;
        goto failure;
    }
    if (program->field_count != 0u) {
        const size_t multi_tail_size =
            offsetof(
                vxml_cmeta_session_options_v1,
                max_collect_result_slots) +
            sizeof(options->max_collect_result_slots);
        size_t requested_slots = 1u;
        size_t mailbox_allocation_bytes;
        size_t remainder;
        if (options->struct_size >= multi_tail_size &&
            options->max_collect_result_slots != 0u)
            requested_slots = options->max_collect_result_slots;
        if (root_shape == NULL || root_shape->field_count == 0u) {
            status = VXML_INVALID_CONTRACT;
            goto failure;
        }
        if (requested_slots > root_shape->field_count)
            requested_slots = root_shape->field_count;
        profile->collect_mailbox.slot_capacity = requested_slots;
        profile->collect_mailbox.root_fields =
            (size_t *)vxml_calloc(
                requested_slots,
                sizeof(*profile->collect_mailbox.root_fields));
        if (profile->collect_mailbox.root_fields == NULL) {
            status = VXML_ALLOCATION_FAILED;
            goto failure;
        }
        if (!measure_collect_mailbox(
                program,
                &profile->collect_mailbox.storage_bytes,
                &profile->collect_mailbox.storage_alignment)) {
            status = VXML_INVALID_CONTRACT;
            goto failure;
        }
        profile->collect_mailbox.storage_stride =
            profile->collect_mailbox.storage_bytes;
        remainder = profile->collect_mailbox.storage_stride %
            profile->collect_mailbox.storage_alignment;
        if (remainder != 0u &&
            !checked_add(
                &profile->collect_mailbox.storage_stride,
                profile->collect_mailbox.storage_alignment - remainder)) {
            status = VXML_LIMIT_EXCEEDED;
            goto failure;
        }
        if (!checked_multiply(
                profile->collect_mailbox.storage_stride,
                requested_slots,
                &mailbox_allocation_bytes) ||
            (profile->collect_mailbox.storage_alignment > 1u &&
             !checked_add(
                &mailbox_allocation_bytes,
                profile->collect_mailbox.storage_alignment - 1u))) {
            status = VXML_LIMIT_EXCEEDED;
            goto failure;
        }
        if (mailbox_allocation_bytes != 0u) {
            profile->collect_mailbox.allocation =
                vxml_malloc(mailbox_allocation_bytes);
            if (profile->collect_mailbox.allocation == NULL ||
                !align_buffer(
                    (unsigned char *)
                        profile->collect_mailbox.allocation,
                    mailbox_allocation_bytes,
                    profile->collect_mailbox.storage_alignment,
                    profile->collect_mailbox.storage_stride *
                        requested_slots,
                    &profile->collect_mailbox.storage)) {
                status = VXML_ALLOCATION_FAILED;
                goto failure;
            }
        }
    }
    if (program->external_data_count != 0u) {
        size_t workspace_alignment = 1u;
        size_t value_alignment = 1u;
        size_t workspace_allocation_bytes;
        size_t value_allocation_bytes;
        if (!session_data_options_valid(options) ||
            !measure_external_data_scratch(
                program,
                &profile->data_workspace_bytes,
                &workspace_alignment,
                &profile->data_value_bytes,
                &value_alignment)) {
            status = VXML_INVALID_CONTRACT;
            goto failure;
        }
        workspace_allocation_bytes = profile->data_workspace_bytes;
        value_allocation_bytes = profile->data_value_bytes;
        if ((workspace_alignment > 1u &&
             !checked_add(
                 &workspace_allocation_bytes,
                 workspace_alignment - 1u)) ||
            (value_alignment > 1u &&
             !checked_add(
                 &value_allocation_bytes,
                 value_alignment - 1u))) {
            status = VXML_LIMIT_EXCEEDED;
            goto failure;
        }
        if (workspace_allocation_bytes != 0u) {
            profile->data_workspace_allocation =
                (unsigned char *)vxml_malloc(
                    workspace_allocation_bytes);
            if (profile->data_workspace_allocation == NULL ||
                !align_buffer(
                    profile->data_workspace_allocation,
                    workspace_allocation_bytes,
                    workspace_alignment,
                    profile->data_workspace_bytes,
                    &profile->data_workspace)) {
                status = VXML_ALLOCATION_FAILED;
                goto failure;
            }
        }
        profile->data_value_allocation =
            (unsigned char *)vxml_malloc(value_allocation_bytes);
        if (profile->data_value_allocation == NULL ||
            !align_buffer(
                profile->data_value_allocation,
                value_allocation_bytes,
                value_alignment,
                profile->data_value_bytes,
                &profile->data_value)) {
            status = VXML_ALLOCATION_FAILED;
            goto failure;
        }
    }
    profile->runtime_scope_capacity = 3u;
    profile->runtime_scopes = (vxml_cmeta_expr_runtime_scope *)vxml_calloc(
        profile->runtime_scope_capacity, sizeof(*profile->runtime_scopes));
    profile->exec_frames = (vxml_cmeta_exec_frame *)vxml_calloc(
        profile->exec_frame_capacity, sizeof(*profile->exec_frames));
    if (profile->runtime_scopes == NULL || profile->exec_frames == NULL) {
        status = VXML_ALLOCATION_FAILED;
        goto failure;
    }
    if (!root_storage_init(&profile->committed_root, program) ||
        !root_storage_init(&profile->staged_root, program)) {
        status = VXML_ALLOCATION_FAILED;
        goto failure;
    }
    for (scope_index = 0u; scope_index < program->scope_count;
         ++scope_index) {
        const cmeta_scope_schema *schema =
            &program->scopes[scope_index].schema;
        if (!session_scope_storage_init(
                &profile->committed_scopes[scope_index], schema) ||
            !session_scope_storage_init(
                &profile->staged_scopes[scope_index], schema)) {
            status = VXML_ALLOCATION_FAILED;
            goto failure;
        }
    }
    status = root_storage_copy_initial(
        &profile->committed_root, program, options->initial_root, undefined);
    if (status != VXML_OK) goto failure;
    status = load_external_data(profile, program, options);
    if (status != VXML_OK) goto failure;
    vxml_free(undefined);
    session->profile_data = profile;
    return VXML_OK;

failure:
    vxml_free(undefined);
    session_data_destroy(profile, program);
    return status;
}

static vxml_status select_directed_field(
    vxml_session_impl *session,
    const vxml_cmeta_program_data *program,
    vxml_cmeta_session_data *profile,
    const vxml_cmeta_form_row *form,
    size_t form_index) {
    const cmeta_data_struct_shape *root_shape =
        session_root_shape(program);
    size_t field_offset;
    vxml_status status;

    settle_prompt_media(profile);
    profile->active_field = VXML_CMETA_NO_INDEX;
    profile->active_block = VXML_CMETA_NO_INDEX;
    if (root_shape == NULL ||
        (root_shape->field_count != 0u &&
         profile->committed_root.bound == NULL))
        return session_fail(session, VXML_INVALID_STRUCTURE);

    for (field_offset = 0u;
         field_offset < form->field_count;
         ++field_offset) {
        const size_t field_index =
            form->first_field + field_offset;
        const vxml_cmeta_field_row *field =
            &program->fields[field_index];
        bool eligible = true;
        if (field->form != form_index ||
            field->root_field >= root_shape->field_count ||
            root_shape->fields[field->root_field].value !=
                field->field_data ||
            root_shape->fields[field->root_field].offset !=
                field->field_offset)
            return session_fail(session, VXML_INVALID_STRUCTURE);
        if (profile->committed_root.bound[field->root_field] != 0u)
            continue;
        if (field->condition != VXML_CMETA_NO_INDEX) {
            const size_t scopes[2] = {
                form->scope, program->document_scope};
            status = evaluate_condition(
                profile, program, false,
                field->condition, scopes, 2u, &eligible);
            if (status != VXML_OK)
                return session_fail(session, status);
        }
        if (!eligible) continue;
        profile->active_field = field_index;
        ++profile->collect_generation;
        if (profile->collect_generation == 0u)
            profile->collect_generation = 1u;
        return VXML_OK;
    }

    session->state = VXML_SESSION_EXITED;
    session->error = VXML_OK;
    return VXML_OK;
}

vxml_status vxml_cmeta_session_start_profile_at(
    vxml_session_impl *session, size_t form_index) {
    const vxml_cmeta_program_data *program;
    vxml_cmeta_session_data *profile;
    const vxml_cmeta_form_row *form;
    size_t block_offset;
    vxml_status status;
    if (session == NULL || session->program == NULL ||
        session->program->profile_data == NULL ||
        session->profile_data == NULL)
        return session_fail(session, VXML_INVALID_STRUCTURE);
    program = (const vxml_cmeta_program_data *)session->program->profile_data;
    profile = (vxml_cmeta_session_data *)session->profile_data;
    if (program->form_count == 0u || program->forms == NULL ||
        form_index >= program->form_count)
        return session_fail(session, VXML_INVALID_STRUCTURE);
    form = &program->forms[form_index];
    if (program->document_scope >= program->scope_count ||
        form->scope >= program->scope_count ||
        !range_valid(form->first_field, form->field_count,
                     program->field_count) ||
        !range_valid(form->first_block, form->block_count,
                     program->block_count) ||
        (form->field_count != 0u &&
         (program->fields == NULL || form->block_count != 0u)) ||
        (form->block_count != 0u && program->blocks == NULL) ||
        (form->field_count == 0u && form->block_count == 0u))
        return session_fail(session, VXML_INVALID_STRUCTURE);
    profile->active_form = form_index;
    reset_form_retry_counters(profile, program, form);
    profile->reprompt_requested = false;
    profile->handler_reprompt_requested = false;
    if (!transaction_begin(profile, program))
        return session_fail(session, VXML_ALLOCATION_FAILED);
    status = initialize_form(profile, program, form, form_index);
    if (status != VXML_OK) {
        transaction_reset(profile, program);
        return session_fail(session, status);
    }
    transaction_commit(profile, program);
    if (form->field_count != 0u)
        return select_directed_field(
            session, program, profile, form, form_index);
    for (;;) {
        const vxml_cmeta_block_row *selected = NULL;
        profile->active_block = VXML_CMETA_NO_INDEX;
        for (block_offset = 0u; block_offset < form->block_count;
             ++block_offset) {
            const vxml_cmeta_block_row *block =
                &program->blocks[form->first_block + block_offset];
            if (profile->committed_scopes[form->scope]
                    .view.bound[block->form_item_slot] == 0u) {
                if (block->condition != VXML_CMETA_NO_INDEX) {
                    const size_t scopes[3] = {
                        block->scope, form->scope,
                        program->document_scope};
                    bool eligible;
                    status = evaluate_condition(
                        profile, program, false, block->condition,
                        scopes, 3u, &eligible);
                    if (status != VXML_OK)
                        return session_fail(session, status);
                    if (!eligible) continue;
                }
                selected = block;
                break;
            }
        }
        if (selected == NULL) {
            session->state = VXML_SESSION_EXITED;
            session->error = VXML_OK;
            return VXML_OK;
        }
        profile->active_block = (size_t)(selected - program->blocks);
        if (!consume_step(profile))
            return session_fail(session, VXML_LIMIT_EXCEEDED);
        status = exit_snapshot_prepare_range(
            &profile->pending_exit, program,
            selected->first_action, selected->action_end);
        if (status != VXML_OK) return session_fail(session, status);
        profile->exit_requested = false;
        if (!transaction_begin(profile, program)) {
            exit_snapshot_destroy(&profile->pending_exit);
            return session_fail(session, VXML_ALLOCATION_FAILED);
        }
        {
            const bool completed = true;
            if (!cmeta_scope_view_assign(
                    &profile->staged_scopes[form->scope].view,
                    selected->form_item_slot, &completed)) {
                transaction_reset(profile, program);
                exit_snapshot_destroy(&profile->pending_exit);
                return session_fail(session, VXML_ALLOCATION_FAILED);
            }
            status = execute_actions(
                profile, program, form, selected);
            if (status != VXML_OK) {
                transaction_reset(profile, program);
                exit_snapshot_destroy(&profile->pending_exit);
                return session_fail(session, status);
            }
        }
        transaction_commit(profile, program);
        if (profile->exit_requested) {
            exit_snapshot_publish(profile);
            session->state = VXML_SESSION_EXITED;
            session->error = VXML_OK;
            return VXML_OK;
        }
        exit_snapshot_destroy(&profile->pending_exit);
    }
}

vxml_status vxml_cmeta_session_start_profile(vxml_session_impl *session) {
    return vxml_cmeta_session_start_profile_at(session, 0u);
}

void vxml_cmeta_session_destroy_profile(vxml_session_impl *session) {
    const vxml_cmeta_program_data *program;
    if (session == NULL || session->profile_data == NULL ||
        session->program == NULL || session->program->profile_data == NULL)
        return;
    program = (const vxml_cmeta_program_data *)session->program->profile_data;
    session_data_destroy(
        (vxml_cmeta_session_data *)session->profile_data, program);
    session->profile_data = NULL;
}

vxml_status vxml_session_init_cmeta(
    vxml_session *session, const vxml_program *program,
    const vxml_cmeta_session_options_v1 *options) {
    if (session == NULL) return VXML_INVALID_ARGUMENT;
    session->impl = NULL;
    if (!session_options_valid(options)) return VXML_INVALID_CONTRACT;
    if (program == NULL || program->impl == NULL) return VXML_INVALID_ARGUMENT;
    if (((const vxml_program_impl *)program->impl)->profile_kind !=
        VXML_PROFILE_CMETA)
        return VXML_INVALID_CONTRACT;
    if (!session_root_contract_valid(
            (const vxml_cmeta_program_data *)
                ((const vxml_program_impl *)program->impl)->profile_data,
            options))
        return VXML_INVALID_CONTRACT;
    return vxml_session_init_profile(session, program, options);
}

static const vxml_session_impl *cmeta_session(const vxml_session *session) {
    const vxml_session_impl *impl;
    if (session == NULL || session->impl == NULL) return NULL;
    impl = (const vxml_session_impl *)session->impl;
    return impl->program != NULL &&
            impl->program->profile_kind == VXML_PROFILE_CMETA
        ? impl : NULL;
}

static vxml_status collect_request_from_impl(
    const vxml_session_impl *impl,
    vxml_cmeta_collect_request_v1 *out_request) {
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_session_data *profile;
    const vxml_cmeta_field_row *field;
    if (out_request == NULL) return VXML_INVALID_ARGUMENT;
    *out_request = (vxml_cmeta_collect_request_v1){0};
    if (impl == NULL || impl->state != VXML_SESSION_RUNNING ||
        impl->program == NULL || impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_STATE;
    program = (const vxml_cmeta_program_data *)impl->program->profile_data;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    if (profile->active_field == VXML_CMETA_NO_INDEX ||
        profile->active_field >= program->field_count ||
        program->fields == NULL ||
        profile->collect_generation == 0u)
        return VXML_INVALID_STATE;
    field = &program->fields[profile->active_field];
    if (field->name == NULL || field->name_size == 0u ||
        field->grammar_type == NULL || field->grammar_type_size == 0u ||
        field->grammar_src == NULL || field->grammar_src_size == 0u)
        return VXML_INVALID_STRUCTURE;
    *out_request = (vxml_cmeta_collect_request_v1){
        .abi_version = VXML_CMETA_COLLECT_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_collect_request_v1),
        .generation = profile->collect_generation,
        .required_capabilities = field->required_capabilities,
        .field = {field->name, field->name_size},
        .grammar_type = {field->grammar_type, field->grammar_type_size},
        .grammar_src = {field->grammar_src, field->grammar_src_size}
    };
    return VXML_OK;
}

vxml_status vxml_session_cmeta_collect_request(
    const vxml_session *session,
    vxml_cmeta_collect_request_v1 *out_request) {
    const vxml_session_impl *impl = cmeta_session(session);
    if (out_request != NULL)
        *out_request = (vxml_cmeta_collect_request_v1){0};
    if (out_request == NULL) return VXML_INVALID_ARGUMENT;
    if (impl == NULL) return VXML_INVALID_CONTRACT;
    return collect_request_from_impl(impl, out_request);
}

vxml_status vxml_session_cmeta_collect_prepare(
    vxml_session *session, const char **out_error) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_collect_request_v1 request;
    vxml_cmeta_collect_ticket_v1 ticket = {0};
    vxml_status status;
    if (out_error != NULL) *out_error = NULL;
    if (session == NULL || session->impl == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA)
        return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_RUNNING ||
        impl->profile_data == NULL)
        return VXML_INVALID_STATE;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (profile->collect_adapter == NULL ||
        profile->collect_adapter->prepare == NULL)
        return VXML_INVALID_CONTRACT;
    if (profile->collect_prepared || profile->collect_in_flight)
        return VXML_INVALID_STATE;
    status = collect_request_from_impl(impl, &request);
    if (status != VXML_OK) return status;
    {
        const vxml_cmeta_program_data *program =
            (const vxml_cmeta_program_data *)impl->program->profile_data;
        if (profile->active_field >= program->field_count ||
            program->fields == NULL ||
            !collect_fixed_scalar_data(
                program->fields[profile->active_field].field_data))
            return VXML_UNSUPPORTED_FEATURE;
    }
    if ((profile->collect_adapter->capabilities &
         request.required_capabilities) !=
        request.required_capabilities)
        return VXML_UNSUPPORTED_FEATURE;

    status = profile->collect_adapter->prepare(
        profile->collect_user, &request, &ticket, out_error);
    if (status != VXML_OK) {
        if (ticket.discard != NULL)
            ticket.discard(ticket.user);
        return status;
    }
    if (ticket.commit == NULL || ticket.discard == NULL) {
        if (ticket.discard != NULL)
            ticket.discard(ticket.user);
        return VXML_INVALID_CONTRACT;
    }
    profile->collect_ticket = ticket;
    profile->collect_prepared = true;
    return VXML_OK;
}

vxml_status vxml_session_cmeta_collect_commit(vxml_session *session) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_collect_ticket_v1 ticket;
    if (session == NULL || session->impl == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA)
        return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_RUNNING ||
        impl->profile_data == NULL)
        return VXML_INVALID_STATE;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (!profile->collect_prepared || profile->collect_in_flight ||
        profile->collect_ticket.commit == NULL ||
        profile->collect_ticket.discard == NULL)
        return VXML_INVALID_STATE;
    {
        const vxml_cmeta_program_data *program =
            (const vxml_cmeta_program_data *)impl->program->profile_data;
        const vxml_cmeta_field_row *field;
        unsigned state;
        if (profile->active_field >= program->field_count ||
            program->fields == NULL)
            return VXML_INVALID_STRUCTURE;
        field = &program->fields[profile->active_field];
        if (!collect_fixed_scalar_data(field->field_data) ||
            profile->collect_mailbox.storage == NULL ||
            field->field_data->storage_type->size >
                profile->collect_mailbox.storage_bytes)
            return VXML_UNSUPPORTED_FEATURE;
        state = atomic_load_explicit(
            &profile->collect_mailbox.state, memory_order_acquire);
        if (state != VXML_CMETA_COLLECT_MAILBOX_DISARMED)
            return VXML_INVALID_STATE;
        profile->collect_mailbox.data = field->field_data;
        atomic_store_explicit(
            &profile->collect_mailbox.generation,
            profile->collect_generation,
            memory_order_relaxed);
        atomic_store_explicit(
            &profile->collect_mailbox.state,
            VXML_CMETA_COLLECT_MAILBOX_EMPTY,
            memory_order_release);
    }
    ticket = profile->collect_ticket;
    profile->collect_ticket = (vxml_cmeta_collect_ticket_v1){0};
    profile->collect_prepared = false;
    profile->collect_in_flight = true;
    ticket.commit(ticket.user);
    return VXML_OK;
}

vxml_status vxml_session_cmeta_collect_discard(vxml_session *session) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_collect_ticket_v1 ticket;
    if (session == NULL || session->impl == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA)
        return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_RUNNING ||
        impl->profile_data == NULL)
        return VXML_INVALID_STATE;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (!profile->collect_prepared ||
        profile->collect_ticket.commit == NULL ||
        profile->collect_ticket.discard == NULL)
        return VXML_INVALID_STATE;
    ticket = profile->collect_ticket;
    profile->collect_ticket = (vxml_cmeta_collect_ticket_v1){0};
    profile->collect_prepared = false;
    ticket.discard(ticket.user);
    return VXML_OK;
}

vxml_cmeta_collect_ingress_result vxml_session_cmeta_collect_try_complete(
    vxml_session *session,
    const vxml_cmeta_collect_completion_v1 *completion) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_collect_mailbox *mailbox;
    const cmeta_type_desc *type;
    uint64_t generation;
    unsigned state;
    unsigned expected;

    if (session == NULL || completion == NULL)
        return VXML_CMETA_COLLECT_INGRESS_INVALID_ARGUMENT;
    if (completion->abi_version !=
            VXML_CMETA_COLLECT_COMPLETION_ABI_V1 ||
        completion->struct_size < sizeof(*completion) ||
        completion->generation == 0u ||
        completion->data == NULL ||
        completion->value == NULL)
        return VXML_CMETA_COLLECT_INGRESS_INVALID_ARGUMENT;

    impl = (vxml_session_impl *)session->impl;
    if (impl == NULL)
        return VXML_CMETA_COLLECT_INGRESS_CLOSED;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->profile_data == NULL)
        return impl->state == VXML_SESSION_CLOSED
            ? VXML_CMETA_COLLECT_INGRESS_CLOSED
            : VXML_CMETA_COLLECT_INGRESS_INVALID_ARGUMENT;
    if (impl->state == VXML_SESSION_CLOSED)
        return VXML_CMETA_COLLECT_INGRESS_CLOSED;

    profile = (vxml_cmeta_session_data *)impl->profile_data;
    mailbox = &profile->collect_mailbox;
    state = atomic_load_explicit(
        &mailbox->state, memory_order_acquire);
    if (state == VXML_CMETA_COLLECT_MAILBOX_CLOSED)
        return VXML_CMETA_COLLECT_INGRESS_CLOSED;
    if (state == VXML_CMETA_COLLECT_MAILBOX_DISARMED)
        return VXML_CMETA_COLLECT_INGRESS_STALE;
    if (state == VXML_CMETA_COLLECT_MAILBOX_WRITING ||
        state == VXML_CMETA_COLLECT_MAILBOX_READY)
        return VXML_CMETA_COLLECT_INGRESS_FULL;
    if (state != VXML_CMETA_COLLECT_MAILBOX_EMPTY)
        return VXML_CMETA_COLLECT_INGRESS_INVALID_ARGUMENT;

    generation = atomic_load_explicit(
        &mailbox->generation, memory_order_relaxed);
    if (completion->generation != generation)
        return VXML_CMETA_COLLECT_INGRESS_STALE;
    if (!cmeta_data_desc_equal(completion->data, mailbox->data) ||
        !collect_fixed_scalar_data(completion->data))
        return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;
    type = completion->data->storage_type;
    if (mailbox->storage == NULL ||
        type->size > mailbox->storage_bytes)
        return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;

    expected = VXML_CMETA_COLLECT_MAILBOX_EMPTY;
    if (!atomic_compare_exchange_strong_explicit(
            &mailbox->state, &expected,
            VXML_CMETA_COLLECT_MAILBOX_WRITING,
            memory_order_acq_rel, memory_order_acquire)) {
        if (expected == VXML_CMETA_COLLECT_MAILBOX_CLOSED)
            return VXML_CMETA_COLLECT_INGRESS_CLOSED;
        if (expected == VXML_CMETA_COLLECT_MAILBOX_DISARMED)
            return VXML_CMETA_COLLECT_INGRESS_STALE;
        return VXML_CMETA_COLLECT_INGRESS_FULL;
    }

    if (atomic_load_explicit(
            &mailbox->generation, memory_order_relaxed) !=
            completion->generation ||
        !cmeta_data_desc_equal(mailbox->data, completion->data)) {
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_COLLECT_MAILBOX_EMPTY,
            memory_order_release);
        return VXML_CMETA_COLLECT_INGRESS_STALE;
    }
    {
        const vxml_cmeta_program_data *program =
            (const vxml_cmeta_program_data *)impl->program->profile_data;
        const vxml_cmeta_field_row *field;
        if (profile->active_field >= program->field_count ||
            program->fields == NULL ||
            mailbox->root_fields == NULL ||
            mailbox->slot_capacity == 0u) {
            atomic_store_explicit(
                &mailbox->state,
                VXML_CMETA_COLLECT_MAILBOX_EMPTY,
                memory_order_release);
            return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;
        }
        field = &program->fields[profile->active_field];
        if (!cmeta_data_desc_equal(
                completion->data, field->field_data)) {
            atomic_store_explicit(
                &mailbox->state,
                VXML_CMETA_COLLECT_MAILBOX_EMPTY,
                memory_order_release);
            return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;
        }
        memcpy(mailbox->storage, completion->value, type->size);
        mailbox->root_fields[0] = field->root_field;
        mailbox->slot_count = 1u;
    }
    expected = VXML_CMETA_COLLECT_MAILBOX_WRITING;
    if (!atomic_compare_exchange_strong_explicit(
            &mailbox->state, &expected,
            VXML_CMETA_COLLECT_MAILBOX_READY,
            memory_order_acq_rel, memory_order_acquire))
        return expected == VXML_CMETA_COLLECT_MAILBOX_CLOSED
            ? VXML_CMETA_COLLECT_INGRESS_CLOSED
            : VXML_CMETA_COLLECT_INGRESS_FULL;
    return VXML_CMETA_COLLECT_INGRESS_ACCEPTED;
}

vxml_cmeta_collect_ingress_result vxml_session_cmeta_collect_try_complete_v2(
    vxml_session *session,
    const vxml_cmeta_collect_completion_v2 *completion) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    const vxml_cmeta_program_data *program;
    const cmeta_data_struct_shape *root_shape;
    const vxml_cmeta_field_row *selected;
    vxml_cmeta_collect_mailbox *mailbox;
    uint64_t generation;
    unsigned state;
    unsigned expected;
    size_t slot_index;
    size_t selected_count = 0u;

    if (session == NULL || completion == NULL)
        return VXML_CMETA_COLLECT_INGRESS_INVALID_ARGUMENT;
    if (completion->abi_version !=
            VXML_CMETA_COLLECT_COMPLETION_ABI_V2 ||
        completion->struct_size < sizeof(*completion) ||
        completion->generation == 0u ||
        completion->slots == NULL ||
        completion->slot_count == 0u)
        return VXML_CMETA_COLLECT_INGRESS_INVALID_ARGUMENT;

    impl = (vxml_session_impl *)session->impl;
    if (impl == NULL)
        return VXML_CMETA_COLLECT_INGRESS_CLOSED;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->profile_data == NULL)
        return impl->state == VXML_SESSION_CLOSED
            ? VXML_CMETA_COLLECT_INGRESS_CLOSED
            : VXML_CMETA_COLLECT_INGRESS_INVALID_ARGUMENT;
    if (impl->state == VXML_SESSION_CLOSED)
        return VXML_CMETA_COLLECT_INGRESS_CLOSED;

    profile = (vxml_cmeta_session_data *)impl->profile_data;
    program = (const vxml_cmeta_program_data *)
        impl->program->profile_data;
    mailbox = &profile->collect_mailbox;
    if (profile->active_field >= program->field_count ||
        program->fields == NULL)
        return VXML_CMETA_COLLECT_INGRESS_STALE;
    selected = &program->fields[profile->active_field];
    root_shape = session_root_shape(program);
    if (root_shape == NULL || mailbox->root_fields == NULL ||
        mailbox->slot_capacity == 0u ||
        mailbox->storage == NULL ||
        mailbox->storage_stride == 0u)
        return VXML_CMETA_COLLECT_INGRESS_INVALID_ARGUMENT;
    if (completion->slot_count > mailbox->slot_capacity)
        return VXML_CMETA_COLLECT_INGRESS_FULL;

    state = atomic_load_explicit(
        &mailbox->state, memory_order_acquire);
    if (state == VXML_CMETA_COLLECT_MAILBOX_CLOSED)
        return VXML_CMETA_COLLECT_INGRESS_CLOSED;
    if (state == VXML_CMETA_COLLECT_MAILBOX_DISARMED)
        return VXML_CMETA_COLLECT_INGRESS_STALE;
    if (state == VXML_CMETA_COLLECT_MAILBOX_WRITING ||
        state == VXML_CMETA_COLLECT_MAILBOX_READY)
        return VXML_CMETA_COLLECT_INGRESS_FULL;
    if (state != VXML_CMETA_COLLECT_MAILBOX_EMPTY)
        return VXML_CMETA_COLLECT_INGRESS_INVALID_ARGUMENT;

    generation = atomic_load_explicit(
        &mailbox->generation, memory_order_relaxed);
    if (completion->generation != generation)
        return VXML_CMETA_COLLECT_INGRESS_STALE;

    expected = VXML_CMETA_COLLECT_MAILBOX_EMPTY;
    if (!atomic_compare_exchange_strong_explicit(
            &mailbox->state, &expected,
            VXML_CMETA_COLLECT_MAILBOX_WRITING,
            memory_order_acq_rel, memory_order_acquire)) {
        if (expected == VXML_CMETA_COLLECT_MAILBOX_CLOSED)
            return VXML_CMETA_COLLECT_INGRESS_CLOSED;
        if (expected == VXML_CMETA_COLLECT_MAILBOX_DISARMED)
            return VXML_CMETA_COLLECT_INGRESS_STALE;
        return VXML_CMETA_COLLECT_INGRESS_FULL;
    }

    mailbox->slot_count = 0u;
    for (slot_index = 0u;
         slot_index < completion->slot_count;
         ++slot_index) {
        const vxml_cmeta_collect_result_slot_v1 *slot =
            &completion->slots[slot_index];
        size_t root_field;
        const cmeta_data_field_desc *target = NULL;
        const cmeta_type_desc *type;
        size_t prior;

        if (slot->name.data == NULL || slot->name.size == 0u ||
            memchr(slot->name.data, '\0', slot->name.size) != NULL ||
            slot->data == NULL || slot->value == NULL) {
            goto incompatible;
        }
        for (root_field = 0u;
             root_field < root_shape->field_count;
             ++root_field) {
            const char *name = root_shape->fields[root_field].name;
            if (name != NULL &&
                strlen(name) == slot->name.size &&
                memcmp(name, slot->name.data, slot->name.size) == 0) {
                target = &root_shape->fields[root_field];
                break;
            }
        }
        if (target == NULL ||
            !cmeta_data_desc_equal(slot->data, target->value) ||
            !collect_fixed_scalar_data(target->value))
            goto incompatible;
        type = target->value->storage_type;
        if (type == NULL ||
            type->size > mailbox->storage_stride)
            goto incompatible;
        for (prior = 0u; prior < slot_index; ++prior)
            if (mailbox->root_fields[prior] == root_field)
                goto incompatible;
        if (root_field == selected->root_field)
            ++selected_count;

        mailbox->root_fields[slot_index] = root_field;
        memcpy(
            mailbox->storage +
                slot_index * mailbox->storage_stride,
            slot->value, type->size);
    }
    if (selected_count != 1u)
        goto incompatible;
    if (atomic_load_explicit(
            &mailbox->generation, memory_order_relaxed) !=
        completion->generation)
        goto stale_after_claim;

    mailbox->slot_count = completion->slot_count;
    mailbox->data = selected->field_data;
    expected = VXML_CMETA_COLLECT_MAILBOX_WRITING;
    if (!atomic_compare_exchange_strong_explicit(
            &mailbox->state, &expected,
            VXML_CMETA_COLLECT_MAILBOX_READY,
            memory_order_acq_rel, memory_order_acquire))
        return expected == VXML_CMETA_COLLECT_MAILBOX_CLOSED
            ? VXML_CMETA_COLLECT_INGRESS_CLOSED
            : VXML_CMETA_COLLECT_INGRESS_FULL;
    return VXML_CMETA_COLLECT_INGRESS_ACCEPTED;

stale_after_claim:
    mailbox->slot_count = 0u;
    atomic_store_explicit(
        &mailbox->state,
        VXML_CMETA_COLLECT_MAILBOX_EMPTY,
        memory_order_release);
    return VXML_CMETA_COLLECT_INGRESS_STALE;

incompatible:
    mailbox->slot_count = 0u;
    atomic_store_explicit(
        &mailbox->state,
        VXML_CMETA_COLLECT_MAILBOX_EMPTY,
        memory_order_release);
    return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;
}

static bool completion_contains_root_field(
    const vxml_cmeta_collect_mailbox *mailbox,
    size_t root_field) {
    size_t index;
    if (mailbox == NULL || mailbox->root_fields == NULL)
        return false;
    for (index = 0u; index < mailbox->slot_count; ++index)
        if (mailbox->root_fields[index] == root_field)
            return true;
    return false;
}

static vxml_status filled_should_run(
    const vxml_cmeta_session_data *profile,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    const vxml_cmeta_filled_row *filled,
    const vxml_cmeta_collect_mailbox *mailbox,
    bool *out) {
    size_t index;
    *out = false;
    if (profile == NULL || program == NULL || form == NULL ||
        filled == NULL || mailbox == NULL ||
        filled->form != profile->active_form)
        return VXML_INVALID_STRUCTURE;
    if (filled->mode == VXML_CMETA_FILLED_FIELD) {
        *out = filled->field == profile->active_field;
        return VXML_OK;
    }
    if (!range_valid(
            filled->first_target, filled->target_count,
            program->filled_root_field_count) ||
        filled->target_count == 0u ||
        program->filled_root_fields == NULL)
        return VXML_INVALID_STRUCTURE;

    if (filled->mode == VXML_CMETA_FILLED_ALL) {
        for (index = 0u; index < filled->target_count; ++index) {
            const size_t root_field =
                program->filled_root_fields[
                    filled->first_target + index];
            if (root_field >=
                    session_root_shape(program)->field_count)
                return VXML_INVALID_STRUCTURE;
            if (profile->staged_root.bound[root_field] == 0u)
                return VXML_OK;
        }
        *out = true;
        return VXML_OK;
    }
    if (filled->mode == VXML_CMETA_FILLED_ANY) {
        for (index = 0u; index < filled->target_count; ++index) {
            const size_t root_field =
                program->filled_root_fields[
                    filled->first_target + index];
            if (completion_contains_root_field(
                    mailbox, root_field)) {
                *out = true;
                return VXML_OK;
            }
        }
        return VXML_OK;
    }
    return VXML_INVALID_STRUCTURE;
}

static vxml_status execute_filled_handler(
    vxml_cmeta_session_data *profile,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    const vxml_cmeta_filled_row *filled) {
    const size_t scopes[2] = {
        form->scope, program->document_scope};
    vxml_status status;
    if (filled->first_action > filled->action_end ||
        !range_valid(
            filled->first_action,
            filled->action_end - filled->first_action,
            program->action_count))
        return VXML_INVALID_STRUCTURE;
    status = exit_snapshot_prepare_range(
        &profile->pending_exit, program,
        filled->first_action, filled->action_end);
    if (status != VXML_OK) return status;
    profile->exit_requested = false;
    status = execute_action_range(
        profile, program, form, form->scope,
        scopes, 2u, filled->first_action, filled->action_end);
    if (status != VXML_OK) {
        exit_snapshot_destroy(&profile->pending_exit);
        return status;
    }
    if (!profile->exit_requested)
        exit_snapshot_destroy(&profile->pending_exit);
    return VXML_OK;
}

static vxml_status execute_filled_process(
    vxml_cmeta_session_data *profile,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    const vxml_cmeta_collect_mailbox *mailbox) {
    const vxml_cmeta_field_row *selected;
    bool run = false;
    size_t offset;
    vxml_status status;

    if (profile->active_field >= program->field_count ||
        program->fields == NULL)
        return VXML_INVALID_STRUCTURE;
    selected = &program->fields[profile->active_field];

    if (selected->filled != VXML_CMETA_NO_INDEX) {
        if (selected->filled >= program->filled_count ||
            program->filled == NULL)
            return VXML_INVALID_STRUCTURE;
        status = filled_should_run(
            profile, program, form,
            &program->filled[selected->filled], mailbox, &run);
        if (status != VXML_OK) return status;
        if (run) {
            status = execute_filled_handler(
                profile, program, form,
                &program->filled[selected->filled]);
            if (status != VXML_OK || profile->exit_requested)
                return status;
        }
    }

    if (form->filled_count == 0u)
        return VXML_OK;
    if (form->first_filled == VXML_CMETA_NO_INDEX ||
        !range_valid(
            form->first_filled, form->filled_count,
            program->filled_count) ||
        program->filled == NULL)
        return VXML_INVALID_STRUCTURE;

    for (offset = 0u; offset < form->filled_count; ++offset) {
        const vxml_cmeta_filled_row *filled =
            &program->filled[form->first_filled + offset];
        status = filled_should_run(
            profile, program, form, filled, mailbox, &run);
        if (status != VXML_OK) return status;
        if (!run) continue;
        status = execute_filled_handler(
            profile, program, form, filled);
        if (status != VXML_OK || profile->exit_requested)
            return status;
    }
    return VXML_OK;
}

vxml_status vxml_session_cmeta_collect_run_ready(
    vxml_session *session,
    bool *out_progressed) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_form_row *form;
    const vxml_cmeta_field_row *field;
    vxml_cmeta_collect_mailbox *mailbox;
    const cmeta_data_struct_shape *root_shape;
    unsigned expected;
    uint64_t generation;
    unsigned char *destination;
    cmeta_status meta_status;
    vxml_status status;

    if (out_progressed != NULL) *out_progressed = false;
    if (session == NULL || out_progressed == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl == NULL) return VXML_CLOSED;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA)
        return VXML_INVALID_CONTRACT;
    if (impl->state == VXML_SESSION_CLOSED)
        return VXML_CLOSED;
    if (impl->state != VXML_SESSION_RUNNING ||
        impl->profile_data == NULL)
        return VXML_INVALID_STATE;

    profile = (vxml_cmeta_session_data *)impl->profile_data;
    program = (const vxml_cmeta_program_data *)
        impl->program->profile_data;
    mailbox = &profile->collect_mailbox;
    if (!profile->collect_in_flight ||
        profile->active_form >= program->form_count ||
        profile->active_field >= program->field_count ||
        program->forms == NULL || program->fields == NULL)
        return VXML_INVALID_STATE;

    expected = VXML_CMETA_COLLECT_MAILBOX_READY;
    if (!atomic_compare_exchange_strong_explicit(
            &mailbox->state, &expected,
            VXML_CMETA_COLLECT_MAILBOX_WRITING,
            memory_order_acq_rel, memory_order_acquire)) {
        if (expected == VXML_CMETA_COLLECT_MAILBOX_EMPTY ||
            expected == VXML_CMETA_COLLECT_MAILBOX_WRITING)
            return VXML_OK;
        if (expected == VXML_CMETA_COLLECT_MAILBOX_CLOSED)
            return VXML_CLOSED;
        return VXML_INVALID_STATE;
    }

    *out_progressed = true;
    generation = atomic_load_explicit(
        &mailbox->generation, memory_order_relaxed);
    field = &program->fields[profile->active_field];
    form = &program->forms[profile->active_form];
    root_shape = session_root_shape(program);
    if (generation != profile->collect_generation ||
        root_shape == NULL ||
        mailbox->root_fields == NULL ||
        mailbox->slot_count == 0u ||
        mailbox->slot_count > mailbox->slot_capacity ||
        mailbox->storage == NULL ||
        mailbox->storage_stride == 0u) {
        profile->collect_in_flight = false;
        mailbox->data = NULL;
        mailbox->slot_count = 0u;
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_COLLECT_MAILBOX_DISARMED,
            memory_order_release);
        return session_fail(impl, VXML_INVALID_STRUCTURE);
    }

    if (!transaction_begin(profile, program)) {
        profile->collect_in_flight = false;
        mailbox->data = NULL;
        mailbox->slot_count = 0u;
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_COLLECT_MAILBOX_DISARMED,
            memory_order_release);
        return session_fail(impl, VXML_ALLOCATION_FAILED);
    }

    {
        size_t slot_index;
        bool selected_seen = false;
        for (slot_index = 0u;
             slot_index < mailbox->slot_count;
             ++slot_index) {
            const size_t root_field =
                mailbox->root_fields[slot_index];
            const cmeta_data_field_desc *target;
            const cmeta_type_desc *type;
            unsigned char *payload;

            if (root_field >= root_shape->field_count) {
                transaction_reset(profile, program);
                profile->collect_in_flight = false;
                mailbox->data = NULL;
                mailbox->slot_count = 0u;
                atomic_store_explicit(
                    &mailbox->state,
                    VXML_CMETA_COLLECT_MAILBOX_DISARMED,
                    memory_order_release);
                return session_fail(impl, VXML_INVALID_STRUCTURE);
            }
            target = &root_shape->fields[root_field];
            type = target->value != NULL
                ? target->value->storage_type : NULL;
            if (!collect_fixed_scalar_data(target->value) ||
                type == NULL ||
                type->size > mailbox->storage_stride ||
                profile->staged_root.bound[root_field] != 0u) {
                transaction_reset(profile, program);
                profile->collect_in_flight = false;
                mailbox->data = NULL;
                mailbox->slot_count = 0u;
                atomic_store_explicit(
                    &mailbox->state,
                    VXML_CMETA_COLLECT_MAILBOX_DISARMED,
                    memory_order_release);
                return session_fail(impl, VXML_SEMANTIC_ERROR);
            }
            payload = mailbox->storage +
                slot_index * mailbox->storage_stride;
            destination =
                profile->staged_root.storage + target->offset;
            meta_status = cmeta_data_value_init_zero(
                target->value, destination);
            if (meta_status == CMETA_OK)
                meta_status = cmeta_data_value_copy(
                    target->value, destination, payload);
            if (meta_status != CMETA_OK) {
                (void)cmeta_data_value_restore_zero(
                    target->value, destination);
                transaction_reset(profile, program);
                profile->collect_in_flight = false;
                mailbox->data = NULL;
                mailbox->slot_count = 0u;
                atomic_store_explicit(
                    &mailbox->state,
                    VXML_CMETA_COLLECT_MAILBOX_DISARMED,
                    memory_order_release);
                return session_fail(
                    impl,
                    meta_status == CMETA_OUT_OF_MEMORY
                        ? VXML_ALLOCATION_FAILED
                        : VXML_SEMANTIC_ERROR);
            }
            profile->staged_root.bound[root_field] = 1u;
            if (root_field == field->root_field)
                selected_seen = true;
        }
        if (!selected_seen) {
            transaction_reset(profile, program);
            profile->collect_in_flight = false;
            mailbox->data = NULL;
            mailbox->slot_count = 0u;
            atomic_store_explicit(
                &mailbox->state,
                VXML_CMETA_COLLECT_MAILBOX_DISARMED,
                memory_order_release);
            return session_fail(impl, VXML_INVALID_STRUCTURE);
        }
    }

    status = execute_filled_process(
        profile, program, form, mailbox);
    if (status != VXML_OK) {
        transaction_reset(profile, program);
        profile->collect_in_flight = false;
        mailbox->data = NULL;
        mailbox->slot_count = 0u;
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_COLLECT_MAILBOX_DISARMED,
            memory_order_release);
        return session_fail(impl, status);
    }

    mark_field_retry_reset(
        profile, program, profile->active_field);
    transaction_commit(profile, program);
    profile->reprompt_requested = false;
    profile->handler_reprompt_requested = false;

    profile->collect_in_flight = false;
    mailbox->data = NULL;
    mailbox->slot_count = 0u;
    atomic_store_explicit(
        &mailbox->state,
        VXML_CMETA_COLLECT_MAILBOX_DISARMED,
        memory_order_release);

    if (profile->exit_requested) {
        exit_snapshot_publish(profile);
        impl->state = VXML_SESSION_EXITED;
        impl->error = VXML_OK;
        return VXML_OK;
    }

    status = select_directed_field(
        impl, program, profile, form, profile->active_form);
    return status;
}

static bool event_prefix_match(
    const char *pattern, size_t pattern_size,
    const char *event_name, size_t event_name_size) {
    if (pattern == NULL || pattern_size == 0u ||
        event_name == NULL || event_name_size < pattern_size ||
        memcmp(pattern, event_name, pattern_size) != 0)
        return false;
    return event_name_size == pattern_size ||
        event_name[pattern_size] == '.';
}

static vxml_status event_counter_next(
    vxml_cmeta_session_data *profile,
    vxml_cmeta_event_scope_kind scope_kind,
    size_t owner,
    const char *event_name, size_t event_name_size,
    unsigned *out_count) {
    size_t index;
    vxml_cmeta_event_counter *counter = NULL;
    if (profile == NULL || out_count == NULL ||
        event_name == NULL || event_name_size == 0u ||
        profile->event_name_stride == 0u ||
        event_name_size >= profile->event_name_stride)
        return VXML_INVALID_CONTRACT;
    for (index = 0u; index < profile->event_counter_count; ++index) {
        vxml_cmeta_event_counter *candidate =
            &profile->event_counters[index];
        if (candidate->scope_kind == scope_kind &&
            candidate->owner == owner &&
            candidate->event_size == event_name_size &&
            memcmp(candidate->event, event_name, event_name_size) == 0) {
            counter = candidate;
            break;
        }
    }
    if (counter == NULL) {
        if (profile->event_counter_count >=
            profile->event_counter_capacity)
            return VXML_LIMIT_EXCEEDED;
        counter = &profile->event_counters[
            profile->event_counter_count++];
        counter->scope_kind = scope_kind;
        counter->owner = owner;
        counter->event_size = event_name_size;
        memcpy(counter->event, event_name, event_name_size);
        counter->event[event_name_size] = '\0';
        counter->count = 0u;
    }
    if (counter->count != UINT_MAX)
        ++counter->count;
    *out_count = counter->count;
    return VXML_OK;
}

static bool event_scope_owner(
    const vxml_cmeta_session_data *profile,
    const vxml_cmeta_program_data *program,
    unsigned scope_rank,
    vxml_cmeta_event_scope_kind *out_kind,
    size_t *out_owner) {
    if (profile == NULL || program == NULL ||
        out_kind == NULL || out_owner == NULL)
        return false;
    if (scope_rank == 0u) {
        if (profile->active_field == VXML_CMETA_NO_INDEX ||
            profile->active_field >= program->field_count)
            return false;
        *out_kind = VXML_CMETA_EVENT_FIELD;
        *out_owner = profile->active_field;
        return true;
    }
    if (scope_rank == 1u) {
        if (profile->active_form == VXML_CMETA_NO_INDEX ||
            profile->active_form >= program->form_count)
            return false;
        *out_kind = VXML_CMETA_EVENT_FORM;
        *out_owner = profile->active_form;
        return true;
    }
    if (scope_rank == 2u) {
        *out_kind = VXML_CMETA_EVENT_DOCUMENT;
        *out_owner = 0u;
        return true;
    }
    return false;
}

static const vxml_cmeta_event_handler_row *select_event_handler(
    const vxml_cmeta_program_data *program,
    vxml_cmeta_event_scope_kind scope_kind,
    size_t owner,
    const char *event_name, size_t event_name_size,
    unsigned occurrence) {
    const vxml_cmeta_event_handler_row *best = NULL;
    size_t index;
    if (program == NULL || event_name == NULL ||
        event_name_size == 0u ||
        (program->event_handler_count != 0u &&
         program->event_handlers == NULL))
        return NULL;
    for (index = 0u; index < program->event_handler_count; ++index) {
        const vxml_cmeta_event_handler_row *row =
            &program->event_handlers[index];
        if (row->scope_kind != scope_kind ||
            row->owner != owner ||
            row->event == NULL || row->event_size == 0u ||
            row->count == 0u || row->count > occurrence ||
            !event_prefix_match(
                row->event, row->event_size,
                event_name, event_name_size))
            continue;
        if (best == NULL ||
            row->event_size > best->event_size ||
            (row->event_size == best->event_size &&
             row->count > best->count))
            best = row;
    }
    return best;
}

static vxml_status execute_event_handler(
    vxml_session_impl *impl,
    const vxml_cmeta_program_data *program,
    vxml_cmeta_session_data *profile,
    const vxml_cmeta_event_handler_row *handler) {
    const vxml_cmeta_form_row *form = NULL;
    const size_t *scopes = NULL;
    size_t scope_values[2] = {0u, 0u};
    size_t scope_count = 0u;
    size_t execution_scope = program->document_scope;
    vxml_status status;
    if (impl == NULL || program == NULL || profile == NULL ||
        handler == NULL ||
        handler->first_action > handler->action_end ||
        !range_valid(
            handler->first_action,
            handler->action_end - handler->first_action,
            program->action_count))
        return VXML_INVALID_STRUCTURE;
    if (profile->active_form != VXML_CMETA_NO_INDEX) {
        if (profile->active_form >= program->form_count ||
            program->forms == NULL)
            return VXML_INVALID_STRUCTURE;
        form = &program->forms[profile->active_form];
    }

    if (handler->scope_kind == VXML_CMETA_EVENT_DOCUMENT) {
        scope_values[0] = program->document_scope;
        scopes = scope_values;
        scope_count = 1u;
        execution_scope = program->document_scope;
    } else if (handler->scope_kind == VXML_CMETA_EVENT_FORM ||
               handler->scope_kind == VXML_CMETA_EVENT_FIELD) {
        if (form == NULL || form->scope >= program->scope_count)
            return VXML_INVALID_STRUCTURE;
        scope_values[0] = form->scope;
        scope_values[1] = program->document_scope;
        scopes = scope_values;
        scope_count = 2u;
        execution_scope = form->scope;
    } else {
        return VXML_INVALID_STRUCTURE;
    }

    status = exit_snapshot_prepare_range(
        &profile->pending_exit, program,
        handler->first_action, handler->action_end);
    if (status != VXML_OK) return status;
    profile->exit_requested = false;
    profile->throw_requested = false;
    profile->rethrow_requested = false;
    profile->handler_reprompt_requested = false;
    profile->thrown_event = NULL;
    profile->thrown_event_size = 0u;
    profile->event_dispatch_active = true;
    if (!transaction_begin(profile, program)) {
        profile->event_dispatch_active = false;
        exit_snapshot_destroy(&profile->pending_exit);
        return VXML_ALLOCATION_FAILED;
    }
    status = execute_action_range(
        profile, program, form, execution_scope,
        scopes, scope_count,
        handler->first_action, handler->action_end);
    profile->event_dispatch_active = false;
    if (status != VXML_OK) {
        transaction_reset(profile, program);
        exit_snapshot_destroy(&profile->pending_exit);
        profile->throw_requested = false;
        profile->rethrow_requested = false;
        profile->handler_reprompt_requested = false;
        profile->thrown_event = NULL;
        profile->thrown_event_size = 0u;
        return status;
    }
    transaction_commit(profile, program);
    if (profile->handler_reprompt_requested)
        profile->reprompt_requested = true;
    profile->handler_reprompt_requested = false;
    if (profile->exit_requested) {
        exit_snapshot_publish(profile);
        impl->state = VXML_SESSION_EXITED;
        impl->error = VXML_OK;
    } else {
        exit_snapshot_destroy(&profile->pending_exit);
    }
    return VXML_OK;
}

vxml_status vxml_session_cmeta_raise(
    vxml_session *session,
    const char *event_name,
    size_t event_name_size) {
    vxml_session_impl *impl;
    vxml_cmeta_program_data const *program;
    vxml_cmeta_session_data *profile;
    const char *current_event;
    size_t current_event_size;
    unsigned depth = 0u;
    unsigned start_scope = 0u;

    if (session == NULL || session->impl == NULL ||
        event_name == NULL || event_name_size == 0u ||
        memchr(event_name, '\0', event_name_size) != NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_RUNNING)
        return VXML_INVALID_STATE;
    program = (const vxml_cmeta_program_data *)
        impl->program->profile_data;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (!cmeta_location_path_valid(
            event_name, event_name_size, SIZE_MAX))
        return VXML_INVALID_ARGUMENT;
    if (program->event_handler_count == 0u)
        return session_fail(impl, VXML_SEMANTIC_ERROR);
    if (profile->event_name_stride == 0u ||
        event_name_size >= profile->event_name_stride)
        return VXML_INVALID_ARGUMENT;
    if (profile->max_event_dispatch_depth == 0u)
        return session_fail(impl, VXML_INVALID_CONTRACT);

    current_event = event_name;
    current_event_size = event_name_size;
    for (;;) {
        unsigned scope_rank;
        bool handled = false;
        if (++depth > profile->max_event_dispatch_depth)
            return session_fail(impl, VXML_LIMIT_EXCEEDED);

        for (scope_rank = start_scope; scope_rank < 3u; ++scope_rank) {
            vxml_cmeta_event_scope_kind scope_kind;
            size_t owner;
            unsigned occurrence;
            const vxml_cmeta_event_handler_row *handler;
            vxml_status status;

            if (!event_scope_owner(
                    profile, program, scope_rank,
                    &scope_kind, &owner))
                continue;
            status = event_counter_next(
                profile, scope_kind, owner,
                current_event, current_event_size,
                &occurrence);
            if (status != VXML_OK)
                return session_fail(impl, status);
            handler = select_event_handler(
                program, scope_kind, owner,
                current_event, current_event_size,
                occurrence);
            if (handler == NULL) continue;

            status = execute_event_handler(
                impl, program, profile, handler);
            if (status != VXML_OK)
                return session_fail(impl, status);
            handled = true;
            if (impl->state != VXML_SESSION_RUNNING)
                return VXML_OK;
            if (profile->throw_requested) {
                current_event = profile->thrown_event;
                current_event_size = profile->thrown_event_size;
                profile->throw_requested = false;
                profile->thrown_event = NULL;
                profile->thrown_event_size = 0u;
                start_scope = 0u;
                break;
            }
            if (profile->rethrow_requested) {
                profile->rethrow_requested = false;
                start_scope = scope_rank + 1u;
                break;
            }
            return VXML_OK;
        }

        if (handled && current_event != NULL &&
            current_event_size != 0u &&
            start_scope < 3u)
            continue;
        return session_fail(impl, VXML_SEMANTIC_ERROR);
    }
}

vxml_status vxml_cmeta_session_raise_event_profile(
    vxml_session_impl *session,
    const char *event_name,
    size_t event_name_size) {
    vxml_session wrapper;
    if (session == NULL)
        return VXML_INVALID_ARGUMENT;
    wrapper.impl = session;
    return vxml_session_cmeta_raise(
        &wrapper, event_name, event_name_size);
}

vxml_status vxml_session_cmeta_noinput(vxml_session *session) {
    vxml_session_impl *impl;
    if (session != NULL && session->impl != NULL) {
        impl = (vxml_session_impl *)session->impl;
        if (impl->program != NULL &&
            impl->program->profile_kind == VXML_PROFILE_CMETA &&
            impl->profile_data != NULL)
            settle_prompt_media(
                (vxml_cmeta_session_data *)impl->profile_data);
    }
    return vxml_session_cmeta_raise(
        session, "noinput", sizeof("noinput") - 1u);
}

vxml_status vxml_session_cmeta_nomatch(vxml_session *session) {
    vxml_session_impl *impl;
    if (session != NULL && session->impl != NULL) {
        impl = (vxml_session_impl *)session->impl;
        if (impl->program != NULL &&
            impl->program->profile_kind == VXML_PROFILE_CMETA &&
            impl->profile_data != NULL)
            settle_prompt_media(
                (vxml_cmeta_session_data *)impl->profile_data);
    }
    return vxml_session_cmeta_raise(
        session, "nomatch", sizeof("nomatch") - 1u);
}

static unsigned field_prompt_count(
    const vxml_cmeta_session_data *profile,
    size_t field_index) {
    size_t index;
    unsigned count = 1u;
    if (profile == NULL) return count;
    for (index = 0u; index < profile->event_counter_count; ++index) {
        const vxml_cmeta_event_counter *counter =
            &profile->event_counters[index];
        if (counter->scope_kind != VXML_CMETA_EVENT_FIELD ||
            counter->owner != field_index ||
            !recovery_event_name(counter))
            continue;
        if (counter->count > UINT_MAX - count)
            return UINT_MAX;
        count += counter->count;
    }
    return count;
}

static vxml_status selected_prompt_row(
    const vxml_session_impl *impl,
    const vxml_cmeta_field_row **out_field,
    const vxml_cmeta_prompt_row **out_prompt,
    unsigned *out_prompt_count) {
    const vxml_cmeta_program_data *program;
    vxml_cmeta_session_data *profile;
    const vxml_cmeta_form_row *form;
    const vxml_cmeta_field_row *field;
    const vxml_cmeta_prompt_row *best = NULL;
    unsigned prompt_count;
    size_t offset;
    if (out_field != NULL) *out_field = NULL;
    if (out_prompt != NULL) *out_prompt = NULL;
    if (out_prompt_count != NULL) *out_prompt_count = 0u;
    if (impl == NULL || out_field == NULL ||
        out_prompt == NULL || out_prompt_count == NULL)
        return VXML_INVALID_ARGUMENT;
    if (impl->state != VXML_SESSION_RUNNING ||
        impl->program == NULL ||
        impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_STATE;
    program = (const vxml_cmeta_program_data *)
        impl->program->profile_data;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (profile->active_form == VXML_CMETA_NO_INDEX ||
        profile->active_form >= program->form_count ||
        program->forms == NULL ||
        profile->active_field == VXML_CMETA_NO_INDEX ||
        profile->active_field >= program->field_count ||
        program->fields == NULL)
        return VXML_INVALID_STATE;
    form = &program->forms[profile->active_form];
    field = &program->fields[profile->active_field];
    if (field->form != profile->active_form ||
        !range_valid(
            field->first_prompt, field->prompt_count,
            program->prompt_count) ||
        (field->prompt_count != 0u && program->prompts == NULL))
        return VXML_INVALID_STRUCTURE;

    prompt_count = field_prompt_count(
        profile, profile->active_field);
    for (offset = 0u; offset < field->prompt_count; ++offset) {
        const vxml_cmeta_prompt_row *row =
            &program->prompts[field->first_prompt + offset];
        bool eligible = true;
        if (row->field != profile->active_field ||
            row->count == 0u ||
            row->media_payload == NULL ||
            row->media_payload_size == 0u)
            return VXML_INVALID_STRUCTURE;
        if (row->media_kind == VXML_CMETA_PROMPT_MEDIA_TEXT) {
            if (row->text == NULL || row->text_size == 0u ||
                row->media_payload != row->text ||
                row->media_payload_size != row->text_size)
                return VXML_INVALID_STRUCTURE;
        } else if (row->media_kind ==
                   VXML_CMETA_PROMPT_MEDIA_AUDIO) {
            if (row->text != NULL || row->text_size != 0u)
                return VXML_INVALID_STRUCTURE;
        } else {
            return VXML_INVALID_STRUCTURE;
        }
        if (row->count > prompt_count)
            continue;
        if (row->condition != VXML_CMETA_NO_INDEX) {
            const size_t scopes[2] = {
                form->scope, program->document_scope};
            const vxml_status status = evaluate_condition(
                profile, program, false,
                row->condition, scopes, 2u, &eligible);
            if (status != VXML_OK) return status;
        }
        if (!eligible) continue;
        if (best == NULL || row->count > best->count)
            best = row;
    }
    *out_field = field;
    *out_prompt = best;
    *out_prompt_count = prompt_count;
    return VXML_OK;
}

vxml_status vxml_session_cmeta_prompt(
    const vxml_session *session,
    vxml_cmeta_prompt_view_v1 *out_prompt) {
    const vxml_session_impl *impl;
    const vxml_cmeta_field_row *field = NULL;
    const vxml_cmeta_prompt_row *best = NULL;
    unsigned prompt_count = 0u;
    vxml_status status;
    if (out_prompt != NULL)
        *out_prompt = (vxml_cmeta_prompt_view_v1){0};
    if (session == NULL || out_prompt == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = cmeta_session(session);
    if (impl == NULL)
        return VXML_INVALID_CONTRACT;
    status = selected_prompt_row(
        impl, &field, &best, &prompt_count);
    if (status != VXML_OK) return status;

    *out_prompt = (vxml_cmeta_prompt_view_v1){
        .abi_version = VXML_CMETA_PROMPT_VIEW_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_prompt_view_v1),
        .field = {field->name, field->name_size},
        .text = best != NULL &&
                best->media_kind == VXML_CMETA_PROMPT_MEDIA_TEXT
            ? (vxml_cmeta_name_view){best->text, best->text_size}
            : (vxml_cmeta_name_view){0},
        .count = best != NULL ? best->count : 0u,
        .prompt_count = prompt_count,
        .generation =
            ((const vxml_cmeta_session_data *)impl->profile_data)
                ->collect_generation
    };
    return VXML_OK;
}

static vxml_status prompt_media_request_from_impl(
    const vxml_session_impl *impl,
    vxml_cmeta_prompt_media_request_v1 *out_request) {
    const vxml_cmeta_field_row *field = NULL;
    const vxml_cmeta_prompt_row *prompt = NULL;
    const vxml_cmeta_session_data *profile;
    unsigned prompt_count = 0u;
    vxml_status status;
    if (out_request == NULL) return VXML_INVALID_ARGUMENT;
    *out_request = (vxml_cmeta_prompt_media_request_v1){0};
    if (impl == NULL) return VXML_INVALID_CONTRACT;
    status = selected_prompt_row(
        impl, &field, &prompt, &prompt_count);
    if (status != VXML_OK) return status;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;

    out_request->abi_version = VXML_CMETA_PROMPT_MEDIA_REQUEST_ABI_V1;
    out_request->struct_size =
        sizeof(vxml_cmeta_prompt_media_request_v1);
    out_request->generation = profile->collect_generation;
    out_request->field =
        (vxml_cmeta_name_view){field->name, field->name_size};
    out_request->prompt_count = prompt_count;
    out_request->selected_count =
        prompt != NULL ? prompt->count : 0u;
    if (prompt == NULL)
        return VXML_OK;

    out_request->segment_count = 1u;
    out_request->segment.kind = prompt->media_kind;
    out_request->segment.payload =
        (vxml_cmeta_name_view){
            prompt->media_payload, prompt->media_payload_size};
    out_request->segment.media_type =
        (vxml_cmeta_name_view){0};
    if (prompt->media_kind == VXML_CMETA_PROMPT_MEDIA_TEXT)
        out_request->required_capabilities =
            VXML_CMETA_PROMPT_MEDIA_CAP_TEXT;
    else if (prompt->media_kind == VXML_CMETA_PROMPT_MEDIA_AUDIO)
        out_request->required_capabilities =
            VXML_CMETA_PROMPT_MEDIA_CAP_AUDIO;
    else
        return VXML_INVALID_STRUCTURE;
    return VXML_OK;
}

vxml_status vxml_session_cmeta_prompt_media_request(
    const vxml_session *session,
    vxml_cmeta_prompt_media_request_v1 *out_request) {
    const vxml_session_impl *impl;
    if (out_request != NULL)
        *out_request = (vxml_cmeta_prompt_media_request_v1){0};
    if (session == NULL || out_request == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = cmeta_session(session);
    if (impl == NULL) return VXML_INVALID_CONTRACT;
    return prompt_media_request_from_impl(impl, out_request);
}

vxml_status vxml_session_cmeta_prompt_media_prepare(
    vxml_session *session, const char **out_error) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_prompt_media_request_v1 request = {0};
    vxml_cmeta_prompt_media_ticket_v1 ticket = {0};
    vxml_status status;
    if (out_error != NULL) *out_error = NULL;
    if (session == NULL || session->impl == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA)
        return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_RUNNING ||
        impl->profile_data == NULL)
        return VXML_INVALID_STATE;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (profile->prompt_media_adapter == NULL ||
        profile->prompt_media_adapter->prepare == NULL)
        return VXML_INVALID_CONTRACT;
    if (profile->prompt_media_prepared ||
        profile->prompt_media_in_flight)
        return VXML_INVALID_STATE;

    status = prompt_media_request_from_impl(impl, &request);
    if (status != VXML_OK) return status;
    if (request.segment_count == 0u)
        return VXML_INVALID_STATE;
    if (request.segment_count != 1u ||
        (request.segment.kind != VXML_CMETA_PROMPT_MEDIA_TEXT &&
         request.segment.kind != VXML_CMETA_PROMPT_MEDIA_AUDIO) ||
        request.segment.payload.data == NULL ||
        request.segment.payload.size == 0u)
        return VXML_INVALID_STRUCTURE;
    if ((profile->prompt_media_adapter->capabilities &
         request.required_capabilities) !=
        request.required_capabilities)
        return VXML_UNSUPPORTED_FEATURE;

    status = profile->prompt_media_adapter->prepare(
        profile->prompt_media_user,
        &request, &ticket, out_error);
    if (status != VXML_OK) {
        if (ticket.discard != NULL)
            ticket.discard(ticket.user);
        return status;
    }
    if (ticket.commit == NULL || ticket.discard == NULL) {
        if (ticket.discard != NULL)
            ticket.discard(ticket.user);
        return VXML_INVALID_CONTRACT;
    }
    profile->prompt_media_ticket = ticket;
    profile->prompt_media_generation = request.generation;
    profile->prompt_media_prepared = true;
    return VXML_OK;
}

vxml_status vxml_session_cmeta_prompt_media_commit(
    vxml_session *session) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_prompt_media_ticket_v1 ticket;
    if (session == NULL || session->impl == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA)
        return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_RUNNING ||
        impl->profile_data == NULL)
        return VXML_INVALID_STATE;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (!profile->prompt_media_prepared ||
        profile->prompt_media_in_flight ||
        profile->prompt_media_ticket.commit == NULL ||
        profile->prompt_media_ticket.discard == NULL)
        return VXML_INVALID_STATE;
    ticket = profile->prompt_media_ticket;
    profile->prompt_media_ticket =
        (vxml_cmeta_prompt_media_ticket_v1){0};
    profile->prompt_media_prepared = false;
    profile->prompt_media_in_flight = true;
    ticket.commit(ticket.user);
    return VXML_OK;
}

vxml_status vxml_session_cmeta_prompt_media_discard(
    vxml_session *session) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_prompt_media_ticket_v1 ticket;
    if (session == NULL || session->impl == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA)
        return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_RUNNING ||
        impl->profile_data == NULL)
        return VXML_INVALID_STATE;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (!profile->prompt_media_prepared ||
        profile->prompt_media_ticket.commit == NULL ||
        profile->prompt_media_ticket.discard == NULL)
        return VXML_INVALID_STATE;
    ticket = profile->prompt_media_ticket;
    profile->prompt_media_ticket =
        (vxml_cmeta_prompt_media_ticket_v1){0};
    profile->prompt_media_generation = 0u;
    profile->prompt_media_prepared = false;
    ticket.discard(ticket.user);
    return VXML_OK;
}

vxml_status vxml_session_cmeta_take_reprompt(
    vxml_session *session, bool *out_requested) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    if (out_requested != NULL) *out_requested = false;
    if (session == NULL || out_requested == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl == NULL)
        return VXML_CLOSED;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA)
        return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_RUNNING ||
        impl->profile_data == NULL)
        return VXML_INVALID_STATE;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    *out_requested = profile->reprompt_requested;
    profile->reprompt_requested = false;
    return VXML_OK;
}

static bool read_sint_value(
    const cmeta_data_desc *data, const void *object,
    vxml_cmeta_value_view *out_value) {
    const cmeta_data_integer_shape *shape;
    if (data == NULL || data->kind != CMETA_DATA_SINT ||
        data->shape == NULL || data->storage_type == NULL || object == NULL)
        return false;
    shape = (const cmeta_data_integer_shape *)data->shape;
    out_value->kind = VXML_CMETA_VALUE_SINT;
    switch (shape->bits) {
        case 8u: {
            int8_t value;
            if (data->storage_type->size != sizeof(value)) return false;
            memcpy(&value, object, sizeof(value));
            out_value->data.sint = value;
            return true;
        }
        case 16u: {
            int16_t value;
            if (data->storage_type->size != sizeof(value)) return false;
            memcpy(&value, object, sizeof(value));
            out_value->data.sint = value;
            return true;
        }
        case 32u: {
            int32_t value;
            if (data->storage_type->size != sizeof(value)) return false;
            memcpy(&value, object, sizeof(value));
            out_value->data.sint = value;
            return true;
        }
        case 64u:
            if (data->storage_type->size != sizeof(out_value->data.sint))
                return false;
            memcpy(&out_value->data.sint, object,
                   sizeof(out_value->data.sint));
            return true;
        default:
            return false;
    }
}

static bool read_uint_value(
    const cmeta_data_desc *data, const void *object,
    vxml_cmeta_value_view *out_value) {
    const cmeta_data_integer_shape *shape;
    if (data == NULL || data->kind != CMETA_DATA_UINT ||
        data->shape == NULL || data->storage_type == NULL ||
        object == NULL)
        return false;
    shape = (const cmeta_data_integer_shape *)data->shape;
    out_value->kind = VXML_CMETA_VALUE_UINT;
    switch (shape->bits) {
        case 8u: {
            uint8_t value;
            if (data->storage_type->size != sizeof(value)) return false;
            memcpy(&value, object, sizeof(value));
            out_value->data.uint_value = value;
            return true;
        }
        case 16u: {
            uint16_t value;
            if (data->storage_type->size != sizeof(value)) return false;
            memcpy(&value, object, sizeof(value));
            out_value->data.uint_value = value;
            return true;
        }
        case 32u: {
            uint32_t value;
            if (data->storage_type->size != sizeof(value)) return false;
            memcpy(&value, object, sizeof(value));
            out_value->data.uint_value = value;
            return true;
        }
        case 64u:
            if (data->storage_type->size !=
                sizeof(out_value->data.uint_value))
                return false;
            memcpy(&out_value->data.uint_value, object,
                   sizeof(out_value->data.uint_value));
            return true;
        default:
            return false;
    }
}

static bool read_float_value(
    const cmeta_data_desc *data, const void *object,
    vxml_cmeta_value_view *out_value) {
    const cmeta_data_float_shape *shape;
    if (data == NULL || data->kind != CMETA_DATA_FLOAT ||
        data->shape == NULL || data->storage_type == NULL ||
        object == NULL)
        return false;
    shape = (const cmeta_data_float_shape *)data->shape;
    out_value->kind = VXML_CMETA_VALUE_FLOAT;
    if (shape->bits == 32u) {
        float value;
        if (data->storage_type->size != sizeof(value)) return false;
        memcpy(&value, object, sizeof(value));
        out_value->data.number = value;
        return true;
    }
    if (shape->bits == 64u) {
        if (data->storage_type->size != sizeof(out_value->data.number))
            return false;
        memcpy(&out_value->data.number, object,
               sizeof(out_value->data.number));
        return true;
    }
    return false;
}

static vxml_status read_scalar_value(
    const cmeta_data_desc *data, const void *object,
    unsigned char *string_scratch, size_t string_capacity,
    vxml_cmeta_value_view *out_value) {
    if (data == NULL || object == NULL || out_value == NULL)
        return VXML_INVALID_STRUCTURE;
    if (data->kind == CMETA_DATA_BOOL) {
        if (data->storage_type == NULL ||
            data->storage_type->size != sizeof(out_value->data.boolean))
            return VXML_INVALID_STRUCTURE;
        out_value->kind = VXML_CMETA_VALUE_BOOL;
        memcpy(&out_value->data.boolean, object,
               sizeof(out_value->data.boolean));
        return VXML_OK;
    }
    if (data->kind == CMETA_DATA_SINT)
        return read_sint_value(data, object, out_value)
            ? VXML_OK : VXML_INVALID_STRUCTURE;
    if (data->kind == CMETA_DATA_UINT)
        return read_uint_value(data, object, out_value)
            ? VXML_OK : VXML_INVALID_STRUCTURE;
    if (data->kind == CMETA_DATA_FLOAT)
        return read_float_value(data, object, out_value)
            ? VXML_OK : VXML_INVALID_STRUCTURE;
    if (data->kind == CMETA_DATA_STRING) {
        const unsigned char *bytes = NULL;
        size_t size = 0u;
        const cmeta_status status = cmeta_data_buffer_read(
            data, object, string_capacity, &bytes, &size);
        if (status != CMETA_OK) return buffer_status(status);
        if (size > string_capacity)
            return VXML_LIMIT_EXCEEDED;
        if (size != 0u && (bytes == NULL || string_scratch == NULL))
            return VXML_SEMANTIC_ERROR;
        if (size != 0u) memmove(string_scratch, bytes, size);
        out_value->kind = VXML_CMETA_VALUE_STRING;
        out_value->data.string.data = (const char *)string_scratch;
        out_value->data.string.size = size;
        return VXML_OK;
    }
    return VXML_INVALID_STRUCTURE;
}

typedef struct committed_read_location {
    const cmeta_data_desc *data;
    const void *object;
    bool bound;
} committed_read_location;

static vxml_status resolve_committed_read(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const char *name, size_t name_size,
    committed_read_location *out) {
    const cmeta_data_struct_shape *root_shape = session_root_shape(program);
    size_t scopes[3];
    size_t scope_count = 0u;
    size_t index;
    memset(out, 0, sizeof(*out));
    if (session->active_block != VXML_CMETA_NO_INDEX) {
        const vxml_cmeta_block_row *block;
        if (session->active_block >= program->block_count ||
            program->blocks == NULL)
            return VXML_INVALID_STRUCTURE;
        block = &program->blocks[session->active_block];
        if (block->form != session->active_form)
            return VXML_INVALID_STRUCTURE;
        scopes[scope_count++] = block->scope;
    }
    if (session->active_form != VXML_CMETA_NO_INDEX) {
        if (session->active_form >= program->form_count ||
            program->forms == NULL)
            return VXML_INVALID_STRUCTURE;
        scopes[scope_count++] = program->forms[session->active_form].scope;
    }
    scopes[scope_count++] = program->document_scope;
    for (index = 0u; index < scope_count; ++index) {
        const size_t scope = scopes[index];
        const cmeta_scope_schema *schema;
        const cmeta_scope_view *view;
        const cmeta_scope_slot *slot;
        unsigned char *declared;
        size_t slot_index = VXML_CMETA_NO_INDEX;
        if (scope >= program->scope_count || program->scopes == NULL ||
            session->committed_scopes == NULL)
            return VXML_INVALID_STRUCTURE;
        schema = &program->scopes[scope].schema;
        view = &session->committed_scopes[scope].view;
        slot = cmeta_scope_find(schema, name, name_size, &slot_index);
        if (slot == NULL) continue;
        declared = session_declared(session, program, false, scope);
        if (slot_index >= schema->slot_count || declared == NULL ||
            !cmeta_scope_view_valid(view) || view->schema != schema)
            return VXML_INVALID_STRUCTURE;
        if (declared[slot_index] == 0u) continue;
        out->data = slot->value;
        out->bound = view->bound[slot_index] != 0u;
        if (out->bound) out->object = view->storage + slot->offset;
        return VXML_OK;
    }
    if (root_shape == NULL || session->committed_root.storage == NULL ||
        (root_shape->field_count != 0u &&
         session->committed_root.bound == NULL))
        return VXML_INVALID_STRUCTURE;
    for (index = 0u; index < root_shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &root_shape->fields[index];
        if (field->name == NULL || strlen(field->name) != name_size ||
            memcmp(field->name, name, name_size) != 0)
            continue;
        out->data = field->value;
        out->bound = session->committed_root.bound[index] != 0u;
        if (out->bound)
            out->object = session->committed_root.storage + field->offset;
        return VXML_OK;
    }
    return VXML_SEMANTIC_ERROR;
}

vxml_status vxml_session_cmeta_read(
    const vxml_session *session, const char *name, size_t name_size,
    vxml_cmeta_value_view *out_value) {
    const vxml_session_impl *impl;
    const vxml_cmeta_program_data *program;
    vxml_cmeta_session_data *profile;
    committed_read_location resolved;
    vxml_status status;
    if (out_value != NULL) *out_value = (vxml_cmeta_value_view){0};
    if (name == NULL || name_size == 0u || out_value == NULL)
        return VXML_INVALID_ARGUMENT;
    if (!cmeta_location_path_valid(name, name_size, 1u))
        return VXML_INVALID_ARGUMENT;
    impl = cmeta_session(session);
    if (impl == NULL) return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_READY &&
        impl->state != VXML_SESSION_EXITED &&
        impl->state != VXML_SESSION_FAILED)
        return VXML_INVALID_STATE;
    if (impl->profile_data == NULL || impl->program->profile_data == NULL)
        return VXML_INVALID_STRUCTURE;
    program = (const vxml_cmeta_program_data *)impl->program->profile_data;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    status = resolve_committed_read(
        profile, program, name, name_size, &resolved);
    if (status != VXML_OK || !resolved.bound) return status;
    status = read_scalar_value(
        resolved.data, resolved.object,
        profile->read_scratch, profile->read_scratch_bytes,
        out_value);
    if (status != VXML_OK) *out_value = (vxml_cmeta_value_view){0};
    return status;
}

static bool terminal_span_valid(
    const char *base, size_t used, const char *data, size_t size) {
    uintptr_t base_address;
    uintptr_t data_address;
    size_t offset;
    if (base == NULL || data == NULL) return false;
    base_address = (uintptr_t)(const void *)base;
    data_address = (uintptr_t)(const void *)data;
    if (data_address < base_address) return false;
    offset = (size_t)(data_address - base_address);
    return offset <= used && size <= used - offset;
}

static bool terminal_value_valid(
    const vxml_cmeta_exit_snapshot *snapshot,
    const vxml_cmeta_value_view *value) {
    if (value == NULL) return false;
    switch (value->kind) {
        case VXML_CMETA_VALUE_UNDEFINED:
        case VXML_CMETA_VALUE_BOOL:
        case VXML_CMETA_VALUE_SINT:
        case VXML_CMETA_VALUE_UINT:
        case VXML_CMETA_VALUE_FLOAT:
            return true;
        case VXML_CMETA_VALUE_STRING:
            return terminal_span_valid(
                snapshot->strings, snapshot->string_size,
                value->data.string.data, value->data.string.size);
        default:
            return false;
    }
}

static bool terminal_exit_valid(
    const vxml_cmeta_exit_snapshot *snapshot) {
    size_t index;
    if (snapshot == NULL || snapshot->count > snapshot->entry_capacity ||
        snapshot->name_size > snapshot->name_capacity ||
        snapshot->string_size > snapshot->string_capacity ||
        (snapshot->entry_capacity != 0u && snapshot->entries == NULL) ||
        (snapshot->name_capacity != 0u && snapshot->names == NULL) ||
        (snapshot->string_capacity != 0u && snapshot->strings == NULL))
        return false;
    if (snapshot->kind == VXML_CMETA_EXIT_EMPTY)
        return snapshot->count == 0u && snapshot->name_size == 0u &&
            snapshot->string_size == 0u;
    if (snapshot->kind == VXML_CMETA_EXIT_EXPRESSION) {
        if (snapshot->count != 1u || snapshot->name_size != 0u ||
            snapshot->entries[0].name.data != NULL ||
            snapshot->entries[0].name.size != 0u)
            return false;
    } else if (snapshot->kind == VXML_CMETA_EXIT_NAMELIST) {
        if (snapshot->count == 0u || snapshot->name_size == 0u)
            return false;
        for (index = 0u; index < snapshot->count; ++index)
            if (snapshot->entries[index].name.size == 0u ||
                !terminal_span_valid(
                    snapshot->names, snapshot->name_size,
                    snapshot->entries[index].name.data,
                    snapshot->entries[index].name.size))
                return false;
    } else {
        return false;
    }
    for (index = 0u; index < snapshot->count; ++index)
        if (!terminal_value_valid(snapshot, &snapshot->entries[index].value))
            return false;
    return true;
}

vxml_status vxml_session_cmeta_exit_kind(
    const vxml_session *session, vxml_cmeta_exit_kind *out_kind) {
    const vxml_session_impl *impl = cmeta_session(session);
    const vxml_cmeta_session_data *profile;
    if (out_kind == NULL) return VXML_INVALID_ARGUMENT;
    *out_kind = VXML_CMETA_EXIT_EMPTY;
    if (impl == NULL) return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_EXITED) return VXML_INVALID_STATE;
    if (impl->profile_data == NULL) return VXML_INVALID_STRUCTURE;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    if (!terminal_exit_valid(&profile->terminal_exit))
        return VXML_INVALID_STRUCTURE;
    *out_kind = profile->terminal_exit.kind;
    return VXML_OK;
}

size_t vxml_session_cmeta_exit_count(const vxml_session *session) {
    const vxml_session_impl *impl = cmeta_session(session);
    const vxml_cmeta_session_data *profile;
    if (impl == NULL || impl->state != VXML_SESSION_EXITED ||
        impl->profile_data == NULL)
        return 0u;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    if (!terminal_exit_valid(&profile->terminal_exit)) return 0u;
    return profile->terminal_exit.kind == VXML_CMETA_EXIT_EXPRESSION ||
            profile->terminal_exit.kind == VXML_CMETA_EXIT_NAMELIST
        ? profile->terminal_exit.count : 0u;
}

vxml_status vxml_session_cmeta_exit_at(
    const vxml_session *session, size_t index,
    vxml_cmeta_name_view *out_name, vxml_cmeta_value_view *out_value) {
    const vxml_session_impl *impl;
    const vxml_cmeta_session_data *profile;
    if (out_name != NULL) *out_name = (vxml_cmeta_name_view){0};
    if (out_value != NULL) *out_value = (vxml_cmeta_value_view){0};
    if (out_name == NULL || out_value == NULL) return VXML_INVALID_ARGUMENT;
    impl = cmeta_session(session);
    if (impl == NULL) return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_EXITED) return VXML_INVALID_STATE;
    if (impl->profile_data == NULL) return VXML_INVALID_STRUCTURE;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    if (!terminal_exit_valid(&profile->terminal_exit))
        return VXML_INVALID_STRUCTURE;
    if (profile->terminal_exit.kind == VXML_CMETA_EXIT_EMPTY ||
        index >= profile->terminal_exit.count)
        return VXML_INVALID_ARGUMENT;
    *out_name = profile->terminal_exit.entries[index].name;
    *out_value = profile->terminal_exit.entries[index].value;
    return VXML_OK;
}
