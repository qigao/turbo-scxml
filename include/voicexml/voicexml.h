#ifndef TURBO_VOICEXML_H
#define TURBO_VOICEXML_H

#include <xml_parser/xml_parser.h>

#include <stddef.h>

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
    VXML_CLOSED
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

typedef enum vxml_session_state {
    VXML_SESSION_READY = 0,
    VXML_SESSION_RUNNING,
    VXML_SESSION_EXITED,
    VXML_SESSION_FAILED,
    VXML_SESSION_CLOSED
} vxml_session_state;

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
vxml_session_state vxml_session_get_state(const vxml_session *session);
vxml_status vxml_session_error(const vxml_session *session);
vxml_status vxml_session_close(vxml_session *session);
void vxml_session_destroy(vxml_session *session);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_VOICEXML_H */
