#ifndef TURBO_VOICEXML_DATA_RESOURCE_H
#define TURBO_VOICEXML_DATA_RESOURCE_H

#include <voicexml/voicexml.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shared VoiceXML data-resource transport ABI.
 *
 * Historical vxml_cmeta_* names are intentionally retained for source and ABI
 * compatibility. The contract is provider-neutral and may be consumed by
 * CMeta and QuickJS profiles without linking the CMeta interpreter.
 */
#define VXML_CMETA_DATA_RESOURCE_ADAPTER_ABI_V1 1u
#define VXML_CMETA_DATA_REQUEST_ABI_V2 2u
#define VXML_CMETA_DATA_REQUEST_ABI_V3 3u

typedef enum vxml_cmeta_data_format {
    VXML_CMETA_DATA_JSON = 1,
    VXML_CMETA_DATA_YAML,
    VXML_CMETA_DATA_CSV,
    VXML_CMETA_DATA_XML
} vxml_cmeta_data_format;

typedef struct vxml_cmeta_data_resource_v1 {
    const void *data;
    size_t size;
    vxml_cmeta_data_format format;
    void *lease;
} vxml_cmeta_data_resource_v1;

typedef enum vxml_cmeta_data_fetch_hint {
    VXML_CMETA_DATA_FETCH_HINT_UNSPECIFIED = 0,
    VXML_CMETA_DATA_FETCH_HINT_PREFETCH,
    VXML_CMETA_DATA_FETCH_HINT_SAFE
} vxml_cmeta_data_fetch_hint;

typedef struct vxml_cmeta_data_request_v2 {
    uint32_t abi_version;
    size_t struct_size;
    const char *uri;
    size_t uri_size;
    size_t max_bytes;

    bool has_timeout;
    uint64_t timeout_us;
    vxml_cmeta_data_fetch_hint fetch_hint;
    bool has_max_age;
    uint64_t max_age_seconds;
    bool has_max_stale;
    uint64_t max_stale_seconds;
} vxml_cmeta_data_request_v2;

#define VXML_CMETA_DATA_REQUEST_V2_INIT     {VXML_CMETA_DATA_REQUEST_ABI_V2,      sizeof(vxml_cmeta_data_request_v2),      NULL, 0u, 0u, false, UINT64_C(0),      VXML_CMETA_DATA_FETCH_HINT_UNSPECIFIED,      false, UINT64_C(0), false, UINT64_C(0)}

typedef struct vxml_cmeta_data_field_v1 {
    const char *name;
    size_t name_size;
    const char *value;
    size_t value_size;
} vxml_cmeta_data_field_v1;

/*
 * Request V3 preserves the V2 policy prefix, then appends the dynamic request
 * surface. Enctype is ignored for GET. fields are callback-borrowed and keep
 * document/namelist order.
 */
typedef struct vxml_cmeta_data_request_v3 {
    uint32_t abi_version;
    size_t struct_size;
    const char *uri;
    size_t uri_size;
    size_t max_bytes;

    bool has_timeout;
    uint64_t timeout_us;
    vxml_cmeta_data_fetch_hint fetch_hint;
    bool has_max_age;
    uint64_t max_age_seconds;
    bool has_max_stale;
    uint64_t max_stale_seconds;

    vxml_submit_method method;
    vxml_submit_enctype enctype;
    const vxml_cmeta_data_field_v1 *fields;
    size_t field_count;
} vxml_cmeta_data_request_v3;

#define VXML_CMETA_DATA_REQUEST_V3_INIT     {VXML_CMETA_DATA_REQUEST_ABI_V3,      sizeof(vxml_cmeta_data_request_v3),      NULL, 0u, 0u, false, UINT64_C(0),      VXML_CMETA_DATA_FETCH_HINT_UNSPECIFIED,      false, UINT64_C(0), false, UINT64_C(0),      VXML_SUBMIT_METHOD_GET, VXML_SUBMIT_ENCTYPE_URLENCODED,      NULL, 0u}

typedef struct vxml_cmeta_data_resource_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    vxml_status (*open)(
        void *user,
        const char *uri, size_t uri_size,
        size_t max_bytes,
        vxml_cmeta_data_resource_v1 *out);
    void (*close)(
        void *user,
        vxml_cmeta_data_resource_v1 *resource);

    /*
     * Optional append-only policy-aware entry point.
     * An effective V2 policy must fail closed when this callback is absent.
     */
    vxml_status (*open_v2)(
        void *user,
        const vxml_cmeta_data_request_v2 *request,
        vxml_cmeta_data_resource_v1 *out);

    /*
     * Optional append-only dynamic request entry point. V3 is required when
     * method/namelist/enctype or a dynamic URI is used.
     */
    vxml_status (*open_v3)(
        void *user,
        const vxml_cmeta_data_request_v3 *request,
        vxml_cmeta_data_resource_v1 *out);
} vxml_cmeta_data_resource_adapter_v1;

#ifdef __cplusplus
}
#endif

#endif
