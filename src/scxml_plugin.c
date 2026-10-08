#include <scxml/plugin.h>

#include <cmeta/cmeta.h>

#include <stdlib.h>
#include <string.h>

#define SCXML_PLUGIN_ADAPTER_ARG_CAPACITY \
    (sizeof(((cmeta_sig_desc *)0)->params) / \
     sizeof(((cmeta_sig_desc *)0)->params[0]))

typedef struct scxml_plugin_lease_row {
    cmeta_plugin_ref ref;
    cmeta_plugin_lease lease;
    const cmeta_plugin_manifest *manifest;
} scxml_plugin_lease_row;

typedef struct scxml_plugin_callable_capture {
    const cmeta_plugin_function_export *function;
} scxml_plugin_callable_capture;

typedef struct scxml_plugin_program_impl {
    scxml_program core;
    cmeta_plugin_registry *registry;
    scxml_plugin_lease_row *leases;
    size_t lease_count;
} scxml_plugin_program_impl;

typedef struct scxml_plugin_event_io_provider_impl {
    cmeta_plugin_registry *registry;
    cmeta_plugin_lease lease;
    scxml_event_io_adapter_bridge bridge;
} scxml_plugin_event_io_provider_impl;

typedef struct scxml_plugin_invoke_provider_impl {
    cmeta_plugin_registry *registry;
    cmeta_plugin_lease lease;
    scxml_invoke_adapter_bridge bridge;
} scxml_plugin_invoke_provider_impl;

static bool plugin_ref_equal(
    cmeta_plugin_ref left, cmeta_plugin_ref right) {
    return left.slot == right.slot &&
           left.generation == right.generation;
}

static bool plugin_action_valid(
    const scxml_plugin_action_v1 *action) {
    return action != NULL &&
           action->struct_size >= sizeof(*action) &&
           cmeta_plugin_ref_valid(action->plugin) &&
           action->export_id != NULL &&
           action->export_id[0] != '\0' &&
           action->contract_id != NULL &&
           action->contract_id[0] != '\0' &&
           action->namespace_uri != NULL &&
           action->namespace_uri_size != 0u &&
           action->local_name != NULL &&
           action->local_name_size != 0u;
}

static bool plugin_signature_matches_function(
    const cmeta_sig_desc *signature,
    const cmeta_function_desc *function) {
    size_t index;
    if (signature == NULL || function == NULL ||
        signature->protocol != CMETA_FN_PROTOCOL_VALUE ||
        signature->param_count != function->param_count ||
        !cmeta_type_equal(signature->return_type, function->return_type))
        return false;
    for (index = 0u; index < function->param_count; ++index) {
        const cmeta_param_desc *param =
            cmeta_function_param(function, index);
        if (param == NULL ||
            !cmeta_type_equal(signature->params[index], param->type))
            return false;
    }
    return true;
}

static bool plugin_function_meta(
    const cmeta_function_desc *function,
    cmeta_fn *out) {
    int value;
    if (out == NULL || !cmeta_function_desc_valid(function))
        return false;
    memset(out, 0, sizeof(*out));
    for (value = (int)CMETA_SIG_INVALID + 1;
         value < (int)CMETA_SIG_COUNT; ++value) {
        cmeta_fn candidate = {0};
        const cmeta_sig_desc *signature;
        candidate.sig = (cmeta_sig)value;
        signature = cmeta_fn_signature(candidate);
        if (!plugin_signature_matches_function(signature, function))
            continue;
        candidate.effects = function->effects;
        candidate.properties = function->properties;
        *out = candidate;
        return true;
    }
    return false;
}

static bool plugin_callable_invoke(
    const cmeta_callable *self,
    void *out,
    const void *const *args) {
    scxml_plugin_callable_capture capture = {0};
    const cmeta_sig_desc *signature;
    void *mutable_args[SCXML_PLUGIN_ADAPTER_ARG_CAPACITY] = {NULL};
    size_t index;
    if (self == NULL ||
        self->capture_size != sizeof(capture))
        return false;
    memcpy(&capture, self->capture.bytes, sizeof(capture));
    signature = cmeta_fn_signature(self->meta);
    if (capture.function == NULL ||
        capture.function->invoke == NULL ||
        signature == NULL ||
        signature->protocol != CMETA_FN_PROTOCOL_VALUE ||
        signature->param_count > SCXML_PLUGIN_ADAPTER_ARG_CAPACITY ||
        (signature->param_count != 0u && args == NULL))
        return false;
    for (index = 0u; index < signature->param_count; ++index) {
        if (args[index] == NULL) return false;
        mutable_args[index] = (void *)args[index];
    }
    return capture.function->invoke(
        capture.function->context, out, mutable_args,
        signature->param_count);
}

static bool plugin_callable(
    const cmeta_plugin_function_export *function,
    cmeta_callable *out) {
    scxml_plugin_callable_capture capture;
    cmeta_callable value = {0};
    if (function == NULL || function->desc == NULL ||
        function->abi == NULL || function->invoke == NULL ||
        out == NULL ||
        !plugin_function_meta(function->desc, &value.meta))
        return false;
    capture.function = function;
    value.invoke = plugin_callable_invoke;
    value.dispatch = CMETA_CALLABLE_DISPATCH_ADAPTER;
    value.capture_size = sizeof(capture);
    if (value.capture_size > sizeof(value.capture.bytes))
        return false;
    memcpy(value.capture.bytes, &capture, sizeof(capture));
    *out = value;
    return true;
}

static scxml_plugin_lease_row *find_lease(
    scxml_plugin_lease_row *rows,
    size_t count,
    cmeta_plugin_ref ref) {
    size_t index;
    for (index = 0u; index < count; ++index)
        if (plugin_ref_equal(rows[index].ref, ref))
            return &rows[index];
    return NULL;
}

static cmeta_plugin_status acquire_manifest(
    cmeta_plugin_registry *registry,
    scxml_plugin_lease_row *rows,
    size_t *count,
    cmeta_plugin_ref ref,
    const cmeta_plugin_manifest **out_manifest) {
    scxml_plugin_lease_row *existing;
    cmeta_plugin_lease lease = {0};
    const cmeta_plugin_manifest *manifest = NULL;
    cmeta_plugin_status status;
    if (registry == NULL || rows == NULL || count == NULL ||
        out_manifest == NULL)
        return CMETA_PLUGIN_INVALID_ARGUMENT;
    existing = find_lease(rows, *count, ref);
    if (existing != NULL) {
        *out_manifest = existing->manifest;
        return CMETA_PLUGIN_OK;
    }
    status = cmeta_plugin_registry_acquire(
        registry, ref, &lease, &manifest);
    if (status != CMETA_PLUGIN_OK)
        return status;
    rows[*count] = (scxml_plugin_lease_row){
        .ref = ref,
        .lease = lease,
        .manifest = manifest};
    ++*count;
    *out_manifest = manifest;
    return CMETA_PLUGIN_OK;
}

static cmeta_plugin_status release_leases(
    cmeta_plugin_registry *registry,
    scxml_plugin_lease_row *rows,
    size_t count) {
    cmeta_plugin_status first = CMETA_PLUGIN_OK;
    while (count != 0u) {
        cmeta_plugin_status status;
        --count;
        if (!cmeta_plugin_lease_valid(rows[count].lease))
            continue;
        status = cmeta_plugin_registry_release(
            registry, &rows[count].lease);
        if (first == CMETA_PLUGIN_OK &&
            status != CMETA_PLUGIN_OK)
            first = status;
    }
    return first;
}

static bool plugin_provider_binding_valid(
    const scxml_plugin_provider_v1 *binding) {
    return binding != NULL &&
           binding->struct_size >= sizeof(*binding) &&
           cmeta_plugin_ref_valid(binding->plugin) &&
           binding->export_id != NULL &&
           binding->export_id[0] != '\0' &&
           binding->contract_id != NULL &&
           binding->contract_id[0] != '\0';
}

static cmeta_plugin_status acquire_interface_export(
    cmeta_plugin_registry *registry,
    const scxml_plugin_provider_v1 *binding,
    const cmeta_interface_desc *expected,
    cmeta_plugin_lease *out_lease,
    const cmeta_plugin_export **out_entry) {
    cmeta_plugin_lease lease = {0};
    const cmeta_plugin_manifest *manifest = NULL;
    const cmeta_plugin_export *entry = NULL;
    cmeta_plugin_status status;
    if (registry == NULL || !plugin_provider_binding_valid(binding) ||
        !cmeta_interface_desc_valid(expected) ||
        out_lease == NULL || out_entry == NULL)
        return CMETA_PLUGIN_INVALID_ARGUMENT;
    *out_lease = (cmeta_plugin_lease){0};
    *out_entry = NULL;
    status = cmeta_plugin_registry_acquire(
        registry, binding->plugin, &lease, &manifest);
    if (status != CMETA_PLUGIN_OK)
        return status;
    status = cmeta_plugin_manifest_find_export(
        manifest, binding->export_id, &entry);
    if (status == CMETA_PLUGIN_OK)
        status = cmeta_plugin_export_require_interface(
            entry, binding->contract_id,
            binding->contract_version,
            binding->required_capabilities,
            expected);
    if (status != CMETA_PLUGIN_OK) {
        const cmeta_plugin_status release_status =
            cmeta_plugin_registry_release(registry, &lease);
        return release_status == CMETA_PLUGIN_OK
            ? status : release_status;
    }
    *out_lease = lease;
    *out_entry = entry;
    return CMETA_PLUGIN_OK;
}

const char *scxml_plugin_status_string(
    scxml_plugin_status status) {
    switch (status) {
    case SCXML_PLUGIN_OK:
        return "ok";
    case SCXML_PLUGIN_INVALID_ARGUMENT:
        return "invalid argument";
    case SCXML_PLUGIN_ALLOCATION_FAILED:
        return "allocation failed";
    case SCXML_PLUGIN_PLUGIN_ERROR:
        return "plugin error";
    case SCXML_PLUGIN_INCOMPATIBLE_EXPORT:
        return "incompatible plugin export";
    case SCXML_PLUGIN_COMPILE_FAILED:
        return "SCXML compile failed";
    default:
        return "unknown";
    }
}

scxml_plugin_status scxml_plugin_event_io_provider_open(
    scxml_plugin_event_io_provider *out,
    cmeta_plugin_registry *registry,
    const scxml_plugin_provider_v1 *binding,
    cmeta_plugin_status *out_plugin_status) {
    scxml_plugin_event_io_provider_impl *impl;
    cmeta_plugin_lease lease = {0};
    const cmeta_plugin_export *entry = NULL;
    const scxml_event_io_provider *provider;
    cmeta_plugin_status plugin_status;

    if (out_plugin_status != NULL)
        *out_plugin_status = CMETA_PLUGIN_OK;
    if (out == NULL || out->impl != NULL ||
        registry == NULL || !plugin_provider_binding_valid(binding))
        return SCXML_PLUGIN_INVALID_ARGUMENT;

    plugin_status = acquire_interface_export(
        registry, binding, scxml_event_io_provider_interface(),
        &lease, &entry);
    if (plugin_status != CMETA_PLUGIN_OK) {
        if (out_plugin_status != NULL)
            *out_plugin_status = plugin_status;
        return SCXML_PLUGIN_INCOMPATIBLE_EXPORT;
    }

    provider = entry != NULL
        ? (const scxml_event_io_provider *)entry->value.interface.value
        : NULL;
    if (!scxml_event_io_provider_valid(provider) ||
        !scxml_event_io_provider_has(
            provider, binding->required_capabilities)) {
        plugin_status = cmeta_plugin_registry_release(registry, &lease);
        if (out_plugin_status != NULL)
            *out_plugin_status = plugin_status == CMETA_PLUGIN_OK
                ? CMETA_PLUGIN_INCOMPATIBLE_CONTRACT : plugin_status;
        return SCXML_PLUGIN_INCOMPATIBLE_EXPORT;
    }

    impl = (scxml_plugin_event_io_provider_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) {
        plugin_status = cmeta_plugin_registry_release(registry, &lease);
        if (out_plugin_status != NULL)
            *out_plugin_status = plugin_status;
        return SCXML_PLUGIN_ALLOCATION_FAILED;
    }
    if (!scxml_event_io_adapter_bridge_init(&impl->bridge, provider)) {
        plugin_status = cmeta_plugin_registry_release(registry, &lease);
        if (out_plugin_status != NULL)
            *out_plugin_status = plugin_status == CMETA_PLUGIN_OK
                ? CMETA_PLUGIN_INCOMPATIBLE_CONTRACT : plugin_status;
        free(impl);
        return SCXML_PLUGIN_INCOMPATIBLE_EXPORT;
    }
    impl->registry = registry;
    impl->lease = lease;
    out->impl = impl;
    return SCXML_PLUGIN_OK;
}

const scxml_event_io_adapter *scxml_plugin_event_io_provider_adapter(
    const scxml_plugin_event_io_provider *provider) {
    const scxml_plugin_event_io_provider_impl *impl =
        provider != NULL
            ? (const scxml_plugin_event_io_provider_impl *)provider->impl
            : NULL;
    return impl != NULL
        ? scxml_event_io_adapter_bridge_get(&impl->bridge)
        : NULL;
}

void *scxml_plugin_event_io_provider_user(
    scxml_plugin_event_io_provider *provider) {
    scxml_plugin_event_io_provider_impl *impl =
        provider != NULL
            ? (scxml_plugin_event_io_provider_impl *)provider->impl
            : NULL;
    return impl != NULL
        ? scxml_event_io_adapter_bridge_user(&impl->bridge)
        : NULL;
}

scxml_plugin_status scxml_plugin_event_io_provider_destroy(
    scxml_plugin_event_io_provider *provider,
    cmeta_plugin_status *out_plugin_status) {
    scxml_plugin_event_io_provider_impl *impl;
    cmeta_plugin_status status;
    if (out_plugin_status != NULL)
        *out_plugin_status = CMETA_PLUGIN_OK;
    if (provider == NULL || provider->impl == NULL)
        return SCXML_PLUGIN_INVALID_ARGUMENT;
    impl = (scxml_plugin_event_io_provider_impl *)provider->impl;
    status = cmeta_plugin_registry_release(
        impl->registry, &impl->lease);
    if (out_plugin_status != NULL)
        *out_plugin_status = status;
    if (status != CMETA_PLUGIN_OK)
        return SCXML_PLUGIN_PLUGIN_ERROR;
    free(impl);
    provider->impl = NULL;
    return SCXML_PLUGIN_OK;
}

scxml_plugin_status scxml_plugin_invoke_provider_open(
    scxml_plugin_invoke_provider *out,
    cmeta_plugin_registry *registry,
    const scxml_plugin_provider_v1 *binding,
    cmeta_plugin_status *out_plugin_status) {
    scxml_plugin_invoke_provider_impl *impl;
    cmeta_plugin_lease lease = {0};
    const cmeta_plugin_export *entry = NULL;
    const scxml_invoke_provider *provider;
    cmeta_plugin_status plugin_status;

    if (out_plugin_status != NULL)
        *out_plugin_status = CMETA_PLUGIN_OK;
    if (out == NULL || out->impl != NULL ||
        registry == NULL || !plugin_provider_binding_valid(binding))
        return SCXML_PLUGIN_INVALID_ARGUMENT;

    plugin_status = acquire_interface_export(
        registry, binding, scxml_invoke_provider_interface(),
        &lease, &entry);
    if (plugin_status != CMETA_PLUGIN_OK) {
        if (out_plugin_status != NULL)
            *out_plugin_status = plugin_status;
        return SCXML_PLUGIN_INCOMPATIBLE_EXPORT;
    }

    provider = entry != NULL
        ? (const scxml_invoke_provider *)entry->value.interface.value
        : NULL;
    if (!scxml_invoke_provider_valid(provider) ||
        !scxml_invoke_provider_has(
            provider, binding->required_capabilities)) {
        plugin_status = cmeta_plugin_registry_release(registry, &lease);
        if (out_plugin_status != NULL)
            *out_plugin_status = plugin_status == CMETA_PLUGIN_OK
                ? CMETA_PLUGIN_INCOMPATIBLE_CONTRACT : plugin_status;
        return SCXML_PLUGIN_INCOMPATIBLE_EXPORT;
    }

    impl = (scxml_plugin_invoke_provider_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) {
        plugin_status = cmeta_plugin_registry_release(registry, &lease);
        if (out_plugin_status != NULL)
            *out_plugin_status = plugin_status;
        return SCXML_PLUGIN_ALLOCATION_FAILED;
    }
    if (!scxml_invoke_adapter_bridge_init(&impl->bridge, provider)) {
        plugin_status = cmeta_plugin_registry_release(registry, &lease);
        if (out_plugin_status != NULL)
            *out_plugin_status = plugin_status == CMETA_PLUGIN_OK
                ? CMETA_PLUGIN_INCOMPATIBLE_CONTRACT : plugin_status;
        free(impl);
        return SCXML_PLUGIN_INCOMPATIBLE_EXPORT;
    }
    impl->registry = registry;
    impl->lease = lease;
    out->impl = impl;
    return SCXML_PLUGIN_OK;
}

const scxml_invoke_adapter *scxml_plugin_invoke_provider_adapter(
    const scxml_plugin_invoke_provider *provider) {
    const scxml_plugin_invoke_provider_impl *impl =
        provider != NULL
            ? (const scxml_plugin_invoke_provider_impl *)provider->impl
            : NULL;
    return impl != NULL
        ? scxml_invoke_adapter_bridge_get(&impl->bridge)
        : NULL;
}

void *scxml_plugin_invoke_provider_user(
    scxml_plugin_invoke_provider *provider) {
    scxml_plugin_invoke_provider_impl *impl =
        provider != NULL
            ? (scxml_plugin_invoke_provider_impl *)provider->impl
            : NULL;
    return impl != NULL
        ? scxml_invoke_adapter_bridge_user(&impl->bridge)
        : NULL;
}

scxml_plugin_status scxml_plugin_invoke_provider_destroy(
    scxml_plugin_invoke_provider *provider,
    cmeta_plugin_status *out_plugin_status) {
    scxml_plugin_invoke_provider_impl *impl;
    cmeta_plugin_status status;
    if (out_plugin_status != NULL)
        *out_plugin_status = CMETA_PLUGIN_OK;
    if (provider == NULL || provider->impl == NULL)
        return SCXML_PLUGIN_INVALID_ARGUMENT;
    impl = (scxml_plugin_invoke_provider_impl *)provider->impl;
    status = cmeta_plugin_registry_release(
        impl->registry, &impl->lease);
    if (out_plugin_status != NULL)
        *out_plugin_status = status;
    if (status != CMETA_PLUGIN_OK)
        return SCXML_PLUGIN_PLUGIN_ERROR;
    free(impl);
    provider->impl = NULL;
    return SCXML_PLUGIN_OK;
}

scxml_plugin_status scxml_plugin_compile_cmeta_v1(
    scxml_plugin_program *out,
    const char *input,
    size_t input_size,
    const scxml_limits *limits,
    const scxml_plugin_compile_options_v1 *options,
    scxml_diagnostic *diagnostic,
    cmeta_plugin_status *out_plugin_status) {
    scxml_plugin_program_impl *impl = NULL;
    scxml_plugin_lease_row *leases = NULL;
    scxml_cmeta_custom_action_v2 *actions = NULL;
    scxml_cmeta_compile_options_v4 cmeta;
    size_t static_count;
    size_t total_count;
    size_t lease_count = 0u;
    size_t index;
    scxml_status compile_status;
    cmeta_plugin_status plugin_status = CMETA_PLUGIN_OK;
    scxml_plugin_status status = SCXML_PLUGIN_INVALID_ARGUMENT;

    if (out_plugin_status != NULL)
        *out_plugin_status = CMETA_PLUGIN_OK;
    if (out == NULL || out->impl != NULL ||
        input == NULL || input_size == 0u ||
        options == NULL ||
        options->abi_version != SCXML_PLUGIN_COMPILE_OPTIONS_ABI_V1 ||
        options->struct_size < sizeof(*options) ||
        options->registry == NULL ||
        options->cmeta == NULL ||
        options->cmeta->abi_version != SCXML_CMETA_COMPILE_OPTIONS_ABI_V4 ||
        options->cmeta->struct_size < sizeof(*options->cmeta) ||
        (options->plugin_action_count != 0u &&
         options->plugin_actions == NULL))
        return SCXML_PLUGIN_INVALID_ARGUMENT;

    static_count = options->cmeta->action_count;
    if (static_count != 0u && options->cmeta->actions == NULL)
        return SCXML_PLUGIN_INVALID_ARGUMENT;
    if (options->plugin_action_count > SIZE_MAX - static_count)
        return SCXML_PLUGIN_INVALID_ARGUMENT;
    total_count = static_count + options->plugin_action_count;

    impl = (scxml_plugin_program_impl *)calloc(1u, sizeof(*impl));
    leases = options->plugin_action_count != 0u
        ? (scxml_plugin_lease_row *)calloc(
              options->plugin_action_count, sizeof(*leases))
        : NULL;
    actions = total_count != 0u
        ? (scxml_cmeta_custom_action_v2 *)calloc(
              total_count, sizeof(*actions))
        : NULL;
    if (impl == NULL ||
        (options->plugin_action_count != 0u && leases == NULL) ||
        (total_count != 0u && actions == NULL)) {
        status = SCXML_PLUGIN_ALLOCATION_FAILED;
        goto fail;
    }

    if (static_count != 0u)
        memcpy(actions, options->cmeta->actions,
               static_count * sizeof(*actions));

    for (index = 0u; index < options->plugin_action_count; ++index) {
        const scxml_plugin_action_v1 *mapping =
            &options->plugin_actions[index];
        const cmeta_plugin_manifest *manifest = NULL;
        const cmeta_plugin_export *entry = NULL;
        cmeta_callable callable = {0};

        if (!plugin_action_valid(mapping)) {
            status = SCXML_PLUGIN_INVALID_ARGUMENT;
            goto fail;
        }
        plugin_status = acquire_manifest(
            options->registry, leases, &lease_count,
            mapping->plugin, &manifest);
        if (plugin_status != CMETA_PLUGIN_OK) {
            status = SCXML_PLUGIN_PLUGIN_ERROR;
            goto fail;
        }
        plugin_status = cmeta_plugin_manifest_find_export(
            manifest, mapping->export_id, &entry);
        if (plugin_status != CMETA_PLUGIN_OK) {
            status = SCXML_PLUGIN_INCOMPATIBLE_EXPORT;
            goto fail;
        }
        plugin_status = cmeta_plugin_export_require_function(
            entry, mapping->contract_id,
            mapping->contract_version,
            mapping->required_capabilities);
        if (plugin_status != CMETA_PLUGIN_OK ||
            entry == NULL ||
            !plugin_callable(&entry->value.function, &callable)) {
            if (plugin_status == CMETA_PLUGIN_OK)
                plugin_status = CMETA_PLUGIN_INCOMPATIBLE_CONTRACT;
            status = SCXML_PLUGIN_INCOMPATIBLE_EXPORT;
            goto fail;
        }

        actions[static_count + index] =
            (scxml_cmeta_custom_action_v2){
                .struct_size = sizeof(scxml_cmeta_custom_action_v2),
                .namespace_uri = mapping->namespace_uri,
                .namespace_uri_size = mapping->namespace_uri_size,
                .local_name = mapping->local_name,
                .local_name_size = mapping->local_name_size,
                .function = entry->value.function.desc,
                .abi = entry->value.function.abi,
                .callable = callable};
    }

    cmeta = *options->cmeta;
    cmeta.actions = actions;
    cmeta.action_count = total_count;
    compile_status = scxml_compile_cmeta_v4(
        &impl->core, input, input_size, limits,
        &cmeta, diagnostic);
    if (compile_status != SCXML_OK) {
        status = SCXML_PLUGIN_COMPILE_FAILED;
        goto fail;
    }

    impl->registry = options->registry;
    impl->leases = leases;
    impl->lease_count = lease_count;
    free(actions);
    out->impl = impl;
    return SCXML_PLUGIN_OK;

fail:
    if (out_plugin_status != NULL)
        *out_plugin_status = plugin_status;
    if (impl != NULL)
        scxml_program_destroy(&impl->core);
    if (leases != NULL) {
        const cmeta_plugin_status release_status =
            release_leases(options != NULL ? options->registry : NULL,
                           leases, lease_count);
        if (release_status != CMETA_PLUGIN_OK &&
            out_plugin_status != NULL &&
            *out_plugin_status == CMETA_PLUGIN_OK)
            *out_plugin_status = release_status;
    }
    free(actions);
    free(leases);
    free(impl);
    return status;
}

const scxml_program *scxml_plugin_program_core(
    const scxml_plugin_program *program) {
    const scxml_plugin_program_impl *impl =
        program != NULL
            ? (const scxml_plugin_program_impl *)program->impl
            : NULL;
    return impl != NULL ? &impl->core : NULL;
}

scxml_plugin_status scxml_plugin_program_destroy(
    scxml_plugin_program *program,
    cmeta_plugin_status *out_plugin_status) {
    scxml_plugin_program_impl *impl;
    cmeta_plugin_status plugin_status;
    if (out_plugin_status != NULL)
        *out_plugin_status = CMETA_PLUGIN_OK;
    if (program == NULL || program->impl == NULL)
        return SCXML_PLUGIN_INVALID_ARGUMENT;
    impl = (scxml_plugin_program_impl *)program->impl;
    scxml_program_destroy(&impl->core);
    plugin_status = release_leases(
        impl->registry, impl->leases, impl->lease_count);
    if (out_plugin_status != NULL)
        *out_plugin_status = plugin_status;
    if (plugin_status != CMETA_PLUGIN_OK)
        return SCXML_PLUGIN_PLUGIN_ERROR;
    free(impl->leases);
    free(impl);
    program->impl = NULL;
    return SCXML_PLUGIN_OK;
}
