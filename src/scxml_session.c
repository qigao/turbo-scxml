#include "scxml_session.h"
#include "scxml_analyze.h"
#include "scxml_emit.h"
#include "scxml_program.h"
#include "scxml_runtime.h"

static bool event_io_adapter_valid(
    const scxml_event_io_adapter_v1 *adapter) {
    const uint64_t known = SCXML_EVENT_IO_CAP_SEND |
        SCXML_EVENT_IO_CAP_DELAYED_SEND |
        SCXML_EVENT_IO_CAP_CANCEL;
    if (adapter == NULL) return true;
    if (adapter->abi_version != SCXML_EVENT_IO_ADAPTER_ABI_V1 ||
        adapter->struct_size < sizeof(*adapter) ||
        (adapter->capabilities & ~known) != 0u ||
        adapter->close == NULL || adapter->is_quiescent == NULL)
        return false;
    if ((adapter->capabilities & SCXML_EVENT_IO_CAP_SEND) != 0u &&
        adapter->prepare_send == NULL)
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

static bool event_io_adapter_v2_valid(
    const scxml_event_io_adapter_v2 *adapter) {
    const uint64_t known = SCXML_EVENT_IO_CAP_SEND |
        SCXML_EVENT_IO_CAP_DELAYED_SEND |
        SCXML_EVENT_IO_CAP_CANCEL |
        SCXML_EVENT_IO_CAP_PAYLOAD;
    if (adapter == NULL) return true;
    if (adapter->abi_version != SCXML_EVENT_IO_ADAPTER_ABI_V2 ||
        adapter->struct_size < sizeof(*adapter) ||
        (adapter->capabilities & ~known) != 0u ||
        adapter->close == NULL || adapter->is_quiescent == NULL)
        return false;
    if ((adapter->capabilities & SCXML_EVENT_IO_CAP_SEND) != 0u &&
        adapter->prepare_send == NULL)
        return false;
    if ((adapter->capabilities & SCXML_EVENT_IO_CAP_PAYLOAD) != 0u &&
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

static bool event_io_adapter_v3_valid(
    const scxml_event_io_adapter_v3 *adapter) {
    const uint64_t known = SCXML_EVENT_IO_CAP_SEND |
        SCXML_EVENT_IO_CAP_DELAYED_SEND |
        SCXML_EVENT_IO_CAP_CANCEL |
        SCXML_EVENT_IO_CAP_PAYLOAD |
        SCXML_EVENT_IO_CAP_CONTENT_V3;
    if (adapter == NULL) return true;
    if (adapter->abi_version != SCXML_EVENT_IO_ADAPTER_ABI_V3 ||
        adapter->struct_size < sizeof(*adapter) ||
        (adapter->capabilities & ~known) != 0u ||
        adapter->close == NULL || adapter->is_quiescent == NULL)
        return false;
    if ((adapter->capabilities & SCXML_EVENT_IO_CAP_SEND) != 0u &&
        adapter->prepare_send == NULL)
        return false;
    if ((adapter->capabilities &
         (SCXML_EVENT_IO_CAP_PAYLOAD |
          SCXML_EVENT_IO_CAP_CONTENT_V3)) != 0u &&
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
    const scxml_invoke_adapter_v1 *adapter) {
    const uint64_t known = SCXML_INVOKE_CAP_START |
        SCXML_INVOKE_CAP_CANCEL | SCXML_INVOKE_CAP_FORWARD;
    if (adapter == NULL) return true;
    if (adapter->abi_version != SCXML_INVOKE_ADAPTER_ABI_V1 ||
        adapter->struct_size < sizeof(*adapter) ||
        (adapter->capabilities & ~known) != 0u ||
        adapter->close == NULL || adapter->is_quiescent == NULL)
        return false;
    if ((adapter->capabilities & SCXML_INVOKE_CAP_START) != 0u &&
        adapter->prepare_start == NULL)
        return false;
    if ((adapter->capabilities & SCXML_INVOKE_CAP_CANCEL) != 0u &&
        adapter->prepare_cancel == NULL)
        return false;
    if ((adapter->capabilities & SCXML_INVOKE_CAP_FORWARD) != 0u &&
        adapter->prepare_forward == NULL)
        return false;
    return true;
}

static bool invoke_adapter_v2_valid(
    const scxml_invoke_adapter_v2 *adapter) {
    const uint64_t known = SCXML_INVOKE_CAP_START |
        SCXML_INVOKE_CAP_CANCEL | SCXML_INVOKE_CAP_FORWARD |
        SCXML_INVOKE_CAP_PAYLOAD;
    if (adapter == NULL) return true;
    if (adapter->abi_version != SCXML_INVOKE_ADAPTER_ABI_V2 ||
        adapter->struct_size < sizeof(*adapter) ||
        (adapter->capabilities & ~known) != 0u ||
        adapter->close == NULL || adapter->is_quiescent == NULL)
        return false;
    if ((adapter->capabilities & SCXML_INVOKE_CAP_START) != 0u &&
        adapter->prepare_start == NULL)
        return false;
    if ((adapter->capabilities & SCXML_INVOKE_CAP_PAYLOAD) != 0u &&
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

static bool invoke_adapter_v3_valid(
    const scxml_invoke_adapter_v3 *adapter) {
    const uint64_t known = SCXML_INVOKE_CAP_START |
        SCXML_INVOKE_CAP_CANCEL | SCXML_INVOKE_CAP_FORWARD |
        SCXML_INVOKE_CAP_PAYLOAD |
        SCXML_INVOKE_CAP_CONTENT_V3;
    if (adapter == NULL) return true;
    if (adapter->abi_version != SCXML_INVOKE_ADAPTER_ABI_V3 ||
        adapter->struct_size < sizeof(*adapter) ||
        (adapter->capabilities & ~known) != 0u ||
        adapter->close == NULL || adapter->is_quiescent == NULL)
        return false;
    if ((adapter->capabilities & SCXML_INVOKE_CAP_START) != 0u &&
        adapter->prepare_start == NULL)
        return false;
    if ((adapter->capabilities &
         (SCXML_INVOKE_CAP_PAYLOAD |
          SCXML_INVOKE_CAP_CONTENT_V3)) != 0u &&
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

static bool session_adapters_v2_valid(
    const scxml_session_config *config,
    const scxml_session_adapters_v2 *adapters) {
    if (adapters == NULL) return true;
    return config != NULL &&
        adapters->abi_version == SCXML_SESSION_ADAPTERS_ABI_V2 &&
        adapters->struct_size >= sizeof(*adapters) &&
        event_io_adapter_v2_valid(adapters->event_io) &&
        invoke_adapter_v2_valid(adapters->invoke) &&
        !(config->event_io != NULL && adapters->event_io != NULL) &&
        !(config->invoke != NULL && adapters->invoke != NULL);
}

static bool session_adapters_v3_valid(
    const scxml_session_config *config,
    const scxml_session_adapters_v3 *adapters) {
    if (adapters == NULL) return true;
    return config != NULL &&
        adapters->abi_version == SCXML_SESSION_ADAPTERS_ABI_V3 &&
        adapters->struct_size >= sizeof(*adapters) &&
        event_io_adapter_v3_valid(adapters->event_io) &&
        invoke_adapter_v3_valid(adapters->invoke) &&
        !(config->event_io != NULL && adapters->event_io != NULL) &&
        !(config->invoke != NULL && adapters->invoke != NULL);
}

static void session_close_adapter(scxml_session_impl *impl) {
    if (impl != NULL && impl->has_event_io &&
        !atomic_exchange_explicit(
            &impl->adapter_close_called, true, memory_order_acq_rel))
        (impl->event_io_abi == SCXML_EVENT_IO_ADAPTER_ABI_V3
             ? impl->event_io_v3.close
         : impl->event_io_abi == SCXML_EVENT_IO_ADAPTER_ABI_V2
             ? impl->event_io_v2.close
             : impl->event_io.close)(impl->adapter_user);
    if (impl != NULL && impl->has_invoke &&
        !atomic_exchange_explicit(
            &impl->invoke_close_called, true, memory_order_acq_rel))
        (impl->invoke_abi == SCXML_INVOKE_ADAPTER_ABI_V3
             ? impl->invoke_v3.close
         : impl->invoke_abi == SCXML_INVOKE_ADAPTER_ABI_V2
             ? impl->invoke_v2.close
             : impl->invoke.close)(impl->invoke_user);
}

static void session_free_storage(scxml_session_impl *impl) {
    size_t index;
    if (impl == NULL) return;
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
    free(impl->late_initializers);
    free(impl->prepared_effects);
    free(impl->external_metadata_rows);
    free(impl->invocation_effects);
    free(impl->invocation_rows);
    free(impl->delayed_sends);
    free(impl->guard_users);
    free(impl->guard_bindings);
    free(impl->binding_users);
    free(impl->bindings);
    free(impl->system_name);
    free(impl->payload_scratch);
    free(impl->payload_scratch_v3);
}

static bool initialization_state_is_active(
    void *user, cflow_machine_state_id state, bool *out_active) {
    (void)user;
    (void)state;
    if (out_active == NULL) return false;
    *out_active = false;
    return true;
}

static cflow_statechart_instance_status initialize_cmeta_state(
    const scxml_program_impl *program, const void *initial_state,
    const scxml_expr_system_values *system_values,
    void **out_state, bool *out_managed) {
    const cmeta_type_desc *type;
    void *state;
    bool managed;
    size_t index;
    if (program == NULL || program->cmeta_root == NULL ||
        program->cmeta_root->storage_type == NULL || initial_state == NULL ||
        system_values == NULL || out_state == NULL || out_managed == NULL)
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
    for (index = 0u; index < program->data_initializer_count; ++index) {
        scxml_expr_diagnostic diagnostic = {0};
        if (scxml_assign_apply_with_system(
                &program->assignments[index], state,
                initialization_state_is_active, NULL, system_values,
                &diagnostic) != SCXML_EXPR_OK) {
            if (managed) type->traits->destroy(state);
            free(state);
            return CFLOW_STATECHART_INSTANCE_INVALID_CONFIGURATION;
        }
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

static cflow_statechart_instance_status scxml_session_init_model(
    scxml_session *session,
    const scxml_session_config *config,
    const scxml_session_adapters_v2 *adapters,
    const scxml_session_adapters_v3 *adapters_v3,
    scxml_data_model data_model, const void *cmeta_initial_state) {
    scxml_session_impl *impl;
    const scxml_program_impl *program;
    cflow_statechart_instance_config native_config;
    cflow_statechart_instance_hooks instance_hooks = {0};
    cflow_statechart_instance_status status;
    turbo_uuid_t session_uuid;
    size_t invocation_effect_capacity = 0u;
    size_t index;
    bool requires_forward = false;
    const scxml_event_io_adapter_v2 *event_io_v2 = NULL;
    const scxml_invoke_adapter_v2 *invoke_v2 = NULL;
    const scxml_event_io_adapter_v3 *event_io_v3 = NULL;
    const scxml_invoke_adapter_v3 *invoke_v3 = NULL;
    uint64_t event_io_capabilities = 0u;
    uint64_t invoke_capabilities = 0u;
    void *initialized_cmeta_state = NULL;
    bool initialized_cmeta_state_managed = false;
    if (session == NULL || session->impl != NULL || config == NULL ||
        config->program == NULL || config->program->impl == NULL ||
        !event_io_adapter_valid(config->event_io) ||
        !invoke_adapter_valid(config->invoke) ||
        !session_adapters_v2_valid(config, adapters) ||
        !session_adapters_v3_valid(config, adapters_v3) ||
        (adapters != NULL && adapters_v3 != NULL))
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    event_io_v2 = adapters != NULL ? adapters->event_io : NULL;
    invoke_v2 = adapters != NULL ? adapters->invoke : NULL;
    event_io_v3 = adapters_v3 != NULL ? adapters_v3->event_io : NULL;
    invoke_v3 = adapters_v3 != NULL ? adapters_v3->invoke : NULL;
    event_io_capabilities = event_io_v3 != NULL
        ? event_io_v3->capabilities
        : event_io_v2 != NULL ? event_io_v2->capabilities
        : config->event_io != NULL ? config->event_io->capabilities : 0u;
    invoke_capabilities = invoke_v3 != NULL
        ? invoke_v3->capabilities
        : invoke_v2 != NULL ? invoke_v2->capabilities
        : config->invoke != NULL ? config->invoke->capabilities : 0u;
    program = (const scxml_program_impl *)config->program->impl;
    if (program->data_model != data_model ||
        (data_model == SCXML_DATA_MODEL_CMETA &&
         cmeta_initial_state == NULL)) {
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    }
    if (program->late_initializer_count != 0u &&
        config->effect_capacity == 0u) {
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    }
    if ((program->requirements & SCXML_REQUIREMENT_EVENT_IO) != 0u) {
        if ((config->event_io == NULL && event_io_v2 == NULL &&
             event_io_v3 == NULL) ||
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
        ((config->invoke == NULL && invoke_v2 == NULL &&
          invoke_v3 == NULL) ||
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
    if ((config->invoke != NULL || invoke_v2 != NULL ||
         invoke_v3 != NULL) &&
        !scxml_analyze_checked_add(config->effect_capacity, 1u,
                     &invocation_effect_capacity))
        return CFLOW_STATECHART_INSTANCE_LIMIT_EXCEEDED;
    if (((program->requirements & SCXML_REQUIREMENT_PAYLOAD) != 0u &&
         ((event_io_v2 == NULL && event_io_v3 == NULL) ||
          (event_io_capabilities & SCXML_EVENT_IO_CAP_PAYLOAD) == 0u)) ||
        ((program->requirements &
          SCXML_REQUIREMENT_INVOKE_PAYLOAD) != 0u &&
         ((invoke_v2 == NULL && invoke_v3 == NULL) ||
          (invoke_capabilities & SCXML_INVOKE_CAP_PAYLOAD) == 0u)))
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    if (((program->requirements & SCXML_REQUIREMENT_CONTENT_V3) != 0u &&
         (event_io_v3 == NULL ||
          (event_io_capabilities & SCXML_EVENT_IO_CAP_CONTENT_V3) == 0u)) ||
        ((program->requirements &
          SCXML_REQUIREMENT_INVOKE_CONTENT_V3) != 0u &&
         (invoke_v3 == NULL ||
          (invoke_capabilities & SCXML_INVOKE_CAP_CONTENT_V3) == 0u)))
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    impl = (scxml_session_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED;
    impl->program = program;
    if (turbo_uuid_v4_generate(&session_uuid) != TURBO_OK ||
        turbo_uuid_format(
            &session_uuid, impl->session_id,
            sizeof(impl->session_id)) != TURBO_OK ||
        snprintf(impl->scxml_location, sizeof(impl->scxml_location),
                 "#_scxml_%s", impl->session_id) < 0) {
        free(impl);
        return CFLOW_STATECHART_INSTANCE_INVALID_CONFIGURATION;
    }
    impl->system_values.session_id =
        (scxml_expr_string_view){
            impl->session_id, TURBO_UUID_STRING_LENGTH};
    impl->system_values.scxml_location =
        (scxml_expr_string_view){
            impl->scxml_location, strlen(impl->scxml_location)};
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
    impl->payload_scratch_capacity = program->max_payload_entries;
    impl->payload_scratch =
        (scxml_payload_entry *)scxml_emit_allocate_rows(
            impl->payload_scratch_capacity,
            sizeof(*impl->payload_scratch));
    impl->payload_scratch_v3 =
        (scxml_payload_entry_v3 *)scxml_emit_allocate_rows(
            impl->payload_scratch_capacity,
            sizeof(*impl->payload_scratch_v3));
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
        (impl->payload_scratch_capacity != 0u &&
         (impl->payload_scratch == NULL ||
          impl->payload_scratch_v3 == NULL)) ||
        (impl->invocation_capacity != 0u &&
         impl->invocation_rows == NULL) ||
        (impl->invocation_effect_capacity != 0u &&
         impl->invocation_effects == NULL)) {
        session_free_storage(impl);
        free(impl);
        return CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED;
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
        impl->event_io_abi = SCXML_EVENT_IO_ADAPTER_ABI_V1;
    } else if (event_io_v2 != NULL) {
        impl->event_io_v2 = *event_io_v2;
        impl->adapter_user = adapters->event_io_user;
        impl->has_event_io = true;
        impl->event_io_abi = SCXML_EVENT_IO_ADAPTER_ABI_V2;
    } else if (event_io_v3 != NULL) {
        impl->event_io_v3 = *event_io_v3;
        impl->adapter_user = adapters_v3->event_io_user;
        impl->has_event_io = true;
        impl->event_io_abi = SCXML_EVENT_IO_ADAPTER_ABI_V3;
    }
    if (config->invoke != NULL) {
        impl->invoke = *config->invoke;
        impl->invoke_user = config->invoke_user;
        impl->has_invoke = true;
        impl->invoke_abi = SCXML_INVOKE_ADAPTER_ABI_V1;
    } else if (invoke_v2 != NULL) {
        impl->invoke_v2 = *invoke_v2;
        impl->invoke_user = adapters->invoke_user;
        impl->has_invoke = true;
        impl->invoke_abi = SCXML_INVOKE_ADAPTER_ABI_V2;
    } else if (invoke_v3 != NULL) {
        impl->invoke_v3 = *invoke_v3;
        impl->invoke_user = adapters_v3->invoke_user;
        impl->has_invoke = true;
        impl->invoke_abi = SCXML_INVOKE_ADAPTER_ABI_V3;
    }
    impl->next_send_token = UINT64_C(1);
    impl->next_invocation_token = UINT64_C(1);
    impl->next_external_metadata_token =
        SCXML_EXTERNAL_METADATA_TOKEN_BIT | UINT64_C(1);
    atomic_init(&impl->adapter_close_called, false);
    atomic_init(&impl->invoke_close_called, false);
    instance_hooks = (cflow_statechart_instance_hooks){
        .abi_version =
            (program->requirements &
             SCXML_REQUIREMENT_INVOKE_IDLOCATION) != 0u
                ? CFLOW_STATECHART_INSTANCE_HOOKS_ABI_V3
                : CFLOW_STATECHART_INSTANCE_HOOKS_ABI_V2,
        .struct_size = sizeof(instance_hooks),
        .on_stable = impl->has_invoke &&
                (program->requirements &
                 SCXML_REQUIREMENT_INVOKE_IDLOCATION) == 0u
            ? scxml_runtime_start_stable_invocations : NULL,
        .preprocess_external =
            impl->has_invoke ? scxml_runtime_preprocess_invocation_external : NULL,
        .on_event = scxml_runtime_observe_event,
        .on_stable_transaction = impl->has_invoke &&
                (program->requirements &
                 SCXML_REQUIREMENT_INVOKE_IDLOCATION) != 0u
            ? scxml_runtime_start_stable_invocations_transaction : NULL};
    if (data_model == SCXML_DATA_MODEL_CMETA &&
        program->data_initializer_count != 0u) {
        status = initialize_cmeta_state(
            program, cmeta_initial_state, &impl->system_values,
            &initialized_cmeta_state, &initialized_cmeta_state_managed);
        if (status != CFLOW_STATECHART_INSTANCE_OK) {
            session_close_adapter(impl);
            turbo_mutex_destroy(&impl->registry_lock);
            session_free_storage(impl);
            free(impl);
            return status;
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
        session, config, NULL, NULL, SCXML_DATA_MODEL_NULL, NULL);
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
        session, config, NULL, NULL, SCXML_DATA_MODEL_CMETA,
        options->initial_state);
}

cflow_statechart_instance_status scxml_session_init_v2(
    scxml_session *session,
    const scxml_session_config *config,
    const scxml_session_adapters_v2 *adapters) {
    return scxml_session_init_model(
        session, config, adapters, NULL, SCXML_DATA_MODEL_NULL, NULL);
}

cflow_statechart_instance_status scxml_session_init_cmeta_v2(
    scxml_session *session,
    const scxml_session_config *config,
    const scxml_cmeta_session_options_v1 *options,
    const scxml_session_adapters_v2 *adapters) {
    if (options == NULL ||
        options->abi_version !=
            SCXML_CMETA_SESSION_OPTIONS_ABI_V1 ||
        options->struct_size < sizeof(*options) ||
        options->initial_state == NULL)
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    return scxml_session_init_model(
        session, config, adapters, NULL, SCXML_DATA_MODEL_CMETA,
        options->initial_state);
}

cflow_statechart_instance_status scxml_session_init_v3(
    scxml_session *session,
    const scxml_session_config *config,
    const scxml_session_adapters_v3 *adapters) {
    return scxml_session_init_model(
        session, config, NULL, adapters, SCXML_DATA_MODEL_NULL, NULL);
}

cflow_statechart_instance_status scxml_session_init_cmeta_v3(
    scxml_session *session,
    const scxml_session_config *config,
    const scxml_cmeta_session_options_v1 *options,
    const scxml_session_adapters_v3 *adapters) {
    if (options == NULL ||
        options->abi_version != SCXML_CMETA_SESSION_OPTIONS_ABI_V1 ||
        options->struct_size < sizeof(*options) ||
        options->initial_state == NULL)
        return CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
    return scxml_session_init_model(
        session, config, NULL, adapters, SCXML_DATA_MODEL_CMETA,
        options->initial_state);
}

cflow_mailbox_status scxml_session_try_send(
    scxml_session *session, const cflow_event_view *event) {
    scxml_session_impl *impl = session != NULL
        ? (scxml_session_impl *)session->impl : NULL;
    return impl != NULL
        ? cflow_statechart_instance_try_send(&impl->instance, event)
        : CFLOW_MAILBOX_INVALID_ARGUMENT;
}

cflow_mailbox_status scxml_session_try_send_v2(
    scxml_session *session, const cflow_event_view *event,
    const scxml_event_metadata *metadata) {
    scxml_session_impl *impl = session != NULL
        ? (scxml_session_impl *)session->impl : NULL;
    scxml_external_event_metadata_row *row = NULL;
    uint64_t token = 0u;
    cflow_mailbox_status status;
    if (impl == NULL || event == NULL || metadata == NULL ||
        !scxml_runtime_metadata_field_valid(
            metadata->send_id, metadata->send_id_size) ||
        !scxml_runtime_metadata_field_valid(
            metadata->origin, metadata->origin_size) ||
        !scxml_runtime_metadata_field_valid(
            metadata->origin_type, metadata->origin_type_size) ||
        !scxml_runtime_metadata_field_valid(
            metadata->invoke_id, metadata->invoke_id_size) ||
        !scxml_runtime_metadata_field_valid(metadata->data, metadata->data_size))
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    row = scxml_runtime_reserve_event_metadata(impl, metadata, &token);
    if (row == NULL) {
        return CFLOW_MAILBOX_FULL;
    }
    status = cflow_statechart_instance_try_send_tagged(
        &impl->instance, event, token);
    if (status != CFLOW_MAILBOX_OK) {
        scxml_runtime_release_event_metadata(row);
    }
    return status;
}

cflow_mailbox_status scxml_session_try_send_v3(
    scxml_session *session, const cflow_event_view *event,
    const scxml_event_metadata_v3 *metadata) {
    scxml_session_impl *impl = session != NULL
        ? (scxml_session_impl *)session->impl : NULL;
    scxml_external_event_metadata_row *row = NULL;
    uint64_t token = 0u;
    cflow_mailbox_status status;
    if (impl == NULL || event == NULL || metadata == NULL ||
        metadata->abi_version != SCXML_EVENT_METADATA_ABI_V3 ||
        metadata->struct_size < sizeof(*metadata) ||
        !scxml_runtime_metadata_field_valid(
            metadata->base.send_id, metadata->base.send_id_size) ||
        !scxml_runtime_metadata_field_valid(
            metadata->base.origin, metadata->base.origin_size) ||
        !scxml_runtime_metadata_field_valid(
            metadata->base.origin_type, metadata->base.origin_type_size) ||
        !scxml_runtime_metadata_field_valid(
            metadata->base.invoke_id, metadata->base.invoke_id_size) ||
        !scxml_runtime_metadata_field_valid(
            metadata->base.data, metadata->base.data_size) ||
        (metadata->data.kind != SCXML_CONTENT_INVALID &&
         metadata->base.data_size != 0u))
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    row = scxml_runtime_reserve_event_metadata(impl, &metadata->base, &token);
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
    cflow_statechart_instance_cancel(&impl->instance);
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

scxml_location_status scxml_session_copy_location(
    const scxml_session *session, char *out_location,
    size_t location_capacity, size_t *out_required_capacity) {
    const scxml_session_impl *impl = session != NULL
        ? (const scxml_session_impl *)session->impl : NULL;
    size_t required;
    if (impl == NULL || out_required_capacity == NULL)
        return SCXML_LOCATION_INVALID_ARGUMENT;
    required = impl->system_values.scxml_location.size + 1u;
    if (out_location == NULL || location_capacity < required) {
        *out_required_capacity = required;
        return SCXML_LOCATION_TOO_SMALL;
    }
    memcpy(out_location, impl->scxml_location, required);
    *out_required_capacity = required;
    return SCXML_LOCATION_OK;
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
        !(impl->event_io_abi == SCXML_EVENT_IO_ADAPTER_ABI_V3
              ? impl->event_io_v3.is_quiescent
          : impl->event_io_abi == SCXML_EVENT_IO_ADAPTER_ABI_V2
              ? impl->event_io_v2.is_quiescent
              : impl->event_io.is_quiescent)(
            impl->adapter_user))
        return CFLOW_STATECHART_INSTANCE_WOULD_BLOCK;
    if (impl->has_invoke &&
        !(impl->invoke_abi == SCXML_INVOKE_ADAPTER_ABI_V3
              ? impl->invoke_v3.is_quiescent
          : impl->invoke_abi == SCXML_INVOKE_ADAPTER_ABI_V2
              ? impl->invoke_v2.is_quiescent
              : impl->invoke.is_quiescent)(
            impl->invoke_user))
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
