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
        .max_fields = 4u, .max_grammar_bytes = 256u};
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
        .initial_root = root, .max_transaction_bytes = 4096u, .max_execution_steps = 128u};
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

spec("Internal QuickJS and CMeta FIA composition") {
    static vxml_program program;
    static vxml_session session;
    static fia_root root;
    static vxml_cmeta_session_options_v1 options;
    before_each() {
        program = (vxml_program){0};
        session = (vxml_session){0};
        root = (fia_root){3, 5};
        options = session_options(&root);
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
}
