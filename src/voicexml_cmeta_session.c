#include <voicexml/cmeta.h>

#include "voicexml_allocator.h"
#include "voicexml_cmeta_internal.h"

#include <data_bind_csv_provider.h>
#include <data_bind_format_provider.h>
#include <data_bind_json_provider.h>
#include <data_bind_xml_provider.h>
#include <data_bind_yaml_provider.h>

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static vxml_status read_scalar_value(
    const cmeta_data_desc *data, const void *object,
    unsigned char *string_scratch, size_t string_capacity,
    vxml_cmeta_value_view *out_value);

static bool range_valid(size_t first, size_t count, size_t total);

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
    const size_t adapter_prefix_size =
        offsetof(vxml_cmeta_prompt_media_adapter_v1, cancel) +
        sizeof(((vxml_cmeta_prompt_media_adapter_v1 *)0)->cancel);
    const vxml_cmeta_prompt_media_adapter_v1 *adapter;
    if (options == NULL || options->struct_size < tail_size)
        return false;
    adapter = options->prompt_media;
    return adapter != NULL &&
        adapter->abi_version == VXML_CMETA_PROMPT_MEDIA_ADAPTER_ABI_V1 &&
        adapter->struct_size >= adapter_prefix_size &&
        adapter->prepare != NULL &&
        adapter->cancel != NULL;
}

static bool session_collect_options_valid(
    const vxml_cmeta_session_options_v1 *options) {
    const size_t tail_size =
        offsetof(vxml_cmeta_session_options_v1, collect_user) +
        sizeof(options->collect_user);
    const size_t adapter_prefix_size =
        offsetof(vxml_cmeta_collect_adapter_v1, cancel) +
        sizeof(((vxml_cmeta_collect_adapter_v1 *)0)->cancel);
    const vxml_cmeta_collect_adapter_v1 *adapter;
    if (options == NULL || options->struct_size < tail_size)
        return false;
    adapter = options->collect;
    return adapter != NULL &&
        adapter->abi_version == VXML_CMETA_COLLECT_ADAPTER_ABI_V1 &&
        adapter->struct_size >= adapter_prefix_size &&
        adapter->prepare != NULL &&
        adapter->cancel != NULL;
}


static bool session_subdialog_completion_options_present(
    const vxml_cmeta_session_options_v1 *options) {
    const size_t tail_size =
        offsetof(vxml_cmeta_session_options_v1, max_subdialog_completion_bytes) +
        sizeof(options->max_subdialog_completion_bytes);
    return options != NULL && options->struct_size >= tail_size &&
        (options->max_subdialog_completion_entries != 0u ||
         options->max_subdialog_completion_bytes != 0u);
}

static bool session_subdialog_options_valid(
    const vxml_cmeta_session_options_v1 *options) {
    const size_t tail_size =
        offsetof(vxml_cmeta_session_options_v1, max_subdialog_snapshot_bytes) +
        sizeof(options->max_subdialog_snapshot_bytes);
    const vxml_cmeta_subdialog_adapter_v1 *adapter;
    if (options == NULL || options->struct_size < tail_size ||
        options->max_subdialog_snapshot_bytes == 0u)
        return false;
    adapter = options->subdialog;
    return adapter != NULL &&
        adapter->abi_version == VXML_CMETA_SUBDIALOG_ADAPTER_ABI_V1 &&
        adapter->struct_size >= sizeof(*adapter) &&
        adapter->prepare != NULL &&
        adapter->cancel != NULL;
}

static bool session_record_options_valid(
    const vxml_cmeta_session_options_v1 *options) {
    const size_t tail_size =
        offsetof(vxml_cmeta_session_options_v1, max_record_bytes) +
        sizeof(options->max_record_bytes);
    const vxml_cmeta_record_adapter_v1 *adapter;
    if (options == NULL || options->struct_size < tail_size ||
        options->max_record_bytes == 0u)
        return false;
    adapter = options->record;
    return adapter != NULL &&
        adapter->abi_version == VXML_CMETA_RECORD_ADAPTER_ABI_V1 &&
        adapter->struct_size >= sizeof(*adapter) &&
        adapter->prepare != NULL &&
        adapter->cancel != NULL &&
        adapter->quiesce != NULL;
}

static bool session_transfer_options_valid(
    const vxml_cmeta_session_options_v1 *options) {
    const size_t tail_size =
        offsetof(vxml_cmeta_session_options_v1, transfer_user) +
        sizeof(options->transfer_user);
    const vxml_cmeta_transfer_adapter_v1 *adapter;
    if (options == NULL || options->struct_size < tail_size)
        return false;
    adapter = options->transfer;
    return adapter != NULL &&
        adapter->abi_version == VXML_CMETA_TRANSFER_ADAPTER_ABI_V1 &&
        adapter->struct_size >= sizeof(*adapter) &&
        adapter->prepare != NULL &&
        adapter->cancel != NULL &&
        adapter->quiesce != NULL;
}

static bool collect_adapter_has_menu(
    const vxml_cmeta_collect_adapter_v1 *adapter) {
    const size_t tail_size =
        offsetof(vxml_cmeta_collect_adapter_v1, prepare_menu) +
        sizeof(((vxml_cmeta_collect_adapter_v1 *)0)->prepare_menu);
    return adapter != NULL &&
        adapter->struct_size >= tail_size &&
        adapter->prepare_menu != NULL;
}

static bool collect_adapter_has_menu_v2(
    const vxml_cmeta_collect_adapter_v1 *adapter) {
    const size_t tail_size =
        offsetof(vxml_cmeta_collect_adapter_v1, prepare_menu_v2) +
        sizeof(((vxml_cmeta_collect_adapter_v1 *)0)->prepare_menu_v2);
    return adapter != NULL &&
        adapter->struct_size >= tail_size &&
        adapter->prepare_menu_v2 != NULL;
}

static bool collect_adapter_has_initial(
    const vxml_cmeta_collect_adapter_v1 *adapter) {
    const size_t tail_size =
        offsetof(vxml_cmeta_collect_adapter_v1, prepare_initial) +
        sizeof(((vxml_cmeta_collect_adapter_v1 *)0)->prepare_initial);
    return adapter != NULL &&
        adapter->struct_size >= tail_size &&
        adapter->prepare_initial != NULL;
}

static bool collect_adapter_has_field_v2(
    const vxml_cmeta_collect_adapter_v1 *adapter) {
    const size_t tail_size =
        offsetof(vxml_cmeta_collect_adapter_v1, quiesce) +
        sizeof(((vxml_cmeta_collect_adapter_v1 *)0)->quiesce);
    return adapter != NULL &&
        adapter->struct_size >= tail_size &&
        adapter->prepare_v2 != NULL &&
        adapter->quiesce != NULL;
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


static bool session_subdialogs_valid(
    const vxml_cmeta_program_data *program) {
    const cmeta_data_struct_shape *shape = session_root_shape(program);
    size_t index;
    if (program == NULL || shape == NULL ||
        (program->subdialog_count != 0u && program->subdialogs == NULL) ||
        (program->subdialog_param_count != 0u &&
         program->subdialog_params == NULL))
        return false;
    for (index = 0u; index < program->subdialog_count; ++index) {
        const vxml_cmeta_subdialog_row *row =
            &program->subdialogs[index];
        const cmeta_data_field_desc *field;
        size_t field_name_size;
        if (row->form >= program->form_count ||
            row->root_field >= shape->field_count ||
            row->name == NULL || row->name_size == 0u ||
            row->src == NULL || row->src_size == 0u ||
            row->result_data == NULL ||
            row->result_data->kind != CMETA_DATA_STRUCT ||
            row->result_data->storage_type == NULL ||
            row->result_data->shape == NULL ||
            (row->condition != VXML_CMETA_NO_INDEX &&
             row->condition >= program->expression_count) ||
            (row->filled != VXML_CMETA_NO_INDEX &&
             (row->filled >= program->filled_count ||
              program->filled == NULL ||
              program->filled[row->filled].form != row->form ||
              program->filled[row->filled].mode !=
                  VXML_CMETA_FILLED_FIELD ||
              program->filled[row->filled].field !=
                  VXML_CMETA_NO_INDEX)))
            return false;
        field = &shape->fields[row->root_field];
        if (field->name == NULL ||
            field->value != row->result_data ||
            field->offset != row->field_offset)
            return false;
        field_name_size = strlen(field->name);
        if (field_name_size != row->name_size ||
            memcmp(field->name, row->name, row->name_size) != 0 ||
            !range_valid(
                row->first_param, row->param_count,
                program->subdialog_param_count))
            return false;
        {
            size_t offset;
            for (offset = 0u; offset < row->param_count; ++offset) {
                const vxml_cmeta_subdialog_param_row *param =
                    &program->subdialog_params[row->first_param + offset];
                size_t prior;
                if (param->subdialog != index ||
                    param->name == NULL || param->name_size == 0u)
                    return false;
                if (param->source == VXML_CMETA_SUBDIALOG_PARAM_TYPED) {
                    if (param->expression == VXML_CMETA_NO_INDEX ||
                        param->expression >= program->expression_count ||
                        param->literal != NULL || param->literal_size != 0u)
                        return false;
                } else if (param->source ==
                               VXML_CMETA_SUBDIALOG_PARAM_LITERAL) {
                    if (param->expression != VXML_CMETA_NO_INDEX ||
                        param->literal == NULL ||
                        param->literal_size >
                            program->max_subdialog_param_value_bytes)
                        return false;
                } else {
                    return false;
                }
                for (prior = 0u; prior < offset; ++prior) {
                    const vxml_cmeta_subdialog_param_row *previous =
                        &program->subdialog_params[
                            row->first_param + prior];
                    if (previous->name_size == param->name_size &&
                        memcmp(
                            previous->name, param->name,
                            param->name_size) == 0)
                        return false;
                }
            }
        }
    }
    return true;
}

static bool session_records_valid(
    const vxml_cmeta_program_data *program) {
    size_t index;
    if (program == NULL ||
        (program->form_count != 0u && program->forms == NULL) ||
        (program->scope_count != 0u && program->scopes == NULL) ||
        (program->record_count != 0u && program->records == NULL))
        return false;
    for (index = 0u; index < program->record_count; ++index) {
        const vxml_cmeta_record_row *row = &program->records[index];
        const vxml_cmeta_form_row *form;
        if (row->form >= program->form_count ||
            program->forms == NULL ||
            row->name == NULL || row->name_size == 0u ||
            row->max_duration_us == UINT64_C(0) ||
            row->max_final_silence_us == UINT64_C(0) ||
            row->max_media_type_bytes == 0u ||
            row->media_type_size > row->max_media_type_bytes ||
            (row->has_maxtime &&
             row->maxtime_us > row->max_duration_us) ||
            (row->has_final_silence &&
             row->final_silence_us > row->max_final_silence_us) ||
            ((row->media_type == NULL) !=
             (row->media_type_size == 0u)) ||
            (row->condition != VXML_CMETA_NO_INDEX &&
             row->condition >= program->expression_count) ||
            (row->filled != VXML_CMETA_NO_INDEX &&
             (row->filled >= program->filled_count ||
              program->filled == NULL ||
              program->filled[row->filled].form != row->form ||
              program->filled[row->filled].mode !=
                  VXML_CMETA_FILLED_FIELD ||
              program->filled[row->filled].field !=
                  VXML_CMETA_NO_INDEX)))
            return false;
        form = &program->forms[row->form];
        if (form->scope >= program->scope_count ||
            row->form_item_slot == VXML_CMETA_NO_INDEX ||
            row->form_item_slot >=
                program->scopes[form->scope].schema.slot_count)
            return false;
    }
    return true;
}

static bool session_transfers_valid(
    const vxml_cmeta_program_data *program) {
    size_t index;
    if (program == NULL ||
        (program->form_count != 0u && program->forms == NULL) ||
        (program->scope_count != 0u && program->scopes == NULL) ||
        (program->transfer_count != 0u && program->transfers == NULL))
        return false;
    for (index = 0u; index < program->transfer_count; ++index) {
        const vxml_cmeta_transfer_row *row = &program->transfers[index];
        const vxml_cmeta_form_row *form;
        const uint64_t mode_cap =
            row->mode == VXML_CMETA_TRANSFER_BRIDGE
                ? VXML_CMETA_TRANSFER_CAP_BRIDGE
                : VXML_CMETA_TRANSFER_CAP_BLIND;
        if (row->form >= program->form_count ||
            program->forms == NULL ||
            row->name == NULL || row->name_size == 0u ||
            row->destination == NULL || row->destination_size == 0u ||
            (row->mode != VXML_CMETA_TRANSFER_BLIND &&
             row->mode != VXML_CMETA_TRANSFER_BRIDGE) ||
            row->max_connect_timeout_us == UINT64_C(0) ||
            row->max_duration_us == UINT64_C(0) ||
            (row->has_connect_timeout &&
             row->connect_timeout_us > row->max_connect_timeout_us) ||
            (row->has_maxtime &&
             row->maxtime_us > row->max_duration_us) ||
            ((row->transfer_audio == NULL) !=
             (row->transfer_audio_size == 0u)) ||
            (row->required_capabilities & mode_cap) == UINT64_C(0) ||
            (row->condition != VXML_CMETA_NO_INDEX &&
             row->condition >= program->expression_count) ||
            (row->filled != VXML_CMETA_NO_INDEX &&
             (row->filled >= program->filled_count ||
              program->filled == NULL ||
              program->filled[row->filled].form != row->form ||
              program->filled[row->filled].mode !=
                  VXML_CMETA_FILLED_FIELD ||
              program->filled[row->filled].field !=
                  VXML_CMETA_NO_INDEX)))
            return false;
        form = &program->forms[row->form];
        if (form->scope >= program->scope_count ||
            row->form_item_slot == VXML_CMETA_NO_INDEX ||
            row->form_item_slot >=
                program->scopes[form->scope].schema.slot_count ||
            index < form->first_transfer ||
            index - form->first_transfer >= form->transfer_count)
            return false;
    }
    return true;
}

static bool session_root_contract_valid(
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_session_options_v1 *options) {
    const cmeta_data_struct_shape *shape = session_root_shape(program);
    return session_root_fields_valid(program) &&
        session_subdialogs_valid(program) &&
        session_records_valid(program) &&
        session_transfers_valid(program) && shape != NULL &&
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
        if (action->kind != VXML_CMETA_ACTION_EXIT &&
            action->kind != VXML_CMETA_ACTION_RETURN &&
            action->kind != VXML_CMETA_ACTION_DISCONNECT)
            continue;
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

static vxml_status exit_snapshot_prepare_capacity(
    vxml_cmeta_exit_snapshot *snapshot,
    size_t entry_capacity,
    size_t name_capacity,
    size_t string_capacity) {
    exit_snapshot_destroy(snapshot);
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

static vxml_status exit_snapshot_prepare_range(
    vxml_cmeta_exit_snapshot *snapshot,
    const vxml_cmeta_program_data *program,
    size_t first_action, size_t action_end) {
    size_t entry_capacity;
    size_t name_capacity;
    size_t string_capacity;
    if (!exit_capacity_measure(
            program, first_action, action_end,
            &entry_capacity, &name_capacity, &string_capacity))
        return VXML_INVALID_STRUCTURE;
    return exit_snapshot_prepare_capacity(
        snapshot, entry_capacity, name_capacity, string_capacity);
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

static void terminal_pending_reset(vxml_cmeta_session_data *session) {
    if (session == NULL) return;
    session->pending_terminal_kind = VXML_CMETA_TERMINAL_NONE;
    session->pending_terminal_event = NULL;
    session->pending_terminal_event_size = 0u;
    session->exit_requested = false;
}

static void terminal_publish(vxml_cmeta_session_data *session) {
    if (session == NULL) return;
    exit_snapshot_publish(session);
    session->terminal_kind = session->pending_terminal_kind;
    session->terminal_event = session->pending_terminal_event;
    session->terminal_event_size = session->pending_terminal_event_size;
    session->pending_terminal_kind = VXML_CMETA_TERMINAL_NONE;
    session->pending_terminal_event = NULL;
    session->pending_terminal_event_size = 0u;
}

static void prompt_media_mailbox_disarm(
    vxml_cmeta_session_data *session) {
    unsigned state;
    if (session == NULL) return;
    atomic_store_explicit(
        &session->prompt_media_mailbox.generation,
        UINT64_C(0), memory_order_relaxed);
    state = atomic_load_explicit(
        &session->prompt_media_mailbox.state, memory_order_acquire);
    if (state != VXML_CMETA_PROMPT_MEDIA_MAILBOX_CLOSED)
        atomic_store_explicit(
            &session->prompt_media_mailbox.state,
            VXML_CMETA_PROMPT_MEDIA_MAILBOX_DISARMED,
            memory_order_release);
    session->prompt_media_mailbox.has_terminal_timing = false;
    session->prompt_media_mailbox.terminal_elapsed_ms = UINT64_C(0);
}

static void settle_prompt_media(
    vxml_cmeta_session_data *session) {
    if (session == NULL) return;
    prompt_media_mailbox_disarm(session);
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


static void subdialog_snapshot_destroy(
    vxml_cmeta_session_data *session) {
    if (session == NULL) return;
    vxml_free(session->subdialog_snapshot_storage);
    vxml_free(session->subdialog_snapshot_params);
    session->subdialog_snapshot_storage = NULL;
    session->subdialog_snapshot_params = NULL;
    session->subdialog_snapshot_storage_size = 0u;
    session->subdialog_snapshot_storage_capacity = 0u;
    session->subdialog_snapshot_param_count = 0u;
    session->subdialog_snapshot_generation = UINT64_C(0);
}

static void settle_subdialog(
    vxml_cmeta_session_data *session) {
    if (session == NULL) return;
    if (session->subdialog_prepared) {
        vxml_cmeta_subdialog_ticket_v1 ticket =
            session->subdialog_ticket;
        session->subdialog_prepared = false;
        session->subdialog_ticket =
            (vxml_cmeta_subdialog_ticket_v1){0};
        if (ticket.discard != NULL)
            ticket.discard(ticket.user);
    } else if (session->subdialog_in_flight &&
               session->subdialog_adapter != NULL) {
        const uint64_t generation = session->subdialog_generation;
        session->subdialog_in_flight = false;
        session->subdialog_adapter->cancel(
            session->subdialog_user, generation);
    }
    subdialog_snapshot_destroy(session);
}

static void record_release_lease(
    vxml_cmeta_recording_lease_v1 *recording) {
    if (recording == NULL) return;
    if (recording->lease != NULL && recording->release != NULL)
        recording->release(recording->release_user, recording->lease);
    *recording = (vxml_cmeta_recording_lease_v1){0};
}

static void collect_recording_payload_reset(
    vxml_cmeta_collect_mailbox *mailbox,
    bool release_recording) {
    vxml_cmeta_recording_lease_v1 recording;
    if (mailbox == NULL) return;
    recording = mailbox->recording;
    mailbox->recording = (vxml_cmeta_recording_lease_v1){0};
    mailbox->recording_duration_us = UINT64_C(0);
    mailbox->recording_media_type_size = 0u;
    if (release_recording)
        record_release_lease(&recording);
}

static void collect_utterance_result_reset(
    vxml_cmeta_session_data *session) {
    vxml_cmeta_collect_utterance_result_slot *result;
    vxml_cmeta_recording_lease_v1 recording;
    char *media_type;
    size_t media_type_capacity;
    bool live;
    if (session == NULL) return;
    result = &session->collect_utterance_result;
    recording = result->recording;
    media_type = result->media_type;
    media_type_capacity = result->media_type_capacity;
    live = result->live;
    *result = (vxml_cmeta_collect_utterance_result_slot){0};
    result->media_type = media_type;
    result->media_type_capacity = media_type_capacity;
    if (live)
        record_release_lease(&recording);
}

static void field_recording_shadow_reset(
    vxml_cmeta_session_data *session,
    size_t field_index) {
    if (session == NULL ||
        session->field_recording_shadows == NULL ||
        field_index >= session->field_recording_shadow_count)
        return;
    session->field_recording_shadows[field_index] =
        (vxml_cmeta_field_recording_shadow){0};
}

static void mark_result_slot_reset(
    vxml_cmeta_mark_result_slot *slot) {
    char *name;
    if (slot == NULL) return;
    name = slot->name;
    *slot = (vxml_cmeta_mark_result_slot){0};
    slot->name = name;
}

static bool mark_result_slot_publish(
    vxml_cmeta_mark_result_slot *slot,
    size_t name_capacity,
    uint64_t generation,
    const char *name,
    size_t name_size,
    uint64_t marktime_ms,
    bool has_mark) {
    if (slot == NULL || generation == UINT64_C(0) ||
        (has_mark &&
         (slot->name == NULL || name == NULL || name_size == 0u ||
          name_size > name_capacity)))
        return false;
    mark_result_slot_reset(slot);
    slot->assigned = true;
    slot->generation = generation;
    slot->has_mark = has_mark;
    if (has_mark) {
        memcpy(slot->name, name, name_size);
        slot->name_size = name_size;
        slot->marktime_ms = marktime_ms;
    }
    return true;
}

static void field_mark_shadow_reset(
    vxml_cmeta_session_data *session,
    size_t field_index) {
    if (session == NULL ||
        session->field_mark_shadows == NULL ||
        field_index >= session->field_mark_shadow_count)
        return;
    mark_result_slot_reset(
        &session->field_mark_shadows[field_index]);
}

static void collect_quiesce_generation(
    vxml_cmeta_session_data *session,
    uint64_t generation) {
    if (session == NULL || generation == UINT64_C(0) ||
        session->collect_adapter == NULL ||
        !collect_adapter_has_field_v2(session->collect_adapter) ||
        session->collect_quiesced_generation == generation)
        return;
    session->collect_adapter->quiesce(
        session->collect_user, generation);
    session->collect_quiesced_generation = generation;
}

static void record_mailbox_payload_reset(
    vxml_cmeta_record_completion_mailbox *mailbox,
    bool release_recording) {
    vxml_cmeta_recording_lease_v1 recording;
    if (mailbox == NULL) return;
    recording = mailbox->recording;
    mailbox->recording = (vxml_cmeta_recording_lease_v1){0};
    mailbox->outcome = (vxml_cmeta_record_outcome)0;
    mailbox->duration_us = UINT64_C(0);
    mailbox->has_termchar = false;
    mailbox->termchar = '\0';
    mailbox->media_type_size = 0u;
    if (release_recording)
        record_release_lease(&recording);
}

static void record_result_slot_reset(
    vxml_cmeta_record_result_slot *slot) {
    vxml_cmeta_recording_lease_v1 recording;
    char *media_type;
    bool live;
    if (slot == NULL) return;
    media_type = slot->media_type;
    recording = slot->recording;
    live = slot->live;
    *slot = (vxml_cmeta_record_result_slot){0};
    slot->media_type = media_type;
    if (live)
        record_release_lease(&recording);
}

static void record_quiesce_generation(
    vxml_cmeta_session_data *session,
    uint64_t generation) {
    if (session == NULL || generation == UINT64_C(0) ||
        session->record_adapter == NULL ||
        session->record_adapter->quiesce == NULL ||
        session->record_quiesced_generation == generation)
        return;
    session->record_adapter->quiesce(
        session->record_user, generation);
    session->record_quiesced_generation = generation;
}

static void record_results_reconcile(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program) {
    size_t index;
    if (session == NULL || program == NULL ||
        session->record_results == NULL ||
        session->committed_scopes == NULL)
        return;
    for (index = 0u;
         index < session->record_result_count &&
         index < program->record_count;
         ++index) {
        vxml_cmeta_record_result_slot *result =
            &session->record_results[index];
        const vxml_cmeta_record_row *record =
            &program->records[index];
        const vxml_cmeta_form_row *form;
        if (!result->live) continue;
        if (record->form >= program->form_count ||
            program->forms == NULL) {
            record_result_slot_reset(result);
            continue;
        }
        form = &program->forms[record->form];
        if (form->scope >= program->scope_count ||
            record->form_item_slot >=
                program->scopes[form->scope].schema.slot_count ||
            session->committed_scopes[form->scope].view.bound == NULL ||
            session->committed_scopes[form->scope].view.bound[
                record->form_item_slot] == 0u)
            record_result_slot_reset(result);
    }
}

static void settle_record(
    vxml_cmeta_session_data *session) {
    unsigned previous_state;
    uint64_t generation;
    size_t index;
    if (session == NULL) return;

    previous_state = atomic_exchange_explicit(
        &session->record_mailbox.state,
        VXML_CMETA_RECORD_MAILBOX_CLOSED,
        memory_order_acq_rel);
    generation = atomic_load_explicit(
        &session->record_mailbox.generation,
        memory_order_relaxed);

    if (session->record_prepared) {
        vxml_cmeta_record_ticket_v1 ticket =
            session->record_ticket;
        session->record_prepared = false;
        session->record_ticket =
            (vxml_cmeta_record_ticket_v1){0};
        if (ticket.discard != NULL)
            ticket.discard(ticket.user);
    }

    if (session->record_in_flight &&
        session->record_adapter != NULL) {
        if (previous_state == VXML_CMETA_RECORD_MAILBOX_EMPTY ||
            previous_state == VXML_CMETA_RECORD_MAILBOX_DISARMED)
            session->record_adapter->cancel(
                session->record_user, generation);
        record_quiesce_generation(session, generation);
        session->record_in_flight = false;
    }

    /*
     * READY means ACCEPTED already transferred the lease to the Session.
     * WRITING never transfers ownership: close waits for the producer but
     * leaves that lease with the producer when ingress returns CLOSED.
     */
    record_mailbox_payload_reset(
        &session->record_mailbox,
        previous_state == VXML_CMETA_RECORD_MAILBOX_READY);
    atomic_store_explicit(
        &session->record_mailbox.generation,
        UINT64_C(0), memory_order_relaxed);

    if (session->record_results != NULL)
        for (index = 0u;
             index < session->record_result_count;
             ++index)
            record_result_slot_reset(
                &session->record_results[index]);
}

static void transfer_quiesce_generation(
    vxml_cmeta_session_data *session,
    uint64_t generation) {
    if (session == NULL || generation == UINT64_C(0) ||
        session->transfer_adapter == NULL ||
        session->transfer_adapter->quiesce == NULL ||
        session->transfer_quiesced_generation == generation)
        return;
    session->transfer_adapter->quiesce(
        session->transfer_user, generation);
    session->transfer_quiesced_generation = generation;
}

static void settle_transfer(
    vxml_cmeta_session_data *session) {
    uint64_t generation;
    if (session == NULL) return;
    generation = session->transfer_generation;
    if (session->transfer_prepared) {
        vxml_cmeta_transfer_ticket_v1 ticket =
            session->transfer_ticket;
        session->transfer_prepared = false;
        session->transfer_ticket =
            (vxml_cmeta_transfer_ticket_v1){0};
        if (ticket.discard != NULL)
            ticket.discard(ticket.user);
    } else if (session->transfer_in_flight &&
               session->transfer_adapter != NULL) {
        session->transfer_in_flight = false;
        session->transfer_adapter->cancel(
            session->transfer_user, generation);
        transfer_quiesce_generation(session, generation);
    }
}

static void session_data_destroy(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program) {
    size_t index;
    if (session == NULL) return;
    atomic_store_explicit(
        &session->prompt_media_mailbox.state,
        VXML_CMETA_PROMPT_MEDIA_MAILBOX_CLOSED,
        memory_order_release);
    atomic_store_explicit(
        &session->prompt_media_mailbox.generation,
        UINT64_C(0), memory_order_relaxed);
    settle_prompt_media(session);
    {
        const unsigned previous_subdialog_state =
            atomic_exchange_explicit(
                &session->subdialog_mailbox.state,
                VXML_CMETA_SUBDIALOG_MAILBOX_CLOSED,
                memory_order_acq_rel);
        atomic_store_explicit(
            &session->subdialog_mailbox.generation,
            UINT64_C(0), memory_order_relaxed);
        if (previous_subdialog_state ==
                VXML_CMETA_SUBDIALOG_MAILBOX_READY)
            session->subdialog_in_flight = false;
    }
    {
        const unsigned previous_collect_state =
            atomic_exchange_explicit(
                &session->collect_mailbox.state,
                VXML_CMETA_COLLECT_MAILBOX_CLOSED,
                memory_order_acq_rel);
        const uint64_t collect_generation =
            atomic_load_explicit(
                &session->collect_mailbox.generation,
                memory_order_relaxed);
        settle_subdialog(session);
        settle_record(session);
        settle_transfer(session);
        if (session->collect_prepared) {
            vxml_cmeta_collect_ticket_v1 ticket = session->collect_ticket;
            session->collect_prepared = false;
            session->collect_ticket = (vxml_cmeta_collect_ticket_v1){0};
            if (ticket.discard != NULL)
                ticket.discard(ticket.user);
        } else if (session->collect_in_flight &&
                   session->collect_adapter != NULL) {
            const bool recorded_utterance =
                session->collect_mailbox.record_utterance_expected;

            /*
             * Preserve the legacy collect contract: close cancels an active
             * generation even when a scalar completion is already READY.
             *
             * Recorded-utterance V3 is different after ACCEPTED: READY means
             * the media lease already transferred to the mailbox, so close
             * quiesces instead of manufacturing a cancel after completion.
             * Before an accepted completion, cancel then quiesce.
             */
            if (!recorded_utterance ||
                previous_collect_state ==
                    VXML_CMETA_COLLECT_MAILBOX_EMPTY ||
                previous_collect_state ==
                    VXML_CMETA_COLLECT_MAILBOX_DISARMED)
                session->collect_adapter->cancel(
                    session->collect_user, collect_generation);
            if (recorded_utterance)
                collect_quiesce_generation(
                    session, collect_generation);
            session->collect_in_flight = false;
        }
        collect_recording_payload_reset(
            &session->collect_mailbox,
            previous_collect_state ==
                VXML_CMETA_COLLECT_MAILBOX_READY);
        collect_utterance_result_reset(session);
        atomic_store_explicit(
            &session->collect_mailbox.generation,
            UINT64_C(0), memory_order_relaxed);
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
    vxml_free(session->subdialog_mailbox.storage);
    vxml_free(session->subdialog_mailbox.entries);
    vxml_free(session->record_mailbox.media_type);
    vxml_free(session->record_result_media_storage);
    vxml_free(session->record_results);
    vxml_free(session->initial_retry_reset_pending);
    vxml_free(session->retry_reset_pending);
    vxml_free(session->event_counter_names);
    vxml_free(session->event_counters);
    vxml_free(session->field_mark_name_storage);
    vxml_free(session->field_mark_shadows);
    vxml_free(session->collect_mark_result_name);
    vxml_free(session->pending_mark_result_name);
    vxml_free(session->prompt_media_last_mark_name);
    vxml_free(session->prompt_media_dynamic_mark_storage);
    vxml_free(session->prompt_media_projected_segments);
    vxml_free(session->collect_mailbox.recording_media_type);
    vxml_free(session->collect_utterance_result_media_type);
    vxml_free(session->field_recording_shadows);
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

static void reset_owner_retry_counters(
    vxml_cmeta_session_data *session,
    vxml_cmeta_event_scope_kind scope_kind,
    size_t owner) {
    size_t index;
    if (session == NULL) return;
    for (index = 0u; index < session->event_counter_count; ++index) {
        vxml_cmeta_event_counter *counter =
            &session->event_counters[index];
        if (counter->scope_kind == scope_kind &&
            counter->owner == owner &&
            recovery_event_name(counter))
            counter->count = 0u;
    }
}

static void reset_field_retry_counters(
    vxml_cmeta_session_data *session, size_t field_index) {
    reset_owner_retry_counters(
        session, VXML_CMETA_EVENT_FIELD, field_index);
}

static void reset_initial_retry_counters(
    vxml_cmeta_session_data *session, size_t initial_index) {
    reset_owner_retry_counters(
        session, VXML_CMETA_EVENT_INITIAL, initial_index);
}

static void reset_form_retry_counters(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form) {
    size_t offset;
    if (session == NULL || program == NULL || form == NULL ||
        !range_valid(form->first_field, form->field_count,
                     program->field_count) ||
        !range_valid(form->first_initial, form->initial_count,
                     program->initial_count))
        return;
    for (offset = 0u; offset < form->field_count; ++offset)
        reset_field_retry_counters(
            session, form->first_field + offset);
    for (offset = 0u; offset < form->initial_count; ++offset)
        reset_initial_retry_counters(
            session, form->first_initial + offset);
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

static void mark_initial_retry_reset(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    size_t initial_index) {
    if (session == NULL || program == NULL ||
        session->initial_retry_reset_pending == NULL ||
        initial_index >= program->initial_count)
        return;
    session->initial_retry_reset_pending[initial_index] = 1u;
}

static void apply_retry_resets(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program) {
    size_t index;
    if (session == NULL || program == NULL)
        return;
    if (session->retry_reset_pending != NULL)
        for (index = 0u; index < program->field_count; ++index)
            if (session->retry_reset_pending[index] != 0u) {
                reset_field_retry_counters(session, index);
                field_recording_shadow_reset(session, index);
                field_mark_shadow_reset(session, index);
            }
    if (session->initial_retry_reset_pending != NULL)
        for (index = 0u; index < program->initial_count; ++index)
            if (session->initial_retry_reset_pending[index] != 0u)
                reset_initial_retry_counters(session, index);
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
    if (program->initial_count != 0u &&
        session->initial_retry_reset_pending != NULL)
        memset(
            session->initial_retry_reset_pending, 0,
            program->initial_count);
}

static bool transaction_begin(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program) {
    size_t index;
    session->pending_navigation_uri = NULL;
    session->pending_navigation_uri_size = 0u;
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
    record_results_reconcile(session, program);
    transaction_reset(session, program);
}

static vxml_status session_fail(
    vxml_session_impl *session, vxml_status status) {
    if (session == NULL) return status;
    session->state = VXML_SESSION_FAILED;
    session->error = status;
    return status;
}

static vxml_status publish_pending_navigation(
    vxml_session_impl *impl,
    vxml_cmeta_session_data *profile) {
    if (impl == NULL || profile == NULL)
        return VXML_INVALID_ARGUMENT;
    if (profile->pending_navigation_uri == NULL)
        return VXML_OK;
    if (profile->pending_navigation_uri_size == 0u ||
        memchr(
            profile->pending_navigation_uri, '\0',
            profile->pending_navigation_uri_size) != NULL)
        return VXML_INVALID_STRUCTURE;
    impl->navigation_uri =
        profile->pending_navigation_uri;
    impl->navigation_uri_size =
        profile->pending_navigation_uri_size;
    impl->navigation_fetchaudio_uri = NULL;
    impl->navigation_fetchaudio_uri_size = 0u;
    profile->pending_navigation_uri = NULL;
    profile->pending_navigation_uri_size = 0u;
    impl->state = VXML_SESSION_NAVIGATING;
    impl->error = VXML_OK;
    return VXML_OK;
}

static bool consume_step(vxml_cmeta_session_data *session) {
    if (session->execution_steps >= session->max_execution_steps)
        return false;
    ++session->execution_steps;
    return true;
}

static bool collect_pending_recording_metadata(
    const vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    size_t source_field,
    size_t *out_size,
    uint64_t *out_duration_ms) {
    unsigned state;
    if (out_size != NULL) *out_size = 0u;
    if (out_duration_ms != NULL) *out_duration_ms = UINT64_C(0);
    if (session == NULL ||
        !session->collect_in_flight ||
        !session->collect_mailbox.record_utterance_expected ||
        session->collect_mailbox.recording.lease == NULL ||
        session->collect_mailbox.recording.data == NULL ||
        session->collect_mailbox.recording.size == 0u)
        return false;
    if (source_field != VXML_CMETA_NO_INDEX) {
        const cmeta_data_struct_shape *root_shape =
            session_root_shape(program);
        const vxml_cmeta_field_row *field;
        if (program == NULL ||
            source_field >= program->field_count ||
            program->fields == NULL ||
            session->active_field != source_field ||
            root_shape == NULL)
            return false;
        field = &program->fields[source_field];
        if (field->root_field >= root_shape->field_count ||
            session->staged_root.bound == NULL ||
            session->staged_root.bound[field->root_field] == 0u)
            return false;
    }
    state = atomic_load_explicit(
        &session->collect_mailbox.state, memory_order_acquire);
    if (state != VXML_CMETA_COLLECT_MAILBOX_WRITING)
        return false;
    if (out_size != NULL)
        *out_size = session->collect_mailbox.recording.size;
    if (out_duration_ms != NULL)
        *out_duration_ms =
            session->collect_mailbox.recording_duration_us / UINT64_C(1000);
    return true;
}

static vxml_status evaluate_recording_shadow_expression(
    const vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    bool staged,
    const vxml_cmeta_expression_row *row,
    vxml_cmeta_value_view *out_value) {
    size_t size = 0u;
    uint64_t duration_ms = UINT64_C(0);
    bool defined = false;
    if (session == NULL || program == NULL || row == NULL ||
        out_value == NULL)
        return VXML_INVALID_ARGUMENT;
    *out_value = (vxml_cmeta_value_view){
        .kind = VXML_CMETA_VALUE_UNDEFINED};

    switch (row->source_kind) {
    case VXML_CMETA_EXPRESSION_LASTRESULT_RECORDING_SIZE:
    case VXML_CMETA_EXPRESSION_LASTRESULT_RECORDING_DURATION:
        if (staged)
            defined = collect_pending_recording_metadata(
                session, program, VXML_CMETA_NO_INDEX,
                &size, &duration_ms);
        if (!defined && session->collect_utterance_result.live) {
            size = session->collect_utterance_result.recording.size;
            duration_ms =
                session->collect_utterance_result.duration_us /
                UINT64_C(1000);
            defined = true;
        }
        break;
    case VXML_CMETA_EXPRESSION_FIELD_RECORDING_SIZE:
    case VXML_CMETA_EXPRESSION_FIELD_RECORDING_DURATION:
        if (row->source_field >= program->field_count ||
            session->field_recording_shadows == NULL ||
            row->source_field >= session->field_recording_shadow_count)
            return VXML_INVALID_STRUCTURE;
        if (staged)
            defined = collect_pending_recording_metadata(
                session, program, row->source_field,
                &size, &duration_ms);
        if (!defined) {
            const vxml_cmeta_field_recording_shadow *shadow =
                &session->field_recording_shadows[row->source_field];
            if (shadow->assigned && shadow->has_recording) {
                size = shadow->size;
                duration_ms = shadow->duration_ms;
                defined = true;
            }
        }
        break;
    case VXML_CMETA_EXPRESSION_GENERIC:
    default:
        return VXML_INVALID_STRUCTURE;
    }

    if (!defined)
        return VXML_OK;

    out_value->kind = VXML_CMETA_VALUE_UINT;
    out_value->data.uint_value =
        row->source_kind ==
                VXML_CMETA_EXPRESSION_LASTRESULT_RECORDING_SIZE ||
            row->source_kind ==
                VXML_CMETA_EXPRESSION_FIELD_RECORDING_SIZE
            ? (uint64_t)size
            : duration_ms;
    return VXML_OK;
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
    if (program->expressions[expression].source_kind !=
            VXML_CMETA_EXPRESSION_GENERIC)
        return evaluate_recording_shadow_expression(
            session, program, staged,
            &program->expressions[expression], out_value);
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


static char *subdialog_snapshot_copy_bytes(
    vxml_cmeta_session_data *session,
    const char *data, size_t size) {
    char *destination;
    if (session == NULL || size == 0u) return NULL;
    if (data == NULL ||
        session->subdialog_snapshot_storage_size >
            session->subdialog_snapshot_storage_capacity ||
        size > session->subdialog_snapshot_storage_capacity -
            session->subdialog_snapshot_storage_size ||
        session->subdialog_snapshot_storage == NULL)
        return NULL;
    destination = session->subdialog_snapshot_storage +
        session->subdialog_snapshot_storage_size;
    memcpy(destination, data, size);
    session->subdialog_snapshot_storage_size += size;
    return destination;
}

static size_t subdialog_scalar_value_bytes(
    vxml_cmeta_value_kind kind) {
    switch (kind) {
    case VXML_CMETA_VALUE_UNDEFINED:
        return 0u;
    case VXML_CMETA_VALUE_BOOL:
        return sizeof(bool);
    case VXML_CMETA_VALUE_SINT:
        return sizeof(int64_t);
    case VXML_CMETA_VALUE_UINT:
        return sizeof(uint64_t);
    case VXML_CMETA_VALUE_FLOAT:
        return sizeof(double);
    case VXML_CMETA_VALUE_STRING:
    default:
        return 0u;
    }
}

static vxml_status build_subdialog_snapshot(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_subdialog_row *subdialog) {
    const vxml_cmeta_form_row *form;
    size_t array_bytes = 0u;
    size_t storage_capacity;
    size_t offset;

    if (session == NULL || program == NULL || subdialog == NULL ||
        subdialog->form >= program->form_count ||
        program->forms == NULL ||
        !range_valid(
            subdialog->first_param, subdialog->param_count,
            program->subdialog_param_count) ||
        (subdialog->param_count != 0u &&
         program->subdialog_params == NULL) ||
        session->max_subdialog_snapshot_bytes == 0u)
        return VXML_INVALID_CONTRACT;

    if (session->subdialog_snapshot_generation ==
            session->subdialog_generation &&
        session->subdialog_generation != UINT64_C(0) &&
        session->subdialog_snapshot_param_count == subdialog->param_count)
        return VXML_OK;

    subdialog_snapshot_destroy(session);
    if (subdialog->param_count == 0u) {
        session->subdialog_snapshot_generation =
            session->subdialog_generation;
        return VXML_OK;
    }

    if (!checked_multiply(
            subdialog->param_count,
            sizeof(vxml_cmeta_subdialog_param_v1),
            &array_bytes) ||
        array_bytes > session->max_subdialog_snapshot_bytes)
        return VXML_LIMIT_EXCEEDED;
    storage_capacity =
        session->max_subdialog_snapshot_bytes - array_bytes;

    session->subdialog_snapshot_params =
        (vxml_cmeta_subdialog_param_v1 *)vxml_calloc(
            subdialog->param_count,
            sizeof(*session->subdialog_snapshot_params));
    if (session->subdialog_snapshot_params == NULL)
        return VXML_ALLOCATION_FAILED;
    if (storage_capacity != 0u) {
        session->subdialog_snapshot_storage =
            (char *)vxml_malloc(storage_capacity);
        if (session->subdialog_snapshot_storage == NULL) {
            subdialog_snapshot_destroy(session);
            return VXML_ALLOCATION_FAILED;
        }
    }
    session->subdialog_snapshot_storage_capacity = storage_capacity;
    session->subdialog_snapshot_param_count = subdialog->param_count;
    form = &program->forms[subdialog->form];

    for (offset = 0u; offset < subdialog->param_count; ++offset) {
        const vxml_cmeta_subdialog_param_row *row =
            &program->subdialog_params[subdialog->first_param + offset];
        vxml_cmeta_subdialog_param_v1 *out =
            &session->subdialog_snapshot_params[offset];
        char *owned_name;

        if (row->subdialog != session->active_subdialog ||
            row->name == NULL || row->name_size == 0u) {
            subdialog_snapshot_destroy(session);
            return VXML_INVALID_STRUCTURE;
        }
        owned_name = subdialog_snapshot_copy_bytes(
            session, row->name, row->name_size);
        if (owned_name == NULL) {
            subdialog_snapshot_destroy(session);
            return VXML_LIMIT_EXCEEDED;
        }
        out->name = (vxml_cmeta_name_view){
            owned_name, row->name_size};
        out->source = row->source;

        if (row->source == VXML_CMETA_SUBDIALOG_PARAM_LITERAL) {
            char *owned_literal = NULL;
            if (row->expression != VXML_CMETA_NO_INDEX ||
                row->literal == NULL ||
                row->literal_size >
                    program->max_subdialog_param_value_bytes) {
                subdialog_snapshot_destroy(session);
                return VXML_INVALID_STRUCTURE;
            }
            if (row->literal_size != 0u) {
                owned_literal = subdialog_snapshot_copy_bytes(
                    session, row->literal, row->literal_size);
                if (owned_literal == NULL) {
                    subdialog_snapshot_destroy(session);
                    return VXML_LIMIT_EXCEEDED;
                }
            }
            out->literal = (vxml_cmeta_name_view){
                owned_literal, row->literal_size};
            continue;
        }

        if (row->source == VXML_CMETA_SUBDIALOG_PARAM_TYPED) {
            const size_t scopes[2] = {
                form->scope, program->document_scope};
            vxml_cmeta_value_view value = {0};
            vxml_status status;
            if (row->expression == VXML_CMETA_NO_INDEX ||
                row->expression >= program->expression_count) {
                subdialog_snapshot_destroy(session);
                return VXML_INVALID_STRUCTURE;
            }
            status = evaluate_expression(
                session, program, false, row->expression,
                scopes, 2u, &value);
            if (status != VXML_OK) {
                subdialog_snapshot_destroy(session);
                return status;
            }
            if (value.kind == VXML_CMETA_VALUE_UNDEFINED) {
                subdialog_snapshot_destroy(session);
                return VXML_SEMANTIC_ERROR;
            }
            if (value.kind == VXML_CMETA_VALUE_STRING) {
                char *owned_string = NULL;
                if ((value.data.string.size != 0u &&
                     value.data.string.data == NULL) ||
                    value.data.string.size >
                        program->max_subdialog_param_value_bytes) {
                    subdialog_snapshot_destroy(session);
                    return value.data.string.size >
                            program->max_subdialog_param_value_bytes
                        ? VXML_LIMIT_EXCEEDED
                        : VXML_INVALID_STRUCTURE;
                }
                if (value.data.string.size != 0u) {
                    owned_string = subdialog_snapshot_copy_bytes(
                        session,
                        value.data.string.data,
                        value.data.string.size);
                    if (owned_string == NULL) {
                        subdialog_snapshot_destroy(session);
                        return VXML_LIMIT_EXCEEDED;
                    }
                }
                value.data.string.data = owned_string;
            } else {
                const size_t scalar_bytes =
                    subdialog_scalar_value_bytes(value.kind);
                if (value.kind != VXML_CMETA_VALUE_UNDEFINED &&
                    scalar_bytes == 0u) {
                    subdialog_snapshot_destroy(session);
                    return VXML_INVALID_STRUCTURE;
                }
                if (scalar_bytes >
                    program->max_subdialog_param_value_bytes) {
                    subdialog_snapshot_destroy(session);
                    return VXML_LIMIT_EXCEEDED;
                }
            }
            out->value = value;
            continue;
        }

        subdialog_snapshot_destroy(session);
        return VXML_INVALID_STRUCTURE;
    }
    session->subdialog_snapshot_generation =
        session->subdialog_generation;
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
    size_t initial_offset;
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

    if (!range_valid(
            form->first_initial, form->initial_count,
            program->initial_count) ||
        (form->initial_count != 0u && program->initials == NULL))
        return VXML_INVALID_STRUCTURE;
    for (initial_offset = 0u;
         initial_offset < form->initial_count;
         ++initial_offset) {
        const vxml_cmeta_initial_row *initial =
            &program->initials[form->first_initial + initial_offset];
        unsigned char *form_declared;
        if (initial->form != form_index ||
            initial->form_item_slot >=
                program->scopes[form->scope].schema.slot_count)
            return VXML_INVALID_STRUCTURE;
        form_declared = session_declared(
            session, program, true, form->scope);
        if (form_declared == NULL) return VXML_INVALID_STRUCTURE;
        form_declared[initial->form_item_slot] = 1u;
        if (initial->initial_expression != VXML_CMETA_NO_INDEX) {
            vxml_cmeta_value_view value;
            status = evaluate_expression(
                session, program, true,
                initial->initial_expression,
                form_scopes, 2u, &value);
            if (status != VXML_OK) return status;
            status = assign_scope_slot(
                session, program, true, form->scope,
                initial->form_item_slot, &value);
            if (status != VXML_OK) return status;
        }
    }

    if (!range_valid(
            form->first_record, form->record_count,
            program->record_count) ||
        (form->record_count != 0u && program->records == NULL))
        return VXML_INVALID_STRUCTURE;
    {
        size_t record_offset;
        for (record_offset = 0u;
             record_offset < form->record_count;
             ++record_offset) {
            const vxml_cmeta_record_row *record =
                &program->records[
                    form->first_record + record_offset];
            unsigned char *form_declared;
            if (record->form != form_index ||
                record->form_item_slot >=
                    program->scopes[form->scope].schema.slot_count)
                return VXML_INVALID_STRUCTURE;
            form_declared = session_declared(
                session, program, true, form->scope);
            if (form_declared == NULL)
                return VXML_INVALID_STRUCTURE;
            form_declared[record->form_item_slot] = 1u;
        }
    }

    if (!range_valid(
            form->first_transfer, form->transfer_count,
            program->transfer_count) ||
        (form->transfer_count != 0u && program->transfers == NULL))
        return VXML_INVALID_STRUCTURE;
    {
        size_t transfer_offset;
        for (transfer_offset = 0u;
             transfer_offset < form->transfer_count;
             ++transfer_offset) {
            const vxml_cmeta_transfer_row *transfer =
                &program->transfers[
                    form->first_transfer + transfer_offset];
            unsigned char *form_declared;
            if (transfer->form != form_index ||
                transfer->form_item_slot >=
                    program->scopes[form->scope].schema.slot_count)
                return VXML_INVALID_STRUCTURE;
            form_declared = session_declared(
                session, program, true, form->scope);
            if (form_declared == NULL)
                return VXML_INVALID_STRUCTURE;
            form_declared[transfer->form_item_slot] = 1u;
        }
    }

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

static void mark_form_initial_retry_reset_by_slot(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    size_t scope,
    size_t slot) {
    size_t offset;
    if (session == NULL || program == NULL || form == NULL ||
        scope != form->scope ||
        !range_valid(
            form->first_initial, form->initial_count,
            program->initial_count) ||
        (form->initial_count != 0u && program->initials == NULL))
        return;
    for (offset = 0u; offset < form->initial_count; ++offset) {
        const size_t initial_index = form->first_initial + offset;
        const vxml_cmeta_initial_row *initial =
            &program->initials[initial_index];
        if (initial->form == session->active_form &&
            initial->form_item_slot == slot) {
            mark_initial_retry_reset(
                session, program, initial_index);
            return;
        }
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
                         program->field_count) ||
            !range_valid(form->first_initial, form->initial_count,
                         program->initial_count) ||
            !range_valid(form->first_subdialog, form->subdialog_count,
                         program->subdialog_count) ||
            !range_valid(form->first_record, form->record_count,
                         program->record_count) ||
            !range_valid(form->first_transfer, form->transfer_count,
                         program->transfer_count) ||
            (form->field_count != 0u && program->fields == NULL) ||
            (form->initial_count != 0u && program->initials == NULL) ||
            (form->subdialog_count != 0u && program->subdialogs == NULL) ||
            (form->record_count != 0u && program->records == NULL) ||
            (form->transfer_count != 0u && program->transfers == NULL))
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
        for (index = 0u; index < form->initial_count; ++index) {
            const vxml_cmeta_initial_row *initial =
                &program->initials[form->first_initial + index];
            if (initial->form != session->active_form ||
                initial->form_item_slot >=
                    program->scopes[form->scope].schema.slot_count)
                return VXML_INVALID_STRUCTURE;
            cmeta_scope_view_clear_slot(
                &session->staged_scopes[form->scope].view,
                initial->form_item_slot);
            mark_initial_retry_reset(
                session, program, form->first_initial + index);
        }
        for (index = 0u; index < form->subdialog_count; ++index) {
            const vxml_cmeta_subdialog_row *subdialog =
                &program->subdialogs[form->first_subdialog + index];
            if (subdialog->form != session->active_form)
                return VXML_INVALID_STRUCTURE;
            root_storage_clear_field(
                &session->staged_root, program, subdialog->root_field);
        }
        for (index = 0u; index < form->record_count; ++index) {
            const vxml_cmeta_record_row *record =
                &program->records[form->first_record + index];
            if (record->form != session->active_form ||
                record->form_item_slot >=
                    program->scopes[form->scope].schema.slot_count)
                return VXML_INVALID_STRUCTURE;
            cmeta_scope_view_clear_slot(
                &session->staged_scopes[form->scope].view,
                record->form_item_slot);
        }
        for (index = 0u; index < form->transfer_count; ++index) {
            const vxml_cmeta_transfer_row *transfer =
                &program->transfers[form->first_transfer + index];
            if (transfer->form != session->active_form ||
                transfer->form_item_slot >=
                    program->scopes[form->scope].schema.slot_count)
                return VXML_INVALID_STRUCTURE;
            cmeta_scope_view_clear_slot(
                &session->staged_scopes[form->scope].view,
                transfer->form_item_slot);
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
            mark_form_initial_retry_reset_by_slot(
                session, program, form,
                resolved.candidate->scope,
                resolved.candidate->location.slot);
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
    session->pending_terminal_kind = VXML_CMETA_TERMINAL_EXIT;
    session->pending_terminal_event = NULL;
    session->pending_terminal_event_size = 0u;
    session->exit_requested = true;
    return VXML_OK;
}

static vxml_status execute_return(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program,
    const size_t *scopes, size_t scope_count,
    const vxml_cmeta_action_row *action) {
    size_t index;
    (void)scopes;
    (void)scope_count;
    if (action->event_name != NULL || action->event_name_size != 0u) {
        if (action->event_name == NULL || action->event_name_size == 0u ||
            action->exit_kind != VXML_CMETA_EXIT_EMPTY)
            return VXML_INVALID_STRUCTURE;
        session->pending_exit.kind = VXML_CMETA_EXIT_EMPTY;
        session->pending_terminal_kind = VXML_CMETA_TERMINAL_RETURN_EVENT;
        session->pending_terminal_event = action->event_name;
        session->pending_terminal_event_size = action->event_name_size;
        session->exit_requested = true;
        return VXML_OK;
    }
    if (action->exit_kind != VXML_CMETA_EXIT_NAMELIST ||
        !range_valid(
            action->first_location, action->location_count,
            program->location_count) ||
        action->location_count == 0u ||
        program->locations == NULL)
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
    session->pending_exit.kind = VXML_CMETA_EXIT_NAMELIST;
    session->pending_terminal_kind = VXML_CMETA_TERMINAL_RETURN;
    session->pending_terminal_event = NULL;
    session->pending_terminal_event_size = 0u;
    session->exit_requested = true;
    return VXML_OK;
}

static vxml_status execute_disconnect(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_action_row *action) {
    if (session == NULL || action == NULL ||
        action->exit_kind != VXML_CMETA_EXIT_EMPTY ||
        action->event_name != NULL || action->event_name_size != 0u ||
        action->location_count != 0u)
        return VXML_INVALID_STRUCTURE;
    session->pending_exit.kind = VXML_CMETA_EXIT_EMPTY;
    session->pending_terminal_kind = VXML_CMETA_TERMINAL_DISCONNECT;
    session->pending_terminal_event = NULL;
    session->pending_terminal_event_size = 0u;
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

static vxml_status execute_goto(
    vxml_cmeta_session_data *session,
    const vxml_cmeta_action_row *action) {
    if (session == NULL || action == NULL ||
        action->kind != VXML_CMETA_ACTION_GOTO ||
        action->navigation_uri == NULL ||
        action->navigation_uri_size == 0u ||
        memchr(
            action->navigation_uri, '\0',
            action->navigation_uri_size) != NULL ||
        session->pending_navigation_uri != NULL)
        return VXML_INVALID_STRUCTURE;
    session->pending_navigation_uri =
        action->navigation_uri;
    session->pending_navigation_uri_size =
        action->navigation_uri_size;
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
            case VXML_CMETA_ACTION_RETURN: {
                const vxml_status status = execute_return(
                    session, program, scopes, scope_count, action);
                if (status != VXML_OK) return status;
                return VXML_OK;
            }
            case VXML_CMETA_ACTION_DISCONNECT: {
                const vxml_status status =
                    execute_disconnect(session, action);
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
            case VXML_CMETA_ACTION_GOTO: {
                const vxml_status status =
                    execute_goto(session, action);
                if (status != VXML_OK) return status;
                return VXML_OK;
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
        &profile->subdialog_mailbox.state,
        VXML_CMETA_SUBDIALOG_MAILBOX_DISARMED);
    atomic_init(
        &profile->subdialog_mailbox.generation, UINT64_C(0));
    atomic_init(
        &profile->collect_mailbox.state,
        VXML_CMETA_COLLECT_MAILBOX_DISARMED);
    atomic_init(&profile->collect_mailbox.generation, UINT64_C(0));
    atomic_init(
        &profile->prompt_media_mailbox.state,
        VXML_CMETA_PROMPT_MEDIA_MAILBOX_DISARMED);
    atomic_init(
        &profile->prompt_media_mailbox.generation, UINT64_C(0));
    atomic_init(
        &profile->record_mailbox.state,
        VXML_CMETA_RECORD_MAILBOX_DISARMED);
    atomic_init(
        &profile->record_mailbox.generation, UINT64_C(0));
    profile->prompt_media_last_mark_segment = SIZE_MAX;
    profile->active_form = VXML_CMETA_NO_INDEX;
    profile->active_field = VXML_CMETA_NO_INDEX;
    profile->active_initial = VXML_CMETA_NO_INDEX;
    profile->active_subdialog = VXML_CMETA_NO_INDEX;
    profile->active_record = VXML_CMETA_NO_INDEX;
    profile->active_transfer = VXML_CMETA_NO_INDEX;
    profile->active_menu = VXML_CMETA_NO_INDEX;
    profile->active_block = VXML_CMETA_NO_INDEX;
    profile->collect_mailbox.item_kind = VXML_CMETA_COLLECT_ITEM_FIELD;
    profile->collect_mailbox.choice_index = SIZE_MAX;
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
    if (program->prompt_mark_expr_count != 0u) {
        size_t index;
        size_t max_segments = 0u;
        size_t max_dynamic_marks = 0u;
        size_t dynamic_storage_bytes;
        if (program->prompt_mark_exprs == NULL ||
            program->max_dynamic_mark_name_bytes == 0u)
            goto failure;
        for (index = 0u; index < program->prompt_count; ++index) {
            const vxml_cmeta_prompt_row *prompt =
                &program->prompts[index];
            if (prompt->segment_count > max_segments)
                max_segments = prompt->segment_count;
            if (prompt->dynamic_mark_count > max_dynamic_marks)
                max_dynamic_marks = prompt->dynamic_mark_count;
        }
        if (max_segments == 0u || max_dynamic_marks == 0u ||
            !checked_multiply(
                max_dynamic_marks,
                program->max_dynamic_mark_name_bytes,
                &dynamic_storage_bytes)) {
            status = VXML_INVALID_CONTRACT;
            goto failure;
        }
        profile->prompt_media_projected_segments =
            (vxml_cmeta_prompt_media_segment_v1 *)vxml_calloc(
                max_segments,
                sizeof(*profile->prompt_media_projected_segments));
        profile->prompt_media_dynamic_mark_storage =
            (char *)vxml_malloc(dynamic_storage_bytes);
        profile->prompt_media_last_mark_name =
            (char *)vxml_malloc(
                program->max_dynamic_mark_name_bytes);
        if (profile->prompt_media_projected_segments == NULL ||
            profile->prompt_media_dynamic_mark_storage == NULL ||
            profile->prompt_media_last_mark_name == NULL) {
            status = VXML_ALLOCATION_FAILED;
            goto failure;
        }
        profile->prompt_media_projected_segment_capacity =
            max_segments;
        profile->prompt_media_dynamic_mark_storage_capacity =
            dynamic_storage_bytes;
        profile->prompt_media_last_mark_name_capacity =
            program->max_dynamic_mark_name_bytes;
    }

    if (program->max_mark_result_name_bytes != 0u) {
        size_t field_name_bytes = 0u;
        size_t index;
        profile->mark_result_name_stride =
            program->max_mark_result_name_bytes;
        profile->pending_mark_result_name =
            (char *)vxml_malloc(program->max_mark_result_name_bytes);
        profile->collect_mark_result_name =
            (char *)vxml_malloc(program->max_mark_result_name_bytes);
        if (profile->pending_mark_result_name == NULL ||
            profile->collect_mark_result_name == NULL) {
            status = VXML_ALLOCATION_FAILED;
            goto failure;
        }
        profile->pending_mark_result.name =
            profile->pending_mark_result_name;
        profile->collect_mark_result.name =
            profile->collect_mark_result_name;

        if (program->field_count != 0u) {
            if (!checked_multiply(
                    program->field_count,
                    program->max_mark_result_name_bytes,
                    &field_name_bytes)) {
                status = VXML_LIMIT_EXCEEDED;
                goto failure;
            }
            profile->field_mark_shadows =
                (vxml_cmeta_mark_result_slot *)vxml_calloc(
                    program->field_count,
                    sizeof(*profile->field_mark_shadows));
            profile->field_mark_name_storage =
                (char *)vxml_malloc(field_name_bytes);
            if (profile->field_mark_shadows == NULL ||
                profile->field_mark_name_storage == NULL) {
                status = VXML_ALLOCATION_FAILED;
                goto failure;
            }
            profile->field_mark_shadow_count =
                program->field_count;
            for (index = 0u; index < program->field_count; ++index)
                profile->field_mark_shadows[index].name =
                    profile->field_mark_name_storage +
                    index * profile->mark_result_name_stride;
        }
    }

    {
        const size_t subdialog_tail =
            offsetof(
                vxml_cmeta_session_options_v1,
                max_subdialog_snapshot_bytes) +
            sizeof(options->max_subdialog_snapshot_bytes);
        if (options->struct_size >= subdialog_tail &&
            options->subdialog != NULL) {
            if (!session_subdialog_options_valid(options)) {
                status = VXML_INVALID_CONTRACT;
                goto failure;
            }
            profile->subdialog_adapter = options->subdialog;
            profile->subdialog_user = options->subdialog_user;
            profile->max_subdialog_snapshot_bytes =
                options->max_subdialog_snapshot_bytes;
        }
    }
    if (program->record_count != 0u) {
        size_t index;
        size_t max_media_type_bytes = 0u;
        size_t result_media_bytes;
        if (!session_record_options_valid(options)) {
            status = VXML_INVALID_CONTRACT;
            goto failure;
        }
        for (index = 0u; index < program->record_count; ++index)
            if (program->records[index].max_media_type_bytes >
                    max_media_type_bytes)
                max_media_type_bytes =
                    program->records[index].max_media_type_bytes;
        if (max_media_type_bytes == 0u ||
            !checked_multiply(
                program->record_count,
                max_media_type_bytes,
                &result_media_bytes)) {
            status = VXML_INVALID_CONTRACT;
            goto failure;
        }

        profile->record_adapter = options->record;
        profile->record_user = options->record_user;
        profile->max_record_bytes = options->max_record_bytes;
        profile->record_result_count = program->record_count;
        profile->record_result_media_stride = max_media_type_bytes;
        profile->record_mailbox.media_type_capacity =
            max_media_type_bytes;
        profile->record_mailbox.media_type =
            (char *)vxml_malloc(max_media_type_bytes);
        profile->record_results =
            (vxml_cmeta_record_result_slot *)vxml_calloc(
                program->record_count,
                sizeof(*profile->record_results));
        profile->record_result_media_storage =
            (char *)vxml_malloc(result_media_bytes);
        if (profile->record_mailbox.media_type == NULL ||
            profile->record_results == NULL ||
            profile->record_result_media_storage == NULL) {
            status = VXML_ALLOCATION_FAILED;
            goto failure;
        }
        for (index = 0u; index < program->record_count; ++index)
            profile->record_results[index].media_type =
                profile->record_result_media_storage +
                index * max_media_type_bytes;
    }
    if (program->transfer_count != 0u) {
        if (!session_transfer_options_valid(options)) {
            status = VXML_INVALID_CONTRACT;
            goto failure;
        }
        profile->transfer_adapter = options->transfer;
        profile->transfer_user = options->transfer_user;
    }
    if (session_subdialog_completion_options_present(options)) {
        if (options->max_subdialog_completion_bytes == 0u) {
            status = VXML_INVALID_CONTRACT;
            goto failure;
        }
        profile->subdialog_mailbox.entry_capacity =
            options->max_subdialog_completion_entries;
        profile->subdialog_mailbox.storage_capacity =
            options->max_subdialog_completion_bytes;
        if (profile->subdialog_mailbox.entry_capacity != 0u) {
            profile->subdialog_mailbox.entries =
                (vxml_cmeta_subdialog_result_entry_v1 *)vxml_calloc(
                    profile->subdialog_mailbox.entry_capacity,
                    sizeof(*profile->subdialog_mailbox.entries));
            if (profile->subdialog_mailbox.entries == NULL) {
                status = VXML_ALLOCATION_FAILED;
                goto failure;
            }
        }
        profile->subdialog_mailbox.storage =
            (char *)vxml_malloc(
                profile->subdialog_mailbox.storage_capacity);
        if (profile->subdialog_mailbox.storage == NULL) {
            status = VXML_ALLOCATION_FAILED;
            goto failure;
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
    if (program->field_count != 0u ||
        program->initial_count != 0u ||
        program->menu_count != 0u) {
        if ((program->field_count != 0u && program->fields == NULL) ||
            (program->initial_count != 0u && program->initials == NULL) ||
            (program->menu_count != 0u &&
             (program->menus == NULL ||
              program->menu_choices == NULL ||
              program->menu_choice_targets == NULL)) ||
            !session_collect_options_valid(options)) {
            status = VXML_INVALID_CONTRACT;
            goto failure;
        }
        profile->collect_adapter = options->collect;
        profile->collect_user = options->collect_user;
        if (program->field_count != 0u) {
            profile->retry_reset_pending =
                (unsigned char *)vxml_calloc(
                    program->field_count, sizeof(unsigned char));
            if (profile->retry_reset_pending == NULL) {
                status = VXML_ALLOCATION_FAILED;
                goto failure;
            }
        }
        if (program->initial_count != 0u) {
            profile->initial_retry_reset_pending =
                (unsigned char *)vxml_calloc(
                    program->initial_count, sizeof(unsigned char));
            if (profile->initial_retry_reset_pending == NULL) {
                status = VXML_ALLOCATION_FAILED;
                goto failure;
            }
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
        size_t form_index;
        size_t max_collect_recording_media_type_bytes = 0u;
        bool uses_collect_recording = false;
        const size_t multi_tail_size =
            offsetof(
                vxml_cmeta_session_options_v1,
                max_collect_result_slots) +
            sizeof(options->max_collect_result_slots);
        size_t requested_slots = 1u;
        size_t mailbox_allocation_bytes;
        size_t remainder;

        for (form_index = 0u; form_index < program->form_count; ++form_index) {
            const vxml_cmeta_form_row *form = &program->forms[form_index];
            if (!form->record_utterance) continue;
            uses_collect_recording = true;
            if (form->max_recording_media_type_bytes >
                    max_collect_recording_media_type_bytes)
                max_collect_recording_media_type_bytes =
                    form->max_recording_media_type_bytes;
        }
        if (uses_collect_recording) {
            const size_t recording_tail_size =
                offsetof(
                    vxml_cmeta_session_options_v1,
                    max_collect_recording_bytes) +
                sizeof(options->max_collect_recording_bytes);
            if (options->struct_size < recording_tail_size ||
                options->max_collect_recording_bytes == 0u ||
                max_collect_recording_media_type_bytes == 0u ||
                !collect_adapter_has_field_v2(profile->collect_adapter)) {
                status = VXML_INVALID_CONTRACT;
                goto failure;
            }
            profile->max_collect_recording_bytes =
                options->max_collect_recording_bytes;
            profile->collect_mailbox.recording_media_type_capacity =
                max_collect_recording_media_type_bytes;
            profile->collect_mailbox.recording_media_type =
                (char *)vxml_malloc(
                    max_collect_recording_media_type_bytes);
            profile->collect_utterance_result_media_type =
                (char *)vxml_malloc(
                    max_collect_recording_media_type_bytes);
            if (profile->collect_mailbox.recording_media_type == NULL ||
                profile->collect_utterance_result_media_type == NULL) {
                status = VXML_ALLOCATION_FAILED;
                goto failure;
            }
            profile->collect_utterance_result.media_type =
                profile->collect_utterance_result_media_type;
            profile->collect_utterance_result.media_type_capacity =
                max_collect_recording_media_type_bytes;
            profile->field_recording_shadows =
                (vxml_cmeta_field_recording_shadow *)vxml_calloc(
                    program->field_count,
                    sizeof(*profile->field_recording_shadows));
            if (profile->field_recording_shadows == NULL) {
                status = VXML_ALLOCATION_FAILED;
                goto failure;
            }
            profile->field_recording_shadow_count =
                program->field_count;
        }

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

static bool form_has_bound_input(
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_session_data *profile,
    const vxml_cmeta_form_row *form) {
    size_t offset;
    if (program == NULL || profile == NULL || form == NULL ||
        !range_valid(form->first_field, form->field_count,
                     program->field_count) ||
        (form->field_count != 0u && program->fields == NULL))
        return true;
    for (offset = 0u; offset < form->field_count; ++offset) {
        const vxml_cmeta_field_row *field =
            &program->fields[form->first_field + offset];
        if (field->root_field >=
            session_root_shape(program)->field_count)
            return true;
        if (profile->committed_root.bound[field->root_field] != 0u)
            return true;
    }
    return false;
}

static vxml_status select_directed_item(
    vxml_session_impl *session,
    const vxml_cmeta_program_data *program,
    vxml_cmeta_session_data *profile,
    const vxml_cmeta_form_row *form,
    size_t form_index) {
    const cmeta_data_struct_shape *root_shape =
        session_root_shape(program);
    const cmeta_scope_storage *form_scope;
    size_t item_offset;
    vxml_status status;

    settle_prompt_media(profile);
    profile->active_field = VXML_CMETA_NO_INDEX;
    profile->active_initial = VXML_CMETA_NO_INDEX;
    profile->active_subdialog = VXML_CMETA_NO_INDEX;
    profile->active_record = VXML_CMETA_NO_INDEX;
    profile->active_transfer = VXML_CMETA_NO_INDEX;
    profile->active_menu = VXML_CMETA_NO_INDEX;
    profile->active_block = VXML_CMETA_NO_INDEX;

    if (root_shape == NULL ||
        (root_shape->field_count != 0u &&
         profile->committed_root.bound == NULL) ||
        form->scope >= program->scope_count ||
        profile->committed_scopes == NULL ||
        !range_valid(
            form->first_item, form->item_count,
            program->form_item_count) ||
        (form->item_count != 0u && program->form_items == NULL))
        return session_fail(session, VXML_INVALID_STRUCTURE);
    form_scope = &profile->committed_scopes[form->scope];

    for (item_offset = 0u;
         item_offset < form->item_count;
         ++item_offset) {
        const vxml_cmeta_form_item_row *item =
            &program->form_items[form->first_item + item_offset];
        bool eligible = true;

        if (item->kind == VXML_CMETA_FORM_ITEM_FIELD) {
            const vxml_cmeta_field_row *field;
            if (item->index >= program->field_count ||
                program->fields == NULL)
                return session_fail(session, VXML_INVALID_STRUCTURE);
            field = &program->fields[item->index];
            if (field->form != form_index ||
                item->index < form->first_field ||
                item->index - form->first_field >= form->field_count ||
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
            profile->active_field = item->index;
        } else if (item->kind == VXML_CMETA_FORM_ITEM_INITIAL) {
            const vxml_cmeta_initial_row *initial;
            if (item->index >= program->initial_count ||
                program->initials == NULL)
                return session_fail(session, VXML_INVALID_STRUCTURE);
            initial = &program->initials[item->index];
            if (initial->form != form_index ||
                item->index < form->first_initial ||
                item->index - form->first_initial >=
                    form->initial_count ||
                initial->form_item_slot >=
                    program->scopes[form->scope].schema.slot_count ||
                form_scope->view.bound == NULL)
                return session_fail(session, VXML_INVALID_STRUCTURE);
            if (form_scope->view.bound[initial->form_item_slot] != 0u)
                continue;
            if (initial->condition != VXML_CMETA_NO_INDEX) {
                const size_t scopes[2] = {
                    form->scope, program->document_scope};
                status = evaluate_condition(
                    profile, program, false,
                    initial->condition, scopes, 2u, &eligible);
                if (status != VXML_OK)
                    return session_fail(session, status);
            } else {
                eligible = !form_has_bound_input(
                    program, profile, form);
            }
            if (!eligible) continue;
            if (form->grammar_type == NULL ||
                form->grammar_type_size == 0u ||
                form->grammar_src == NULL ||
                form->grammar_src_size == 0u ||
                form->grammar_required_capabilities == 0u)
                return session_fail(session, VXML_INVALID_STRUCTURE);
            profile->active_initial = item->index;
        } else if (item->kind == VXML_CMETA_FORM_ITEM_SUBDIALOG) {
            const vxml_cmeta_subdialog_row *subdialog;
            if (item->index >= program->subdialog_count ||
                program->subdialogs == NULL)
                return session_fail(session, VXML_INVALID_STRUCTURE);
            subdialog = &program->subdialogs[item->index];
            if (subdialog->form != form_index ||
                item->index < form->first_subdialog ||
                item->index - form->first_subdialog >=
                    form->subdialog_count ||
                subdialog->root_field >= root_shape->field_count ||
                root_shape->fields[subdialog->root_field].value !=
                    subdialog->result_data ||
                root_shape->fields[subdialog->root_field].offset !=
                    subdialog->field_offset ||
                subdialog->result_data == NULL ||
                subdialog->result_data->kind != CMETA_DATA_STRUCT ||
                subdialog->src == NULL || subdialog->src_size == 0u)
                return session_fail(session, VXML_INVALID_STRUCTURE);
            if (profile->committed_root.bound[subdialog->root_field] != 0u)
                continue;
            if (subdialog->condition != VXML_CMETA_NO_INDEX) {
                const size_t scopes[2] = {
                    form->scope, program->document_scope};
                status = evaluate_condition(
                    profile, program, false,
                    subdialog->condition, scopes, 2u, &eligible);
                if (status != VXML_OK)
                    return session_fail(session, status);
            }
            if (!eligible) continue;
            profile->active_subdialog = item->index;
            ++profile->subdialog_generation;
            if (profile->subdialog_generation == 0u)
                profile->subdialog_generation = 1u;
            return VXML_OK;
        } else if (item->kind == VXML_CMETA_FORM_ITEM_RECORD) {
            const vxml_cmeta_record_row *record;
            if (item->index >= program->record_count ||
                program->records == NULL)
                return session_fail(session, VXML_INVALID_STRUCTURE);
            record = &program->records[item->index];
            if (record->form != form_index ||
                item->index < form->first_record ||
                item->index - form->first_record >= form->record_count ||
                form->scope >= program->scope_count ||
                record->form_item_slot >=
                    program->scopes[form->scope].schema.slot_count ||
                form_scope->view.bound == NULL ||
                record->name == NULL || record->name_size == 0u)
                return session_fail(session, VXML_INVALID_STRUCTURE);
            if (form_scope->view.bound[record->form_item_slot] != 0u)
                continue;
            if (record->condition != VXML_CMETA_NO_INDEX) {
                const size_t scopes[2] = {
                    form->scope, program->document_scope};
                status = evaluate_condition(
                    profile, program, false,
                    record->condition, scopes, 2u, &eligible);
                if (status != VXML_OK)
                    return session_fail(session, status);
            }
            if (!eligible) continue;
            profile->active_record = item->index;
            ++profile->record_generation;
            if (profile->record_generation == 0u)
                profile->record_generation = 1u;
            profile->record_quiesced_generation = UINT64_C(0);
            return VXML_OK;
        } else if (item->kind == VXML_CMETA_FORM_ITEM_TRANSFER) {
            const vxml_cmeta_transfer_row *transfer;
            if (item->index >= program->transfer_count ||
                program->transfers == NULL)
                return session_fail(session, VXML_INVALID_STRUCTURE);
            transfer = &program->transfers[item->index];
            if (transfer->form != form_index ||
                item->index < form->first_transfer ||
                item->index - form->first_transfer >=
                    form->transfer_count ||
                form->scope >= program->scope_count ||
                transfer->form_item_slot >=
                    program->scopes[form->scope].schema.slot_count ||
                form_scope->view.bound == NULL ||
                transfer->name == NULL || transfer->name_size == 0u ||
                transfer->destination == NULL ||
                transfer->destination_size == 0u)
                return session_fail(session, VXML_INVALID_STRUCTURE);
            if (form_scope->view.bound[transfer->form_item_slot] != 0u)
                continue;
            if (transfer->condition != VXML_CMETA_NO_INDEX) {
                const size_t scopes[2] = {
                    form->scope, program->document_scope};
                status = evaluate_condition(
                    profile, program, false,
                    transfer->condition, scopes, 2u, &eligible);
                if (status != VXML_OK)
                    return session_fail(session, status);
            }
            if (!eligible) continue;
            profile->active_transfer = item->index;
            ++profile->transfer_generation;
            if (profile->transfer_generation == UINT64_C(0))
                profile->transfer_generation = UINT64_C(1);
            profile->transfer_quiesced_generation = UINT64_C(0);
            return VXML_OK;
        } else {
            return session_fail(session, VXML_INVALID_STRUCTURE);
        }

        ++profile->collect_generation;
        if (profile->collect_generation == 0u)
            profile->collect_generation = 1u;
        return VXML_OK;
    }

    session->state = VXML_SESSION_EXITED;
    session->error = VXML_OK;
    return VXML_OK;
}

static vxml_status select_menu(
    vxml_session_impl *session,
    const vxml_cmeta_program_data *program,
    vxml_cmeta_session_data *profile,
    const vxml_cmeta_form_row *form,
    size_t form_index) {
    const vxml_cmeta_menu_row *menu;
    if (session == NULL || program == NULL || profile == NULL ||
        form == NULL || form->menu == VXML_CMETA_NO_INDEX ||
        form->menu >= program->menu_count || program->menus == NULL)
        return session_fail(session, VXML_INVALID_STRUCTURE);
    menu = &program->menus[form->menu];
    if (menu->form != form_index || menu->choice_count == 0u ||
        !range_valid(
            menu->first_choice, menu->choice_count,
            program->menu_choice_count) ||
        program->menu_choices == NULL ||
        program->menu_choice_targets == NULL)
        return session_fail(session, VXML_INVALID_STRUCTURE);
    settle_prompt_media(profile);
    profile->active_field = VXML_CMETA_NO_INDEX;
    profile->active_initial = VXML_CMETA_NO_INDEX;
    profile->active_subdialog = VXML_CMETA_NO_INDEX;
    profile->active_record = VXML_CMETA_NO_INDEX;
    profile->active_transfer = VXML_CMETA_NO_INDEX;
    profile->active_menu = form->menu;
    profile->active_block = VXML_CMETA_NO_INDEX;
    ++profile->collect_generation;
    if (profile->collect_generation == 0u)
        profile->collect_generation = 1u;
    return VXML_OK;
}

static const vxml_cmeta_declaration_row *child_form_parameter(
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    vxml_cmeta_name_view name) {
    size_t offset;
    if (program == NULL || form == NULL ||
        name.data == NULL || name.size == 0u ||
        memchr(name.data, '\0', name.size) != NULL ||
        !range_valid(
            form->first_declaration, form->declaration_count,
            program->declaration_count) ||
        (form->declaration_count != 0u &&
         program->declarations == NULL))
        return NULL;
    for (offset = 0u; offset < form->declaration_count; ++offset) {
        const vxml_cmeta_declaration_row *row =
            &program->declarations[form->first_declaration + offset];
        if (row->scope == form->scope &&
            row->name != NULL &&
            row->name_size == name.size &&
            memcmp(row->name, name.data, name.size) == 0)
            return row;
    }
    return NULL;
}

static bool child_entry_contains_parameter(
    const vxml_cmeta_child_entry_v1 *entry,
    vxml_cmeta_name_view name) {
    size_t index;
    if (entry == NULL ||
        (entry->param_count != 0u && entry->params == NULL))
        return false;
    for (index = 0u; index < entry->param_count; ++index) {
        const vxml_cmeta_name_view candidate = entry->params[index].name;
        if (candidate.data != NULL &&
            candidate.size == name.size &&
            memcmp(candidate.data, name.data, name.size) == 0)
            return true;
    }
    return false;
}

static vxml_status child_literal_value(
    const cmeta_data_desc *target,
    vxml_cmeta_name_view literal,
    size_t max_literal_bytes,
    vxml_cmeta_value_view *out) {
    char *text = NULL;
    char *end = NULL;
    vxml_status status = VXML_SEMANTIC_ERROR;

    if (out == NULL || !cmeta_data_desc_valid(target) ||
        target->storage_type == NULL ||
        (literal.size != 0u &&
         (literal.data == NULL ||
          memchr(literal.data, '\0', literal.size) != NULL)) ||
        max_literal_bytes == 0u ||
        literal.size > max_literal_bytes)
        return literal.size > max_literal_bytes
            ? VXML_LIMIT_EXCEEDED : VXML_SEMANTIC_ERROR;

    *out = (vxml_cmeta_value_view){0};
    if (target->kind == CMETA_DATA_STRING) {
        out->kind = VXML_CMETA_VALUE_STRING;
        out->data.string.data = literal.data;
        out->data.string.size = literal.size;
        return VXML_OK;
    }
    if (literal.size == 0u || literal.size == SIZE_MAX)
        return VXML_SEMANTIC_ERROR;

    text = (char *)vxml_malloc(literal.size + 1u);
    if (text == NULL)
        return VXML_ALLOCATION_FAILED;
    memcpy(text, literal.data, literal.size);
    text[literal.size] = '\0';

    errno = 0;
    switch (target->kind) {
    case CMETA_DATA_BOOL:
        if (literal.size == sizeof("true") - 1u &&
            memcmp(text, "true", sizeof("true") - 1u) == 0) {
            out->kind = VXML_CMETA_VALUE_BOOL;
            out->data.boolean = true;
            status = VXML_OK;
        } else if (literal.size == sizeof("false") - 1u &&
                   memcmp(text, "false", sizeof("false") - 1u) == 0) {
            out->kind = VXML_CMETA_VALUE_BOOL;
            out->data.boolean = false;
            status = VXML_OK;
        }
        break;
    case CMETA_DATA_SINT: {
        const long long value = strtoll(text, &end, 10);
        if (errno != ERANGE && end != text && *end == '\0') {
            out->kind = VXML_CMETA_VALUE_SINT;
            out->data.sint = (int64_t)value;
            status = VXML_OK;
        }
        break;
    }
    case CMETA_DATA_UINT: {
        unsigned long long value;
        if (text[0] == '-')
            break;
        value = strtoull(text, &end, 10);
        if (errno != ERANGE && end != text && *end == '\0') {
            out->kind = VXML_CMETA_VALUE_UINT;
            out->data.uint_value = (uint64_t)value;
            status = VXML_OK;
        }
        break;
    }
    case CMETA_DATA_FLOAT: {
        const double value = strtod(text, &end);
        if (errno != ERANGE && end != text && *end == '\0' &&
            isfinite(value)) {
            out->kind = VXML_CMETA_VALUE_FLOAT;
            out->data.number = value;
            status = VXML_OK;
        }
        break;
    }
    default:
        break;
    }
    vxml_free(text);
    return status;
}

static vxml_status import_child_entry_parameters(
    vxml_cmeta_session_data *profile,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    const vxml_cmeta_child_entry_v1 *entry) {
    const cmeta_scope_schema *schema;
    size_t index;
    size_t declaration_offset;

    if (profile == NULL || program == NULL || form == NULL ||
        entry == NULL ||
        form->scope >= program->scope_count ||
        (entry->param_count != 0u && entry->params == NULL) ||
        entry->param_count > form->declaration_count ||
        !range_valid(
            form->first_declaration, form->declaration_count,
            program->declaration_count) ||
        (form->declaration_count != 0u &&
         program->declarations == NULL))
        return VXML_SEMANTIC_ERROR;
    schema = &program->scopes[form->scope].schema;

    for (index = 0u; index < entry->param_count; ++index) {
        const vxml_cmeta_subdialog_param_v1 *param =
            &entry->params[index];
        const vxml_cmeta_declaration_row *row;
        const cmeta_scope_slot *slot;
        vxml_cmeta_value_view value = {0};
        vxml_status status;
        size_t prior;

        if (param->name.data == NULL || param->name.size == 0u ||
            memchr(param->name.data, '\0', param->name.size) != NULL)
            return VXML_SEMANTIC_ERROR;
        for (prior = 0u; prior < index; ++prior) {
            const vxml_cmeta_name_view previous =
                entry->params[prior].name;
            if (previous.data != NULL &&
                previous.size == param->name.size &&
                memcmp(
                    previous.data, param->name.data,
                    param->name.size) == 0)
                return VXML_SEMANTIC_ERROR;
        }

        row = child_form_parameter(program, form, param->name);
        if (row == NULL || row->slot >= schema->slot_count)
            return VXML_SEMANTIC_ERROR;
        slot = &schema->slots[row->slot];
        if (slot->value == NULL || slot->value->storage_type == NULL)
            return VXML_INVALID_STRUCTURE;

        if (param->source == VXML_CMETA_SUBDIALOG_PARAM_TYPED) {
            if (param->literal.data != NULL ||
                param->literal.size != 0u ||
                param->value.kind == VXML_CMETA_VALUE_UNDEFINED)
                return VXML_SEMANTIC_ERROR;
            value = param->value;
        } else if (param->source ==
                       VXML_CMETA_SUBDIALOG_PARAM_LITERAL) {
            const size_t literal_limit =
                program->max_subdialog_param_value_bytes != 0u
                    ? program->max_subdialog_param_value_bytes
                    : program->max_string_bytes;
            if (param->value.kind != VXML_CMETA_VALUE_UNDEFINED)
                return VXML_SEMANTIC_ERROR;
            status = child_literal_value(
                slot->value, param->literal,
                literal_limit, &value);
            if (status != VXML_OK)
                return status;
        } else {
            return VXML_SEMANTIC_ERROR;
        }

        status = assign_scope_slot(
            profile, program, true,
            form->scope, row->slot, &value);
        if (status != VXML_OK)
            return status;
    }

    for (declaration_offset = 0u;
         declaration_offset < form->declaration_count;
         ++declaration_offset) {
        const vxml_cmeta_declaration_row *row =
            &program->declarations[
                form->first_declaration + declaration_offset];
        const vxml_cmeta_name_view name = {
            row->name, row->name_size};
        if (row->scope != form->scope ||
            row->slot >= schema->slot_count ||
            row->name == NULL || row->name_size == 0u)
            return VXML_INVALID_STRUCTURE;
        if (row->expression == VXML_CMETA_NO_INDEX &&
            !child_entry_contains_parameter(entry, name))
            return VXML_SEMANTIC_ERROR;
    }
    return VXML_OK;
}

static vxml_status cmeta_session_start_profile_at_entry(
    vxml_session_impl *session, size_t form_index,
    const vxml_cmeta_child_entry_v1 *entry) {
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
        !range_valid(form->first_initial, form->initial_count,
                     program->initial_count) ||
        !range_valid(form->first_subdialog, form->subdialog_count,
                     program->subdialog_count) ||
        !range_valid(form->first_record, form->record_count,
                     program->record_count) ||
        !range_valid(form->first_transfer, form->transfer_count,
                     program->transfer_count) ||
        !range_valid(form->first_item, form->item_count,
                     program->form_item_count) ||
        !range_valid(form->first_block, form->block_count,
                     program->block_count) ||
        form->field_count > SIZE_MAX - form->initial_count ||
        form->field_count + form->initial_count >
            SIZE_MAX - form->subdialog_count ||
        form->field_count + form->initial_count +
            form->subdialog_count >
            SIZE_MAX - form->record_count ||
        form->field_count + form->initial_count +
            form->subdialog_count + form->record_count >
            SIZE_MAX - form->transfer_count ||
        (form->item_count !=
             form->field_count + form->initial_count +
             form->subdialog_count + form->record_count +
             form->transfer_count) ||
        ((form->field_count != 0u || form->initial_count != 0u ||
          form->subdialog_count != 0u || form->record_count != 0u ||
          form->transfer_count != 0u) &&
         ((form->field_count != 0u && program->fields == NULL) ||
          (form->initial_count != 0u && program->initials == NULL) ||
          (form->subdialog_count != 0u && program->subdialogs == NULL) ||
          (form->record_count != 0u && program->records == NULL) ||
          (form->transfer_count != 0u && program->transfers == NULL) ||
          program->form_items == NULL ||
          form->block_count != 0u ||
          form->menu != VXML_CMETA_NO_INDEX)) ||
        (form->block_count != 0u &&
         (program->blocks == NULL ||
          form->menu != VXML_CMETA_NO_INDEX)) ||
        (form->menu != VXML_CMETA_NO_INDEX &&
         (form->field_count != 0u || form->initial_count != 0u ||
          form->subdialog_count != 0u || form->record_count != 0u ||
          form->transfer_count != 0u ||
          form->item_count != 0u || form->block_count != 0u ||
          form->menu >= program->menu_count ||
          program->menus == NULL)) ||
        (form->field_count == 0u && form->initial_count == 0u &&
         form->subdialog_count == 0u && form->record_count == 0u &&
         form->transfer_count == 0u &&
         form->block_count == 0u &&
         form->menu == VXML_CMETA_NO_INDEX))
        return session_fail(session, VXML_INVALID_STRUCTURE);
    profile->active_form = form_index;
    reset_form_retry_counters(profile, program, form);
    profile->reprompt_requested = false;
    profile->handler_reprompt_requested = false;
    if (!transaction_begin(profile, program))
        return session_fail(session, VXML_ALLOCATION_FAILED);
    status = initialize_form(profile, program, form, form_index);
    if (status == VXML_OK && entry != NULL)
        status = import_child_entry_parameters(
            profile, program, form, entry);
    if (status != VXML_OK) {
        transaction_reset(profile, program);
        return session_fail(session, status);
    }
    transaction_commit(profile, program);
    if (form->item_count != 0u)
        return select_directed_item(
            session, program, profile, form, form_index);
    if (form->menu != VXML_CMETA_NO_INDEX)
        return select_menu(
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
        terminal_pending_reset(profile);
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
        if (profile->pending_navigation_uri != NULL) {
            status = publish_pending_navigation(
                session, profile);
            exit_snapshot_destroy(&profile->pending_exit);
            return status != VXML_OK
                ? session_fail(session, status) : VXML_OK;
        }
        if (profile->exit_requested) {
            terminal_publish(profile);
            session->state = VXML_SESSION_EXITED;
            session->error = VXML_OK;
            return VXML_OK;
        }
        exit_snapshot_destroy(&profile->pending_exit);
    }
}

vxml_status vxml_cmeta_session_start_profile_at(
    vxml_session_impl *session, size_t form_index) {
    return cmeta_session_start_profile_at_entry(
        session, form_index, NULL);
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


vxml_status vxml_session_cmeta_start_child(
    vxml_session *session,
    const vxml_cmeta_child_entry_v1 *entry) {
    vxml_session_impl *impl;
    size_t form_index = 0u;
    const size_t entry_prefix =
        offsetof(vxml_cmeta_child_entry_v1, param_count) +
        sizeof(((vxml_cmeta_child_entry_v1 *)0)->param_count);

    if (session == NULL || entry == NULL ||
        entry->abi_version != VXML_CMETA_CHILD_ENTRY_ABI_V1 ||
        entry->struct_size < entry_prefix ||
        ((entry->form_id.data == NULL) !=
         (entry->form_id.size == 0u)) ||
        (entry->form_id.size != 0u &&
         memchr(entry->form_id.data, '\0', entry->form_id.size) != NULL) ||
        ((entry->params == NULL) != (entry->param_count == 0u)))
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl == NULL)
        return VXML_INVALID_STATE;
    if (impl->state == VXML_SESSION_CLOSED)
        return VXML_CLOSED;
    if (impl->state != VXML_SESSION_READY)
        return VXML_INVALID_STATE;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->program->profile_data == NULL ||
        impl->profile_data == NULL ||
        impl->program->forms == NULL ||
        impl->program->form_count == 0u)
        return VXML_INVALID_CONTRACT;

    if (entry->form_id.size != 0u) {
        for (form_index = 0u;
             form_index < impl->program->form_count;
             ++form_index) {
            const vxml_form_row *row =
                &impl->program->forms[form_index];
            if (row->id != NULL &&
                row->id_size == entry->form_id.size &&
                memcmp(
                    row->id, entry->form_id.data,
                    entry->form_id.size) == 0)
                break;
        }
        if (form_index == impl->program->form_count) {
            impl->state = VXML_SESSION_RUNNING;
            return session_fail(impl, VXML_SEMANTIC_ERROR);
        }
    }

    impl->state = VXML_SESSION_RUNNING;
    impl->error = VXML_OK;
    impl->navigation_uri = NULL;
    impl->navigation_uri_size = 0u;
    impl->navigation_fetchaudio_uri = NULL;
    impl->navigation_fetchaudio_uri_size = 0u;
    impl->submit_uri = NULL;
    impl->submit_uri_size = 0u;
    impl->submit_method = 0;
    impl->submit_enctype = 0;
    impl->script_src = NULL;
    impl->script_src_size = 0u;
    impl->script_charset = NULL;
    impl->script_charset_size = 0u;

    return cmeta_session_start_profile_at_entry(
        impl, form_index, entry);
}

static const vxml_session_impl *cmeta_session(const vxml_session *session) {
    const vxml_session_impl *impl;
    if (session == NULL || session->impl == NULL) return NULL;
    impl = (const vxml_session_impl *)session->impl;
    return impl->program != NULL &&
            impl->program->profile_kind == VXML_PROFILE_CMETA
        ? impl : NULL;
}


vxml_status vxml_session_cmeta_subdialog_prepare(
    vxml_session *session, const char **out_error) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_subdialog_row *subdialog;
    vxml_cmeta_subdialog_request_v1 request = {0};
    vxml_cmeta_subdialog_ticket_v1 ticket = {0};
    vxml_status status;

    if (out_error != NULL) *out_error = NULL;
    if (session == NULL || session->impl == NULL)
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
    if (profile->active_subdialog == VXML_CMETA_NO_INDEX ||
        profile->active_subdialog >= program->subdialog_count ||
        program->subdialogs == NULL ||
        profile->subdialog_generation == 0u)
        return VXML_INVALID_STATE;
    if (profile->subdialog_adapter == NULL ||
        profile->max_subdialog_snapshot_bytes == 0u)
        return VXML_INVALID_CONTRACT;
    if (profile->subdialog_prepared || profile->subdialog_in_flight)
        return VXML_INVALID_STATE;

    subdialog = &program->subdialogs[profile->active_subdialog];
    if (subdialog->form != profile->active_form ||
        subdialog->src == NULL || subdialog->src_size == 0u ||
        !range_valid(
            subdialog->first_param, subdialog->param_count,
            program->subdialog_param_count))
        return VXML_INVALID_STRUCTURE;

    status = build_subdialog_snapshot(
        profile, program, subdialog);
    if (status != VXML_OK)
        return status;

    request = (vxml_cmeta_subdialog_request_v1){
        .abi_version = VXML_CMETA_SUBDIALOG_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_subdialog_request_v1),
        .generation = profile->subdialog_generation,
        .src = {subdialog->src, subdialog->src_size},
        .params = profile->subdialog_snapshot_param_count != 0u
            ? profile->subdialog_snapshot_params : NULL,
        .param_count = profile->subdialog_snapshot_param_count
    };
    status = profile->subdialog_adapter->prepare(
        profile->subdialog_user, &request, &ticket, out_error);
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
    profile->subdialog_ticket = ticket;
    profile->subdialog_prepared = true;
    return VXML_OK;
}

vxml_status vxml_session_cmeta_subdialog_commit(
    vxml_session *session) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_subdialog_ticket_v1 ticket;
    if (session == NULL || session->impl == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_RUNNING)
        return VXML_INVALID_STATE;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (!profile->subdialog_prepared ||
        profile->subdialog_in_flight ||
        profile->subdialog_ticket.commit == NULL ||
        profile->subdialog_ticket.discard == NULL ||
        profile->active_subdialog == VXML_CMETA_NO_INDEX ||
        profile->subdialog_generation == 0u)
        return VXML_INVALID_STATE;
    ticket = profile->subdialog_ticket;
    if (profile->subdialog_mailbox.storage_capacity != 0u) {
        unsigned state = atomic_load_explicit(
            &profile->subdialog_mailbox.state, memory_order_acquire);
        if (state != VXML_CMETA_SUBDIALOG_MAILBOX_DISARMED)
            return VXML_INVALID_STATE;
        profile->subdialog_mailbox.kind = 0;
        profile->subdialog_mailbox.global_exit_kind =
            VXML_CMETA_SUBDIALOG_GLOBAL_NATURAL;
        profile->subdialog_mailbox.entry_count = 0u;
        profile->subdialog_mailbox.storage_size = 0u;
        profile->subdialog_mailbox.event =
            (vxml_cmeta_name_view){0};
        atomic_store_explicit(
            &profile->subdialog_mailbox.generation,
            profile->subdialog_generation,
            memory_order_relaxed);
        atomic_store_explicit(
            &profile->subdialog_mailbox.state,
            VXML_CMETA_SUBDIALOG_MAILBOX_EMPTY,
            memory_order_release);
    }
    profile->subdialog_ticket = (vxml_cmeta_subdialog_ticket_v1){0};
    profile->subdialog_prepared = false;
    profile->subdialog_in_flight = true;
    ticket.commit(ticket.user);
    return VXML_OK;
}

vxml_status vxml_session_cmeta_subdialog_discard(
    vxml_session *session) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_subdialog_ticket_v1 ticket;
    if (session == NULL || session->impl == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_RUNNING)
        return VXML_INVALID_STATE;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (!profile->subdialog_prepared ||
        profile->subdialog_in_flight ||
        profile->subdialog_ticket.discard == NULL)
        return VXML_INVALID_STATE;
    ticket = profile->subdialog_ticket;
    profile->subdialog_ticket = (vxml_cmeta_subdialog_ticket_v1){0};
    profile->subdialog_prepared = false;
    ticket.discard(ticket.user);
    return VXML_OK;
}


static vxml_status record_request_from_impl(
    const vxml_session_impl *impl,
    vxml_cmeta_record_request_v1 *out_request) {
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_session_data *profile;
    const vxml_cmeta_record_row *record;

    if (out_request == NULL) return VXML_INVALID_ARGUMENT;
    *out_request = (vxml_cmeta_record_request_v1){0};
    if (impl == NULL || impl->state != VXML_SESSION_RUNNING ||
        impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_STATE;

    program = (const vxml_cmeta_program_data *)
        impl->program->profile_data;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    if (profile->active_record == VXML_CMETA_NO_INDEX ||
        profile->active_record >= program->record_count ||
        program->records == NULL ||
        profile->record_generation == UINT64_C(0) ||
        profile->max_record_bytes == 0u)
        return VXML_INVALID_STATE;
    record = &program->records[profile->active_record];
    if (record->form != profile->active_form ||
        record->name == NULL || record->name_size == 0u ||
        record->max_duration_us == UINT64_C(0) ||
        record->max_final_silence_us == UINT64_C(0))
        return VXML_INVALID_STRUCTURE;

    *out_request = (vxml_cmeta_record_request_v1){
        .abi_version = VXML_CMETA_RECORD_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_record_request_v1),
        .generation = profile->record_generation,
        .required_capabilities = record->required_capabilities,
        .name = {record->name, record->name_size},
        .modal = record->modal,
        .beep = record->beep,
        .dtmf_term = record->dtmf_term,
        .has_maxtime = record->has_maxtime,
        .maxtime_us = record->maxtime_us,
        .max_duration_us = record->max_duration_us,
        .has_final_silence = record->has_final_silence,
        .final_silence_us = record->final_silence_us,
        .max_final_silence_us = record->max_final_silence_us,
        .media_type = {
            record->media_type, record->media_type_size},
        .max_bytes = profile->max_record_bytes
    };
    return VXML_OK;
}

vxml_status vxml_session_cmeta_record_request(
    const vxml_session *session,
    vxml_cmeta_record_request_v1 *out_request) {
    if (session == NULL)
        return VXML_INVALID_ARGUMENT;
    return record_request_from_impl(
        (const vxml_session_impl *)session->impl,
        out_request);
}

vxml_status vxml_session_cmeta_record_prepare(
    vxml_session *session, const char **out_error) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_record_request_v1 request = {0};
    vxml_cmeta_record_ticket_v1 ticket = {0};
    vxml_status status;

    if (out_error != NULL) *out_error = NULL;
    if (session == NULL || session->impl == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_RUNNING)
        return VXML_INVALID_STATE;

    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (profile->record_adapter == NULL ||
        profile->active_record == VXML_CMETA_NO_INDEX ||
        profile->record_generation == UINT64_C(0) ||
        profile->record_prepared || profile->record_in_flight)
        return VXML_INVALID_STATE;

    status = record_request_from_impl(impl, &request);
    if (status != VXML_OK) return status;
    if ((profile->record_adapter->capabilities &
         request.required_capabilities) !=
        request.required_capabilities)
        return VXML_UNSUPPORTED_FEATURE;

    status = profile->record_adapter->prepare(
        profile->record_user, &request, &ticket, out_error);
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
    profile->record_ticket = ticket;
    profile->record_prepared = true;
    return VXML_OK;
}

vxml_status vxml_session_cmeta_record_commit(
    vxml_session *session) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_record_ticket_v1 ticket;

    if (session == NULL || session->impl == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_RUNNING)
        return VXML_INVALID_STATE;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (!profile->record_prepared ||
        profile->record_in_flight ||
        profile->record_ticket.commit == NULL ||
        profile->record_ticket.discard == NULL ||
        profile->active_record == VXML_CMETA_NO_INDEX ||
        profile->record_generation == UINT64_C(0))
        return VXML_INVALID_STATE;

    if (atomic_load_explicit(
            &profile->record_mailbox.state,
            memory_order_acquire) !=
            VXML_CMETA_RECORD_MAILBOX_DISARMED)
        return VXML_INVALID_STATE;

    ticket = profile->record_ticket;
    profile->record_ticket = (vxml_cmeta_record_ticket_v1){0};
    profile->record_prepared = false;
    profile->record_in_flight = true;
    profile->record_quiesced_generation = UINT64_C(0);
    record_mailbox_payload_reset(
        &profile->record_mailbox, false);
    atomic_store_explicit(
        &profile->record_mailbox.generation,
        profile->record_generation,
        memory_order_relaxed);
    atomic_store_explicit(
        &profile->record_mailbox.state,
        VXML_CMETA_RECORD_MAILBOX_EMPTY,
        memory_order_release);
    ticket.commit(ticket.user);
    return VXML_OK;
}

vxml_status vxml_session_cmeta_record_discard(
    vxml_session *session) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_record_ticket_v1 ticket;

    if (session == NULL || session->impl == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_RUNNING)
        return VXML_INVALID_STATE;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (!profile->record_prepared ||
        profile->record_in_flight ||
        profile->record_ticket.discard == NULL)
        return VXML_INVALID_STATE;

    ticket = profile->record_ticket;
    profile->record_ticket = (vxml_cmeta_record_ticket_v1){0};
    profile->record_prepared = false;
    ticket.discard(ticket.user);
    return VXML_OK;
}

static vxml_status transfer_request_from_impl(
    const vxml_session_impl *impl,
    vxml_cmeta_transfer_request_v1 *out_request) {
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_session_data *profile;
    const vxml_cmeta_transfer_row *transfer;
    if (out_request != NULL)
        *out_request = (vxml_cmeta_transfer_request_v1){0};
    if (impl == NULL || out_request == NULL)
        return VXML_INVALID_ARGUMENT;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_RUNNING)
        return VXML_INVALID_STATE;
    program = (const vxml_cmeta_program_data *)
        impl->program->profile_data;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    if (profile->active_transfer == VXML_CMETA_NO_INDEX ||
        profile->active_transfer >= program->transfer_count ||
        program->transfers == NULL ||
        profile->transfer_generation == UINT64_C(0))
        return VXML_INVALID_STATE;
    transfer = &program->transfers[profile->active_transfer];
    *out_request = (vxml_cmeta_transfer_request_v1){
        .abi_version = VXML_CMETA_TRANSFER_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_transfer_request_v1),
        .generation = profile->transfer_generation,
        .required_capabilities = transfer->required_capabilities,
        .name = {transfer->name, transfer->name_size},
        .destination = {
            transfer->destination, transfer->destination_size},
        .mode = transfer->mode,
        .has_connect_timeout = transfer->has_connect_timeout,
        .connect_timeout_us = transfer->connect_timeout_us,
        .max_connect_timeout_us = transfer->max_connect_timeout_us,
        .has_maxtime = transfer->has_maxtime,
        .maxtime_us = transfer->maxtime_us,
        .max_duration_us = transfer->max_duration_us,
        .transfer_audio = {
            transfer->transfer_audio, transfer->transfer_audio_size}
    };
    return VXML_OK;
}

vxml_status vxml_session_cmeta_transfer_request(
    const vxml_session *session,
    vxml_cmeta_transfer_request_v1 *out_request) {
    if (session == NULL)
        return VXML_INVALID_ARGUMENT;
    return transfer_request_from_impl(
        (const vxml_session_impl *)session->impl,
        out_request);
}

vxml_status vxml_session_cmeta_transfer_prepare(
    vxml_session *session, const char **out_error) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_transfer_request_v1 request = {0};
    vxml_cmeta_transfer_ticket_v1 ticket = {0};
    vxml_status status;

    if (out_error != NULL) *out_error = NULL;
    if (session == NULL || session->impl == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_RUNNING)
        return VXML_INVALID_STATE;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (profile->transfer_adapter == NULL ||
        profile->active_transfer == VXML_CMETA_NO_INDEX ||
        profile->transfer_generation == UINT64_C(0) ||
        profile->transfer_prepared || profile->transfer_in_flight)
        return VXML_INVALID_STATE;

    status = transfer_request_from_impl(impl, &request);
    if (status != VXML_OK) return status;
    if ((profile->transfer_adapter->capabilities &
         request.required_capabilities) !=
        request.required_capabilities)
        return VXML_UNSUPPORTED_FEATURE;

    status = profile->transfer_adapter->prepare(
        profile->transfer_user, &request, &ticket, out_error);
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
    profile->transfer_ticket = ticket;
    profile->transfer_prepared = true;
    return VXML_OK;
}

vxml_status vxml_session_cmeta_transfer_commit(
    vxml_session *session) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_transfer_ticket_v1 ticket;
    if (session == NULL || session->impl == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_RUNNING)
        return VXML_INVALID_STATE;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (!profile->transfer_prepared ||
        profile->transfer_in_flight ||
        profile->transfer_ticket.commit == NULL ||
        profile->transfer_ticket.discard == NULL ||
        profile->active_transfer == VXML_CMETA_NO_INDEX ||
        profile->transfer_generation == UINT64_C(0))
        return VXML_INVALID_STATE;

    ticket = profile->transfer_ticket;
    profile->transfer_ticket = (vxml_cmeta_transfer_ticket_v1){0};
    profile->transfer_prepared = false;
    profile->transfer_in_flight = true;
    profile->transfer_quiesced_generation = UINT64_C(0);
    ticket.commit(ticket.user);
    return VXML_OK;
}

vxml_status vxml_session_cmeta_transfer_discard(
    vxml_session *session) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_transfer_ticket_v1 ticket;
    if (session == NULL || session->impl == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_RUNNING)
        return VXML_INVALID_STATE;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (!profile->transfer_prepared ||
        profile->transfer_in_flight ||
        profile->transfer_ticket.discard == NULL)
        return VXML_INVALID_STATE;

    ticket = profile->transfer_ticket;
    profile->transfer_ticket = (vxml_cmeta_transfer_ticket_v1){0};
    profile->transfer_prepared = false;
    ticket.discard(ticket.user);
    return VXML_OK;
}

static bool record_termchar_valid(char value) {
    return (value >= '0' && value <= '9') ||
        value == '*' || value == '#' ||
        (value >= 'A' && value <= 'D');
}

static bool record_completion_empty_recording(
    const vxml_cmeta_record_completion_v1 *completion) {
    return completion != NULL &&
        completion->recording.data == NULL &&
        completion->recording.size == 0u &&
        completion->recording.lease == NULL &&
        completion->recording.release == NULL &&
        completion->recording.release_user == NULL;
}

vxml_cmeta_record_ingress_result
vxml_session_cmeta_record_try_complete(
    vxml_session *session,
    const vxml_cmeta_record_completion_v1 *completion) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_record_row *record;
    vxml_cmeta_record_completion_mailbox *mailbox;
    uint64_t generation;
    unsigned state;
    unsigned expected;

    if (session == NULL || completion == NULL ||
        completion->abi_version !=
            VXML_CMETA_RECORD_COMPLETION_ABI_V1 ||
        completion->struct_size < sizeof(*completion) ||
        completion->generation == UINT64_C(0))
        return VXML_CMETA_RECORD_INGRESS_INVALID_ARGUMENT;

    impl = (vxml_session_impl *)session->impl;
    if (impl == NULL)
        return VXML_CMETA_RECORD_INGRESS_CLOSED;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return impl->state == VXML_SESSION_CLOSED
            ? VXML_CMETA_RECORD_INGRESS_CLOSED
            : VXML_CMETA_RECORD_INGRESS_INVALID_ARGUMENT;
    if (impl->state == VXML_SESSION_CLOSED)
        return VXML_CMETA_RECORD_INGRESS_CLOSED;

    profile = (vxml_cmeta_session_data *)impl->profile_data;
    program = (const vxml_cmeta_program_data *)
        impl->program->profile_data;
    mailbox = &profile->record_mailbox;

    state = atomic_load_explicit(
        &mailbox->state, memory_order_acquire);
    if (state == VXML_CMETA_RECORD_MAILBOX_CLOSED)
        return VXML_CMETA_RECORD_INGRESS_CLOSED;
    if (state == VXML_CMETA_RECORD_MAILBOX_DISARMED)
        return VXML_CMETA_RECORD_INGRESS_STALE;
    if (state == VXML_CMETA_RECORD_MAILBOX_WRITING ||
        state == VXML_CMETA_RECORD_MAILBOX_READY)
        return VXML_CMETA_RECORD_INGRESS_FULL;
    if (state != VXML_CMETA_RECORD_MAILBOX_EMPTY)
        return VXML_CMETA_RECORD_INGRESS_INVALID_ARGUMENT;

    generation = atomic_load_explicit(
        &mailbox->generation, memory_order_relaxed);
    if (completion->generation != generation ||
        completion->generation != profile->record_generation ||
        !profile->record_in_flight ||
        profile->active_record == VXML_CMETA_NO_INDEX ||
        profile->active_record >= program->record_count ||
        program->records == NULL)
        return VXML_CMETA_RECORD_INGRESS_STALE;
    record = &program->records[profile->active_record];

    if (completion->duration_us > record->max_duration_us ||
        (record->has_maxtime &&
         completion->duration_us > record->maxtime_us))
        return VXML_CMETA_RECORD_INGRESS_INCOMPATIBLE_RESULT;
    if (completion->has_termchar) {
        if (!record_termchar_valid(completion->termchar))
            return VXML_CMETA_RECORD_INGRESS_INVALID_ARGUMENT;
        if (!record->dtmf_term)
            return VXML_CMETA_RECORD_INGRESS_INCOMPATIBLE_RESULT;
    } else if (completion->termchar != '\0') {
        return VXML_CMETA_RECORD_INGRESS_INVALID_ARGUMENT;
    }

    switch (completion->outcome) {
    case VXML_CMETA_RECORD_OUTCOME_SUCCESS:
        if (completion->recording.data == NULL ||
            completion->recording.size == 0u ||
            completion->recording.lease == NULL ||
            completion->recording.release == NULL ||
            completion->media_type.data == NULL ||
            completion->media_type.size == 0u ||
            memchr(
                completion->media_type.data, '\0',
                completion->media_type.size) != NULL)
            return VXML_CMETA_RECORD_INGRESS_INVALID_ARGUMENT;
        if (completion->recording.size > profile->max_record_bytes ||
            completion->media_type.size >
                record->max_media_type_bytes)
            return VXML_CMETA_RECORD_INGRESS_INCOMPATIBLE_RESULT;
        if (record->media_type_size != 0u &&
            (record->media_type == NULL ||
             completion->media_type.size != record->media_type_size ||
             memcmp(
                 completion->media_type.data,
                 record->media_type,
                 record->media_type_size) != 0))
            return VXML_CMETA_RECORD_INGRESS_INCOMPATIBLE_RESULT;
        break;
    case VXML_CMETA_RECORD_OUTCOME_TERMCHAR:
        if (!record->dtmf_term)
            return VXML_CMETA_RECORD_INGRESS_INCOMPATIBLE_RESULT;
        if (!completion->has_termchar ||
            !record_completion_empty_recording(completion) ||
            completion->media_type.data != NULL ||
            completion->media_type.size != 0u)
            return VXML_CMETA_RECORD_INGRESS_INVALID_ARGUMENT;
        break;
    case VXML_CMETA_RECORD_OUTCOME_NOINPUT:
    case VXML_CMETA_RECORD_OUTCOME_ERROR:
        if (completion->has_termchar ||
            !record_completion_empty_recording(completion) ||
            completion->media_type.data != NULL ||
            completion->media_type.size != 0u)
            return VXML_CMETA_RECORD_INGRESS_INVALID_ARGUMENT;
        break;
    default:
        return VXML_CMETA_RECORD_INGRESS_INVALID_ARGUMENT;
    }

    expected = VXML_CMETA_RECORD_MAILBOX_EMPTY;
    if (!atomic_compare_exchange_strong_explicit(
            &mailbox->state, &expected,
            VXML_CMETA_RECORD_MAILBOX_WRITING,
            memory_order_acq_rel, memory_order_acquire)) {
        if (expected == VXML_CMETA_RECORD_MAILBOX_CLOSED)
            return VXML_CMETA_RECORD_INGRESS_CLOSED;
        if (expected == VXML_CMETA_RECORD_MAILBOX_DISARMED)
            return VXML_CMETA_RECORD_INGRESS_STALE;
        return VXML_CMETA_RECORD_INGRESS_FULL;
    }

    if (atomic_load_explicit(
            &mailbox->generation, memory_order_relaxed) !=
            completion->generation ||
        !profile->record_in_flight ||
        profile->record_generation != completion->generation) {
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_RECORD_MAILBOX_EMPTY,
            memory_order_release);
        return VXML_CMETA_RECORD_INGRESS_STALE;
    }

    record_mailbox_payload_reset(mailbox, false);
    mailbox->outcome = completion->outcome;
    mailbox->duration_us = completion->duration_us;
    mailbox->has_termchar = completion->has_termchar;
    mailbox->termchar = completion->termchar;
    if (completion->media_type.size != 0u) {
        if (mailbox->media_type == NULL ||
            completion->media_type.size >
                mailbox->media_type_capacity) {
            atomic_store_explicit(
                &mailbox->state,
                VXML_CMETA_RECORD_MAILBOX_EMPTY,
                memory_order_release);
            return VXML_CMETA_RECORD_INGRESS_INCOMPATIBLE_RESULT;
        }
        memcpy(
            mailbox->media_type,
            completion->media_type.data,
            completion->media_type.size);
        mailbox->media_type_size = completion->media_type.size;
    }
    mailbox->recording = completion->recording;

    expected = VXML_CMETA_RECORD_MAILBOX_WRITING;
    if (!atomic_compare_exchange_strong_explicit(
            &mailbox->state, &expected,
            VXML_CMETA_RECORD_MAILBOX_READY,
            memory_order_acq_rel, memory_order_acquire))
        return expected == VXML_CMETA_RECORD_MAILBOX_CLOSED
            ? VXML_CMETA_RECORD_INGRESS_CLOSED
            : VXML_CMETA_RECORD_INGRESS_FULL;

    return VXML_CMETA_RECORD_INGRESS_ACCEPTED;
}

vxml_status vxml_session_cmeta_record_result(
    const vxml_session *session,
    const char *name,
    size_t name_size,
    vxml_cmeta_record_result_view_v1 *out_result) {
    const vxml_session_impl *impl;
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_session_data *profile;
    const vxml_cmeta_form_row *form;
    size_t offset;

    if (session == NULL || name == NULL || name_size == 0u ||
        out_result == NULL ||
        memchr(name, '\0', name_size) != NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (const vxml_session_impl *)session->impl;
    if (impl == NULL)
        return VXML_CLOSED;
    if (impl->state == VXML_SESSION_CLOSED)
        return VXML_CLOSED;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;

    program = (const vxml_cmeta_program_data *)
        impl->program->profile_data;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    if (profile->active_form == VXML_CMETA_NO_INDEX ||
        profile->active_form >= program->form_count ||
        program->forms == NULL ||
        profile->record_results == NULL)
        return VXML_INVALID_STATE;
    form = &program->forms[profile->active_form];
    if (!range_valid(
            form->first_record, form->record_count,
            program->record_count) ||
        (form->record_count != 0u && program->records == NULL))
        return VXML_INVALID_STRUCTURE;

    for (offset = 0u; offset < form->record_count; ++offset) {
        const size_t index = form->first_record + offset;
        const vxml_cmeta_record_row *record =
            &program->records[index];
        const vxml_cmeta_record_result_slot *result;
        if (record->name_size != name_size ||
            memcmp(record->name, name, name_size) != 0)
            continue;
        if (index >= profile->record_result_count)
            return VXML_INVALID_STRUCTURE;
        result = &profile->record_results[index];
        if (!result->live)
            return VXML_INVALID_STATE;
        *out_result = (vxml_cmeta_record_result_view_v1){
            .abi_version = VXML_CMETA_RECORD_RESULT_VIEW_ABI_V1,
            .struct_size = sizeof(vxml_cmeta_record_result_view_v1),
            .name = {record->name, record->name_size},
            .outcome = result->outcome,
            .duration_us = result->duration_us,
            .has_termchar = result->has_termchar,
            .termchar = result->termchar,
            .media_type = {
                result->media_type_size != 0u
                    ? result->media_type : NULL,
                result->media_type_size},
            .data = result->recording.data,
            .size = result->recording.size
        };
        return VXML_OK;
    }
    return VXML_INVALID_ARGUMENT;
}


static void subdialog_mailbox_payload_reset(
    vxml_cmeta_subdialog_completion_mailbox *mailbox) {
    if (mailbox == NULL) return;
    mailbox->kind = 0;
    mailbox->global_exit_kind = VXML_CMETA_SUBDIALOG_GLOBAL_NATURAL;
    mailbox->entry_count = 0u;
    mailbox->storage_size = 0u;
    mailbox->event = (vxml_cmeta_name_view){0};
    if (mailbox->entries != NULL && mailbox->entry_capacity != 0u)
        memset(
            mailbox->entries, 0,
            mailbox->entry_capacity * sizeof(*mailbox->entries));
}

static char *subdialog_mailbox_copy_bytes(
    vxml_cmeta_subdialog_completion_mailbox *mailbox,
    const char *data, size_t size) {
    char *destination;
    if (mailbox == NULL || size == 0u) return NULL;
    if (data == NULL || mailbox->storage == NULL ||
        mailbox->storage_size > mailbox->storage_capacity ||
        size > mailbox->storage_capacity - mailbox->storage_size)
        return NULL;
    destination = mailbox->storage + mailbox->storage_size;
    memcpy(destination, data, size);
    mailbox->storage_size += size;
    return destination;
}

static bool subdialog_completion_value_copy(
    vxml_cmeta_subdialog_completion_mailbox *mailbox,
    vxml_cmeta_value_view *out,
    const vxml_cmeta_value_view *value) {
    if (mailbox == NULL || out == NULL || value == NULL)
        return false;
    *out = *value;
    switch (value->kind) {
    case VXML_CMETA_VALUE_UNDEFINED:
    case VXML_CMETA_VALUE_BOOL:
    case VXML_CMETA_VALUE_SINT:
    case VXML_CMETA_VALUE_UINT:
    case VXML_CMETA_VALUE_FLOAT:
        return true;
    case VXML_CMETA_VALUE_STRING:
        if (value->data.string.size == 0u) {
            out->data.string.data = NULL;
            return true;
        }
        out->data.string.data = subdialog_mailbox_copy_bytes(
            mailbox,
            value->data.string.data,
            value->data.string.size);
        return out->data.string.data != NULL;
    default:
        return false;
    }
}

static bool subdialog_global_completion_shape_valid(
    const vxml_cmeta_subdialog_completion_v1 *completion) {
    size_t index;
    if (completion == NULL ||
        completion->event.data != NULL ||
        completion->event.size != 0u)
        return false;
    switch (completion->global_exit_kind) {
    case VXML_CMETA_SUBDIALOG_GLOBAL_NATURAL:
    case VXML_CMETA_SUBDIALOG_GLOBAL_EXIT_EMPTY:
    case VXML_CMETA_SUBDIALOG_GLOBAL_DISCONNECT:
        return completion->entry_count == 0u &&
            completion->entries == NULL;
    case VXML_CMETA_SUBDIALOG_GLOBAL_EXIT_EXPRESSION:
        return completion->entry_count == 1u &&
            completion->entries != NULL &&
            completion->entries[0].name.data == NULL &&
            completion->entries[0].name.size == 0u;
    case VXML_CMETA_SUBDIALOG_GLOBAL_EXIT_NAMELIST:
        if (completion->entry_count == 0u ||
            completion->entries == NULL)
            return false;
        for (index = 0u; index < completion->entry_count; ++index) {
            const vxml_cmeta_name_view name =
                completion->entries[index].name;
            if (name.data == NULL || name.size == 0u ||
                memchr(name.data, '\0', name.size) != NULL ||
                !cmeta_location_path_valid(
                    name.data, name.size, SIZE_MAX))
                return false;
        }
        return true;
    default:
        return false;
    }
}

vxml_cmeta_subdialog_ingress_result
vxml_session_cmeta_subdialog_try_complete(
    vxml_session *session,
    const vxml_cmeta_subdialog_completion_v1 *completion) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_subdialog_completion_mailbox *mailbox;
    uint64_t generation;
    unsigned state;
    unsigned expected;
    size_t index;

    if (session == NULL || completion == NULL ||
        completion->abi_version !=
            VXML_CMETA_SUBDIALOG_COMPLETION_ABI_V1 ||
        completion->struct_size < sizeof(*completion) ||
        completion->generation == UINT64_C(0))
        return VXML_CMETA_SUBDIALOG_INGRESS_INVALID_ARGUMENT;

    impl = (vxml_session_impl *)session->impl;
    if (impl == NULL)
        return VXML_CMETA_SUBDIALOG_INGRESS_CLOSED;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return impl->state == VXML_SESSION_CLOSED
            ? VXML_CMETA_SUBDIALOG_INGRESS_CLOSED
            : VXML_CMETA_SUBDIALOG_INGRESS_INVALID_ARGUMENT;
    if (impl->state == VXML_SESSION_CLOSED)
        return VXML_CMETA_SUBDIALOG_INGRESS_CLOSED;

    profile = (vxml_cmeta_session_data *)impl->profile_data;
    mailbox = &profile->subdialog_mailbox;
    state = atomic_load_explicit(
        &mailbox->state, memory_order_acquire);
    if (state == VXML_CMETA_SUBDIALOG_MAILBOX_CLOSED)
        return VXML_CMETA_SUBDIALOG_INGRESS_CLOSED;
    if (state == VXML_CMETA_SUBDIALOG_MAILBOX_DISARMED)
        return VXML_CMETA_SUBDIALOG_INGRESS_STALE;
    if (state == VXML_CMETA_SUBDIALOG_MAILBOX_WRITING ||
        state == VXML_CMETA_SUBDIALOG_MAILBOX_READY)
        return VXML_CMETA_SUBDIALOG_INGRESS_FULL;
    if (state != VXML_CMETA_SUBDIALOG_MAILBOX_EMPTY)
        return VXML_CMETA_SUBDIALOG_INGRESS_INVALID_ARGUMENT;

    generation = atomic_load_explicit(
        &mailbox->generation, memory_order_relaxed);
    if (completion->generation != generation ||
        completion->generation != profile->subdialog_generation ||
        !profile->subdialog_in_flight ||
        profile->active_subdialog == VXML_CMETA_NO_INDEX)
        return VXML_CMETA_SUBDIALOG_INGRESS_STALE;

    if (completion->kind == VXML_CMETA_SUBDIALOG_RETURN_DATA) {
        if (completion->event.data != NULL ||
            completion->event.size != 0u ||
            completion->global_exit_kind !=
                VXML_CMETA_SUBDIALOG_GLOBAL_NATURAL ||
            ((completion->entries == NULL) !=
             (completion->entry_count == 0u)))
            return VXML_CMETA_SUBDIALOG_INGRESS_INVALID_ARGUMENT;
        for (index = 0u; index < completion->entry_count; ++index) {
            const vxml_cmeta_subdialog_result_entry_v1 *entry =
                &completion->entries[index];
            if (entry->name.data == NULL ||
                entry->name.size == 0u ||
                memchr(entry->name.data, '\0', entry->name.size) != NULL)
                return VXML_CMETA_SUBDIALOG_INGRESS_INVALID_ARGUMENT;
            if (entry->value.kind == VXML_CMETA_VALUE_STRING &&
                entry->value.data.string.size != 0u &&
                entry->value.data.string.data == NULL)
                return VXML_CMETA_SUBDIALOG_INGRESS_INVALID_ARGUMENT;
            if (entry->value.kind < VXML_CMETA_VALUE_UNDEFINED ||
                entry->value.kind > VXML_CMETA_VALUE_STRING)
                return VXML_CMETA_SUBDIALOG_INGRESS_INVALID_ARGUMENT;
        }
    } else if (completion->kind == VXML_CMETA_SUBDIALOG_RETURN_EVENT) {
        if (completion->entries != NULL ||
            completion->entry_count != 0u ||
            completion->event.data == NULL ||
            completion->event.size == 0u ||
            memchr(
                completion->event.data, '\0',
                completion->event.size) != NULL ||
            !cmeta_location_path_valid(
                completion->event.data,
                completion->event.size, SIZE_MAX) ||
            completion->global_exit_kind !=
                VXML_CMETA_SUBDIALOG_GLOBAL_NATURAL)
            return VXML_CMETA_SUBDIALOG_INGRESS_INVALID_ARGUMENT;
    } else if (completion->kind ==
                   VXML_CMETA_SUBDIALOG_GLOBAL_EXIT) {
        if (!subdialog_global_completion_shape_valid(completion))
            return VXML_CMETA_SUBDIALOG_INGRESS_INVALID_ARGUMENT;
    } else {
        return VXML_CMETA_SUBDIALOG_INGRESS_INVALID_ARGUMENT;
    }

    if (completion->entry_count > mailbox->entry_capacity)
        return VXML_CMETA_SUBDIALOG_INGRESS_FULL;

    expected = VXML_CMETA_SUBDIALOG_MAILBOX_EMPTY;
    if (!atomic_compare_exchange_strong_explicit(
            &mailbox->state, &expected,
            VXML_CMETA_SUBDIALOG_MAILBOX_WRITING,
            memory_order_acq_rel, memory_order_acquire)) {
        if (expected == VXML_CMETA_SUBDIALOG_MAILBOX_CLOSED)
            return VXML_CMETA_SUBDIALOG_INGRESS_CLOSED;
        if (expected == VXML_CMETA_SUBDIALOG_MAILBOX_DISARMED)
            return VXML_CMETA_SUBDIALOG_INGRESS_STALE;
        return VXML_CMETA_SUBDIALOG_INGRESS_FULL;
    }

    if (atomic_load_explicit(
            &mailbox->generation, memory_order_relaxed) !=
            completion->generation ||
        !profile->subdialog_in_flight ||
        profile->subdialog_generation != completion->generation) {
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_SUBDIALOG_MAILBOX_EMPTY,
            memory_order_release);
        return VXML_CMETA_SUBDIALOG_INGRESS_STALE;
    }

    subdialog_mailbox_payload_reset(mailbox);
    mailbox->kind = completion->kind;
    mailbox->global_exit_kind = completion->global_exit_kind;

    if (completion->kind == VXML_CMETA_SUBDIALOG_RETURN_EVENT) {
        char *event = subdialog_mailbox_copy_bytes(
            mailbox, completion->event.data, completion->event.size);
        if (event == NULL) goto capacity_failure;
        mailbox->event = (vxml_cmeta_name_view){
            event, completion->event.size};
    } else {
        for (index = 0u; index < completion->entry_count; ++index) {
            const vxml_cmeta_subdialog_result_entry_v1 *in =
                &completion->entries[index];
            vxml_cmeta_subdialog_result_entry_v1 *out =
                &mailbox->entries[index];
            if (in->name.size != 0u) {
                char *name = subdialog_mailbox_copy_bytes(
                    mailbox, in->name.data, in->name.size);
                if (name == NULL) goto capacity_failure;
                out->name = (vxml_cmeta_name_view){
                    name, in->name.size};
            }
            if (!subdialog_completion_value_copy(
                    mailbox, &out->value, &in->value))
                goto incompatible_failure;
            ++mailbox->entry_count;
        }
    }

    expected = VXML_CMETA_SUBDIALOG_MAILBOX_WRITING;
    if (!atomic_compare_exchange_strong_explicit(
            &mailbox->state, &expected,
            VXML_CMETA_SUBDIALOG_MAILBOX_READY,
            memory_order_acq_rel, memory_order_acquire))
        return expected == VXML_CMETA_SUBDIALOG_MAILBOX_CLOSED
            ? VXML_CMETA_SUBDIALOG_INGRESS_CLOSED
            : VXML_CMETA_SUBDIALOG_INGRESS_FULL;
    return VXML_CMETA_SUBDIALOG_INGRESS_ACCEPTED;

capacity_failure:
    subdialog_mailbox_payload_reset(mailbox);
    atomic_store_explicit(
        &mailbox->state,
        VXML_CMETA_SUBDIALOG_MAILBOX_EMPTY,
        memory_order_release);
    return VXML_CMETA_SUBDIALOG_INGRESS_FULL;

incompatible_failure:
    subdialog_mailbox_payload_reset(mailbox);
    atomic_store_explicit(
        &mailbox->state,
        VXML_CMETA_SUBDIALOG_MAILBOX_EMPTY,
        memory_order_release);
    return VXML_CMETA_SUBDIALOG_INGRESS_INCOMPATIBLE_RESULT;
}


static bool prompt_dynamic_mark_segment(
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_prompt_row *prompt,
    size_t absolute_segment) {
    size_t index;
    size_t matched = 0u;
    if (program == NULL || prompt == NULL ||
        prompt->dynamic_mark_count == 0u ||
        program->prompt_mark_exprs == NULL)
        return false;
    for (index = 0u;
         index < program->prompt_mark_expr_count;
         ++index) {
        const vxml_cmeta_prompt_mark_expr_row *row =
            &program->prompt_mark_exprs[index];
        if (row->segment_index < prompt->first_segment)
            continue;
        if (row->segment_index >=
            prompt->first_segment + prompt->segment_count)
            break;
        ++matched;
        if (row->segment_index == absolute_segment)
            return true;
        if (matched >= prompt->dynamic_mark_count)
            break;
    }
    return false;
}

static vxml_status selected_prompt_row(
    const vxml_session_impl *impl,
    const vxml_cmeta_field_row **out_field,
    const vxml_cmeta_prompt_row **out_prompt,
    unsigned *out_prompt_count);

static vxml_status collect_request_from_impl(
    const vxml_session_impl *impl,
    vxml_cmeta_collect_request_v1 *out_request) {
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_session_data *profile;
    const vxml_cmeta_field_row *field;
    const vxml_cmeta_prompt_row *prompt = NULL;
    unsigned prompt_count = 0u;
    vxml_status status;
    if (out_request == NULL) return VXML_INVALID_ARGUMENT;
    *out_request = (vxml_cmeta_collect_request_v1){0};
    if (impl == NULL || impl->state != VXML_SESSION_RUNNING ||
        impl->program == NULL || impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_STATE;
    program = (const vxml_cmeta_program_data *)impl->program->profile_data;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    if (profile->active_initial != VXML_CMETA_NO_INDEX ||
        profile->active_menu != VXML_CMETA_NO_INDEX)
        return VXML_INVALID_STATE;
    if (profile->active_field == VXML_CMETA_NO_INDEX ||
        profile->active_field >= program->field_count ||
        program->fields == NULL ||
        profile->collect_generation == 0u)
        return VXML_INVALID_STATE;
    field = &program->fields[profile->active_field];
    status = selected_prompt_row(
        impl, &field, &prompt, &prompt_count);
    if (status != VXML_OK) return status;
    (void)prompt_count;
    if (field->name == NULL || field->name_size == 0u ||
        field->grammar_type == NULL || field->grammar_type_size == 0u ||
        field->grammar_src == NULL || field->grammar_src_size == 0u)
        return VXML_INVALID_STRUCTURE;
    *out_request = (vxml_cmeta_collect_request_v1){
        .abi_version = VXML_CMETA_COLLECT_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_collect_request_v1),
        .generation = profile->collect_generation,
        .required_capabilities = field->required_capabilities,
        .field = field != NULL
            ? (vxml_cmeta_name_view){field->name, field->name_size}
            : (vxml_cmeta_name_view){0},
        .grammar_type = {field->grammar_type, field->grammar_type_size},
        .grammar_src = {field->grammar_src, field->grammar_src_size},
        .has_timeout = prompt != NULL ? prompt->has_timeout : false,
        .timeout_us = prompt != NULL ? prompt->timeout_us : UINT64_C(0)
    };
    return VXML_OK;
}

static vxml_status collect_request_v2_from_impl(
    const vxml_session_impl *impl,
    vxml_cmeta_collect_request_v2 *out_request) {
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_session_data *profile;
    const vxml_cmeta_form_row *form;
    vxml_cmeta_collect_request_v1 base = {0};
    vxml_status status;
    if (out_request == NULL) return VXML_INVALID_ARGUMENT;
    *out_request = (vxml_cmeta_collect_request_v2){0};
    status = collect_request_from_impl(impl, &base);
    if (status != VXML_OK) return status;
    program = (const vxml_cmeta_program_data *)impl->program->profile_data;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    if (profile->active_form >= program->form_count ||
        program->forms == NULL)
        return VXML_INVALID_STRUCTURE;
    form = &program->forms[profile->active_form];
    *out_request = (vxml_cmeta_collect_request_v2){
        .abi_version = VXML_CMETA_COLLECT_REQUEST_ABI_V2,
        .struct_size = sizeof(vxml_cmeta_collect_request_v2),
        .generation = base.generation,
        .required_capabilities = base.required_capabilities,
        .field = base.field,
        .grammar_type = base.grammar_type,
        .grammar_src = base.grammar_src,
        .has_timeout = base.has_timeout,
        .timeout_us = base.timeout_us,
        .record_utterance = form->record_utterance
    };
    if (form->record_utterance) {
        out_request->required_capabilities |=
            VXML_CMETA_COLLECT_CAP_RECORD_UTTERANCE;
        if (form->recording_media_type_size != 0u) {
            if (form->recording_media_type == NULL)
                return VXML_INVALID_STRUCTURE;
            out_request->required_capabilities |=
                VXML_CMETA_COLLECT_CAP_RECORD_UTTERANCE_TYPE;
            out_request->recording_media_type =
                (vxml_cmeta_name_view){
                    form->recording_media_type,
                    form->recording_media_type_size};
        }
        if (form->max_recording_duration_us == UINT64_C(0) ||
            profile->max_collect_recording_bytes == 0u)
            return VXML_INVALID_CONTRACT;
        out_request->max_recording_duration_us =
            form->max_recording_duration_us;
        out_request->max_recording_bytes =
            profile->max_collect_recording_bytes;
    }
    return VXML_OK;
}

static vxml_status menu_collect_request_from_impl(
    const vxml_session_impl *impl,
    vxml_cmeta_menu_collect_request_v1 *out_request) {
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_session_data *profile;
    const vxml_cmeta_menu_row *menu;
    uint64_t required_capabilities =
        VXML_CMETA_COLLECT_CAP_MENU_CHOICE;
    size_t choice_offset;
    if (out_request == NULL) return VXML_INVALID_ARGUMENT;
    *out_request = (vxml_cmeta_menu_collect_request_v1){0};
    if (impl == NULL || impl->state != VXML_SESSION_RUNNING ||
        impl->program == NULL || impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_STATE;
    program = (const vxml_cmeta_program_data *)impl->program->profile_data;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    if (profile->active_field != VXML_CMETA_NO_INDEX ||
        profile->active_initial != VXML_CMETA_NO_INDEX ||
        profile->active_menu == VXML_CMETA_NO_INDEX ||
        profile->active_menu >= program->menu_count ||
        program->menus == NULL ||
        program->menu_choices == NULL ||
        profile->collect_generation == 0u)
        return VXML_INVALID_STATE;
    menu = &program->menus[profile->active_menu];
    if (menu->form != profile->active_form ||
        menu->choice_count == 0u ||
        !range_valid(
            menu->first_choice, menu->choice_count,
            program->menu_choice_count) ||
        !range_valid(
            menu->first_speech_policy, menu->speech_policy_count,
            program->menu_speech_policy_count) ||
        !range_valid(
            menu->first_grammar, menu->grammar_count,
            program->menu_grammar_count))
        return VXML_INVALID_STRUCTURE;
    if (menu->speech_policy_count != 0u ||
        menu->grammar_count != 0u)
        return VXML_UNSUPPORTED_FEATURE;
    for (choice_offset = 0u;
         choice_offset < menu->choice_count;
         ++choice_offset) {
        const vxml_cmeta_menu_choice_v1 *choice =
            &program->menu_choices[
                menu->first_choice + choice_offset];
        if ((choice->dtmf.data == NULL) !=
                (choice->dtmf.size == 0u) ||
            (choice->speech.data == NULL) !=
                (choice->speech.size == 0u) ||
            (choice->dtmf.size == 0u &&
             choice->speech.size == 0u))
            return VXML_INVALID_STRUCTURE;
        if (choice->speech.size != 0u)
            required_capabilities |=
                VXML_CMETA_COLLECT_CAP_MENU_SPEECH_EXACT;
    }
    *out_request = (vxml_cmeta_menu_collect_request_v1){
        .abi_version = VXML_CMETA_MENU_COLLECT_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_menu_collect_request_v1),
        .generation = profile->collect_generation,
        .required_capabilities = required_capabilities,
        .choices = &program->menu_choices[menu->first_choice],
        .choice_count = menu->choice_count
    };
    return VXML_OK;
}


static vxml_status menu_collect_request_v2_from_impl(
    const vxml_session_impl *impl,
    vxml_cmeta_menu_collect_request_v2 *out_request) {
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_session_data *profile;
    const vxml_cmeta_menu_row *menu;
    uint64_t required_capabilities =
        VXML_CMETA_COLLECT_CAP_MENU_CHOICE;
    size_t choice_offset;
    size_t policy_offset;
    size_t grammar_offset;

    if (out_request == NULL) return VXML_INVALID_ARGUMENT;
    *out_request = (vxml_cmeta_menu_collect_request_v2){0};
    if (impl == NULL || impl->state != VXML_SESSION_RUNNING ||
        impl->program == NULL || impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_STATE;
    program = (const vxml_cmeta_program_data *)impl->program->profile_data;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    if (profile->active_field != VXML_CMETA_NO_INDEX ||
        profile->active_menu == VXML_CMETA_NO_INDEX ||
        profile->active_menu >= program->menu_count ||
        program->menus == NULL ||
        program->menu_choices == NULL ||
        profile->collect_generation == 0u)
        return VXML_INVALID_STATE;
    menu = &program->menus[profile->active_menu];
    if (menu->form != profile->active_form ||
        menu->choice_count == 0u ||
        !range_valid(
            menu->first_choice, menu->choice_count,
            program->menu_choice_count) ||
        !range_valid(
            menu->first_speech_policy, menu->speech_policy_count,
            program->menu_speech_policy_count) ||
        !range_valid(
            menu->first_grammar, menu->grammar_count,
            program->menu_grammar_count) ||
        (menu->speech_policy_count != 0u &&
         program->menu_speech_policies == NULL) ||
        (menu->grammar_count != 0u &&
         program->menu_grammars == NULL))
        return VXML_INVALID_STRUCTURE;

    for (policy_offset = 0u;
         policy_offset < menu->speech_policy_count;
         ++policy_offset) {
        const vxml_cmeta_menu_speech_policy_v1 *row =
            &program->menu_speech_policies[
                menu->first_speech_policy + policy_offset];
        if (row->choice_index >= menu->choice_count ||
            row->mode != VXML_CMETA_MENU_ACCEPT_APPROXIMATE ||
            (policy_offset != 0u &&
             program->menu_speech_policies[
                 menu->first_speech_policy + policy_offset - 1u]
                 .choice_index >= row->choice_index))
            return VXML_INVALID_STRUCTURE;
    }
    for (grammar_offset = 0u;
         grammar_offset < menu->grammar_count;
         ++grammar_offset) {
        const vxml_cmeta_menu_grammar_ref_v1 *row =
            &program->menu_grammars[
                menu->first_grammar + grammar_offset];
        if (row->choice_index >= menu->choice_count ||
            row->media_type.data == NULL || row->media_type.size == 0u ||
            row->src.data == NULL || row->src.size == 0u ||
            (grammar_offset != 0u &&
             program->menu_grammars[
                 menu->first_grammar + grammar_offset - 1u]
                 .choice_index >= row->choice_index))
            return VXML_INVALID_STRUCTURE;
    }

    for (choice_offset = 0u;
         choice_offset < menu->choice_count;
         ++choice_offset) {
        const vxml_cmeta_menu_choice_v1 *choice =
            &program->menu_choices[
                menu->first_choice + choice_offset];
        bool approximate = false;
        bool explicit_grammar = false;
        if ((choice->dtmf.data == NULL) !=
                (choice->dtmf.size == 0u) ||
            (choice->speech.data == NULL) !=
                (choice->speech.size == 0u))
            return VXML_INVALID_STRUCTURE;
        for (policy_offset = 0u;
             policy_offset < menu->speech_policy_count;
             ++policy_offset) {
            const vxml_cmeta_menu_speech_policy_v1 *row =
                &program->menu_speech_policies[
                    menu->first_speech_policy + policy_offset];
            if (row->choice_index == choice_offset) {
                approximate = true;
                break;
            }
            if (row->choice_index > choice_offset) break;
        }
        for (grammar_offset = 0u;
             grammar_offset < menu->grammar_count;
             ++grammar_offset) {
            const vxml_cmeta_menu_grammar_ref_v1 *row =
                &program->menu_grammars[
                    menu->first_grammar + grammar_offset];
            if (row->choice_index == choice_offset) {
                explicit_grammar = true;
                break;
            }
            if (row->choice_index > choice_offset) break;
        }
        if (approximate && explicit_grammar)
            return VXML_INVALID_STRUCTURE;
        if (explicit_grammar) {
            if (choice->speech.size != 0u)
                return VXML_INVALID_STRUCTURE;
            required_capabilities |=
                VXML_CMETA_COLLECT_CAP_MENU_GRAMMAR_EXTERNAL;
        } else if (choice->speech.size != 0u) {
            required_capabilities |= approximate
                ? VXML_CMETA_COLLECT_CAP_MENU_SPEECH_APPROXIMATE
                : VXML_CMETA_COLLECT_CAP_MENU_SPEECH_EXACT;
        } else if (choice->dtmf.size == 0u) {
            return VXML_INVALID_STRUCTURE;
        }
    }

    *out_request = (vxml_cmeta_menu_collect_request_v2){
        .abi_version = VXML_CMETA_MENU_COLLECT_REQUEST_ABI_V2,
        .struct_size = sizeof(vxml_cmeta_menu_collect_request_v2),
        .generation = profile->collect_generation,
        .required_capabilities = required_capabilities,
        .choices = &program->menu_choices[menu->first_choice],
        .choice_count = menu->choice_count,
        .speech_policies = menu->speech_policy_count != 0u
            ? &program->menu_speech_policies[menu->first_speech_policy]
            : NULL,
        .speech_policy_count = menu->speech_policy_count,
        .grammars = menu->grammar_count != 0u
            ? &program->menu_grammars[menu->first_grammar]
            : NULL,
        .grammar_count = menu->grammar_count
    };
    return VXML_OK;
}


static vxml_status initial_collect_request_from_impl(
    const vxml_session_impl *impl,
    vxml_cmeta_initial_collect_request_v1 *out_request) {
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_session_data *profile;
    const vxml_cmeta_initial_row *initial;
    const vxml_cmeta_form_row *form;
    if (out_request == NULL) return VXML_INVALID_ARGUMENT;
    *out_request = (vxml_cmeta_initial_collect_request_v1){0};
    if (impl == NULL || impl->state != VXML_SESSION_RUNNING ||
        impl->program == NULL || impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_STATE;
    program = (const vxml_cmeta_program_data *)impl->program->profile_data;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    if (profile->active_field != VXML_CMETA_NO_INDEX ||
        profile->active_menu != VXML_CMETA_NO_INDEX ||
        profile->active_initial == VXML_CMETA_NO_INDEX ||
        profile->active_initial >= program->initial_count ||
        profile->active_form >= program->form_count ||
        program->initials == NULL || program->forms == NULL ||
        profile->collect_generation == 0u)
        return VXML_INVALID_STATE;
    initial = &program->initials[profile->active_initial];
    form = &program->forms[profile->active_form];
    if (initial->form != profile->active_form ||
        profile->active_initial < form->first_initial ||
        profile->active_initial - form->first_initial >=
            form->initial_count ||
        form->grammar_type == NULL || form->grammar_type_size == 0u ||
        form->grammar_src == NULL || form->grammar_src_size == 0u ||
        form->grammar_required_capabilities !=
            (VXML_CMETA_COLLECT_CAP_SRGS_XML |
             VXML_CMETA_COLLECT_CAP_INITIAL_MULTI))
        return VXML_INVALID_STRUCTURE;
    *out_request = (vxml_cmeta_initial_collect_request_v1){
        .abi_version = VXML_CMETA_INITIAL_COLLECT_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_initial_collect_request_v1),
        .generation = profile->collect_generation,
        .required_capabilities = form->grammar_required_capabilities,
        .grammar_type = {
            form->grammar_type, form->grammar_type_size},
        .grammar_src = {
            form->grammar_src, form->grammar_src_size}
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
    {
        const vxml_cmeta_program_data *program =
            (const vxml_cmeta_program_data *)impl->program->profile_data;
        const vxml_cmeta_session_data *profile =
            (const vxml_cmeta_session_data *)impl->profile_data;
        if (profile != NULL && program != NULL &&
            profile->active_form < program->form_count &&
            program->forms != NULL &&
            program->forms[profile->active_form].record_utterance)
            return VXML_UNSUPPORTED_FEATURE;
    }
    return collect_request_from_impl(impl, out_request);
}

vxml_status vxml_session_cmeta_collect_request_v2(
    const vxml_session *session,
    vxml_cmeta_collect_request_v2 *out_request) {
    const vxml_session_impl *impl = cmeta_session(session);
    if (out_request != NULL)
        *out_request = (vxml_cmeta_collect_request_v2){0};
    if (out_request == NULL) return VXML_INVALID_ARGUMENT;
    if (impl == NULL) return VXML_INVALID_CONTRACT;
    return collect_request_v2_from_impl(impl, out_request);
}

vxml_status vxml_session_cmeta_menu_collect_request(
    const vxml_session *session,
    vxml_cmeta_menu_collect_request_v1 *out_request) {
    const vxml_session_impl *impl = cmeta_session(session);
    if (out_request != NULL)
        *out_request = (vxml_cmeta_menu_collect_request_v1){0};
    if (out_request == NULL) return VXML_INVALID_ARGUMENT;
    if (impl == NULL) return VXML_INVALID_CONTRACT;
    return menu_collect_request_from_impl(impl, out_request);
}

vxml_status vxml_session_cmeta_menu_collect_request_v2(
    const vxml_session *session,
    vxml_cmeta_menu_collect_request_v2 *out_request) {
    const vxml_session_impl *impl = cmeta_session(session);
    if (out_request != NULL)
        *out_request = (vxml_cmeta_menu_collect_request_v2){0};
    if (out_request == NULL) return VXML_INVALID_ARGUMENT;
    if (impl == NULL) return VXML_INVALID_CONTRACT;
    return menu_collect_request_v2_from_impl(impl, out_request);
}

vxml_status vxml_session_cmeta_initial_collect_request(
    const vxml_session *session,
    vxml_cmeta_initial_collect_request_v1 *out_request) {
    const vxml_session_impl *impl = cmeta_session(session);
    if (out_request != NULL)
        *out_request = (vxml_cmeta_initial_collect_request_v1){0};
    if (out_request == NULL) return VXML_INVALID_ARGUMENT;
    if (impl == NULL) return VXML_INVALID_CONTRACT;
    return initial_collect_request_from_impl(impl, out_request);
}

vxml_status vxml_session_cmeta_collect_prepare(
    vxml_session *session, const char **out_error) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
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
    if (profile->collect_adapter == NULL)
        return VXML_INVALID_CONTRACT;
    if (profile->collect_prepared || profile->collect_in_flight)
        return VXML_INVALID_STATE;

    if (profile->active_initial != VXML_CMETA_NO_INDEX) {
        vxml_cmeta_initial_collect_request_v1 request = {0};
        status = initial_collect_request_from_impl(impl, &request);
        if (status != VXML_OK) return status;
        if ((profile->collect_adapter->capabilities &
             request.required_capabilities) !=
            request.required_capabilities)
            return VXML_UNSUPPORTED_FEATURE;
        if (!collect_adapter_has_initial(profile->collect_adapter))
            return VXML_UNSUPPORTED_FEATURE;
        status = profile->collect_adapter->prepare_initial(
            profile->collect_user, &request, &ticket, out_error);
    } else if (profile->active_menu != VXML_CMETA_NO_INDEX) {
        const vxml_cmeta_program_data *program =
            (const vxml_cmeta_program_data *)impl->program->profile_data;
        const vxml_cmeta_menu_row *menu;
        if (profile->active_menu >= program->menu_count ||
            program->menus == NULL)
            return VXML_INVALID_STRUCTURE;
        menu = &program->menus[profile->active_menu];
        if (menu->speech_policy_count != 0u ||
            menu->grammar_count != 0u) {
            vxml_cmeta_menu_collect_request_v2 request = {0};
            status = menu_collect_request_v2_from_impl(impl, &request);
            if (status != VXML_OK) return status;
            if ((profile->collect_adapter->capabilities &
                 request.required_capabilities) !=
                request.required_capabilities)
                return VXML_UNSUPPORTED_FEATURE;
            if (!collect_adapter_has_menu_v2(profile->collect_adapter))
                return VXML_UNSUPPORTED_FEATURE;
            status = profile->collect_adapter->prepare_menu_v2(
                profile->collect_user, &request, &ticket, out_error);
        } else {
            vxml_cmeta_menu_collect_request_v1 request = {0};
            status = menu_collect_request_from_impl(impl, &request);
            if (status != VXML_OK) return status;
            if ((profile->collect_adapter->capabilities &
                 request.required_capabilities) !=
                request.required_capabilities)
                return VXML_UNSUPPORTED_FEATURE;
            if (!collect_adapter_has_menu(profile->collect_adapter))
                return VXML_UNSUPPORTED_FEATURE;
            status = profile->collect_adapter->prepare_menu(
                profile->collect_user, &request, &ticket, out_error);
        }
    } else {
        const vxml_cmeta_program_data *program =
            (const vxml_cmeta_program_data *)impl->program->profile_data;
        const vxml_cmeta_form_row *form;
        if (profile->active_field >= program->field_count ||
            profile->active_form >= program->form_count ||
            program->fields == NULL || program->forms == NULL ||
            !collect_fixed_scalar_data(
                program->fields[profile->active_field].field_data))
            return VXML_UNSUPPORTED_FEATURE;
        form = &program->forms[profile->active_form];
        if (form->record_utterance) {
            vxml_cmeta_collect_request_v2 request = {0};
            status = collect_request_v2_from_impl(impl, &request);
            if (status != VXML_OK) return status;
            if ((profile->collect_adapter->capabilities &
                 request.required_capabilities) !=
                request.required_capabilities)
                return VXML_UNSUPPORTED_FEATURE;
            if (!collect_adapter_has_field_v2(profile->collect_adapter))
                return VXML_UNSUPPORTED_FEATURE;
            status = profile->collect_adapter->prepare_v2(
                profile->collect_user, &request, &ticket, out_error);
        } else {
            vxml_cmeta_collect_request_v1 request = {0};
            status = collect_request_from_impl(impl, &request);
            if (status != VXML_OK) return status;
            if ((profile->collect_adapter->capabilities &
                 request.required_capabilities) !=
                request.required_capabilities)
                return VXML_UNSUPPORTED_FEATURE;
            if (profile->collect_adapter->prepare == NULL)
                return VXML_INVALID_CONTRACT;
            status = profile->collect_adapter->prepare(
                profile->collect_user, &request, &ticket, out_error);
        }
    }

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
        unsigned state = atomic_load_explicit(
            &profile->collect_mailbox.state, memory_order_acquire);
        if (state != VXML_CMETA_COLLECT_MAILBOX_DISARMED)
            return VXML_INVALID_STATE;

        profile->collect_mailbox.data = NULL;
        profile->collect_mailbox.slot_count = 0u;
        profile->collect_mailbox.choice_index = SIZE_MAX;
        profile->collect_mailbox.record_utterance_expected = false;
        profile->collect_mailbox.max_recording_duration_us = UINT64_C(0);
        collect_recording_payload_reset(
            &profile->collect_mailbox, false);
        profile->collect_quiesced_generation = UINT64_C(0);

        if (profile->active_initial != VXML_CMETA_NO_INDEX) {
            const vxml_cmeta_form_row *form;
            const vxml_cmeta_initial_row *initial;
            if (profile->active_field != VXML_CMETA_NO_INDEX ||
                profile->active_menu != VXML_CMETA_NO_INDEX ||
                profile->active_form >= program->form_count ||
                profile->active_initial >= program->initial_count ||
                program->forms == NULL || program->initials == NULL ||
                profile->collect_mailbox.storage == NULL ||
                profile->collect_mailbox.root_fields == NULL ||
                profile->collect_mailbox.slot_capacity == 0u)
                return VXML_INVALID_STRUCTURE;
            form = &program->forms[profile->active_form];
            initial = &program->initials[profile->active_initial];
            if (initial->form != profile->active_form ||
                form->grammar_required_capabilities !=
                    (VXML_CMETA_COLLECT_CAP_SRGS_XML |
                     VXML_CMETA_COLLECT_CAP_INITIAL_MULTI))
                return VXML_INVALID_STRUCTURE;
            profile->collect_mailbox.item_kind =
                VXML_CMETA_COLLECT_ITEM_INITIAL;
        } else if (profile->active_menu != VXML_CMETA_NO_INDEX) {
            const vxml_cmeta_menu_row *menu;
            if (profile->active_field != VXML_CMETA_NO_INDEX ||
                profile->active_menu >= program->menu_count ||
                program->menus == NULL)
                return VXML_INVALID_STRUCTURE;
            menu = &program->menus[profile->active_menu];
            if (menu->form != profile->active_form ||
                menu->choice_count == 0u ||
                !range_valid(
                    menu->first_choice, menu->choice_count,
                    program->menu_choice_count))
                return VXML_INVALID_STRUCTURE;
            profile->collect_mailbox.item_kind =
                VXML_CMETA_COLLECT_ITEM_MENU;
        } else {
            const vxml_cmeta_field_row *field;
            const vxml_cmeta_form_row *form;
            if (profile->active_field >= program->field_count ||
                profile->active_form >= program->form_count ||
                program->fields == NULL || program->forms == NULL)
                return VXML_INVALID_STRUCTURE;
            field = &program->fields[profile->active_field];
            form = &program->forms[profile->active_form];
            if (field->form != profile->active_form ||
                !collect_fixed_scalar_data(field->field_data) ||
                profile->collect_mailbox.storage == NULL ||
                field->field_data->storage_type->size >
                    profile->collect_mailbox.storage_bytes)
                return VXML_UNSUPPORTED_FEATURE;
            profile->collect_mailbox.item_kind =
                VXML_CMETA_COLLECT_ITEM_FIELD;
            profile->collect_mailbox.data = field->field_data;
            profile->collect_mailbox.record_utterance_expected =
                form->record_utterance;
            if (form->record_utterance) {
                if (form->max_recording_duration_us == UINT64_C(0) ||
                    profile->max_collect_recording_bytes == 0u)
                    return VXML_INVALID_CONTRACT;
                profile->collect_mailbox.max_recording_duration_us =
                    form->max_recording_duration_us;
            }
        }
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
    if (mailbox->item_kind != VXML_CMETA_COLLECT_ITEM_FIELD ||
        profile->active_menu != VXML_CMETA_NO_INDEX ||
        mailbox->record_utterance_expected)
        return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;
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
    const vxml_cmeta_field_row *selected = NULL;
    const vxml_cmeta_form_row *form = NULL;
    vxml_cmeta_collect_mailbox *mailbox;
    bool initial_mode;
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

    if (profile->active_menu != VXML_CMETA_NO_INDEX)
        return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;
    initial_mode = profile->active_initial != VXML_CMETA_NO_INDEX;
    if (mailbox->record_utterance_expected)
        return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;

    if (initial_mode) {
        const vxml_cmeta_initial_row *initial;
        if (profile->active_field != VXML_CMETA_NO_INDEX ||
            profile->active_initial >= program->initial_count ||
            profile->active_form >= program->form_count ||
            program->initials == NULL || program->forms == NULL)
            return VXML_CMETA_COLLECT_INGRESS_STALE;
        initial = &program->initials[profile->active_initial];
        form = &program->forms[profile->active_form];
        if (initial->form != profile->active_form ||
            form->initial_count == 0u ||
            form->field_count == 0u)
            return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;
    } else {
        if (profile->active_field >= program->field_count ||
            program->fields == NULL)
            return VXML_CMETA_COLLECT_INGRESS_STALE;
        selected = &program->fields[profile->active_field];
        if (profile->active_form >= program->form_count ||
            program->forms == NULL)
            return VXML_CMETA_COLLECT_INGRESS_STALE;
        form = &program->forms[profile->active_form];
        if (selected->form != profile->active_form)
            return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;
    }

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
    if ((!initial_mode &&
         mailbox->item_kind != VXML_CMETA_COLLECT_ITEM_FIELD) ||
        (initial_mode &&
         mailbox->item_kind != VXML_CMETA_COLLECT_ITEM_INITIAL))
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
            slot->data == NULL || slot->value == NULL)
            goto incompatible;

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

        if (initial_mode) {
            size_t field_offset;
            bool same_form = false;
            if (!range_valid(
                    form->first_field, form->field_count,
                    program->field_count) ||
                program->fields == NULL)
                goto incompatible;
            for (field_offset = 0u;
                 field_offset < form->field_count;
                 ++field_offset) {
                const vxml_cmeta_field_row *candidate =
                    &program->fields[form->first_field + field_offset];
                if (candidate->form != profile->active_form)
                    goto incompatible;
                if (candidate->root_field == root_field) {
                    same_form = true;
                    break;
                }
            }
            if (!same_form) goto incompatible;
        }

        type = target->value->storage_type;
        if (type == NULL ||
            type->size > mailbox->storage_stride)
            goto incompatible;
        for (prior = 0u; prior < slot_index; ++prior)
            if (mailbox->root_fields[prior] == root_field)
                goto incompatible;
        if (!initial_mode && root_field == selected->root_field)
            ++selected_count;

        mailbox->root_fields[slot_index] = root_field;
        memcpy(
            mailbox->storage +
                slot_index * mailbox->storage_stride,
            slot->value, type->size);
    }

    if (!initial_mode && selected_count != 1u)
        goto incompatible;
    if (atomic_load_explicit(
            &mailbox->generation, memory_order_relaxed) !=
            completion->generation)
        goto stale_after_claim;

    mailbox->slot_count = completion->slot_count;
    mailbox->data = initial_mode ? NULL : selected->field_data;
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

vxml_cmeta_collect_ingress_result vxml_session_cmeta_collect_try_complete_v3(
    vxml_session *session,
    const vxml_cmeta_collect_completion_v3 *completion) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    const vxml_cmeta_program_data *program;
    const cmeta_data_struct_shape *root_shape;
    const vxml_cmeta_field_row *selected = NULL;
    const vxml_cmeta_form_row *form = NULL;
    vxml_cmeta_collect_mailbox *mailbox;
    bool initial_mode;
    bool recording_present;
    uint64_t generation;
    unsigned state;
    unsigned expected;
    size_t slot_index;
    size_t selected_count = 0u;

    if (session == NULL || completion == NULL)
        return VXML_CMETA_COLLECT_INGRESS_INVALID_ARGUMENT;
    if (completion->abi_version !=
            VXML_CMETA_COLLECT_COMPLETION_ABI_V3 ||
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

    if (profile->active_menu != VXML_CMETA_NO_INDEX)
        return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;
    initial_mode = profile->active_initial != VXML_CMETA_NO_INDEX;
    if (initial_mode || !mailbox->record_utterance_expected)
        return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;

    recording_present =
        completion->recording.data != NULL ||
        completion->recording.size != 0u ||
        completion->recording.lease != NULL ||
        completion->recording.release != NULL ||
        completion->recording.release_user != NULL ||
        completion->recording_duration_us != UINT64_C(0) ||
        completion->recording_media_type.data != NULL ||
        completion->recording_media_type.size != 0u;
    if (recording_present) {
        if (completion->recording.data == NULL ||
            completion->recording.size == 0u ||
            completion->recording.lease == NULL ||
            completion->recording.release == NULL ||
            completion->recording.size >
                profile->max_collect_recording_bytes ||
            completion->recording_duration_us >
                mailbox->max_recording_duration_us ||
            completion->recording_media_type.data == NULL ||
            completion->recording_media_type.size == 0u ||
            completion->recording_media_type.size >
                mailbox->recording_media_type_capacity ||
            mailbox->recording_media_type == NULL ||
            memchr(
                completion->recording_media_type.data,
                '\0',
                completion->recording_media_type.size) != NULL)
            return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;
    } else if (completion->recording_duration_us != UINT64_C(0) ||
               completion->recording_media_type.data != NULL ||
               completion->recording_media_type.size != 0u)
        return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;

    if (initial_mode) {
        const vxml_cmeta_initial_row *initial;
        if (profile->active_field != VXML_CMETA_NO_INDEX ||
            profile->active_initial >= program->initial_count ||
            profile->active_form >= program->form_count ||
            program->initials == NULL || program->forms == NULL)
            return VXML_CMETA_COLLECT_INGRESS_STALE;
        initial = &program->initials[profile->active_initial];
        form = &program->forms[profile->active_form];
        if (initial->form != profile->active_form ||
            form->initial_count == 0u ||
            form->field_count == 0u)
            return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;
    } else {
        if (profile->active_field >= program->field_count ||
            program->fields == NULL)
            return VXML_CMETA_COLLECT_INGRESS_STALE;
        selected = &program->fields[profile->active_field];
        if (profile->active_form >= program->form_count ||
            program->forms == NULL)
            return VXML_CMETA_COLLECT_INGRESS_STALE;
        form = &program->forms[profile->active_form];
        if (selected->form != profile->active_form)
            return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;
    }

    if (recording_present &&
        form->recording_media_type_size != 0u &&
        (form->recording_media_type == NULL ||
         completion->recording_media_type.size !=
             form->recording_media_type_size ||
         memcmp(
             completion->recording_media_type.data,
             form->recording_media_type,
             form->recording_media_type_size) != 0))
        return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;

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
    if ((!initial_mode &&
         mailbox->item_kind != VXML_CMETA_COLLECT_ITEM_FIELD) ||
        (initial_mode &&
         mailbox->item_kind != VXML_CMETA_COLLECT_ITEM_INITIAL))
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
            slot->data == NULL || slot->value == NULL)
            goto incompatible;

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

        if (initial_mode) {
            size_t field_offset;
            bool same_form = false;
            if (!range_valid(
                    form->first_field, form->field_count,
                    program->field_count) ||
                program->fields == NULL)
                goto incompatible;
            for (field_offset = 0u;
                 field_offset < form->field_count;
                 ++field_offset) {
                const vxml_cmeta_field_row *candidate =
                    &program->fields[form->first_field + field_offset];
                if (candidate->form != profile->active_form)
                    goto incompatible;
                if (candidate->root_field == root_field) {
                    same_form = true;
                    break;
                }
            }
            if (!same_form) goto incompatible;
        }

        type = target->value->storage_type;
        if (type == NULL ||
            type->size > mailbox->storage_stride)
            goto incompatible;
        for (prior = 0u; prior < slot_index; ++prior)
            if (mailbox->root_fields[prior] == root_field)
                goto incompatible;
        if (!initial_mode && root_field == selected->root_field)
            ++selected_count;

        mailbox->root_fields[slot_index] = root_field;
        memcpy(
            mailbox->storage +
                slot_index * mailbox->storage_stride,
            slot->value, type->size);
    }

    if (!initial_mode && selected_count != 1u)
        goto incompatible;
    if (atomic_load_explicit(
            &mailbox->generation, memory_order_relaxed) !=
            completion->generation)
        goto stale_after_claim;

    mailbox->slot_count = completion->slot_count;
    mailbox->data = initial_mode ? NULL : selected->field_data;
    collect_recording_payload_reset(mailbox, false);
    if (recording_present) {
        memcpy(
            mailbox->recording_media_type,
            completion->recording_media_type.data,
            completion->recording_media_type.size);
        mailbox->recording_media_type_size =
            completion->recording_media_type.size;
        mailbox->recording_duration_us =
            completion->recording_duration_us;
        mailbox->recording = completion->recording;
    }
    expected = VXML_CMETA_COLLECT_MAILBOX_WRITING;
    if (!atomic_compare_exchange_strong_explicit(
            &mailbox->state, &expected,
            VXML_CMETA_COLLECT_MAILBOX_READY,
            memory_order_acq_rel, memory_order_acquire)) {
        /*
         * READY was not published, so ownership never transferred. Clear the
         * borrowed copy without invoking release; producer still owns it.
         */
        collect_recording_payload_reset(mailbox, false);
        return expected == VXML_CMETA_COLLECT_MAILBOX_CLOSED
            ? VXML_CMETA_COLLECT_INGRESS_CLOSED
            : VXML_CMETA_COLLECT_INGRESS_FULL;
    }
    return VXML_CMETA_COLLECT_INGRESS_ACCEPTED;

stale_after_claim:
    collect_recording_payload_reset(mailbox, false);
    mailbox->slot_count = 0u;
    atomic_store_explicit(
        &mailbox->state,
        VXML_CMETA_COLLECT_MAILBOX_EMPTY,
        memory_order_release);
    return VXML_CMETA_COLLECT_INGRESS_STALE;

incompatible:
    collect_recording_payload_reset(mailbox, false);
    mailbox->slot_count = 0u;
    atomic_store_explicit(
        &mailbox->state,
        VXML_CMETA_COLLECT_MAILBOX_EMPTY,
        memory_order_release);
    return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;
}

vxml_status vxml_session_cmeta_collect_utterance_result(
    const vxml_session *session,
    vxml_cmeta_collect_utterance_result_view_v1 *out_result) {
    const vxml_session_impl *impl;
    const vxml_cmeta_session_data *profile;
    const vxml_cmeta_collect_utterance_result_slot *result;
    if (out_result != NULL)
        *out_result =
            (vxml_cmeta_collect_utterance_result_view_v1){0};
    if (session == NULL || out_result == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (const vxml_session_impl *)session->impl;
    if (impl == NULL || impl->state == VXML_SESSION_CLOSED)
        return VXML_CLOSED;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    result = &profile->collect_utterance_result;
    if (!result->live ||
        result->recording.data == NULL ||
        result->recording.size == 0u ||
        result->recording.lease == NULL ||
        result->media_type == NULL ||
        result->media_type_size == 0u)
        return VXML_INVALID_STATE;
    *out_result = (vxml_cmeta_collect_utterance_result_view_v1){
        .abi_version =
            VXML_CMETA_COLLECT_UTTERANCE_RESULT_VIEW_ABI_V1,
        .struct_size =
            sizeof(vxml_cmeta_collect_utterance_result_view_v1),
        .duration_us = result->duration_us,
        .media_type = {
            result->media_type, result->media_type_size},
        .data = result->recording.data,
        .size = result->recording.size
    };
    return VXML_OK;
}

typedef enum vxml_cmeta_recording_shadow_path_kind {
    VXML_CMETA_RECORDING_SHADOW_INVALID = 0,
    VXML_CMETA_RECORDING_SHADOW_APP_RECORDING,
    VXML_CMETA_RECORDING_SHADOW_APP_SIZE,
    VXML_CMETA_RECORDING_SHADOW_APP_DURATION,
    VXML_CMETA_RECORDING_SHADOW_FIELD_RECORDING,
    VXML_CMETA_RECORDING_SHADOW_FIELD_SIZE,
    VXML_CMETA_RECORDING_SHADOW_FIELD_DURATION
} vxml_cmeta_recording_shadow_path_kind;

typedef struct vxml_cmeta_recording_shadow_path {
    vxml_cmeta_recording_shadow_path_kind kind;
    size_t field;
} vxml_cmeta_recording_shadow_path;

static bool recording_shadow_suffix(
    const char *path, size_t path_size,
    const char *suffix, size_t suffix_size,
    size_t *out_owner_size) {
    if (out_owner_size != NULL) *out_owner_size = 0u;
    if (path == NULL || suffix == NULL ||
        path_size <= suffix_size ||
        memcmp(path + path_size - suffix_size,
               suffix, suffix_size) != 0)
        return false;
    if (out_owner_size != NULL)
        *out_owner_size = path_size - suffix_size;
    return true;
}

static vxml_status resolve_recording_shadow_path(
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_session_data *profile,
    const char *path, size_t path_size,
    vxml_cmeta_recording_shadow_path *out) {
    static const char app_recording[] =
        "application.lastresult$.recording";
    static const char app_size[] =
        "application.lastresult$.recordingsize";
    static const char app_duration[] =
        "application.lastresult$.recordingduration";
    static const char field_recording_suffix[] = "$.recording";
    static const char field_size_suffix[] = "$.recordingsize";
    static const char field_duration_suffix[] = "$.recordingduration";
    size_t owner_size = 0u;
    size_t offset;
    vxml_cmeta_recording_shadow_path_kind field_kind =
        VXML_CMETA_RECORDING_SHADOW_INVALID;

    if (out != NULL)
        *out = (vxml_cmeta_recording_shadow_path){
            VXML_CMETA_RECORDING_SHADOW_INVALID,
            VXML_CMETA_NO_INDEX};
    if (program == NULL || profile == NULL ||
        path == NULL || path_size == 0u || out == NULL ||
        memchr(path, '\0', path_size) != NULL)
        return VXML_INVALID_ARGUMENT;

    if (path_size == sizeof(app_recording) - 1u &&
        memcmp(path, app_recording, path_size) == 0) {
        out->kind = VXML_CMETA_RECORDING_SHADOW_APP_RECORDING;
        return VXML_OK;
    }
    if (path_size == sizeof(app_size) - 1u &&
        memcmp(path, app_size, path_size) == 0) {
        out->kind = VXML_CMETA_RECORDING_SHADOW_APP_SIZE;
        return VXML_OK;
    }
    if (path_size == sizeof(app_duration) - 1u &&
        memcmp(path, app_duration, path_size) == 0) {
        out->kind = VXML_CMETA_RECORDING_SHADOW_APP_DURATION;
        return VXML_OK;
    }

    if (recording_shadow_suffix(
            path, path_size,
            field_recording_suffix,
            sizeof(field_recording_suffix) - 1u,
            &owner_size))
        field_kind = VXML_CMETA_RECORDING_SHADOW_FIELD_RECORDING;
    else if (recording_shadow_suffix(
                 path, path_size,
                 field_size_suffix,
                 sizeof(field_size_suffix) - 1u,
                 &owner_size))
        field_kind = VXML_CMETA_RECORDING_SHADOW_FIELD_SIZE;
    else if (recording_shadow_suffix(
                 path, path_size,
                 field_duration_suffix,
                 sizeof(field_duration_suffix) - 1u,
                 &owner_size))
        field_kind = VXML_CMETA_RECORDING_SHADOW_FIELD_DURATION;
    else
        return VXML_INVALID_ARGUMENT;

    if (owner_size == 0u ||
        profile->active_form == VXML_CMETA_NO_INDEX ||
        profile->active_form >= program->form_count ||
        program->forms == NULL)
        return VXML_INVALID_STATE;

    {
        const vxml_cmeta_form_row *form =
            &program->forms[profile->active_form];
        if (!range_valid(
                form->first_field, form->field_count,
                program->field_count) ||
            (form->field_count != 0u && program->fields == NULL))
            return VXML_INVALID_STRUCTURE;
        for (offset = 0u; offset < form->field_count; ++offset) {
            const size_t field_index = form->first_field + offset;
            const vxml_cmeta_field_row *field =
                &program->fields[field_index];
            if (field->name != NULL &&
                field->name_size == owner_size &&
                memcmp(field->name, path, owner_size) == 0) {
                out->kind = field_kind;
                out->field = field_index;
                return VXML_OK;
            }
        }
    }
    return VXML_INVALID_ARGUMENT;
}

vxml_status vxml_session_cmeta_recording_shadow_value(
    const vxml_session *session,
    const char *path,
    size_t path_size,
    vxml_cmeta_value_view *out_value) {
    const vxml_session_impl *impl;
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_session_data *profile;
    vxml_cmeta_recording_shadow_path resolved;
    const vxml_cmeta_field_recording_shadow *field_shadow = NULL;
    bool defined = false;
    uint64_t value = UINT64_C(0);
    vxml_status status;

    if (out_value != NULL)
        *out_value = (vxml_cmeta_value_view){
            .kind = VXML_CMETA_VALUE_UNDEFINED};
    if (session == NULL || path == NULL || path_size == 0u ||
        out_value == NULL || memchr(path, '\0', path_size) != NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (const vxml_session_impl *)session->impl;
    if (impl == NULL || impl->state == VXML_SESSION_CLOSED)
        return VXML_CLOSED;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    program = (const vxml_cmeta_program_data *)
        impl->program->profile_data;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    status = resolve_recording_shadow_path(
        program, profile, path, path_size, &resolved);
    if (status != VXML_OK) return status;

    switch (resolved.kind) {
    case VXML_CMETA_RECORDING_SHADOW_APP_SIZE:
        if (profile->collect_utterance_result.live) {
            value = (uint64_t)
                profile->collect_utterance_result.recording.size;
            defined = true;
        }
        break;
    case VXML_CMETA_RECORDING_SHADOW_APP_DURATION:
        if (profile->collect_utterance_result.live) {
            value =
                profile->collect_utterance_result.duration_us /
                UINT64_C(1000);
            defined = true;
        }
        break;
    case VXML_CMETA_RECORDING_SHADOW_FIELD_SIZE:
    case VXML_CMETA_RECORDING_SHADOW_FIELD_DURATION:
        if (profile->field_recording_shadows == NULL ||
            resolved.field >= profile->field_recording_shadow_count)
            return VXML_INVALID_STATE;
        field_shadow =
            &profile->field_recording_shadows[resolved.field];
        if (field_shadow->assigned && field_shadow->has_recording) {
            value = resolved.kind ==
                    VXML_CMETA_RECORDING_SHADOW_FIELD_SIZE
                ? (uint64_t)field_shadow->size
                : field_shadow->duration_ms;
            defined = true;
        }
        break;
    case VXML_CMETA_RECORDING_SHADOW_APP_RECORDING:
    case VXML_CMETA_RECORDING_SHADOW_FIELD_RECORDING:
        return VXML_INVALID_ARGUMENT;
    default:
        return VXML_INVALID_STRUCTURE;
    }

    if (defined) {
        out_value->kind = VXML_CMETA_VALUE_UINT;
        out_value->data.uint_value = value;
    }
    return VXML_OK;
}

vxml_status vxml_session_cmeta_recording_shadow(
    const vxml_session *session,
    const char *path,
    size_t path_size,
    vxml_cmeta_recording_ref_view_v1 *out_recording) {
    const vxml_session_impl *impl;
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_session_data *profile;
    const vxml_cmeta_collect_utterance_result_slot *result;
    const vxml_cmeta_field_recording_shadow *field_shadow = NULL;
    vxml_cmeta_recording_shadow_path resolved;
    vxml_status status;

    if (out_recording != NULL)
        *out_recording = (vxml_cmeta_recording_ref_view_v1){0};
    if (session == NULL || path == NULL || path_size == 0u ||
        out_recording == NULL || memchr(path, '\0', path_size) != NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (const vxml_session_impl *)session->impl;
    if (impl == NULL || impl->state == VXML_SESSION_CLOSED)
        return VXML_CLOSED;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    program = (const vxml_cmeta_program_data *)
        impl->program->profile_data;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    status = resolve_recording_shadow_path(
        program, profile, path, path_size, &resolved);
    if (status != VXML_OK) return status;
    if (resolved.kind != VXML_CMETA_RECORDING_SHADOW_APP_RECORDING &&
        resolved.kind != VXML_CMETA_RECORDING_SHADOW_FIELD_RECORDING)
        return VXML_INVALID_ARGUMENT;

    result = &profile->collect_utterance_result;
    if (!result->live || result->recording.data == NULL ||
        result->recording.size == 0u || result->recording.lease == NULL ||
        result->media_type == NULL || result->media_type_size == 0u)
        return VXML_INVALID_STATE;

    if (resolved.kind ==
            VXML_CMETA_RECORDING_SHADOW_FIELD_RECORDING) {
        if (profile->field_recording_shadows == NULL ||
            resolved.field >= profile->field_recording_shadow_count)
            return VXML_INVALID_STATE;
        field_shadow =
            &profile->field_recording_shadows[resolved.field];
        if (!field_shadow->assigned ||
            !field_shadow->has_recording ||
            field_shadow->generation != result->generation)
            return VXML_INVALID_STATE;
    }

    *out_recording = (vxml_cmeta_recording_ref_view_v1){
        .abi_version = VXML_CMETA_RECORDING_REF_VIEW_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_recording_ref_view_v1),
        .media_type = {
            result->media_type, result->media_type_size},
        .data = result->recording.data,
        .size = result->recording.size,
        .duration_ms = result->duration_us / UINT64_C(1000)
    };
    return VXML_OK;
}

static bool root_field_list_contains(
    const size_t *root_fields, size_t root_field_count,
    size_t root_field) {
    size_t index;
    if (root_field_count != 0u && root_fields == NULL)
        return false;
    for (index = 0u; index < root_field_count; ++index)
        if (root_fields[index] == root_field)
            return true;
    return false;
}

static bool completion_contains_root_field(
    const vxml_cmeta_collect_mailbox *mailbox,
    size_t root_field) {
    if (mailbox == NULL)
        return false;
    return root_field_list_contains(
        mailbox->root_fields, mailbox->slot_count, root_field);
}

static vxml_status filled_should_run_for_roots(
    const vxml_cmeta_session_data *profile,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    const vxml_cmeta_filled_row *filled,
    const size_t *completed_root_fields,
    size_t completed_root_field_count,
    bool *out) {
    const cmeta_data_struct_shape *root_shape;
    size_t index;
    if (out == NULL)
        return VXML_INVALID_ARGUMENT;
    *out = false;
    if (profile == NULL || program == NULL || form == NULL ||
        filled == NULL ||
        (completed_root_field_count != 0u &&
         completed_root_fields == NULL) ||
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
    root_shape = session_root_shape(program);
    if (root_shape == NULL)
        return VXML_INVALID_STRUCTURE;

    if (filled->mode == VXML_CMETA_FILLED_ALL) {
        for (index = 0u; index < filled->target_count; ++index) {
            const size_t root_field =
                program->filled_root_fields[
                    filled->first_target + index];
            if (root_field >= root_shape->field_count)
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
            if (root_field >= root_shape->field_count)
                return VXML_INVALID_STRUCTURE;
            if (root_field_list_contains(
                    completed_root_fields,
                    completed_root_field_count,
                    root_field)) {
                *out = true;
                return VXML_OK;
            }
        }
        return VXML_OK;
    }
    return VXML_INVALID_STRUCTURE;
}

static vxml_status filled_should_run(
    const vxml_cmeta_session_data *profile,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    const vxml_cmeta_filled_row *filled,
    const vxml_cmeta_collect_mailbox *mailbox,
    bool *out) {
    if (mailbox == NULL)
        return VXML_INVALID_STRUCTURE;
    return filled_should_run_for_roots(
        profile, program, form, filled,
        mailbox->root_fields, mailbox->slot_count, out);
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
    terminal_pending_reset(profile);
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
            if (status != VXML_OK || profile->exit_requested ||
                profile->pending_navigation_uri != NULL)
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
        if (status != VXML_OK || profile->exit_requested ||
            profile->pending_navigation_uri != NULL)
            return status;
    }
    return VXML_OK;
}


static vxml_status execute_record_filled_process(
    vxml_cmeta_session_data *profile,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    const vxml_cmeta_record_row *record) {
    const vxml_cmeta_filled_row *filled;
    if (profile == NULL || program == NULL || form == NULL ||
        record == NULL ||
        record->form != profile->active_form)
        return VXML_INVALID_STRUCTURE;
    if (record->filled == VXML_CMETA_NO_INDEX)
        return VXML_OK;
    if (record->filled >= program->filled_count ||
        program->filled == NULL)
        return VXML_INVALID_STRUCTURE;
    filled = &program->filled[record->filled];
    if (filled->form != profile->active_form ||
        filled->mode != VXML_CMETA_FILLED_FIELD ||
        filled->field != VXML_CMETA_NO_INDEX)
        return VXML_INVALID_STRUCTURE;
    return execute_filled_handler(
        profile, program, form, filled);
}


static vxml_status execute_subdialog_filled_process(
    vxml_cmeta_session_data *profile,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    const vxml_cmeta_subdialog_row *subdialog) {
    const cmeta_data_struct_shape *root_shape =
        session_root_shape(program);
    const size_t completed_root_field =
        subdialog != NULL ? subdialog->root_field : VXML_CMETA_NO_INDEX;
    size_t offset;
    vxml_status status;
    bool run = false;

    if (profile == NULL || program == NULL || form == NULL ||
        subdialog == NULL || root_shape == NULL ||
        subdialog->form != profile->active_form ||
        completed_root_field >= root_shape->field_count)
        return VXML_INVALID_STRUCTURE;

    if (subdialog->filled != VXML_CMETA_NO_INDEX) {
        const vxml_cmeta_filled_row *filled;
        if (subdialog->filled >= program->filled_count ||
            program->filled == NULL)
            return VXML_INVALID_STRUCTURE;
        filled = &program->filled[subdialog->filled];
        if (filled->form != profile->active_form ||
            filled->mode != VXML_CMETA_FILLED_FIELD ||
            filled->field != VXML_CMETA_NO_INDEX)
            return VXML_INVALID_STRUCTURE;
        status = execute_filled_handler(
            profile, program, form, filled);
        if (status != VXML_OK || profile->exit_requested ||
            profile->pending_navigation_uri != NULL)
            return status;
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
        status = filled_should_run_for_roots(
            profile, program, form, filled,
            &completed_root_field, 1u, &run);
        if (status != VXML_OK) return status;
        if (!run) continue;
        status = execute_filled_handler(
            profile, program, form, filled);
        if (status != VXML_OK || profile->exit_requested ||
            profile->pending_navigation_uri != NULL)
            return status;
    }
    return VXML_OK;
}


vxml_cmeta_collect_ingress_result vxml_session_cmeta_menu_try_complete(
    vxml_session *session,
    const vxml_cmeta_menu_completion_v1 *completion) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_menu_row *menu;
    vxml_cmeta_collect_mailbox *mailbox;
    uint64_t generation;
    unsigned state;
    unsigned expected;

    if (session == NULL || completion == NULL ||
        completion->abi_version != VXML_CMETA_MENU_COMPLETION_ABI_V1 ||
        completion->struct_size < sizeof(*completion) ||
        completion->generation == 0u)
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
    if (mailbox->item_kind != VXML_CMETA_COLLECT_ITEM_MENU ||
        profile->active_field != VXML_CMETA_NO_INDEX)
        return VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT;
    if (profile->active_menu == VXML_CMETA_NO_INDEX ||
        profile->active_menu >= program->menu_count ||
        program->menus == NULL)
        return VXML_CMETA_COLLECT_INGRESS_STALE;
    menu = &program->menus[profile->active_menu];
    if (menu->form != profile->active_form ||
        completion->choice_index >= menu->choice_count)
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
        mailbox->item_kind != VXML_CMETA_COLLECT_ITEM_MENU ||
        profile->active_menu == VXML_CMETA_NO_INDEX ||
        profile->active_menu >= program->menu_count ||
        program->menus == NULL ||
        completion->choice_index >=
            program->menus[profile->active_menu].choice_count) {
        expected = VXML_CMETA_COLLECT_MAILBOX_WRITING;
        if (!atomic_compare_exchange_strong_explicit(
                &mailbox->state, &expected,
                VXML_CMETA_COLLECT_MAILBOX_DISARMED,
                memory_order_acq_rel, memory_order_acquire) &&
            expected == VXML_CMETA_COLLECT_MAILBOX_CLOSED)
            return VXML_CMETA_COLLECT_INGRESS_CLOSED;
        return VXML_CMETA_COLLECT_INGRESS_STALE;
    }

    mailbox->choice_index = completion->choice_index;
    expected = VXML_CMETA_COLLECT_MAILBOX_WRITING;
    if (!atomic_compare_exchange_strong_explicit(
            &mailbox->state, &expected,
            VXML_CMETA_COLLECT_MAILBOX_READY,
            memory_order_acq_rel, memory_order_acquire)) {
        if (expected == VXML_CMETA_COLLECT_MAILBOX_CLOSED)
            return VXML_CMETA_COLLECT_INGRESS_CLOSED;
        if (expected == VXML_CMETA_COLLECT_MAILBOX_DISARMED)
            return VXML_CMETA_COLLECT_INGRESS_STALE;
        return VXML_CMETA_COLLECT_INGRESS_FULL;
    }
    return VXML_CMETA_COLLECT_INGRESS_ACCEPTED;
}


static vxml_status mark_initial_controls_filled(
    vxml_cmeta_session_data *profile,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form) {
    size_t offset;
    const bool completed = true;
    cmeta_scope_view *scope;
    if (profile == NULL || program == NULL || form == NULL ||
        form->scope >= program->scope_count ||
        profile->staged_scopes == NULL ||
        !range_valid(
            form->first_initial, form->initial_count,
            program->initial_count) ||
        form->initial_count == 0u ||
        program->initials == NULL)
        return VXML_INVALID_STRUCTURE;
    scope = &profile->staged_scopes[form->scope].view;
    for (offset = 0u; offset < form->initial_count; ++offset) {
        const vxml_cmeta_initial_row *initial =
            &program->initials[form->first_initial + offset];
        if (initial->form != profile->active_form ||
            initial->form_item_slot >= scope->schema->slot_count)
            return VXML_INVALID_STRUCTURE;
        if (!cmeta_scope_view_assign(
                scope, initial->form_item_slot, &completed))
            return VXML_ALLOCATION_FAILED;
    }
    return VXML_OK;
}

static vxml_status execute_initial_filled_process(
    vxml_cmeta_session_data *profile,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_form_row *form,
    const vxml_cmeta_collect_mailbox *mailbox) {
    size_t offset;
    vxml_status status;
    bool run = false;

    if (profile == NULL || program == NULL || form == NULL ||
        mailbox == NULL ||
        !range_valid(
            form->first_field, form->field_count,
            program->field_count) ||
        form->field_count == 0u ||
        program->fields == NULL)
        return VXML_INVALID_STRUCTURE;

    for (offset = 0u; offset < form->field_count; ++offset) {
        const vxml_cmeta_field_row *field =
            &program->fields[form->first_field + offset];
        if (field->form != profile->active_form)
            return VXML_INVALID_STRUCTURE;
        if (!completion_contains_root_field(mailbox, field->root_field))
            continue;
        if (field->filled == VXML_CMETA_NO_INDEX)
            continue;
        if (field->filled >= program->filled_count ||
            program->filled == NULL)
            return VXML_INVALID_STRUCTURE;
        status = execute_filled_handler(
            profile, program, form,
            &program->filled[field->filled]);
        if (status != VXML_OK || profile->exit_requested ||
            profile->pending_navigation_uri != NULL)
            return status;
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
        if (status != VXML_OK || profile->exit_requested ||
            profile->pending_navigation_uri != NULL)
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
        program->forms == NULL)
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
    form = &program->forms[profile->active_form];

    if (mailbox->record_utterance_expected)
        collect_quiesce_generation(profile, generation);

    if (generation != profile->collect_generation) {
        profile->collect_in_flight = false;
        collect_recording_payload_reset(mailbox, true);
        mailbox->record_utterance_expected = false;
        mailbox->max_recording_duration_us = UINT64_C(0);
        mailbox->data = NULL;
        mailbox->slot_count = 0u;
        mailbox->choice_index = SIZE_MAX;
        mailbox->item_kind = VXML_CMETA_COLLECT_ITEM_FIELD;
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_COLLECT_MAILBOX_DISARMED,
            memory_order_release);
        return session_fail(impl, VXML_INVALID_STRUCTURE);
    }

    if (mailbox->item_kind == VXML_CMETA_COLLECT_ITEM_MENU) {
        const vxml_cmeta_menu_row *menu;
        const vxml_cmeta_menu_choice_target_row *target;
        size_t absolute_choice;
        if (profile->active_field != VXML_CMETA_NO_INDEX ||
            profile->active_menu == VXML_CMETA_NO_INDEX ||
            profile->active_menu >= program->menu_count ||
            form->menu != profile->active_menu ||
            program->menus == NULL ||
            program->menu_choice_targets == NULL) {
            profile->collect_in_flight = false;
            mailbox->choice_index = SIZE_MAX;
            mailbox->item_kind = VXML_CMETA_COLLECT_ITEM_FIELD;
            atomic_store_explicit(
                &mailbox->state,
                VXML_CMETA_COLLECT_MAILBOX_DISARMED,
                memory_order_release);
            return session_fail(impl, VXML_INVALID_STRUCTURE);
        }
        menu = &program->menus[profile->active_menu];
        if (menu->form != profile->active_form ||
            menu->choice_count == 0u ||
            !range_valid(
                menu->first_choice, menu->choice_count,
                program->menu_choice_count) ||
            mailbox->choice_index >= menu->choice_count) {
            profile->collect_in_flight = false;
            mailbox->choice_index = SIZE_MAX;
            mailbox->item_kind = VXML_CMETA_COLLECT_ITEM_FIELD;
            atomic_store_explicit(
                &mailbox->state,
                VXML_CMETA_COLLECT_MAILBOX_DISARMED,
                memory_order_release);
            return session_fail(impl, VXML_INVALID_STRUCTURE);
        }
        absolute_choice = menu->first_choice + mailbox->choice_index;
        target = &program->menu_choice_targets[absolute_choice];

        profile->collect_in_flight = false;
        collect_recording_payload_reset(mailbox, true);
        mailbox->record_utterance_expected = false;
        mailbox->max_recording_duration_us = UINT64_C(0);
        mailbox->data = NULL;
        mailbox->slot_count = 0u;
        mailbox->choice_index = SIZE_MAX;
        mailbox->item_kind = VXML_CMETA_COLLECT_ITEM_FIELD;
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_COLLECT_MAILBOX_DISARMED,
            memory_order_release);

        if (target->target == NULL || target->target_size == 0u)
            return session_fail(impl, VXML_INVALID_STRUCTURE);
        if (target->kind == VXML_CMETA_MENU_CHOICE_EVENT) {
            status = vxml_session_cmeta_raise(
                session, target->target, target->target_size);
            if (status != VXML_OK) return status;
            if (impl->state == VXML_SESSION_RUNNING) {
                ++profile->collect_generation;
                if (profile->collect_generation == 0u)
                    profile->collect_generation = 1u;
            }
            return VXML_OK;
        }
        if (target->kind == VXML_CMETA_MENU_CHOICE_NEXT) {
            impl->navigation_uri = target->target;
            impl->navigation_uri_size = target->target_size;
            impl->navigation_fetchaudio_uri = NULL;
            impl->navigation_fetchaudio_uri_size = 0u;
            impl->state = VXML_SESSION_NAVIGATING;
            impl->error = VXML_OK;
            return VXML_OK;
        }
        return session_fail(impl, VXML_INVALID_STRUCTURE);
    }

    {
        const bool initial_mode =
            mailbox->item_kind == VXML_CMETA_COLLECT_ITEM_INITIAL;
        if ((!initial_mode &&
             mailbox->item_kind != VXML_CMETA_COLLECT_ITEM_FIELD) ||
            profile->active_menu != VXML_CMETA_NO_INDEX)
            return session_fail(impl, VXML_INVALID_STRUCTURE);

        if (initial_mode) {
            const vxml_cmeta_initial_row *initial;
            if (profile->active_field != VXML_CMETA_NO_INDEX ||
                profile->active_initial == VXML_CMETA_NO_INDEX ||
                profile->active_initial >= program->initial_count ||
                program->initials == NULL)
                return session_fail(impl, VXML_INVALID_STRUCTURE);
            initial = &program->initials[profile->active_initial];
            if (initial->form != profile->active_form ||
                form->initial_count == 0u)
                return session_fail(impl, VXML_INVALID_STRUCTURE);
            field = NULL;
        } else {
            if (profile->active_initial != VXML_CMETA_NO_INDEX ||
                profile->active_field >= program->field_count ||
                program->fields == NULL)
                return session_fail(impl, VXML_INVALID_STRUCTURE);
            field = &program->fields[profile->active_field];
            if (field->form != profile->active_form ||
                mailbox->record_utterance_expected !=
                    form->record_utterance)
                return session_fail(impl, VXML_INVALID_STRUCTURE);
        }

        root_shape = session_root_shape(program);
        if (root_shape == NULL ||
            mailbox->root_fields == NULL ||
            mailbox->slot_count == 0u ||
            mailbox->slot_count > mailbox->slot_capacity ||
            mailbox->storage == NULL ||
            mailbox->storage_stride == 0u) {
            profile->collect_in_flight = false;
            collect_recording_payload_reset(mailbox, true);
            mailbox->record_utterance_expected = false;
            mailbox->max_recording_duration_us = UINT64_C(0);
            mailbox->data = NULL;
            mailbox->slot_count = 0u;
            mailbox->item_kind = VXML_CMETA_COLLECT_ITEM_FIELD;
            atomic_store_explicit(
                &mailbox->state,
                VXML_CMETA_COLLECT_MAILBOX_DISARMED,
                memory_order_release);
            return session_fail(impl, VXML_INVALID_STRUCTURE);
        }

        if (!transaction_begin(profile, program)) {
            profile->collect_in_flight = false;
            collect_recording_payload_reset(mailbox, true);
            mailbox->record_utterance_expected = false;
            mailbox->max_recording_duration_us = UINT64_C(0);
            mailbox->data = NULL;
            mailbox->slot_count = 0u;
            mailbox->item_kind = VXML_CMETA_COLLECT_ITEM_FIELD;
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
                    goto invalid_ready;
                }
                target = &root_shape->fields[root_field];
                type = target->value != NULL
                    ? target->value->storage_type : NULL;
                if (!collect_fixed_scalar_data(target->value) ||
                    type == NULL ||
                    type->size > mailbox->storage_stride ||
                    profile->staged_root.bound[root_field] != 0u) {
                    transaction_reset(profile, program);
                    goto semantic_ready;
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
                    mailbox->item_kind = VXML_CMETA_COLLECT_ITEM_FIELD;
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
                if (!initial_mode &&
                    root_field == field->root_field)
                    selected_seen = true;
            }
            if (!initial_mode && !selected_seen) {
                transaction_reset(profile, program);
                goto invalid_ready;
            }
        }

        if (initial_mode) {
            status = mark_initial_controls_filled(
                profile, program, form);
            if (status == VXML_OK)
                status = execute_initial_filled_process(
                    profile, program, form, mailbox);
        } else {
            status = execute_filled_process(
                profile, program, form, mailbox);
        }
        if (status != VXML_OK) {
            transaction_reset(profile, program);
            profile->collect_in_flight = false;
            collect_recording_payload_reset(mailbox, true);
            mailbox->record_utterance_expected = false;
            mailbox->max_recording_duration_us = UINT64_C(0);
            mailbox->data = NULL;
            mailbox->slot_count = 0u;
            mailbox->item_kind = VXML_CMETA_COLLECT_ITEM_FIELD;
            atomic_store_explicit(
                &mailbox->state,
                VXML_CMETA_COLLECT_MAILBOX_DISARMED,
                memory_order_release);
            return session_fail(impl, status);
        }

        {
            size_t slot_index;
            for (slot_index = 0u;
                 slot_index < mailbox->slot_count;
                 ++slot_index)
                mark_form_retry_reset_by_root(
                    profile, program, form,
                    mailbox->root_fields[slot_index]);
        }

        transaction_commit(profile, program);

        /*
         * Every committed recognition replaces application.lastresult$.
         * Advance a bounded generation even when no utterance recording was
         * collected so old field recording aliases can never expose freed
         * media after a later recognition.
         */
        ++profile->collect_lastresult_generation;
        if (profile->collect_lastresult_generation == UINT64_C(0))
            profile->collect_lastresult_generation = UINT64_C(1);

        /*
         * transaction_commit() cleared the prior field shadow through the
         * retry-reset path. Publish this field's scalar snapshot only after
         * the recognition transaction is durable.
         */
        if (mailbox->record_utterance_expected &&
            field != NULL &&
            field->root_field < root_shape->field_count &&
            profile->committed_root.bound != NULL &&
            profile->committed_root.bound[field->root_field] != 0u &&
            profile->active_field < program->field_count &&
            profile->field_recording_shadows != NULL &&
            profile->active_field <
                profile->field_recording_shadow_count) {
            vxml_cmeta_field_recording_shadow *shadow =
                &profile->field_recording_shadows[
                    profile->active_field];
            *shadow = (vxml_cmeta_field_recording_shadow){
                .assigned = true,
                .has_recording =
                    mailbox->recording.lease != NULL &&
                    mailbox->recording.data != NULL &&
                    mailbox->recording.size != 0u,
                .generation =
                    profile->collect_lastresult_generation,
                .size = mailbox->recording.size,
                .duration_ms =
                    mailbox->recording_duration_us /
                    UINT64_C(1000)
            };
        }

        /*
         * A committed recognition replaces the prior application last-result
         * recording. The scalar transaction is already durable; only now may
         * the Session adopt the ACCEPTED media lease from the mailbox.
         */
        collect_utterance_result_reset(profile);
        profile->collect_utterance_result.generation =
            profile->collect_lastresult_generation;
        if (mailbox->record_utterance_expected &&
            mailbox->recording.lease != NULL) {
            vxml_cmeta_collect_utterance_result_slot *result =
                &profile->collect_utterance_result;
            if (mailbox->recording_media_type_size == 0u ||
                result->media_type == NULL ||
                mailbox->recording_media_type_size >
                    result->media_type_capacity) {
                collect_recording_payload_reset(mailbox, true);
                mailbox->record_utterance_expected = false;
                mailbox->max_recording_duration_us = UINT64_C(0);
                profile->collect_in_flight = false;
                atomic_store_explicit(
                    &mailbox->state,
                    VXML_CMETA_COLLECT_MAILBOX_DISARMED,
                    memory_order_release);
                return session_fail(impl, VXML_INVALID_STRUCTURE);
            }
            memcpy(
                result->media_type,
                mailbox->recording_media_type,
                mailbox->recording_media_type_size);
            result->media_type_size =
                mailbox->recording_media_type_size;
            result->duration_us =
                mailbox->recording_duration_us;
            result->recording = mailbox->recording;
            result->live = true;
            mailbox->recording =
                (vxml_cmeta_recording_lease_v1){0};
        }

        profile->reprompt_requested = false;
        profile->handler_reprompt_requested = false;

        profile->collect_in_flight = false;
        collect_recording_payload_reset(mailbox, true);
        mailbox->record_utterance_expected = false;
        mailbox->max_recording_duration_us = UINT64_C(0);
        mailbox->data = NULL;
        mailbox->slot_count = 0u;
        mailbox->item_kind = VXML_CMETA_COLLECT_ITEM_FIELD;
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_COLLECT_MAILBOX_DISARMED,
            memory_order_release);

        if (profile->pending_navigation_uri != NULL) {
            status = publish_pending_navigation(
                impl, profile);
            if (status != VXML_OK)
                return session_fail(impl, status);
            return VXML_OK;
        }
        if (profile->exit_requested) {
            terminal_publish(profile);
            impl->state = VXML_SESSION_EXITED;
            impl->error = VXML_OK;
            return VXML_OK;
        }

        return select_directed_item(
            impl, program, profile, form, profile->active_form);

invalid_ready:
        profile->collect_in_flight = false;
        collect_recording_payload_reset(mailbox, true);
        mailbox->record_utterance_expected = false;
        mailbox->max_recording_duration_us = UINT64_C(0);
        mailbox->data = NULL;
        mailbox->slot_count = 0u;
        mailbox->item_kind = VXML_CMETA_COLLECT_ITEM_FIELD;
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_COLLECT_MAILBOX_DISARMED,
            memory_order_release);
        return session_fail(impl, VXML_INVALID_STRUCTURE);

semantic_ready:
        profile->collect_in_flight = false;
        collect_recording_payload_reset(mailbox, true);
        mailbox->record_utterance_expected = false;
        mailbox->max_recording_duration_us = UINT64_C(0);
        mailbox->data = NULL;
        mailbox->slot_count = 0u;
        mailbox->item_kind = VXML_CMETA_COLLECT_ITEM_FIELD;
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_COLLECT_MAILBOX_DISARMED,
            memory_order_release);
        return session_fail(impl, VXML_SEMANTIC_ERROR);
    }
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
        if (profile->active_subdialog != VXML_CMETA_NO_INDEX) {
            if (profile->active_subdialog >= program->subdialog_count ||
                program->subdialogs == NULL ||
                profile->active_initial != VXML_CMETA_NO_INDEX ||
                profile->active_field != VXML_CMETA_NO_INDEX ||
                profile->active_menu != VXML_CMETA_NO_INDEX ||
                profile->active_block != VXML_CMETA_NO_INDEX)
                return false;
            *out_kind = VXML_CMETA_EVENT_SUBDIALOG;
            *out_owner = profile->active_subdialog;
            return true;
        }
        if (profile->active_initial != VXML_CMETA_NO_INDEX) {
            if (profile->active_field != VXML_CMETA_NO_INDEX ||
                profile->active_record != VXML_CMETA_NO_INDEX ||
                profile->active_initial >= program->initial_count ||
                program->initials == NULL)
                return false;
            *out_kind = VXML_CMETA_EVENT_INITIAL;
            *out_owner = profile->active_initial;
            return true;
        }
        if (profile->active_transfer != VXML_CMETA_NO_INDEX) {
            if (profile->active_field != VXML_CMETA_NO_INDEX ||
                profile->active_initial != VXML_CMETA_NO_INDEX ||
                profile->active_subdialog != VXML_CMETA_NO_INDEX ||
                profile->active_record != VXML_CMETA_NO_INDEX ||
                profile->active_transfer >= program->transfer_count ||
                program->transfers == NULL)
                return false;
            *out_kind = VXML_CMETA_EVENT_TRANSFER;
            *out_owner = profile->active_transfer;
            return true;
        }
        if (profile->active_record != VXML_CMETA_NO_INDEX) {
            if (profile->active_field != VXML_CMETA_NO_INDEX ||
                profile->active_transfer != VXML_CMETA_NO_INDEX ||
                profile->active_record >= program->record_count ||
                program->records == NULL)
                return false;
            *out_kind = VXML_CMETA_EVENT_RECORD;
            *out_owner = profile->active_record;
            return true;
        }
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
               handler->scope_kind == VXML_CMETA_EVENT_FIELD ||
               handler->scope_kind == VXML_CMETA_EVENT_INITIAL ||
               handler->scope_kind == VXML_CMETA_EVENT_SUBDIALOG ||
               handler->scope_kind == VXML_CMETA_EVENT_RECORD ||
               handler->scope_kind == VXML_CMETA_EVENT_TRANSFER) {
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
    terminal_pending_reset(profile);
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
    if (profile->pending_navigation_uri != NULL) {
        status = publish_pending_navigation(
            impl, profile);
        profile->handler_reprompt_requested = false;
        exit_snapshot_destroy(&profile->pending_exit);
        return status;
    }
    if (profile->handler_reprompt_requested)
        profile->reprompt_requested = true;
    profile->handler_reprompt_requested = false;
    if (profile->exit_requested) {
        terminal_publish(profile);
        impl->state = VXML_SESSION_EXITED;
        impl->error = VXML_OK;
    } else {
        exit_snapshot_destroy(&profile->pending_exit);
    }
    return VXML_OK;
}

static vxml_status cmeta_raise_event_impl(
    vxml_session *session,
    const char *event_name,
    size_t event_name_size,
    bool allow_subdialog) {
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
    if (profile->active_subdialog != VXML_CMETA_NO_INDEX &&
        !allow_subdialog)
        return VXML_INVALID_STATE;
    if (profile->active_initial != VXML_CMETA_NO_INDEX &&
        (profile->active_initial >= program->initial_count ||
         program->initials == NULL ||
         profile->active_field != VXML_CMETA_NO_INDEX ||
         profile->active_menu != VXML_CMETA_NO_INDEX ||
         profile->active_block != VXML_CMETA_NO_INDEX))
        return session_fail(impl, VXML_INVALID_STRUCTURE);
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


static vxml_status subdialog_publish_global_exit(
    vxml_session_impl *impl,
    vxml_cmeta_session_data *profile,
    const vxml_cmeta_subdialog_completion_mailbox *mailbox) {
    size_t entry_capacity = 0u;
    size_t name_capacity = 0u;
    size_t string_capacity = 0u;
    size_t index;
    vxml_status status;

    if (impl == NULL || profile == NULL || mailbox == NULL)
        return VXML_INVALID_ARGUMENT;

    switch (mailbox->global_exit_kind) {
    case VXML_CMETA_SUBDIALOG_GLOBAL_NATURAL:
        profile->pending_terminal_kind = VXML_CMETA_TERMINAL_NONE;
        break;
    case VXML_CMETA_SUBDIALOG_GLOBAL_EXIT_EMPTY:
        profile->pending_terminal_kind = VXML_CMETA_TERMINAL_EXIT;
        break;
    case VXML_CMETA_SUBDIALOG_GLOBAL_DISCONNECT:
        profile->pending_terminal_kind = VXML_CMETA_TERMINAL_DISCONNECT;
        break;
    case VXML_CMETA_SUBDIALOG_GLOBAL_EXIT_EXPRESSION:
        if (mailbox->entry_count != 1u ||
            mailbox->entries == NULL ||
            mailbox->entries[0].name.data != NULL ||
            mailbox->entries[0].name.size != 0u)
            return VXML_INVALID_STRUCTURE;
        entry_capacity = 1u;
        if (mailbox->entries[0].value.kind ==
                VXML_CMETA_VALUE_STRING)
            string_capacity =
                mailbox->entries[0].value.data.string.size;
        profile->pending_terminal_kind = VXML_CMETA_TERMINAL_EXIT;
        break;
    case VXML_CMETA_SUBDIALOG_GLOBAL_EXIT_NAMELIST:
        if (mailbox->entry_count == 0u ||
            mailbox->entries == NULL)
            return VXML_INVALID_STRUCTURE;
        entry_capacity = mailbox->entry_count;
        for (index = 0u; index < mailbox->entry_count; ++index) {
            const vxml_cmeta_subdialog_result_entry_v1 *entry =
                &mailbox->entries[index];
            if (entry->name.data == NULL || entry->name.size == 0u ||
                name_capacity > SIZE_MAX - entry->name.size)
                return VXML_INVALID_STRUCTURE;
            name_capacity += entry->name.size;
            if (entry->value.kind == VXML_CMETA_VALUE_STRING) {
                const size_t bytes =
                    entry->value.data.string.size;
                if (string_capacity > SIZE_MAX - bytes)
                    return VXML_LIMIT_EXCEEDED;
                string_capacity += bytes;
            }
        }
        profile->pending_terminal_kind = VXML_CMETA_TERMINAL_EXIT;
        break;
    default:
        return VXML_INVALID_STRUCTURE;
    }

    status = exit_snapshot_prepare_capacity(
        &profile->pending_exit,
        entry_capacity, name_capacity, string_capacity);
    if (status != VXML_OK) return status;

    if (mailbox->global_exit_kind ==
            VXML_CMETA_SUBDIALOG_GLOBAL_EXIT_EXPRESSION) {
        status = exit_snapshot_append(
            &profile->pending_exit, NULL, 0u,
            &mailbox->entries[0].value);
        if (status != VXML_OK) goto failure;
        profile->pending_exit.kind =
            VXML_CMETA_EXIT_EXPRESSION;
    } else if (mailbox->global_exit_kind ==
                   VXML_CMETA_SUBDIALOG_GLOBAL_EXIT_NAMELIST) {
        for (index = 0u; index < mailbox->entry_count; ++index) {
            const vxml_cmeta_subdialog_result_entry_v1 *entry =
                &mailbox->entries[index];
            status = exit_snapshot_append(
                &profile->pending_exit,
                entry->name.data, entry->name.size,
                &entry->value);
            if (status != VXML_OK) goto failure;
        }
        profile->pending_exit.kind = VXML_CMETA_EXIT_NAMELIST;
    } else {
        profile->pending_exit.kind = VXML_CMETA_EXIT_EMPTY;
    }

    profile->pending_terminal_event = NULL;
    profile->pending_terminal_event_size = 0u;
    profile->exit_requested = true;
    terminal_publish(profile);
    impl->state = VXML_SESSION_EXITED;
    impl->error = VXML_OK;
    return VXML_OK;

failure:
    exit_snapshot_destroy(&profile->pending_exit);
    terminal_pending_reset(profile);
    return status;
}

static const cmeta_data_field_desc *subdialog_result_member_by_name(
    const cmeta_data_desc *result_data,
    vxml_cmeta_name_view name) {
    const cmeta_data_struct_shape *shape;
    size_t index;
    if (!cmeta_data_desc_valid(result_data) ||
        result_data->kind != CMETA_DATA_STRUCT ||
        result_data->storage_type == NULL ||
        result_data->shape == NULL ||
        name.data == NULL || name.size == 0u)
        return NULL;
    shape = (const cmeta_data_struct_shape *)result_data->shape;
    if (shape->field_count != 0u && shape->fields == NULL)
        return NULL;
    for (index = 0u; index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &shape->fields[index];
        size_t field_name_size;
        if (field->name == NULL || field->value == NULL ||
            field->value->storage_type == NULL)
            return NULL;
        field_name_size = strlen(field->name);
        if (field_name_size == name.size &&
            memcmp(field->name, name.data, name.size) == 0)
            return field;
    }
    return NULL;
}

static vxml_status subdialog_stage_return_data(
    vxml_cmeta_session_data *profile,
    const vxml_cmeta_program_data *program,
    const vxml_cmeta_subdialog_row *subdialog,
    const vxml_cmeta_subdialog_completion_mailbox *mailbox) {
    const cmeta_data_struct_shape *root_shape;
    const cmeta_data_struct_shape *result_shape;
    const cmeta_data_field_desc *root_field;
    unsigned char *result_object;
    cmeta_status meta_status;
    size_t index;

    if (profile == NULL || program == NULL || subdialog == NULL ||
        mailbox == NULL ||
        mailbox->kind != VXML_CMETA_SUBDIALOG_RETURN_DATA ||
        (mailbox->entry_count != 0u && mailbox->entries == NULL))
        return VXML_INVALID_STRUCTURE;
    root_shape = session_root_shape(program);
    if (root_shape == NULL ||
        subdialog->root_field >= root_shape->field_count ||
        !cmeta_data_desc_valid(subdialog->result_data) ||
        subdialog->result_data->kind != CMETA_DATA_STRUCT ||
        subdialog->result_data->storage_type == NULL ||
        subdialog->result_data->shape == NULL)
        return VXML_INVALID_STRUCTURE;
    root_field = &root_shape->fields[subdialog->root_field];
    if (root_field->value != subdialog->result_data ||
        root_field->offset != subdialog->field_offset ||
        subdialog->field_offset > program->root->storage_type->size ||
        subdialog->result_data->storage_type->size >
            program->root->storage_type->size - subdialog->field_offset)
        return VXML_INVALID_STRUCTURE;
    result_shape =
        (const cmeta_data_struct_shape *)subdialog->result_data->shape;
    if ((result_shape->field_count != 0u &&
         result_shape->fields == NULL) ||
        profile->staged_root.bound[subdialog->root_field] != 0u)
        return VXML_SEMANTIC_ERROR;

    for (index = 0u; index < mailbox->entry_count; ++index) {
        const vxml_cmeta_subdialog_result_entry_v1 *entry =
            &mailbox->entries[index];
        const cmeta_data_field_desc *member;
        size_t prior;
        if (entry->name.data == NULL || entry->name.size == 0u ||
            entry->value.kind == VXML_CMETA_VALUE_UNDEFINED)
            return VXML_SEMANTIC_ERROR;
        for (prior = 0u; prior < index; ++prior) {
            const vxml_cmeta_name_view previous =
                mailbox->entries[prior].name;
            if (previous.size == entry->name.size &&
                previous.data != NULL &&
                memcmp(
                    previous.data, entry->name.data,
                    entry->name.size) == 0)
                return VXML_SEMANTIC_ERROR;
        }
        member = subdialog_result_member_by_name(
            subdialog->result_data, entry->name);
        if (member == NULL ||
            member->offset > subdialog->result_data->storage_type->size ||
            member->value->storage_type->size >
                subdialog->result_data->storage_type->size -
                    member->offset)
            return VXML_SEMANTIC_ERROR;
    }

    result_object =
        profile->staged_root.storage + subdialog->field_offset;
    meta_status = cmeta_data_value_init_zero(
        subdialog->result_data, result_object);
    if (meta_status != CMETA_OK)
        return buffer_status(meta_status);
    profile->staged_root.bound[subdialog->root_field] = 1u;

    for (index = 0u; index < mailbox->entry_count; ++index) {
        const vxml_cmeta_subdialog_result_entry_v1 *entry =
            &mailbox->entries[index];
        const cmeta_data_field_desc *member =
            subdialog_result_member_by_name(
                subdialog->result_data, entry->name);
        vxml_status status;
        if (member == NULL)
            return VXML_INVALID_STRUCTURE;
        status = assign_scalar_object(
            member->value,
            result_object + member->offset,
            &entry->value,
            program->max_string_bytes);
        if (status != VXML_OK)
            return status;
    }
    return VXML_OK;
}


vxml_status vxml_session_cmeta_record_run_ready(
    vxml_session *session,
    bool *out_progressed) {
    vxml_session_impl *impl;
    const vxml_cmeta_program_data *program;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_record_completion_mailbox *mailbox;
    const vxml_cmeta_record_row *record;
    const vxml_cmeta_form_row *form;
    uint64_t generation;
    unsigned expected;
    vxml_status status;

    if (out_progressed != NULL) *out_progressed = false;
    if (session == NULL || out_progressed == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl == NULL)
        return VXML_CLOSED;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    if (impl->state == VXML_SESSION_CLOSED)
        return VXML_CLOSED;
    if (impl->state != VXML_SESSION_RUNNING)
        return VXML_INVALID_STATE;

    program = (const vxml_cmeta_program_data *)
        impl->program->profile_data;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    mailbox = &profile->record_mailbox;
    if (!profile->record_in_flight ||
        profile->active_record == VXML_CMETA_NO_INDEX ||
        profile->active_record >= program->record_count ||
        program->records == NULL ||
        profile->active_form == VXML_CMETA_NO_INDEX ||
        profile->active_form >= program->form_count ||
        program->forms == NULL)
        return VXML_INVALID_STATE;

    expected = VXML_CMETA_RECORD_MAILBOX_READY;
    if (!atomic_compare_exchange_strong_explicit(
            &mailbox->state, &expected,
            VXML_CMETA_RECORD_MAILBOX_WRITING,
            memory_order_acq_rel, memory_order_acquire)) {
        if (expected == VXML_CMETA_RECORD_MAILBOX_EMPTY ||
            expected == VXML_CMETA_RECORD_MAILBOX_WRITING)
            return VXML_OK;
        if (expected == VXML_CMETA_RECORD_MAILBOX_CLOSED)
            return VXML_CLOSED;
        if (expected == VXML_CMETA_RECORD_MAILBOX_DISARMED)
            return VXML_INVALID_STATE;
        return VXML_INVALID_STRUCTURE;
    }

    *out_progressed = true;
    generation = atomic_load_explicit(
        &mailbox->generation, memory_order_relaxed);
    if (generation == UINT64_C(0) ||
        generation != profile->record_generation) {
        record_quiesce_generation(
            profile, profile->record_generation);
        profile->record_in_flight = false;
        record_mailbox_payload_reset(mailbox, true);
        atomic_store_explicit(
            &mailbox->generation, UINT64_C(0),
            memory_order_relaxed);
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_RECORD_MAILBOX_DISARMED,
            memory_order_release);
        return session_fail(impl, VXML_INVALID_STRUCTURE);
    }

    record = &program->records[profile->active_record];
    form = &program->forms[profile->active_form];
    if (record->form != profile->active_form ||
        form->scope >= program->scope_count ||
        record->form_item_slot >=
            program->scopes[form->scope].schema.slot_count ||
        profile->record_results == NULL ||
        profile->active_record >= profile->record_result_count) {
        record_quiesce_generation(profile, generation);
        profile->record_in_flight = false;
        record_mailbox_payload_reset(mailbox, true);
        atomic_store_explicit(
            &mailbox->generation, UINT64_C(0),
            memory_order_relaxed);
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_RECORD_MAILBOX_DISARMED,
            memory_order_release);
        return session_fail(impl, VXML_INVALID_STRUCTURE);
    }

    /*
     * READY is a terminal provider generation. Wait for the callback to leave
     * the provider before the Session adopts or releases its accepted lease.
     */
    record_quiesce_generation(profile, generation);
    profile->record_in_flight = false;
    atomic_store_explicit(
        &mailbox->generation, UINT64_C(0),
        memory_order_relaxed);

    if (mailbox->outcome == VXML_CMETA_RECORD_OUTCOME_NOINPUT ||
        mailbox->outcome == VXML_CMETA_RECORD_OUTCOME_ERROR) {
        const char *event_name =
            mailbox->outcome == VXML_CMETA_RECORD_OUTCOME_NOINPUT
                ? "noinput" : "error.record";
        const size_t event_size =
            mailbox->outcome == VXML_CMETA_RECORD_OUTCOME_NOINPUT
                ? sizeof("noinput") - 1u
                : sizeof("error.record") - 1u;
        record_mailbox_payload_reset(mailbox, true);
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_RECORD_MAILBOX_DISARMED,
            memory_order_release);
        status = cmeta_raise_event_impl(
            session, event_name, event_size, false);
        if (status != VXML_OK)
            return status;
        if (impl->state != VXML_SESSION_RUNNING)
            return VXML_OK;
        return select_directed_item(
            impl, program, profile, form, profile->active_form);
    }

    if (mailbox->outcome != VXML_CMETA_RECORD_OUTCOME_SUCCESS &&
        mailbox->outcome != VXML_CMETA_RECORD_OUTCOME_TERMCHAR) {
        record_mailbox_payload_reset(mailbox, true);
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_RECORD_MAILBOX_DISARMED,
            memory_order_release);
        return session_fail(impl, VXML_INVALID_STRUCTURE);
    }

    if (!transaction_begin(profile, program)) {
        record_mailbox_payload_reset(mailbox, true);
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_RECORD_MAILBOX_DISARMED,
            memory_order_release);
        return session_fail(impl, VXML_ALLOCATION_FAILED);
    }
    {
        const bool completed = true;
        if (!cmeta_scope_view_assign(
                &profile->staged_scopes[form->scope].view,
                record->form_item_slot, &completed)) {
            transaction_reset(profile, program);
            record_mailbox_payload_reset(mailbox, true);
            atomic_store_explicit(
                &mailbox->state,
                VXML_CMETA_RECORD_MAILBOX_DISARMED,
                memory_order_release);
            return session_fail(impl, VXML_ALLOCATION_FAILED);
        }
    }
    status = execute_record_filled_process(
        profile, program, form, record);
    if (status != VXML_OK) {
        transaction_reset(profile, program);
        record_mailbox_payload_reset(mailbox, true);
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_RECORD_MAILBOX_DISARMED,
            memory_order_release);
        return session_fail(impl, status);
    }

    transaction_commit(profile, program);
    reset_owner_retry_counters(
        profile, VXML_CMETA_EVENT_RECORD,
        profile->active_record);
    {
        const bool retained =
            profile->committed_scopes[form->scope].view.bound != NULL &&
            profile->committed_scopes[form->scope].view.bound[
                record->form_item_slot] != 0u;
        vxml_cmeta_record_result_slot *result =
            &profile->record_results[profile->active_record];
        if (retained) {
            record_result_slot_reset(result);
            result->live = true;
            result->outcome = mailbox->outcome;
            result->duration_us = mailbox->duration_us;
            result->has_termchar = mailbox->has_termchar;
            result->termchar = mailbox->termchar;
            if (mailbox->media_type_size != 0u) {
                if (result->media_type == NULL ||
                    mailbox->media_type_size >
                        profile->record_result_media_stride) {
                    record_mailbox_payload_reset(mailbox, true);
                    atomic_store_explicit(
                        &mailbox->state,
                        VXML_CMETA_RECORD_MAILBOX_DISARMED,
                        memory_order_release);
                    return session_fail(
                        impl, VXML_INVALID_STRUCTURE);
                }
                memcpy(
                    result->media_type,
                    mailbox->media_type,
                    mailbox->media_type_size);
                result->media_type_size =
                    mailbox->media_type_size;
            }
            if (mailbox->outcome ==
                    VXML_CMETA_RECORD_OUTCOME_SUCCESS) {
                result->recording = mailbox->recording;
                mailbox->recording =
                    (vxml_cmeta_recording_lease_v1){0};
            }
        }
    }

    record_mailbox_payload_reset(mailbox, true);
    atomic_store_explicit(
        &mailbox->state,
        VXML_CMETA_RECORD_MAILBOX_DISARMED,
        memory_order_release);
    profile->active_record = VXML_CMETA_NO_INDEX;

    if (profile->pending_navigation_uri != NULL) {
        status = publish_pending_navigation(
            impl, profile);
        if (status != VXML_OK)
            return session_fail(impl, status);
        return VXML_OK;
    }
    if (profile->exit_requested) {
        terminal_publish(profile);
        impl->state = VXML_SESSION_EXITED;
        impl->error = VXML_OK;
        return VXML_OK;
    }
    return select_directed_item(
        impl, program, profile, form, profile->active_form);
}


vxml_status vxml_session_cmeta_subdialog_run_ready(
    vxml_session *session,
    bool *out_progressed) {
    vxml_session_impl *impl;
    const vxml_cmeta_program_data *program;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_subdialog_completion_mailbox *mailbox;
    const vxml_cmeta_form_row *form;
    uint64_t generation;
    unsigned expected;
    vxml_status status;

    if (out_progressed != NULL) *out_progressed = false;
    if (session == NULL || out_progressed == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl == NULL) return VXML_CLOSED;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    if (impl->state == VXML_SESSION_CLOSED)
        return VXML_CLOSED;
    if (impl->state != VXML_SESSION_RUNNING)
        return VXML_INVALID_STATE;

    program = (const vxml_cmeta_program_data *)
        impl->program->profile_data;
    profile = (vxml_cmeta_session_data *)impl->profile_data;
    mailbox = &profile->subdialog_mailbox;
    if (!profile->subdialog_in_flight ||
        profile->active_subdialog == VXML_CMETA_NO_INDEX ||
        profile->active_subdialog >= program->subdialog_count ||
        program->subdialogs == NULL ||
        profile->active_form == VXML_CMETA_NO_INDEX ||
        profile->active_form >= program->form_count ||
        program->forms == NULL)
        return VXML_INVALID_STATE;

    expected = VXML_CMETA_SUBDIALOG_MAILBOX_READY;
    if (!atomic_compare_exchange_strong_explicit(
            &mailbox->state, &expected,
            VXML_CMETA_SUBDIALOG_MAILBOX_WRITING,
            memory_order_acq_rel, memory_order_acquire)) {
        if (expected == VXML_CMETA_SUBDIALOG_MAILBOX_EMPTY ||
            expected == VXML_CMETA_SUBDIALOG_MAILBOX_WRITING)
            return VXML_OK;
        if (expected == VXML_CMETA_SUBDIALOG_MAILBOX_CLOSED)
            return VXML_CLOSED;
        if (expected == VXML_CMETA_SUBDIALOG_MAILBOX_DISARMED)
            return VXML_INVALID_STATE;
        return VXML_INVALID_STRUCTURE;
    }

    *out_progressed = true;
    generation = atomic_load_explicit(
        &mailbox->generation, memory_order_relaxed);
    if (generation == 0u ||
        generation != profile->subdialog_generation) {
        atomic_store_explicit(
            &mailbox->state,
            VXML_CMETA_SUBDIALOG_MAILBOX_DISARMED,
            memory_order_release);
        profile->subdialog_in_flight = false;
        subdialog_snapshot_destroy(profile);
        subdialog_mailbox_payload_reset(mailbox);
        return session_fail(impl, VXML_INVALID_STRUCTURE);
    }

    form = &program->forms[profile->active_form];

    /*
     * Settle provider ownership before any parent PROCESS/Event work.
     * An accepted completion is terminal for this child generation, so close
     * and destroy must never call cancel for it.
     */
    profile->subdialog_in_flight = false;
    atomic_store_explicit(
        &mailbox->generation, UINT64_C(0), memory_order_relaxed);
    atomic_store_explicit(
        &mailbox->state,
        VXML_CMETA_SUBDIALOG_MAILBOX_DISARMED,
        memory_order_release);
    subdialog_snapshot_destroy(profile);

    if (mailbox->kind == VXML_CMETA_SUBDIALOG_RETURN_EVENT) {
        const vxml_cmeta_name_view event = mailbox->event;
        if (event.data == NULL || event.size == 0u) {
            subdialog_mailbox_payload_reset(mailbox);
            return session_fail(impl, VXML_INVALID_STRUCTURE);
        }
        status = cmeta_raise_event_impl(
            session, event.data, event.size, true);
        subdialog_mailbox_payload_reset(mailbox);
        if (status != VXML_OK) return status;
        if (impl->state != VXML_SESSION_RUNNING) {
            profile->active_subdialog = VXML_CMETA_NO_INDEX;
            return VXML_OK;
        }
        return select_directed_item(
            impl, program, profile, form, profile->active_form);
    }

    if (mailbox->kind == VXML_CMETA_SUBDIALOG_GLOBAL_EXIT) {
        status = subdialog_publish_global_exit(
            impl, profile, mailbox);
        subdialog_mailbox_payload_reset(mailbox);
        profile->active_subdialog = VXML_CMETA_NO_INDEX;
        if (status != VXML_OK)
            return session_fail(impl, status);
        return VXML_OK;
    }

    if (mailbox->kind == VXML_CMETA_SUBDIALOG_RETURN_DATA) {
        const vxml_cmeta_subdialog_row *subdialog =
            &program->subdialogs[profile->active_subdialog];
        if (subdialog->form != profile->active_form) {
            subdialog_mailbox_payload_reset(mailbox);
            profile->active_subdialog = VXML_CMETA_NO_INDEX;
            return session_fail(impl, VXML_INVALID_STRUCTURE);
        }
        if (!transaction_begin(profile, program)) {
            subdialog_mailbox_payload_reset(mailbox);
            profile->active_subdialog = VXML_CMETA_NO_INDEX;
            return session_fail(impl, VXML_ALLOCATION_FAILED);
        }
        status = subdialog_stage_return_data(
            profile, program, subdialog, mailbox);
        if (status == VXML_OK)
            status = execute_subdialog_filled_process(
                profile, program, form, subdialog);
        if (status != VXML_OK) {
            transaction_reset(profile, program);
            subdialog_mailbox_payload_reset(mailbox);
            profile->active_subdialog = VXML_CMETA_NO_INDEX;
            return session_fail(impl, status);
        }

        transaction_commit(profile, program);
        subdialog_mailbox_payload_reset(mailbox);
        profile->active_subdialog = VXML_CMETA_NO_INDEX;

        if (profile->pending_navigation_uri != NULL) {
            status = publish_pending_navigation(
                impl, profile);
            if (status != VXML_OK)
                return session_fail(impl, status);
            return VXML_OK;
        }
        if (profile->exit_requested) {
            terminal_publish(profile);
            impl->state = VXML_SESSION_EXITED;
            impl->error = VXML_OK;
            return VXML_OK;
        }
        return select_directed_item(
            impl, program, profile, form, profile->active_form);
    }

    subdialog_mailbox_payload_reset(mailbox);
    profile->active_subdialog = VXML_CMETA_NO_INDEX;
    return session_fail(impl, VXML_INVALID_STRUCTURE);
}

vxml_status vxml_session_cmeta_raise(
    vxml_session *session,
    const char *event_name,
    size_t event_name_size) {
    return cmeta_raise_event_impl(
        session, event_name, event_name_size, false);
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

static unsigned prompt_retry_count(
    const vxml_cmeta_session_data *profile,
    vxml_cmeta_event_scope_kind scope_kind,
    size_t owner) {
    size_t index;
    unsigned count = 1u;
    if (profile == NULL) return count;
    for (index = 0u; index < profile->event_counter_count; ++index) {
        const vxml_cmeta_event_counter *counter =
            &profile->event_counters[index];
        if (counter->scope_kind != scope_kind ||
            counter->owner != owner ||
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
    const vxml_cmeta_field_row *field = NULL;
    const vxml_cmeta_initial_row *initial = NULL;
    const vxml_cmeta_prompt_row *best = NULL;
    vxml_cmeta_prompt_owner_kind owner_kind;
    vxml_cmeta_event_scope_kind event_scope;
    size_t owner;
    size_t first_prompt;
    size_t prompt_row_count;
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
        program->forms == NULL)
        return VXML_INVALID_STATE;
    form = &program->forms[profile->active_form];

    if (profile->active_field != VXML_CMETA_NO_INDEX) {
        if (profile->active_initial != VXML_CMETA_NO_INDEX ||
            profile->active_field >= program->field_count ||
            program->fields == NULL)
            return VXML_INVALID_STRUCTURE;
        field = &program->fields[profile->active_field];
        if (field->form != profile->active_form)
            return VXML_INVALID_STRUCTURE;
        owner_kind = VXML_CMETA_PROMPT_OWNER_FIELD;
        event_scope = VXML_CMETA_EVENT_FIELD;
        owner = profile->active_field;
        first_prompt = field->first_prompt;
        prompt_row_count = field->prompt_count;
    } else if (profile->active_initial != VXML_CMETA_NO_INDEX) {
        if (profile->active_initial >= program->initial_count ||
            program->initials == NULL)
            return VXML_INVALID_STRUCTURE;
        initial = &program->initials[profile->active_initial];
        if (initial->form != profile->active_form ||
            profile->active_initial < form->first_initial ||
            profile->active_initial - form->first_initial >=
                form->initial_count)
            return VXML_INVALID_STRUCTURE;
        owner_kind = VXML_CMETA_PROMPT_OWNER_INITIAL;
        event_scope = VXML_CMETA_EVENT_INITIAL;
        owner = profile->active_initial;
        first_prompt = initial->first_prompt;
        prompt_row_count = initial->prompt_count;
    } else {
        return VXML_INVALID_STATE;
    }

    if (!range_valid(
            first_prompt, prompt_row_count,
            program->prompt_count) ||
        (prompt_row_count != 0u && program->prompts == NULL))
        return VXML_INVALID_STRUCTURE;

    prompt_count = prompt_retry_count(
        profile, event_scope, owner);
    for (offset = 0u; offset < prompt_row_count; ++offset) {
        const vxml_cmeta_prompt_row *row =
            &program->prompts[first_prompt + offset];
        bool eligible = true;
        if (row->owner_kind != owner_kind ||
            row->owner != owner ||
            row->count == 0u ||
            row->segment_count == 0u ||
            !range_valid(
                row->first_segment, row->segment_count,
                program->prompt_segment_count) ||
            program->prompt_segments == NULL ||
            row->required_capabilities == 0u)
            return VXML_INVALID_STRUCTURE;
        if (row->segment_count == 1u) {
            const vxml_cmeta_prompt_media_segment_v1 *segment =
                &program->prompt_segments[row->first_segment];
            if ((segment->kind != VXML_CMETA_PROMPT_MEDIA_TEXT &&
                 segment->kind != VXML_CMETA_PROMPT_MEDIA_SSML &&
                 segment->kind != VXML_CMETA_PROMPT_MEDIA_AUDIO &&
                 segment->kind != VXML_CMETA_PROMPT_MEDIA_MARK) ||
                ((segment->payload.data == NULL ||
                  segment->payload.size == 0u) &&
                 !(segment->kind == VXML_CMETA_PROMPT_MEDIA_MARK &&
                   prompt_dynamic_mark_segment(
                       program, row, row->first_segment))) ||
                row->media_kind != segment->kind ||
                row->media_payload != segment->payload.data ||
                row->media_payload_size != segment->payload.size)
                return VXML_INVALID_STRUCTURE;
            if (segment->kind == VXML_CMETA_PROMPT_MEDIA_TEXT) {
                if (row->text != segment->payload.data ||
                    row->text_size != segment->payload.size)
                    return VXML_INVALID_STRUCTURE;
            } else if (row->text != NULL || row->text_size != 0u) {
                return VXML_INVALID_STRUCTURE;
            }
        } else {
            size_t segment_offset;
            if ((row->required_capabilities &
                 VXML_CMETA_PROMPT_MEDIA_CAP_BATCH) == 0u)
                return VXML_INVALID_STRUCTURE;
            for (segment_offset = 0u;
                 segment_offset < row->segment_count;
                 ++segment_offset) {
                const vxml_cmeta_prompt_media_segment_v1 *segment =
                    &program->prompt_segments[
                        row->first_segment + segment_offset];
                if ((segment->kind != VXML_CMETA_PROMPT_MEDIA_TEXT &&
                     segment->kind != VXML_CMETA_PROMPT_MEDIA_SSML &&
                     segment->kind != VXML_CMETA_PROMPT_MEDIA_AUDIO &&
                     segment->kind != VXML_CMETA_PROMPT_MEDIA_MARK) ||
                    ((segment->payload.data == NULL ||
                      segment->payload.size == 0u) &&
                     !(segment->kind == VXML_CMETA_PROMPT_MEDIA_MARK &&
                       prompt_dynamic_mark_segment(
                           program, row,
                           row->first_segment + segment_offset))))
                    return VXML_INVALID_STRUCTURE;
            }
        }
        if (row->fallback_count != 0u) {
            size_t fallback_offset;
            if ((row->required_capabilities &
                 VXML_CMETA_PROMPT_MEDIA_CAP_AUDIO_FALLBACK) == 0u ||
                row->segment_count < 2u ||
                !range_valid(
                    row->first_fallback, row->fallback_count,
                    program->prompt_fallback_count) ||
                program->prompt_fallbacks == NULL)
                return VXML_INVALID_STRUCTURE;
            for (fallback_offset = 0u;
                 fallback_offset < row->fallback_count;
                 ++fallback_offset) {
                const vxml_cmeta_prompt_media_fallback_v1 *fallback =
                    &program->prompt_fallbacks[
                        row->first_fallback + fallback_offset];
                size_t fallback_segment_offset;
                if (fallback->audio_segment_index >= row->segment_count ||
                    fallback->first_fallback_segment !=
                        fallback->audio_segment_index + 1u ||
                    fallback->fallback_segment_count == 0u ||
                    fallback->first_fallback_segment >= row->segment_count ||
                    fallback->fallback_segment_count >
                        row->segment_count -
                            fallback->first_fallback_segment ||
                    program->prompt_segments[
                        row->first_segment +
                        fallback->audio_segment_index].kind !=
                            VXML_CMETA_PROMPT_MEDIA_AUDIO)
                    return VXML_INVALID_STRUCTURE;
                for (fallback_segment_offset = 0u;
                     fallback_segment_offset <
                        fallback->fallback_segment_count;
                     ++fallback_segment_offset) {
                    const vxml_cmeta_prompt_media_segment_kind kind =
                        program->prompt_segments[
                            row->first_segment +
                            fallback->first_fallback_segment +
                            fallback_segment_offset].kind;
                    if (kind != VXML_CMETA_PROMPT_MEDIA_TEXT &&
                        kind != VXML_CMETA_PROMPT_MEDIA_SSML)
                        return VXML_INVALID_STRUCTURE;
                }
            }
        } else if ((row->required_capabilities &
                    VXML_CMETA_PROMPT_MEDIA_CAP_AUDIO_FALLBACK) != 0u) {
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
        .field = field != NULL
            ? (vxml_cmeta_name_view){field->name, field->name_size}
            : (vxml_cmeta_name_view){0},
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
    out_request->field = field != NULL
        ? (vxml_cmeta_name_view){field->name, field->name_size}
        : (vxml_cmeta_name_view){0};
    out_request->prompt_count = prompt_count;
    out_request->selected_count =
        prompt != NULL ? prompt->count : 0u;
    if (prompt == NULL)
        return VXML_OK;
    out_request->bargein = prompt->bargein;
    out_request->bargein_type = prompt->bargein_type;
    if (prompt->segment_count != 1u)
        return VXML_UNSUPPORTED_FEATURE;

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
    else if (prompt->media_kind == VXML_CMETA_PROMPT_MEDIA_SSML) {
        out_request->required_capabilities =
            VXML_CMETA_PROMPT_MEDIA_CAP_SSML;
        out_request->segment.media_type =
            (vxml_cmeta_name_view){
                "application/ssml+xml",
                sizeof("application/ssml+xml") - 1u};
    } else if (prompt->media_kind == VXML_CMETA_PROMPT_MEDIA_AUDIO)
        out_request->required_capabilities =
            VXML_CMETA_PROMPT_MEDIA_CAP_AUDIO;
    else if (prompt->media_kind == VXML_CMETA_PROMPT_MEDIA_MARK)
        out_request->required_capabilities =
            VXML_CMETA_PROMPT_MEDIA_CAP_MARK;
    else
        return VXML_INVALID_STRUCTURE;
    return VXML_OK;
}

static bool prompt_media_unresolved_dynamic_mark(
    const vxml_cmeta_prompt_media_segment_v1 *segment) {
    return segment != NULL &&
        segment->kind == VXML_CMETA_PROMPT_MEDIA_MARK &&
        (segment->payload.data == NULL ||
         segment->payload.size == 0u);
}

vxml_status vxml_session_cmeta_prompt_media_request(
    const vxml_session *session,
    vxml_cmeta_prompt_media_request_v1 *out_request) {
    const vxml_session_impl *impl;
    vxml_status status;
    if (out_request != NULL)
        *out_request = (vxml_cmeta_prompt_media_request_v1){0};
    if (session == NULL || out_request == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = cmeta_session(session);
    if (impl == NULL) return VXML_INVALID_CONTRACT;
    status = prompt_media_request_from_impl(impl, out_request);
    if (status == VXML_OK &&
        out_request->segment_count != 0u &&
        prompt_media_unresolved_dynamic_mark(
            &out_request->segment))
        return VXML_UNSUPPORTED_FEATURE;
    return status;
}

static vxml_status prompt_media_batch_request_from_impl(
    const vxml_session_impl *impl,
    vxml_cmeta_prompt_media_batch_request_v1 *out_request) {
    const vxml_cmeta_field_row *field = NULL;
    const vxml_cmeta_prompt_row *prompt = NULL;
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_session_data *profile;
    unsigned prompt_count = 0u;
    vxml_status status;
    if (out_request == NULL) return VXML_INVALID_ARGUMENT;
    *out_request = (vxml_cmeta_prompt_media_batch_request_v1){0};
    if (impl == NULL || impl->program == NULL ||
        impl->program->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    status = selected_prompt_row(
        impl, &field, &prompt, &prompt_count);
    if (status != VXML_OK) return status;
    program = (const vxml_cmeta_program_data *)
        impl->program->profile_data;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;

    out_request->abi_version =
        VXML_CMETA_PROMPT_MEDIA_BATCH_REQUEST_ABI_V1;
    out_request->struct_size =
        sizeof(vxml_cmeta_prompt_media_batch_request_v1);
    out_request->generation = profile->collect_generation;
    out_request->field = field != NULL
        ? (vxml_cmeta_name_view){field->name, field->name_size}
        : (vxml_cmeta_name_view){0};
    out_request->prompt_count = prompt_count;
    out_request->selected_count =
        prompt != NULL ? prompt->count : 0u;
    if (prompt == NULL)
        return VXML_OK;
    out_request->bargein = prompt->bargein;
    out_request->bargein_type = prompt->bargein_type;
    if (prompt->segment_count == 0u ||
        !range_valid(
            prompt->first_segment, prompt->segment_count,
            program->prompt_segment_count) ||
        program->prompt_segments == NULL)
        return VXML_INVALID_STRUCTURE;
    out_request->required_capabilities =
        prompt->required_capabilities;
    out_request->segments =
        &program->prompt_segments[prompt->first_segment];
    out_request->segment_count = prompt->segment_count;
    if (prompt->fallback_count != 0u) {
        if (!range_valid(
                prompt->first_fallback, prompt->fallback_count,
                program->prompt_fallback_count) ||
            program->prompt_fallbacks == NULL)
            return VXML_INVALID_STRUCTURE;
        out_request->fallbacks =
            &program->prompt_fallbacks[prompt->first_fallback];
        out_request->fallback_count = prompt->fallback_count;
    }
    return VXML_OK;
}

vxml_status vxml_session_cmeta_prompt_media_batch_request(
    const vxml_session *session,
    vxml_cmeta_prompt_media_batch_request_v1 *out_request) {
    const vxml_session_impl *impl;
    vxml_status status;
    size_t index;
    if (out_request != NULL)
        *out_request = (vxml_cmeta_prompt_media_batch_request_v1){0};
    if (session == NULL || out_request == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = cmeta_session(session);
    if (impl == NULL) return VXML_INVALID_CONTRACT;
    status = prompt_media_batch_request_from_impl(impl, out_request);
    if (status != VXML_OK) return status;
    for (index = 0u; index < out_request->segment_count; ++index)
        if (prompt_media_unresolved_dynamic_mark(
                &out_request->segments[index]))
            return VXML_UNSUPPORTED_FEATURE;
    return VXML_OK;
}

static vxml_status prompt_media_project_dynamic_marks(
    vxml_session_impl *impl,
    vxml_cmeta_prompt_media_batch_request_v1 *batch) {
    vxml_cmeta_session_data *profile;
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_field_row *field = NULL;
    const vxml_cmeta_prompt_row *prompt = NULL;
    unsigned prompt_count = 0u;
    size_t side_index = 0u;
    size_t dynamic_offset = 0u;
    size_t storage_used = 0u;
    size_t scopes[2];
    vxml_status status;

    if (impl == NULL || batch == NULL ||
        impl->program == NULL ||
        impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_ARGUMENT;
    program = (const vxml_cmeta_program_data *)
        impl->program->profile_data;
    profile = (vxml_cmeta_session_data *)impl->profile_data;

    status = selected_prompt_row(
        impl, &field, &prompt, &prompt_count);
    if (status != VXML_OK) return status;
    (void)prompt_count;
    profile->prompt_media_projected_generation = UINT64_C(0);
    profile->prompt_media_projected_first_segment = SIZE_MAX;
    profile->prompt_media_projected_segment_count = 0u;
    if (prompt == NULL || prompt->dynamic_mark_count == 0u)
        return VXML_OK;

    if (batch->segments == NULL ||
        batch->segment_count != prompt->segment_count ||
        batch->segment_count >
            profile->prompt_media_projected_segment_capacity ||
        profile->prompt_media_projected_segments == NULL ||
        profile->prompt_media_dynamic_mark_storage == NULL ||
        program->prompt_mark_exprs == NULL ||
        program->max_dynamic_mark_name_bytes == 0u)
        return VXML_INVALID_STRUCTURE;

    memcpy(
        profile->prompt_media_projected_segments,
        batch->segments,
        batch->segment_count *
            sizeof(*profile->prompt_media_projected_segments));

    if (prompt->owner_kind == VXML_CMETA_PROMPT_OWNER_FIELD) {
        const vxml_cmeta_field_row *owner;
        const vxml_cmeta_form_row *form;
        if (prompt->owner >= program->field_count ||
            program->fields == NULL ||
            program->forms == NULL)
            return VXML_INVALID_STRUCTURE;
        owner = &program->fields[prompt->owner];
        if (owner->form >= program->form_count)
            return VXML_INVALID_STRUCTURE;
        form = &program->forms[owner->form];
        scopes[0] = form->scope;
    } else if (
        prompt->owner_kind == VXML_CMETA_PROMPT_OWNER_INITIAL) {
        const vxml_cmeta_initial_row *owner;
        const vxml_cmeta_form_row *form;
        if (prompt->owner >= program->initial_count ||
            program->initials == NULL ||
            program->forms == NULL)
            return VXML_INVALID_STRUCTURE;
        owner = &program->initials[prompt->owner];
        if (owner->form >= program->form_count)
            return VXML_INVALID_STRUCTURE;
        form = &program->forms[owner->form];
        scopes[0] = form->scope;
    } else {
        return VXML_INVALID_STRUCTURE;
    }
    scopes[1] = program->document_scope;

    while (side_index < program->prompt_mark_expr_count &&
           program->prompt_mark_exprs[side_index].segment_index <
               prompt->first_segment)
        ++side_index;

    for (dynamic_offset = 0u;
         dynamic_offset < prompt->dynamic_mark_count;
         ++dynamic_offset) {
        const vxml_cmeta_prompt_mark_expr_row *dynamic;
        size_t absolute_segment;
        if (side_index >= program->prompt_mark_expr_count)
            return VXML_INVALID_STRUCTURE;
        dynamic = &program->prompt_mark_exprs[side_index++];
        absolute_segment = dynamic->segment_index;
        size_t relative_segment;
        vxml_cmeta_value_view value = {0};
        char *destination;

        if (dynamic->expression == VXML_CMETA_NO_INDEX ||
            dynamic->expression >= program->expression_count ||
            absolute_segment < prompt->first_segment ||
            absolute_segment >=
                prompt->first_segment + prompt->segment_count)
            return VXML_INVALID_STRUCTURE;
        relative_segment =
            absolute_segment - prompt->first_segment;
        if (relative_segment >= batch->segment_count ||
            profile->prompt_media_projected_segments[
                relative_segment].kind !=
                VXML_CMETA_PROMPT_MEDIA_MARK)
            return VXML_INVALID_STRUCTURE;

        status = evaluate_expression(
            profile, program, false,
            dynamic->expression, scopes, 2u, &value);
        if (status != VXML_OK)
            return VXML_SEMANTIC_ERROR;
        if (value.kind != VXML_CMETA_VALUE_STRING ||
            value.data.string.data == NULL ||
            value.data.string.size == 0u ||
            value.data.string.size >
                program->max_dynamic_mark_name_bytes ||
            storage_used >
                profile->prompt_media_dynamic_mark_storage_capacity ||
            value.data.string.size >
                profile->prompt_media_dynamic_mark_storage_capacity -
                    storage_used)
            return VXML_SEMANTIC_ERROR;

        destination =
            profile->prompt_media_dynamic_mark_storage +
            storage_used;
        memcpy(
            destination,
            value.data.string.data,
            value.data.string.size);
        profile->prompt_media_projected_segments[
            relative_segment].payload =
            (vxml_cmeta_name_view){
                destination, value.data.string.size};
        storage_used += value.data.string.size;
    }

    batch->segments =
        profile->prompt_media_projected_segments;
    profile->prompt_media_projected_generation =
        batch->generation;
    profile->prompt_media_projected_first_segment =
        prompt->first_segment;
    profile->prompt_media_projected_segment_count =
        prompt->segment_count;
    return VXML_OK;
}

static vxml_status prompt_media_raise_semantic(
    vxml_session *session) {
    vxml_status event_status = vxml_session_cmeta_raise(
        session, "error.semantic",
        sizeof("error.semantic") - 1u);
    return event_status == VXML_OK
        ? VXML_SEMANTIC_ERROR : event_status;
}

static bool prompt_media_failure_event(
    vxml_cmeta_prompt_media_failure failure,
    const char **out_event,
    size_t *out_event_size) {
    if (out_event == NULL || out_event_size == NULL)
        return false;
    *out_event = NULL;
    *out_event_size = 0u;
    switch (failure) {
    case VXML_CMETA_PROMPT_MEDIA_FAILURE_BADFETCH:
        *out_event = "error.badfetch";
        *out_event_size = sizeof("error.badfetch") - 1u;
        return true;
    case VXML_CMETA_PROMPT_MEDIA_FAILURE_UNSUPPORTED_FORMAT:
        *out_event = "error.unsupported.format";
        *out_event_size = sizeof("error.unsupported.format") - 1u;
        return true;
    case VXML_CMETA_PROMPT_MEDIA_FAILURE_NORESOURCE:
        *out_event = "error.noresource";
        *out_event_size = sizeof("error.noresource") - 1u;
        return true;
    default:
        return false;
    }
}

static vxml_status prompt_media_raise_failure(
    vxml_session *session,
    vxml_cmeta_prompt_media_failure failure) {
    const char *event_name;
    size_t event_name_size;
    if (!prompt_media_failure_event(
            failure, &event_name, &event_name_size))
        return VXML_INVALID_ARGUMENT;
    return vxml_session_cmeta_raise(
        session, event_name, event_name_size);
}

static vxml_status prompt_media_admission_failure(
    vxml_session *session,
    vxml_status media_status) {
    vxml_cmeta_prompt_media_failure failure;
    vxml_status event_status;
    if (media_status == VXML_LIMIT_EXCEEDED)
        failure = VXML_CMETA_PROMPT_MEDIA_FAILURE_NORESOURCE;
    else if (media_status == VXML_UNSUPPORTED_FEATURE)
        failure = VXML_CMETA_PROMPT_MEDIA_FAILURE_UNSUPPORTED_FORMAT;
    else
        return media_status;
    event_status = prompt_media_raise_failure(session, failure);
    return event_status == VXML_OK ? media_status : event_status;
}

vxml_status vxml_session_cmeta_prompt_media_prepare(
    vxml_session *session, const char **out_error) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_prompt_media_batch_request_v1 batch = {0};
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
    if (profile->prompt_media_adapter == NULL)
        return VXML_INVALID_CONTRACT;
    if (profile->prompt_media_prepared ||
        profile->prompt_media_in_flight)
        return VXML_INVALID_STATE;
    if (profile->collect_generation != 0u &&
        profile->prompt_media_barged_generation ==
            profile->collect_generation)
        return VXML_INVALID_STATE;

    status = prompt_media_batch_request_from_impl(impl, &batch);
    if (status != VXML_OK) return status;
    if (batch.segment_count == 0u)
        return VXML_INVALID_STATE;
    status = prompt_media_project_dynamic_marks(impl, &batch);
    if (status != VXML_OK)
        return status == VXML_SEMANTIC_ERROR
            ? prompt_media_raise_semantic(session)
            : status;
    if ((profile->prompt_media_adapter->capabilities &
         batch.required_capabilities) !=
        batch.required_capabilities)
        return prompt_media_admission_failure(
            session, VXML_UNSUPPORTED_FEATURE);

    if (batch.segment_count == 1u) {
        vxml_cmeta_prompt_media_request_v1 request = {
            .abi_version = VXML_CMETA_PROMPT_MEDIA_REQUEST_ABI_V1,
            .struct_size =
                sizeof(vxml_cmeta_prompt_media_request_v1),
            .generation = batch.generation,
            .required_capabilities =
                batch.required_capabilities,
            .field = batch.field,
            .prompt_count = batch.prompt_count,
            .selected_count = batch.selected_count,
            .segment_count = 1u,
            .segment = batch.segments[0],
            .bargein = batch.bargein,
            .bargein_type = batch.bargein_type
        };
        if (profile->prompt_media_adapter->prepare == NULL)
            return VXML_INVALID_CONTRACT;
        status = profile->prompt_media_adapter->prepare(
            profile->prompt_media_user,
            &request, &ticket, out_error);
    } else {
        const size_t batch_field_size =
            offsetof(vxml_cmeta_prompt_media_adapter_v1, prepare_batch) +
            sizeof(profile->prompt_media_adapter->prepare_batch);
        if (profile->prompt_media_adapter->struct_size <
                batch_field_size ||
            profile->prompt_media_adapter->prepare_batch == NULL)
            return prompt_media_admission_failure(
                session, VXML_UNSUPPORTED_FEATURE);
        status = profile->prompt_media_adapter->prepare_batch(
            profile->prompt_media_user,
            &batch, &ticket, out_error);
    }

    if (status != VXML_OK) {
        if (ticket.discard != NULL)
            ticket.discard(ticket.user);
        return prompt_media_admission_failure(session, status);
    }
    if (ticket.commit == NULL || ticket.discard == NULL) {
        if (ticket.discard != NULL)
            ticket.discard(ticket.user);
        return VXML_INVALID_CONTRACT;
    }
    profile->prompt_media_ticket = ticket;
    profile->prompt_media_generation = batch.generation;
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
    if (profile->prompt_media_generation == 0u ||
        atomic_load_explicit(
            &profile->prompt_media_mailbox.state,
            memory_order_acquire) !=
            VXML_CMETA_PROMPT_MEDIA_MAILBOX_DISARMED)
        return VXML_INVALID_STATE;
    profile->prompt_media_mark_generation =
        profile->prompt_media_generation;
    profile->prompt_media_last_mark_segment = SIZE_MAX;
    profile->prompt_media_last_mark_name_size = 0u;
    profile->prompt_media_last_mark_has_elapsed = false;
    profile->prompt_media_last_mark_elapsed_ms = UINT64_C(0);
    atomic_store_explicit(
        &profile->prompt_media_mailbox.generation,
        profile->prompt_media_generation,
        memory_order_relaxed);
    atomic_store_explicit(
        &profile->prompt_media_mailbox.state,
        VXML_CMETA_PROMPT_MEDIA_MAILBOX_EMPTY,
        memory_order_release);
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

static vxml_status prompt_mark_last_name_view(
    const vxml_cmeta_session_data *profile,
    const vxml_cmeta_program_data *program,
    vxml_cmeta_name_view *out_name) {
    const vxml_cmeta_prompt_media_segment_v1 *segment;
    if (out_name != NULL) *out_name = (vxml_cmeta_name_view){0};
    if (profile == NULL || program == NULL || out_name == NULL)
        return VXML_INVALID_ARGUMENT;
    if (profile->prompt_media_last_mark_segment == SIZE_MAX)
        return VXML_OK;
    if (program->prompt_segments == NULL ||
        profile->prompt_media_last_mark_segment >=
            program->prompt_segment_count)
        return VXML_INVALID_STRUCTURE;
    segment = &program->prompt_segments[
        profile->prompt_media_last_mark_segment];
    if (segment->kind != VXML_CMETA_PROMPT_MEDIA_MARK)
        return VXML_INVALID_STRUCTURE;
    if (profile->prompt_media_last_mark_name_size != 0u) {
        if (profile->prompt_media_last_mark_name == NULL ||
            profile->prompt_media_last_mark_name_size >
                profile->prompt_media_last_mark_name_capacity)
            return VXML_INVALID_STRUCTURE;
        *out_name = (vxml_cmeta_name_view){
            profile->prompt_media_last_mark_name,
            profile->prompt_media_last_mark_name_size};
        return VXML_OK;
    }
    if (segment->payload.data == NULL ||
        segment->payload.size == 0u)
        return VXML_INVALID_STRUCTURE;
    *out_name = segment->payload;
    return VXML_OK;
}

static vxml_status prompt_mark_capture_terminal(
    vxml_cmeta_session_data *profile,
    const vxml_cmeta_program_data *program,
    uint64_t generation,
    bool has_terminal_timing,
    uint64_t terminal_elapsed_ms) {
    vxml_cmeta_name_view name = {0};
    bool has_mark = false;
    uint64_t marktime_ms = UINT64_C(0);
    vxml_status status;

    if (profile == NULL || program == NULL ||
        generation == UINT64_C(0))
        return VXML_INVALID_ARGUMENT;

    if (profile->prompt_media_mark_generation == generation &&
        profile->prompt_media_last_mark_segment != SIZE_MAX &&
        has_terminal_timing &&
        profile->prompt_media_last_mark_has_elapsed) {
        if (terminal_elapsed_ms <
            profile->prompt_media_last_mark_elapsed_ms)
            return VXML_INVALID_STRUCTURE;
        status = prompt_mark_last_name_view(
            profile, program, &name);
        if (status != VXML_OK) return status;
        if (name.data == NULL || name.size == 0u)
            return VXML_INVALID_STRUCTURE;
        marktime_ms =
            terminal_elapsed_ms -
            profile->prompt_media_last_mark_elapsed_ms;
        has_mark = true;
    }

    if (!mark_result_slot_publish(
            &profile->pending_mark_result,
            profile->mark_result_name_stride,
            generation,
            name.data, name.size,
            marktime_ms, has_mark))
        return VXML_INVALID_STRUCTURE;
    return VXML_OK;
}

static vxml_cmeta_prompt_media_ingress_result
prompt_media_try_complete_impl(
    vxml_session *session,
    uint64_t completion_generation,
    vxml_cmeta_prompt_media_outcome outcome,
    vxml_cmeta_prompt_media_failure failure,
    bool has_terminal_timing,
    uint64_t terminal_elapsed_ms) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    vxml_cmeta_prompt_media_mailbox *mailbox;
    unsigned state;
    unsigned expected;
    uint64_t generation;

    if (session == NULL ||
        completion_generation == UINT64_C(0) ||
        (outcome != VXML_CMETA_PROMPT_MEDIA_OUTCOME_COMPLETED &&
         outcome != VXML_CMETA_PROMPT_MEDIA_OUTCOME_FAILED))
        return VXML_CMETA_PROMPT_MEDIA_INGRESS_INVALID_ARGUMENT;

    if (outcome == VXML_CMETA_PROMPT_MEDIA_OUTCOME_FAILED) {
        if (failure == VXML_CMETA_PROMPT_MEDIA_FAILURE_NONE)
            failure = VXML_CMETA_PROMPT_MEDIA_FAILURE_NORESOURCE;
        if (failure != VXML_CMETA_PROMPT_MEDIA_FAILURE_BADFETCH &&
            failure != VXML_CMETA_PROMPT_MEDIA_FAILURE_UNSUPPORTED_FORMAT &&
            failure != VXML_CMETA_PROMPT_MEDIA_FAILURE_NORESOURCE)
            return VXML_CMETA_PROMPT_MEDIA_INGRESS_INVALID_ARGUMENT;
    } else if (failure != VXML_CMETA_PROMPT_MEDIA_FAILURE_NONE) {
        return VXML_CMETA_PROMPT_MEDIA_INGRESS_INVALID_ARGUMENT;
    }

    impl = (vxml_session_impl *)session->impl;
    if (impl == NULL)
        return VXML_CMETA_PROMPT_MEDIA_INGRESS_CLOSED;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->profile_data == NULL)
        return impl->state == VXML_SESSION_CLOSED
            ? VXML_CMETA_PROMPT_MEDIA_INGRESS_CLOSED
            : VXML_CMETA_PROMPT_MEDIA_INGRESS_INVALID_ARGUMENT;
    if (impl->state == VXML_SESSION_CLOSED)
        return VXML_CMETA_PROMPT_MEDIA_INGRESS_CLOSED;

    profile = (vxml_cmeta_session_data *)impl->profile_data;
    program = (const vxml_cmeta_program_data *)
        impl->program->profile_data;
    mailbox = &profile->prompt_media_mailbox;
    state = atomic_load_explicit(
        &mailbox->state, memory_order_acquire);
    if (state == VXML_CMETA_PROMPT_MEDIA_MAILBOX_CLOSED)
        return VXML_CMETA_PROMPT_MEDIA_INGRESS_CLOSED;
    if (state == VXML_CMETA_PROMPT_MEDIA_MAILBOX_DISARMED)
        return VXML_CMETA_PROMPT_MEDIA_INGRESS_STALE;
    if (state == VXML_CMETA_PROMPT_MEDIA_MAILBOX_WRITING ||
        state == VXML_CMETA_PROMPT_MEDIA_MAILBOX_READY)
        return VXML_CMETA_PROMPT_MEDIA_INGRESS_FULL;
    if (state != VXML_CMETA_PROMPT_MEDIA_MAILBOX_EMPTY)
        return VXML_CMETA_PROMPT_MEDIA_INGRESS_INVALID_ARGUMENT;

    generation = atomic_load_explicit(
        &mailbox->generation, memory_order_relaxed);
    if (!profile->prompt_media_in_flight ||
        completion_generation != generation ||
        completion_generation != profile->prompt_media_generation)
        return VXML_CMETA_PROMPT_MEDIA_INGRESS_STALE;

    expected = VXML_CMETA_PROMPT_MEDIA_MAILBOX_EMPTY;
    if (!atomic_compare_exchange_strong_explicit(
            &mailbox->state, &expected,
            VXML_CMETA_PROMPT_MEDIA_MAILBOX_WRITING,
            memory_order_acq_rel, memory_order_acquire)) {
        if (expected == VXML_CMETA_PROMPT_MEDIA_MAILBOX_CLOSED)
            return VXML_CMETA_PROMPT_MEDIA_INGRESS_CLOSED;
        if (expected == VXML_CMETA_PROMPT_MEDIA_MAILBOX_DISARMED)
            return VXML_CMETA_PROMPT_MEDIA_INGRESS_STALE;
        return VXML_CMETA_PROMPT_MEDIA_INGRESS_FULL;
    }

    if (atomic_load_explicit(
            &mailbox->generation, memory_order_relaxed) !=
            completion_generation ||
        !profile->prompt_media_in_flight ||
        profile->prompt_media_generation != completion_generation) {
        expected = VXML_CMETA_PROMPT_MEDIA_MAILBOX_WRITING;
        (void)atomic_compare_exchange_strong_explicit(
            &mailbox->state, &expected,
            VXML_CMETA_PROMPT_MEDIA_MAILBOX_DISARMED,
            memory_order_acq_rel, memory_order_acquire);
        return VXML_CMETA_PROMPT_MEDIA_INGRESS_STALE;
    }

    mailbox->outcome = outcome;
    mailbox->failure = failure;
    mailbox->has_terminal_timing = has_terminal_timing;
    mailbox->terminal_elapsed_ms =
        has_terminal_timing
            ? terminal_elapsed_ms : UINT64_C(0);
    expected = VXML_CMETA_PROMPT_MEDIA_MAILBOX_WRITING;
    if (!atomic_compare_exchange_strong_explicit(
            &mailbox->state, &expected,
            VXML_CMETA_PROMPT_MEDIA_MAILBOX_READY,
            memory_order_acq_rel, memory_order_acquire)) {
        if (expected == VXML_CMETA_PROMPT_MEDIA_MAILBOX_CLOSED)
            return VXML_CMETA_PROMPT_MEDIA_INGRESS_CLOSED;
        if (expected == VXML_CMETA_PROMPT_MEDIA_MAILBOX_DISARMED)
            return VXML_CMETA_PROMPT_MEDIA_INGRESS_STALE;
        return VXML_CMETA_PROMPT_MEDIA_INGRESS_FULL;
    }
    return VXML_CMETA_PROMPT_MEDIA_INGRESS_ACCEPTED;
}

vxml_cmeta_prompt_media_ingress_result
vxml_session_cmeta_prompt_media_try_complete(
    vxml_session *session,
    const vxml_cmeta_prompt_media_completion_v1 *completion) {
    vxml_cmeta_prompt_media_failure failure =
        VXML_CMETA_PROMPT_MEDIA_FAILURE_NONE;
    const size_t completion_prefix =
        offsetof(vxml_cmeta_prompt_media_completion_v1, outcome) +
        sizeof(completion->outcome);
    const size_t failure_tail =
        offsetof(vxml_cmeta_prompt_media_completion_v1, failure) +
        sizeof(completion->failure);

    if (session == NULL || completion == NULL ||
        completion->abi_version !=
            VXML_CMETA_PROMPT_MEDIA_COMPLETION_ABI_V1 ||
        completion->struct_size < completion_prefix)
        return VXML_CMETA_PROMPT_MEDIA_INGRESS_INVALID_ARGUMENT;
    if (completion->struct_size >= failure_tail)
        failure = completion->failure;
    return prompt_media_try_complete_impl(
        session,
        completion->generation,
        completion->outcome,
        failure,
        false, UINT64_C(0));
}

vxml_cmeta_prompt_media_ingress_result
vxml_session_cmeta_prompt_media_try_complete_v2(
    vxml_session *session,
    const vxml_cmeta_prompt_media_completion_v2 *completion) {
    if (session == NULL || completion == NULL ||
        completion->abi_version !=
            VXML_CMETA_PROMPT_MEDIA_COMPLETION_ABI_V2 ||
        completion->struct_size < sizeof(*completion))
        return VXML_CMETA_PROMPT_MEDIA_INGRESS_INVALID_ARGUMENT;
    return prompt_media_try_complete_impl(
        session,
        completion->generation,
        completion->outcome,
        completion->failure,
        true,
        completion->playback_elapsed_ms);
}

vxml_status vxml_session_cmeta_prompt_media_run_ready(
    vxml_session *session,
    bool *out_progressed,
    vxml_cmeta_prompt_media_outcome *out_outcome) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    const vxml_cmeta_program_data *program;
    vxml_cmeta_prompt_media_mailbox *mailbox;
    unsigned expected;
    uint64_t generation;
    vxml_cmeta_prompt_media_failure failure;
    bool has_terminal_timing;
    uint64_t terminal_elapsed_ms;
    vxml_status status;

    if (out_progressed != NULL) *out_progressed = false;
    if (out_outcome != NULL) *out_outcome = 0;
    if (session == NULL || out_progressed == NULL ||
        out_outcome == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl == NULL) return VXML_CLOSED;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;
    if (impl->state == VXML_SESSION_CLOSED)
        return VXML_CLOSED;
    if (impl->state != VXML_SESSION_RUNNING)
        return VXML_INVALID_STATE;

    profile = (vxml_cmeta_session_data *)impl->profile_data;
    mailbox = &profile->prompt_media_mailbox;
    expected = VXML_CMETA_PROMPT_MEDIA_MAILBOX_READY;
    if (!atomic_compare_exchange_strong_explicit(
            &mailbox->state, &expected,
            VXML_CMETA_PROMPT_MEDIA_MAILBOX_WRITING,
            memory_order_acq_rel, memory_order_acquire)) {
        if (expected == VXML_CMETA_PROMPT_MEDIA_MAILBOX_EMPTY ||
            expected == VXML_CMETA_PROMPT_MEDIA_MAILBOX_WRITING ||
            expected == VXML_CMETA_PROMPT_MEDIA_MAILBOX_DISARMED)
            return VXML_OK;
        if (expected == VXML_CMETA_PROMPT_MEDIA_MAILBOX_CLOSED)
            return VXML_CLOSED;
        return VXML_INVALID_STATE;
    }

    generation = atomic_load_explicit(
        &mailbox->generation, memory_order_relaxed);
    if (!profile->prompt_media_in_flight ||
        generation == 0u ||
        generation != profile->prompt_media_generation ||
        (mailbox->outcome !=
             VXML_CMETA_PROMPT_MEDIA_OUTCOME_COMPLETED &&
         mailbox->outcome !=
             VXML_CMETA_PROMPT_MEDIA_OUTCOME_FAILED) ||
        (mailbox->outcome ==
             VXML_CMETA_PROMPT_MEDIA_OUTCOME_COMPLETED &&
         mailbox->failure != VXML_CMETA_PROMPT_MEDIA_FAILURE_NONE) ||
        (mailbox->outcome ==
             VXML_CMETA_PROMPT_MEDIA_OUTCOME_FAILED &&
         mailbox->failure != VXML_CMETA_PROMPT_MEDIA_FAILURE_BADFETCH &&
         mailbox->failure !=
             VXML_CMETA_PROMPT_MEDIA_FAILURE_UNSUPPORTED_FORMAT &&
         mailbox->failure != VXML_CMETA_PROMPT_MEDIA_FAILURE_NORESOURCE)) {
        profile->prompt_media_in_flight = false;
        profile->prompt_media_generation = 0u;
        prompt_media_mailbox_disarm(profile);
        return VXML_INVALID_STRUCTURE;
    }

    *out_progressed = true;
    *out_outcome = mailbox->outcome;
    failure = mailbox->failure;
    has_terminal_timing = mailbox->has_terminal_timing;
    terminal_elapsed_ms = mailbox->terminal_elapsed_ms;

    if (*out_outcome == VXML_CMETA_PROMPT_MEDIA_OUTCOME_COMPLETED) {
        status = prompt_mark_capture_terminal(
            profile, program, generation,
            has_terminal_timing, terminal_elapsed_ms);
        if (status != VXML_OK) {
            profile->prompt_media_in_flight = false;
            profile->prompt_media_generation = 0u;
            prompt_media_mailbox_disarm(profile);
            return status;
        }
    } else if (profile->pending_mark_result.assigned &&
               profile->pending_mark_result.generation == generation) {
        mark_result_slot_reset(
            &profile->pending_mark_result);
    }

    profile->prompt_media_in_flight = false;
    profile->prompt_media_generation = 0u;
    prompt_media_mailbox_disarm(profile);
    if (*out_outcome == VXML_CMETA_PROMPT_MEDIA_OUTCOME_FAILED)
        return prompt_media_raise_failure(session, failure);
    return VXML_OK;
}

vxml_cmeta_prompt_barge_result
vxml_session_cmeta_prompt_media_barge_in(
    vxml_session *session,
    uint64_t collect_generation,
    vxml_cmeta_prompt_bargein_type signal_type) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    const vxml_cmeta_field_row *field = NULL;
    const vxml_cmeta_prompt_row *prompt = NULL;
    unsigned prompt_count = 0u;
    vxml_status status;

    if (session == NULL || collect_generation == 0u ||
        (signal_type != VXML_CMETA_PROMPT_BARGEIN_SPEECH &&
         signal_type != VXML_CMETA_PROMPT_BARGEIN_HOTWORD))
        return VXML_CMETA_PROMPT_BARGE_INVALID_ARGUMENT;

    impl = (vxml_session_impl *)session->impl;
    if (impl == NULL || impl->state == VXML_SESSION_CLOSED)
        return VXML_CMETA_PROMPT_BARGE_STALE;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->profile_data == NULL ||
        impl->state != VXML_SESSION_RUNNING)
        return VXML_CMETA_PROMPT_BARGE_INVALID_ARGUMENT;

    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (collect_generation != profile->collect_generation ||
        !profile->prompt_media_in_flight ||
        profile->prompt_media_generation != collect_generation)
        return VXML_CMETA_PROMPT_BARGE_STALE;

    status = selected_prompt_row(
        impl, &field, &prompt, &prompt_count);
    (void)field;
    (void)prompt_count;
    if (status != VXML_OK || prompt == NULL)
        return VXML_CMETA_PROMPT_BARGE_STALE;
    if (!prompt->bargein)
        return VXML_CMETA_PROMPT_BARGE_DISABLED;
    if (prompt->bargein_type !=
            VXML_CMETA_PROMPT_BARGEIN_UNSPECIFIED &&
        prompt->bargein_type != signal_type)
        return VXML_CMETA_PROMPT_BARGE_TYPE_MISMATCH;

    profile->prompt_media_barged_generation =
        collect_generation;
    settle_prompt_media(profile);
    return VXML_CMETA_PROMPT_BARGE_CANCELED;
}



vxml_cmeta_prompt_mark_result
vxml_session_cmeta_prompt_media_mark(
    vxml_session *session,
    uint64_t generation,
    size_t segment_index) {
    vxml_session_impl *impl;
    vxml_cmeta_session_data *profile;
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_field_row *field = NULL;
    const vxml_cmeta_prompt_row *prompt = NULL;
    unsigned prompt_count = 0u;
    size_t absolute_index;
    vxml_status status;

    if (session == NULL || generation == 0u)
        return VXML_CMETA_PROMPT_MARK_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl == NULL || impl->state == VXML_SESSION_CLOSED)
        return VXML_CMETA_PROMPT_MARK_CLOSED;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->program->profile_data == NULL ||
        impl->profile_data == NULL ||
        impl->state != VXML_SESSION_RUNNING)
        return VXML_CMETA_PROMPT_MARK_INVALID_ARGUMENT;

    profile = (vxml_cmeta_session_data *)impl->profile_data;
    if (!profile->prompt_media_in_flight ||
        profile->prompt_media_generation != generation ||
        profile->prompt_media_mark_generation != generation)
        return VXML_CMETA_PROMPT_MARK_STALE;

    status = selected_prompt_row(
        impl, &field, &prompt, &prompt_count);
    (void)field;
    (void)prompt_count;
    if (status != VXML_OK || prompt == NULL)
        return VXML_CMETA_PROMPT_MARK_STALE;
    if (segment_index >= prompt->segment_count)
        return VXML_CMETA_PROMPT_MARK_INVALID_ARGUMENT;

    program = (const vxml_cmeta_program_data *)
        impl->program->profile_data;
    if (!range_valid(
            prompt->first_segment, prompt->segment_count,
            program->prompt_segment_count) ||
        program->prompt_segments == NULL)
        return VXML_CMETA_PROMPT_MARK_INVALID_ARGUMENT;
    absolute_index = prompt->first_segment + segment_index;
    if (program->prompt_segments[absolute_index].kind !=
        VXML_CMETA_PROMPT_MEDIA_MARK)
        return VXML_CMETA_PROMPT_MARK_NOT_MARK;

    if (profile->prompt_media_last_mark_segment != SIZE_MAX &&
        absolute_index <= profile->prompt_media_last_mark_segment)
        return VXML_CMETA_PROMPT_MARK_OUT_OF_ORDER;

    profile->prompt_media_last_mark_name_size = 0u;
    if (profile->prompt_media_projected_generation == generation &&
        profile->prompt_media_projected_first_segment != SIZE_MAX &&
        absolute_index >=
            profile->prompt_media_projected_first_segment &&
        absolute_index -
            profile->prompt_media_projected_first_segment <
            profile->prompt_media_projected_segment_count) {
        const size_t relative =
            absolute_index -
            profile->prompt_media_projected_first_segment;
        const vxml_cmeta_prompt_media_segment_v1 *projected;
        if (profile->prompt_media_projected_segments == NULL ||
            relative >=
                profile->prompt_media_projected_segment_capacity)
            return VXML_CMETA_PROMPT_MARK_INVALID_ARGUMENT;
        projected =
            &profile->prompt_media_projected_segments[relative];
        if (projected->kind != VXML_CMETA_PROMPT_MEDIA_MARK ||
            projected->payload.data == NULL ||
            projected->payload.size == 0u ||
            profile->prompt_media_last_mark_name == NULL ||
            projected->payload.size >
                profile->prompt_media_last_mark_name_capacity)
            return VXML_CMETA_PROMPT_MARK_INVALID_ARGUMENT;
        memcpy(
            profile->prompt_media_last_mark_name,
            projected->payload.data,
            projected->payload.size);
        profile->prompt_media_last_mark_name_size =
            projected->payload.size;
    }
    profile->prompt_media_last_mark_segment = absolute_index;
    return VXML_CMETA_PROMPT_MARK_ACCEPTED;
}

vxml_status vxml_session_cmeta_prompt_media_last_mark(
    const vxml_session *session,
    vxml_cmeta_prompt_mark_view_v1 *out_mark) {
    const vxml_session_impl *impl;
    const vxml_cmeta_session_data *profile;
    const vxml_cmeta_program_data *program;
    const vxml_cmeta_prompt_media_segment_v1 *segment;
    if (out_mark != NULL)
        *out_mark = (vxml_cmeta_prompt_mark_view_v1){0};
    if (session == NULL || out_mark == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (const vxml_session_impl *)session->impl;
    if (impl == NULL) return VXML_CLOSED;
    if (impl->state == VXML_SESSION_CLOSED) return VXML_CLOSED;
    if (impl->program == NULL ||
        impl->program->profile_kind != VXML_PROFILE_CMETA ||
        impl->program->profile_data == NULL ||
        impl->profile_data == NULL)
        return VXML_INVALID_CONTRACT;

    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    out_mark->abi_version = VXML_CMETA_PROMPT_MARK_VIEW_ABI_V1;
    out_mark->struct_size = sizeof(*out_mark);
    out_mark->generation = profile->prompt_media_mark_generation;
    out_mark->segment_index = SIZE_MAX;
    if (profile->prompt_media_mark_generation == 0u ||
        profile->prompt_media_last_mark_segment == SIZE_MAX)
        return VXML_OK;

    program = (const vxml_cmeta_program_data *)
        impl->program->profile_data;
    if (program->prompt_segments == NULL ||
        profile->prompt_media_last_mark_segment >=
            program->prompt_segment_count)
        return VXML_INVALID_STRUCTURE;
    segment = &program->prompt_segments[
        profile->prompt_media_last_mark_segment];
    if (segment->kind != VXML_CMETA_PROMPT_MEDIA_MARK)
        return VXML_INVALID_STRUCTURE;
    if (profile->prompt_media_last_mark_name_size != 0u) {
        if (profile->prompt_media_last_mark_name == NULL ||
            profile->prompt_media_last_mark_name_size >
                profile->prompt_media_last_mark_name_capacity)
            return VXML_INVALID_STRUCTURE;
    } else if (segment->payload.data == NULL ||
               segment->payload.size == 0u) {
        return VXML_INVALID_STRUCTURE;
    }

    {
        size_t prompt_index;
        bool found = false;
        for (prompt_index = 0u;
             prompt_index < program->prompt_count;
             ++prompt_index) {
            const vxml_cmeta_prompt_row *prompt =
                &program->prompts[prompt_index];
            if (range_valid(
                    prompt->first_segment, prompt->segment_count,
                    program->prompt_segment_count) &&
                profile->prompt_media_last_mark_segment >=
                    prompt->first_segment &&
                profile->prompt_media_last_mark_segment -
                    prompt->first_segment <
                    prompt->segment_count) {
                out_mark->segment_index =
                    profile->prompt_media_last_mark_segment -
                    prompt->first_segment;
                found = true;
                break;
            }
        }
        if (!found) return VXML_INVALID_STRUCTURE;
    }
    out_mark->name =
        profile->prompt_media_last_mark_name_size != 0u
            ? (vxml_cmeta_name_view){
                profile->prompt_media_last_mark_name,
                profile->prompt_media_last_mark_name_size}
            : segment->payload;
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

static bool terminal_control_valid(
    const vxml_session_impl *impl,
    const vxml_cmeta_session_data *profile) {
    const vxml_cmeta_program_data *program;
    if (impl == NULL || profile == NULL ||
        impl->program == NULL || impl->program->profile_data == NULL ||
        !terminal_exit_valid(&profile->terminal_exit))
        return false;
    program = (const vxml_cmeta_program_data *)impl->program->profile_data;
    switch (profile->terminal_kind) {
    case VXML_CMETA_TERMINAL_NONE:
        return profile->terminal_event == NULL &&
            profile->terminal_event_size == 0u &&
            profile->terminal_exit.kind == VXML_CMETA_EXIT_EMPTY;
    case VXML_CMETA_TERMINAL_EXIT:
        return profile->terminal_event == NULL &&
            profile->terminal_event_size == 0u;
    case VXML_CMETA_TERMINAL_RETURN:
        return profile->terminal_event == NULL &&
            profile->terminal_event_size == 0u &&
            profile->terminal_exit.kind == VXML_CMETA_EXIT_NAMELIST &&
            profile->terminal_exit.count != 0u;
    case VXML_CMETA_TERMINAL_DISCONNECT:
        return profile->terminal_event == NULL &&
            profile->terminal_event_size == 0u &&
            profile->terminal_exit.kind == VXML_CMETA_EXIT_EMPTY;
    case VXML_CMETA_TERMINAL_RETURN_EVENT:
        return profile->terminal_exit.kind == VXML_CMETA_EXIT_EMPTY &&
            profile->terminal_event != NULL &&
            profile->terminal_event_size != 0u &&
            terminal_span_valid(
                program->strings, program->string_size,
                profile->terminal_event, profile->terminal_event_size);
    default:
        return false;
    }
}

vxml_status vxml_session_cmeta_terminal_kind(
    const vxml_session *session,
    vxml_cmeta_terminal_kind *out_kind) {
    const vxml_session_impl *impl = cmeta_session(session);
    const vxml_cmeta_session_data *profile;
    if (out_kind == NULL) return VXML_INVALID_ARGUMENT;
    *out_kind = VXML_CMETA_TERMINAL_NONE;
    if (impl == NULL) return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_EXITED) return VXML_INVALID_STATE;
    if (impl->profile_data == NULL) return VXML_INVALID_STRUCTURE;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    if (!terminal_control_valid(impl, profile))
        return VXML_INVALID_STRUCTURE;
    *out_kind = profile->terminal_kind;
    return VXML_OK;
}

vxml_status vxml_session_cmeta_terminal_event(
    const vxml_session *session,
    vxml_cmeta_name_view *out_event) {
    const vxml_session_impl *impl;
    const vxml_cmeta_session_data *profile;
    if (out_event != NULL) *out_event = (vxml_cmeta_name_view){0};
    if (out_event == NULL) return VXML_INVALID_ARGUMENT;
    impl = cmeta_session(session);
    if (impl == NULL) return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_EXITED) return VXML_INVALID_STATE;
    if (impl->profile_data == NULL) return VXML_INVALID_STRUCTURE;
    profile = (const vxml_cmeta_session_data *)impl->profile_data;
    if (!terminal_control_valid(impl, profile))
        return VXML_INVALID_STRUCTURE;
    if (profile->terminal_kind != VXML_CMETA_TERMINAL_RETURN_EVENT)
        return VXML_INVALID_STATE;
    out_event->data = profile->terminal_event;
    out_event->size = profile->terminal_event_size;
    return VXML_OK;
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
