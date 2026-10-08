#include <scxml/component.h>

#include <string.h>

static scxml_component_status component_status(
    salts_component_plugin_status status) {
    switch (status) {
    case SALTS_COMPONENT_PLUGIN_OK:
        return SCXML_COMPONENT_OK;
    case SALTS_COMPONENT_PLUGIN_INVALID_ARGUMENT:
        return SCXML_COMPONENT_INVALID_ARGUMENT;
    case SALTS_COMPONENT_PLUGIN_BUSY:
        return SCXML_COMPONENT_BUSY;
    case SALTS_COMPONENT_PLUGIN_CAPACITY_EXCEEDED:
    case SALTS_COMPONENT_PLUGIN_PLUGIN_ERROR:
    case SALTS_COMPONENT_PLUGIN_PROVIDER_ERROR:
    case SALTS_COMPONENT_PLUGIN_COMPONENT_ERROR:
    case SALTS_COMPONENT_PLUGIN_INVALID_STATE:
        return SCXML_COMPONENT_ERROR;
    }
    return SCXML_COMPONENT_ERROR;
}

static bool scope_live(const scxml_component_scope *scope) {
    return scope != NULL &&
           scope->live &&
           scope->generation_id != UINT64_C(0) &&
           salts_component_plugin_scope_generation_id(
               &scope->component_scope) == scope->generation_id;
}

const char *scxml_component_status_string(
    scxml_component_status status) {
    switch (status) {
    case SCXML_COMPONENT_OK:
        return "ok";
    case SCXML_COMPONENT_INVALID_ARGUMENT:
        return "invalid argument";
    case SCXML_COMPONENT_UNAVAILABLE:
        return "component provider unavailable";
    case SCXML_COMPONENT_CAPABILITY_MISMATCH:
        return "provider capability mismatch";
    case SCXML_COMPONENT_BUSY:
        return "busy";
    case SCXML_COMPONENT_ERROR:
        return "component runtime error";
    }
    return "unknown component status";
}

scxml_component_status scxml_component_scope_acquire(
    scxml_component_scope *scope,
    salts_component_plugin_runtime *runtime) {
    salts_component_plugin_status status;
    uint64_t generation_id;

    if (scope == NULL || runtime == NULL || scope->live)
        return SCXML_COMPONENT_INVALID_ARGUMENT;

    memset(scope, 0, sizeof(*scope));
    status = salts_component_plugin_scope_acquire(
        runtime, &scope->component_scope);
    if (status != SALTS_COMPONENT_PLUGIN_OK)
        return component_status(status);

    generation_id = salts_component_plugin_scope_generation_id(
        &scope->component_scope);
    if (generation_id == UINT64_C(0)) {
        (void)salts_component_plugin_scope_release(
            &scope->component_scope);
        memset(scope, 0, sizeof(*scope));
        return SCXML_COMPONENT_ERROR;
    }

    scope->generation_id = generation_id;
    scope->live = true;
    return SCXML_COMPONENT_OK;
}

scxml_component_status scxml_component_scope_release(
    scxml_component_scope *scope) {
    salts_component_plugin_status status;

    if (!scope_live(scope))
        return SCXML_COMPONENT_INVALID_ARGUMENT;
    if (scope->binding_count != 0u)
        return SCXML_COMPONENT_BUSY;

    status = salts_component_plugin_scope_release(
        &scope->component_scope);
    if (status != SALTS_COMPONENT_PLUGIN_OK)
        return component_status(status);

    memset(scope, 0, sizeof(*scope));
    return SCXML_COMPONENT_OK;
}

uint64_t scxml_component_scope_generation_id(
    const scxml_component_scope *scope) {
    return scope_live(scope) ? scope->generation_id : UINT64_C(0);
}

scxml_component_status scxml_component_event_io_provider_bind(
    scxml_component_event_io_provider *provider,
    scxml_component_scope *scope,
    const char *component_id,
    uint64_t required_capabilities) {
    salts_component_service service;
    scxml_event_io_provider interface_value =
        scxml_event_io_provider_bind(NULL, NULL);
    salts_component_plugin_status status;
    cmeta_status projection_status;

    if (provider == NULL || provider->live ||
        !scope_live(scope) ||
        component_id == NULL || component_id[0] == '\0' ||
        scope->binding_count == SIZE_MAX)
        return SCXML_COMPONENT_INVALID_ARGUMENT;

    memset(&service, 0, sizeof(service));
    status = salts_component_plugin_scope_find_service_from(
        &scope->component_scope,
        component_id,
        scxml_event_io_provider_interface(),
        &service);
    if (status != SALTS_COMPONENT_PLUGIN_OK)
        return status == SALTS_COMPONENT_PLUGIN_COMPONENT_ERROR
            ? SCXML_COMPONENT_UNAVAILABLE
            : component_status(status);

    projection_status = scxml_event_io_provider_borrow_from_object(
        service.object, service.interfaces, &interface_value);
    if (projection_status != CMETA_OK ||
        !scxml_event_io_provider_valid(&interface_value))
        return SCXML_COMPONENT_UNAVAILABLE;
    if (!scxml_event_io_provider_has(
            &interface_value, required_capabilities))
        return SCXML_COMPONENT_CAPABILITY_MISMATCH;

    memset(provider, 0, sizeof(*provider));
    if (!scxml_event_io_adapter_bridge_init(
            &provider->bridge, &interface_value))
        return SCXML_COMPONENT_UNAVAILABLE;

    provider->scope = scope;
    provider->generation_id = scope->generation_id;
    provider->live = true;
    ++scope->binding_count;
    return SCXML_COMPONENT_OK;
}

const scxml_event_io_adapter *scxml_component_event_io_provider_adapter(
    const scxml_component_event_io_provider *provider) {
    return provider != NULL && provider->live
        ? scxml_event_io_adapter_bridge_get(&provider->bridge)
        : NULL;
}

void *scxml_component_event_io_provider_user(
    scxml_component_event_io_provider *provider) {
    return provider != NULL && provider->live
        ? scxml_event_io_adapter_bridge_user(&provider->bridge)
        : NULL;
}

scxml_component_status scxml_component_event_io_provider_destroy(
    scxml_component_event_io_provider *provider) {
    const scxml_event_io_adapter *adapter;

    if (provider == NULL || !provider->live ||
        provider->scope == NULL ||
        !scope_live(provider->scope) ||
        provider->scope->generation_id != provider->generation_id ||
        provider->scope->binding_count == 0u)
        return SCXML_COMPONENT_INVALID_ARGUMENT;

    adapter = scxml_event_io_adapter_bridge_get(&provider->bridge);
    if (adapter == NULL || adapter->is_quiescent == NULL)
        return SCXML_COMPONENT_ERROR;
    if (!adapter->is_quiescent(
            scxml_event_io_adapter_bridge_user(&provider->bridge)))
        return SCXML_COMPONENT_BUSY;

    --provider->scope->binding_count;
    memset(provider, 0, sizeof(*provider));
    return SCXML_COMPONENT_OK;
}

scxml_component_status scxml_component_invoke_provider_bind(
    scxml_component_invoke_provider *provider,
    scxml_component_scope *scope,
    const char *component_id,
    uint64_t required_capabilities) {
    salts_component_service service;
    scxml_invoke_provider interface_value =
        scxml_invoke_provider_bind(NULL, NULL);
    salts_component_plugin_status status;
    cmeta_status projection_status;

    if (provider == NULL || provider->live ||
        !scope_live(scope) ||
        component_id == NULL || component_id[0] == '\0' ||
        scope->binding_count == SIZE_MAX)
        return SCXML_COMPONENT_INVALID_ARGUMENT;

    memset(&service, 0, sizeof(service));
    status = salts_component_plugin_scope_find_service_from(
        &scope->component_scope,
        component_id,
        scxml_invoke_provider_interface(),
        &service);
    if (status != SALTS_COMPONENT_PLUGIN_OK)
        return status == SALTS_COMPONENT_PLUGIN_COMPONENT_ERROR
            ? SCXML_COMPONENT_UNAVAILABLE
            : component_status(status);

    projection_status = scxml_invoke_provider_borrow_from_object(
        service.object, service.interfaces, &interface_value);
    if (projection_status != CMETA_OK ||
        !scxml_invoke_provider_valid(&interface_value))
        return SCXML_COMPONENT_UNAVAILABLE;
    if (!scxml_invoke_provider_has(
            &interface_value, required_capabilities))
        return SCXML_COMPONENT_CAPABILITY_MISMATCH;

    memset(provider, 0, sizeof(*provider));
    if (!scxml_invoke_adapter_bridge_init(
            &provider->bridge, &interface_value))
        return SCXML_COMPONENT_UNAVAILABLE;

    provider->scope = scope;
    provider->generation_id = scope->generation_id;
    provider->live = true;
    ++scope->binding_count;
    return SCXML_COMPONENT_OK;
}

const scxml_invoke_adapter *scxml_component_invoke_provider_adapter(
    const scxml_component_invoke_provider *provider) {
    return provider != NULL && provider->live
        ? scxml_invoke_adapter_bridge_get(&provider->bridge)
        : NULL;
}

void *scxml_component_invoke_provider_user(
    scxml_component_invoke_provider *provider) {
    return provider != NULL && provider->live
        ? scxml_invoke_adapter_bridge_user(&provider->bridge)
        : NULL;
}

scxml_component_status scxml_component_invoke_provider_destroy(
    scxml_component_invoke_provider *provider) {
    const scxml_invoke_adapter *adapter;

    if (provider == NULL || !provider->live ||
        provider->scope == NULL ||
        !scope_live(provider->scope) ||
        provider->scope->generation_id != provider->generation_id ||
        provider->scope->binding_count == 0u)
        return SCXML_COMPONENT_INVALID_ARGUMENT;

    adapter = scxml_invoke_adapter_bridge_get(&provider->bridge);
    if (adapter == NULL || adapter->is_quiescent == NULL)
        return SCXML_COMPONENT_ERROR;
    if (!adapter->is_quiescent(
            scxml_invoke_adapter_bridge_user(&provider->bridge)))
        return SCXML_COMPONENT_BUSY;

    --provider->scope->binding_count;
    memset(provider, 0, sizeof(*provider));
    return SCXML_COMPONENT_OK;
}
