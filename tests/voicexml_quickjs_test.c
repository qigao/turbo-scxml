#include <voicexml/quickjs.h>
#include <voicexml/script_resource.h>

#include "tinytest.h"
#include "voicexml_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct voice_quickjs_state {
    int value;
} voice_quickjs_state;

static const cmeta_type_identity voice_quickjs_state_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.voicexml.quickjs.state");
static const cmeta_type_traits voice_quickjs_state_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY |
             CMETA_TRAIT_TRIVIAL_DESTROY
};
static const cmeta_type_desc voice_quickjs_state_type = {
    .name = "voice_quickjs_state",
    .size = sizeof(voice_quickjs_state),
    .align = _Alignof(voice_quickjs_state),
    .kind = CMETA_T_OBJECT,
    .traits = &voice_quickjs_state_traits,
    .identity = &voice_quickjs_state_identity
};
static const cmeta_field_desc voice_quickjs_state_layout_fields[] = {
    {"value", "int", offsetof(voice_quickjs_state, value),
     sizeof(int), _Alignof(int), &cmeta_type_int, NULL}
};
static const cmeta_struct_desc voice_quickjs_state_layout = {
    .name = "voice_quickjs_state",
    .size = sizeof(voice_quickjs_state),
    .align = _Alignof(voice_quickjs_state),
    .fields = voice_quickjs_state_layout_fields,
    .field_count = 1u
};
static const cmeta_data_field_desc voice_quickjs_state_fields[] = {
    {"test.voicexml.quickjs.state.value", "value",
     offsetof(voice_quickjs_state, value), &cmeta_data_int}
};
static const cmeta_data_struct_shape voice_quickjs_state_shape = {
    .layout = &voice_quickjs_state_layout,
    .fields = voice_quickjs_state_fields,
    .field_count = 1u
};
static const cmeta_data_desc voice_quickjs_state_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.voicexml.quickjs.state.data",
    .display_name = "VoiceXML QuickJS test state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &voice_quickjs_state_type,
    .shape = &voice_quickjs_state_shape
};

typedef struct quickjs_script_probe {
    size_t open_calls;
    size_t close_calls;
    vxml_script_resource_status open_status;
    bool publish_on_failure;
    const char *first_body;
    size_t first_body_size;
    const char *second_body;
    size_t second_body_size;
    char last_uri[256];
    size_t last_uri_size;
} quickjs_script_probe;

static vxml_script_resource_status quickjs_script_open(
    void *user,
    const char *resolved_uri,
    size_t resolved_uri_size,
    const char *charset,
    size_t charset_size,
    size_t max_bytes,
    vxml_script_source *out_source) {
    quickjs_script_probe *probe =
        (quickjs_script_probe *)user;
    const char *body;
    size_t body_size;
    if (probe == NULL || resolved_uri == NULL ||
        resolved_uri_size == 0u ||
        resolved_uri_size >= sizeof(probe->last_uri) ||
        charset == NULL || charset_size == 0u ||
        out_source == NULL)
        return VXML_SCRIPT_RESOURCE_INVALID_ARGUMENT;
    ++probe->open_calls;
    memcpy(
        probe->last_uri, resolved_uri,
        resolved_uri_size);
    probe->last_uri[resolved_uri_size] = '\0';
    probe->last_uri_size = resolved_uri_size;
    if (probe->open_calls == 1u) {
        body = probe->first_body;
        body_size = probe->first_body_size;
    } else {
        body = probe->second_body;
        body_size = probe->second_body_size;
    }
    if (body_size > max_bytes)
        return VXML_SCRIPT_RESOURCE_LIMIT_EXCEEDED;
    if (probe->open_status != VXML_SCRIPT_RESOURCE_OK) {
        if (probe->publish_on_failure) {
            *out_source = (vxml_script_source){
                .data = body,
                .size = body_size,
                .lease = probe};
        }
        return probe->open_status;
    }
    *out_source = (vxml_script_source){
        .data = body,
        .size = body_size,
        .lease = probe};
    return VXML_SCRIPT_RESOURCE_OK;
}

static void quickjs_script_close(
    void *user, vxml_script_source *source) {
    quickjs_script_probe *probe =
        (quickjs_script_probe *)user;
    if (probe != NULL && source != NULL &&
        source->lease == probe)
        ++probe->close_calls;
    if (source != NULL)
        *source = (vxml_script_source){0};
}

static const vxml_script_resource_adapter_v1
quickjs_script_adapter = {
    .abi_version = VXML_SCRIPT_RESOURCE_ADAPTER_ABI_V1,
    .struct_size =
        sizeof(vxml_script_resource_adapter_v1),
    .open = quickjs_script_open,
    .close = quickjs_script_close
};

static vxml_dialog_manager_status quickjs_unused_document_open(
    void *user,
    const char *source, size_t source_size,
    const char *media_type, size_t media_type_size,
    size_t max_bytes,
    vxml_dialog_document *out_document) {
    (void)user;
    (void)source;
    (void)source_size;
    (void)media_type;
    (void)media_type_size;
    (void)max_bytes;
    if (out_document != NULL)
        *out_document = (vxml_dialog_document){0};
    return VXML_DIALOG_MANAGER_DOCUMENT_ERROR;
}

static void quickjs_unused_document_close(
    void *user, vxml_dialog_document *document) {
    (void)user;
    if (document != NULL)
        *document = (vxml_dialog_document){0};
}

static const vxml_dialog_document_adapter_v1
quickjs_document_adapter = {
    .abi_version = VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1,
    .struct_size =
        sizeof(vxml_dialog_document_adapter_v1),
    .open = quickjs_unused_document_open,
    .close = quickjs_unused_document_close
};

static void quickjs_init_resolver(
    vxml_document_store *store) {
    vxml_document_store_config_v1 config = {
        .abi_version = VXML_DOCUMENT_STORE_CONFIG_ABI_V1,
        .struct_size =
            sizeof(vxml_document_store_config_v1),
        .application_uri =
            "https://voice.example/app/root.vxml",
        .application_uri_size =
            sizeof("https://voice.example/app/root.vxml") - 1u,
        .capacity = 1u,
        .max_uri_bytes = 255u,
        .max_document_bytes = 1024u,
        .max_cache_bytes = 2048u,
        .documents = &quickjs_document_adapter
    };
    config.voice_limits = vxml_default_limits();
    check_equal(
        vxml_document_store_init(store, &config),
        VXML_DOCUMENT_STORE_OK);
}

static vxml_quickjs_compile_options_v1
typed_compile_options(void) {
    vxml_quickjs_compile_options_v1 options =
        vxml_quickjs_default_compile_options();
    options.root = &voice_quickjs_state_data;
    return options;
}

static vxml_quickjs_session_options_v1
typed_session_options(
    const voice_quickjs_state *initial) {
    vxml_quickjs_session_options_v1 options =
        vxml_quickjs_default_session_options();
    options.initial_state = initial;
    return options;
}

static vxml_quickjs_script_execution_v1
script_execution(vxml_document_store *store) {
    vxml_quickjs_script_execution_v1 execution =
        VXML_QUICKJS_SCRIPT_EXECUTION_V1_INIT;
    execution.resolver = store;
    execution.script_resources = &quickjs_script_adapter;
    execution.base_document_uri =
        "https://voice.example/app/dialogs/current.vxml";
    execution.base_document_uri_size =
        sizeof("https://voice.example/app/dialogs/current.vxml") - 1u;
    return execution;
}

static vxml_quickjs_session_options_v1 session_options(void) {
    return vxml_quickjs_default_session_options();
}

spec("VoiceXML QuickJS script target profile") {
    it("keeps base and static script profiles fail-closed for srcexpr") {
        static const char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script srcexpr=\"'scripts/main.js'\"/>"
            "</block></form></vxml>";
        vxml_program program = {0};

        check_equal(
            vxml_compile(
                document, sizeof(document) - 1u,
                NULL, &program, NULL),
            VXML_UNSUPPORTED_FEATURE);
        check_null(program.impl);
        check_equal(
            vxml_compile_external_script_profile(
                document, sizeof(document) - 1u,
                NULL, &program, NULL),
            VXML_UNSUPPORTED_FEATURE);
        check_null(program.impl);
    }

    it("compiles immutable srcexpr metadata and validates syntax") {
        char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script "
            "srcexpr=\"'scripts/' + 'main.js'\" charset='utf-8'/>"
            "</block></form></vxml>";
        const vxml_quickjs_compile_options_v1 options =
            vxml_quickjs_default_compile_options();
        vxml_program program = {0};

        check_equal(
            vxml_compile_quickjs_script_profile(
                document, strlen(document),
                NULL, &options, &program, NULL),
            VXML_OK);
        memset(document, 'x', sizeof(document) - 1u);
        {
            const vxml_program_impl *impl =
                (const vxml_program_impl *)program.impl;
            const vxml_action_row *action;
            check_not_null(impl);
            check_equal(impl->profile_kind, VXML_PROFILE_QUICKJS);
            check_equal(impl->action_count, (size_t)1u);
            action = &impl->actions[0];
            check_equal(action->kind, VXML_ACTION_SCRIPT_EXTERNAL);
            check_null(action->script_src);
            check_equal(action->script_src_size, (size_t)0u);
            check_not_null(action->script_srcexpr);
            check_equal(
                action->script_srcexpr_size,
                sizeof("'scripts/' + 'main.js'") - 1u);
            check_equal(
                memcmp(
                    action->script_srcexpr,
                    "'scripts/' + 'main.js'",
                    action->script_srcexpr_size),
                0);
            check_equal(
                action->script_charset_size,
                sizeof("UTF-8") - 1u);
            check_equal(
                memcmp(
                    action->script_charset, "UTF-8",
                    action->script_charset_size),
                0);
        }
        vxml_program_destroy(&program);
    }

    it("keeps static src target handoff unchanged") {
        static const char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script src='scripts/main.js'/></block>"
            "</form></vxml>";
        const vxml_quickjs_compile_options_v1 compile =
            vxml_quickjs_default_compile_options();
        const vxml_quickjs_session_options_v1 options =
            session_options();
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_external_script_target_v1 target = {0};

        check_equal(
            vxml_compile_quickjs_script_profile(
                document, sizeof(document) - 1u,
                NULL, &compile, &program, NULL),
            VXML_OK);
        check_equal(
            vxml_session_init_quickjs(
                &session, &program, &options),
            VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(
            vxml_session_get_state(&session),
            VXML_SESSION_SCRIPTING);
        check_equal(
            vxml_session_script(&session, &target), VXML_OK);
        check_equal(
            target.src_size, sizeof("scripts/main.js") - 1u);
        check_equal(
            memcmp(
                target.src, "scripts/main.js",
                target.src_size),
            0);
        check_equal(
            target.charset_size, sizeof("UTF-8") - 1u);
        check_equal(
            memcmp(target.charset, "UTF-8", target.charset_size),
            0);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }

    it("evaluates srcexpr once and publishes Session-owned target bytes") {
        char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script "
            "srcexpr=\"(globalThis.n=(globalThis.n||0)+1,"
            "'scripts/'+globalThis.n+'.js')\"/>"
            "</block></form></vxml>";
        const vxml_quickjs_compile_options_v1 compile =
            vxml_quickjs_default_compile_options();
        const vxml_quickjs_session_options_v1 options =
            session_options();
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_external_script_target_v1 first = {0};
        vxml_external_script_target_v1 second = {0};

        check_equal(
            vxml_compile_quickjs_script_profile(
                document, strlen(document),
                NULL, &compile, &program, NULL),
            VXML_OK);
        memset(document, 'x', sizeof(document) - 1u);
        check_equal(
            vxml_session_init_quickjs(
                &session, &program, &options),
            VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(
            vxml_session_get_state(&session),
            VXML_SESSION_SCRIPTING);
        check_equal(
            vxml_session_script(&session, &first), VXML_OK);
        check_equal(
            first.src_size, sizeof("scripts/1.js") - 1u);
        check_equal(
            memcmp(first.src, "scripts/1.js", first.src_size), 0);
        check_equal(
            vxml_session_script(&session, &second), VXML_OK);
        check_equal(first.src, second.src);
        check_equal(first.src_size, second.src_size);
        check_equal(
            memcmp(second.src, "scripts/1.js", second.src_size), 0);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }

    it("rejects invalid srcexpr language and syntax before Session init") {
        static const char *const documents[] = {
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script src='a.js' srcexpr=\"'b.js'\"/>"
            "</block></form></vxml>",
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script srcexpr='('/></block></form></vxml>",
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script srcexpr=\"'a.js'\">inline()</script>"
            "</block></form></vxml>"
        };
        static const vxml_status expected[] = {
            VXML_INVALID_STRUCTURE,
            VXML_SEMANTIC_ERROR,
            VXML_INVALID_STRUCTURE
        };
        const vxml_quickjs_compile_options_v1 options =
            vxml_quickjs_default_compile_options();
        size_t index;

        for (index = 0u;
             index < sizeof(documents) / sizeof(documents[0]);
             ++index) {
            vxml_program program = {0};
            vxml_diagnostic diagnostic = {0};
            check_equal(
                vxml_compile_quickjs_script_profile(
                    documents[index], strlen(documents[index]),
                    NULL, &options, &program, &diagnostic),
                expected[index]);
            check_null(program.impl);
            check_equal(diagnostic.status, expected[index]);
        }
    }

    it("rejects srcexpr source overflow during compile") {
        static const char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script srcexpr=\"'scripts/main.js'\"/>"
            "</block></form></vxml>";
        vxml_quickjs_compile_options_v1 options =
            vxml_quickjs_default_compile_options();
        vxml_program program = {0};
        vxml_diagnostic diagnostic = {0};

        options.max_expression_bytes = 4u;
        check_equal(
            vxml_compile_quickjs_script_profile(
                document, sizeof(document) - 1u,
                NULL, &options, &program, &diagnostic),
            VXML_LIMIT_EXCEEDED);
        check_null(program.impl);
        check_equal(diagnostic.status, VXML_LIMIT_EXCEEDED);
    }

    it("fails non-string empty exception and deadline expressions before target publication") {
        static const char *const expressions[] = {
            "42",
            "''",
            "(()=>{throw new Error('boom')})()",
            "(()=>{for(;;){} })()"
        };
        static const vxml_status expected[] = {
            VXML_SEMANTIC_ERROR,
            VXML_SEMANTIC_ERROR,
            VXML_SEMANTIC_ERROR,
            VXML_LIMIT_EXCEEDED
        };
        size_t index;

        for (index = 0u;
             index < sizeof(expressions) / sizeof(expressions[0]);
             ++index) {
            char document[512];
            vxml_quickjs_compile_options_v1 compile =
                vxml_quickjs_default_compile_options();
            const vxml_quickjs_session_options_v1 options =
                session_options();
            vxml_program program = {0};
            vxml_session session = {0};
            const char *event = NULL;
            size_t event_size = 0u;
            int written;

            compile.max_eval_milliseconds = UINT64_C(5);
            written = snprintf(
                document, sizeof(document),
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block><script srcexpr=\"%s\"/></block>"
                "</form></vxml>",
                expressions[index]);
            check_true(written > 0);
            check_true((size_t)written < sizeof(document));
            check_equal(
                vxml_compile_quickjs_script_profile(
                    document, (size_t)written,
                    NULL, &compile, &program, NULL),
                VXML_OK);
            check_equal(
                vxml_session_init_quickjs(
                    &session, &program, &options),
                VXML_OK);
            check_equal(
                vxml_session_start(&session),
                expected[index]);
            check_equal(
                vxml_session_get_state(&session),
                VXML_SESSION_FAILED);
            check_equal(
                vxml_quickjs_session_last_event(
                    &session, &event, &event_size),
                VXML_OK);
            check_equal(
                event_size, sizeof("error.semantic") - 1u);
            check_equal(
                memcmp(event, "error.semantic", event_size), 0);
            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }
    }

    it("rejects oversized dynamic URI before SCRIPTING target publication") {
        static const char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script srcexpr=\"'abcde'\"/></block>"
            "</form></vxml>";
        vxml_quickjs_compile_options_v1 compile =
            vxml_quickjs_default_compile_options();
        const vxml_quickjs_session_options_v1 options =
            session_options();
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_external_script_target_v1 target = {0};

        compile.max_dynamic_script_uri_bytes = 4u;
        check_equal(
            vxml_compile_quickjs_script_profile(
                document, sizeof(document) - 1u,
                NULL, &compile, &program, NULL),
            VXML_OK);
        check_equal(
            vxml_session_init_quickjs(
                &session, &program, &options),
            VXML_OK);
        check_equal(
            vxml_session_start(&session),
            VXML_LIMIT_EXCEEDED);
        check_equal(
            vxml_session_get_state(&session),
            VXML_SESSION_FAILED);
        check_equal(
            vxml_session_script(&session, &target),
            VXML_INVALID_STATE);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }
}
