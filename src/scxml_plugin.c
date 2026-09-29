#include <scxml/plugin.h>

#include <cmeta/cmeta.h>

#include <stdlib.h>
#include <string.h>

#define SCXML_PLUGIN_ADAPTER_ARG_CAPACITY \
    (sizeof(((cmeta_sig_desc *)0)->params) / \
     sizeof(((cmeta_sig_desc *)0)->params[0]))

typedef struct scxml_plugin_lease_row {
    salts_plugin_ref ref;
    salts_plugin_lease lease;
    const salts_plugin_manifest *manifest;
} scxml_plugin_lease_row;

typedef struct scxml_plugin_callable_capture {
    const salts_plugin_function_export *function;
} scxml_plugin_callable_capture;

typedef struct scxml_plugin_program_impl {
    scxml_program core;
    salts_plugin_registry *registry;
    scxml_plugin_lease_row *leases;
    size_t lease_count;
} scxml_plugin_program_impl;

static bool plugin_ref_equal(
    salts_plugin_ref left, salts_plugin_ref right) {
    return left.slot == right.slot &&
           left.generation == right.generation;
}

static bool plugin_action_valid(
    const scxml_plugin_action_v1 *action) {
    return action != NULL &&
           action->struct_size >= sizeof(*action) &&
           salts_plugin_ref_valid(action->plugin) &&
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
    const salts_plugin_function_export *function,
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
    salts_plugin_ref ref) {
    size_t index;
    for (index = 0u; index < count; ++index)
        if (plugin_ref_equal(rows[index].ref, ref))
            return &rows[index];
    return NULL;
}

static salts_plugin_status acquire_manifest(
    salts_plugin_registry *registry,
    salts_plugin_lease_row *rows,
    size_t *count,
    salts_plugin_ref ref,
    const salts_plugin_manifest **out_manifest) {
    scxml_plugin_lease_row *existing;
    salts_plugin_lease lease = {0};
    const salts_plugin_manifest *manifest = NULL;
    salts_plugin_status status;
    if (registry == NULL || rows == NULL || count == NULL ||
        out_manifest == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    existing = find_lease(rows, *count, ref);
    if (existing != NULL) {
        *out_manifest = existing->manifest;
        return SALTS_PLUGIN_OK;
    }
    status = salts_plugin_registry_acquire(
        registry, ref, &lease, &manifest);
    if (status != SALTS_PLUGIN_OK)
        return status;
    rows[*count] = (scxml_plugin_lease_row){
        .ref = ref,
        .lease = lease,
        .manifest = manifest};
    ++*count;
    *out_manifest = manifest;
    return SALTS_PLUGIN_OK;
}

static salts_plugin_status release_leases(
    salts_plugin_registry *registry,
    scxml_plugin_lease_row *rows,
    size_t count) {
    salts_plugin_status first = SALTS_PLUGIN_OK;
    while (count != 0u) {
        salts_plugin_status status;
        --count;
        if (!salts_plugin_lease_valid(rows[count].lease))
            continue;
        status = salts_plugin_registry_release(
            registry, &rows[count].lease);
        if (first == SALTS_PLUGIN_OK &&
            status != SALTS_PLUGIN_OK)
            first = status;
    }
    return first;
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

scxml_plugin_status scxml_plugin_compile_cmeta_v1(
    scxml_plugin_program *out,
    const char *input,
    size_t input_size,
    const scxml_limits *limits,
    const scxml_plugin_compile_options_v1 *options,
    scxml_diagnostic *diagnostic,
    salts_plugin_status *out_plugin_status) {
    scxml_plugin_program_impl *impl = NULL;
    scxml_plugin_lease_row *leases = NULL;
    scxml_cmeta_custom_action_v2 *actions = NULL;
    scxml_cmeta_compile_options_v4 cmeta;
    size_t static_count;
    size_t total_count;
    size_t lease_count = 0u;
    size_t index;
    scxml_status compile_status;
    salts_plugin_status plugin_status = SALTS_PLUGIN_OK;
    scxml_plugin_status status = SCXML_PLUGIN_INVALID_ARGUMENT;

    if (out_plugin_status != NULL)
        *out_plugin_status = SALTS_PLUGIN_OK;
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
        const salts_plugin_manifest *manifest = NULL;
        const salts_plugin_export *entry = NULL;
        cmeta_callable callable = {0};

        if (!plugin_action_valid(mapping)) {
            status = SCXML_PLUGIN_INVALID_ARGUMENT;
            goto fail;
        }
        plugin_status = acquire_manifest(
            options->registry, leases, &lease_count,
            mapping->plugin, &manifest);
        if (plugin_status != SALTS_PLUGIN_OK) {
            status = SCXML_PLUGIN_PLUGIN_ERROR;
            goto fail;
        }
        plugin_status = salts_plugin_manifest_find_export(
            manifest, mapping->export_id, &entry);
        if (plugin_status != SALTS_PLUGIN_OK) {
            status = SCXML_PLUGIN_INCOMPATIBLE_EXPORT;
            goto fail;
        }
        plugin_status = salts_plugin_export_require_function(
            entry, mapping->contract_id,
            mapping->contract_version,
            mapping->required_capabilities);
        if (plugin_status != SALTS_PLUGIN_OK ||
            entry == NULL ||
            !plugin_callable(&entry->value.function, &callable)) {
            if (plugin_status == SALTS_PLUGIN_OK)
                plugin_status = SALTS_PLUGIN_INCOMPATIBLE_CONTRACT;
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
        const salts_plugin_status release_status =
            release_leases(options != NULL ? options->registry : NULL,
                           leases, lease_count);
        if (release_status != SALTS_PLUGIN_OK &&
            out_plugin_status != NULL &&
            *out_plugin_status == SALTS_PLUGIN_OK)
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
    salts_plugin_status *out_plugin_status) {
    scxml_plugin_program_impl *impl;
    salts_plugin_status plugin_status;
    if (out_plugin_status != NULL)
        *out_plugin_status = SALTS_PLUGIN_OK;
    if (program == NULL || program->impl == NULL)
        return SCXML_PLUGIN_INVALID_ARGUMENT;
    impl = (scxml_plugin_program_impl *)program->impl;
    scxml_program_destroy(&impl->core);
    plugin_status = release_leases(
        impl->registry, impl->leases, impl->lease_count);
    if (out_plugin_status != NULL)
        *out_plugin_status = plugin_status;
    if (plugin_status != SALTS_PLUGIN_OK)
        return SCXML_PLUGIN_PLUGIN_ERROR;
    free(impl->leases);
    free(impl);
    program->impl = NULL;
    return SCXML_PLUGIN_OK;
}
