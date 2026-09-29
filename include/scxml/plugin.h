#ifndef TURBO_SCXML_PLUGIN_H
#define TURBO_SCXML_PLUGIN_H

#include <scxml/scxml.h>
#include <salts/plugin.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCXML_PLUGIN_COMPILE_OPTIONS_ABI_V1 1u

typedef enum scxml_plugin_status {
    SCXML_PLUGIN_OK = 0,
    SCXML_PLUGIN_INVALID_ARGUMENT,
    SCXML_PLUGIN_ALLOCATION_FAILED,
    SCXML_PLUGIN_PLUGIN_ERROR,
    SCXML_PLUGIN_INCOMPATIBLE_EXPORT,
    SCXML_PLUGIN_COMPILE_FAILED
} scxml_plugin_status;

/**
 * Map one foreign SCXML executable element to one Salts Plugin Function export.
 *
 * The Plugin export remains borrowed from its DSO. The resulting
 * scxml_plugin_program owns the registry lease that keeps every borrowed
 * descriptor, callable adapter, type and code pointer alive until destruction.
 */
typedef struct scxml_plugin_action_v1 {
    size_t struct_size;
    salts_plugin_ref plugin;
    const char *export_id;
    const char *contract_id;
    uint32_t contract_version;
    uint64_t required_capabilities;
    const char *namespace_uri;
    size_t namespace_uri_size;
    const char *local_name;
    size_t local_name_size;
} scxml_plugin_action_v1;

#define SCXML_PLUGIN_ACTION_V1_INIT     {sizeof(scxml_plugin_action_v1), {0u, 0u}, NULL, NULL, 0u, 0u,      NULL, 0u, NULL, 0u}

/**
 * V1 Plugin-backed CMeta compile provider.
 *
 * cmeta points at the canonical V4 SCXML CMeta provider. Its static action
 * table, if any, is combined with plugin_actions for one compile. The registry
 * and referenced plugins are caller-owned, already loaded and STARTED; this
 * API never performs implicit load/start/stop/unload transitions.
 */
typedef struct scxml_plugin_compile_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    salts_plugin_registry *registry;
    const scxml_cmeta_compile_options_v4 *cmeta;
    const scxml_plugin_action_v1 *plugin_actions;
    size_t plugin_action_count;
} scxml_plugin_compile_options_v1;

#define SCXML_PLUGIN_COMPILE_OPTIONS_V1_INIT     {SCXML_PLUGIN_COMPILE_OPTIONS_ABI_V1,      sizeof(scxml_plugin_compile_options_v1), NULL, NULL, NULL, 0u}

/**
 * Owning wrapper around one core SCXML Program plus all Plugin leases required
 * by Plugin-backed custom actions. The wrapper must outlive every session that
 * borrows scxml_plugin_program_core().
 */
typedef struct scxml_plugin_program {
    void *impl;
} scxml_plugin_program;

const char *scxml_plugin_status_string(scxml_plugin_status status);

/**
 * Compile one CMeta SCXML Program from static and Plugin Function actions.
 *
 * On Plugin failure, out_plugin_status receives the underlying Salts status
 * when available. On SCXML compile failure, diagnostic contains the canonical
 * compiler diagnostic. Failure leaves out empty.
 */
scxml_plugin_status scxml_plugin_compile_cmeta_v1(
    scxml_plugin_program *out,
    const char *input,
    size_t input_size,
    const scxml_limits *limits,
    const scxml_plugin_compile_options_v1 *options,
    scxml_diagnostic *diagnostic,
    salts_plugin_status *out_plugin_status);

/** Borrow the wrapped core Program. It remains owned by program. */
const scxml_program *scxml_plugin_program_core(
    const scxml_plugin_program *program);

/**
 * Destroy the core Program first, then release every Plugin lease.
 * No session may still borrow the wrapped Program.
 */
scxml_plugin_status scxml_plugin_program_destroy(
    scxml_plugin_program *program,
    salts_plugin_status *out_plugin_status);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_SCXML_PLUGIN_H */
