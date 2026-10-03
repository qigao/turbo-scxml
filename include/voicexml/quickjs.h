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
#define VXML_QUICKJS_SCRIPT_EXECUTE_REQUEST_ABI_V1 1u

/**
 * Bounded limits for the opt-in VoiceXML QuickJS script-target profile.
 *
 * No QuickJS ABI type crosses this boundary. These limits apply only to
 * script@srcexpr validation/evaluation and its Session-owned URI result.
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
     * Optional append-only external-script execution state contract.
     *
     * root == NULL preserves the target-only #231 profile. A non-NULL root
     * enables #235 execution and requires all conversion/snapshot bounds.
     * The descriptor is borrowed for the Program lifetime.
     */
    const cmeta_data_desc *root;
    size_t max_string_bytes;
    size_t max_conversion_depth;
    size_t max_properties;
    size_t max_array_items;
    size_t max_snapshot_bytes;
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
     * Optional append-only initial typed root. Required only when the Program
     * compile options enabled a non-NULL root descriptor. The Session copies
     * this object transactionally and does not retain this pointer.
     */
    const void *initial_root;
} vxml_quickjs_session_options_v1;

typedef struct vxml_quickjs_script_execute_request_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const vxml_document_store *resolver;
    const vxml_script_resource_adapter_v1 *resources;
    void *resource_user;
    const char *base_document_uri;
    size_t base_document_uri_size;
    size_t max_uri_bytes;
    size_t max_source_bytes;
} vxml_quickjs_script_execute_request_v1;

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
 * Execute the currently published external SCRIPTING target, close its source
 * lease exactly once, atomically publish the typed root on success, then resume
 * after the script action. The request is borrowed only for this call.
 */
vxml_status vxml_quickjs_session_execute_external_script(
    vxml_session *session,
    const vxml_quickjs_script_execute_request_v1 *request);

/** Borrow the Session-owned committed typed root when execution state is enabled. */
vxml_status vxml_quickjs_session_root(
    const vxml_session *session,
    const void **out_root);

/** Query the exact Event produced by the last profile-level failure. */
vxml_status vxml_quickjs_session_last_event(
    const vxml_session *session,
    const char **out_event,
    size_t *out_event_size);

#ifdef __cplusplus
}
#endif

#endif
