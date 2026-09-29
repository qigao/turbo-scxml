#ifndef TURBO_VOICEXML_DOCUMENT_STORE_H
#define TURBO_VOICEXML_DOCUMENT_STORE_H

#include <voicexml/dialog_manager.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VXML_DOCUMENT_STORE_CONFIG_ABI_V1 1u

typedef enum vxml_document_store_status {
    VXML_DOCUMENT_STORE_OK = 0,
    VXML_DOCUMENT_STORE_INVALID_ARGUMENT,
    VXML_DOCUMENT_STORE_ALLOCATION_FAILED,
    VXML_DOCUMENT_STORE_INVALID_URI,
    VXML_DOCUMENT_STORE_LIMIT_EXCEEDED,
    VXML_DOCUMENT_STORE_FULL,
    VXML_DOCUMENT_STORE_RESOURCE_ERROR,
    VXML_DOCUMENT_STORE_COMPILE_ERROR,
    VXML_DOCUMENT_STORE_STALE,
    VXML_DOCUMENT_STORE_BUSY
} vxml_document_store_status;

/**
 * Caller-owned output buffers for URI resolution.
 *
 * document_uri never includes a fragment and is suitable as the cache/fetch
 * key. fragment excludes the leading '#'. Empty fragments have size zero.
 */
typedef struct vxml_resolved_uri_v1 {
    uint32_t abi_version;
    size_t struct_size;
    char *document_uri;
    size_t document_uri_capacity;
    size_t document_uri_size;
    char *fragment;
    size_t fragment_capacity;
    size_t fragment_size;
} vxml_resolved_uri_v1;

#define VXML_RESOLVED_URI_V1_INIT     {1u, sizeof(vxml_resolved_uri_v1), NULL, 0u, 0u, NULL, 0u, 0u}

/** Generation-safe borrowed cache reference. slot is 1-based. */
typedef struct vxml_document_ref {
    uint32_t slot;
    uint32_t generation;
} vxml_document_ref;

typedef struct vxml_document_view {
    const char *document_uri;
    size_t document_uri_size;
    const void *source;
    size_t source_size;
    const vxml_program *program;
} vxml_document_view;

typedef struct vxml_document_store_error {
    vxml_document_store_status status;
    vxml_dialog_manager_status resource_status;
    vxml_status voice_status;
} vxml_document_store_error;

typedef struct vxml_document_store_stats {
    size_t capacity;
    size_t entries;
    size_t cached_bytes;
    size_t active_borrows;
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
} vxml_document_store_stats;

/**
 * Fixed-row, bounded-byte immutable VoiceXML document cache.
 *
 * application_uri is copied during initialization and must be an absolute
 * hierarchical URI. The store borrows the document provider operations/user
 * until destruction. Each successfully cached entry owns normalized URI bytes,
 * source bytes, and one compiled vxml_program.
 *
 * max_cache_bytes bounds copied URI+source bytes across all entries.
 * vxml_limits independently bounds compiled-program storage.
 */
typedef struct vxml_document_store_config_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const char *application_uri;
    size_t application_uri_size;
    size_t capacity;
    size_t max_uri_bytes;
    size_t max_document_bytes;
    size_t max_cache_bytes;
    vxml_limits voice_limits;
    const vxml_dialog_document_adapter_v1 *documents;
    void *document_user;
} vxml_document_store_config_v1;

typedef struct vxml_document_store {
    void *impl;
} vxml_document_store;

const char *vxml_document_store_status_string(
    vxml_document_store_status status);

vxml_document_store_status vxml_document_store_init(
    vxml_document_store *store,
    const vxml_document_store_config_v1 *config);

/**
 * Resolve reference relative to base_document_uri.
 *
 * When base_document_uri is empty, the copied application URI is used.
 * Absolute references replace the base. Network-path, absolute-path, relative
 * path, query-only, empty, and fragment-only references follow the RFC3986
 * hierarchical resolution model. Dot segments are removed from the path.
 *
 * The fetch URI and fragment are copied into caller-provided output buffers.
 * Failure publishes zero output sizes.
 */
vxml_document_store_status vxml_document_store_resolve(
    const vxml_document_store *store,
    const char *base_document_uri,
    size_t base_document_uri_size,
    const char *reference,
    size_t reference_size,
    vxml_resolved_uri_v1 *out);

/**
 * Acquire one immutable compiled document by normalized fetch URI.
 *
 * Cache hit increments a pin without provider I/O. Cache miss opens the
 * provider, copies source bytes, closes the provider exactly once, compiles
 * from owned bytes, and then publishes the entry atomically. If capacity or
 * byte budget requires eviction, only unpinned least-recently-used entries may
 * be evicted. Total simultaneous live borrows are bounded by cache capacity.
 */
vxml_document_store_status vxml_document_store_acquire(
    vxml_document_store *store,
    const char *document_uri,
    size_t document_uri_size,
    vxml_document_ref *out_ref,
    vxml_document_store_error *out_error);

/** Borrow one cached immutable view while ref remains live. */
vxml_document_store_status vxml_document_store_view(
    const vxml_document_store *store,
    vxml_document_ref ref,
    vxml_document_view *out_view);

/** Release one live borrow and clear ref. */
vxml_document_store_status vxml_document_store_release(
    vxml_document_store *store,
    vxml_document_ref *ref);

bool vxml_document_store_get_stats(
    const vxml_document_store *store,
    vxml_document_store_stats *out_stats);

/**
 * Evict every unpinned entry. Returns BUSY while any borrow is still live;
 * the store remains valid for release/retry.
 */
vxml_document_store_status vxml_document_store_clear(
    vxml_document_store *store);

/** Destroy only with no live borrows. */
vxml_document_store_status vxml_document_store_destroy(
    vxml_document_store *store);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_VOICEXML_DOCUMENT_STORE_H */
