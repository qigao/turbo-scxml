#ifndef TURBO_VOICEXML_SCRIPT_RESOURCE_H
#define TURBO_VOICEXML_SCRIPT_RESOURCE_H

#include <voicexml/document_store.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VXML_SCRIPT_RESOURCE_ADAPTER_ABI_V1 1u
#define VXML_SCRIPT_REQUEST_ABI_V1 1u

typedef enum vxml_script_resource_status {
    VXML_SCRIPT_RESOURCE_OK = 0,
    VXML_SCRIPT_RESOURCE_INVALID_ARGUMENT,
    VXML_SCRIPT_RESOURCE_ALLOCATION_FAILED,
    VXML_SCRIPT_RESOURCE_INVALID_URI,
    VXML_SCRIPT_RESOURCE_UNSUPPORTED_CHARSET,
    VXML_SCRIPT_RESOURCE_LIMIT_EXCEEDED,
    VXML_SCRIPT_RESOURCE_PROVIDER_ERROR,
    VXML_SCRIPT_RESOURCE_INVALID_DATA
} vxml_script_resource_status;

/**
 * Provider-owned immutable decoded script source.
 *
 * A successful provider open must publish one non-NULL lease. The source
 * bytes are borrowed until vxml_script_resource_close() pairs the lease with
 * exactly one provider close.
 */
typedef struct vxml_script_source {
    const void *data;
    size_t size;
    void *lease;
} vxml_script_source;

/**
 * Transport-neutral script provider.
 *
 * resolved_uri and charset are callback-borrowed. The provider returns decoded
 * text bytes for the requested charset and retains ownership through lease.
 */
typedef struct vxml_script_resource_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    vxml_script_resource_status (*open)(
        void *user,
        const char *resolved_uri,
        size_t resolved_uri_size,
        const char *charset,
        size_t charset_size,
        size_t max_bytes,
        vxml_script_source *out_source);
    void (*close)(void *user, vxml_script_source *source);
} vxml_script_resource_adapter_v1;

/**
 * One bounded external-script acquisition request.
 *
 * reference is resolved relative to base_document_uri with the same RFC3986
 * normalization used by VoiceXMLDocumentStore. Empty charset means UTF-8.
 */
typedef struct vxml_script_request_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const char *base_document_uri;
    size_t base_document_uri_size;
    const char *reference;
    size_t reference_size;
    const char *charset;
    size_t charset_size;
    size_t max_uri_bytes;
    size_t max_source_bytes;
} vxml_script_request_v1;

#define VXML_SCRIPT_REQUEST_V1_INIT \
    {VXML_SCRIPT_REQUEST_ABI_V1, sizeof(vxml_script_request_v1), \
     NULL, 0u, NULL, 0u, NULL, 0u, 0u, 0u}

const char *vxml_script_resource_status_string(
    vxml_script_resource_status status);

/**
 * Resolve and open one external script.
 *
 * The resolver Store is borrowed only for this call; this function does not
 * cache script bytes. On failure out_source is empty. URI fragments are
 * rejected because script acquisition fetches a complete source resource.
 */
vxml_script_resource_status vxml_script_resource_acquire(
    const vxml_document_store *resolver,
    const vxml_script_resource_adapter_v1 *adapter,
    void *adapter_user,
    const vxml_script_request_v1 *request,
    vxml_script_source *out_source);

/**
 * Close one live script lease. An already-empty source is a no-op success.
 */
vxml_script_resource_status vxml_script_resource_close(
    const vxml_script_resource_adapter_v1 *adapter,
    void *adapter_user,
    vxml_script_source *source);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_VOICEXML_SCRIPT_RESOURCE_H */
