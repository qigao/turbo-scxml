#ifndef SCXML_CHTTP_RESOURCE_H
#define SCXML_CHTTP_RESOURCE_H

#include <http_client/http.h>
#include <scxml/scxml.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCXML_CHTTP_RESOURCE_CONFIG_ABI_V1 1u
#define SCXML_CHTTP_RESOURCE_CONFIG_ABI_V2 2u
#define SCXML_CHTTP_RESOLUTION_ABI_V1 1u
#define SCXML_CHTTP_DATA_DECODER_ABI_V1 1u

typedef enum scxml_chttp_resource_kind {
    SCXML_CHTTP_RESOURCE_DATA = 1,
    SCXML_CHTTP_RESOURCE_TEXT
} scxml_chttp_resource_kind;

/**
 * One authorized mapping from a logical SCXML URI to an HTTP/1 request.
 *
 * Every byte view must remain valid until the generic resource `open` call
 * returns. The adapter copies each view into bounded NUL-terminated storage
 * before entering CHTTP. `media_type` is the exact response Content-Type
 * admitted for this mapping.
 */
typedef struct scxml_chttp_resolution_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const char *connection_uri;
    size_t connection_uri_size;
    const char *authority;
    size_t authority_size;
    const char *target;
    size_t target_size;
    const char *media_type;
    size_t media_type_size;
} scxml_chttp_resolution_v1;

/**
 * Compatibility-only decoder for the legacy V1 resource profile.
 *
 * New CHTTP resource providers should use config/resolver V2 so the provider
 * returns raw response bytes plus an explicit DataBindFormat and never owns a
 * CSerde reader or observes a CMeta destination descriptor.
 */
typedef struct scxml_chttp_data_decoder_v1 {
    uint32_t abi_version;
    size_t struct_size;
    scxml_resource_status (*open)(
        void *user, const void *body, size_t body_size,
        const cmeta_data_desc *expected, scxml_data_resource *out);
    void (*close)(void *user, scxml_data_resource *resource);
} scxml_chttp_data_decoder_v1;

/**
 * Authorize one logical URI and select its immutable transport mapping.
 *
 * Returning anything other than `SCXML_RESOURCE_OK` must leave both outputs
 * unused and prevents network admission. Data requests require one valid
 * decoder; text requests ignore the decoder outputs. Resolver output is
 * copied immediately after the callback returns. The returned byte views must
 * therefore remain valid until the enclosing generic resource `open` returns.
 * A returned decoder operation table is copied immediately; its user pointer
 * remains borrowed until the matching data-resource close.
 */
typedef scxml_resource_status (*scxml_chttp_resolve_fn)(
    void *user, const char *uri, size_t uri_size,
    scxml_chttp_resource_kind kind,
    scxml_chttp_resolution_v1 *out_resolution,
    const scxml_chttp_data_decoder_v1 **out_decoder,
    void **out_decoder_user);

/**
 * Canonical resolver for CHTTP resource V2.
 *
 * For DATA requests, `out_format` must receive one explicit DataBindFormat.
 * For TEXT requests the format result is ignored. No MIME/extension inference,
 * parser registry lookup or fallback occurs in TurboSCXML.
 */
typedef scxml_resource_status (*scxml_chttp_resolve_v2_fn)(
    void *user, const char *uri, size_t uri_size,
    scxml_chttp_resource_kind kind,
    scxml_chttp_resolution_v1 *out_resolution,
    DataBindFormat *out_format);

/**
 * Fixed policy for one single-owner blocking CHTTP adapter.
 *
 * `client`, resolver operations, and resolver user are borrowed until adapter
 * destruction. The same owner must not be called concurrently or shared by
 * multiple session executors. Endpoint limits are positive hard admission
 * bounds. `timeout_ms` is the CHTTP HTTP-result wait deadline; terminal
 * cancellation/drain can extend the blocking call after that deadline.
 * `max_response_body_bytes` is an adapter acceptance ceiling checked after
 * CHTTP returns. The borrowed client must therefore have been initialized
 * with positive connect/read/write deadlines and a hard response-body bound
 * no greater than this value. CHTTP currently exposes no client-config
 * introspection, so satisfying that transport precondition remains the
 * caller's responsibility.
 */
typedef struct scxml_chttp_resource_config_v1 {
    uint32_t abi_version;
    size_t struct_size;
    chttp_client *client;
    scxml_chttp_resolve_fn resolve;
    void *resolver_user;
    uint32_t timeout_ms;
    size_t max_connection_uri_bytes;
    size_t max_authority_bytes;
    size_t max_target_bytes;
    size_t max_media_type_bytes;
    size_t max_response_body_bytes;
} scxml_chttp_resource_config_v1;

/**
 * Canonical V2 policy. Limits and client ownership match V1; only resolver
 * semantics change. DATA resolution selects DataBindFormat explicitly and the
 * returned generic adapter exposes response bytes without a decoder layer.
 */
typedef struct scxml_chttp_resource_config_v2 {
    uint32_t abi_version;
    size_t struct_size;
    chttp_client *client;
    scxml_chttp_resolve_v2_fn resolve;
    void *resolver_user;
    uint32_t timeout_ms;
    size_t max_connection_uri_bytes;
    size_t max_authority_bytes;
    size_t max_target_bytes;
    size_t max_media_type_bytes;
    size_t max_response_body_bytes;
} scxml_chttp_resource_config_v2;

/** Opaque single-owner adapter. Zero initialization is required. */
typedef struct scxml_chttp_resource {
    void *impl;
} scxml_chttp_resource;

/**
 * Initialize the legacy V1 decoder profile without taking ownership of the
 * configured CHTTP client.
 */
scxml_status scxml_chttp_resource_init(
    scxml_chttp_resource *resource,
    const scxml_chttp_resource_config_v1 *config);

/**
 * Initialize the canonical V2 raw-resource profile without taking ownership of
 * the configured CHTTP client.
 */
scxml_status scxml_chttp_resource_init_v2(
    scxml_chttp_resource *resource,
    const scxml_chttp_resource_config_v2 *config);

/** Return the compatibility V1 CSerde-returning data adapter. */
const scxml_data_resource_adapter_v1 *
scxml_chttp_resource_data_adapter(const scxml_chttp_resource *resource);

/**
 * Return the canonical V2 raw-bytes + DataBindFormat data adapter.
 * Pass `resource` as its user value.
 */
const scxml_data_resource_adapter_v2 *
scxml_chttp_resource_data_adapter_v2(const scxml_chttp_resource *resource);

/**
 * Return the generic compile-time text adapter. Pass `resource` as its user
 * value. Text open rejects invalid UTF-8 before publishing a lease.
 */
const scxml_text_resource_adapter_v1 *
scxml_chttp_resource_text_adapter(const scxml_chttp_resource *resource);

/**
 * Release adapter-owned policy storage. An active unclosed lease returns
 * `SCXML_INVALID_ARGUMENT` and leaves the owner intact for a later retry.
 */
scxml_status scxml_chttp_resource_destroy(scxml_chttp_resource *resource);

#ifdef __cplusplus
}
#endif

#endif /* SCXML_CHTTP_RESOURCE_H */
