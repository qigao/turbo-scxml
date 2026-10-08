#include <scxml/provider.h>
#include <salts/component_plugin_abi.h>
#include <salts/plugin_decl.h>

#include <string.h>

/* Each separately loaded DSO owns its counter and its callback code. The test
 * reads the counter only while holding the generation's Component scope. */
cmeta_component(ScxmlDsoFixture, cmeta_provides(scxml_event_io_provider));

static int marker = SCXML_COMPONENT_DSO_MARKER;

static void ticket_commit(void *user) {
    ++*(int *)user;
}

static void ticket_discard(void *user) {
    *(int *)user += 1000;
}

static scxml_adapter_status prepare_send(
    void *self, const scxml_send_request *request,
    cflow_statechart_effect_ticket *ticket, const char **error) {
    if (self == NULL || request == NULL || ticket == NULL || error == NULL ||
        request->event_size != 5u ||
        memcmp(request->event, "probe", 5u) != 0)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    *error = NULL;
    *ticket = (cflow_statechart_effect_ticket){
        ticket_commit, ticket_discard, self};
    return SCXML_ADAPTER_ACCEPTED;
}

static void close_provider(void *self) {
    *(int *)self += 10;
}

static bool is_quiescent(void *self) {
    return self != NULL;
}

CMETA_IMPLEMENTS(scxml_event_io_provider, event_impl,
    SCXML_EVENT_IO_CAP_SEND,
    .prepare_send = prepare_send,
    .close = close_provider,
    .is_quiescent = is_quiescent);

static cmeta_status project(
    void *context, const cmeta_object_ref *object,
    const cmeta_interface_desc *expected, cmeta_interface_projection *out) {
    (void)context;
    if (object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (!cmeta_interface_desc_equal(
            expected, scxml_event_io_provider_interface()))
        return CMETA_TRAIT_MISSING;
    *out = (cmeta_interface_projection){
        sizeof(*out), scxml_event_io_provider_interface(),
        object->object, &event_impl_vtable};
    return CMETA_OK;
}

static const cmeta_object_interface_provider interfaces = {
    sizeof(cmeta_object_interface_provider), NULL, project};

static cmeta_status SALTS_COMPONENT_CALL create(
    void *context, const cmeta_data_desc *config_data, const void *config_value,
    const salts_component_dependency *dependencies, size_t dependency_count,
    cmeta_object_ref *out) {
    (void)dependencies;
    if (context == NULL || config_data != NULL || config_value != NULL ||
        dependency_count != 0u)
        return CMETA_INVALID_ARGUMENT;
    return cmeta_object_borrow(out, context, &cmeta_data_int, NULL);
}

static const salts_component_provider_binding binding = {
    sizeof(salts_component_provider_binding),
    SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION,
    cmeta_component_meta(ScxmlDsoFixture), &marker, &interfaces,
    create, NULL, NULL};

static const salts_component_provider_binding *get_binding(void *self) {
    return (const salts_component_provider_binding *)self;
}

CMETA_IMPLEMENTS(salts_component_provider, provider_impl, 0u,
    .get_binding = get_binding);

static salts_component_provider provider = {
    (void *)&binding, &provider_impl_vtable};

#define SCXML_COMPONENT_DSO_EXPORTS(X) \
    X(interface, (salts_component_provider, &provider), "component-provider", \
      SALTS_COMPONENT_PROVIDER_CONTRACT_ID, \
      SALTS_COMPONENT_PROVIDER_CONTRACT_VERSION, 0)

CMETA_PLUGIN_DECLARE(scxml_component_fixture,
    SCXML_COMPONENT_DSO_ID, (1,0,0), SCXML_COMPONENT_DSO_EXPORTS,
    CMETA_PLUGIN_PASSIVE());
