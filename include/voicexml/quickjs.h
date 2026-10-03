#ifndef TURBO_VOICEXML_QUICKJS_H
#define TURBO_VOICEXML_QUICKJS_H

#include <voicexml/voicexml.h>
#include <voicexml/script_resource.h>
#include <cmeta/data.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VXML_QUICKJS_COMPILE_OPTIONS_ABI_V1 1u
#define VXML_QUICKJS_SESSION_OPTIONS_ABI_V1 1u
#define VXML_QUICKJS_SCRIPT_EXECUTION_ABI_V1 1u

/**
 * Bounded limits for the opt-in VoiceXML QuickJS script-target profile.
 *
 * No QuickJS ABI type crosses this boundary. These limits apply only to
 * script@srcexpr validation/evaluation and its Session-owned URI result.
 *
 * The profile does not expose or claim a deterministic QuickJS bytecode
 * instruction-count quota. Runtime safety uses explicit heap/stack bounds,
 * monotonic max_eval_milliseconds, and the conversion/snapshot limits below.
 */
typedef struct vxml_quickjs_compile_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    size_t max_expression_bytes;
    size_t max_dynamic_script_uri_bytes;
    size_t max_heap_bytes;
    size_t max_stack_bytes;
    uint64_t max_eval_milliseconds;

    /*
     * Optional append-only transactional CMeta state profile.
     *
     * root is borrowed by the compiled Program and must outlive it. A NULL
     * root preserves the target-only #231 profile and ignores the conversion
     * fields below.
     */
    const cmeta_data_desc *root;
    size_t max_conversion_depth;
    size_t max_properties;
    size_t max_array_items;
    size_t max_snapshot_bytes;
    size_t max_state_string_bytes;

    /* External source acquisition bounds used by #235 execution. */
    size_t max_resolved_script_uri_bytes;
    size_t max_script_source_bytes;
} vxml_quickjs_compile_options_v1;

/**
 * Append-only runtime options for one VoiceXML QuickJS target session.
 *
 * This first slice has no resource/provider fields: it stops at
 * VXML_SESSION_SCRIPTING and reuses vxml_session_script() as the handoff.
 */
typedef struct vxml_quickjs_session_options_v1 {
    uint32_t abi_version;
    size_t struct_size;

    /*
     * Optional append-only initial committed CMeta root.
     * Required when the Program was compiled with a non-NULL root and copied
     * transactionally into Session-owned storage during init.
     */
    const void *initial_state;
} vxml_quickjs_session_options_v1;

/**
 * One explicit external-script execution attempt.
 *
 * The resolver/provider/base URI are borrowed only for this call. The profile
 * performs exactly one ScriptResource acquisition and closes any published
 * lease exactly once before returning.
 */
typedef struct vxml_quickjs_script_execution_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const vxml_document_store *resolver;
    const vxml_script_resource_adapter_v1 *script_resources;
    void *script_resource_user;
    const char *base_document_uri;
    size_t base_document_uri_size;
} vxml_quickjs_script_execution_v1;

#define VXML_QUICKJS_SCRIPT_EXECUTION_V1_INIT \
    {VXML_QUICKJS_SCRIPT_EXECUTION_ABI_V1, \
     sizeof(vxml_quickjs_script_execution_v1), \
     NULL, NULL, NULL, NULL, 0u}

vxml_quickjs_compile_options_v1
vxml_quickjs_default_compile_options(void);

vxml_quickjs_session_options_v1
vxml_quickjs_default_session_options(void);

/**
 * Compile the explicit VoiceXML QuickJS script-target profile.
 *
 * Accepts static script@src plus VoiceXML 2.1 script@srcexpr. Dynamic
 * expressions are retained in Program-owned storage and validated once.
 * Base vxml_compile() and vxml_compile_external_script_profile() remain
 * fail-closed for srcexpr.
 */
vxml_status vxml_compile_quickjs_script_profile(
    const void *bytes, size_t size,
    const vxml_limits *limits,
    const vxml_quickjs_compile_options_v1 *options,
    vxml_program *out,
    vxml_diagnostic *diagnostic);

/** Initialize one Session for a program compiled by this profile. */
vxml_status vxml_session_init_quickjs(
    vxml_session *session,
    const vxml_program *program,
    const vxml_quickjs_session_options_v1 *options);

/**
 * Acquire, execute and settle the currently exposed external script target.
 *
 * Requires VXML_SESSION_SCRIPTING and a Program compiled with a typed CMeta
 * root. Source execution uses a fresh hardened context imported from committed
 * state, exports into typed scratch, publishes atomically, closes the resource
 * lease exactly once, and resumes after the script action.
 */
vxml_status vxml_quickjs_session_execute_script(
    vxml_session *session,
    const vxml_quickjs_script_execution_v1 *execution);

/**
 * Borrow the current committed typed CMeta root.
 *
 * Valid only for a Session whose Program enabled the typed-state tail. The
 * returned object is Session-owned and may change only after a successful
 * transactional script publication.
 */
vxml_status vxml_quickjs_session_state(
    const vxml_session *session,
    const void **out_state);

/** Query the exact Event produced by the last profile-level failure. */
vxml_status vxml_quickjs_session_last_event(
    const vxml_session *session,
    const char **out_event,
    size_t *out_event_size);

#ifdef __cplusplus
}
#endif

#endif
