#include <scxml/component.h>
#include <voicexml/dialog_manager.h>
#include <cflow/actor.h>
#include <salts/clock.h>
#include <salts/thread.h>
#include <tinytest.h>

#include <stdatomic.h>
#include <string.h>

/* P3 conformance fixture: the resource Interface is the existing canonical
 * scxml_text_resource_provider, projected from one Salts Component generation.
 * The VoiceXML DialogManager and CCXML ticket semantics remain authoritative.
 * There is no new provider registry, DSO lease or runtime/Actor in this test. */
#define VOICE_STATIC_COMPONENT "VoiceComponentResourceFixture"

cmeta_component(VoiceComponentResourceFixture,
    cmeta_provides(scxml_text_resource_provider));

static const char voice_document[] =
    "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
    "<form id='main'><block><exit/></block></form></vxml>";

typedef struct voice_resource_state {
    int anchor;
    const char *uri;
    size_t uri_size;
    size_t open_count;
    size_t close_count;
    size_t outstanding;
    cmeta_object_interface_provider interfaces;
    salts_component_provider_binding binding;
    salts_component_deployment deployment;
} voice_resource_state;

typedef struct voice_generation {
    salts_component_plugin_generation generation;
    salts_component_deployment deployments[1];
    salts_component_instance instances[1];
    salts_component_dependency dependencies[1];
    size_t activation_order[1];
    salts_component_plugin_module modules[1];
} voice_generation;

typedef struct voice_resource_borrow {
    scxml_component_scope *scope; /* borrowed, address-stable */
    uint64_t generation_id;
    scxml_text_resource_provider provider;
    scxml_text_resource text;
    bool active;
} voice_resource_borrow;

typedef struct voice_events {
    char names[6][48];
    char dialog_ids[6][48];
    size_t count;
} voice_events;

typedef struct voice_telephony {
    size_t close_count;
    bool quiescent;
} voice_telephony;

static scxml_resource_status voice_resource_open(
    void *user, const char *uri, size_t uri_size, size_t max_bytes,
    scxml_text_resource *out) {
    voice_resource_state *state = (voice_resource_state *)user;
    if (state == NULL || uri == NULL || out == NULL ||
        uri_size != state->uri_size ||
        memcmp(uri, state->uri, uri_size) != 0 ||
        sizeof(voice_document) - 1u > max_bytes || state->outstanding != 0u)
        return SCXML_RESOURCE_FAILED;
    ++state->open_count;
    ++state->outstanding;
    *out = (scxml_text_resource){
        .data = voice_document,
        .size = sizeof(voice_document) - 1u,
        .lease = state
    };
    return SCXML_RESOURCE_OK;
}

static void voice_resource_close(void *user, scxml_text_resource *text) {
    voice_resource_state *state = (voice_resource_state *)user;
    if (state == NULL || text == NULL ||
        text->lease != state || state->outstanding == 0u)
        return;
    --state->outstanding;
    ++state->close_count;
    *text = (scxml_text_resource){0};
}

static const scxml_text_resource_provider_vtable resource_vtable = {
    .implementation = "voice-component-document",
    .capabilities = 0u,
    .open = voice_resource_open,
    .close = voice_resource_close
};

static cmeta_status voice_component_project(
    void *context, const cmeta_object_ref *object,
    const cmeta_interface_desc *expected,
    cmeta_interface_projection *out) {
    voice_resource_state *state = (voice_resource_state *)context;
    if (state == NULL || object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (!cmeta_interface_desc_equal(
            expected, scxml_text_resource_provider_interface()))
        return CMETA_TRAIT_MISSING;
    *out = (cmeta_interface_projection){
        sizeof(*out), scxml_text_resource_provider_interface(),
        state, &resource_vtable
    };
    return CMETA_OK;
}

static cmeta_status SALTS_COMPONENT_CALL voice_component_create(
    void *context, const cmeta_data_desc *config_desc,
    const void *config_value, const salts_component_dependency *dependencies,
    size_t dependency_count, cmeta_object_ref *out) {
    voice_resource_state *state = (voice_resource_state *)context;
    (void)dependencies;
    if (state == NULL || config_desc != NULL || config_value != NULL ||
        dependency_count != 0u || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    return cmeta_object_borrow(
        out, &state->anchor, &cmeta_data_int, NULL);
}

static void voice_resource_init(
    voice_resource_state *state, int anchor, const char *uri) {
    memset(state, 0, sizeof(*state));
    state->anchor = anchor;
    state->uri = uri;
    state->uri_size = strlen(uri);
    state->interfaces = (cmeta_object_interface_provider){
        sizeof(cmeta_object_interface_provider),
        state, voice_component_project
    };
    state->binding = (salts_component_provider_binding){
        sizeof(salts_component_provider_binding),
        SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION,
        cmeta_component_meta(VoiceComponentResourceFixture),
        state, &state->interfaces, voice_component_create,
        NULL, NULL
    };
    state->deployment = (salts_component_deployment){
        &state->binding, NULL, NULL
    };
}

static salts_component_plugin_status voice_generation_build(
    voice_generation *g, voice_resource_state *state, uint64_t generation_id) {
    const salts_component_plugin_generation_storage storage = {
        g->deployments, 1u, g->instances, 1u, g->dependencies, 1u,
        g->activation_order, 1u, g->modules, 1u
    };
    memset(g, 0, sizeof(*g));
    g->deployments[0] = state->deployment;
    return salts_component_plugin_generation_build(
        &g->generation, generation_id, NULL, &storage,
        g->deployments, 1u, NULL, 0u, NULL, 0u);
}

/* This is one borrowed canonical CMeta ObjectRef/Interface projection. The
 * outer Component Scope remains live until the DialogManager is destroyed. */
static bool voice_resource_bind(
    voice_resource_borrow *borrow, scxml_component_scope *scope,
    const char *component_id) {
    salts_component_service service = {0};
    cmeta_status projection;
    if (borrow == NULL || scope == NULL || !scope->live ||
        component_id == NULL || component_id[0] == '\0') return false;
    memset(borrow, 0, sizeof(*borrow));
    if (salts_component_plugin_scope_find_service_from(
            &scope->component_scope, component_id,
            scxml_text_resource_provider_interface(), &service) !=
        SALTS_COMPONENT_PLUGIN_OK)
        return false;
    projection = scxml_text_resource_provider_borrow_from_object(
        service.object, service.interfaces, &borrow->provider);
    if (projection != CMETA_OK ||
        !scxml_text_resource_provider_valid(&borrow->provider))
        return false;
    borrow->scope = scope;
    borrow->generation_id = scxml_component_scope_generation_id(scope);
    return borrow->generation_id != UINT64_C(0);
}

static vxml_dialog_manager_status voice_document_open(
    void *user, const char *source, size_t source_size,
    const char *media, size_t media_size, size_t max_bytes,
    vxml_dialog_document *out) {
    static const char expected_media[] = "application/voicexml+xml";
    voice_resource_borrow *borrow = (voice_resource_borrow *)user;
    if (borrow == NULL || out == NULL || source == NULL ||
        media == NULL || media_size != sizeof(expected_media) - 1u ||
        memcmp(media, expected_media, media_size) != 0 ||
        borrow->scope == NULL || !borrow->scope->live ||
        borrow->active ||
        scxml_component_scope_generation_id(borrow->scope) !=
            borrow->generation_id)
        return VXML_DIALOG_MANAGER_DOCUMENT_ERROR;
    if (scxml_text_resource_provider_open(
            &borrow->provider, source, source_size, max_bytes,
            &borrow->text) != SCXML_RESOURCE_OK)
        return VXML_DIALOG_MANAGER_DOCUMENT_ERROR;
    borrow->active = true;
    *out = (vxml_dialog_document){
        .data = borrow->text.data,
        .size = borrow->text.size,
        .lease = borrow
    };
    return VXML_DIALOG_MANAGER_OK;
}

static void voice_document_close(
    void *user, vxml_dialog_document *document) {
    voice_resource_borrow *borrow = (voice_resource_borrow *)user;
    if (borrow == NULL || document == NULL ||
        document->lease != borrow || !borrow->active)
        return;
    scxml_text_resource_provider_close(&borrow->provider, &borrow->text);
    borrow->active = false;
    *document = (vxml_dialog_document){0};
}

static const vxml_dialog_document_adapter_v1 voice_documents = {
    .abi_version = VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1,
    .struct_size = sizeof(vxml_dialog_document_adapter_v1),
    .open = voice_document_open,
    .close = voice_document_close
};

static vxml_dialog_event_sink_status voice_publish(
    void *user, const vxml_dialog_event_v1 *event) {
    voice_events *sink = (voice_events *)user;
    size_t n;
    if (sink == NULL || event == NULL ||
        sink->count >= 6u || event->name == NULL ||
        event->dialog_id == NULL ||
        event->name_size >= sizeof(sink->names[0]) ||
        event->dialog_id_size >= sizeof(sink->dialog_ids[0]))
        return VXML_DIALOG_EVENT_INVALID_ARGUMENT;
    n = sink->count++;
    memcpy(sink->names[n], event->name, event->name_size);
    sink->names[n][event->name_size] = '\0';
    memcpy(sink->dialog_ids[n], event->dialog_id, event->dialog_id_size);
    sink->dialog_ids[n][event->dialog_id_size] = '\0';
    return VXML_DIALOG_EVENT_ACCEPTED;
}

static const vxml_dialog_event_sink_v1 voice_sink = {
    .abi_version = VXML_DIALOG_EVENT_SINK_ABI_V1,
    .struct_size = sizeof(vxml_dialog_event_sink_v1),
    .try_publish = voice_publish
};

static scxml_adapter_status voice_accept_refused(
    void *user, const ccxml_accept_request *request,
    cflow_statechart_effect_ticket *ticket, const char **error) {
    (void)user;
    (void)request;
    if (ticket != NULL) *ticket = (cflow_statechart_effect_ticket){0};
    if (error != NULL) *error = "test telephony: no accept";
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static void voice_telephony_close(void *user) {
    voice_telephony *upstream = (voice_telephony *)user;
    if (upstream != NULL) ++upstream->close_count;
}

static bool voice_telephony_quiescent(void *user) {
    voice_telephony *upstream = (voice_telephony *)user;
    return upstream != NULL && upstream->quiescent;
}

static const ccxml_telephony_adapter_v1 voice_upstream = {
    .abi_version = CCXML_TELEPHONY_ADAPTER_ABI_V1,
    .struct_size = sizeof(ccxml_telephony_adapter_v1),
    .prepare_accept = voice_accept_refused,
    .close = voice_telephony_close,
    .is_quiescent = voice_telephony_quiescent
};

static vxml_dialog_manager_status voice_manager_init(
    vxml_dialog_manager *manager,
    voice_resource_borrow *document,
    voice_telephony *upstream,
    voice_events *events) {
    vxml_dialog_manager_config_v1 config =
        vxml_dialog_manager_default_config_v1();
    config.capacity = 1u;
    config.documents = &voice_documents;
    config.document_user = document;
    config.upstream = &voice_upstream;
    config.upstream_user = upstream;
    config.events = &voice_sink;
    config.event_user = events;
    return vxml_dialog_manager_init(manager, &config);
}

static void voice_manager_close_destroy(
    vxml_dialog_manager *manager, voice_telephony *upstream) {
    vxml_dialog_manager_close(manager);
    upstream->quiescent = true;
    check_true(vxml_dialog_manager_is_quiescent(manager));
    check_equal(vxml_dialog_manager_destroy(manager),
                VXML_DIALOG_MANAGER_OK);
}

/* The identical resource Interface is also published by an independently
 * loaded Plugin DSO; only the Salts Plugin runtime owns module unload. */
#define VOICE_DSO_COMPONENT "VoiceComponentDsoFixture"

static salts_component_plugin_status voice_dso_generation_build(
    voice_generation *g, cmeta_plugin_registry *registry,
    cmeta_plugin_ref plugin, uint64_t generation_id) {
    const salts_component_plugin_generation_storage storage = {
        g->deployments, 1u, g->instances, 1u, g->dependencies, 1u,
        g->activation_order, 1u, g->modules, 1u
    };
    const salts_component_plugin_source source = {
        plugin, "component-provider", NULL, NULL
    };
    memset(g, 0, sizeof(*g));
    return salts_component_plugin_generation_build(
        &g->generation, generation_id, registry, &storage,
        NULL, 0u, &source, 1u, NULL, 0u);
}

static int voice_dso_marker(const scxml_component_scope *scope) {
    salts_component_service service = {0};
    if (scope == NULL || !scope->live ||
        salts_component_plugin_scope_find_service_from(
            &scope->component_scope, VOICE_DSO_COMPONENT,
            scxml_text_resource_provider_interface(), &service) !=
            SALTS_COMPONENT_PLUGIN_OK ||
        service.object == NULL ||
        !cmeta_data_desc_equal(service.object->data, &cmeta_data_int))
        return -1;
    return *(const int *)service.object->object;
}

/* This CFlow Machine is an INDEPENDENT Host control Actor, never a wrapper
 * around the VoiceXML Session/Program or an additional Statechart instance.
 * It sends only small native int control messages; DialogManager's one
 * serialized run_ready() progresses on the Actor owner executor lane. */
typedef struct voice_actor_probe {
    vxml_dialog_manager *manager;
    const void *owner_thread;
    atomic_bool wrong_owner;
    atomic_int actions;
    atomic_int values;
    atomic_int failures;
    atomic_int dones;
    atomic_int last_message;
} voice_actor_probe;

static bool voice_actor_progress(
    void *user, const void *state, const void *event,
    void *next_state, void *observation, const char **out_error) {
    voice_actor_probe *probe = (voice_actor_probe *)user;
    size_t advanced = 0u;
    vxml_dialog_manager_status status;
    if (probe == NULL || state == NULL || event == NULL ||
        next_state == NULL || observation == NULL || out_error == NULL)
        return false;
    if (probe->owner_thread != cmeta_thread_current_token())
        atomic_store(&probe->wrong_owner, true);
    status = vxml_dialog_manager_run_ready(probe->manager, 1u, &advanced);
    if (status != VXML_DIALOG_MANAGER_OK || advanced != 1u) {
        *out_error = "VoiceXML host Actor cannot advance bounded dialog row";
        return false;
    }
    *(int *)next_state = *(const int *)state + 1;
    *(int *)observation = *(const int *)event;
    atomic_fetch_add(&probe->actions, 1);
    *out_error = NULL;
    return true;
}

static bool voice_actor_on_value(
    void *user, const cmeta_type_desc *type, const void *value) {
    voice_actor_probe *probe = (voice_actor_probe *)user;
    if (probe == NULL || value == NULL ||
        !cmeta_type_equal(type, &cmeta_type_int))
        return false;
    if (probe->owner_thread != cmeta_thread_current_token())
        atomic_store(&probe->wrong_owner, true);
    atomic_store(&probe->last_message, *(const int *)value);
    atomic_fetch_add(&probe->values, 1);
    return true;
}

static void voice_actor_on_error(void *user, const char *message) {
    voice_actor_probe *probe = (voice_actor_probe *)user;
    if (probe != NULL && message != NULL)
        atomic_fetch_add(&probe->failures, 1);
}

static void voice_actor_on_done(void *user) {
    voice_actor_probe *probe = (voice_actor_probe *)user;
    if (probe != NULL) atomic_fetch_add(&probe->dones, 1);
}

spec("VoiceXML/CCXML Component resource generation") {
    it("uses one independent bounded Host Actor for owner-affine DialogManager progress") {
        static const char source[] = "mem:voice-actor";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "actor-call";
        voice_resource_state resource = {0};
        voice_generation generation = {0};
        salts_component_plugin_runtime runtime = {0};
        salts_component_plugin_generation *previous = NULL;
        scxml_component_scope scope = {0};
        voice_resource_borrow document = {0};
        vxml_dialog_manager manager = {0};
        voice_telephony upstream = {0};
        voice_events events = {0};
        cflow_machine machine = {0};
        cflow_executor actor_executor = {0};
        cflow_scheduler actor_scheduler = {0};
        cflow_actor actor = {0};
        cflow_actor_ref producer = {0};
        cflow_actor_stats actor_stats = {0};
        voice_actor_probe probe = {0};
        const ccxml_telephony_adapter_v1 *adapter =
            vxml_dialog_manager_ccxml_adapter();
        const ccxml_dialog_start_request request = {
            .source = source, .source_size = sizeof(source) - 1u,
            .media_type = media, .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u
        };
        ccxml_string_view dialog = {0};
        cflow_statechart_effect_ticket ticket = {0};
        cflow_machine_action_binding action_binding = {
            300u, voice_actor_progress, &probe
        };
        const cflow_machine_state states[1] = {
            {10u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE}
        };
        const cflow_event_type control_events[1] = {
            {100u, &cmeta_type_int}
        };
        const cflow_machine_action actions[1] = {
            {300u, &cmeta_type_int, 100u, &cmeta_type_int,
             &cmeta_type_int, CMETA_EFFECT_STATEFUL | CMETA_EFFECT_MAY_FAIL,
             CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS,
             CFLOW_MACHINE_ACTION_VALUE,
             &cmeta_type_int, 0u}
        };
        const cflow_machine_transition transitions[1] = {
            {10u, 100u, 0u, 300u, 10u, 1u}
        };
        const cflow_machine_definition definition = {
            states, 1u, 10u,
            control_events, 1u,
            NULL, 0u,
            actions, 1u,
            transitions, 1u
        };
        int state_value = 0;
        int mutable_control = 1;
        const cflow_event_view control = {
            100u, &cmeta_type_int, &mutable_control
        };
        const char *error = NULL;
        uint64_t deadline;

        voice_resource_init(&resource, 100, source);
        check_equal(voice_generation_build(
            &generation, &resource, UINT64_C(41)),
            SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_init(&runtime),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_publish(
            &runtime, &generation.generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_null(previous);
        check_equal(scxml_component_scope_acquire(
            &scope, &runtime), SCXML_COMPONENT_OK);
        check_true(voice_resource_bind(
            &document, &scope, VOICE_STATIC_COMPONENT));
        check_equal(voice_manager_init(
            &manager, &document, &upstream, &events),
            VXML_DIALOG_MANAGER_OK);

        /* Prepare/commit is CCXML-owned and must NOT open the document.
           Only an admitted independent Host Actor control message may
           call the manager's explicitly serialized progress point. */
        check_equal(adapter->prepare_dialog_start(
            vxml_dialog_manager_ccxml_user(&manager),
            &request, &dialog, &ticket, &error), SCXML_ADAPTER_ACCEPTED);
        check_not_null(ticket.commit);
        ticket.commit(ticket.user);
        check_equal(resource.open_count, (size_t)0u);
        check_equal(events.count, (size_t)0u);

        atomic_init(&probe.wrong_owner, false);
        atomic_init(&probe.actions, 0);
        atomic_init(&probe.values, 0);
        atomic_init(&probe.failures, 0);
        atomic_init(&probe.dones, 0);
        atomic_init(&probe.last_message, -1);
        probe.manager = &manager;
        probe.owner_thread = cmeta_thread_current_token();

        check_equal(cflow_machine_build(
            &machine, &definition), CFLOW_MACHINE_OK);
        check_true(cflow_executor_owner_init_with_capacity(
            &actor_executor, 8u, NULL, NULL));
        /* Bind the scheduler to the SAME owner executor. Both Machine
           action and Subscription callbacks then run on the Host lane;
           there is no second worker or Session/Statechart instance. */
        check_true(cflow_scheduler_owner_bind(
            &actor_scheduler, &actor_executor, 4u));
        {
            cflow_actor_config config = {0};
            config.machine = (cflow_machine_instance_config){
                &machine, &state_value, &cmeta_type_int,
                NULL, 0u, &action_binding, 1u, 1u, &actor_executor
            };
            config.scheduler = &actor_scheduler;
            config.callbacks = (cflow_subscriber_callbacks){
                voice_actor_on_value, voice_actor_on_error,
                voice_actor_on_done, &probe
            };
            check_equal(cflow_actor_init(
                &actor, &config).status, CFLOW_ACTOR_OK);
        }
        check_true(cflow_actor_ref_acquire(&actor, &producer));
        check_equal(cflow_actor_ref_try_send(
            &producer, &control), CFLOW_ACTOR_SEND_NOT_STARTED);

        /* Deterministic FULL: do not run the owner executor after start
           until one typed control message fills capacity-one Actor Mailbox.
           A concurrent worker scheduler is not required for this gate. */
        check_equal(cflow_actor_start(&actor), CFLOW_ACTOR_OK);
        check_equal(cflow_actor_ref_try_send(
            &producer, &control), CFLOW_ACTOR_SEND_ACCEPTED);
        mutable_control = 77; /* accepted trivial payload is a copied int */
        check_equal(cflow_actor_ref_try_send(
            &producer, &control), CFLOW_ACTOR_SEND_FULL);
        check_equal(resource.open_count, (size_t)0u);

        deadline = cmeta_monotonic_ms() + UINT64_C(5000);
        while ((atomic_load(&probe.values) < 1 ||
                atomic_load(&probe.actions) < 1) &&
               cmeta_monotonic_ms() < deadline) {
            if (!cflow_executor_run_one(&actor_executor))
                cmeta_sleep_ms(1u);
        }
        check_equal(atomic_load(&probe.actions), 1);
        check_equal(atomic_load(&probe.values), 1);
        check_equal(atomic_load(&probe.last_message), 1);
        check_false(atomic_load(&probe.wrong_owner));
        check_equal(atomic_load(&probe.failures), 0);
        check_equal(resource.open_count, (size_t)1u);
        check_equal(resource.close_count, (size_t)1u);
        check_equal(events.count, (size_t)2u);
        check_equal(events.names[0], "dialog.started");
        check_equal(events.names[1], "dialog.exit");
        check_true(cflow_actor_get_stats(&actor, &actor_stats));
        check_equal(actor_stats.machine.accepted, UINT64_C(1));
        check_equal(actor_stats.machine.completed, UINT64_C(1));
        check_equal(actor_stats.machine.pending, (size_t)0u);

        check_equal(cflow_actor_request_stop(&actor), CFLOW_ACTOR_OK);
        deadline = cmeta_monotonic_ms() + UINT64_C(5000);
        while (cflow_actor_current_state(&actor) !=
                   CFLOW_ACTOR_STATE_STOPPED &&
               cmeta_monotonic_ms() < deadline) {
            if (!cflow_executor_run_one(&actor_executor))
                cmeta_sleep_ms(1u);
        }
        check_equal(cflow_actor_current_state(
            &actor), CFLOW_ACTOR_STATE_STOPPED);
        check_equal(cflow_actor_wait(&actor), CFLOW_ACTOR_STATE_STOPPED);
        check_equal(cflow_actor_ref_try_send(
            &producer, &control), CFLOW_ACTOR_SEND_STOPPED);
        cflow_actor_destroy(&actor);
        check_equal(cflow_actor_ref_try_send(
            &producer, &control), CFLOW_ACTOR_SEND_STALE);
        cflow_actor_ref_release(&producer);
        check_null(producer.impl);
        cflow_scheduler_destroy(&actor_scheduler);
        cflow_executor_destroy(&actor_executor);
        cflow_machine_destroy(&machine);

        voice_manager_close_destroy(&manager, &upstream);
        check_equal(upstream.close_count, (size_t)1u);
        check_equal(scxml_component_scope_release(&scope),
                    SCXML_COMPONENT_OK);
        check_equal(salts_component_plugin_runtime_close(
            &runtime, &previous), SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &generation.generation);
        check_equal(salts_component_plugin_generation_drain(
            &runtime, &generation.generation), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_destroy(
            &runtime), SALTS_COMPONENT_PLUGIN_OK);
    }

    it("pins the prepared dialog to gN while gN+1 starts independently") {
        static const char source_old[] = "mem:voice-gN";
        static const char source_next[] = "mem:voice-gN1";
        static const char media[] = "application/voicexml+xml";
        static const char call_old[] = "call-old";
        static const char call_next[] = "call-next";
        voice_resource_state states[2] = {{0}, {0}};
        voice_generation generations[2] = {{0}, {0}};
        salts_component_plugin_runtime runtime = {0};
        salts_component_plugin_generation *previous = NULL;
        scxml_component_scope scopes[2] = {{0}, {0}};
        voice_resource_borrow documents[2] = {{0}, {0}};
        vxml_dialog_manager managers[2] = {{0}, {0}};
        voice_telephony upstream[2] = {{0}, {0}};
        voice_events events[2] = {{0}, {0}};
        const ccxml_telephony_adapter_v1 *adapter =
            vxml_dialog_manager_ccxml_adapter();
        ccxml_string_view dialog = {0};
        ccxml_dialog_prepare_request prepare = {
            .source = source_old,
            .source_size = sizeof(source_old) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u
        };
        ccxml_prepared_dialog_start_request start = {0};
        ccxml_dialog_start_request new_start = {
            .source = source_next,
            .source_size = sizeof(source_next) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = call_next,
            .connection_id_size = sizeof(call_next) - 1u
        };
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;
        size_t processed = 0u;
        vxml_dialog_manager_stats stats = {0};

        voice_resource_init(&states[0], 100, source_old);
        voice_resource_init(&states[1], 200, source_next);
        check_equal(voice_generation_build(
            &generations[0], &states[0], UINT64_C(11)),
            SALTS_COMPONENT_PLUGIN_OK);
        check_equal(voice_generation_build(
            &generations[1], &states[1], UINT64_C(12)),
            SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_init(&runtime),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_publish(
            &runtime, &generations[0].generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_null(previous);
        check_equal(scxml_component_scope_acquire(
            &scopes[0], &runtime), SCXML_COMPONENT_OK);
        check_true(voice_resource_bind(&documents[0], &scopes[0],
            VOICE_STATIC_COMPONENT));
        check_equal(documents[0].generation_id, UINT64_C(11));
        check_equal(voice_manager_init(
            &managers[0], &documents[0], &upstream[0], &events[0]),
            VXML_DIALOG_MANAGER_OK);

        /* Old CCXML dialogprepare transfers one bounded row but does not
           acquire a document, compile or execute inside ticket.commit(). */
        check_equal(adapter->prepare_dialog_prepare(
            vxml_dialog_manager_ccxml_user(&managers[0]),
            &prepare, &dialog, &ticket, &error),
            SCXML_ADAPTER_ACCEPTED);
        check_not_null(ticket.commit);
        check_not_null(ticket.discard);
        check_equal(states[0].open_count, (size_t)0u);
        ticket.commit(ticket.user);
        check_equal(states[0].open_count, (size_t)0u);
        check_equal(vxml_dialog_manager_run_ready(
            &managers[0], 1u, &processed), VXML_DIALOG_MANAGER_OK);
        check_equal(processed, (size_t)1u);
        check_equal(states[0].open_count, (size_t)1u);
        check_equal(states[0].close_count, (size_t)1u);
        check_equal(states[0].outstanding, (size_t)0u);
        check_equal(events[0].count, (size_t)1u);
        check_equal(events[0].names[0], "dialog.prepared");
        check_true(vxml_dialog_manager_get_stats(&managers[0], &stats));
        check_equal(stats.prepared, (size_t)1u);

        /* Replacement publication moves only Component selection. The
           old DialogManager's prepared Program and original gN Scope remain
           intact; a pending old dialog is not migrated to the new provider. */
        check_equal(salts_component_plugin_runtime_publish(
            &runtime, &generations[1].generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &generations[0].generation);
        check_equal(salts_component_plugin_generation_drain(
            &runtime, &generations[0].generation),
            SALTS_COMPONENT_PLUGIN_BUSY);
        check_equal(scxml_component_scope_acquire(
            &scopes[1], &runtime), SCXML_COMPONENT_OK);
        check_true(voice_resource_bind(&documents[1], &scopes[1],
            VOICE_STATIC_COMPONENT));
        check_equal(documents[1].generation_id, UINT64_C(12));
        check_equal(voice_manager_init(
            &managers[1], &documents[1], &upstream[1], &events[1]),
            VXML_DIALOG_MANAGER_OK);
        check_equal(states[1].open_count, (size_t)0u);

        start.dialog_id = dialog.data;
        start.dialog_id_size = dialog.size;
        start.connection_id = call_old;
        start.connection_id_size = sizeof(call_old) - 1u;
        ticket = (cflow_statechart_effect_ticket){0};
        check_equal(adapter->prepare_prepared_dialog_start(
            vxml_dialog_manager_ccxml_user(&managers[0]),
            &start, &ticket, &error),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(vxml_dialog_manager_run_ready(
            &managers[0], 1u, &processed), VXML_DIALOG_MANAGER_OK);
        check_equal(events[0].count, (size_t)3u);
        check_equal(events[0].names[1], "dialog.started");
        check_equal(events[0].names[2], "dialog.exit");
        check_equal(states[0].open_count, (size_t)1u);
        check_equal(states[1].open_count, (size_t)0u);
        check_equal(salts_component_plugin_generation_drain(
            &runtime, &generations[0].generation),
            SALTS_COMPONENT_PLUGIN_BUSY);

        voice_manager_close_destroy(&managers[0], &upstream[0]);
        check_equal(upstream[0].close_count, (size_t)1u);
        check_equal(scxml_component_scope_release(&scopes[0]),
                    SCXML_COMPONENT_OK);
        check_equal(salts_component_plugin_generation_drain(
            &runtime, &generations[0].generation),
            SALTS_COMPONENT_PLUGIN_OK);

        /* Fresh dialog binds gN+1 after gN has fully drained. Native
           VoiceXML/CCXML execution is unchanged and remains manager-owned. */
        ticket = (cflow_statechart_effect_ticket){0};
        check_equal(adapter->prepare_dialog_start(
            vxml_dialog_manager_ccxml_user(&managers[1]),
            &new_start, &dialog, &ticket, &error),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(vxml_dialog_manager_run_ready(
            &managers[1], 1u, &processed), VXML_DIALOG_MANAGER_OK);
        check_equal(events[1].count, (size_t)2u);
        check_equal(events[1].names[0], "dialog.started");
        check_equal(events[1].names[1], "dialog.exit");
        check_equal(states[1].open_count, (size_t)1u);
        check_equal(states[1].close_count, (size_t)1u);
        check_equal(states[1].outstanding, (size_t)0u);
        check_equal(states[0].open_count, (size_t)1u);

        voice_manager_close_destroy(&managers[1], &upstream[1]);
        check_equal(upstream[1].close_count, (size_t)1u);
        check_equal(scxml_component_scope_release(&scopes[1]),
                    SCXML_COMPONENT_OK);
        check_equal(salts_component_plugin_runtime_close(
            &runtime, &previous), SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &generations[1].generation);
        check_equal(salts_component_plugin_generation_drain(
            &runtime, &generations[1].generation),
            SALTS_COMPONENT_PLUGIN_OK);
        check_equal(runtime.attached_generations, (size_t)0u);
        check_equal(runtime.active_scopes, (size_t)0u);
        check_equal(salts_component_plugin_runtime_destroy(
            &runtime), SALTS_COMPONENT_PLUGIN_OK);
    }

    it("executes DSO resource callbacks through old CCXML prepare/terminate after new generation publishes") {
        static const char uri[] = "mem:voice-dso";
        static const char media[] = "application/voicexml+xml";
        static const char call[] = "call-new-dso";
        const char *paths[] = {VOICE_RESOURCE_DSO_ONE, VOICE_RESOURCE_DSO_TWO};
        const cmeta_plugin_registry_config registry_conf = {.capacity = 2u};
        cmeta_plugin_registry registry = {0};
        cmeta_plugin_ref plugins[2] = {{0}, {0}};
        voice_generation generations[2] = {{0}, {0}};
        salts_component_plugin_runtime runtime = {0};
        salts_component_plugin_generation *previous = NULL;
        scxml_component_scope scopes[2] = {{0}, {0}};
        voice_resource_borrow documents[2] = {{0}, {0}};
        vxml_dialog_manager managers[2] = {{0}, {0}};
        voice_telephony upstream[2] = {{0}, {0}};
        voice_events events[2] = {{0}, {0}};
        const ccxml_telephony_adapter_v1 *adapter =
            vxml_dialog_manager_ccxml_adapter();
        const ccxml_dialog_prepare_request prepare = {
            .source = uri, .source_size = sizeof(uri) - 1u,
            .media_type = media, .media_type_size = sizeof(media) - 1u
        };
        const ccxml_dialog_start_request new_start = {
            .source = uri, .source_size = sizeof(uri) - 1u,
            .media_type = media, .media_type_size = sizeof(media) - 1u,
            .connection_id = call, .connection_id_size = sizeof(call) - 1u
        };
        ccxml_dialog_terminate_request terminate = {0};
        ccxml_string_view old_dialog = {0};
        ccxml_string_view new_dialog = {0};
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;
        size_t progressed = 0u;
        bool quiescent = false;
        size_t i;

        check_equal(cmeta_plugin_registry_init(
            &registry, &registry_conf), CMETA_PLUGIN_OK);
        for (i = 0u; i < 2u; ++i) {
            check_equal(cmeta_plugin_registry_load(
                &registry, paths[i], &plugins[i]), CMETA_PLUGIN_OK);
            check_equal(cmeta_plugin_registry_start(
                &registry, plugins[i]), CMETA_PLUGIN_OK);
            check_equal(voice_dso_generation_build(
                &generations[i], &registry, plugins[i],
                UINT64_C(21) + (uint64_t)i),
                SALTS_COMPONENT_PLUGIN_OK);
        }
        check_equal(salts_component_plugin_runtime_init(&runtime),
                    SALTS_COMPONENT_PLUGIN_OK);
        check_equal(salts_component_plugin_runtime_publish(
            &runtime, &generations[0].generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_null(previous);
        check_equal(scxml_component_scope_acquire(
            &scopes[0], &runtime), SCXML_COMPONENT_OK);
        check_true(voice_resource_bind(
            &documents[0], &scopes[0], VOICE_DSO_COMPONENT));
        check_equal(voice_dso_marker(&scopes[0]), 100);
        check_equal(voice_manager_init(
            &managers[0], &documents[0], &upstream[0], &events[0]),
            VXML_DIALOG_MANAGER_OK);

        /* Old dialog ticket commits before gN+1 publication, but the
           actual DSO open/close callbacks execute AFTER publication. */
        check_equal(adapter->prepare_dialog_prepare(
            vxml_dialog_manager_ccxml_user(&managers[0]),
            &prepare, &old_dialog, &ticket, &error),
            SCXML_ADAPTER_ACCEPTED);
        check_not_null(ticket.commit);
        check_equal(voice_dso_marker(&scopes[0]), 100);
        ticket.commit(ticket.user);

        check_equal(salts_component_plugin_runtime_publish(
            &runtime, &generations[1].generation, &previous),
            SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &generations[0].generation);
        check_equal(salts_component_plugin_generation_drain(
            &runtime, &generations[0].generation),
            SALTS_COMPONENT_PLUGIN_BUSY);
        check_equal(cmeta_plugin_registry_unload(
            &registry, plugins[0]), CMETA_PLUGIN_BUSY);
        check_equal(scxml_component_scope_acquire(
            &scopes[1], &runtime), SCXML_COMPONENT_OK);
        check_true(voice_resource_bind(
            &documents[1], &scopes[1], VOICE_DSO_COMPONENT));
        check_equal(scxml_component_scope_generation_id(
            &scopes[1]), UINT64_C(22));
        check_equal(voice_dso_marker(&scopes[1]), 200);
        check_equal(voice_manager_init(
            &managers[1], &documents[1], &upstream[1], &events[1]),
            VXML_DIALOG_MANAGER_OK);

        check_equal(vxml_dialog_manager_run_ready(
            &managers[0], 1u, &progressed), VXML_DIALOG_MANAGER_OK);
        check_equal(progressed, (size_t)1u);
        check_equal(events[0].count, (size_t)1u);
        check_equal(events[0].names[0], "dialog.prepared");
        check_equal(voice_dso_marker(&scopes[0]), 111);
        check_equal(voice_dso_marker(&scopes[1]), 200);
        check_false(documents[0].active);

        /* Termination is a distinct CCXML ticket; do not refetch the old
           document and do not migrate a prepared Program to gN+1. */
        terminate.dialog_id = old_dialog.data;
        terminate.dialog_id_size = old_dialog.size;
        terminate.immediate = false;
        ticket = (cflow_statechart_effect_ticket){0};
        check_equal(adapter->prepare_dialog_terminate(
            vxml_dialog_manager_ccxml_user(&managers[0]),
            &terminate, &ticket, &error), SCXML_ADAPTER_ACCEPTED);
        check_not_null(ticket.commit);
        ticket.commit(ticket.user);
        check_equal(vxml_dialog_manager_run_ready(
            &managers[0], 1u, &progressed), VXML_DIALOG_MANAGER_OK);
        check_equal(events[0].count, (size_t)2u);
        check_equal(events[0].names[1], "dialog.exit");
        check_equal(voice_dso_marker(&scopes[0]), 111);
        check_equal(voice_dso_marker(&scopes[1]), 200);

        /* A borrowed provider dispatch inside the DialogManager requires
           its caller-retained Scope. Retire manager before that Scope and
           unload only after its original generation is fully draining. */
        check_equal(cmeta_plugin_registry_unload(
            &registry, plugins[0]), CMETA_PLUGIN_BUSY);
        voice_manager_close_destroy(&managers[0], &upstream[0]);
        check_equal(upstream[0].close_count, (size_t)1u);
        check_equal(scxml_component_scope_release(&scopes[0]),
                    SCXML_COMPONENT_OK);
        check_equal(salts_component_plugin_generation_drain(
            &runtime, &generations[0].generation),
            SALTS_COMPONENT_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_request_stop(
            &registry, plugins[0]), CMETA_PLUGIN_OK);
        quiescent = false;
        check_equal(cmeta_plugin_registry_poll_quiescent(
            &registry, plugins[0], &quiescent), CMETA_PLUGIN_OK);
        check_true(quiescent);
        check_equal(cmeta_plugin_registry_unload(
            &registry, plugins[0]), CMETA_PLUGIN_OK);
        plugins[0] = (cmeta_plugin_ref){0};

        /* This authentic second DSO stays callable AFTER gN code unloaded;
           it owns a distinct source marker and document lease. */
        ticket = (cflow_statechart_effect_ticket){0};
        check_equal(adapter->prepare_dialog_start(
            vxml_dialog_manager_ccxml_user(&managers[1]),
            &new_start, &new_dialog, &ticket, &error),
            SCXML_ADAPTER_ACCEPTED);
        check_not_null(ticket.commit);
        ticket.commit(ticket.user);
        check_equal(vxml_dialog_manager_run_ready(
            &managers[1], 1u, &progressed), VXML_DIALOG_MANAGER_OK);
        check_equal(events[1].count, (size_t)2u);
        check_equal(events[1].names[0], "dialog.started");
        check_equal(events[1].names[1], "dialog.exit");
        check_equal(voice_dso_marker(&scopes[1]), 211);
        check_false(documents[1].active);
        voice_manager_close_destroy(&managers[1], &upstream[1]);
        check_equal(upstream[1].close_count, (size_t)1u);
        check_equal(scxml_component_scope_release(&scopes[1]),
                    SCXML_COMPONENT_OK);
        check_equal(salts_component_plugin_runtime_close(
            &runtime, &previous), SALTS_COMPONENT_PLUGIN_OK);
        check_true(previous == &generations[1].generation);
        check_equal(salts_component_plugin_generation_drain(
            &runtime, &generations[1].generation),
            SALTS_COMPONENT_PLUGIN_OK);
        check_equal(runtime.attached_generations, (size_t)0u);
        check_equal(runtime.active_scopes, (size_t)0u);
        check_equal(salts_component_plugin_runtime_destroy(
            &runtime), SALTS_COMPONENT_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_request_stop(
            &registry, plugins[1]), CMETA_PLUGIN_OK);
        quiescent = false;
        check_equal(cmeta_plugin_registry_poll_quiescent(
            &registry, plugins[1], &quiescent), CMETA_PLUGIN_OK);
        check_true(quiescent);
        check_equal(cmeta_plugin_registry_unload(
            &registry, plugins[1]), CMETA_PLUGIN_OK);
        check_equal(cmeta_plugin_registry_destroy(
            &registry), CMETA_PLUGIN_OK);
    }
}
