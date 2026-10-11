#include <ccxml/quickjs.h>
#include <voicexml/dialog_manager.h>
#include <cmeta/cmeta.h>
#include "tinytest.h"
#include <string.h>

enum { TEST_TEXT_CAPACITY = 95u };
typedef struct test_text { size_t size; char data[TEST_TEXT_CAPACITY + 1u]; } test_text;
typedef struct test_state { test_text id; int count; } test_state;

static void text_move(void *destination, void *source) {
    memcpy(destination, source, sizeof(test_text));
    memset(source, 0, sizeof(test_text));
}

static void text_destroy(void *object) {
    memset(object, 0, sizeof(test_text));
}

static const cmeta_type_traits text_traits = {
    .flags = CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .move_construct = text_move,
    .destroy = text_destroy};

static const cmeta_type_desc text_type = {
    .name = "ccxml_test_text",
    .size = sizeof(test_text),
    .align = _Alignof(test_text),
    .kind = CMETA_T_OBJECT,
    .traits = &text_traits};

static bool text_is_zero(const void *object) {
    return object != NULL && ((const test_text *)object)->size == 0u;
}

static cmeta_status text_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
    test_text *text = (test_text *)object;
    if (text == NULL || (data == NULL && size != 0u))
        return CMETA_INVALID_ARGUMENT;
    if (size > max_bytes || size > TEST_TEXT_CAPACITY)
        return CMETA_CAPACITY_EXCEEDED;
    if (size != 0u) memcpy(text->data, data, size);
    text->data[size] = '\0';
    text->size = size;
    return CMETA_OK;
}

static void text_restore_zero(void *object) {
    if (object != NULL) memset(object, 0, sizeof(test_text));
}

static cmeta_status text_init_zero(void *object) {
    if (object == NULL) return CMETA_INVALID_ARGUMENT;
    text_restore_zero(object);
    return CMETA_OK;
}

static cmeta_status text_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
    const test_text *text = (const test_text *)object;
    if (text == NULL || out_data == NULL || out_size == NULL ||
        text->size > TEST_TEXT_CAPACITY)
        return CMETA_INVALID_ARGUMENT;
    *out_data = (const unsigned char *)text->data;
    *out_size = text->size;
    return CMETA_OK;
}

static const cmeta_data_buffer_shape text_shape = {
    .ownership = CMETA_DATA_BUFFER_OWNED};

static const cmeta_data_buffer_ops text_ops = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &text_type,
    .ownership = CMETA_DATA_BUFFER_OWNED,
    .is_zero = text_is_zero,
    .assign = text_assign,
    .restore_zero = text_restore_zero,
    .read = text_read,
    .init_zero = text_init_zero,
    .move = text_move};

static const cmeta_data_desc text_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ccxml.text",
    .display_name = "CCXML test text",
    .kind = CMETA_DATA_STRING,
    .storage_type = &text_type,
    .shape = &text_shape,
    .buffer_ops = &text_ops};


static const cmeta_type_traits state_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY};
static const cmeta_type_desc state_type = {
    .name = "ccxml_quickjs_test_state", .size = sizeof(test_state),
    .align = _Alignof(test_state), .kind = CMETA_T_OBJECT, .traits = &state_traits};
static const cmeta_data_field_desc state_fields[] = {
    {"ccxml.quickjs.id", "id", offsetof(test_state, id), &text_desc},
    {"ccxml.quickjs.count", "count", offsetof(test_state, count), &cmeta_data_int}};
static const cmeta_field_desc state_layout_fields[] = {
    {"id", "test_text", offsetof(test_state, id), sizeof(test_text),
        _Alignof(test_text), &text_type, NULL},
    {"count", "int", offsetof(test_state, count), sizeof(int),
        _Alignof(int), &cmeta_type_int, NULL}};
static const cmeta_struct_desc state_layout = {
    .name = "test_state", .size = sizeof(test_state), .align = _Alignof(test_state),
    .fields = state_layout_fields, .field_count = 2u};
static const cmeta_data_struct_shape state_shape = {
    .layout = &state_layout, .fields = state_fields, .field_count = 2u};
static const cmeta_data_desc state_desc = {
    .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "ccxml.quickjs.state", .display_name = "CCXML QuickJS test state",
    .kind = CMETA_DATA_STRUCT, .storage_type = &state_type, .shape = &state_shape};

static const ccxml_event event = {
    .name = "go", .name_size = 2u, .connection_id = "call-7", .connection_id_size = 6u};

typedef struct dialog_probe {
    bool reject;
    size_t committed, discarded, opened, closed, events;
} dialog_probe;
static void commit_accept(void *user) { ++((dialog_probe *)user)->committed; }
static void discard_accept(void *user) { ++((dialog_probe *)user)->discarded; }
static void close_provider(void *user) { (void)user; }
static bool quiescent(void *user) { (void)user; return true; }
static scxml_adapter_status prepare_accept(void *user, const ccxml_accept_request *request,
    cflow_statechart_effect_ticket *ticket, const char **error) {
    dialog_probe *probe = user;
    (void)request;
    if (error != NULL) *error = NULL;
    if (probe->reject) return SCXML_ADAPTER_ERROR_EXECUTION;
    *ticket = (cflow_statechart_effect_ticket){commit_accept, discard_accept, probe};
    return SCXML_ADAPTER_ACCEPTED;
}
static const ccxml_telephony_adapter_v1 telephony = {
    .abi_version = CCXML_TELEPHONY_ADAPTER_ABI_V1, .struct_size = sizeof(telephony),
    .prepare_accept = prepare_accept, .close = close_provider, .is_quiescent = quiescent};
static vxml_dialog_manager_status open_document(void *user, const char *source,
    size_t size, const char *media, size_t media_size, size_t max_bytes,
    vxml_dialog_document *out) {
    static const char document[] =
        "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
        "<form><block><exit/></block></form></vxml>";
    (void)media; (void)media_size;
    if (size != sizeof("app7.vxml")-1u || memcmp(source, "app7.vxml", size) != 0 ||
        sizeof(document)-1u > max_bytes) return VXML_DIALOG_MANAGER_DOCUMENT_ERROR;
    ++((dialog_probe *)user)->opened;
    *out = (vxml_dialog_document){.data = document, .size = sizeof(document)-1u, .lease = user};
    return VXML_DIALOG_MANAGER_OK;
}
static void close_document(void *user, vxml_dialog_document *document) {
    ++((dialog_probe *)user)->closed;
    *document = (vxml_dialog_document){0};
}
static vxml_dialog_event_sink_status publish_event(void *user, const vxml_dialog_event_v1 *value) {
    (void)value;
    ++((dialog_probe *)user)->events;
    return VXML_DIALOG_EVENT_ACCEPTED;
}
static const vxml_dialog_document_adapter_v1 documents = {
    .abi_version = VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1, .struct_size = sizeof(documents),
    .open = open_document, .close = close_document};
static const vxml_dialog_event_sink_v1 events = {
    .abi_version = VXML_DIALOG_EVENT_SINK_ABI_V1, .struct_size = sizeof(events),
    .try_publish = publish_event};

spec("CCXML QuickJS-NG expression adapter") {
    static test_state state;
    static ccxml_quickjs_datamodel owner;
    static ccxml_quickjs_datamodel_config_v1 config;
    static ccxml_string_expression expression;
    static ccxml_condition condition;
    static ccxml_program program;
    static ccxml_session session;
    static vxml_dialog_manager manager;
    static dialog_probe provider;
    static const ccxml_datamodel_adapter_v1 *adapter;
    static const char *error;

    before_each() {
        state = (test_state){.id = {.size = 3u, .data = "old"}, .count = 7};
        owner = (ccxml_quickjs_datamodel){0};
        expression = (ccxml_string_expression){0};
        condition = (ccxml_condition){0};
        program = (ccxml_program){0};
        session = (ccxml_session){0};
        manager = (vxml_dialog_manager){0};
        provider = (dialog_probe){0};
        config = ccxml_quickjs_default_datamodel_config();
        config.state_config.root = &state_desc;
        config.state_config.state = &state;
        config.state_config.max_string_bytes = TEST_TEXT_CAPACITY;
        adapter = ccxml_quickjs_datamodel_adapter();
        error = NULL;
    }
    after_each() {
        if (session.impl != NULL) check_equal(ccxml_session_destroy(&session), CCXML_OK);
        if (manager.impl != NULL) {
            vxml_dialog_manager_close(&manager);
            check_equal(vxml_dialog_manager_destroy(&manager), VXML_DIALOG_MANAGER_OK);
        }
        adapter->destroy_string_expression(&owner, &expression);
        adapter->destroy_condition(&owner, &condition);
        ccxml_quickjs_datamodel_destroy(&owner);
        ccxml_program_destroy(&program);
    }

    it("evaluates JavaScript concatenation against current state and event fields") {
        static const char source[] = "id + '/' + (count + 1) + '/' + event$.connectionid + '.vxml'";
        ccxml_string_view value = {0};
        check_equal(ccxml_quickjs_datamodel_init(&owner, &config), CCXML_OK);
        check_equal(adapter->compile_string_expression(&owner, source, sizeof(source)-1u,
            &expression, &error), SCXML_ADAPTER_ACCEPTED);
        state.count = 8;
        check_equal(adapter->evaluate_string_expression(&owner, &expression, &event,
            &value, &error), SCXML_ADAPTER_ACCEPTED);
        check_equal(value.data, "old/9/call-7.vxml");
        check_equal(value.size, strlen(value.data));
    }

    for (size_t row = 0u; row < 4u; ++row) {
        it("uses JavaScript truth conversion for condition %zu", row) {
            static const char *sources[] = {"count === 7 && event$.name === 'go'", "0", "({})", "''"};
            static const bool expected[] = {true, false, true, false};
            bool value = false;
            check_equal(ccxml_quickjs_datamodel_init(&owner, &config), CCXML_OK);
            check_equal(adapter->compile_condition(&owner, sources[row], strlen(sources[row]),
                &condition, &error), SCXML_ADAPTER_ACCEPTED);
            check_equal(adapter->evaluate_condition(&owner, &condition, &event,
                &value, &error), SCXML_ADAPTER_ACCEPTED);
            check_equal(value, expected[row]);
        }
    }

    it("discards expression mutations and recreates clean globals each time") {
        static const char source[] = "(count += 1, globalThis.temp = (globalThis.temp || 0) + 1, '' + count + ':' + temp)";
        ccxml_string_view value = {0};
        check_equal(ccxml_quickjs_datamodel_init(&owner, &config), CCXML_OK);
        check_equal(adapter->compile_string_expression(&owner, source, sizeof(source)-1u,
            &expression, &error), SCXML_ADAPTER_ACCEPTED);
        for (size_t i = 0u; i < 2u; ++i) {
            check_equal(adapter->evaluate_string_expression(&owner, &expression, &event,
                &value, &error), SCXML_ADAPTER_ACCEPTED);
            check_equal(value.data, "8:1");
            check_equal(state.count, 7);
        }
    }

    for (size_t row = 0u; row < 4u; ++row) {
        it("rejects non-string or malformed result %zu", row) {
            static const char *sources[] = {"42", "({uri: 'x'})", "''", "'a\\0b'"};
            ccxml_string_view value = {0};
            check_equal(ccxml_quickjs_datamodel_init(&owner, &config), CCXML_OK);
            check_equal(adapter->compile_string_expression(&owner, sources[row], strlen(sources[row]),
                &expression, &error), SCXML_ADAPTER_ACCEPTED);
            check_equal(adapter->evaluate_string_expression(&owner, &expression, &event,
                &value, &error), SCXML_ADAPTER_INVALID_CONTRACT);
            check_null(value.data);
            check_equal(value.size, (size_t)0);
        }
    }

    it("interrupts a loop and then evaluates successfully in a fresh context") {
        static const char source[] = "(() => { if (count === 7) { for (;;) {} } return 'ok'; })()";
        ccxml_string_view value = {0};
        config.max_eval_milliseconds = 20u;
        check_equal(ccxml_quickjs_datamodel_init(&owner, &config), CCXML_OK);
        check_equal(adapter->compile_string_expression(&owner, source, sizeof(source)-1u,
            &expression, &error), SCXML_ADAPTER_ACCEPTED);
        check_equal(adapter->evaluate_string_expression(&owner, &expression, &event,
            &value, &error), SCXML_ADAPTER_ERROR_EXECUTION);
        check_null(value.data);
        state.count = 0;
        check_equal(adapter->evaluate_string_expression(&owner, &expression, &event,
            &value, &error), SCXML_ADAPTER_ACCEPTED);
        check_equal(value.data, "ok");
    }

    it("keeps script exceptions out of diagnostics and application state") {
        static const char source[] = "(() => { count = 100; throw new Error('private-value'); })()";
        ccxml_string_view value = {0};
        check_equal(ccxml_quickjs_datamodel_init(&owner, &config), CCXML_OK);
        check_equal(adapter->compile_string_expression(&owner, source, sizeof(source)-1u,
            &expression, &error), SCXML_ADAPTER_ACCEPTED);
        check_equal(adapter->evaluate_string_expression(&owner, &expression, &event,
            &value, &error), SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(state.count, 7);
        check_equal(error, "CCXML QuickJS expression failed or exceeded limits");
    }

    it("bounds live expression handles and recovers capacity on destroy") {
        config.max_expressions = 1u;
        check_equal(ccxml_quickjs_datamodel_init(&owner, &config), CCXML_OK);
        check_equal(adapter->compile_condition(&owner, "true", 4u, &condition, &error), SCXML_ADAPTER_ACCEPTED);
        check_equal(adapter->compile_string_expression(&owner, "'ok'", 4u, &expression, &error), SCXML_ADAPTER_FULL);
        check_null(expression.impl);
        adapter->destroy_condition(&owner, &condition);
        check_equal(adapter->compile_string_expression(&owner, "'ok'", 4u, &expression, &error), SCXML_ADAPTER_ACCEPTED);
    }

    it("rejects a result beyond the configured string bound and recovers") {
        static const char source[] = "'x'.repeat(count)";
        ccxml_string_view value = {0};
        check_equal(ccxml_quickjs_datamodel_init(&owner, &config), CCXML_OK);
        check_equal(adapter->compile_string_expression(&owner, source, sizeof(source)-1u,
            &expression, &error), SCXML_ADAPTER_ACCEPTED);
        state.count = TEST_TEXT_CAPACITY + 1;
        check_equal(adapter->evaluate_string_expression(&owner, &expression, &event,
            &value, &error), SCXML_ADAPTER_ERROR_EXECUTION);
        check_null(value.data);
        state.count = TEST_TEXT_CAPACITY;
        check_equal(adapter->evaluate_string_expression(&owner, &expression, &event,
            &value, &error), SCXML_ADAPTER_ACCEPTED);
        check_equal(value.size, (size_t)TEST_TEXT_CAPACITY);
    }

    it("rejects oversized source before reading beyond its supplied view") {
        check_equal(ccxml_quickjs_datamodel_init(&owner, &config), CCXML_OK);
        check_equal(adapter->compile_condition(&owner, "true",
            config.max_expression_bytes + 1u, &condition, &error),
            SCXML_ADAPTER_INVALID_CONTRACT);
        check_null(condition.impl);
    }

    it("exposes absent event IDs as undefined and rejects invalid byte views") {
        static const char source[] =
            "event$.connectionid === undefined && event$.conferenceid === undefined";
        ccxml_event current = {.name = "go", .name_size = 2u};
        bool value = false;
        check_equal(ccxml_quickjs_datamodel_init(&owner, &config), CCXML_OK);
        check_equal(adapter->compile_condition(&owner, source, sizeof(source)-1u,
            &condition, &error), SCXML_ADAPTER_ACCEPTED);
        check_equal(adapter->evaluate_condition(&owner, &condition, &current,
            &value, &error), SCXML_ADAPTER_ACCEPTED);
        check(value);
        current.connection_id_size = 1u;
        check_equal(adapter->evaluate_condition(&owner, &condition, &current,
            &value, &error), SCXML_ADAPTER_INVALID_CONTRACT);
        check_false(value);
    }

    it("rejects foreach instead of silently selecting the CMeta evaluator") {
        static const char source[] =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='go'>"
            "<foreach array='items' item='item'><accept/></foreach>"
            "</transition></eventprocessor></ccxml>";
        ccxml_diagnostic diagnostic = {0};
        check_equal(ccxml_quickjs_datamodel_init(&owner, &config), CCXML_OK);
        check_equal(ccxml_compile(&program, source, sizeof(source)-1u,
            NULL, &diagnostic), CCXML_OK);
        const ccxml_session_config session_config = {
            .program = &program, .datamodel = adapter, .datamodel_user = &owner,
            .telephony = &telephony, .telephony_user = &provider};
        check_equal(ccxml_session_init(&session, &session_config), CCXML_INVALID_ARGUMENT);
        check_null(session.impl);
    }

    it("discards earlier provider tickets when JavaScript evaluation fails") {
        static const char source[] =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='go'><accept/>"
            "<if cond='missingVariable'><accept/></if>"
            "</transition></eventprocessor></ccxml>";
        ccxml_diagnostic diagnostic = {0};
        check_equal(ccxml_quickjs_datamodel_init(&owner, &config), CCXML_OK);
        check_equal(ccxml_compile(&program, source, sizeof(source)-1u,
            NULL, &diagnostic), CCXML_OK);
        const ccxml_session_config session_config = {
            .program = &program, .datamodel = adapter, .datamodel_user = &owner,
            .telephony = &telephony, .telephony_user = &provider};
        check_equal(ccxml_session_init(&session, &session_config), CCXML_OK);
        check_equal(ccxml_session_dispatch(&session, &event), CCXML_ADAPTER_ERROR);
        check_equal(provider.committed, (size_t)0);
        check_equal(provider.discarded, (size_t)1);
        check_equal(state.count, 7);
    }

    it("rejects syntax without running it during admission") {
        check_equal(ccxml_quickjs_datamodel_init(&owner, &config), CCXML_OK);
        check_equal(adapter->compile_condition(&owner, "count +", 7u, &condition, &error), SCXML_ADAPTER_ERROR_EXECUTION);
        check_null(condition.impl);
        check_equal(state.count, 7);
    }

    it("rejects invalid options without publishing an owner") {
        config.max_expressions = 0u;
        check_equal(ccxml_quickjs_datamodel_init(&owner, &config), CCXML_INVALID_ARGUMENT);
        check_null(owner.impl);
    }

    group("real VoiceXML dialog transaction") {
        before_each() {
            static const char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='go' "
                "cond=\"count === 7 &amp;&amp; event$.name === 'go'\">"
                "<dialogstart dialogid='id' src=\"('app' + count + '.vxml')\" "
                "connectionid='event$.connectionid'/><accept/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_diagnostic diagnostic = {0};
            vxml_dialog_manager_config_v1 manager_config = vxml_dialog_manager_default_config_v1();
            manager_config.capacity = 1u;
            manager_config.upstream = &telephony;
            manager_config.upstream_user = &provider;
            manager_config.documents = &documents;
            manager_config.document_user = &provider;
            manager_config.events = &events;
            manager_config.event_user = &provider;
            check_equal(vxml_dialog_manager_init(&manager, &manager_config), VXML_DIALOG_MANAGER_OK);
            check_equal(ccxml_quickjs_datamodel_init(&owner, &config), CCXML_OK);
            check_equal(ccxml_compile(&program, source, sizeof(source)-1u,
                NULL, &diagnostic), CCXML_OK);
            const ccxml_session_config session_config = {
                .program = &program, .datamodel = adapter, .datamodel_user = &owner,
                .telephony = vxml_dialog_manager_ccxml_adapter(),
                .telephony_user = vxml_dialog_manager_ccxml_user(&manager)};
            check_equal(ccxml_session_init(&session, &session_config), CCXML_OK);
        }
        it("writes the generated ID and fetches the JavaScript-selected document") {
            size_t processed = 0u;
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_not_equal(state.id.data, "old");
            check_equal(provider.opened, (size_t)0);
            check_equal(vxml_dialog_manager_run_ready(&manager, 1u, &processed), VXML_DIALOG_MANAGER_OK);
            check_equal(provider.opened, (size_t)1);
            check_equal(provider.closed, (size_t)1);
            check_equal(provider.events, (size_t)2);
        }
        it("rolls back the ID and reservation on refusal then retries successfully") {
            size_t processed = 0u;
            provider.reject = true;
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_ADAPTER_ERROR);
            check_equal(state.id.data, "old");
            check_equal(vxml_dialog_manager_run_ready(&manager, 1u, &processed), VXML_DIALOG_MANAGER_OK);
            check_equal(provider.opened, (size_t)0);
            check_equal(provider.events, (size_t)0);
            provider.reject = false;
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(vxml_dialog_manager_run_ready(&manager, 1u, &processed), VXML_DIALOG_MANAGER_OK);
            check_equal(provider.closed, (size_t)1);
            check_equal(provider.events, (size_t)2);
        }
    }
}
