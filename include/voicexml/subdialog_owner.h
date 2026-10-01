#ifndef TURBO_VOICEXML_SUBDIALOG_OWNER_H
#define TURBO_VOICEXML_SUBDIALOG_OWNER_H

#include <voicexml/cmeta.h>
#include <voicexml/document_store.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VXML_CMETA_SUBDIALOG_OWNER_CONFIG_ABI_V1 1u

typedef struct vxml_cmeta_subdialog_owner_config_v1 {
    uint32_t abi_version;
    size_t struct_size;

    /* Fixed child-row count and copied request bounds. */
    size_t capacity;
    size_t max_uri_bytes;
    size_t max_params;
    size_t max_param_bytes;
    size_t max_completion_entries;

    /* Hard control-flow bounds. */
    size_t max_navigation_hops;
    size_t max_nesting_depth;

    /* Borrowed for the owner lifetime. */
    vxml_document_store *document_store;

    /*
     * Copied as a size-versioned prefix during owner init. initial_root and
     * provider user pointers inside the options remain caller-owned. For every
     * child Session, the owner replaces the subdialog adapter/user with its
     * nested context while preserving the remaining provider configuration.
     */
    const vxml_cmeta_session_options_v1 *child_session_options;
} vxml_cmeta_subdialog_owner_config_v1;

typedef struct vxml_cmeta_subdialog_owner_stats {
    size_t capacity;
    size_t active;
    size_t reserved;
    size_t pending;
    size_t canceled;
    uint64_t accepted;
    uint64_t discarded;
    uint64_t stale;
    bool root_bound;
    bool closed;
} vxml_cmeta_subdialog_owner_stats;

typedef struct vxml_cmeta_subdialog_owner {
    void *impl;
} vxml_cmeta_subdialog_owner;

vxml_cmeta_subdialog_owner_config_v1
vxml_cmeta_subdialog_owner_default_config_v1(void);

vxml_status vxml_cmeta_subdialog_owner_init(
    vxml_cmeta_subdialog_owner *owner,
    const vxml_cmeta_subdialog_owner_config_v1 *config);

/**
 * Existing parent Sessions use this adapter. The adapter is immutable and
 * process-wide; use vxml_cmeta_subdialog_owner_root_user() as subdialog_user.
 */
const vxml_cmeta_subdialog_adapter_v1 *
vxml_cmeta_subdialog_owner_adapter(void);

/**
 * Stable root parent-context pointer used as vxml_cmeta_session_options_v1
 * subdialog_user. It may be obtained before the root Session is bound.
 */
void *vxml_cmeta_subdialog_owner_root_user(
    vxml_cmeta_subdialog_owner *owner);

/**
 * Bind the root parent Session and copy its current normalized document URI.
 *
 * Call after vxml_session_init_cmeta() and before that Session can prepare a
 * subdialog. Rebinding is rejected while any child row is live.
 */
vxml_status vxml_cmeta_subdialog_owner_bind_root(
    vxml_cmeta_subdialog_owner *owner,
    vxml_session *parent_session,
    const char *current_document_uri,
    size_t current_document_uri_size);

/**
 * Serialized owner progress. Starts pending children, follows child-local
 * external navigation through DocumentStore, drives nested subdialogs, and
 * publishes terminal child completions into the parent generation mailbox.
 */
vxml_status vxml_cmeta_subdialog_owner_run_ready(
    vxml_cmeta_subdialog_owner *owner,
    size_t max_work,
    size_t *out_processed);

/**
 * Stop new child admission and mark every live child for serialized teardown.
 * No provider/document release occurs from the adapter cancel callback.
 */
void vxml_cmeta_subdialog_owner_close(
    vxml_cmeta_subdialog_owner *owner);

bool vxml_cmeta_subdialog_owner_is_quiescent(
    const vxml_cmeta_subdialog_owner *owner);

bool vxml_cmeta_subdialog_owner_get_stats(
    const vxml_cmeta_subdialog_owner *owner,
    vxml_cmeta_subdialog_owner_stats *out_stats);

/**
 * Destroy only after close + quiescence. DocumentStore, parent Session, child
 * session provider pointers, and initial_root remain caller-owned.
 */
vxml_status vxml_cmeta_subdialog_owner_destroy(
    vxml_cmeta_subdialog_owner *owner);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_VOICEXML_SUBDIALOG_OWNER_H */
