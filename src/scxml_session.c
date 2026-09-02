#include "scxml_session.h"
#include "scxml_analyze.h"
#include "scxml_emit.h"
#include "scxml_program.h"
#include "scxml_runtime.h"
#include "scxml_quickjs.h"

static bool event_io_adapter_valid(
    const scxml_event_io_adapter *adapter) {
    const uint64_t known = SCXML_EVENT_IO_CAP_SEND |
        SCXML_EVENT_IO_CAP_DELAYED_SEND |
        SCXML_EVENT_IO_CAP_CANCEL |
        SCXML_EVENT_IO_CAP_PAYLOAD |
        SCXML_EVENT_IO_CAP_CONTENT;
    if (adapter == NULL) return true;
    if (adapter->abi_version != SCXML_ADAPTER_ABI ||
        adapter->struct_size != sizeof(*adapter) ||
        (adapter->capabilities & ~known) != 0u ||
        adapter->close == NULL || adapter->is_quiescent == NULL)
        return false;
    if ((adapter->capabilities & SCXML_EVENT_IO_CAP_SEND) != 0u &&
        adapter->prepare_send == NULL)
        return false;
    if ((adapter->capabilities &
         (SCXML_EVENT_IO_CAP_PAYLOAD |
          SCXML_EVENT_IO_CAP_CONTENT)) != 0u &&
        (adapter->capabilities & SCXML_EVENT_IO_CAP_SEND) == 0u)
        return false;
    if ((adapter->capabilities & SCXML_EVENT_IO_CAP_DELAYED_SEND) != 0u &&
        (adapter->capabilities & SCXML_EVENT_IO_CAP_SEND) == 0u)
        return false;
    if ((adapter->capabilities & SCXML_EVENT_IO_CAP_CANCEL) != 0u &&
        ((adapter->capabilities & SCXML_EVENT_IO_CAP_DELAYED_SEND) == 0u ||
         adapter->prepare_cancel == NULL))
        return false;
    return true;
}

static bool invoke_adapter_valid(
    const scxml_invoke_adapter *adapter) {
    const uint64_t known = SCXML_INVOKE_CAP_START |
        SCXML_INVOKE_CAP_CANCEL | SCXML_INVOKE_CAP_FORWARD |
        SCXML_INVOKE_CAP_PAYLOAD |
        SCXML_INVOKE_CAP_CONTENT;
    if (adapter == NULL) return true;
    if (adapter->abi_version != SCXML_ADAPTER_ABI ||
        adapter->struct_size != sizeof(*adapter) ||
        (adapter->capabilities & ~known) != 0u ||
        adapter->close == NULL || adapter->is_quiescent == NULL)
        return false;
    if ((adapter->capabilities & SCXML_INVOKE_CAP_START) != 0u &&
        adapter->prepare_start == NULL)
        return false;
    if ((adapter->capabilities &
         (SCXML_INVOKE_CAP_PAYLOAD |
          SCXML_INVOKE_CAP_CONTENT)) != 0u &&
        (adapter->capabilities & SCXML_INVOKE_CAP_START) == 0u)
        return false;
    if ((adapter->capabilities & SCXML_INVOKE_CAP_CANCEL) != 0u &&
        adapter->prepare_cancel == NULL)
        return false;
    if ((adapter->capabilities & SCXML_INVOKE_CAP_FORWARD) != 0u &&
        adapter->prepare_forward == NULL)
        return false;
    return true;
}

static void session_close_adapter(scxml_session_impl *impl) {
    if (impl != NULL && impl->has_event_io &&
        !atomic_exchange_explicit(
            &impl->adapter_close_called, true, memory_order_acq_rel))
        impl->event_io.close(impl->adapter_user);
    if (impl != NULL && impl->has_invoke &&
        !atomic_exchange_explicit(
            &impl->invoke_close_called, true, memory_order_acq_rel))
        impl->invoke.close(impl->invoke_user);
}

static void session_free_storage(scxml_session_impl *impl) {
    size_t index;
    if (impl == NULL) return;
    scxml_quickjs_session_runtime_destroy(impl);
    scxml_scope_view_clear(&impl->supplemental_checkpoint);
    scxml_scope_view_clear(&impl->supplemental_staged);
    scxml_scope_view_clear(&impl->supplemental_committed);
    if (impl->current_event_data_object_live) {
        scxml_runtime_destroy_event_data_object(
            impl->current_event_data_schema,
            impl->current_event_data_object.bytes);
        impl->current_event_data_object_live = false;
    }
    if (impl->external_metadata_rows != NULL) {
        for (index = 0u; index < impl->external_metadata_capacity; ++index) {
            scxml_external_event_metadata_row *row =
                &impl->external_metadata_rows[index];
            if (row->data_object_live) {
                scxml_runtime_destroy_event_data_object(
                    row->data_schema, row->data_object.bytes);
                row->data_object_live = false;
            }
        }
    }
    if (impl->completion_data_slots != NULL) {
        for (index = 0u; index < impl->completion_data_capacity; ++index) {
            scxml_completion_data_slot *slot =
                &impl->completion_data_slots[index];
            if (slot->data_object_live) {
                scxml_runtime_destroy_event_data_object(
                    slot->data_schema, slot->data_object.bytes);
                slot->data_object_live = false;
            }
        }
    }
    free(impl->late_initializers);
    free(impl->environment_override_assignments);
    free(impl->prepared_effects);
    free(impl->external_metadata_rows);
    free(impl->completion_projection_stable_ids);
    free(impl->completion_projection_fields);
    free(impl->completion_data_slots);
    free(impl->invocation_effects);
    free(impl->invocation_rows);
    free(impl->delayed_sends);
    free(impl->guard_users);
    free(impl->guard_bindings);
    free(impl->binding_users);
    free(impl->bindings);
    free(impl->system_name);
    free(impl->ioprocessor_storage);
    free(impl->payload_scratch);
    free(impl->cbind_scratch);
    free(impl->data_decode_allocation);
    free(impl->supplemental_checkpoint_allocation);
    free(impl->supplemental_staged_allocation);
    free(impl->supplemental_committed_allocation);
}

static bool descriptor_field_present(const char *data, size_t size) {
    return data != NULL && size != 0u;
}

static bool descriptor_field_equal(
    const char *left, size_t left_size,
    const char *right, size_t right_size) {
    return left_size == right_size &&
        memcmp(left, right, left_size) == 0;
}

static bool retain_descriptor_string(
    char **cursor, const char *source, size_t size, const char **out) {
    if (cursor == NULL || *cursor == NULL || source == NULL || out == NULL)
        return false;
    *out = *cursor;
    memcpy(*cursor, source, size);
    (*cursor)[size] = '\0';
    *cursor += size + 1u;
    return true;
}

static cflow_statechart_instance_status retain_ioprocessors(
    scxml_session_impl *session,
    const scxml_ioprocessor_descriptor *configured,
    size_t configured_count, const char *scxml_location,
    size_t scxml_location_size, size_t max_storage_bytes) {
    const char core_name[] = SCXML_SCXML_IOPROCESSOR_NAME;
    const char core_type[] = SCXML_SCXML_IOPROCESSOR_TYPE;
    size_t row_count;
    size_t row_bytes;
    size_t total_bytes;
    size_t index;
    char *cursor;
    scxml_ioprocessor_descriptor *rows;
    if (session == NULL || scxml_location == NULL ||
        scxml_location_size == 0u ||
        (configured_count != 0u && configured == NULL))
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    if (!scxml_analyze_checked_add(configured_count, 1u, &row_count) ||
        !scxml_analyze_checked_multiply(
            row_count, sizeof(*rows), &row_bytes))
        return CFLOW_STATECHART_INSTANCE_LIMIT_EXCEEDED;
    total_bytes = row_bytes;
#define SCXML_MEASURE_DESCRIPTOR_FIELD(size_)                              \
    do {                                                                   \
        size_t terminated_;                                                \
        if (!scxml_analyze_checked_add((size_), 1u, &terminated_) ||       \
            !scxml_analyze_checked_add(                                    \
                total_bytes, terminated_, &total_bytes))                   \
            return CFLOW_STATECHART_INSTANCE_LIMIT_EXCEEDED;               \
    } while (0)
    SCXML_MEASURE_DESCRIPTOR_FIELD(sizeof(core_name) - 1u);
    SCXML_MEASURE_DESCRIPTOR_FIELD(sizeof(core_type) - 1u);
    SCXML_MEASURE_DESCRIPTOR_FIELD(scxml_location_size);
    for (index = 0u; index < configured_count; ++index) {
        const scxml_ioprocessor_descriptor *row = &configured[index];
        if (!descriptor_field_present(row->name, row->name_size) ||
            !descriptor_field_present(row->type, row->type_size) ||
            !descriptor_field_present(row->location, row->location_size))
            return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        SCXML_MEASURE_DESCRIPTOR_FIELD(row->name_size);
        SCXML_MEASURE_DESCRIPTOR_FIELD(row->type_size);
        SCXML_MEASURE_DESCRIPTOR_FIELD(row->location_size);
    }
#undef SCXML_MEASURE_DESCRIPTOR_FIELD
    if (configured_count != 0u && total_bytes > max_storage_bytes)
        return CFLOW_STATECHART_INSTANCE_LIMIT_EXCEEDED;
    for (index = 0u; index < configured_count; ++index) {
        const scxml_ioprocessor_descriptor *row = &configured[index];
        size_t previous;
        if (!scxml_analyze_is_xml_ncname(
                (turbo_xml_string_view){row->name, row->name_size}) ||
            descriptor_field_equal(
                row->name, row->name_size,
                core_name, sizeof(core_name) - 1u) ||
            descriptor_field_equal(
                row->type, row->type_size,
                core_type, sizeof(core_type) - 1u))
            return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        for (previous = 0u; previous < index; ++previous) {
            const scxml_ioprocessor_descriptor *prior = &configured[previous];
            if (descriptor_field_equal(
                    row->name, row->name_size,
                    prior->name, prior->name_size) ||
                descriptor_field_equal(
                    row->type, row->type_size,
                    prior->type, prior->type_size))
                return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        }
    }
    session->ioprocessor_storage = malloc(total_bytes);
    if (session->ioprocessor_storage == NULL)
        return CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED;
    rows = (scxml_ioprocessor_descriptor *)session->ioprocessor_storage;
    cursor = (char *)session->ioprocessor_storage + row_bytes;
    rows[0] = (scxml_ioprocessor_descriptor){
        .name_size = sizeof(core_name) - 1u,
        .type_size = sizeof(core_type) - 1u,
        .location_size = scxml_location_size};
    if (!retain_descriptor_string(
            &cursor, core_name, rows[0].name_size, &rows[0].name) ||
        !retain_descriptor_string(
            &cursor, core_type, rows[0].type_size, &rows[0].type) ||
        !retain_descriptor_string(
            &cursor, scxml_location, rows[0].location_size,
            &rows[0].location))
        return CFLOW_STATECHART_INSTANCE_INVALID_CONFIGURATION;
    for (index = 0u; index < configured_count; ++index) {
        const scxml_ioprocessor_descriptor *source = &configured[index];
        scxml_ioprocessor_descriptor *destination = &rows[index + 1u];
        *destination = (scxml_ioprocessor_descriptor){
            .name_size = source->name_size,
            .type_size = source->type_size,
            .location_size = source->location_size};
        if (!retain_descriptor_string(
                &cursor, source->name, source->name_size,
                &destination->name) ||
            !retain_descriptor_string(
                &cursor, source->type, source->type_size,
                &destination->type) ||
            !retain_descriptor_string(
                &cursor, source->location, source->location_size,
                &destination->location))
            return CFLOW_STATECHART_INSTANCE_INVALID_CONFIGURATION;
    }
    session->ioprocessors = rows;
    session->ioprocessor_count = row_count;
    session->system_values.ioprocessors = rows;
    session->system_values.ioprocessor_count = row_count;
    session->system_values.scxml_location = (scxml_expr_string_view){
        rows[0].location, rows[0].location_size};
    return CFLOW_STATECHART_INSTANCE_OK;
}

bool scxml_session_data_initializer_is_overridden(
    const scxml_session_impl *session, size_t assignment) {
    size_t index;
    if (session == NULL) return false;
    for (index = 0u; index < session->environment_override_count; ++index) {
        if (session->environment_override_assignments[index] == assignment)
            return true;
    }
    return false;
}

static bool data_resource_adapter_valid(
    const scxml_data_resource_adapter_v1 *adapter) {
    return adapter != NULL &&
           adapter->abi_version == SCXML_DATA_RESOURCE_ADAPTER_ABI_V1 &&
           adapter->struct_size >= sizeof(*adapter) &&
           adapter->open != NULL && adapter->close != NULL;
}

static bool session_allocate_scope_view(
    const scxml_scope_schema *schema,
    void **out_allocation, scxml_scope_view *out_view) {
    size_t allocation_size;
    uintptr_t address;
    uintptr_t aligned;
    unsigned char *bound;
    if (schema == NULL || out_allocation == NULL || out_view == NULL ||
        *out_allocation != NULL || out_view->schema != NULL ||
        schema->slot_count == 0u || schema->storage_size == 0u ||
        schema->storage_align == 0u ||
        schema->storage_size > SIZE_MAX - (schema->storage_align - 1u) ||
        schema->storage_size + schema->storage_align - 1u >
            SIZE_MAX - schema->slot_count)
        return false;
    allocation_size = schema->storage_size + schema->storage_align - 1u +
                      schema->slot_count;
    *out_allocation = calloc(1u, allocation_size);
    if (*out_allocation == NULL) return false;
    address = (uintptr_t)*out_allocation;
    if (address > UINTPTR_MAX - (schema->storage_align - 1u)) {
        free(*out_allocation);
        *out_allocation = NULL;
        return false;
    }
    aligned = (address + schema->storage_align - 1u) &
              ~((uintptr_t)schema->storage_align - 1u);
    bound = (unsigned char *)aligned + schema->storage_size;
    *out_view = (scxml_scope_view){
        .schema = schema,
        .storage = (unsigned char *)aligned,
        .bound = bound};
    return true;
}

static cflow_statechart_instance_status retain_supplemental_scope(
    scxml_session_impl *session, size_t max_storage_bytes) {
    const scxml_scope_schema *schema;
    size_t one_view_bytes;
    size_t total_bytes;
    if (session == NULL || session->program == NULL)
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    schema = &session->program->supplemental_scope;
    if (schema->slot_count == 0u) {
        session->supplemental_committed.schema = schema;
        session->supplemental_staged.schema = schema;
        session->supplemental_checkpoint.schema = schema;
        session->system_values.supplemental =
            &session->supplemental_committed;
        return CFLOW_STATECHART_INSTANCE_OK;
    }
    if (schema->storage_size > SIZE_MAX - (schema->storage_align - 1u) ||
        schema->storage_size + schema->storage_align - 1u >
            SIZE_MAX - schema->slot_count)
        return CFLOW_STATECHART_INSTANCE_LIMIT_EXCEEDED;
    one_view_bytes = schema->storage_size + schema->storage_align - 1u +
                     schema->slot_count;
    if (!scxml_analyze_checked_multiply(one_view_bytes, 3u, &total_bytes) ||
        total_bytes > max_storage_bytes)
        return CFLOW_STATECHART_INSTANCE_LIMIT_EXCEEDED;
    if (!session_allocate_scope_view(
            schema, &session->supplemental_committed_allocation,
            &session->supplemental_committed) ||
        !session_allocate_scope_view(
            schema, &session->supplemental_staged_allocation,
            &session->supplemental_staged) ||
        !session_allocate_scope_view(
            schema, &session->supplemental_checkpoint_allocation,
            &session->supplemental_checkpoint))
        return CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED;
    session->system_values.supplemental =
        &session->supplemental_committed;
    return CFLOW_STATECHART_INSTANCE_OK;
}

static cflow_statechart_instance_status retain_data_resource_options(
    scxml_session_impl *session,
    const scxml_cmeta_session_options_v3 *options) {
    size_t assignment;
    size_t max_size = 0u;
    size_t max_align = 0u;
    size_t allocation_size;
    uintptr_t address;
    uintptr_t aligned;
    if (session == NULL || session->program == NULL)
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    for (assignment = 0u;
         assignment < session->program->assignment_count; ++assignment) {
        const cmeta_data_desc *destination = NULL;
        const char *uri = NULL;
        size_t uri_size = 0u;
        const cmeta_type_desc *type;
        if (scxml_session_data_initializer_is_overridden(session, assignment) ||
            !scxml_assign_external_source(
                &session->program->assignments[assignment], &uri, &uri_size,
                &destination))
            continue;
        if (uri == NULL || uri_size == 0u || destination == NULL ||
            destination->storage_type == NULL)
            return CFLOW_STATECHART_INSTANCE_INVALID_CONFIGURATION;
        type = destination->storage_type;
        if (type->size == 0u || type->align == 0u ||
            (type->align & (type->align - 1u)) != 0u)
            return CFLOW_STATECHART_INSTANCE_INVALID_CONFIGURATION;
        if (type->size > max_size) max_size = type->size;
        if (type->align > max_align) max_align = type->align;
    }
    if (max_size == 0u) return CFLOW_STATECHART_INSTANCE_OK;
    if (options == NULL ||
        !data_resource_adapter_valid(options->data_resources) ||
        options->cbind_scratch_bytes == 0u ||
        options->max_data_depth == 0u ||
        options->max_data_container_items == 0u ||
        options->max_data_buffer_bytes == 0u ||
        max_size > SIZE_MAX - (max_align - 1u))
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    allocation_size = max_size + max_align - 1u;
    session->data_decode_allocation = calloc(1u, allocation_size);
    session->cbind_scratch = calloc(1u, options->cbind_scratch_bytes);
    if (session->data_decode_allocation == NULL ||
        session->cbind_scratch == NULL)
        return CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED;
    address = (uintptr_t)session->data_decode_allocation;
    if (address > UINTPTR_MAX - (max_align - 1u))
        return CFLOW_STATECHART_INSTANCE_INVALID_CONFIGURATION;
    aligned = (address + max_align - 1u) &
              ~((uintptr_t)max_align - 1u);
    session->data_decode_storage = (void *)aligned;
    session->data_decode_storage_size = max_size;
    session->data_resources = *options->data_resources;
    session->data_resource_user = options->data_resource_user;
    session->cbind = (cbind_context)
        CBIND_CONTEXT_WITH_BUFFERS_INIT(
            session->cbind_scratch, options->cbind_scratch_bytes,
            options->max_data_depth, options->max_data_container_items,
            options->max_data_buffer_bytes);
    session->has_data_resources = true;
    return CFLOW_STATECHART_INSTANCE_OK;
}

static bool session_data_range_is_bound(
    void *user, size_t offset, size_t storage_size, bool *out_bound) {
    const scxml_session_impl *session =
        (const scxml_session_impl *)user;
    const scxml_program_impl *program;
    const size_t root_size =
        session != NULL && session->program != NULL &&
                session->program->cmeta_root != NULL &&
                session->program->cmeta_root->storage_type != NULL
            ? session->program->cmeta_root->storage_type->size
            : 0u;
    size_t range_end;
    size_t index;
    bool declared = false;
    if (session == NULL || out_bound == NULL || storage_size == 0u ||
        root_size == 0u ||
        !scxml_analyze_checked_add(offset, storage_size, &range_end) ||
        range_end > root_size)
        return false;
    program = session->program;
    *out_bound = false;
    for (index = 0u; index < program->data_binding_count; ++index) {
        const scxml_data_binding_descriptor *binding =
            &program->data_bindings[index];
        size_t binding_end;
        if (binding->assignment >= program->assignment_count)
            return false;
        if (binding->read_only_system) {
            if (binding->storage_size != 0u) return false;
            continue;
        }
        if (binding->storage_size == 0u) return false;
        if (!scxml_analyze_checked_add(
                binding->offset, binding->storage_size, &binding_end) ||
            binding_end > root_size)
            return false;
        if (offset >= binding_end || binding->offset >= range_end) continue;
        declared = true;
        if (binding->late_initializer == SIZE_MAX ||
            scxml_session_data_initializer_is_overridden(
                session, binding->assignment)) {
            *out_bound = true;
            return true;
        }
        if (binding->late_initializer >= session->late_initializer_count)
            return false;
        if (session->late_initializers[binding->late_initializer].phase !=
            SCXML_LATE_INITIALIZER_NEVER) {
            *out_bound = true;
            return true;
        }
    }
    *out_bound = !declared;
    return true;
}

static cflow_statechart_instance_status retain_environment_overrides(
    scxml_session_impl *session,
    const scxml_cmeta_environment_override *overrides,
    size_t override_count) {
    const scxml_program_impl *program;
    size_t row;
    if (session == NULL || session->program == NULL)
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    program = session->program;
    if (override_count == 0u) return CFLOW_STATECHART_INSTANCE_OK;
    if (overrides == NULL ||
        override_count > program->top_level_data_initializer_count ||
        program->assignments == NULL ||
        program->cmeta_max_source_bytes == 0u ||
        program->cmeta_max_path_depth == 0u)
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    session->environment_override_assignments =
        (size_t *)scxml_emit_allocate_rows(
            override_count,
            sizeof(*session->environment_override_assignments));
    if (session->environment_override_assignments == NULL)
        return CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED;
    for (row = 0u; row < override_count; ++row) {
        const scxml_cmeta_environment_override *override = &overrides[row];
        scxml_location location = {0};
        scxml_expr_diagnostic diagnostic = {0};
        size_t assignment;
        size_t matched_assignment = SIZE_MAX;
        size_t prior;
        if (override->location == NULL || override->location_size == 0u ||
            override->location_size > program->cmeta_max_source_bytes ||
            scxml_location_compile(
                &location, override->location, override->location_size,
                program->cmeta_root, program->cmeta_max_path_depth, true,
                &diagnostic) != SCXML_EXPR_OK)
            return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        for (assignment = program->top_level_data_initializer_first;
             assignment < program->top_level_data_initializer_first +
                              program->top_level_data_initializer_count;
             ++assignment) {
            if (scxml_assign_destination_matches(
                    &program->assignments[assignment], &location)) {
                if (matched_assignment != SIZE_MAX)
                    return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
                matched_assignment = assignment;
            }
        }
        if (matched_assignment == SIZE_MAX)
            return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        for (prior = 0u; prior < row; ++prior) {
            if (session->environment_override_assignments[prior] ==
                matched_assignment)
                return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        }
        session->environment_override_assignments[row] = matched_assignment;
    }
    session->environment_override_count = override_count;
    return CFLOW_STATECHART_INSTANCE_OK;
}

static cflow_statechart_instance_status initialize_cmeta_state(
    const scxml_session_impl *session, const void *initial_state,
    void **out_state, bool *out_managed) {
    const scxml_program_impl *program =
        session != NULL ? session->program : NULL;
    const cmeta_type_desc *type;
    void *state;
    bool managed;
    if (program == NULL || program->cmeta_root == NULL ||
        program->cmeta_root->storage_type == NULL || initial_state == NULL ||
        out_state == NULL || out_managed == NULL)
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    type = program->cmeta_root->storage_type;
    managed = cmeta_type_require_traits(
                  type, CMETA_TRAIT_TRIVIAL_COPY |
                            CMETA_TRAIT_TRIVIAL_DESTROY) != CMETA_OK;
    state = malloc(type->size);
    if (state == NULL) return CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED;
    if (managed) {
        if (cmeta_type_require_traits(
                type, CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                          CMETA_TRAIT_DESTROY) != CMETA_OK ||
            !type->traits->copy_construct(state, initial_state)) {
            free(state);
            return CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED;
        }
    } else {
        memcpy(state, initial_state, type->size);
    }
    *out_state = state;
    *out_managed = managed;
    return CFLOW_STATECHART_INSTANCE_OK;
}

static void destroy_initialized_cmeta_state(
    const scxml_program_impl *program, void *state, bool managed) {
    if (state == NULL || program == NULL || program->cmeta_root == NULL ||
        program->cmeta_root->storage_type == NULL)
        return;
    if (managed)
        program->cmeta_root->storage_type->traits->destroy(state);
    free(state);
}

static cflow_statechart_instance_status completion_projection_limits(
    const scxml_program_impl *program,
    size_t *out_field_capacity, size_t *out_stable_id_capacity) {
    size_t field_capacity = 0u;
    size_t stable_id_capacity = 0u;
    size_t index;
    if (program == NULL || out_field_capacity == NULL ||
        out_stable_id_capacity == NULL ||
        (program->done_data_count != 0u && program->done_data == NULL))
        return CFLOW_STATECHART_INSTANCE_INVALID_CONFIGURATION;
    for (index = 0u; index < program->done_data_count; ++index) {
        const scxml_done_data_descriptor *descriptor =
            &program->done_data[index];
        size_t required;
        if (descriptor->assignment_count == 0u) continue;
        if (!cmeta_data_desc_valid(&descriptor->schema))
            return CFLOW_STATECHART_INSTANCE_INVALID_CONFIGURATION;
        required = strlen(descriptor->schema.stable_id);
        if (!scxml_analyze_checked_add(
                required, SCXML_COMPLETION_DATA_SUBSET_SUFFIX_SIZE,
                &required) ||
            !scxml_analyze_checked_add(
                required, descriptor->assignment_count, &required) ||
            !scxml_analyze_checked_add(required, 1u, &required))
            return CFLOW_STATECHART_INSTANCE_LIMIT_EXCEEDED;
        if (descriptor->assignment_count > field_capacity)
            field_capacity = descriptor->assignment_count;
        if (required > stable_id_capacity)
            stable_id_capacity = required;
    }
    *out_field_capacity = field_capacity;
    *out_stable_id_capacity = stable_id_capacity;
    return CFLOW_STATECHART_INSTANCE_OK;
}

static cflow_statechart_instance_status scxml_session_init_model(
    scxml_session *session,
    const scxml_session_config *config,
    scxml_data_model data_model, const void *cmeta_initial_state,
    const scxml_cmeta_environment_override *environment_overrides,
    size_t environment_override_count,
    const scxml_cmeta_session_options_v3 *resource_options,
    bool quickjs_profile) {
    scxml_session_impl *impl;
    const scxml_program_impl *program;
    cflow_statechart_instance_config native_config;
    cflow_statechart_instance_hooks instance_hooks = {0};
    cflow_statechart_instance_status status;
    turbo_uuid_t session_uuid;
    size_t invocation_effect_capacity = 0u;
    size_t completion_data_capacity = 0u;
    size_t completion_projection_field_capacity = 0u;
    size_t completion_projection_stable_id_capacity = 0u;
    size_t completion_projection_field_total = 0u;
    size_t completion_projection_stable_id_total = 0u;
    size_t index;
    bool requires_forward = false;
    uint64_t event_io_capabilities = 0u;
    uint64_t invoke_capabilities = 0u;
    char scxml_location[sizeof("#_scxml_") - 1u +
                        TURBO_UUID_STRING_SIZE];
    int scxml_location_size;
    void *initialized_cmeta_state = NULL;
    bool initialized_cmeta_state_managed = false;
    if (session == NULL || session->impl != NULL || config == NULL ||
        config->program == NULL || config->program->impl == NULL ||
        !event_io_adapter_valid(config->event_io) ||
        !invoke_adapter_valid(config->invoke))
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    event_io_capabilities = config->event_io != NULL
        ? config->event_io->capabilities : 0u;
    invoke_capabilities = config->invoke != NULL
        ? config->invoke->capabilities : 0u;
    program = (const scxml_program_impl *)config->program->impl;
    if (program->data_model != data_model ||
        program->quickjs_profile != quickjs_profile ||
        (data_model == SCXML_DATA_MODEL_CMETA &&
         cmeta_initial_state == NULL)) {
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    }
    if ((program->late_initializer_count != 0u ||
         program->supplemental_scope.slot_count != 0u) &&
        config->effect_capacity == 0u) {
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    }
    if (program->done_data_count != 0u &&
        !scxml_analyze_checked_add(
            config->completion_capacity, 1u,
            &completion_data_capacity))
        return CFLOW_STATECHART_INSTANCE_LIMIT_EXCEEDED;
    status = completion_projection_limits(
        program, &completion_projection_field_capacity,
        &completion_projection_stable_id_capacity);
    if (status != CFLOW_STATECHART_INSTANCE_OK) return status;
    if (!scxml_analyze_checked_multiply(
            completion_data_capacity,
            completion_projection_field_capacity,
            &completion_projection_field_total) ||
        !scxml_analyze_checked_multiply(
            completion_data_capacity,
            completion_projection_stable_id_capacity,
            &completion_projection_stable_id_total))
        return CFLOW_STATECHART_INSTANCE_LIMIT_EXCEEDED;
    if ((program->requirements & SCXML_REQUIREMENT_EVENT_IO) != 0u) {
        if (config->event_io == NULL ||
            config->effect_capacity == 0u ||
            config->adapter_internal_event_capacity == 0u ||
            (event_io_capabilities &
             SCXML_EVENT_IO_CAP_SEND) == 0u) {
            return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        }
    }
    if ((program->requirements & SCXML_REQUIREMENT_DELAYED_SEND) != 0u &&
        (config->delayed_send_capacity == 0u ||
         (event_io_capabilities &
          SCXML_EVENT_IO_CAP_DELAYED_SEND) == 0u)) {
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    }
    if ((program->requirements & SCXML_REQUIREMENT_CANCEL) != 0u &&
        (event_io_capabilities &
         SCXML_EVENT_IO_CAP_CANCEL) == 0u) {
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    }
    if ((program->requirements & SCXML_REQUIREMENT_INVOKE) != 0u &&
        (config->invoke == NULL ||
         config->effect_capacity == 0u ||
         config->adapter_internal_event_capacity == 0u ||
         config->invocation_capacity < program->invocation_count ||
         (invoke_capabilities &
          (SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL)) !=
             (SCXML_INVOKE_CAP_START |
              SCXML_INVOKE_CAP_CANCEL))) {
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    }
    for (index = 0u; index < program->invocation_count; ++index) {
        if (program->invocations[index].autoforward) {
            requires_forward = true;
            break;
        }
    }
    if (requires_forward &&
        (invoke_capabilities & SCXML_INVOKE_CAP_FORWARD) == 0u)
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    if (config->invoke != NULL &&
        !scxml_analyze_checked_add(config->effect_capacity, 1u,
                     &invocation_effect_capacity))
        return CFLOW_STATECHART_INSTANCE_LIMIT_EXCEEDED;
    if (((program->requirements & SCXML_REQUIREMENT_PAYLOAD) != 0u &&
         (config->event_io == NULL ||
          (event_io_capabilities & SCXML_EVENT_IO_CAP_PAYLOAD) == 0u)) ||
        ((program->requirements &
          SCXML_REQUIREMENT_INVOKE_PAYLOAD) != 0u &&
         (config->invoke == NULL ||
          (invoke_capabilities & SCXML_INVOKE_CAP_PAYLOAD) == 0u)))
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    if (((program->requirements & SCXML_REQUIREMENT_CONTENT) != 0u &&
         (config->event_io == NULL ||
          (event_io_capabilities & SCXML_EVENT_IO_CAP_CONTENT) == 0u)) ||
        ((program->requirements &
          SCXML_REQUIREMENT_INVOKE_CONTENT) != 0u &&
         (config->invoke == NULL ||
          (invoke_capabilities & SCXML_INVOKE_CAP_CONTENT) == 0u)))
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    impl = (scxml_session_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED;
    impl->program = program;
    impl->system_values.is_data_bound = session_data_range_is_bound;
    impl->system_values.data_bound_user = impl;
    impl->system_values.datamodel_user = impl;
    if (turbo_uuid_v4_generate(&session_uuid) != TURBO_OK ||
        turbo_uuid_format(
            &session_uuid, impl->session_id,
            sizeof(impl->session_id)) != TURBO_OK) {
        free(impl);
        return CFLOW_STATECHART_INSTANCE_INVALID_CONFIGURATION;
    }
    scxml_location_size = snprintf(
        scxml_location, sizeof(scxml_location),
        "#_scxml_%s", impl->session_id);
    if (scxml_location_size <= 0 ||
        (size_t)scxml_location_size >= sizeof(scxml_location)) {
        free(impl);
        return CFLOW_STATECHART_INSTANCE_INVALID_CONFIGURATION;
    }
    impl->system_values.session_id =
        (scxml_expr_string_view){
            impl->session_id, TURBO_UUID_STRING_LENGTH};
    status = retain_ioprocessors(
        impl, config->ioprocessors, config->ioprocessor_count,
        scxml_location, (size_t)scxml_location_size,
        config->max_storage_bytes);
    if (status != CFLOW_STATECHART_INSTANCE_OK) {
        session_free_storage(impl);
        free(impl);
        return status;
    }
    scxml_runtime_clear_current_event_metadata(impl);
    if (data_model == SCXML_DATA_MODEL_CMETA) {
        if (program->document_name_size != 0u) {
            impl->system_name =
                (char *)malloc(program->document_name_size);
            if (impl->system_name == NULL) {
                free(impl);
                return CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED;
            }
            memcpy(impl->system_name, program->document_name,
                   program->document_name_size);
        }
        impl->system_values.name =
            (scxml_expr_string_view){
                program->document_name_size != 0u ? impl->system_name : "",
                program->document_name_size};
        status = retain_environment_overrides(
            impl, environment_overrides, environment_override_count);
        if (status != CFLOW_STATECHART_INSTANCE_OK) {
            session_free_storage(impl);
            free(impl);
            return status;
        }
        status = retain_data_resource_options(impl, resource_options);
        if (status != CFLOW_STATECHART_INSTANCE_OK) {
            session_free_storage(impl);
            free(impl);
            return status;
        }
        status = retain_supplemental_scope(
            impl, config->max_storage_bytes);
        if (status != CFLOW_STATECHART_INSTANCE_OK) {
            session_free_storage(impl);
            free(impl);
            return status;
        }
        impl->guard_binding_count = program->guard_binding_count;
        impl->guard_bindings =
            (cflow_statechart_guard_binding *)scxml_emit_allocate_rows(
                impl->guard_binding_count, sizeof(*impl->guard_bindings));
        impl->guard_users = (scxml_session_guard_user *)scxml_emit_allocate_rows(
            impl->guard_binding_count, sizeof(*impl->guard_users));
    }
    impl->binding_count = program->binding_count;
    impl->bindings = (cflow_statechart_executable_binding *)scxml_emit_allocate_rows(
        impl->binding_count, sizeof(*impl->bindings));
    impl->binding_users = (scxml_session_binding_user *)scxml_emit_allocate_rows(
        impl->binding_count, sizeof(*impl->binding_users));
    impl->late_initializer_count = program->late_initializer_count;
    impl->late_initializers =
        (scxml_late_initializer_state *)scxml_emit_allocate_rows(
            impl->late_initializer_count,
            sizeof(*impl->late_initializers));
    impl->delayed_send_capacity = config->delayed_send_capacity;
    impl->delayed_sends = (scxml_delayed_send *)scxml_emit_allocate_rows(
        impl->delayed_send_capacity, sizeof(*impl->delayed_sends));
    impl->prepared_effect_capacity = config->effect_capacity;
    impl->prepared_effects = (scxml_prepared_effect *)scxml_emit_allocate_rows(
        impl->prepared_effect_capacity, sizeof(*impl->prepared_effects));
    impl->external_metadata_capacity = config->external_event_capacity;
    impl->external_metadata_rows =
        (scxml_external_event_metadata_row *)scxml_emit_allocate_rows(
            impl->external_metadata_capacity,
            sizeof(*impl->external_metadata_rows));
    impl->completion_data_capacity = completion_data_capacity;
    impl->completion_data_slots =
        (scxml_completion_data_slot *)scxml_emit_allocate_rows(
            impl->completion_data_capacity,
            sizeof(*impl->completion_data_slots));
    impl->completion_projection_field_capacity =
        completion_projection_field_capacity;
    impl->completion_projection_stable_id_capacity =
        completion_projection_stable_id_capacity;
    impl->completion_projection_fields =
        (cmeta_data_field_desc *)scxml_emit_allocate_rows(
            completion_projection_field_total,
            sizeof(*impl->completion_projection_fields));
    impl->completion_projection_stable_ids =
        (char *)scxml_emit_allocate_rows(
            completion_projection_stable_id_total,
            sizeof(*impl->completion_projection_stable_ids));
    impl->payload_scratch_capacity = program->max_payload_entries;
    impl->payload_scratch =
        (scxml_payload_entry *)scxml_emit_allocate_rows(
            impl->payload_scratch_capacity,
            sizeof(*impl->payload_scratch));
    impl->invocation_capacity = config->invocation_capacity;
    impl->invocation_rows = (scxml_invocation_row *)scxml_emit_allocate_rows(
        impl->invocation_capacity, sizeof(*impl->invocation_rows));
    /* One bounded probe row lets the native effect journal remain the sole
       authority for EFFECT_JOURNAL_FULL. A rejected ticket releases it
       immediately and it is never retained beyond the failed stage call. */
    impl->invocation_effect_capacity = invocation_effect_capacity;
    impl->invocation_effects =
        (scxml_invocation_lifecycle_effect *)scxml_emit_allocate_rows(
            impl->invocation_effect_capacity,
            sizeof(*impl->invocation_effects));
    if ((impl->binding_count != 0u &&
         (impl->bindings == NULL || impl->binding_users == NULL)) ||
        (impl->guard_binding_count != 0u &&
         (impl->guard_bindings == NULL || impl->guard_users == NULL)) ||
        (impl->late_initializer_count != 0u &&
         impl->late_initializers == NULL)) {
        session_free_storage(impl);
        free(impl);
        return CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED;
    }
    if ((impl->delayed_send_capacity != 0u &&
         impl->delayed_sends == NULL) ||
        (impl->prepared_effect_capacity != 0u &&
         impl->prepared_effects == NULL) ||
        (impl->external_metadata_capacity != 0u &&
         impl->external_metadata_rows == NULL) ||
        (impl->completion_data_capacity != 0u &&
         impl->completion_data_slots == NULL) ||
        (completion_projection_field_total != 0u &&
         impl->completion_projection_fields == NULL) ||
        (completion_projection_stable_id_total != 0u &&
         impl->completion_projection_stable_ids == NULL) ||
        (impl->payload_scratch_capacity != 0u &&
         impl->payload_scratch == NULL) ||
        (impl->invocation_capacity != 0u &&
         impl->invocation_rows == NULL) ||
        (impl->invocation_effect_capacity != 0u &&
         impl->invocation_effects == NULL)) {
        session_free_storage(impl);
        free(impl);
        return CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED;
    }
    for (index = 0u; index < impl->completion_data_capacity; ++index) {
        scxml_completion_data_slot *slot =
            &impl->completion_data_slots[index];
        slot->projection_field_capacity =
            impl->completion_projection_field_capacity;
        slot->projection_stable_id_capacity =
            impl->completion_projection_stable_id_capacity;
        if (slot->projection_field_capacity != 0u)
            slot->projection_fields =
                impl->completion_projection_fields +
                index * slot->projection_field_capacity;
        if (slot->projection_stable_id_capacity != 0u)
            slot->projection_stable_id =
                impl->completion_projection_stable_ids +
                index * slot->projection_stable_id_capacity;
    }
    turbo_mutex_init(&impl->registry_lock);
    if (impl->registry_lock == NULL) {
        session_free_storage(impl);
        free(impl);
        return CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED;
    }
    for (index = 0u; index < impl->binding_count; ++index) {
        impl->binding_users[index] = (scxml_session_binding_user){
            (const scxml_block *)program->bindings[index].user, impl};
        impl->bindings[index] = (cflow_statechart_executable_binding){
            .id = program->bindings[index].id,
            .user = &impl->binding_users[index],
            .contextual_fn = scxml_runtime_execute_session_block};
    }
    for (index = 0u; index < impl->guard_binding_count; ++index) {
        impl->guard_users[index] = (scxml_session_guard_user){
            &program->guard_users[index], impl};
        impl->guard_bindings[index] = (cflow_statechart_guard_binding){
            .id = program->guard_bindings[index].id,
            .user = &impl->guard_users[index],
            .contextual_fn = scxml_emit_evaluate_session_transition_guard};
    }
    if (config->event_io != NULL) {
        impl->event_io = *config->event_io;
        impl->adapter_user = config->adapter_user;
        impl->has_event_io = true;
    }
    if (config->invoke != NULL) {
        impl->invoke = *config->invoke;
        impl->invoke_user = config->invoke_user;
        impl->has_invoke = true;
    }
    impl->next_send_token = UINT64_C(1);
    impl->next_invocation_token = UINT64_C(1);
    impl->next_external_metadata_token =
        SCXML_EXTERNAL_METADATA_TOKEN_BIT | UINT64_C(1);
    impl->current_completion_data_slot = SIZE_MAX;
    impl->next_completion_data_sequence = UINT64_C(1);
    atomic_init(&impl->adapter_close_called, false);
    atomic_init(&impl->invoke_close_called, false);
    instance_hooks = (cflow_statechart_instance_hooks){
        .abi_version = CFLOW_STATECHART_INSTANCE_HOOKS_ABI_V4,
        .struct_size = sizeof(instance_hooks),
        .on_host_transaction = scxml_runtime_host_transaction};
    if (data_model == SCXML_DATA_MODEL_CMETA &&
        (program->data_initializer_count != 0u || quickjs_profile)) {
        status = initialize_cmeta_state(
            impl, cmeta_initial_state,
            &initialized_cmeta_state, &initialized_cmeta_state_managed);
        if (status != CFLOW_STATECHART_INSTANCE_OK) {
            session_close_adapter(impl);
            turbo_mutex_destroy(&impl->registry_lock);
            session_free_storage(impl);
            free(impl);
            return status;
        }
    }
    if (quickjs_profile) {
        const char *quickjs_error = NULL;
        if (initialized_cmeta_state == NULL ||
            program->root_script_count > program->script_count ||
            !scxml_quickjs_session_runtime_init(impl, &quickjs_error)) {
            destroy_initialized_cmeta_state(
                program, initialized_cmeta_state,
                initialized_cmeta_state_managed);
            session_close_adapter(impl);
            turbo_mutex_destroy(&impl->registry_lock);
            session_free_storage(impl);
            free(impl);
            return CFLOW_STATECHART_INSTANCE_INVALID_CONFIGURATION;
        }
    }
    native_config = (cflow_statechart_instance_config){
        .statechart = &program->statechart,
        .initial_state = data_model == SCXML_DATA_MODEL_CMETA
                             ? (initialized_cmeta_state != NULL
                                    ? initialized_cmeta_state
                                    : cmeta_initial_state)
                             : &program->null_value,
        .guards = data_model == SCXML_DATA_MODEL_CMETA
                      ? impl->guard_bindings : program->guard_bindings,
        .guard_count = data_model == SCXML_DATA_MODEL_CMETA
                           ? impl->guard_binding_count
                           : program->guard_binding_count,
        .executables = impl->bindings,
        .executable_count = impl->binding_count,
        .external_event_capacity = config->external_event_capacity,
        .internal_event_capacity = config->internal_event_capacity,
        .completion_capacity = config->completion_capacity,
        .microstep_limit = config->microstep_limit,
        .max_storage_bytes = config->max_storage_bytes,
        .executor = config->executor,
        .clock = config->clock,
        .timer_capacity = config->timer_capacity,
        .effect_capacity = config->effect_capacity,
        .adapter_internal_event_capacity =
            config->adapter_internal_event_capacity,
        .hooks = &instance_hooks,
        .hook_user = impl};
    status = cflow_statechart_instance_init(&impl->instance, &native_config);
    destroy_initialized_cmeta_state(
        program, initialized_cmeta_state, initialized_cmeta_state_managed);
    if (status != CFLOW_STATECHART_INSTANCE_OK) {
        session_close_adapter(impl);
        turbo_mutex_destroy(&impl->registry_lock);
        session_free_storage(impl);
        free(impl);
        return status;
    }
    session->impl = impl;
    return CFLOW_STATECHART_INSTANCE_OK;
}

cflow_statechart_instance_status scxml_session_init(
    scxml_session *session,
    const scxml_session_config *config) {
    return scxml_session_init_model(
        session, config, SCXML_DATA_MODEL_NULL, NULL, NULL, 0u, NULL, false);
}

cflow_statechart_instance_status scxml_session_init_cmeta(
    scxml_session *session,
    const scxml_session_config *config,
    const scxml_cmeta_session_options_v1 *options) {
    if (options == NULL ||
        options->abi_version !=
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1 ||
        options->struct_size < sizeof(*options) ||
        options->initial_state == NULL) {
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    }
    return scxml_session_init_model(
        session, config, SCXML_DATA_MODEL_CMETA,
        options->initial_state, NULL, 0u, NULL, false);
}

cflow_statechart_instance_status scxml_session_init_cmeta_v2(
    scxml_session *session,
    const scxml_session_config *config,
    const scxml_cmeta_session_options_v2 *options) {
    if (options == NULL ||
        options->abi_version != SCXML_CMETA_SESSION_OPTIONS_ABI_V2 ||
        options->struct_size < sizeof(*options) ||
        options->initial_state == NULL ||
        (options->environment_override_count != 0u &&
         options->environment_overrides == NULL))
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    return scxml_session_init_model(
        session, config, SCXML_DATA_MODEL_CMETA,
        options->initial_state, options->environment_overrides,
        options->environment_override_count, NULL, false);
}

cflow_statechart_instance_status scxml_session_init_cmeta_v3(
    scxml_session *session,
    const scxml_session_config *config,
    const scxml_cmeta_session_options_v3 *options) {
    if (options == NULL ||
        options->abi_version != SCXML_CMETA_SESSION_OPTIONS_ABI_V3 ||
        options->struct_size < sizeof(*options) ||
        options->initial_state == NULL ||
        (options->environment_override_count != 0u &&
         options->environment_overrides == NULL) ||
        (options->data_resources != NULL &&
         (!data_resource_adapter_valid(options->data_resources) ||
          options->cbind_scratch_bytes == 0u ||
          options->max_data_depth == 0u ||
          options->max_data_container_items == 0u ||
          options->max_data_buffer_bytes == 0u)))
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    return scxml_session_init_model(
        session, config, SCXML_DATA_MODEL_CMETA,
        options->initial_state, options->environment_overrides,
        options->environment_override_count, options, false);
}

cflow_statechart_instance_status scxml_session_init_quickjs_model(
    scxml_session *session, const scxml_session_config *config,
    const scxml_quickjs_session_options_v1 *options) {
    if (options == NULL ||
        options->abi_version != SCXML_QUICKJS_SESSION_OPTIONS_ABI_V1 ||
        options->struct_size < sizeof(*options) ||
        options->initial_state == NULL)
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    return scxml_session_init_model(
        session, config, SCXML_DATA_MODEL_CMETA,
        options->initial_state, NULL, 0u, NULL, true);
}

cflow_statechart_instance_status scxml_session_init_quickjs(
    scxml_session *session, const scxml_session_config *config,
    const scxml_quickjs_session_options_v1 *options) {
    return scxml_quickjs_session_init(session, config, options);
}

cflow_mailbox_status scxml_session_try_send(
    scxml_session *session, const cflow_event_view *event) {
    scxml_session_impl *impl = session != NULL
        ? (scxml_session_impl *)session->impl : NULL;
    return impl != NULL
        ? cflow_statechart_instance_try_send(&impl->instance, event)
        : CFLOW_MAILBOX_INVALID_ARGUMENT;
}

static cflow_mailbox_status session_try_send_with_metadata_impl(
    scxml_session_impl *impl, const cflow_event_view *event,
    const scxml_event_metadata *metadata, const char *name,
    size_t name_size) {
    scxml_external_event_metadata_row *row = NULL;
    uint64_t token = 0u;
    cflow_mailbox_status status;
    if (impl == NULL || event == NULL || metadata == NULL ||
        metadata->abi_version != SCXML_EVENT_METADATA_ABI ||
        metadata->struct_size != sizeof(*metadata) ||
        !scxml_runtime_metadata_field_valid(
            metadata->send_id, metadata->send_id_size) ||
        !scxml_runtime_metadata_field_valid(
            metadata->origin, metadata->origin_size) ||
        !scxml_runtime_metadata_field_valid(
            metadata->origin_type, metadata->origin_type_size) ||
        !scxml_runtime_metadata_field_valid(
            metadata->invoke_id, metadata->invoke_id_size))
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    row = scxml_runtime_reserve_event_metadata(
        impl, metadata, name, name_size, &token);
    if (row == NULL) return CFLOW_MAILBOX_FULL;
    if (!scxml_analyze_attach_event_content(impl, row, &metadata->data)) {
        scxml_runtime_release_event_metadata(row);
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    }
    status = cflow_statechart_instance_try_send_tagged(
        &impl->instance, event, token);
    if (status != CFLOW_MAILBOX_OK) scxml_runtime_release_event_metadata(row);
    return status;
}

cflow_mailbox_status scxml_session_try_send_with_metadata(
    scxml_session *session, const cflow_event_view *event,
    const scxml_event_metadata *metadata) {
    scxml_session_impl *impl = session != NULL
        ? (scxml_session_impl *)session->impl : NULL;
    return session_try_send_with_metadata_impl(
        impl, event, metadata, NULL, 0u);
}

cflow_mailbox_status scxml_session_try_send_named_with_metadata(
    scxml_session *session, const char *name, size_t name_size,
    const scxml_event_metadata *metadata) {
    scxml_session_impl *impl = session != NULL
        ? (scxml_session_impl *)session->impl : NULL;
    cflow_event_view event = {0};
    cflow_event_id event_id = 0u;
    if (impl == NULL || name == NULL || name_size == 0u ||
        name_size > SCXML_EVENT_METADATA_CAPACITY ||
        !scxml_program_route_external_name(
            impl->program, name, name_size, &event_id))
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    event = (cflow_event_view){
        event_id, &cmeta_type_bool, &impl->program->null_value};
    return session_try_send_with_metadata_impl(
        impl, &event, metadata, name, name_size);
}

cflow_mailbox_status scxml_session_report_invoke_event(
    scxml_session *session, uint64_t token,
    const cflow_event_view *event) {
    scxml_session_impl *impl = session != NULL
        ? (scxml_session_impl *)session->impl : NULL;
    cflow_mailbox_status status;
    bool live = false;
    size_t index;
    if (impl == NULL || !impl->has_invoke || token == 0u || event == NULL)
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    turbo_mutex_lock(&impl->registry_lock);
    for (index = 0u; index < impl->program->invocation_count; ++index) {
        if (impl->invocation_rows[index].state == SCXML_INVOCATION_ACTIVE &&
            impl->invocation_rows[index].token == token) {
            live = true;
            break;
        }
    }
    if (!live) {
        scxml_runtime_increment_u64(&impl->invoke_stats.returned_rejected);
        turbo_mutex_unlock(&impl->registry_lock);
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    }
    turbo_mutex_unlock(&impl->registry_lock);
    status = cflow_statechart_instance_try_send_tagged(
        &impl->instance, event, token);
    turbo_mutex_lock(&impl->registry_lock);
    if (status == CFLOW_MAILBOX_OK)
        scxml_runtime_increment_u64(&impl->invoke_stats.returned_accepted);
    else
        scxml_runtime_increment_u64(&impl->invoke_stats.returned_rejected);
    turbo_mutex_unlock(&impl->registry_lock);
    return status;
}

cflow_mailbox_status scxml_session_report_invoke_done(
    scxml_session *session, uint64_t token) {
    scxml_session_impl *impl = session != NULL
        ? (scxml_session_impl *)session->impl : NULL;
    const bool null_value = false;
    cflow_event_view event = {0};
    cflow_mailbox_status status;
    size_t index;
    if (impl == NULL || !impl->has_invoke || token == 0u)
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    turbo_mutex_lock(&impl->registry_lock);
    for (index = 0u; index < impl->program->invocation_count; ++index) {
        if (impl->invocation_rows[index].state == SCXML_INVOCATION_ACTIVE &&
            impl->invocation_rows[index].token == token) {
            event = (cflow_event_view){
                impl->program->invocations[index].done_event,
                &cmeta_type_bool, &null_value};
            break;
        }
    }
    if (event.id == 0u) {
        scxml_runtime_increment_u64(&impl->invoke_stats.returned_rejected);
        turbo_mutex_unlock(&impl->registry_lock);
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    }
    turbo_mutex_unlock(&impl->registry_lock);
    status = cflow_statechart_instance_try_send_tagged(
        &impl->instance, &event, token);
    turbo_mutex_lock(&impl->registry_lock);
    if (status == CFLOW_MAILBOX_OK)
        scxml_runtime_increment_u64(&impl->invoke_stats.returned_accepted);
    else
        scxml_runtime_increment_u64(&impl->invoke_stats.returned_rejected);
    turbo_mutex_unlock(&impl->registry_lock);
    return status;
}

cflow_mailbox_status scxml_session_report_adapter_error(
    scxml_session *session,
    scxml_adapter_error_kind kind) {
    scxml_session_impl *impl = session != NULL
        ? (scxml_session_impl *)session->impl : NULL;
    const scxml_program_name *event;
    const char *name;
    size_t name_size;
    cflow_event_view reported;
    if (impl == NULL) return CFLOW_MAILBOX_INVALID_ARGUMENT;
    if (kind == SCXML_ADAPTER_ERROR_KIND_EXECUTION) {
        name = SCXML_ERROR_EXECUTION_EVENT;
        name_size = sizeof(SCXML_ERROR_EXECUTION_EVENT) - 1u;
    } else if (kind == SCXML_ADAPTER_ERROR_KIND_COMMUNICATION) {
        name = SCXML_ERROR_COMMUNICATION_EVENT;
        name_size = sizeof(SCXML_ERROR_COMMUNICATION_EVENT) - 1u;
    } else {
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    }
    event = scxml_program_find_name(
        impl->program->event_names, impl->program->event_name_count,
        name, name_size);
    if (event == NULL) return CFLOW_MAILBOX_INVALID_ARGUMENT;
    reported = (cflow_event_view){
        (cflow_event_id)event->id, &cmeta_type_bool,
        &impl->program->null_value};
    return cflow_statechart_instance_try_send_internal(
        &impl->instance, &reported);
}

bool scxml_session_report_send_done(
    scxml_session *session, const char *send_id, size_t send_id_size) {
    scxml_session_impl *impl = session != NULL
        ? (scxml_session_impl *)session->impl : NULL;
    scxml_delayed_send *row;
    if (impl == NULL || send_id == NULL || send_id_size == 0u)
        return false;
    turbo_mutex_lock(&impl->registry_lock);
    row = scxml_runtime_find_delayed_send_locked(impl, send_id, send_id_size, NULL);
    if (row == NULL ||
        (row->state != SCXML_DELAYED_ACTIVE &&
         (row->state != SCXML_DELAYED_CANCEL_RESERVED ||
          row->previous_state != SCXML_DELAYED_ACTIVE))) {
        turbo_mutex_unlock(&impl->registry_lock);
        return false;
    }
    if (row->state == SCXML_DELAYED_CANCEL_RESERVED)
        row->previous_state = SCXML_DELAYED_FREE;
    else
        *row = (scxml_delayed_send){0};
    turbo_mutex_unlock(&impl->registry_lock);
    return true;
}

void scxml_session_close(scxml_session *session) {
    scxml_session_impl *impl = session != NULL
        ? (scxml_session_impl *)session->impl : NULL;
    if (impl == NULL) return;
    cflow_statechart_instance_close(&impl->instance);
    session_close_adapter(impl);
}

void scxml_session_cancel(scxml_session *session) {
    scxml_session_impl *impl = session != NULL
        ? (scxml_session_impl *)session->impl : NULL;
    if (impl == NULL) return;
    cflow_statechart_instance_request_exit(&impl->instance);
    session_close_adapter(impl);
}

bool scxml_session_get_stats(
    const scxml_session *session,
    cflow_statechart_instance_stats *out) {
    const scxml_session_impl *impl = session != NULL
        ? (const scxml_session_impl *)session->impl : NULL;
    return impl != NULL &&
        cflow_statechart_instance_get_stats(&impl->instance, out);
}

bool scxml_session_get_invoke_stats(
    const scxml_session *session, scxml_invoke_stats *out) {
    scxml_session_impl *impl = session != NULL
        ? (scxml_session_impl *)session->impl : NULL;
    if (impl == NULL || out == NULL) return false;
    turbo_mutex_lock(&impl->registry_lock);
    *out = impl->invoke_stats;
    turbo_mutex_unlock(&impl->registry_lock);
    return true;
}

scxml_location_status scxml_session_copy_ioprocessor_location(
    const scxml_session *session, const char *type, size_t type_size,
    char *out_location, size_t location_capacity,
    size_t *out_required_capacity) {
    const scxml_session_impl *impl = session != NULL
        ? (const scxml_session_impl *)session->impl : NULL;
    const scxml_ioprocessor_descriptor *matched = NULL;
    size_t required;
    size_t index;
    if (impl == NULL || type == NULL || type_size == 0u ||
        out_required_capacity == NULL)
        return SCXML_LOCATION_INVALID_ARGUMENT;
    for (index = 0u; index < impl->ioprocessor_count; ++index) {
        const scxml_ioprocessor_descriptor *candidate =
            &impl->ioprocessors[index];
        if (descriptor_field_equal(
                candidate->type, candidate->type_size, type, type_size)) {
            matched = candidate;
            break;
        }
    }
    if (matched == NULL || !scxml_analyze_checked_add(
            matched->location_size, 1u, &required))
        return SCXML_LOCATION_INVALID_ARGUMENT;
    if (out_location == NULL || location_capacity < required) {
        *out_required_capacity = required;
        return SCXML_LOCATION_TOO_SMALL;
    }
    memcpy(out_location, matched->location, required);
    *out_required_capacity = required;
    return SCXML_LOCATION_OK;
}

scxml_location_status scxml_session_copy_location(
    const scxml_session *session, char *out_location,
    size_t location_capacity, size_t *out_required_capacity) {
    static const char scxml_type[] = SCXML_SCXML_IOPROCESSOR_TYPE;
    return scxml_session_copy_ioprocessor_location(
        session, scxml_type, sizeof(scxml_type) - 1u,
        out_location, location_capacity, out_required_capacity);
}

const char *scxml_session_error(
    const scxml_session *session) {
    const scxml_session_impl *impl = session != NULL
        ? (const scxml_session_impl *)session->impl : NULL;
    return impl != NULL
        ? cflow_statechart_instance_error(&impl->instance) : NULL;
}

cflow_statechart_instance_status scxml_session_destroy(
    scxml_session *session) {
    scxml_session_impl *impl;
    cflow_statechart_instance_status status;
    if (session == NULL) return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    impl = (scxml_session_impl *)session->impl;
    if (impl == NULL) return CFLOW_STATECHART_INSTANCE_OK;
    cflow_statechart_instance_close(&impl->instance);
    session_close_adapter(impl);
    if (impl->has_event_io &&
        !impl->event_io.is_quiescent(impl->adapter_user))
        return CFLOW_STATECHART_INSTANCE_WOULD_BLOCK;
    if (impl->has_invoke &&
        !impl->invoke.is_quiescent(impl->invoke_user))
        return CFLOW_STATECHART_INSTANCE_WOULD_BLOCK;
    status = cflow_statechart_instance_destroy(&impl->instance);
    if (status != CFLOW_STATECHART_INSTANCE_OK) return status;
    turbo_mutex_destroy(&impl->registry_lock);
    session_free_storage(impl);
    free(impl);
    session->impl = NULL;
    return CFLOW_STATECHART_INSTANCE_OK;
}

bool scxml_program_guard_bindings(
    const scxml_program *program,
    const cflow_statechart_guard_binding **out_bindings,
    size_t *out_count) {
    if (program == NULL || program->impl == NULL || out_bindings == NULL ||
        out_count == NULL) {
        return false;
    }
    {
        const scxml_program_impl *impl =
            (const scxml_program_impl *)program->impl;
        *out_bindings = impl->guard_bindings;
        *out_count = impl->guard_binding_count;
    }
    return true;
}
