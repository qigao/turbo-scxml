#ifndef TURBO_VOICEXML_DIALOG_MANAGER_H
#define TURBO_VOICEXML_DIALOG_MANAGER_H

#include <ccxml/ccxml.h>
#include <voicexml/voicexml.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VXML_DIALOG_MANAGER_CONFIG_ABI_V1 1u
#define VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1 1u
#define VXML_DIALOG_EVENT_SINK_ABI_V1 1u

typedef enum vxml_dialog_manager_status {
    VXML_DIALOG_MANAGER_OK = 0,
    VXML_DIALOG_MANAGER_INVALID_ARGUMENT,
    VXML_DIALOG_MANAGER_ALLOCATION_FAILED,
    VXML_DIALOG_MANAGER_FULL,
    VXML_DIALOG_MANAGER_CLOSED,
    VXML_DIALOG_MANAGER_NOT_FOUND,
    VXML_DIALOG_MANAGER_INVALID_STATE,
    VXML_DIALOG_MANAGER_DOCUMENT_ERROR,
    VXML_DIALOG_MANAGER_VXML_ERROR,
    VXML_DIALOG_MANAGER_EVENT_FULL,
    VXML_DIALOG_MANAGER_EVENT_CLOSED,
    VXML_DIALOG_MANAGER_BUSY
} vxml_dialog_manager_status;

typedef enum vxml_dialog_event_sink_status {
    VXML_DIALOG_EVENT_ACCEPTED = 0,
    VXML_DIALOG_EVENT_FULL,
    VXML_DIALOG_EVENT_CLOSED,
    VXML_DIALOG_EVENT_INVALID_ARGUMENT
} vxml_dialog_event_sink_status;

/**
 * Provider-owned immutable VoiceXML source bytes.
 *
 * A successful open is paired with exactly one close after compilation.
 * The manager never retains data beyond close.
 */
typedef struct vxml_dialog_document {
    const void *data;
    size_t size;
    void *lease;
} vxml_dialog_document;

/**
 * Synchronous document acquisition boundary.
 *
 * source/media_type are copied manager-owned bytes. max_bytes is a hard
 * caller limit. Network policy, redirects, authorization and cache policy
 * belong to the provider; #48 may supply a CHTTP implementation.
 */
typedef struct vxml_dialog_document_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    vxml_dialog_manager_status (*open)(
        void *user,
        const char *source, size_t source_size,
        const char *media_type, size_t media_type_size,
        size_t max_bytes,
        vxml_dialog_document *out_document);
    void (*close)(void *user, vxml_dialog_document *document);
} vxml_dialog_document_adapter_v1;

/** Callback-scoped manager Event. Copy any retained bytes before returning. */
typedef struct vxml_dialog_event_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const char *name;
    size_t name_size;
    const char *dialog_id;
    size_t dialog_id_size;
    const char *connection_id;
    size_t connection_id_size;
    vxml_status voice_status;
} vxml_dialog_event_v1;

/**
 * Nonblocking Event publication boundary.
 *
 * FULL publishes nothing and leaves the manager row pending for a later
 * run_ready(). CLOSED publishes nothing and makes progress visibly fail.
 */
typedef struct vxml_dialog_event_sink_v1 {
    uint32_t abi_version;
    size_t struct_size;
    vxml_dialog_event_sink_status (*try_publish)(
        void *user, const vxml_dialog_event_v1 *event);
} vxml_dialog_event_sink_v1;

typedef struct vxml_dialog_manager_config_v1 {
    uint32_t abi_version;
    size_t struct_size;

    /** Fixed number of dialog rows. */
    size_t capacity;
    /** Hard copied-byte bounds per row. */
    size_t max_source_bytes;
    size_t max_media_type_bytes;
    size_t max_connection_id_bytes;
    size_t max_dialog_id_bytes;
    /** Hard provider document bound. */
    size_t max_document_bytes;

    /** VoiceXML compile limits applied to every acquired document. */
    vxml_limits voice_limits;

    /** Existing call/conference provider; copied as a size-versioned prefix. */
    const ccxml_telephony_adapter_v1 *upstream;
    void *upstream_user;

    const vxml_dialog_document_adapter_v1 *documents;
    void *document_user;

    const vxml_dialog_event_sink_v1 *events;
    void *event_user;
} vxml_dialog_manager_config_v1;

typedef struct vxml_dialog_manager_stats {
    size_t capacity;
    size_t active;
    size_t reserved;
    size_t pending;
    size_t prepared;
    size_t terminal_pending;
    uint64_t accepted_operations;
    uint64_t discarded_operations;
    uint64_t rejected_full;
    uint64_t stale_operations;
    bool closed;
} vxml_dialog_manager_stats;

typedef struct vxml_dialog_manager {
    void *impl;
} vxml_dialog_manager;

/** Defaults include bounded capacities but no provider pointers. */
vxml_dialog_manager_config_v1 vxml_dialog_manager_default_config_v1(void);

const char *vxml_dialog_manager_status_string(
    vxml_dialog_manager_status status);

vxml_dialog_manager_status vxml_dialog_manager_init(
    vxml_dialog_manager *manager,
    const vxml_dialog_manager_config_v1 *config);

/**
 * Full-size CCXML telephony decorator. Non-dialog operations forward to the
 * configured upstream adapter. Dialog operations are owned by the manager.
 */
const ccxml_telephony_adapter_v1 *vxml_dialog_manager_ccxml_adapter(void);

/** Use this as ccxml_session_config.telephony_user. */
void *vxml_dialog_manager_ccxml_user(vxml_dialog_manager *manager);

/**
 * Explicit serialized progress point.
 *
 * Processes up to max_work published rows. max_work must be positive. This
 * function performs document acquisition, VoiceXML compile/start and Event
 * publication; no such work occurs in effect-ticket commit callbacks.
 */
vxml_dialog_manager_status vxml_dialog_manager_run_ready(
    vxml_dialog_manager *manager,
    size_t max_work,
    size_t *out_processed);

bool vxml_dialog_manager_get_stats(
    const vxml_dialog_manager *manager,
    vxml_dialog_manager_stats *out_stats);

/**
 * Stop new dialog admission, release every manager-owned dialog/program/session
 * row, then close the upstream telephony adapter exactly once.
 */
void vxml_dialog_manager_close(vxml_dialog_manager *manager);

bool vxml_dialog_manager_is_quiescent(
    const vxml_dialog_manager *manager);

/**
 * Destroy only after close and quiescence. Borrowed providers remain caller
 * owned and are never destroyed by the manager.
 */
vxml_dialog_manager_status vxml_dialog_manager_destroy(
    vxml_dialog_manager *manager);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_VOICEXML_DIALOG_MANAGER_H */
