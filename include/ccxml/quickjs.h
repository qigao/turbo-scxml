#ifndef TURBO_CCXML_QUICKJS_H
#define TURBO_CCXML_QUICKJS_H

#include <ccxml/ccxml.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CCXML_QUICKJS_DATAMODEL_CONFIG_ABI_V1 1u

/** Serialized owner; destroy Sessions and expression handles before this owner. */
typedef struct ccxml_quickjs_datamodel { void *impl; } ccxml_quickjs_datamodel;

/**
 * Opt-in QuickJS-NG expression profile, with no engine types in the public ABI.
 * All limits must be positive. state_config and this config are copied; the
 * root schema and application state remain borrowed and must outlive the owner.
 * Each evaluation imports current state into a fresh hardened context. JS
 * mutations are discarded; only the existing CMeta assignment tickets publish
 * state. event$ is reserved for current Event fields. foreach is unsupported.
 */
typedef struct ccxml_quickjs_datamodel_config_v1 {
    uint32_t abi_version;
    size_t struct_size;
    ccxml_cmeta_datamodel_config_v1 state_config;
    size_t max_expression_bytes;
    size_t max_expressions;
    size_t max_heap_bytes;
    size_t max_stack_bytes;
    uint64_t max_eval_milliseconds;
    size_t max_conversion_depth;
    size_t max_properties;
    size_t max_array_items;
    size_t max_snapshot_bytes; /**< Bounds root storage size; no native snapshot is made. */
} ccxml_quickjs_datamodel_config_v1;

/** Default bounds; set state_config.root and state_config.state before init. */
ccxml_quickjs_datamodel_config_v1 ccxml_quickjs_default_datamodel_config(void);

/**
 * Initialize an empty owner. Returns INVALID_ARGUMENT for invalid options or
 * schema, ALLOCATION_FAILED for allocation failure, LIMIT_EXCEEDED for bounds,
 * or ADAPTER_ERROR for VM setup failure. On failure the owner remains empty.
 */
ccxml_status ccxml_quickjs_datamodel_init(
    ccxml_quickjs_datamodel *datamodel,
    const ccxml_quickjs_datamodel_config_v1 *config);

/**
 * Static operations; pass the owner as ccxml_session_config.datamodel_user.
 * Conditions use JS truth conversion. Dynamic strings must be nonempty,
 * NUL-free JS strings, borrowed until the next evaluation/destroy.
 * Handle capacity or explicit allocation failure returns SCXML_ADAPTER_FULL;
 * invalid inputs/result types return SCXML_ADAPTER_INVALID_CONTRACT. VM
 * exceptions (including out-of-memory exceptions) and exceeded evaluation
 * limits return SCXML_ADAPTER_ERROR_EXECUTION.
 * Example: config.datamodel = ccxml_quickjs_datamodel_adapter();
 *          config.datamodel_user = &datamodel;
 */
const ccxml_datamodel_adapter_v1 *ccxml_quickjs_datamodel_adapter(void);

/** Release VM and bookkeeping, leaving borrowed application state untouched. */
void ccxml_quickjs_datamodel_destroy(ccxml_quickjs_datamodel *datamodel);

#ifdef __cplusplus
}
#endif
#endif
