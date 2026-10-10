#include <scxml/provider.h>
#include <salts/component_plugin_abi.h>
#include <salts/plugin_decl.h>

#include <string.h>

/* A genuine code-generation-specific DSO, not an executable-owned callback.
 * The caller borrows this provider only through its Component generation
 * Scope, and the dialog manager owns the document lease close ordering. */
cmeta_component(VoiceComponentDsoFixture,
    cmeta_provides(scxml_text_resource_provider));

static int marker = VOICE_COMPONENT_DSO_MARKER;
static bool document_open = false;
static const char voice_document[] =
    "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
    "<form id='main'><block><exit/></block></form></vxml>";
static const char voice_uri[] = "mem:voice-dso";

static scxml_resource_status open_document(
    void *self, const char *uri, size_t uri_size, size_t max_bytes,
    scxml_text_resource *out) {
    int *state = (int *)self;
    if (state != &marker || uri == NULL || out == NULL ||
        uri_size != sizeof(voice_uri) - 1u ||
        memcmp(uri, voice_uri, sizeof(voice_uri) - 1u) != 0 ||
        sizeof(voice_document) - 1u > max_bytes || document_open)
        return SCXML_RESOURCE_FAILED;
    document_open = true;
    ++marker;
    *out = (scxml_text_resource){
        .data = voice_document,
        .size = sizeof(voice_document) - 1u,
        .lease = self
    };
    return SCXML_RESOURCE_OK;
}

static void close_document(void *self, scxml_text_resource *resource) {
    if (self != &marker || resource == NULL ||
        resource->lease != &marker || !document_open)
        return;
    document_open = false;
    marker += 10;
    *resource = (scxml_text_resource){0};
}

CMETA_IMPLEMENTS(scxml_text_resource_provider, text_resource_impl, 0u,
    .open = open_document, .close = close_document);

static cmeta_status project(
    void *context, const cmeta_object_ref *object,
    const cmeta_interface_desc *expected,
    cmeta_interface_projection *out) {
    (void)context;
    if (object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (!cmeta_interface_desc_equal(
            expected, scxml_text_resource_provider_interface()))
        return CMETA_TRAIT_MISSING;
    *out = (cmeta_interface_projection){
        sizeof(*out), scxml_text_resource_provider_interface(),
        object->object, &text_resource_impl_vtable
    };
    return CMETA_OK;
}

static const cmeta_object_interface_provider interfaces = {
    sizeof(cmeta_object_interface_provider), NULL, project
};

static cmeta_status SALTS_COMPONENT_CALL create(
    void *context, const cmeta_data_desc *config_data,
    const void *config_value,
    const salts_component_dependency *dependencies,
    size_t dependency_count, cmeta_object_ref *out) {
    (void)dependencies;
    if (context != &marker || config_data != NULL ||
        config_value != NULL || dependency_count != 0u || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    return cmeta_object_borrow(out, context, &cmeta_data_int, NULL);
}

static const salts_component_provider_binding binding = {
    sizeof(salts_component_provider_binding),
    SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION,
    cmeta_component_meta(VoiceComponentDsoFixture),
    &marker, &interfaces, create, NULL, NULL
};

static const salts_component_provider_binding *get_binding(void *self) {
    return (const salts_component_provider_binding *)self;
}

CMETA_IMPLEMENTS(salts_component_provider, provider_impl, 0u,
    .get_binding = get_binding);

static salts_component_provider provider = {
    (void *)&binding, &provider_impl_vtable
};

#define VOICE_DSO_EXPORTS(X) \
    X(interface, (salts_component_provider, &provider), "component-provider", \
      SALTS_COMPONENT_PROVIDER_CONTRACT_ID, \
      SALTS_COMPONENT_PROVIDER_CONTRACT_VERSION, 0)

CMETA_PLUGIN_DECLARE(voice_component_resource_fixture,
    VOICE_COMPONENT_DSO_ID, (1,0,0), VOICE_DSO_EXPORTS,
    CMETA_PLUGIN_PASSIVE());
