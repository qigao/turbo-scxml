#include <voicexml/script_resource.h>

#include "voicexml_internal.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static bool ascii_case_equal(
    const char *data, size_t size, const char *literal) {
    size_t index;
    const size_t literal_size = literal != NULL ? strlen(literal) : 0u;
    if (data == NULL || size != literal_size)
        return false;
    for (index = 0u; index < size; ++index) {
        unsigned char left = (unsigned char)data[index];
        unsigned char right = (unsigned char)literal[index];
        if (left >= 'A' && left <= 'Z') left = (unsigned char)(left + 32u);
        if (right >= 'A' && right <= 'Z') right = (unsigned char)(right + 32u);
        if (left != right) return false;
    }
    return true;
}

static bool adapter_valid(
    const vxml_script_resource_adapter_v1 *adapter) {
    const size_t prefix =
        offsetof(vxml_script_resource_adapter_v1, close) +
        sizeof(adapter->close);
    return adapter != NULL &&
        adapter->abi_version == VXML_SCRIPT_RESOURCE_ADAPTER_ABI_V1 &&
        adapter->struct_size >= prefix &&
        adapter->open != NULL &&
        adapter->close != NULL;
}

static void provider_close_if_live(
    const vxml_script_resource_adapter_v1 *adapter,
    void *user,
    vxml_script_source *source) {
    if (adapter != NULL && adapter->close != NULL &&
        source != NULL && source->lease != NULL)
        adapter->close(user, source);
    if (source != NULL)
        *source = (vxml_script_source){0};
}

vxml_status vxml_compile_external_script_profile(
    const void *bytes, size_t size,
    const vxml_limits *limits,
    vxml_program *out,
    vxml_diagnostic *diagnostic) {
    return vxml_compile_with_features(
        bytes, size, limits,
        VXML_COMPILE_FEATURE_EXTERNAL_SCRIPT,
        out, diagnostic);
}

const char *vxml_script_resource_status_string(
    vxml_script_resource_status status) {
    switch (status) {
    case VXML_SCRIPT_RESOURCE_OK:
        return "ok";
    case VXML_SCRIPT_RESOURCE_INVALID_ARGUMENT:
        return "invalid_argument";
    case VXML_SCRIPT_RESOURCE_ALLOCATION_FAILED:
        return "allocation_failed";
    case VXML_SCRIPT_RESOURCE_INVALID_URI:
        return "invalid_uri";
    case VXML_SCRIPT_RESOURCE_UNSUPPORTED_CHARSET:
        return "unsupported_charset";
    case VXML_SCRIPT_RESOURCE_LIMIT_EXCEEDED:
        return "limit_exceeded";
    case VXML_SCRIPT_RESOURCE_PROVIDER_ERROR:
        return "provider_error";
    case VXML_SCRIPT_RESOURCE_INVALID_DATA:
        return "invalid_data";
    default:
        return "unknown";
    }
}

const char *vxml_script_resource_failure_event(
    vxml_script_resource_status status,
    size_t *out_event_size) {
    static const char badfetch[] = "error.badfetch";
    static const char unsupported_format[] = "error.unsupported.format";
    static const char noresource[] = "error.noresource";

    if (out_event_size != NULL)
        *out_event_size = 0u;
    switch (status) {
    case VXML_SCRIPT_RESOURCE_INVALID_URI:
    case VXML_SCRIPT_RESOURCE_LIMIT_EXCEEDED:
    case VXML_SCRIPT_RESOURCE_PROVIDER_ERROR:
    case VXML_SCRIPT_RESOURCE_INVALID_DATA:
        if (out_event_size != NULL)
            *out_event_size = sizeof(badfetch) - 1u;
        return badfetch;
    case VXML_SCRIPT_RESOURCE_UNSUPPORTED_CHARSET:
        if (out_event_size != NULL)
            *out_event_size = sizeof(unsupported_format) - 1u;
        return unsupported_format;
    case VXML_SCRIPT_RESOURCE_ALLOCATION_FAILED:
        if (out_event_size != NULL)
            *out_event_size = sizeof(noresource) - 1u;
        return noresource;
    case VXML_SCRIPT_RESOURCE_OK:
    case VXML_SCRIPT_RESOURCE_INVALID_ARGUMENT:
    default:
        return NULL;
    }
}

vxml_script_resource_status vxml_script_resource_acquire(
    const vxml_document_store *resolver,
    const vxml_script_resource_adapter_v1 *adapter,
    void *adapter_user,
    const vxml_script_request_v1 *request,
    vxml_script_source *out_source) {
    static const char utf8[] = "UTF-8";
    const char *charset;
    size_t charset_size;
    char *uri = NULL;
    char *fragment = NULL;
    vxml_resolved_uri_v1 resolved = VXML_RESOLVED_URI_V1_INIT;
    vxml_script_source source = {0};
    vxml_script_resource_status provider_status;
    vxml_document_store_status resolve_status;

    if (out_source != NULL)
        *out_source = (vxml_script_source){0};
    if (resolver == NULL || resolver->impl == NULL ||
        !adapter_valid(adapter) ||
        request == NULL || out_source == NULL ||
        request->abi_version != VXML_SCRIPT_REQUEST_ABI_V1 ||
        request->struct_size < sizeof(*request) ||
        request->reference == NULL ||
        request->reference_size == 0u ||
        memchr(request->reference, '\0', request->reference_size) != NULL ||
        (request->base_document_uri_size != 0u &&
         (request->base_document_uri == NULL ||
          memchr(
              request->base_document_uri, '\0',
              request->base_document_uri_size) != NULL)) ||
        (request->charset_size != 0u &&
         (request->charset == NULL ||
          memchr(request->charset, '\0', request->charset_size) != NULL)) ||
        request->max_uri_bytes == 0u ||
        request->max_uri_bytes == SIZE_MAX ||
        request->max_source_bytes == 0u)
        return VXML_SCRIPT_RESOURCE_INVALID_ARGUMENT;

    charset = request->charset_size != 0u
        ? request->charset : utf8;
    charset_size = request->charset_size != 0u
        ? request->charset_size : sizeof(utf8) - 1u;
    if (!ascii_case_equal(charset, charset_size, utf8))
        return VXML_SCRIPT_RESOURCE_UNSUPPORTED_CHARSET;

    uri = (char *)malloc(request->max_uri_bytes + 1u);
    fragment = (char *)malloc(request->max_uri_bytes + 1u);
    if (uri == NULL || fragment == NULL) {
        free(fragment);
        free(uri);
        return VXML_SCRIPT_RESOURCE_ALLOCATION_FAILED;
    }
    resolved.document_uri = uri;
    resolved.document_uri_capacity = request->max_uri_bytes + 1u;
    resolved.fragment = fragment;
    resolved.fragment_capacity = request->max_uri_bytes + 1u;
    resolve_status = vxml_document_store_resolve(
        resolver,
        request->base_document_uri,
        request->base_document_uri_size,
        request->reference,
        request->reference_size,
        &resolved);
    if (resolve_status != VXML_DOCUMENT_STORE_OK ||
        resolved.document_uri_size == 0u ||
        resolved.fragment_size != 0u) {
        free(fragment);
        free(uri);
        return resolve_status == VXML_DOCUMENT_STORE_LIMIT_EXCEEDED
            ? VXML_SCRIPT_RESOURCE_LIMIT_EXCEEDED
            : VXML_SCRIPT_RESOURCE_INVALID_URI;
    }

    provider_status = adapter->open(
        adapter_user,
        resolved.document_uri,
        resolved.document_uri_size,
        charset,
        charset_size,
        request->max_source_bytes,
        &source);
    free(fragment);
    free(uri);

    if (provider_status != VXML_SCRIPT_RESOURCE_OK) {
        provider_close_if_live(adapter, adapter_user, &source);
        return provider_status;
    }
    if (source.lease == NULL ||
        (source.size != 0u && source.data == NULL)) {
        provider_close_if_live(adapter, adapter_user, &source);
        return VXML_SCRIPT_RESOURCE_INVALID_DATA;
    }
    if (source.size > request->max_source_bytes) {
        provider_close_if_live(adapter, adapter_user, &source);
        return VXML_SCRIPT_RESOURCE_LIMIT_EXCEEDED;
    }

    *out_source = source;
    return VXML_SCRIPT_RESOURCE_OK;
}

vxml_script_resource_status vxml_script_resource_close(
    const vxml_script_resource_adapter_v1 *adapter,
    void *adapter_user,
    vxml_script_source *source) {
    if (!adapter_valid(adapter) || source == NULL)
        return VXML_SCRIPT_RESOURCE_INVALID_ARGUMENT;
    if (source->lease == NULL) {
        if (source->data != NULL || source->size != 0u)
            return VXML_SCRIPT_RESOURCE_INVALID_DATA;
        return VXML_SCRIPT_RESOURCE_OK;
    }
    adapter->close(adapter_user, source);
    *source = (vxml_script_source){0};
    return VXML_SCRIPT_RESOURCE_OK;
}
