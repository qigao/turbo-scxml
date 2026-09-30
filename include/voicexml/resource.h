#ifndef TURBO_VOICEXML_RESOURCE_H
#define TURBO_VOICEXML_RESOURCE_H

#include <voicexml/voicexml.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1 1u
#define VXML_DOCUMENT_FETCH_POLICY_ABI_V1 1u

/*
 * Shared status space for the delivered dialog/document boundary.
 *
 * The name is retained for source/ABI compatibility with the original
 * dialog-manager publication. DocumentStore and transport bridges use only
 * the document-related values; DialogManager owns the lifecycle/event values.
 */
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

/**
 * Provider-owned immutable VoiceXML source bytes.
 *
 * A successful open is paired with exactly one close after the consumer has
 * copied or compiled the bytes. The provider owns the backing lease.
 */
typedef struct vxml_dialog_document {
    const void *data;
    size_t size;
    void *lease;
} vxml_dialog_document;

/**
 * Per-request document fetch policy.
 *
 * The policy is callback-borrowed only. has_timeout distinguishes an absent
 * override from an explicit zero deadline. timeout_us is an exact integer
 * microsecond value; the provider must fail closed when it cannot honor it.
 */
typedef struct vxml_document_fetch_policy_v1 {
    uint32_t abi_version;
    size_t struct_size;
    bool has_timeout;
    uint64_t timeout_us;

    /*
     * Optional append-only wait-audio hint for VoiceXML document fetches.
     * Failure to retrieve/play this URI is non-fatal for the document fetch.
     */
    const char *fetchaudio_uri;
    size_t fetchaudio_uri_size;
} vxml_document_fetch_policy_v1;

#define VXML_DOCUMENT_FETCH_POLICY_V1_INIT \
    {VXML_DOCUMENT_FETCH_POLICY_ABI_V1, \
     sizeof(vxml_document_fetch_policy_v1), false, UINT64_C(0), NULL, 0u}

/**
 * Synchronous bounded VoiceXML document acquisition boundary.
 *
 * source/media_type are borrowed only for open. max_bytes is a hard caller
 * limit. Transport, authorization, redirects, deadlines, cache and media
 * policy belong to the provider.
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

    /**
     * Optional append-only per-request fetch-policy entry point.
     *
     * Providers that cannot honor an explicit policy must fail closed rather
     * than silently falling back to global/shared transport settings.
     */
    vxml_dialog_manager_status (*open_with_policy)(
        void *user,
        const char *source, size_t source_size,
        const char *media_type, size_t media_type_size,
        size_t max_bytes,
        const vxml_document_fetch_policy_v1 *policy,
        vxml_dialog_document *out_document);
} vxml_dialog_document_adapter_v1;

#ifdef __cplusplus
}
#endif

#endif /* TURBO_VOICEXML_RESOURCE_H */
