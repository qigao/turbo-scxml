#ifndef TURBO_SCXML_COMPONENT_H
#define TURBO_SCXML_COMPONENT_H

#include <scxml/provider.h>
#include <salts/component_plugin.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum scxml_component_status {
    SCXML_COMPONENT_OK = 0,
    SCXML_COMPONENT_INVALID_ARGUMENT,
    SCXML_COMPONENT_UNAVAILABLE,
    SCXML_COMPONENT_CAPABILITY_MISMATCH,
    SCXML_COMPONENT_BUSY,
    SCXML_COMPONENT_ERROR
} scxml_component_status;

typedef struct scxml_component_scope {
    salts_component_plugin_scope component_scope;
    uint64_t generation_id;
    size_t binding_count;
    bool live;
} scxml_component_scope;

typedef struct scxml_component_event_io_provider {
    scxml_component_scope *scope;
    uint64_t generation_id;
    scxml_event_io_adapter_bridge bridge;
    bool live;
} scxml_component_event_io_provider;

typedef struct scxml_component_invoke_provider {
    scxml_component_scope *scope;
    uint64_t generation_id;
    scxml_invoke_adapter_bridge bridge;
    bool live;
} scxml_component_invoke_provider;

const char *scxml_component_status_string(scxml_component_status status);

scxml_component_status scxml_component_scope_acquire(
    scxml_component_scope *scope,
    salts_component_plugin_runtime *runtime);

scxml_component_status scxml_component_scope_release(
    scxml_component_scope *scope);

uint64_t scxml_component_scope_generation_id(
    const scxml_component_scope *scope);

scxml_component_status scxml_component_event_io_provider_bind(
    scxml_component_event_io_provider *provider,
    scxml_component_scope *scope,
    const char *component_id,
    uint64_t required_capabilities);

const scxml_event_io_adapter *scxml_component_event_io_provider_adapter(
    const scxml_component_event_io_provider *provider);

void *scxml_component_event_io_provider_user(
    scxml_component_event_io_provider *provider);

scxml_component_status scxml_component_event_io_provider_destroy(
    scxml_component_event_io_provider *provider);

scxml_component_status scxml_component_invoke_provider_bind(
    scxml_component_invoke_provider *provider,
    scxml_component_scope *scope,
    const char *component_id,
    uint64_t required_capabilities);

const scxml_invoke_adapter *scxml_component_invoke_provider_adapter(
    const scxml_component_invoke_provider *provider);

void *scxml_component_invoke_provider_user(
    scxml_component_invoke_provider *provider);

scxml_component_status scxml_component_invoke_provider_destroy(
    scxml_component_invoke_provider *provider);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_SCXML_COMPONENT_H */
