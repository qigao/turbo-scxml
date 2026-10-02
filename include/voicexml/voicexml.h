#ifndef TURBO_VOICEXML_H
#define TURBO_VOICEXML_H

#include <xml_parser/xml_parser.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VXML_DIAGNOSTIC_CAPACITY 256u

typedef enum vxml_status {
    VXML_OK = 0,
    VXML_INVALID_ARGUMENT,
    VXML_XML_ERROR,
    VXML_ALLOCATION_FAILED,
    VXML_LIMIT_EXCEEDED,
    VXML_INVALID_NAMESPACE,
    VXML_INVALID_VERSION,
    VXML_DUPLICATE_ID,
    VXML_INVALID_STRUCTURE,
    VXML_UNSUPPORTED_FEATURE,
    VXML_INVALID_STATE,
    VXML_CLOSED,
    VXML_INVALID_CONTRACT,
    VXML_SEMANTIC_ERROR
} vxml_status;

typedef struct vxml_limits {
    salts_xml_limits xml;
    size_t max_forms;
    size_t max_blocks;
    size_t max_actions;
    size_t max_name_bytes;
} vxml_limits;

typedef struct vxml_diagnostic {
    vxml_status status;
    salts_xml_location location;
    char message[VXML_DIAGNOSTIC_CAPACITY];
} vxml_diagnostic;

typedef enum vxml_program_state {
    VXML_PROGRAM_EMPTY = 0,
    VXML_PROGRAM_COMPILED
} vxml_program_state;

/**
 * Caller-allocated, single-owner handle for one private immutable program.
 * An initialized handle is non-copyable: a plain struct copy would create two
 * apparent owners and must never be destroyed twice. To move ownership, copy
 * the complete handle into an empty destination and immediately zero the
 * source handle. Destroy the current program before reusing its handle.
 */
typedef struct vxml_program {
    void *impl;
} vxml_program;

typedef enum vxml_submit_method {
    VXML_SUBMIT_METHOD_GET = 1,
    VXML_SUBMIT_METHOD_POST
} vxml_submit_method;

typedef enum vxml_session_state {
    VXML_SESSION_READY = 0,
    VXML_SESSION_RUNNING,
    VXML_SESSION_EXITED,
    VXML_SESSION_FAILED,
    VXML_SESSION_CLOSED,
    VXML_SESSION_NAVIGATING,
    VXML_SESSION_SUBMITTING,
    VXML_SESSION_SCRIPTING
} vxml_session_state;

typedef enum vxml_submit_enctype {
    VXML_SUBMIT_ENCTYPE_URLENCODED = 1,
    VXML_SUBMIT_ENCTYPE_MULTIPART_FORM_DATA
} vxml_submit_enctype;

#define VXML_SUBMIT_TARGET_ABI_V1 1u

typedef struct vxml_submit_target_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const char *uri;
    size_t uri_size;
    vxml_submit_method method;
    vxml_submit_enctype enctype;
} vxml_submit_target_v1;

#define VXML_EXTERNAL_SCRIPT_TARGET_ABI_V1 1u

typedef struct vxml_external_script_target_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const char *src;
    size_t src_size;
    const char *charset;
    size_t charset_size;
} vxml_external_script_target_v1;

typedef struct vxml_navigation_target {
    const char *uri;
    size_t uri_size;
} vxml_navigation_target;

#define VXML_NAVIGATION_REQUEST_ABI_V1 1u

typedef struct vxml_navigation_request_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const char *uri;
    size_t uri_size;
    const char *fetchaudio_uri;
    size_t fetchaudio_uri_size;

    /* Optional append-only inherited fetch-audio timing policy. */
    bool has_fetchaudio_delay;
    uint64_t fetchaudio_delay_us;
    bool has_fetchaudio_minimum;
    uint64_t fetchaudio_minimum_us;
} vxml_navigation_request_v1;

/**
 * Caller-allocated, single-owner, non-copyable session handle. Move only by
 * copying the complete handle into an empty destination and immediately
 * zeroing the source. Destroy the current session before reusing its handle.
 */
typedef struct vxml_session {
    void *impl;
} vxml_session;

vxml_limits vxml_default_limits(void);

/**
 * Compile input bytes into a program. `out` may contain indeterminate storage;
 * this function initializes it to an empty handle before validation. A caller
 * must destroy any previously compiled program before reusing its handle.
 * Every failure leaves a supplied output handle empty.
 */
vxml_status vxml_compile(const void *bytes, size_t size,
                         const vxml_limits *limits,
                         vxml_program *out,
                         vxml_diagnostic *diagnostic);

void vxml_program_destroy(vxml_program *program);

/**
 * Initializes a caller-allocated session to borrow `program`. `program` must
 * outlive the session. `session` may contain indeterminate storage; this
 * function clears it before validation. Destroy a previously initialized
 * session before reusing its handle.
 */
vxml_status vxml_session_init(vxml_session *session,
                              const vxml_program *program);
vxml_status vxml_session_start(vxml_session *session);

/**
 * Start a supported runtime profile at one compiled form ID.
 *
 * The ID is borrowed only for this call. Literal and built-in CMeta profiles
 * enter the selected immutable form. Missing IDs fail the session with
 * VXML_INVALID_STRUCTURE; profiles without named-form entry support return
 * VXML_INVALID_CONTRACT.
 */
vxml_status vxml_session_start_at_form(
    vxml_session *session,
    const char *form_id,
    size_t form_id_size);

/**
 * Borrow the external goto target while state is VXML_SESSION_NAVIGATING.
 *
 * The returned Program-owned view remains valid until close/destruction.
 */
vxml_status vxml_session_navigation(
    const vxml_session *session,
    vxml_navigation_target *out_target);

vxml_status vxml_session_navigation_request(
    const vxml_session *session,
    vxml_navigation_request_v1 *out_request);

/** Borrow the literal submit target while state is VXML_SESSION_SUBMITTING. */
vxml_status vxml_session_submit(
    const vxml_session *session,
    vxml_submit_target_v1 *out_target);

/** Borrow the external script descriptor while state is VXML_SESSION_SCRIPTING. */
vxml_status vxml_session_script(
    const vxml_session *session,
    vxml_external_script_target_v1 *out_target);

/**
 * Synchronously inject one byte-counted Event into the active runtime profile.
 *
 * Profiles without Event semantics return VXML_UNSUPPORTED_FEATURE without
 * mutating the Session. Event bytes are borrowed only for this call.
 */
vxml_status vxml_session_raise_event(
    vxml_session *session,
    const char *event_name,
    size_t event_name_size);

vxml_session_state vxml_session_get_state(const vxml_session *session);
vxml_status vxml_session_error(const vxml_session *session);
vxml_status vxml_session_close(vxml_session *session);
void vxml_session_destroy(vxml_session *session);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_VOICEXML_H */
