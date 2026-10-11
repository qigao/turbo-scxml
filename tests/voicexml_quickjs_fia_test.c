#include "voicexml_quickjs_fia.h"
#include "voicexml_cmeta_internal.h"
#include <cmeta/data_reflect.h>
#include <cmeta_cmeta_data.h>
#include <tinytest.h>
#include <string.h>

typedef struct fia_root { int value, other; } fia_root;
static const cmeta_type_identity root_identity = CMETA_TYPE_ID_ATOM_INIT("test.voicexml.quickjs.fia");
static const cmeta_type_traits root_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY};
static const cmeta_type_desc root_type = {
    .name = "fia_root", .size = sizeof(fia_root), .align = _Alignof(fia_root),
    .kind = CMETA_T_OBJECT, .traits = &root_traits, .identity = &root_identity};
static const cmeta_field_desc layout_fields[] = {
    {"value", "int", offsetof(fia_root, value), sizeof(int), _Alignof(int), &cmeta_type_int, NULL},
    {"other", "int", offsetof(fia_root, other), sizeof(int), _Alignof(int), &cmeta_type_int, NULL}};
static const cmeta_struct_desc layout = {
    .name = "fia_root", .size = sizeof(fia_root), .align = _Alignof(fia_root),
    .fields = layout_fields, .field_count = 2u};
static const cmeta_data_field_desc fields[] = {
    {"test.fia.value", "value", offsetof(fia_root, value), &cmeta_data_int},
    {"test.fia.other", "other", offsetof(fia_root, other), &cmeta_data_int}};
static const cmeta_data_struct_shape shape = {.layout = &layout, .fields = fields, .field_count = 2u};
static const cmeta_data_desc root_data = {
    .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.voicexml.quickjs.fia", .display_name = "fia_root",
    .kind = CMETA_DATA_STRUCT, .storage_type = &root_type, .shape = &shape};

typedef struct fia_owned_root { int value; tstr text; } fia_owned_root;
cmeta_reflect_value(fia_owned_root, "test.voicexml.quickjs.fia.owned",
    cmeta_field(int, value)
    cmeta_data_field(tstr, text, SALTS_TSTR_CMETA_DATA_REF, SALTS_TSTR_CMETA_TYPE_REF)
);

/* The current CMeta FIA root contract requires native storage traits. Keep
 * reflection as the field/lifecycle fact source and provide those exact native
 * callbacks at its existing admission boundary. */
static bool owned_copy(void *to, const void *from) {
    return cmeta_data_trait_copy_construct(cmeta_reflected_data(fia_owned_root), to, from);
}
static void owned_move(void *to, void *from) {
    cmeta_data_trait_move_construct(cmeta_reflected_data(fia_owned_root), to, from);
}
static void owned_destroy(void *value) {
    cmeta_data_trait_destroy(cmeta_reflected_data(fia_owned_root), value);
}
static const cmeta_type_traits owned_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = owned_copy, .move_construct = owned_move, .destroy = owned_destroy};
static const cmeta_type_identity owned_identity = CMETA_TYPE_ID_ATOM_INIT("test.fia.owned.native");
static const cmeta_type_desc owned_type = {
    .name = "fia_owned_root", .size = sizeof(fia_owned_root), .align = _Alignof(fia_owned_root),
    .kind = CMETA_T_OBJECT, .traits = &owned_traits, .identity = &owned_identity};
static cmeta_data_desc owned_data;
static const cmeta_data_desc *owned_descriptor(void) {
    owned_data = *cmeta_reflected_data(fia_owned_root);
    owned_data.abi_version = CMETA_DATA_DESC_ABI_VERSION;
    owned_data.storage_type = &owned_type;
    return &owned_data;
}

#define VXML_BEGIN "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
#define VXML_END "</vxml>"

static vxml_cmeta_compile_options_v1 compile_options(void) {
    const vxml_cmeta_compile_options_v1 options = {
        .abi_version = VXML_CMETA_COMPILE_OPTIONS_ABI_V1, .struct_size = sizeof(options),
        .root = &root_data, .max_expression_bytes = 1024u, .max_expression_instructions = 256u,
        .max_expression_operands = 32u, .max_expression_depth = 16u, .max_path_depth = 8u,
        .max_literal_bytes = 1024u, .max_string_bytes = 1024u, .max_scope_slots = 32u,
        .max_scope_storage_bytes = 4096u, .max_conditional_depth = 8u,
        .max_fields = 4u, .max_grammar_bytes = 256u,
        .max_event_handlers = 8u, .max_event_name_bytes = 64u,
        .max_prompts = 8u, .max_prompt_bytes = 1024u,
        .max_prompt_segments = 16u, .max_dynamic_mark_name_bytes = 64u};
    return options;
}
static vxml_quickjs_fia_limits engine_limits(void) {
    const vxml_quickjs_fia_limits limits = {
        .engine = {1024u, 1024u, 4u * 1024u * 1024u, 256u * 1024u, 100u},
        .values = {16u, 128u, 128u, 4096u, 1024u}};
    return limits;
}
static vxml_status compile(const char *source, vxml_program *program) {
    vxml_cmeta_compile_options_v1 options = compile_options();
    vxml_quickjs_fia_limits limits = engine_limits();
    vxml_diagnostic diagnostic = {0};
    vxml_status status = vxml_compile_cmeta_backend(source, strlen(source), NULL,
        &options, &vxml_quickjs_fia_backend, &limits, program, &diagnostic);
    check(status == VXML_OK, "compile: %s", diagnostic.message);
    return status;
}
static vxml_cmeta_session_options_v1 session_options(const fia_root *root) {
    const vxml_cmeta_session_options_v1 options = {
        .abi_version = VXML_CMETA_SESSION_OPTIONS_ABI_V1, .struct_size = sizeof(options),
        .initial_root = root, .max_transaction_bytes = 4096u, .max_execution_steps = 128u,
        .max_event_counters = 16u, .max_event_name_bytes = 64u, .max_event_dispatch_depth = 8u};
    return options;
}
static const fia_root *committed(const vxml_session *session) {
    const vxml_session_impl *impl = session->impl;
    const vxml_cmeta_session_data *data = impl->profile_data;
    return (const fia_root *)data->committed_root.storage;
}
static vxml_cmeta_value_view exit_value(const vxml_session *session) {
    vxml_cmeta_name_view name = {0};
    vxml_cmeta_value_view value = {0};
    check_equal(vxml_session_cmeta_exit_count(session), (size_t)1u);
    check_equal(vxml_session_cmeta_exit_at(session, 0u, &name, &value), VXML_OK);
    return value;
}

typedef struct collect_probe { uint64_t generation; size_t commits, cancels; } collect_probe;
static void commit_collect(void *user) { ++((collect_probe *)user)->commits; }
static void discard_collect(void *user) { (void)user; }
static void cancel_collect(void *user, uint64_t generation) {
    collect_probe *probe = user;
    check_equal(generation, probe->generation);
    ++probe->cancels;
}
static vxml_status prepare_collect(void *user, const vxml_cmeta_collect_request_v1 *request,
    vxml_cmeta_collect_ticket_v1 *ticket, const char **error) {
    collect_probe *probe = user;
    (void)error;
    probe->generation = request->generation;
    *ticket = (vxml_cmeta_collect_ticket_v1){commit_collect, discard_collect, probe};
    return VXML_OK;
}
static const vxml_cmeta_collect_adapter_v1 collect_adapter = {
    .abi_version = VXML_CMETA_COLLECT_ADAPTER_ABI_V1, .struct_size = sizeof(collect_adapter),
    .capabilities = VXML_CMETA_COLLECT_CAP_SRGS_XML,
    .prepare = prepare_collect, .cancel = cancel_collect};

typedef struct prompt_probe {
    vxml_status status;
    uint64_t generation;
    size_t prepares, commits, discards, cancels;
    char payload[64];
} prompt_probe;
static void commit_prompt(void *user) { ++((prompt_probe *)user)->commits; }
static void discard_prompt(void *user) { ++((prompt_probe *)user)->discards; }
static void cancel_prompt(void *user, uint64_t generation) {
    prompt_probe *probe = user;
    check_equal(generation, probe->generation);
    ++probe->cancels;
}
static vxml_status prepare_prompt(void *user, const vxml_cmeta_prompt_media_request_v1 *request,
    vxml_cmeta_prompt_media_ticket_v1 *ticket, const char **error) {
    prompt_probe *probe = user;
    (void)error;
    ++probe->prepares;
    probe->generation = request->generation;
    check_less(request->segment.payload.size, sizeof(probe->payload));
    memcpy(probe->payload, request->segment.payload.data, request->segment.payload.size);
    probe->payload[request->segment.payload.size] = '\0';
    *ticket = (vxml_cmeta_prompt_media_ticket_v1){commit_prompt, discard_prompt, probe};
    return probe->status;
}
static const vxml_cmeta_prompt_media_adapter_v1 prompt_adapter = {
    .abi_version = VXML_CMETA_PROMPT_MEDIA_ADAPTER_ABI_V1, .struct_size = sizeof(prompt_adapter),
    .capabilities = VXML_CMETA_PROMPT_MEDIA_CAP_TEXT | VXML_CMETA_PROMPT_MEDIA_CAP_MARK,
    .prepare = prepare_prompt, .cancel = cancel_prompt};
static void attach_media(vxml_cmeta_session_options_v1 *options,
    prompt_probe *prompt, collect_probe *collect) {
    static const vxml_cmeta_name_view undefined[] = {{"value", 5u}};
    options->initially_undefined = undefined;
    options->initially_undefined_count = 1u;
    options->collect = &collect_adapter;
    options->collect_user = collect;
    options->prompt_media = &prompt_adapter;
    options->prompt_media_user = prompt;
}

typedef struct script_probe {
    const char *source;
    size_t source_size;
    vxml_script_resource_status status;
    bool partial;
    size_t opens, leases, closes, max_bytes;
    char uri[256];
} script_probe;
static vxml_script_resource_status open_script(void *user, const char *uri, size_t size,
    const char *charset, size_t charset_size, size_t max_bytes, vxml_script_source *out) {
    script_probe *probe = user;
    ++probe->opens;
    check_less(size, sizeof(probe->uri));
    memcpy(probe->uri, uri, size);
    probe->uri[size] = '\0';
    check_equal(charset_size, (size_t)5u);
    check_equal(charset, "UTF-8", 5u);
    probe->max_bytes = max_bytes;
    if (probe->status == VXML_SCRIPT_RESOURCE_OK || probe->partial) {
        ++probe->leases;
        *out = (vxml_script_source){probe->source, probe->source_size, probe};
    }
    return probe->status;
}
static void close_script(void *user, vxml_script_source *source) {
    script_probe *probe = user;
    check_true(source->lease == probe);
    ++probe->closes;
    *source = (vxml_script_source){0};
}
static const vxml_script_resource_adapter_v1 script_adapter = {
    .abi_version = VXML_SCRIPT_RESOURCE_ADAPTER_ABI_V1, .struct_size = sizeof(script_adapter),
    .open = open_script, .close = close_script};
static void script_body(script_probe *probe, const char *source) {
    probe->source = source;
    probe->source_size = strlen(source);
}
static vxml_dialog_manager_status unused_document_open(void *user,
    const char *uri, size_t uri_size, const char *media, size_t media_size,
    size_t max_bytes, vxml_dialog_document *out) {
    (void)user; (void)uri; (void)uri_size; (void)media; (void)media_size; (void)max_bytes;
    *out = (vxml_dialog_document){0};
    return VXML_DIALOG_MANAGER_DOCUMENT_ERROR;
}
static void unused_document_close(void *user, vxml_dialog_document *document) {
    (void)user;
    *document = (vxml_dialog_document){0};
}
static void init_script_resolver(vxml_document_store *store) {
    static const vxml_dialog_document_adapter_v1 documents = {
        .abi_version = VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1, .struct_size = sizeof(documents),
        .open = unused_document_open, .close = unused_document_close};
    vxml_document_store_config_v1 config = {
        .abi_version = VXML_DOCUMENT_STORE_CONFIG_ABI_V1, .struct_size = sizeof(config),
        .application_uri = "https://voice.example/app/root.vxml",
        .capacity = 1u, .max_uri_bytes = 255u, .max_document_bytes = 1024u,
        .max_cache_bytes = 2048u, .documents = &documents};
    config.application_uri_size = strlen(config.application_uri);
    config.voice_limits = vxml_default_limits();
    check_equal(vxml_document_store_init(store, &config), VXML_DOCUMENT_STORE_OK);
}
static void bind_script_document(vxml_session *session, vxml_document_store *store, const char *uri) {
    check_equal(vxml_session_bind_document(session, store, uri, strlen(uri)), VXML_OK);
}

spec("Internal QuickJS and CMeta FIA composition") {
    static vxml_program program;
    static vxml_session session;
    static fia_root root;
    static vxml_cmeta_session_options_v1 options;
    static prompt_probe prompt;
    static collect_probe collect;
    before_each() {
        program = (vxml_program){0};
        session = (vxml_session){0};
        root = (fia_root){3, 5};
        options = session_options(&root);
        prompt = (prompt_probe){0};
        collect = (collect_probe){0};
    }
    after_each() {
        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }
    it("executes actual JavaScript expressions and shares staged writes with the next action") {
        check_equal(compile(VXML_BEGIN "<form><block>"
            "<assign name='value' expr='(other += 2, Math.max(value, other) ** 2)'/>"
            "<if cond='value === 49'><assign name='other' expr='value + other'/></if>"
            "<exit expr='other'/></block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(committed(&session)->value, 49);
        check_equal(committed(&session)->other, 56);
        check_equal(exit_value(&session).data.sint, (int64_t)56);
        check_equal(root.other, 5);
    }
    it("previews a side-effecting prompt condition without changing committed state") {
        attach_media(&options, &prompt, &collect);
        check_equal(compile(VXML_BEGIN "<form><field name='value'>"
            "<prompt cond='++other === 6'>selected</prompt>"
            "<grammar type='application/srgs+xml' src='digits'/></field></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        vxml_cmeta_prompt_view_v1 view = {0};
        check_equal(vxml_session_cmeta_prompt(&session, &view), VXML_OK);
        check_equal(committed(&session)->other, 5);
        check_equal(vxml_session_cmeta_prompt(&session, &view), VXML_OK);
        check_equal(committed(&session)->other, 5);
        check_equal(vxml_session_cmeta_prompt_media_prepare(&session, NULL), VXML_OK);
        check_equal(prompt.payload, "selected");
        check_equal(committed(&session)->other, 5);
        check_equal(vxml_session_cmeta_prompt_media_commit(&session), VXML_OK);
        check_equal(committed(&session)->other, 6);
        check_equal(prompt.commits, (size_t)1u);
    }
    it("discards prompt selection effects and retries from unchanged state") {
        attach_media(&options, &prompt, &collect);
        check_equal(compile(VXML_BEGIN "<form><field name='value'>"
            "<prompt cond='++other === 6'>selected</prompt>"
            "<grammar type='application/srgs+xml' src='digits'/></field></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(vxml_session_cmeta_prompt_media_prepare(&session, NULL), VXML_OK);
        check_equal(vxml_session_cmeta_prompt_media_discard(&session), VXML_OK);
        check_equal(prompt.discards, (size_t)1u);
        check_equal(committed(&session)->other, 5);
        check_equal(vxml_session_cmeta_prompt_media_prepare(&session, NULL), VXML_OK);
        check_equal(vxml_session_cmeta_prompt_media_commit(&session), VXML_OK);
        check_equal(committed(&session)->other, 6);
    }
    it("rolls back prompt expressions when media admission fails") {
        attach_media(&options, &prompt, &collect);
        prompt.status = VXML_ALLOCATION_FAILED;
        check_equal(compile(VXML_BEGIN "<form><field name='value'>"
            "<prompt cond='++other === 6'>selected</prompt>"
            "<grammar type='application/srgs+xml' src='digits'/></field></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(vxml_session_cmeta_prompt_media_prepare(&session, NULL), VXML_ALLOCATION_FAILED);
        check_equal(prompt.prepares, (size_t)1u);
        check_equal(prompt.discards, (size_t)1u);
        check_equal(committed(&session)->other, 5);
    }
    it("selects once across aggregate prompt progress completion and collect admission") {
        attach_media(&options, &prompt, &collect);
        check_equal(compile(VXML_BEGIN "<form><field name='value'>"
            "<prompt cond='++other === 6'>selected</prompt>"
            "<grammar type='application/srgs+xml' src='digits'/>"
            "<filled><script>other += value;</script><exit expr='other'/></filled>"
            "</field></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        bool progressed = false;
        check_equal(vxml_session_cmeta_run_ready(&session, &progressed), VXML_OK);
        check_true(progressed);
        check_equal(prompt.commits, (size_t)1u);
        check_equal(committed(&session)->other, 6);
        vxml_cmeta_prompt_media_completion_v1 complete = {
            .abi_version = VXML_CMETA_PROMPT_MEDIA_COMPLETION_ABI_V1, .struct_size = sizeof(complete),
            .generation = prompt.generation, .outcome = VXML_CMETA_PROMPT_MEDIA_OUTCOME_COMPLETED};
        check_equal(vxml_session_cmeta_prompt_media_try_complete(&session, &complete), VXML_CMETA_PROMPT_MEDIA_INGRESS_ACCEPTED);
        check_equal(vxml_session_cmeta_run_ready(&session, &progressed), VXML_OK);
        check_equal(vxml_session_cmeta_run_ready(&session, &progressed), VXML_OK);
        check_equal(collect.commits, (size_t)1u);
        check_equal(committed(&session)->other, 6);
        const int recognized = 7;
        const vxml_cmeta_collect_completion_v1 result = {
            .abi_version = VXML_CMETA_COLLECT_COMPLETION_ABI_V1, .struct_size = sizeof(result),
            .generation = collect.generation, .data = &cmeta_data_int, .value = &recognized};
        check_equal(vxml_session_cmeta_collect_try_complete(&session, &result), VXML_CMETA_COLLECT_INGRESS_ACCEPTED);
        check_equal(vxml_session_cmeta_run_ready(&session, &progressed), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)13);
    }
    it("commits a false prompt condition once before proceeding directly to collection") {
        attach_media(&options, &prompt, &collect);
        check_equal(compile(VXML_BEGIN "<form><field name='value'>"
            "<prompt cond='(++other, false)'>unselected</prompt>"
            "<grammar type='application/srgs+xml' src='digits'/></field></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        bool progressed = false;
        check_equal(vxml_session_cmeta_run_ready(&session, &progressed), VXML_OK);
        check_true(progressed);
        check_equal(prompt.prepares, (size_t)0u);
        check_equal(collect.commits, (size_t)1u);
        check_equal(committed(&session)->other, 6);
    }
    it("stages dynamic mark expression effects with prompt selection until commit") {
        attach_media(&options, &prompt, &collect);
        check_equal(compile(VXML_BEGIN "<form><field name='value'>"
            "<prompt cond='++other === 6'><mark nameexpr='`mark${++other}`'/></prompt>"
            "<grammar type='application/srgs+xml' src='digits'/></field></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(vxml_session_cmeta_prompt_media_prepare(&session, NULL), VXML_OK);
        check_equal(prompt.payload, "mark7");
        check_equal(committed(&session)->other, 5);
        check_equal(vxml_session_cmeta_prompt_media_discard(&session), VXML_OK);
        check_equal(committed(&session)->other, 5);
        check_equal(vxml_session_cmeta_prompt_media_prepare(&session, NULL), VXML_OK);
        check_equal(prompt.payload, "mark7");
        check_equal(vxml_session_cmeta_prompt_media_commit(&session), VXML_OK);
        check_equal(committed(&session)->other, 7);
    }
    it("discards a prepared prompt transaction when the Session closes") {
        attach_media(&options, &prompt, &collect);
        check_equal(compile(VXML_BEGIN "<form><field name='value'>"
            "<prompt cond='++other === 6'>selected</prompt>"
            "<grammar type='application/srgs+xml' src='digits'/></field></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(vxml_session_cmeta_prompt_media_prepare(&session, NULL), VXML_OK);
        check_equal(committed(&session)->other, 5);
        check_equal(vxml_session_close(&session), VXML_OK);
        check_equal(vxml_session_close(&session), VXML_OK);
        check_equal(vxml_session_get_state(&session), VXML_SESSION_CLOSED);
        check_equal(prompt.discards, (size_t)1u);
        check_equal(prompt.commits, (size_t)0u);
    }
    it("rolls back earlier prompt conditions when a later condition throws") {
        attach_media(&options, &prompt, &collect);
        check_equal(compile(VXML_BEGIN "<form><field name='value'>"
            "<prompt cond='(++other, false)'>first</prompt>"
            "<prompt cond='(()=>{throw new Error()})()'>second</prompt>"
            "<grammar type='application/srgs+xml' src='digits'/></field></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(vxml_session_cmeta_prompt_media_prepare(&session, NULL), VXML_SEMANTIC_ERROR);
        check_equal(prompt.prepares, (size_t)0u);
        check_equal(committed(&session)->other, 5);
    }
    it("executes inline scripts and expressions in the same FIA turn") {
        check_equal(compile(VXML_BEGIN "<form><block>"
            "<script>const twice = x => x * 2; value = twice(other);</script>"
            "<assign name='other' expr='value + 1'/><exit expr='other'/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)11);
        check_equal(committed(&session)->value, 10);
    }
    it("preserves source order across document and form script initializers") {
        check_equal(compile(VXML_BEGIN "<var name='value' expr='10'/>"
            "<script>value += 2; other = value;</script><var name='other' expr='value + 1'/>"
            "<form><script>value = other * 2;</script><var name='other' expr='value + 1'/>"
            "<script>other += value;</script><block><exit expr='other'/></block>"
            "</form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)53);
        check_equal(committed(&session)->value, 3);
    }
    it("executes scripts in conditional and catch action ranges") {
        attach_media(&options, &prompt, &collect);
        check_equal(compile(VXML_BEGIN "<form><field name='value'>"
            "<grammar type='application/srgs+xml' src='digits'/></field>"
            "<catch event='custom'><if cond='other === 5'><script>other = 11;</script>"
            "<else/><script>other = 99;</script></if><script>other += 2;</script>"
            "<exit expr='other'/></catch>"
            "</form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(vxml_session_cmeta_raise(&session, "custom", 6u), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)13);
    }
    it("decodes ordinary XML script text once and preserves CDATA and comment boundaries") {
        check_equal(compile(VXML_BEGIN "<form><block><script>"
            "value = 1 &lt; 2 ? 10 : 20; other = 1<!--join-->2; other += '&amp;lt;'.length;"
            "<![CDATA[other += '&lt;'.length;]]></script>"
            "<exit expr='value + other'/></block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)30);
    }
    it("accepts empty scripts and ignores their JavaScript completion value") {
        check_equal(compile(VXML_BEGIN "<script/><form><script> </script><block>"
            "<script/><script>({ignored: true});</script><exit expr='value'/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)3);
    }
    it("retains typed bindings but not script-local helper declarations between evaluations") {
        check_equal(compile(VXML_BEGIN "<form><block>"
            "<script>var helper = 9; value = helper;</script>"
            "<script>other = typeof helper === 'undefined' ? value : 99;</script>"
            "<exit expr='other'/></block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)9);
    }
    it("rolls back preceding actions and script writes when an inline script throws") {
        check_equal(compile(VXML_BEGIN "<form><block><assign name='value' expr='10'/>"
            "<script>other = 12; throw new Error('failure');</script><exit/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_SEMANTIC_ERROR);
        check_equal(committed(&session)->value, 3);
        check_equal(committed(&session)->other, 5);
    }
    it("rolls back script writes on typed export failure") {
        check_equal(compile(VXML_BEGIN "<form><block>"
            "<script>value = 10; other = 'wrong type';</script><exit/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_SEMANTIC_ERROR);
        check_equal(committed(&session)->value, 3);
        check_equal(committed(&session)->other, 5);
    }
    it("interrupts inline script execution without publishing its writes") {
        check_equal(compile(VXML_BEGIN "<form><block>"
            "<script>value = 99; while (true) {}</script><exit/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_LIMIT_EXCEEDED);
        check_equal(committed(&session)->value, 3);
    }
    it("rolls back document script initialization when a form initializer fails") {
        check_equal(compile(VXML_BEGIN "<script>other = 12;</script><form>"
            "<script>value = 99; throw new Error();</script><block><exit/></block>"
            "</form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_SEMANTIC_ERROR);
        check_equal(committed(&session)->value, 3);
        check_equal(committed(&session)->other, 5);
    }
    it("charges document and form scripts to the FIA execution-step budget") {
        options.max_execution_steps = 1u;
        check_equal(compile(VXML_BEGIN "<script>other = 12;</script><form>"
            "<script>other = 99;</script><block><exit/></block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_LIMIT_EXCEEDED);
        check_equal(committed(&session)->other, 5);
    }
    it("counts script initializers against the compiled action limit") {
        const char *source = VXML_BEGIN "<script>value = 12;</script><form><block><exit/></block></form>" VXML_END;
        vxml_limits bounds = vxml_default_limits();
        const vxml_cmeta_compile_options_v1 config = compile_options();
        const vxml_quickjs_fia_limits limits = engine_limits();
        bounds.max_actions = 2u;
        check_equal(vxml_compile_cmeta_backend(source, strlen(source), &bounds,
            &config, &vxml_quickjs_fia_backend, &limits, &program, NULL), VXML_OK);
        vxml_program_destroy(&program);
        bounds.max_actions = 1u;
        check_equal(vxml_compile_cmeta_backend(source, strlen(source), &bounds,
            &config, &vxml_quickjs_fia_backend, &limits, &program, NULL), VXML_LIMIT_EXCEEDED);
        check_null(program.impl);
    }
    it("rejects unsupported script shapes and invalid source before publication") {
        static const char *sources[] = {
            VXML_BEGIN "<form><block><script src='external.js' srcexpr='value'/></block></form>" VXML_END,
            VXML_BEGIN "<form><block><script src='external.js'>value = 1;</script></block></form>" VXML_END,
            VXML_BEGIN "<form><block><script src='external.js' charset='ASCII'/></block></form>" VXML_END,
            VXML_BEGIN "<form><block><script charset='UTF-8'>value = 1;</script></block></form>" VXML_END,
            VXML_BEGIN "<form><block><script src=''/></block></form>" VXML_END,
            VXML_BEGIN "<form><block><script src='external.js#part'/></block></form>" VXML_END,
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.0' datamodel='cmeta'>"
                "<form><block><script srcexpr=\"'step.js'\"/></block></form>" VXML_END,
            VXML_BEGIN "<form><block><script><assign name='value' expr='1'/></script></block></form>" VXML_END,
            VXML_BEGIN "<form><block><script>const = ;</script></block></form>" VXML_END};
        vxml_cmeta_compile_options_v1 config = compile_options();
        vxml_quickjs_fia_limits limits = engine_limits();
        for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
            check_not_equal(vxml_compile_cmeta_backend(sources[i], strlen(sources[i]), NULL,
                &config, &vxml_quickjs_fia_backend, &limits, &program, NULL), VXML_OK);
            check_null(program.impl);
        }
        const char *source = VXML_BEGIN "<form><block><script>value = 12;</script><exit/></block></form>" VXML_END;
        config.max_expression_bytes = 4u;
        check_equal(vxml_compile_cmeta_backend(source, strlen(source), NULL,
            &config, &vxml_quickjs_fia_backend, &limits, &program, NULL), VXML_LIMIT_EXCEEDED);
        check_null(program.impl);
        config = compile_options();
        check_equal(vxml_compile_cmeta(source, strlen(source), NULL, &config, &program, NULL), VXML_UNSUPPORTED_FEATURE);
        check_null(program.impl);
    }
    it("evaluates filled against an asynchronously recognized value in the same FIA") {
        collect_probe probe = {0};
        static const vxml_cmeta_name_view undefined[] = {{"value", 5u}};
        options.initially_undefined = undefined;
        options.initially_undefined_count = 1u;
        options.collect = &collect_adapter;
        options.collect_user = &probe;
        check_equal(compile(VXML_BEGIN "<form>"
            "<field name='value' cond='other === 5'><grammar type='application/srgs+xml' src='digits'/>"
            "<filled><assign name='other' expr='value ** 2'/><exit expr='`recognized:${other}`'/></filled>"
            "</field></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        bool progressed = false;
        check_equal(vxml_session_cmeta_run_ready(&session, &progressed), VXML_OK);
        check_true(progressed);
        check_equal(probe.commits, (size_t)1u);
        check_equal(vxml_session_cmeta_run_ready(&session, &progressed), VXML_OK);
        check_false(progressed);
        const int recognized = 7;
        const vxml_cmeta_collect_completion_v1 completion = {
            .abi_version = VXML_CMETA_COLLECT_COMPLETION_ABI_V1, .struct_size = sizeof(completion),
            .generation = probe.generation, .data = &cmeta_data_int, .value = &recognized};
        check_equal(vxml_session_cmeta_collect_try_complete(&session, &completion), VXML_CMETA_COLLECT_INGRESS_ACCEPTED);
        check_equal(vxml_session_cmeta_run_ready(&session, &progressed), VXML_OK);
        check_true(progressed);
        check_equal(vxml_session_get_state(&session), VXML_SESSION_EXITED);
        check_equal(committed(&session)->other, 49);
        const vxml_cmeta_value_view value = exit_value(&session);
        check_equal(value.kind, VXML_CMETA_VALUE_STRING);
        check_equal(value.data.string.size, (size_t)13u);
        check_equal(memcmp(value.data.string.data, "recognized:49", 13u), 0);
        vxml_session_destroy(&session);
        check_equal(probe.cancels, (size_t)0u);
    }
    it("exports only the nearest declared scope and preserves the application root") {
        check_equal(compile(VXML_BEGIN "<var name='value' expr='10'/><form><block>"
            "<var name='value' expr='20'/><assign name='other' expr='(++value, value)'/>"
            "<exit expr='value'/></block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(committed(&session)->value, 3);
        check_equal(committed(&session)->other, 21);
        check_equal(exit_value(&session).data.sint, (int64_t)21);
    }
    it("does not let an undeclared later slot hide an outer binding") {
        check_equal(compile(VXML_BEGIN "<var name='value' expr='10'/><form><block>"
            "<assign name='other' expr='value'/><var name='value' expr='20'/>"
            "<exit expr='other'/></block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)10);
    }
    it("keeps a declared unbound slot visible as undefined") {
        check_equal(compile(VXML_BEGIN "<form><block><var name='value'/>"
            "<assign name='other' expr='typeof value === &quot;undefined&quot; ? 12 : 99'/>"
            "<exit expr='other'/></block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)12);
    }
    it("rolls back the FIA turn when a later JavaScript expression throws") {
        check_equal(compile(VXML_BEGIN "<form><block><assign name='value' expr='other += 2'/>"
            "<assign name='other' expr='(()=>{value=99;throw new Error(&quot;private&quot;)})()'/>"
            "<exit/></block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_SEMANTIC_ERROR);
        check_equal(committed(&session)->value, 3);
        check_equal(committed(&session)->other, 5);
    }
    it("discards all expression effects when a typed export fails") {
        check_equal(compile(VXML_BEGIN "<form><block>"
            "<assign name='value' expr='(value=10,other=1.5,20)'/><exit/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_SEMANTIC_ERROR);
        check_equal(committed(&session)->value, 3);
        check_equal(committed(&session)->other, 5);
    }
    it("checks the requested result type before publishing expression effects") {
        check_equal(compile(VXML_BEGIN "<form><block>"
            "<assign name='value' expr='(other=99,&quot;wrong type&quot;)'/><exit/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_SEMANTIC_ERROR);
        check_equal(committed(&session)->other, 5);
    }
    it("interrupts a nonterminating expression and leaves committed state intact") {
        check_equal(compile(VXML_BEGIN "<form><block>"
            "<assign name='value' expr='(()=>{other=99;while(true){};return 1;})()'/><exit/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_LIMIT_EXCEEDED);
        check_equal(committed(&session)->value, 3);
        check_equal(committed(&session)->other, 5);
    }
    it("leaves the default CMeta language admission unchanged") {
        const char *source = VXML_BEGIN "<form><block><assign name='value' expr='Math.max(1,2)'/>"
            "<exit/></block></form>" VXML_END;
        const vxml_cmeta_compile_options_v1 plain_options = compile_options();
        check_not_equal(vxml_compile_cmeta(source, strlen(source), NULL, &plain_options, &program, NULL), VXML_OK);
        check_null(program.impl);
    }
    it("owns JavaScript source and copies the compile-only engine configuration") {
        char source[] = VXML_BEGIN "<form><block><exit expr='other ** 2'/></block></form>" VXML_END;
        vxml_cmeta_compile_options_v1 compile_config = compile_options();
        vxml_quickjs_fia_limits limits = engine_limits();
        vxml_cmeta_expression_backend backend = vxml_quickjs_fia_backend;
        check_equal(vxml_compile_cmeta_backend(source, strlen(source), NULL, &compile_config,
            &backend, &limits, &program, NULL), VXML_OK);
        memset(source, 'x', sizeof(source));
        memset(&limits, 0, sizeof(limits));
        memset(&backend, 0, sizeof(backend));
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)25);
    }
    it("uses ECMAScript truthiness only for conditions") {
        check_equal(compile(VXML_BEGIN "<form><block><if cond='other'>"
            "<assign name='value' expr='9'/></if><exit expr='value'/></block></form>" VXML_END,
            &program), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)9);
    }
    it("fails syntax and independent CMeta source limits before Program publication") {
        const char *source = VXML_BEGIN "<form><block><exit expr='other **'/></block></form>" VXML_END;
        vxml_cmeta_compile_options_v1 compile_config = compile_options();
        const vxml_quickjs_fia_limits limits = engine_limits();
        check_equal(vxml_compile_cmeta_backend(source, strlen(source), NULL, &compile_config,
            &vxml_quickjs_fia_backend, &limits, &program, NULL), VXML_SEMANTIC_ERROR);
        check_null(program.impl);
        source = VXML_BEGIN "<form><block><exit expr='other ** 2'/></block></form>" VXML_END;
        compile_config.max_expression_bytes = 8u;
        check_equal(vxml_compile_cmeta_backend(source, strlen(source), NULL, &compile_config,
            &vxml_quickjs_fia_backend, &limits, &program, NULL), VXML_LIMIT_EXCEEDED);
        check_null(program.impl);
    }
    it("moves an initially unbound managed lexical value without changing the root owner") {
        const char *source = VXML_BEGIN "<form><block><var name='text'/>"
            "<assign name='value' expr='(text=&quot;local&quot;,value+1)'/>"
            "<exit expr='text.toUpperCase()'/></block></form>" VXML_END;
        fia_owned_root owned = {3, tstr_dup("application")};
        check_not_null(owned.text);
        vxml_cmeta_compile_options_v1 compile_config = compile_options();
        compile_config.root = owned_descriptor();
        const vxml_quickjs_fia_limits limits = engine_limits();
        check_equal(vxml_compile_cmeta_backend(source, strlen(source), NULL, &compile_config,
            &vxml_quickjs_fia_backend, &limits, &program, NULL), VXML_OK);
        options.initial_root = &owned;
        const vxml_status init_status = vxml_session_init_cmeta(&session, &program, &options);
        tstr_free(owned.text);
        owned.text = NULL;
        check_equal(init_status, VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        const vxml_cmeta_value_view value = exit_value(&session);
        check_equal(value.kind, VXML_CMETA_VALUE_STRING);
        check_equal(value.data.string.size, (size_t)5u);
        check_equal(memcmp(value.data.string.data, "LOCAL", 5u), 0);
        const vxml_session_impl *impl = session.impl;
        const vxml_cmeta_session_data *runtime = impl->profile_data;
        const fia_owned_root *state = (const fia_owned_root *)runtime->committed_root.storage;
        check_equal(state->text, "application");
        check_equal(state->value, 4);
    }
    it("round trips an owning string through the expression bridge") {
        fia_owned_root owned = {3, tstr_dup("application")};
        unsigned char bound[] = {1u, 1u};
        unsigned char bytes[1024];
        vxml_cmeta_expr_scratch scratch = {bytes, sizeof(bytes), 0u};
        const vxml_cmeta_backend_runtime runtime = {&owned, bound, 2u, NULL, 0u};
        const vxml_quickjs_fia_limits limits = engine_limits();
        vxml_cmeta_expr_diagnostic diagnostic = {0};
        vxml_cmeta_value_view value = {0};
        void *expression = NULL;
        const char *source = "(text='local',value+1)";
        vxml_status status = vxml_quickjs_fia_backend.compile(&limits, source, strlen(source),
            false, owned_descriptor(), &expression, &diagnostic);
        check_equal(status, VXML_OK);
        status = vxml_quickjs_fia_backend.evaluate(expression, VXML_CMETA_VALUE_SINT, &runtime, &scratch, &value);
        vxml_quickjs_fia_backend.destroy(expression);
        expression = NULL;
        check_equal(status, VXML_OK);
        check_equal(owned.text, "local");
        source = "text.toUpperCase()";
        check_equal(vxml_quickjs_fia_backend.compile(&limits, source, strlen(source), false,
            owned_descriptor(), &expression, &diagnostic), VXML_OK);
        status = vxml_quickjs_fia_backend.evaluate(expression, VXML_CMETA_VALUE_STRING, &runtime, &scratch, &value);
        vxml_quickjs_fia_backend.destroy(expression);
        tstr_free(owned.text);
        check_equal(status, VXML_OK);
        check_equal(value.data.string.size, (size_t)5u);
        check_equal(memcmp(value.data.string.data, "LOCAL", 5u), 0);
    }
    it("clears and reconstructs an owning application binding across FIA turns") {
        const char *source = VXML_BEGIN "<form>"
            "<block><assign name='value' expr='(text=undefined,4)'/></block>"
            "<block><assign name='value' expr='(text=&quot;again&quot;,5)'/>"
            "<exit expr='text'/></block></form>" VXML_END;
        fia_owned_root owned = {3, tstr_dup("application")};
        vxml_cmeta_compile_options_v1 compile_config = compile_options();
        compile_config.root = owned_descriptor();
        const vxml_quickjs_fia_limits limits = engine_limits();
        const vxml_status compile_status = vxml_compile_cmeta_backend(source, strlen(source), NULL,
            &compile_config, &vxml_quickjs_fia_backend, &limits, &program, NULL);
        options.initial_root = &owned;
        const vxml_status init_status = compile_status == VXML_OK
            ? vxml_session_init_cmeta(&session, &program, &options) : compile_status;
        tstr_free(owned.text);
        check_equal(init_status, VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        const vxml_cmeta_value_view value = exit_value(&session);
        check_equal(value.kind, VXML_CMETA_VALUE_STRING);
        check_equal(value.data.string.size, (size_t)5u);
        check_equal(memcmp(value.data.string.data, "again", 5u), 0);
    }
    it("discards an owning string replacement when the subsequent FIA action fails") {
        const char *source = VXML_BEGIN "<form><block>"
            "<assign name='value' expr='(text=&quot;replacement&quot;,4)'/>"
            "<assign name='value' expr='(()=>{throw new Error(&quot;private&quot;)})()'/>"
            "<exit/></block></form>" VXML_END;
        fia_owned_root owned = {3, tstr_dup("application")};
        vxml_cmeta_compile_options_v1 compile_config = compile_options();
        compile_config.root = owned_descriptor();
        const vxml_quickjs_fia_limits limits = engine_limits();
        const vxml_status compile_status = vxml_compile_cmeta_backend(source, strlen(source), NULL,
            &compile_config, &vxml_quickjs_fia_backend, &limits, &program, NULL);
        options.initial_root = &owned;
        const vxml_status init_status = compile_status == VXML_OK
            ? vxml_session_init_cmeta(&session, &program, &options) : compile_status;
        tstr_free(owned.text);
        check_equal(init_status, VXML_OK);
        check_equal(vxml_session_start(&session), VXML_SEMANTIC_ERROR);
        const vxml_session_impl *impl = session.impl;
        const vxml_cmeta_session_data *runtime = impl->profile_data;
        const fia_owned_root *state = (const fia_owned_root *)runtime->committed_root.storage;
        check_equal(state->value, 3);
        check_equal(state->text, "application");
    }
    it("owns script string updates and rolls back prompt string replacements on discard") {
        const char *source = VXML_BEGIN "<script>text += ':script';</script><form><field name='value'>"
            "<prompt cond='(text += &quot;:prompt&quot;, true)'>selected</prompt>"
            "<grammar type='application/srgs+xml' src='digits'/></field></form>" VXML_END;
        fia_owned_root owned = {3, tstr_dup("application")};
        vxml_cmeta_compile_options_v1 config = compile_options();
        config.root = owned_descriptor();
        const vxml_quickjs_fia_limits limits = engine_limits();
        const vxml_status compile_status = vxml_compile_cmeta_backend(source, strlen(source), NULL,
            &config, &vxml_quickjs_fia_backend, &limits, &program, NULL);
        attach_media(&options, &prompt, &collect);
        options.initial_root = &owned;
        const vxml_status init_status = compile_status == VXML_OK
            ? vxml_session_init_cmeta(&session, &program, &options) : compile_status;
        tstr_free(owned.text);
        check_equal(init_status, VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        const vxml_cmeta_session_data *runtime = ((vxml_session_impl *)session.impl)->profile_data;
        check_equal(((const fia_owned_root *)runtime->committed_root.storage)->text, "application:script");
        check_equal(vxml_session_cmeta_prompt_media_prepare(&session, NULL), VXML_OK);
        check_equal(((const fia_owned_root *)runtime->committed_root.storage)->text, "application:script");
        check_equal(vxml_session_cmeta_prompt_media_discard(&session), VXML_OK);
        check_equal(((const fia_owned_root *)runtime->committed_root.storage)->text, "application:script");
        check_equal(vxml_session_cmeta_prompt_media_prepare(&session, NULL), VXML_OK);
        check_equal(vxml_session_cmeta_prompt_media_commit(&session), VXML_OK);
        check_equal(((const fia_owned_root *)runtime->committed_root.storage)->text, "application:script:prompt");
    }
}

spec("External scripts inside the QuickJS FIA transaction") {
    static vxml_program program;
    static vxml_session session, peer;
    static vxml_document_store store;
    static fia_root root;
    static vxml_cmeta_session_options_v1 options;
    static vxml_quickjs_fia_scripts scripts;
    static script_probe script, second;
    static collect_probe collect;
    static prompt_probe prompt;
    before_each() {
        program = (vxml_program){0};
        session = peer = (vxml_session){0};
        store = (vxml_document_store){0};
        root = (fia_root){3, 5};
        options = session_options(&root);
        script = second = (script_probe){0};
        collect = (collect_probe){0};
        prompt = (prompt_probe){0};
        scripts = (vxml_quickjs_fia_scripts){&script_adapter, &script, 255u, 1024u};
        init_script_resolver(&store);
        script_body(&script, "other += 1;");
    }
    after_each() {
        vxml_session_destroy(&peer);
        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
        check_equal(vxml_document_store_destroy(&store), VXML_DOCUMENT_STORE_OK);
        check_equal(script.closes, script.leases);
        check_equal(second.closes, second.leases);
    }
    it("executes document form block and conditional external scripts in source order") {
        check_equal(compile(VXML_BEGIN "<script src='step.js'/><form><script src='step.js'/>"
            "<block><script>other *= 2;</script><script src='step.js' charset='uTf-8'/>"
            "<if cond='other === 15'><script src='step.js'/></if><exit expr='other'/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_OK);
        bind_script_document(&session, &store, "https://voice.example/app/current.vxml");
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)16);
        check_equal(script.opens, (size_t)4u);
        check_equal(script.closes, (size_t)4u);
        check_equal(script.uri, "https://voice.example/app/step.js");
    }
    it("shares dynamic URI effects with external source and later inline actions") {
        script_body(&script, "value = other ** 2;");
        check_equal(compile(VXML_BEGIN "<form><block>"
            "<script srcexpr='(++other, `../scripts/${other}.js`)'/>"
            "<script>other = value + 1;</script><exit expr='other'/></block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_OK);
        bind_script_document(&session, &store, "https://voice.example/app/dialog/current.vxml");
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)37);
        check_equal(script.uri, "https://voice.example/app/scripts/6.js");
        check_equal(script.closes, (size_t)1u);
    }
    it("decodes reference entities once and accepts an empty external source") {
        script.source = NULL;
        script.source_size = 0u;
        check_equal(compile(VXML_BEGIN "<form><block>"
            "<script src='empty.js?a=1&amp;b=2' charset='UTF&#45;8'/><exit expr='value'/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_OK);
        bind_script_document(&session, &store, "https://voice.example/app/current.vxml");
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)3);
        check_equal(script.uri, "https://voice.example/app/empty.js?a=1&b=2");
        check_equal(script.closes, (size_t)1u);
    }
    it("copies adapter and limits while borrowing the provider user through close") {
        vxml_script_resource_adapter_v1 adapter = script_adapter;
        scripts.resources = &adapter;
        scripts.max_source_bytes = 40u;
        check_equal(compile(VXML_BEGIN "<form><block><script src='step.js'/><exit expr='value'/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_OK);
        adapter = (vxml_script_resource_adapter_v1){0};
        scripts = (vxml_quickjs_fia_scripts){0};
        script_body(&script, "value = 17;");
        bind_script_document(&session, &store, "https://voice.example/app/current.vxml");
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)17);
        check_equal(script.max_bytes, (size_t)40u);
    }
    it("keeps resources and document bases independent for Sessions sharing a Program") {
        check_equal(compile(VXML_BEGIN "<form><block><script src='step.js'/><exit expr='value'/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        script_body(&script, "value = 11;");
        script_body(&second, "value = 22;");
        check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_OK);
        scripts.user = &second;
        check_equal(vxml_session_init_quickjs_fia(&peer, &program, &options, &scripts), VXML_OK);
        bind_script_document(&session, &store, "https://voice.example/one/current.vxml");
        bind_script_document(&peer, &store, "https://voice.example/two/current.vxml");
        check_equal(vxml_session_start(&peer), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)11);
        check_equal(exit_value(&peer).data.sint, (int64_t)22);
        check_equal(script.uri, "https://voice.example/one/step.js");
        check_equal(second.uri, "https://voice.example/two/step.js");
    }
    it("owns the bound URI and keeps a valid binding when a replacement is rejected") {
        char uri[] = "https://voice.example/app/current.vxml";
        check_equal(compile(VXML_BEGIN "<form><block><script src='step.js'/><exit/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_OK);
        bind_script_document(&session, &store, uri);
        memset(uri, 'x', strlen(uri));
        check_equal(vxml_session_bind_document(&session, &store, "", 0u), VXML_INVALID_ARGUMENT);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(script.uri, "https://voice.example/app/step.js");
        check_equal(vxml_session_bind_document(&session, &store, "next", 4u), VXML_INVALID_STATE);
        check_equal(vxml_session_close(&session), VXML_OK);
        check_null(((vxml_session_impl *)session.impl)->document_context.uri);
        check_equal(vxml_session_close(&session), VXML_OK);
    }
    it("fails missing provider or document binding before resource admission") {
        check_equal(compile(VXML_BEGIN "<form><block><script src='step.js'/><exit/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, NULL), VXML_OK);
        bind_script_document(&session, &store, "https://voice.example/app/current.vxml");
        check_equal(vxml_session_start(&session), VXML_INVALID_CONTRACT);
        check_equal(vxml_session_init_quickjs_fia(&peer, &program, &options, &scripts), VXML_OK);
        check_equal(vxml_session_start(&peer), VXML_INVALID_CONTRACT);
        check_equal(script.opens, (size_t)0u);
    }
    it("rejects invalid dynamic targets and rolls back their writes before provider admission") {
        static const char *sources[] = {
            VXML_BEGIN "<form><block><script srcexpr=\"(++other, 4)\"/><exit/></block></form>" VXML_END,
            VXML_BEGIN "<form><block><script srcexpr=\"(++other, '')\"/><exit/></block></form>" VXML_END,
            VXML_BEGIN "<form><block><script srcexpr=\"(++other, 'a\\0b')\"/><exit/></block></form>" VXML_END,
            VXML_BEGIN "<form><block><script srcexpr=\"(++other, 'step.js#part')\"/><exit/></block></form>" VXML_END};
        for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
            check_equal(compile(sources[i], &program), VXML_OK);
            check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_OK);
            bind_script_document(&session, &store, "https://voice.example/app/current.vxml");
            check_equal(vxml_session_start(&session), VXML_SEMANTIC_ERROR);
            check_equal(committed(&session)->other, 5);
            check_equal(script.opens, (size_t)0u);
            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }
    }
    it("enforces the source bound exactly and closes the rejected source lease") {
        script_body(&script, "value = 11;");
        scripts.max_source_bytes = script.source_size;
        check_equal(compile(VXML_BEGIN "<form><block><script src='step.js'/><exit expr='value'/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_OK);
        bind_script_document(&session, &store, "https://voice.example/app/current.vxml");
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)11);
        --scripts.max_source_bytes;
        check_equal(vxml_session_init_quickjs_fia(&peer, &program, &options, &scripts), VXML_OK);
        bind_script_document(&peer, &store, "https://voice.example/app/current.vxml");
        check_equal(vxml_session_start(&peer), VXML_LIMIT_EXCEEDED);
        check_equal(committed(&peer)->value, 3);
        check_equal(script.opens, (size_t)2u);
        check_equal(script.closes, (size_t)2u);
    }
    it("enforces URI bounds before provider admission") {
        scripts.max_uri_bytes = 8u;
        check_equal(compile(VXML_BEGIN "<form><block><script src='step.js'/><exit/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_OK);
        bind_script_document(&session, &store, "https://voice.example/app/current.vxml");
        check_equal(vxml_session_start(&session), VXML_LIMIT_EXCEEDED);
        check_equal(script.opens, (size_t)0u);
    }
    it("rolls back URI and earlier action effects on source syntax execution and typed export failures") {
        static const char *bodies[] = {"const = ;", "value = 99; throw new Error();", "value = 99; other = 'wrong type';"};
        check_equal(compile(VXML_BEGIN "<form><block><assign name='value' expr='44'/>"
            "<script srcexpr=\"(++other, 'step.js')\"/><exit/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        for (size_t i = 0u; i < sizeof(bodies) / sizeof(bodies[0]); ++i) {
            script_body(&script, bodies[i]);
            check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_OK);
            bind_script_document(&session, &store, "https://voice.example/app/current.vxml");
            check_equal(vxml_session_start(&session), VXML_SEMANTIC_ERROR);
            check_equal(committed(&session)->value, 3);
            check_equal(committed(&session)->other, 5);
            check_equal(script.closes, i + 1u);
            vxml_session_destroy(&session);
        }
    }
    it("closes invalid source bytes without publishing dynamic URI effects") {
        static const char body[] = "value=9;\0other=8;";
        script.source = body;
        script.source_size = sizeof(body) - 1u;
        check_equal(compile(VXML_BEGIN "<form><block><script srcexpr=\"(++other, 'step.js')\"/>"
            "<exit/></block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_OK);
        bind_script_document(&session, &store, "https://voice.example/app/current.vxml");
        check_equal(vxml_session_start(&session), VXML_SEMANTIC_ERROR);
        check_equal(committed(&session)->other, 5);
        check_equal(script.closes, (size_t)1u);
    }
    it("closes a partial failure lease exactly once and rolls back the entire action range") {
        script.status = VXML_SCRIPT_RESOURCE_PROVIDER_ERROR;
        script.partial = true;
        check_equal(compile(VXML_BEGIN "<form><block><assign name='value' expr='44'/>"
            "<script srcexpr=\"(++other, 'step.js')\"/><exit/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_OK);
        bind_script_document(&session, &store, "https://voice.example/app/current.vxml");
        check_equal(vxml_session_start(&session), VXML_SEMANTIC_ERROR);
        check_equal(committed(&session)->value, 3);
        check_equal(committed(&session)->other, 5);
        check_equal(script.opens, (size_t)1u);
        check_equal(script.closes, (size_t)1u);
    }
    it("interrupts external source execution and closes its lease before Session destruction") {
        script_body(&script, "value = 99; while (true) {}");
        check_equal(compile(VXML_BEGIN "<form><block><script src='step.js'/><exit/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_OK);
        bind_script_document(&session, &store, "https://voice.example/app/current.vxml");
        check_equal(vxml_session_start(&session), VXML_LIMIT_EXCEEDED);
        check_equal(committed(&session)->value, 3);
        check_equal(script.closes, (size_t)1u);
    }
    it("rolls back typed writes on external script heap exhaustion and closes its lease") {
        const char *source = VXML_BEGIN "<form><block><script src='heap.js'/><exit/>"
            "</block></form>" VXML_END;
        vxml_cmeta_compile_options_v1 config = compile_options();
        vxml_quickjs_fia_limits limits = engine_limits();
        limits.engine.max_eval_milliseconds = 5000u;
        script_body(&script, "value = 99; let large = 'x'.repeat(64 * 1024 * 1024);");
        check_equal(vxml_compile_cmeta_backend(source, strlen(source), NULL, &config,
            &vxml_quickjs_fia_backend, &limits, &program, NULL), VXML_OK);
        check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_OK);
        bind_script_document(&session, &store, "https://voice.example/app/current.vxml");
        check_equal(vxml_session_start(&session), VXML_SEMANTIC_ERROR);
        check_equal(committed(&session)->value, 3);
        check_equal(script.closes, (size_t)1u);
    }
    it("rolls back owning string exports when an action after external source fails") {
        const char *source = VXML_BEGIN "<form><block><script src='owning.js'/>"
            "<script>throw new Error();</script><exit/></block></form>" VXML_END;
        vxml_cmeta_compile_options_v1 config = compile_options();
        const vxml_quickjs_fia_limits limits = engine_limits();
        config.root = owned_descriptor();
        check_equal(vxml_compile_cmeta_backend(source, strlen(source), NULL, &config,
            &vxml_quickjs_fia_backend, &limits, &program, NULL), VXML_OK);
        fia_owned_root initial = {3, tstr_dup("original")};
        check_not_null(initial.text);
        options.initial_root = &initial;
        const vxml_status status = vxml_session_init_quickjs_fia(&session, &program, &options, &scripts);
        tstr_free(initial.text);
        check_equal(status, VXML_OK);
        script_body(&script, "text += ':external'; value = 11;");
        bind_script_document(&session, &store, "https://voice.example/app/current.vxml");
        check_equal(vxml_session_start(&session), VXML_SEMANTIC_ERROR);
        const vxml_cmeta_session_data *state = ((vxml_session_impl *)session.impl)->profile_data;
        check_equal(((const fia_owned_root *)state->committed_root.storage)->text, "original");
        check_equal(((const fia_owned_root *)state->committed_root.storage)->value, 3);
        check_equal(script.closes, (size_t)1u);
    }
    it("rejects incomplete provider configuration without publishing a Session") {
        check_equal(compile(VXML_BEGIN "<form><block><script src='step.js'/><exit/>"
            "</block></form>" VXML_END, &program), VXML_OK);
        vxml_script_resource_adapter_v1 adapter = script_adapter;
        scripts.resources = &adapter;
        adapter.close = NULL;
        check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_INVALID_CONTRACT);
        check_null(session.impl);
        adapter = script_adapter;
        scripts.max_uri_bytes = SIZE_MAX;
        check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_INVALID_CONTRACT);
        check_null(session.impl);
        check_equal(script.opens, (size_t)0u);
    }
    it("executes external scripts in a scoped catch") {
        attach_media(&options, &prompt, &collect);
        check_equal(compile(VXML_BEGIN "<form><field name='value'>"
            "<grammar type='application/srgs+xml' src='digits'/></field>"
            "<catch event='custom'><script src='step.js'/><exit expr='other'/></catch>"
            "</form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_OK);
        bind_script_document(&session, &store, "https://voice.example/app/current.vxml");
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(vxml_session_cmeta_raise(&session, "custom", 6u), VXML_OK);
        check_equal(exit_value(&session).data.sint, (int64_t)6);
        check_equal(script.closes, (size_t)1u);
    }
    it("executes an external filled script after a delayed collect completion") {
        attach_media(&options, &prompt, &collect);
        script_body(&script, "value = value ** 2;");
        check_equal(compile(VXML_BEGIN "<form><field name='value'>"
            "<grammar type='application/srgs+xml' src='digits'/>"
            "<filled><script src='step.js'/><exit expr='value'/></filled>"
            "</field></form>" VXML_END, &program), VXML_OK);
        check_equal(vxml_session_init_quickjs_fia(&session, &program, &options, &scripts), VXML_OK);
        bind_script_document(&session, &store, "https://voice.example/app/current.vxml");
        check_equal(vxml_session_start(&session), VXML_OK);
        bool progressed = false;
        check_equal(vxml_session_cmeta_run_ready(&session, &progressed), VXML_OK);
        check_true(progressed);
        check_equal(script.opens, (size_t)0u);
        check_equal(vxml_session_cmeta_run_ready(&session, &progressed), VXML_OK);
        check_false(progressed);
        const int recognized = 7;
        const vxml_cmeta_collect_completion_v1 completion = {
            .abi_version = VXML_CMETA_COLLECT_COMPLETION_ABI_V1, .struct_size = sizeof(completion),
            .generation = collect.generation, .data = &cmeta_data_int, .value = &recognized};
        check_equal(vxml_session_cmeta_collect_try_complete(&session, &completion), VXML_CMETA_COLLECT_INGRESS_ACCEPTED);
        check_equal(vxml_session_cmeta_run_ready(&session, &progressed), VXML_OK);
        check_true(progressed);
        check_equal(exit_value(&session).data.sint, (int64_t)49);
        check_equal(script.closes, (size_t)1u);
    }
}
