#ifndef TURBO_VOICEXML_RESOURCE_H
#define TURBO_VOICEXML_RESOURCE_H

#include <voicexml/voicexml.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1 1u

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
} vxml_dialog_document_adapter_v1;

#ifdef __cplusplus
}
#endif

#endif /* TURBO_VOICEXML_RESOURCE_H */
