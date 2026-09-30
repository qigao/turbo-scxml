#include <voicexml/chttp_resource.h>

#include <stdlib.h>
#include <string.h>

typedef struct vxml_chttp_resource_impl {
    scxml_chttp_resource *resource;
    const scxml_text_resource_adapter_v1 *text_adapter;
    scxml_text_resource active_text;
    bool active;
    scxml_resource_status last_status;
} vxml_chttp_resource_impl;

static vxml_dialog_manager_status document_open(
    void *user,
    const char *source, size_t source_size,
    const char *media_type, size_t media_type_size,
    size_t max_bytes,
    vxml_dialog_document *out_document) {
    static const char voice_media_type[] = "application/voicexml+xml";
    vxml_chttp_resource *owner = (vxml_chttp_resource *)user;
    vxml_chttp_resource_impl *impl = owner != NULL
        ? (vxml_chttp_resource_impl *)owner->impl : NULL;
    scxml_resource_status status;

    if (out_document != NULL)
        memset(out_document, 0, sizeof(*out_document));
    if (impl == NULL || out_document == NULL ||
        source == NULL || source_size == 0u ||
        media_type == NULL ||
        media_type_size != sizeof(voice_media_type) - 1u ||
        memcmp(media_type, voice_media_type, media_type_size) != 0 ||
        max_bytes == 0u || impl->active)
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;

    memset(&impl->active_text, 0, sizeof(impl->active_text));
    status = impl->text_adapter->open(
        impl->resource, source, source_size,
        max_bytes, &impl->active_text);
    impl->last_status = status;
    if (status != SCXML_RESOURCE_OK)
        return VXML_DIALOG_MANAGER_DOCUMENT_ERROR;

    if (impl->active_text.lease == NULL ||
        (impl->active_text.size != 0u &&
         impl->active_text.data == NULL)) {
        impl->text_adapter->close(
            impl->resource, &impl->active_text);
        impl->last_status = SCXML_RESOURCE_INVALID_DATA;
        return VXML_DIALOG_MANAGER_DOCUMENT_ERROR;
    }

    impl->active = true;
    *out_document = (vxml_dialog_document){
        .data = impl->active_text.data,
        .size = impl->active_text.size,
        .lease = impl};
    return VXML_DIALOG_MANAGER_OK;
}

static vxml_dialog_manager_status document_open_with_policy(
    void *user,
    const char *source, size_t source_size,
    const char *media_type, size_t media_type_size,
    size_t max_bytes,
    const vxml_document_fetch_policy_v1 *policy,
    vxml_dialog_document *out_document) {
    vxml_chttp_resource *owner = (vxml_chttp_resource *)user;
    vxml_chttp_resource_impl *impl = owner != NULL
        ? (vxml_chttp_resource_impl *)owner->impl : NULL;
    if (policy == NULL ||
        policy->abi_version != VXML_DOCUMENT_FETCH_POLICY_ABI_V1 ||
        policy->struct_size <
            offsetof(vxml_document_fetch_policy_v1, timeout_us) +
            sizeof(policy->timeout_us))
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;
    if (!policy->has_timeout)
        return document_open(
            user, source, source_size, media_type, media_type_size,
            max_bytes, out_document);
    if (out_document != NULL)
        memset(out_document, 0, sizeof(*out_document));
    if (impl != NULL)
        impl->last_status = SCXML_RESOURCE_FAILED;
    return VXML_DIALOG_MANAGER_DOCUMENT_ERROR;
}

static void document_close(
    void *user, vxml_dialog_document *document) {
    vxml_chttp_resource *owner = (vxml_chttp_resource *)user;
    vxml_chttp_resource_impl *impl = owner != NULL
        ? (vxml_chttp_resource_impl *)owner->impl : NULL;
    if (impl == NULL || document == NULL || !impl->active ||
        document->lease != impl)
        return;
    impl->text_adapter->close(
        impl->resource, &impl->active_text);
    impl->active = false;
    memset(document, 0, sizeof(*document));
}

static const vxml_dialog_document_adapter_v1 DOCUMENT_ADAPTER = {
    .abi_version = VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1,
    .struct_size = sizeof(vxml_dialog_document_adapter_v1),
    .open = document_open,
    .close = document_close,
    .open_with_policy = document_open_with_policy};

const char *vxml_chttp_resource_status_string(
    vxml_chttp_resource_status status) {
    switch (status) {
    case VXML_CHTTP_RESOURCE_OK:
        return "ok";
    case VXML_CHTTP_RESOURCE_INVALID_ARGUMENT:
        return "invalid_argument";
    case VXML_CHTTP_RESOURCE_ALLOCATION_FAILED:
        return "allocation_failed";
    case VXML_CHTTP_RESOURCE_BUSY:
        return "busy";
    default:
        return "unknown";
    }
}

vxml_chttp_resource_status vxml_chttp_resource_init(
    vxml_chttp_resource *resource,
    const vxml_chttp_resource_config_v1 *config) {
    vxml_chttp_resource_impl *impl;
    const scxml_text_resource_adapter_v1 *text_adapter;
    if (resource == NULL || resource->impl != NULL ||
        config == NULL ||
        config->abi_version != VXML_CHTTP_RESOURCE_CONFIG_ABI_V1 ||
        config->struct_size < sizeof(*config) ||
        config->resource == NULL ||
        config->resource->impl == NULL)
        return VXML_CHTTP_RESOURCE_INVALID_ARGUMENT;

    text_adapter =
        scxml_chttp_resource_text_adapter(config->resource);
    if (text_adapter == NULL ||
        text_adapter->abi_version !=
            SCXML_TEXT_RESOURCE_ADAPTER_ABI_V1 ||
        text_adapter->struct_size < sizeof(*text_adapter) ||
        text_adapter->open == NULL ||
        text_adapter->close == NULL)
        return VXML_CHTTP_RESOURCE_INVALID_ARGUMENT;

    impl = (vxml_chttp_resource_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return VXML_CHTTP_RESOURCE_ALLOCATION_FAILED;
    impl->resource = config->resource;
    impl->text_adapter = text_adapter;
    impl->last_status = SCXML_RESOURCE_OK;
    resource->impl = impl;
    return VXML_CHTTP_RESOURCE_OK;
}

const vxml_dialog_document_adapter_v1 *
vxml_chttp_resource_document_adapter(
    const vxml_chttp_resource *resource) {
    return resource != NULL && resource->impl != NULL
        ? &DOCUMENT_ADAPTER : NULL;
}

bool vxml_chttp_resource_last_status(
    const vxml_chttp_resource *resource,
    scxml_resource_status *out_status) {
    const vxml_chttp_resource_impl *impl =
        resource != NULL
            ? (const vxml_chttp_resource_impl *)resource->impl
            : NULL;
    if (impl == NULL || out_status == NULL)
        return false;
    *out_status = impl->last_status;
    return true;
}

vxml_chttp_resource_status vxml_chttp_resource_destroy(
    vxml_chttp_resource *resource) {
    vxml_chttp_resource_impl *impl;
    if (resource == NULL)
        return VXML_CHTTP_RESOURCE_INVALID_ARGUMENT;
    impl = (vxml_chttp_resource_impl *)resource->impl;
    if (impl == NULL)
        return VXML_CHTTP_RESOURCE_OK;
    if (impl->active)
        return VXML_CHTTP_RESOURCE_BUSY;
    free(impl);
    resource->impl = NULL;
    return VXML_CHTTP_RESOURCE_OK;
}
