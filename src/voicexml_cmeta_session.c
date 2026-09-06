#include <voicexml/cmeta.h>

#include "voicexml_allocator.h"
#include "voicexml_cmeta_internal.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static bool session_options_valid(
    const vxml_cmeta_session_options_v1 *options) {
    return options != NULL &&
        options->abi_version == VXML_CMETA_SESSION_OPTIONS_ABI_V1 &&
        options->struct_size >= sizeof(*options) &&
        options->max_transaction_bytes != 0u &&
        options->max_execution_steps != 0u &&
        ((options->initially_undefined == NULL) ==
         (options->initially_undefined_count == 0u));
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

static bool transaction_bytes_measure(
    const vxml_cmeta_program_data *program,
    size_t declared_count, size_t *out_bytes,
    size_t *out_exec_frame_capacity) {
    const cmeta_data_struct_shape *shape = session_root_shape(program);
    size_t total = 0u;
    size_t amount;
    size_t scope_index;
    size_t frame_capacity;
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

static void session_data_destroy(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program) {
    size_t index;
    if (session == NULL) return;
    root_storage_destroy(&session->staged_root, program);
    root_storage_destroy(&session->committed_root, program);
    if (session->staged_scopes != NULL)
        for (index = 0u; index < program->scope_count; ++index)
            cmeta_scope_storage_destroy(&session->staged_scopes[index]);
    if (session->committed_scopes != NULL)
        for (index = 0u; index < program->scope_count; ++index)
            cmeta_scope_storage_destroy(&session->committed_scopes[index]);
    vxml_free(session->exec_frames);
    vxml_free(session->runtime_scopes);
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

static void transaction_reset(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program) {
    size_t index;
    root_storage_clear(&session->staged_root, program);
    for (index = 0u; index < program->scope_count; ++index)
        cmeta_scope_view_clear(&session->staged_scopes[index].view);
    if (session->declared_count != 0u)
        memset(session->staged_declared, 0, session->declared_count);
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
    transaction_reset(session, program);
}

static vxml_status session_fail(
    vxml_session_impl *session, vxml_status status) {
    if (session == NULL) return status;
    session->state = VXML_SESSION_FAILED;
    session->error = status;
    return status;
}

static bool range_valid(size_t first, size_t count, size_t total) {
    return first <= total && count <= total - first;
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

static vxml_status initialize_first_form(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form) {
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
        if (block->form != 0u || block->scope >= program->scope_count ||
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

static vxml_status execute_clear(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    const vxml_cmeta_action_row *action) {
    size_t index;
    if (action->clear_all_form_items) {
        if (!range_valid(form->first_block, form->block_count,
                         program->block_count))
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
        }
    }
    return VXML_OK;
}

static vxml_status execute_var(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    const vxml_cmeta_block_row *block,
    const vxml_cmeta_action_row *action) {
    const size_t scopes[3] = {
        block->scope, form->scope, program->document_scope};
    unsigned char *declared;
    vxml_cmeta_value_view value;
    vxml_status status;
    if (action->scope != block->scope ||
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
        scopes, 3u, &value);
    if (status != VXML_OK) return status;
    return assign_scope_slot(
        session, program, true, action->scope, action->slot, &value);
}

static vxml_status execute_assign(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    const vxml_cmeta_block_row *block,
    const vxml_cmeta_action_row *action) {
    const size_t scopes[3] = {
        block->scope, form->scope, program->document_scope};
    vxml_cmeta_value_view value;
    vxml_status status;
    if (action->scope != VXML_CMETA_NO_INDEX) {
        unsigned char *declared;
        if (action->scope >= program->scope_count ||
            action->scope != block->scope ||
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
        scopes, 3u, &value);
    if (status != VXML_OK) return status;
    return assign_resolved_location(
        session, program, &program->locations[action->target], &value);
}

static vxml_status execute_exit(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    const vxml_cmeta_block_row *block,
    const vxml_cmeta_action_row *action) {
    const size_t scopes[3] = {
        block->scope, form->scope, program->document_scope};
    size_t index;
    if (action->exit_kind == VXML_CMETA_EXIT_EXPRESSION) {
        vxml_cmeta_value_view ignored;
        const vxml_status status = evaluate_expression(
            session, program, true, action->expression,
            scopes, 3u, &ignored);
        if (status != VXML_OK) return status;
    } else if (action->exit_kind == VXML_CMETA_EXIT_NAMELIST) {
        if (!range_valid(
                action->first_location, action->location_count,
                program->location_count) ||
            (action->location_count != 0u && program->locations == NULL))
            return VXML_INVALID_STRUCTURE;
        for (index = 0u; index < action->location_count; ++index) {
            resolved_location ignored;
            const vxml_status status = resolve_staged_location(
                session, program,
                &program->locations[action->first_location + index],
                &ignored);
            if (status != VXML_OK) return status;
        }
    } else if (action->exit_kind != VXML_CMETA_EXIT_EMPTY) {
        return VXML_INVALID_STRUCTURE;
    }
    session->exit_requested = true;
    return VXML_OK;
}

static vxml_status select_if_branch(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    const vxml_cmeta_block_row *block,
    const vxml_cmeta_action_row *action,
    size_t action_index,
    bool *out_selected, size_t *out_first, size_t *out_end) {
    const size_t scopes[3] = {
        block->scope, form->scope, program->document_scope};
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
                scopes, 3u, &matches);
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

static vxml_status execute_actions(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    const vxml_cmeta_block_row *block) {
    size_t frame_count = 1u;
    if (block->first_action > block->action_end ||
        !range_valid(block->first_action,
                     block->action_end - block->first_action,
                     program->action_count) ||
        session->exec_frame_capacity == 0u ||
        session->exec_frames == NULL ||
        (program->action_count != 0u && program->actions == NULL))
        return VXML_INVALID_STRUCTURE;
    session->exec_frames[0] = (vxml_cmeta_exec_frame){
        block->first_action, block->action_end};
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
                    session, program, form, block, action);
                if (status != VXML_OK) return status;
                break;
            }
            case VXML_CMETA_ACTION_IF: {
                bool selected;
                size_t first;
                size_t end;
                const vxml_status status = select_if_branch(
                    session, program, form, block, action,
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
                    session, program, form, block, action);
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
                    session, program, form, block, action);
                if (status != VXML_OK) return status;
                return VXML_OK;
            }
            default:
                return VXML_INVALID_STRUCTURE;
        }
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
    vxml_free(undefined);
    session->profile_data = profile;
    return VXML_OK;

failure:
    vxml_free(undefined);
    session_data_destroy(profile, program);
    return status;
}

vxml_status vxml_cmeta_session_start_profile(vxml_session_impl *session) {
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
        program->block_count == 0u || program->blocks == NULL)
        return session_fail(session, VXML_INVALID_STRUCTURE);
    form = &program->forms[0];
    if (program->document_scope >= program->scope_count ||
        form->scope >= program->scope_count ||
        !range_valid(form->first_block, form->block_count,
                     program->block_count))
        return session_fail(session, VXML_INVALID_STRUCTURE);
    if (!transaction_begin(profile, program))
        return session_fail(session, VXML_ALLOCATION_FAILED);
    status = initialize_first_form(profile, program, form);
    if (status != VXML_OK) {
        transaction_reset(profile, program);
        return session_fail(session, status);
    }
    transaction_commit(profile, program);
    for (;;) {
        const vxml_cmeta_block_row *selected = NULL;
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
        if (!consume_step(profile))
            return session_fail(session, VXML_LIMIT_EXCEEDED);
        if (!transaction_begin(profile, program))
            return session_fail(session, VXML_ALLOCATION_FAILED);
        {
            const bool completed = true;
            if (!cmeta_scope_view_assign(
                    &profile->staged_scopes[form->scope].view,
                    selected->form_item_slot, &completed)) {
                transaction_reset(profile, program);
                return session_fail(session, VXML_ALLOCATION_FAILED);
            }
            status = execute_actions(
                profile, program, form, selected);
            if (status != VXML_OK) {
                transaction_reset(profile, program);
                return session_fail(session, status);
            }
        }
        transaction_commit(profile, program);
        if (profile->exit_requested) {
            session->state = VXML_SESSION_EXITED;
            session->error = VXML_OK;
            return VXML_OK;
        }
    }
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

vxml_status vxml_session_cmeta_read(
    const vxml_session *session, const char *name, size_t name_size,
    vxml_cmeta_value_view *out_value) {
    if (out_value != NULL) *out_value = (vxml_cmeta_value_view){0};
    if (name == NULL || name_size == 0u || out_value == NULL)
        return VXML_INVALID_ARGUMENT;
    return cmeta_session(session) != NULL
        ? VXML_SEMANTIC_ERROR : VXML_INVALID_CONTRACT;
}

vxml_status vxml_session_cmeta_exit_kind(
    const vxml_session *session, vxml_cmeta_exit_kind *out_kind) {
    const vxml_session_impl *impl = cmeta_session(session);
    if (out_kind == NULL) return VXML_INVALID_ARGUMENT;
    if (impl == NULL) return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_EXITED) return VXML_INVALID_STATE;
    *out_kind = VXML_CMETA_EXIT_EMPTY;
    return VXML_OK;
}

size_t vxml_session_cmeta_exit_count(const vxml_session *session) {
    (void)session;
    return 0u;
}

vxml_status vxml_session_cmeta_exit_at(
    const vxml_session *session, size_t index,
    vxml_cmeta_name_view *out_name, vxml_cmeta_value_view *out_value) {
    if (out_name != NULL) *out_name = (vxml_cmeta_name_view){0};
    if (out_value != NULL) *out_value = (vxml_cmeta_value_view){0};
    if (out_name == NULL || out_value == NULL) return VXML_INVALID_ARGUMENT;
    if (cmeta_session(session) == NULL) return VXML_INVALID_CONTRACT;
    (void)index;
    return VXML_INVALID_ARGUMENT;
}
