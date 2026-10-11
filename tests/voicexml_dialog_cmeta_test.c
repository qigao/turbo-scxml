#include <voicexml/cmeta.h>
#include <voicexml/dialog_manager.h>
#include <voicexml/document_store.h>
#include <tinytest.h>
#include <string.h>
#ifdef TURBOSCXML_TEST_QUICKJS_FIA
#include "voicexml_quickjs_fia.h"
#include "voicexml_internal.h"
#endif

typedef struct dialog_value { int value; } dialog_value;
static const cmeta_type_identity root_identity = CMETA_TYPE_ID_ATOM_INIT("test.dialog.cmeta");
static const cmeta_type_traits root_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY};
static const cmeta_type_desc root_type = {
    .name = "dialog_value", .size = sizeof(dialog_value), .align = _Alignof(dialog_value),
    .kind = CMETA_T_OBJECT, .traits = &root_traits, .identity = &root_identity};
static const cmeta_field_desc layout_fields[] = {
    {"value", "int", offsetof(dialog_value, value), sizeof(int), _Alignof(int), &cmeta_type_int, NULL}};
static const cmeta_struct_desc layout = {
    .name = "dialog_value", .size = sizeof(dialog_value), .align = _Alignof(dialog_value),
    .fields = layout_fields, .field_count = 1u};
static const cmeta_data_field_desc fields[] = {
    {"test.dialog.cmeta.value", "value", offsetof(dialog_value, value), &cmeta_data_int}};
static const cmeta_data_struct_shape shape = {.layout = &layout, .fields = fields, .field_count = 1u};
static const cmeta_data_desc root_data = {
    .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.dialog.cmeta", .display_name = "dialog_value",
    .kind = CMETA_DATA_STRUCT, .storage_type = &root_type, .shape = &shape};

enum { DIALOG_CAPACITY = 2, EVENT_CAPACITY = 8 };
typedef struct dialog_probe dialog_probe;
typedef struct collect_probe {
    dialog_probe *owner;
    vxml_session *session;
    uint64_t generation;
    size_t prepares, commits, cancels, progress_calls, barriers;
    bool stopped;
#ifdef TURBOSCXML_TEST_QUICKJS_FIA
    char bound_uri[256];
#endif
} collect_probe;
struct dialog_probe {
    vxml_document_store store;
    vxml_dialog_manager manager;
    collect_probe collect[DIALOG_CAPACITY];
    size_t inits, opens, closes, event_count;
    const char *source;
    const char *next_source;
    const char *events[EVENT_CAPACITY];
    vxml_status statuses[EVENT_CAPACITY];
    bool full;
    vxml_status progress_status;
    int64_t last_exit_value;
#ifdef TURBOSCXML_TEST_QUICKJS_FIA
    const char *script;
    size_t script_opens, script_closes;
    vxml_script_resource_status script_status;
    size_t submit_opens, submit_closes;
    char script_uri[256];
#endif
};

#ifdef TURBOSCXML_TEST_QUICKJS_FIA
static vxml_script_resource_status open_script(void *user, const char *uri, size_t size,
    const char *charset, size_t charset_size, size_t max_bytes, vxml_script_source *out) {
    dialog_probe *probe = user;
    check_less(size, sizeof(probe->script_uri));
    memcpy(probe->script_uri, uri, size);
    probe->script_uri[size] = '\0';
    check_equal(charset_size, (size_t)5u);
    check_equal(charset, "UTF-8", 5u);
    check_not_null(probe->script);
    check_less_equal(strlen(probe->script), max_bytes);
    ++probe->script_opens;
    *out = (vxml_script_source){probe->script, strlen(probe->script), probe};
    return probe->script_status;
}
static void close_script(void *user, vxml_script_source *source) {
    dialog_probe *probe = user;
    check_true(source->lease == probe);
    ++probe->script_closes;
    *source = (vxml_script_source){0};
}
static const vxml_script_resource_adapter_v1 script_adapter = {
    .abi_version = VXML_SCRIPT_RESOURCE_ADAPTER_ABI_V1, .struct_size = sizeof(script_adapter),
    .open = open_script, .close = close_script};
static vxml_submit_resource_status submit_response(void *user,
    const vxml_submit_wire_request_v1 *request, vxml_submit_response *out) {
    dialog_probe *probe = user;
    check_equal(request->method, VXML_SUBMIT_METHOD_POST);
    check_equal(request->uri_size, strlen("https://voice.example/submit"));
    check_equal(request->uri, "https://voice.example/submit", request->uri_size);
    check_equal(request->body_size, (size_t)7u);
    check_equal(request->body, "value=7", 7u);
    ++probe->submit_opens;
    static const char effective_uri[] = "https://voice.example/replies/final.vxml";
    static const char media_type[] = "application/voicexml+xml";
    *out = (vxml_submit_response){.data = probe->next_source, .size = strlen(probe->next_source),
        .media_type = media_type, .media_type_size = sizeof(media_type) - 1u,
        .effective_uri = effective_uri, .effective_uri_size = sizeof(effective_uri) - 1u, .lease = probe};
    return VXML_SUBMIT_RESOURCE_OK;
}
static void close_submit(void *user, vxml_submit_response *response) {
    dialog_probe *probe = user;
    check_true(response->lease == probe);
    ++probe->submit_closes;
    *response = (vxml_submit_response){0};
}
static const vxml_submit_resource_adapter_v1 submit_adapter = {
    .abi_version = VXML_SUBMIT_RESOURCE_ADAPTER_ABI_V1, .struct_size = sizeof(submit_adapter),
    .execute = submit_response, .close = close_submit};
#endif

static void collect_commit(void *user) { ++((collect_probe *)user)->commits; }
static void collect_discard(void *user) { (void)user; }
static void collect_cancel(void *user, uint64_t generation) {
    collect_probe *probe = user;
    check_true(probe->stopped);
    check_equal(generation, probe->generation);
    ++probe->cancels;
}
static vxml_status collect_prepare(void *user, const vxml_cmeta_collect_request_v1 *request,
    vxml_cmeta_collect_ticket_v1 *ticket, const char **error) {
    collect_probe *probe = user;
    (void)error;
    ++probe->prepares;
    probe->generation = request->generation;
    *ticket = (vxml_cmeta_collect_ticket_v1){collect_commit, collect_discard, probe};
    return VXML_OK;
}
static const vxml_cmeta_collect_adapter_v1 collect_adapter = {
    .abi_version = VXML_CMETA_COLLECT_ADAPTER_ABI_V1, .struct_size = sizeof(collect_adapter),
    .capabilities = VXML_CMETA_COLLECT_CAP_SRGS_XML,
    .prepare = collect_prepare, .cancel = collect_cancel};

static vxml_status compile_document(void *user, const void *bytes, size_t size,
    vxml_program *program, vxml_diagnostic *diagnostic) {
    (void)user;
    const vxml_cmeta_compile_options_v1 options = {
        .abi_version = VXML_CMETA_COMPILE_OPTIONS_ABI_V1, .struct_size = sizeof(options),
        .root = &root_data, .max_expression_bytes = 1024u, .max_expression_instructions = 256u,
        .max_expression_operands = 32u, .max_expression_depth = 16u, .max_path_depth = 8u,
        .max_literal_bytes = 1024u, .max_string_bytes = 1024u, .max_scope_slots = 32u,
        .max_scope_storage_bytes = 4096u, .max_conditional_depth = 8u,
        .max_fields = 4u, .max_grammar_bytes = 256u,
        .max_submit_fields = 4u, .max_submit_value_bytes = 128u,
        .max_submit_uri_bytes = 255u,
        .max_event_handlers = 8u, .max_event_name_bytes = 64u};
#ifdef TURBOSCXML_TEST_QUICKJS_FIA
    const vxml_quickjs_fia_limits limits = {
        .engine = {1024u, 1024u, 4u * 1024u * 1024u, 256u * 1024u, 100u},
        .values = {16u, 128u, 128u, 4096u, 1024u}};
    vxml_status status = vxml_compile_cmeta_backend(bytes, size, NULL, &options,
        &vxml_quickjs_fia_backend, &limits, program, diagnostic);
#else
    vxml_status status = vxml_compile_cmeta(bytes, size, NULL, &options, program, diagnostic);
#endif
    check(status == VXML_OK, "%s", diagnostic != NULL ? diagnostic->message : "compile failed");
    return status;
}
static vxml_dialog_manager_status open_document(void *user, const char *uri, size_t uri_size,
    const char *media, size_t media_size, size_t max_bytes, vxml_dialog_document *out) {
    dialog_probe *probe = user;
    (void)media; (void)media_size;
    const char *source = probe->source;
    const char suffix[] = "/next.vxml";
    if (probe->next_source != NULL && uri_size >= sizeof(suffix) - 1u &&
        memcmp(uri + uri_size - (sizeof(suffix) - 1u), suffix, sizeof(suffix) - 1u) == 0)
        source = probe->next_source;
    if (strlen(source) > max_bytes) return VXML_DIALOG_MANAGER_DOCUMENT_ERROR;
    ++probe->opens;
    *out = (vxml_dialog_document){.data = source, .size = strlen(source), .lease = probe};
    return VXML_DIALOG_MANAGER_OK;
}
static void close_document(void *user, vxml_dialog_document *document) {
    ++((dialog_probe *)user)->closes;
    *document = (vxml_dialog_document){0};
}
static vxml_status init_session(void *user, vxml_session *session, const vxml_program *program) {
    dialog_probe *probe = user;
    if (probe->inits >= DIALOG_CAPACITY) return VXML_LIMIT_EXCEEDED;
    collect_probe *collect = &probe->collect[probe->inits++];
    collect->session = session;
    collect->owner = probe;
    static const dialog_value initial = {0};
    static const vxml_cmeta_name_view undefined[] = {{"value", 5u}};
    const vxml_cmeta_session_options_v1 options = {
        .abi_version = VXML_CMETA_SESSION_OPTIONS_ABI_V1, .struct_size = sizeof(options),
        .initial_root = &initial, .initially_undefined = undefined, .initially_undefined_count = 1u,
        .max_transaction_bytes = 4096u, .max_execution_steps = 128u,
        .collect = &collect_adapter, .collect_user = collect,
        .max_event_counters = 16u, .max_event_name_bytes = 64u, .max_event_dispatch_depth = 8u};
#ifdef TURBOSCXML_TEST_QUICKJS_FIA
    const vxml_quickjs_fia_scripts scripts = {&script_adapter, probe, 255u, 1024u};
    vxml_status status = vxml_session_init_quickjs_fia(session, program, &options, &scripts);
#else
    vxml_status status = vxml_session_init_cmeta(session, program, &options);
#endif
    check_equal(status, VXML_OK);
    return status;
}
static collect_probe *find_collect(dialog_probe *probe, vxml_session *session) {
    for (size_t i = 0u; i < probe->inits; ++i)
        if (probe->collect[i].session == session && !probe->collect[i].stopped) return &probe->collect[i];
    return NULL;
}
static vxml_status progress_session(void *user, vxml_session *session, bool *progressed) {
    dialog_probe *probe = user;
    collect_probe *collect = find_collect(probe, session);
    if (collect == NULL) return VXML_INVALID_STATE;
#ifdef TURBOSCXML_TEST_QUICKJS_FIA
    const vxml_session_document_context *document = &((vxml_session_impl *)session->impl)->document_context;
    if (collect->bound_uri[0] == '\0') {
        check_less(document->uri_size, sizeof(collect->bound_uri));
        memcpy(collect->bound_uri, document->uri, document->uri_size);
        collect->bound_uri[document->uri_size] = '\0';
    }
#endif
    ++collect->progress_calls;
    *progressed = false;
    if (probe->progress_status != VXML_OK) return probe->progress_status;
    const vxml_status status = vxml_session_cmeta_run_ready(session, progressed);
    if (status == VXML_OK && vxml_session_cmeta_exit_count(session) == 1u) {
        vxml_cmeta_name_view name = {0};
        vxml_cmeta_value_view value = {0};
        check_equal(vxml_session_cmeta_exit_at(session, 0u, &name, &value), VXML_OK);
        check_equal(value.kind, VXML_CMETA_VALUE_SINT);
        probe->last_exit_value = value.data.sint;
    }
    return status;
}
static void quiesce_session(void *user, vxml_session *session) {
    collect_probe *collect = find_collect(user, session);
    check_not_null(collect);
    check_not_null(session->impl);
#ifdef TURBOSCXML_TEST_QUICKJS_FIA
    if (collect->bound_uri[0] != '\0')
        check_equal(((vxml_session_impl *)session->impl)->document_context.uri, collect->bound_uri);
#endif
    ++collect->barriers;
    collect->stopped = true;
}
static vxml_dialog_event_sink_status publish(void *user, const vxml_dialog_event_v1 *event) {
    dialog_probe *probe = user;
    if (probe->full) return VXML_DIALOG_EVENT_FULL;
    if (probe->event_count == EVENT_CAPACITY) return VXML_DIALOG_EVENT_FULL;
    /* Manager event names have static lifetime; only names are retained here. */
    probe->events[probe->event_count] = event->name;
    probe->statuses[probe->event_count++] = event->voice_status;
    return VXML_DIALOG_EVENT_ACCEPTED;
}
static scxml_adapter_status accept_call(void *user, const ccxml_accept_request *request,
    cflow_statechart_effect_ticket *ticket, const char **error) {
    (void)user; (void)request; (void)ticket; (void)error;
    return SCXML_ADAPTER_ERROR_EXECUTION;
}
static void close_upstream(void *user) { (void)user; }
static bool upstream_quiescent(void *user) { (void)user; return true; }
static vxml_dialog_manager_config_v3 setup(dialog_probe *probe, vxml_session_factory_v1 *factory) {
    static const vxml_dialog_document_adapter_v1 documents = {
        .abi_version = VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1, .struct_size = sizeof(documents),
        .open = open_document, .close = close_document};
    static const vxml_document_compile_adapter_v1 compiler = {
        .abi_version = VXML_DOCUMENT_COMPILE_ADAPTER_ABI_V1, .struct_size = sizeof(compiler),
        .compile = compile_document};
    static const ccxml_telephony_adapter_v1 upstream = {
        .abi_version = CCXML_TELEPHONY_ADAPTER_ABI_V1, .struct_size = sizeof(upstream),
        .prepare_accept = accept_call, .close = close_upstream, .is_quiescent = upstream_quiescent};
    static const vxml_dialog_event_sink_v1 events = {
        .abi_version = VXML_DIALOG_EVENT_SINK_ABI_V1, .struct_size = sizeof(events), .try_publish = publish};
    vxml_document_store_config_v1 store = {
        .abi_version = VXML_DOCUMENT_STORE_CONFIG_ABI_V1, .struct_size = sizeof(store),
        .application_uri = "https://voice.example/root.vxml",
        .capacity = 2u, .max_uri_bytes = 255u, .max_document_bytes = 4096u,
        .max_cache_bytes = 8192u, .documents = &documents, .document_user = probe, .compiler = &compiler};
    store.application_uri_size = strlen(store.application_uri);
    store.voice_limits = vxml_default_limits();
    check_equal(vxml_document_store_init(&probe->store, &store), VXML_DOCUMENT_STORE_OK);
    *factory = (vxml_session_factory_v1){
        .abi_version = VXML_SESSION_FACTORY_ABI_V1, .struct_size = sizeof(*factory),
        .init = init_session, .run_ready = progress_session, .quiesce = quiesce_session};
    vxml_dialog_manager_config_v3 config = vxml_dialog_manager_default_config_v3();
    config.capacity = DIALOG_CAPACITY;
    config.upstream = &upstream;
    config.document_store = &probe->store;
    config.events = &events;
    config.event_user = probe;
    config.session_factory = factory;
    config.session_factory_user = probe;
    return config;
}
static ccxml_string_view start_dialog(dialog_probe *probe) {
    const ccxml_telephony_adapter_v1 *adapter = vxml_dialog_manager_ccxml_adapter();
    const ccxml_dialog_start_request request = {
        .source = "dialog.vxml", .source_size = sizeof("dialog.vxml") - 1u,
        .media_type = "application/voicexml+xml", .media_type_size = sizeof("application/voicexml+xml") - 1u,
        .connection_id = "call", .connection_id_size = sizeof("call") - 1u};
    ccxml_string_view id = {0};
    cflow_statechart_effect_ticket ticket = {0};
    check_equal(adapter->prepare_dialog_start(vxml_dialog_manager_ccxml_user(&probe->manager),
        &request, &id, &ticket, NULL), SCXML_ADAPTER_ACCEPTED);
    ticket.commit(ticket.user);
    return id;
}
static void tick(dialog_probe *probe) {
    size_t processed = 0u;
    check_equal(vxml_dialog_manager_run_ready(&probe->manager, 1u, &processed), VXML_DIALOG_MANAGER_OK);
    check_equal(processed, (size_t)1u);
}
static void complete(collect_probe *probe) {
    static const int value = 7;
    const vxml_cmeta_collect_completion_v1 completion = {
        .abi_version = VXML_CMETA_COLLECT_COMPLETION_ABI_V1, .struct_size = sizeof(completion),
        .generation = probe->generation, .data = &cmeta_data_int, .value = &value};
    check_equal(vxml_session_cmeta_collect_try_complete(probe->session, &completion),
        VXML_CMETA_COLLECT_INGRESS_ACCEPTED);
}

spec("DialogManager asynchronous CMeta ownership") {
    static dialog_probe probe;
    static vxml_session_factory_v1 factory;
    static vxml_dialog_manager_config_v3 config;
    before_each() {
        probe = (dialog_probe){0};
        probe.source = "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
            "<form><field name='value'><grammar type='application/srgs+xml' src='a'/>"
#ifdef TURBOSCXML_TEST_QUICKJS_FIA
            "<filled><script>value = Math.pow(value, 2);</script><exit expr='value'/></filled>"
            "</field></form></vxml>";
#else
            "<filled><exit/></filled></field></form></vxml>";
#endif
        config = setup(&probe, &factory);
    }
    after_each() {
        if (probe.manager.impl != NULL) {
            vxml_dialog_manager_close(&probe.manager);
            check_equal(vxml_dialog_manager_destroy(&probe.manager), VXML_DIALOG_MANAGER_OK);
        }
        check_equal(vxml_document_store_destroy(&probe.store), VXML_DOCUMENT_STORE_OK);
        check_equal(probe.opens, probe.closes);
#ifdef TURBOSCXML_TEST_QUICKJS_FIA
        check_equal(probe.script_opens, probe.script_closes);
        check_equal(probe.submit_opens, probe.submit_closes);
#endif
        const size_t expected_barriers = factory.struct_size <= offsetof(vxml_session_factory_v1, run_ready) ? 0u : 1u;
        for (size_t i = 0; i < probe.inits; ++i) check_equal(probe.collect[i].barriers, expected_barriers);
    }
    it("publishes exit after a real collect completion and does not replay work on FULL") {
        check_equal(vxml_dialog_manager_init_v3(&probe.manager, &config), VXML_DIALOG_MANAGER_OK);
        factory.run_ready = NULL;
        start_dialog(&probe);
        tick(&probe);
        check_equal(probe.events[0], "dialog.started");
        tick(&probe);
        check_equal(probe.collect[0].commits, (size_t)1u);
        tick(&probe);
        check_equal(probe.event_count, (size_t)1u);
        complete(&probe.collect[0]);
        probe.full = true;
        size_t processed = 0u;
        check_equal(vxml_dialog_manager_run_ready(&probe.manager, 1u, &processed), VXML_DIALOG_MANAGER_EVENT_FULL);
        const size_t calls = probe.collect[0].progress_calls;
        check_equal(vxml_dialog_manager_run_ready(&probe.manager, 1u, &processed), VXML_DIALOG_MANAGER_EVENT_FULL);
        check_equal(probe.collect[0].progress_calls, calls);
        check_equal(probe.collect[0].barriers, (size_t)0u);
        probe.full = false;
        tick(&probe);
        check_equal(probe.events[1], "dialog.exit");
#ifdef TURBOSCXML_TEST_QUICKJS_FIA
        check_equal(probe.last_exit_value, (int64_t)49);
#endif
        check_equal(probe.collect[0].barriers, (size_t)1u);
        check_equal(probe.collect[0].cancels, (size_t)0u);
    }
    it("rotates past an idle running dialog with a one-row work budget") {
        check_equal(vxml_dialog_manager_init_v3(&probe.manager, &config), VXML_DIALOG_MANAGER_OK);
        start_dialog(&probe); tick(&probe); tick(&probe);
        start_dialog(&probe); tick(&probe);
        check_equal(probe.inits, (size_t)2u);
        tick(&probe); tick(&probe);
        complete(&probe.collect[1]);
        tick(&probe); tick(&probe);
        check_equal(probe.events[2], "dialog.exit");
        check_equal(probe.collect[0].barriers, (size_t)0u);
        check_equal(probe.collect[1].barriers, (size_t)1u);
    }
    it("quiesces producers before close cancels the in-flight collect") {
        check_equal(vxml_dialog_manager_init_v3(&probe.manager, &config), VXML_DIALOG_MANAGER_OK);
        start_dialog(&probe); tick(&probe); tick(&probe);
        vxml_dialog_manager_close(&probe.manager);
        check_equal(probe.collect[0].barriers, (size_t)1u);
        check_equal(probe.collect[0].cancels, (size_t)1u);
    }
    it("quiesces once before normal dialog termination dispatches hangup") {
        check_equal(vxml_dialog_manager_init_v3(&probe.manager, &config), VXML_DIALOG_MANAGER_OK);
        const ccxml_string_view id = start_dialog(&probe);
        tick(&probe); tick(&probe);
        const ccxml_dialog_terminate_request request = {
            .dialog_id = id.data, .dialog_id_size = id.size};
        cflow_statechart_effect_ticket ticket = {0};
        check_equal(vxml_dialog_manager_ccxml_adapter()->prepare_dialog_terminate(
            vxml_dialog_manager_ccxml_user(&probe.manager), &request, &ticket, NULL), SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(probe.collect[0].barriers, (size_t)0u);
        tick(&probe);
        check_equal(probe.events[1], "dialog.exit");
        check_equal(probe.collect[0].barriers, (size_t)1u);
        check_equal(probe.collect[0].cancels, (size_t)1u);
    }
    it("replaces an asynchronously navigating Session and publishes started only once") {
        probe.source = "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
            "<form><field name='value'><grammar type='application/srgs+xml' src='a'/>"
            "<filled><goto next='next.vxml'/></filled></field></form></vxml>";
        probe.next_source = "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
            "<form><block><exit/></block></form></vxml>";
        check_equal(vxml_dialog_manager_init_v3(&probe.manager, &config), VXML_DIALOG_MANAGER_OK);
        start_dialog(&probe); tick(&probe); tick(&probe);
        complete(&probe.collect[0]);
        tick(&probe);
        check_equal(probe.inits, (size_t)2u);
        check_equal(probe.event_count, (size_t)2u);
        check_equal(probe.events[0], "dialog.started");
        check_equal(probe.events[1], "dialog.exit");
        check_equal(probe.collect[0].barriers, (size_t)1u);
        check_equal(probe.collect[1].barriers, (size_t)1u);
    }
    it("publishes exact progress failure and settles the live provider once") {
        check_equal(vxml_dialog_manager_init_v3(&probe.manager, &config), VXML_DIALOG_MANAGER_OK);
        start_dialog(&probe); tick(&probe); tick(&probe);
        probe.progress_status = VXML_LIMIT_EXCEEDED;
        tick(&probe);
        check_equal(probe.events[1], "error.dialog.start");
        check_equal(probe.statuses[1], VXML_LIMIT_EXCEEDED);
        check_equal(probe.collect[0].cancels, (size_t)1u);
    }
    it("rejects partial progress tails and an unpaired quiescence barrier") {
        factory.struct_size = offsetof(vxml_session_factory_v1, quiesce);
        check_equal(vxml_dialog_manager_init_v3(&probe.manager, &config), VXML_DIALOG_MANAGER_INVALID_ARGUMENT);
        factory.struct_size = sizeof(factory);
        factory.quiesce = NULL;
        check_equal(vxml_dialog_manager_init_v3(&probe.manager, &config), VXML_DIALOG_MANAGER_INVALID_ARGUMENT);
    }
    it("keeps the historical script factory prefix on manual asynchronous progress") {
        factory.struct_size = offsetof(vxml_session_factory_v1, run_ready);
        check_equal(vxml_dialog_manager_init_v3(&probe.manager, &config), VXML_DIALOG_MANAGER_OK);
        start_dialog(&probe); tick(&probe);
        size_t processed = 1u;
        check_equal(vxml_dialog_manager_run_ready(&probe.manager, 1u, &processed), VXML_DIALOG_MANAGER_OK);
        check_equal(processed, (size_t)0u);
        check_equal(probe.collect[0].progress_calls, (size_t)0u);
        check_equal(probe.collect[0].commits, (size_t)0u);
    }
#ifdef TURBOSCXML_TEST_QUICKJS_FIA
    it("publishes a caught external failure once despite repeated FULL publication") {
        probe.script = "value = 99;";
        probe.script_status = VXML_SCRIPT_RESOURCE_PROVIDER_ERROR;
        probe.source = "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
            "<form><field name='value'><grammar type='application/srgs+xml' src='a'/>"
            "<filled><script src='scripts/missing.js'/></filled>"
            "<catch event='error.badfetch'><script>value = 13;</script><exit expr='value'/></catch>"
            "</field></form></vxml>";
        check_equal(vxml_dialog_manager_init_v3(&probe.manager, &config), VXML_DIALOG_MANAGER_OK);
        start_dialog(&probe); tick(&probe); tick(&probe);
        complete(&probe.collect[0]);
        probe.full = true;
        size_t processed = 0u;
        check_equal(vxml_dialog_manager_run_ready(&probe.manager, 1u, &processed), VXML_DIALOG_MANAGER_EVENT_FULL);
        check_equal(probe.script_opens, (size_t)1u);
        check_equal(probe.script_closes, (size_t)1u);
        check_equal(probe.last_exit_value, (int64_t)13);
        const size_t progress_calls = probe.collect[0].progress_calls;
        check_equal(vxml_dialog_manager_run_ready(&probe.manager, 1u, &processed), VXML_DIALOG_MANAGER_EVENT_FULL);
        check_equal(probe.collect[0].progress_calls, progress_calls);
        check_equal(probe.script_opens, (size_t)1u);
        probe.full = false;
        tick(&probe);
        check_equal(probe.events[1], "dialog.exit");
        check_equal(probe.statuses[1], VXML_OK);
        check_equal(probe.collect[0].barriers, (size_t)1u);
        check_equal(probe.collect[0].cancels, (size_t)0u);
    }
    it("executes external filled source once despite repeated FULL publication") {
        probe.script = "value = value ** 2;";
        probe.source = "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
            "<form><field name='value'><grammar type='application/srgs+xml' src='a'/>"
            "<filled><script src='scripts/filled.js'/><exit expr='value'/></filled></field></form></vxml>";
        check_equal(vxml_dialog_manager_init_v3(&probe.manager, &config), VXML_DIALOG_MANAGER_OK);
        start_dialog(&probe); tick(&probe); tick(&probe);
        complete(&probe.collect[0]);
        probe.full = true;
        size_t processed = 0u;
        check_equal(vxml_dialog_manager_run_ready(&probe.manager, 1u, &processed), VXML_DIALOG_MANAGER_EVENT_FULL);
        check_equal(probe.script_opens, (size_t)1u);
        check_equal(probe.script_closes, (size_t)1u);
        check_equal(probe.script_uri, "https://voice.example/scripts/filled.js");
        check_equal(probe.last_exit_value, (int64_t)49);
        check_equal(vxml_dialog_manager_run_ready(&probe.manager, 1u, &processed), VXML_DIALOG_MANAGER_EVENT_FULL);
        check_equal(probe.script_opens, (size_t)1u);
        probe.full = false;
        tick(&probe);
        check_equal(probe.events[1], "dialog.exit");
        check_equal(probe.collect[0].barriers, (size_t)1u);
    }
    it("binds the replacement document URI before external script execution after navigation") {
        probe.script = "value = 11;";
        probe.source = "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
            "<form><field name='value'><grammar type='application/srgs+xml' src='a'/>"
            "<filled><script src='scripts/first.js'/><goto next='nested/next.vxml'/></filled>"
            "</field></form></vxml>";
        probe.next_source = "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
            "<form><script src='scripts/second.js'/><block><exit expr='value'/></block></form></vxml>";
        check_equal(vxml_dialog_manager_init_v3(&probe.manager, &config), VXML_DIALOG_MANAGER_OK);
        start_dialog(&probe); tick(&probe); tick(&probe);
        complete(&probe.collect[0]);
        tick(&probe);
        check_equal(probe.inits, (size_t)2u);
        check_equal(probe.script_opens, (size_t)2u);
        check_equal(probe.script_closes, (size_t)2u);
        check_equal(probe.script_uri, "https://voice.example/nested/scripts/second.js");
        check_equal(probe.events[0], "dialog.started");
        check_equal(probe.events[1], "dialog.exit");
        check_equal(probe.collect[0].barriers, (size_t)1u);
        check_equal(probe.collect[1].barriers, (size_t)1u);
    }
    it("closes a dialog after external initialization without retaining its script lease") {
        probe.script = "value = undefined;";
        probe.source = "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
            "<script src='init.js'/><form><field name='value'>"
            "<grammar type='application/srgs+xml' src='a'/></field></form></vxml>";
        check_equal(vxml_dialog_manager_init_v3(&probe.manager, &config), VXML_DIALOG_MANAGER_OK);
        start_dialog(&probe); tick(&probe); tick(&probe);
        check_equal(probe.script_opens, (size_t)1u);
        check_equal(probe.script_closes, (size_t)1u);
        vxml_dialog_manager_close(&probe.manager);
        check_equal(probe.collect[0].barriers, (size_t)1u);
        check_equal(probe.collect[0].cancels, (size_t)1u);
        check_equal(probe.script_closes, (size_t)1u);
    }
    it("uses the submit response effective URI for subsequent external scripts") {
        probe.script = "value = 12;";
        probe.source = "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
            "<form><field name='value'><grammar type='application/srgs+xml' src='a'/>"
            "<filled><submit next='submit' method='post' namelist='value'/></filled>"
            "</field></form></vxml>";
        probe.next_source = "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
            "<script src='scripts/response.js'/><form><block><exit expr='value'/></block></form></vxml>";
        vxml_dialog_manager_config_v4 submit = vxml_dialog_manager_default_config_v4();
        submit.capacity = config.capacity;
        submit.upstream = config.upstream;
        submit.upstream_user = config.upstream_user;
        submit.document_store = config.document_store;
        submit.events = config.events;
        submit.event_user = config.event_user;
        submit.session_factory = config.session_factory;
        submit.session_factory_user = config.session_factory_user;
        submit.submit = &submit_adapter;
        submit.submit_user = &probe;
        check_equal(vxml_dialog_manager_init_v4(&probe.manager, &submit), VXML_DIALOG_MANAGER_OK);
        start_dialog(&probe); tick(&probe); tick(&probe);
        complete(&probe.collect[0]); tick(&probe);
        check_equal(probe.event_count, (size_t)2u);
        check_equal(probe.statuses[1], VXML_OK);
        check_equal(probe.inits, (size_t)2u);
        check_equal(probe.submit_opens, (size_t)1u);
        check_equal(probe.submit_closes, (size_t)1u);
        check_equal(probe.script_opens, (size_t)1u);
        check_equal(probe.script_uri, "https://voice.example/replies/scripts/response.js");
        check_equal(probe.events[1], "dialog.exit");
    }
#endif
}
