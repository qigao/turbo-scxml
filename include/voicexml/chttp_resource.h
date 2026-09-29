#ifndef TURBO_VOICEXML_CHTTP_RESOURCE_H
#define TURBO_VOICEXML_CHTTP_RESOURCE_H

#include <scxml/chttp_resource.h>
#include <voicexml/resource.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VXML_CHTTP_RESOURCE_CONFIG_ABI_V1 1u

typedef enum vxml_chttp_resource_status {
    VXML_CHTTP_RESOURCE_OK = 0,
    VXML_CHTTP_RESOURCE_INVALID_ARGUMENT,
    VXML_CHTTP_RESOURCE_ALLOCATION_FAILED,
    VXML_CHTTP_RESOURCE_BUSY
} vxml_chttp_resource_status;

/**
 * Thin VoiceXML projection over one initialized TurboSCXML CHTTP resource.
 *
 * The underlying resource, its CHTTP client, resolver and policy remain owned
 * by the caller and must outlive this bridge. The bridge does not create a
 * second HTTP client, resolver, redirect policy, cache, or transport loop.
 */
typedef struct vxml_chttp_resource_config_v1 {
    uint32_t abi_version;
    size_t struct_size;
    scxml_chttp_resource *resource;
} vxml_chttp_resource_config_v1;

#define VXML_CHTTP_RESOURCE_CONFIG_V1_INIT     {VXML_CHTTP_RESOURCE_CONFIG_ABI_V1,      sizeof(vxml_chttp_resource_config_v1), NULL}

typedef struct vxml_chttp_resource {
    void *impl;
} vxml_chttp_resource;

const char *vxml_chttp_resource_status_string(
    vxml_chttp_resource_status status);

vxml_chttp_resource_status vxml_chttp_resource_init(
    vxml_chttp_resource *resource,
    const vxml_chttp_resource_config_v1 *config);

/**
 * Static document adapter. Pass the vxml_chttp_resource owner as adapter user.
 *
 * open delegates to scxml_chttp_resource_text_adapter(), preserving the
 * underlying resolver authorization, exact Content-Type admission, timeout,
 * response-byte bound, UTF-8 validation and response lease.
 */
const vxml_dialog_document_adapter_v1 *
vxml_chttp_resource_document_adapter(
    const vxml_chttp_resource *resource);

/**
 * Last underlying acquisition status.
 *
 * The value is observational only. It is set to SCXML_RESOURCE_OK after a
 * successful open and to the exact scxml_resource_status returned by the
 * underlying CHTTP text adapter after a failed open. close does not overwrite
 * the last acquisition result.
 */
bool vxml_chttp_resource_last_status(
    const vxml_chttp_resource *resource,
    scxml_resource_status *out_status);

/**
 * Release bridge bookkeeping. An active document lease returns BUSY and leaves
 * the bridge intact so the matching document close can complete first.
 *
 * The borrowed scxml_chttp_resource is never destroyed here.
 */
vxml_chttp_resource_status vxml_chttp_resource_destroy(
    vxml_chttp_resource *resource);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_VOICEXML_CHTTP_RESOURCE_H */
