#include <voicexml/cmeta.h>
#include <voicexml/dialog_manager.h>
#include <voicexml/document_store.h>
#include <tinytest.h>
#include <string.h>

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
};

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
        .max_fields = 4u, .max_grammar_bytes = 256u};
    vxml_status status = vxml_compile_cmeta(bytes, size, NULL, &options, program, diagnostic);
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
        .collect = &collect_adapter, .collect_user = collect};
    vxml_status status = vxml_session_init_cmeta(session, program, &options);
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
    ++collect->progress_calls;
    *progressed = false;
    if (probe->progress_status != VXML_OK) return probe->progress_status;
    return vxml_session_cmeta_run_ready(session, progressed);
}
static void quiesce_session(void *user, vxml_session *session) {
    collect_probe *collect = find_collect(user, session);
    check_not_null(collect);
    check_not_null(session->impl);
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
            "<filled><exit/></filled></field></form></vxml>";
        config = setup(&probe, &factory);
    }
    after_each() {
        if (probe.manager.impl != NULL) {
            vxml_dialog_manager_close(&probe.manager);
            check_equal(vxml_dialog_manager_destroy(&probe.manager), VXML_DIALOG_MANAGER_OK);
        }
        check_equal(vxml_document_store_destroy(&probe.store), VXML_DOCUMENT_STORE_OK);
        check_equal(probe.opens, probe.closes);
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
}
