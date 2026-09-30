#ifndef TURBO_VOICEXML_DIALOG_MANAGER_H
#define TURBO_VOICEXML_DIALOG_MANAGER_H

#include <ccxml/ccxml.h>
#include <voicexml/resource.h>
#include <voicexml/submit_resource.h>
#include <voicexml/voicexml.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VXML_DIALOG_MANAGER_CONFIG_ABI_V1 1u
#define VXML_DIALOG_MANAGER_CONFIG_ABI_V2 2u
#define VXML_DIALOG_MANAGER_CONFIG_ABI_V3 3u
#define VXML_DIALOG_MANAGER_CONFIG_ABI_V4 4u
#define VXML_DIALOG_EVENT_SINK_ABI_V1 1u

typedef enum vxml_dialog_event_sink_status {
    VXML_DIALOG_EVENT_ACCEPTED = 0,
    VXML_DIALOG_EVENT_FULL,
    VXML_DIALOG_EVENT_CLOSED,
    VXML_DIALOG_EVENT_INVALID_ARGUMENT
} vxml_dialog_event_sink_status;

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

typedef struct vxml_document_store vxml_document_store;

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

/**
 * Store-backed dialog-manager configuration.
 *
 * V2 does not own/fetch/compile documents itself. It borrows one
 * VoiceXMLDocumentStore and retains generation-safe document refs in dialog
 * rows for as long as a prepared Program or running Session needs them.
 *
 * max_source_bytes also bounds URI-resolution scratch. A resolved URI that
 * exceeds this bound fails before store acquisition.
 */
typedef struct vxml_dialog_manager_config_v2 {
    uint32_t abi_version;
    size_t struct_size;

    size_t capacity;
    size_t max_source_bytes;
    size_t max_media_type_bytes;
    size_t max_connection_id_bytes;
    size_t max_dialog_id_bytes;

    const ccxml_telephony_adapter_v1 *upstream;
    void *upstream_user;

    vxml_document_store *document_store;

    const vxml_dialog_event_sink_v1 *events;
    void *event_user;
} vxml_dialog_manager_config_v2;

/**
 * Navigation-enabled store-backed manager configuration.
 *
 * V3 keeps the V2 ownership model and adds one hard bound for external
 * document handoffs produced by VXML_SESSION_NAVIGATING. Each hop resolves
 * against the current cached document URI, acquires the next document before
 * releasing the old borrow, and optionally enters the resolved fragment.
 */
typedef struct vxml_dialog_manager_config_v3 {
    uint32_t abi_version;
    size_t struct_size;

    size_t capacity;
    size_t max_source_bytes;
    size_t max_media_type_bytes;
    size_t max_connection_id_bytes;
    size_t max_dialog_id_bytes;
    size_t max_navigation_hops;

    const ccxml_telephony_adapter_v1 *upstream;
    void *upstream_user;

    vxml_document_store *document_store;

    const vxml_dialog_event_sink_v1 *events;
    void *event_user;
} vxml_dialog_manager_config_v3;

/**
 * Submit-enabled store-backed manager configuration.
 *
 * V4 preserves V3 external navigation and adds one borrowed one-attempt submit
 * provider. max_navigation_hops bounds the combined external goto/submit
 * control-transfer chain. max_submit_response_bytes bounds leased response
 * VoiceXML before compilation.
 */
typedef struct vxml_dialog_manager_config_v4 {
    uint32_t abi_version;
    size_t struct_size;

    size_t capacity;
    size_t max_source_bytes;
    size_t max_media_type_bytes;
    size_t max_connection_id_bytes;
    size_t max_dialog_id_bytes;
    size_t max_navigation_hops;
    size_t max_submit_response_bytes;
    vxml_limits voice_limits;

    const ccxml_telephony_adapter_v1 *upstream;
    void *upstream_user;

    vxml_document_store *document_store;

    const vxml_submit_resource_adapter_v1 *submit;
    void *submit_user;

    const vxml_dialog_event_sink_v1 *events;
    void *event_user;
} vxml_dialog_manager_config_v4;

typedef struct vxml_dialog_manager_stats {
    size_t capacity;
    size_t active;
    size_t reserved;
    size_t pending;
    size_t prepared;
    size_t event_pending;
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
vxml_dialog_manager_config_v2 vxml_dialog_manager_default_config_v2(void);
vxml_dialog_manager_config_v3 vxml_dialog_manager_default_config_v3(void);
vxml_dialog_manager_config_v4 vxml_dialog_manager_default_config_v4(void);

const char *vxml_dialog_manager_status_string(
    vxml_dialog_manager_status status);

vxml_dialog_manager_status vxml_dialog_manager_init(
    vxml_dialog_manager *manager,
    const vxml_dialog_manager_config_v1 *config);

vxml_dialog_manager_status vxml_dialog_manager_init_v2(
    vxml_dialog_manager *manager,
    const vxml_dialog_manager_config_v2 *config);

vxml_dialog_manager_status vxml_dialog_manager_init_v3(
    vxml_dialog_manager *manager,
    const vxml_dialog_manager_config_v3 *config);

vxml_dialog_manager_status vxml_dialog_manager_init_v4(
    vxml_dialog_manager *manager,
    const vxml_dialog_manager_config_v4 *config);

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
