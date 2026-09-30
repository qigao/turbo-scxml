#ifndef TURBO_VOICEXML_SUBMIT_RESOURCE_H
#define TURBO_VOICEXML_SUBMIT_RESOURCE_H

#include <voicexml/document_store.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VXML_SUBMIT_RESOURCE_ADAPTER_ABI_V1 1u
#define VXML_SUBMIT_REQUEST_ABI_V1 1u
#define VXML_SUBMIT_WIRE_REQUEST_ABI_V1 1u

typedef enum vxml_submit_resource_status {
    VXML_SUBMIT_RESOURCE_OK = 0,
    VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT,
    VXML_SUBMIT_RESOURCE_ALLOCATION_FAILED,
    VXML_SUBMIT_RESOURCE_INVALID_URI,
    VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED,
    VXML_SUBMIT_RESOURCE_UNSUPPORTED_ENCODING,
    VXML_SUBMIT_RESOURCE_PROVIDER_ERROR,
    VXML_SUBMIT_RESOURCE_POSSIBLY_PROCESSED,
    VXML_SUBMIT_RESOURCE_INVALID_RESPONSE
} vxml_submit_resource_status;

typedef struct vxml_submit_field_v1 {
    const char *name;
    size_t name_size;
    const char *value;
    size_t value_size;
} vxml_submit_field_v1;

/**
 * Caller request before URI resolution and form encoding.
 *
 * Empty enctype means application/x-www-form-urlencoded for POST. Enctype is
 * ignored for GET, matching VoiceXML's POST-only enctype semantics.
 */
typedef struct vxml_submit_request_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const char *base_document_uri;
    size_t base_document_uri_size;
    const char *target;
    size_t target_size;
    vxml_submit_method method;
    const char *enctype;
    size_t enctype_size;
    const vxml_submit_field_v1 *fields;
    size_t field_count;
    size_t max_uri_bytes;
    size_t max_body_bytes;
    size_t max_response_bytes;
} vxml_submit_request_v1;

#define VXML_SUBMIT_REQUEST_V1_INIT \
    {VXML_SUBMIT_REQUEST_ABI_V1, sizeof(vxml_submit_request_v1), \
     NULL, 0u, NULL, 0u, VXML_SUBMIT_METHOD_GET, NULL, 0u, \
     NULL, 0u, 0u, 0u, 0u}

/** Callback-borrowed, already resolved/encoded one-attempt request. */
typedef struct vxml_submit_wire_request_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const char *uri;
    size_t uri_size;
    const char *fragment;
    size_t fragment_size;
    vxml_submit_method method;
    const char *content_type;
    size_t content_type_size;
    const void *body;
    size_t body_size;
} vxml_submit_wire_request_v1;

/** Provider-owned VoiceXML response source retained by lease. */
typedef struct vxml_submit_response {
    const void *data;
    size_t size;
    const char *media_type;
    size_t media_type_size;
    const char *effective_uri;
    size_t effective_uri_size;
    void *lease;
} vxml_submit_response;

/**
 * One synchronous outbound attempt.
 *
 * execute is invoked at most once by each vxml_submit_resource_execute call.
 * POST providers must return VXML_SUBMIT_RESOURCE_POSSIBLY_PROCESSED when a
 * failure occurs after the request may have reached the server. TurboSCXML
 * never converts that status into an automatic retry.
 */
typedef struct vxml_submit_resource_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    vxml_submit_resource_status (*execute)(
        void *user,
        const vxml_submit_wire_request_v1 *request,
        vxml_submit_response *out_response);
    void (*close)(void *user, vxml_submit_response *response);
} vxml_submit_resource_adapter_v1;

const char *vxml_submit_resource_status_string(
    vxml_submit_resource_status status);

vxml_submit_resource_status vxml_submit_resource_execute(
    const vxml_document_store *resolver,
    const vxml_submit_resource_adapter_v1 *adapter,
    void *adapter_user,
    const vxml_submit_request_v1 *request,
    vxml_submit_response *out_response);

vxml_submit_resource_status vxml_submit_resource_close(
    const vxml_submit_resource_adapter_v1 *adapter,
    void *adapter_user,
    vxml_submit_response *response);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_VOICEXML_SUBMIT_RESOURCE_H */
