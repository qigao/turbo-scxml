#ifndef TURBO_VOICEXML_QUICKJS_H
#define TURBO_VOICEXML_QUICKJS_H

#include <voicexml/script_resource.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VXML_QUICKJS_COMPILE_OPTIONS_ABI_V1 1u
#define VXML_QUICKJS_SESSION_OPTIONS_ABI_V1 1u

/**
 * Bounded compile/runtime sandbox limits for the opt-in VoiceXML QuickJS
 * script profile. No QuickJS ABI type crosses this boundary.
 */
typedef struct vxml_quickjs_compile_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    size_t max_expression_bytes;
    size_t max_dynamic_script_uri_bytes;
    size_t max_heap_bytes;
    size_t max_stack_bytes;
    uint64_t max_eval_milliseconds;
} vxml_quickjs_compile_options_v1;

/**
 * Runtime resource boundary for one VoiceXML QuickJS session.
 *
 * resolver and script_resources are borrowed for the Session lifetime.
 * base_document_uri is copied during init.
 */
typedef struct vxml_quickjs_session_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const vxml_document_store *resolver;
    const vxml_script_resource_adapter_v1 *script_resources;
    void *script_resource_user;
    const char *base_document_uri;
    size_t base_document_uri_size;
    size_t max_resolved_uri_bytes;
    size_t max_script_source_bytes;
} vxml_quickjs_session_options_v1;

vxml_quickjs_compile_options_v1
vxml_quickjs_default_compile_options(void);

/**
 * Compile the explicit VoiceXML QuickJS external-script profile.
 *
 * This admits static script@src plus VoiceXML 2.1 script@srcexpr and validates
 * JavaScript expression syntax at compile time. It does not make base
 * vxml_compile() or vxml_compile_external_script_profile() accept srcexpr.
 */
vxml_status vxml_compile_quickjs_script_profile(
    const void *bytes, size_t size,
    const vxml_limits *limits,
    const vxml_quickjs_compile_options_v1 *options,
    vxml_program *out,
    vxml_diagnostic *diagnostic);

/** Initialize one session for a program compiled by the QuickJS profile. */
vxml_status vxml_session_init_quickjs(
    vxml_session *session,
    const vxml_program *program,
    const vxml_quickjs_session_options_v1 *options);

/** Query the exact VoiceXML Event produced by the last QuickJS-profile failure. */
vxml_status vxml_quickjs_session_last_event(
    const vxml_session *session,
    const char **out_event,
    size_t *out_event_size);

#ifdef __cplusplus
}
#endif

#endif
