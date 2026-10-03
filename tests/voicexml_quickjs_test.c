#include <voicexml/quickjs.h>

#include "tinytest.h"
#include "voicexml_internal.h"

#include <stdio.h>
#include <string.h>

typedef struct quickjs_script_probe {
    size_t open_calls;
    size_t close_calls;
    vxml_script_resource_status failure_status;
    bool publish_on_failure;
    char uris[4][256];
} quickjs_script_probe;

static vxml_script_resource_status quickjs_script_open(
    void *user,
    const char *resolved_uri,
    size_t resolved_uri_size,
    const char *charset,
    size_t charset_size,
    size_t max_bytes,
    vxml_script_source *out_source) {
    static const char bootstrap[] =
        "globalThis.nextScript='dynamic.js';";
    static const char dynamic[] =
        "globalThis.dynamicExecuted=true;";
    static const char throwing[] =
        "throw new Error('script boom');";
    quickjs_script_probe *probe =
        (quickjs_script_probe *)user;
    const char *body = NULL;
    size_t body_size = 0u;
    size_t call;
    (void)max_bytes;
    if (probe == NULL || resolved_uri == NULL ||
        resolved_uri_size == 0u ||
        resolved_uri_size >= sizeof(probe->uris[0]) ||
        charset == NULL ||
        charset_size != sizeof("UTF-8") - 1u ||
        memcmp(charset, "UTF-8", charset_size) != 0 ||
        out_source == NULL)
        return VXML_SCRIPT_RESOURCE_INVALID_ARGUMENT;
    call = probe->open_calls++;
    if (call < sizeof(probe->uris) / sizeof(probe->uris[0])) {
        memcpy(probe->uris[call], resolved_uri, resolved_uri_size);
        probe->uris[call][resolved_uri_size] = '\0';
    }
    if (probe->failure_status != VXML_SCRIPT_RESOURCE_OK) {
        if (probe->publish_on_failure)
            *out_source = (vxml_script_source){
                .data = bootstrap,
                .size = sizeof(bootstrap) - 1u,
                .lease = probe};
        return probe->failure_status;
    }
    if (strstr(resolved_uri, "bootstrap.js") != NULL) {
        body = bootstrap;
        body_size = sizeof(bootstrap) - 1u;
    } else if (strstr(resolved_uri, "dynamic.js") != NULL) {
        body = dynamic;
        body_size = sizeof(dynamic) - 1u;
    } else if (strstr(resolved_uri, "throw.js") != NULL) {
        body = throwing;
        body_size = sizeof(throwing) - 1u;
    } else {
        return VXML_SCRIPT_RESOURCE_PROVIDER_ERROR;
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
    .struct_size = sizeof(vxml_script_resource_adapter_v1),
    .open = quickjs_script_open,
    .close = quickjs_script_close};

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
    .struct_size = sizeof(vxml_dialog_document_adapter_v1),
    .open = quickjs_unused_document_open,
    .close = quickjs_unused_document_close};

static void quickjs_init_resolver(
    vxml_document_store *store) {
    vxml_document_store_config_v1 config = {
        .abi_version = VXML_DOCUMENT_STORE_CONFIG_ABI_V1,
        .struct_size = sizeof(vxml_document_store_config_v1),
        .application_uri =
            "https://voice.example/app/root.vxml",
        .application_uri_size =
            sizeof("https://voice.example/app/root.vxml") - 1u,
        .capacity = 1u,
        .max_uri_bytes = 255u,
        .max_document_bytes = 1024u,
        .max_cache_bytes = 2048u,
        .voice_limits = {0},
        .documents = &quickjs_document_adapter,
        .document_user = NULL};
    config.voice_limits = vxml_default_limits();
    check_equal(
        vxml_document_store_init(store, &config),
        VXML_DOCUMENT_STORE_OK);
}

static vxml_quickjs_session_options_v1
quickjs_session_options(
    vxml_document_store *store,
    quickjs_script_probe *probe,
    const char *base_uri) {
    return (vxml_quickjs_session_options_v1){
        .abi_version =
            VXML_QUICKJS_SESSION_OPTIONS_ABI_V1,
        .struct_size =
            sizeof(vxml_quickjs_session_options_v1),
        .resolver = store,
        .script_resources = &quickjs_script_adapter,
        .script_resource_user = probe,
        .base_document_uri = base_uri,
        .base_document_uri_size = strlen(base_uri),
        .max_resolved_uri_bytes = 255u,
        .max_script_source_bytes = 1024u};
}

spec("VoiceXML QuickJS script profile") {
    it("keeps base and static script profiles fail-closed for srcexpr") {
        static const char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script srcexpr="'scripts/main.js'"/>"
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

    it("compiles immutable dynamic script expression metadata") {
        char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script "
            "srcexpr="'scripts/' + 'main.js'" charset='utf-8'/>"
            "</block></form></vxml>";
        const vxml_quickjs_compile_options_v1 options =
            vxml_quickjs_default_compile_options();
        vxml_program program = {0};

        check_equal(
            vxml_compile_quickjs_script_profile(
                document, strlen(document),
                NULL, &options, &program, NULL),
            VXML_OK);
        check_not_null(program.impl);
        memset(document, 'x', sizeof(document) - 1u);
        {
            const vxml_program_impl *impl =
                (const vxml_program_impl *)program.impl;
            const vxml_action_row *action;
            check_equal(impl->action_count, (size_t)1u);
            check_not_null(impl->actions);
            action = &impl->actions[0];
            check_equal(
                action->kind,
                VXML_ACTION_SCRIPT_EXTERNAL);
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
            check_not_null(action->script_charset);
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

    it("keeps the static src descriptor path unchanged") {
        static const char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script src='scripts/main.js'/>"
            "</block></form></vxml>";
        const vxml_quickjs_compile_options_v1 options =
            vxml_quickjs_default_compile_options();
        vxml_program program = {0};

        check_equal(
            vxml_compile_quickjs_script_profile(
                document, sizeof(document) - 1u,
                NULL, &options, &program, NULL),
            VXML_OK);
        {
            const vxml_program_impl *impl =
                (const vxml_program_impl *)program.impl;
            const vxml_action_row *action = &impl->actions[0];
            check_not_null(action->script_src);
            check_equal(
                action->script_src_size,
                sizeof("scripts/main.js") - 1u);
            check_null(action->script_srcexpr);
            check_equal(
                action->script_srcexpr_size, (size_t)0u);
        }
        vxml_program_destroy(&program);
    }

    it("rejects invalid dynamic script language and syntax before session init") {
        static const char *const documents[] = {
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script src='a.js' srcexpr="'b.js'"/>"
            "</block></form></vxml>",
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script srcexpr='('/>"
            "</block></form></vxml>"
        };
        static const vxml_status expected[] = {
            VXML_INVALID_STRUCTURE,
            VXML_SEMANTIC_ERROR
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

    it("rejects srcexpr source overflow after bounded immutable compilation") {
        static const char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script srcexpr="'scripts/main.js'"/>"
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
        check_equal(
            diagnostic.status, VXML_LIMIT_EXCEEDED);
    }

    it("executes static script state before evaluating dynamic srcexpr") {
        static const char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form>"
            "<block><script src='scripts/bootstrap.js'/></block>"
            "<block><script srcexpr='nextScript'/></block>"
            "<block><exit/></block>"
            "</form></vxml>";
        char base_uri[] =
            "https://voice.example/app/dialogs/current.vxml";
        const vxml_quickjs_compile_options_v1 compile =
            vxml_quickjs_default_compile_options();
        quickjs_script_probe probe = {0};
        vxml_document_store store = {0};
        vxml_quickjs_session_options_v1 options;
        vxml_program program = {0};
        vxml_session session = {0};

        quickjs_init_resolver(&store);
        options = quickjs_session_options(
            &store, &probe, base_uri);
        check_equal(
            vxml_compile_quickjs_script_profile(
                document, sizeof(document) - 1u,
                NULL, &compile, &program, NULL),
            VXML_OK);
        check_equal(
            vxml_session_init(&session, &program),
            VXML_INVALID_CONTRACT);
        check_equal(
            vxml_session_init_quickjs(
                &session, &program, &options),
            VXML_OK);
        memset(base_uri, 'x', sizeof(base_uri) - 1u);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(
            vxml_session_get_state(&session),
            VXML_SESSION_EXITED);
        check_equal(probe.open_calls, (size_t)2u);
        check_equal(probe.close_calls, (size_t)2u);
        check_equal(
            probe.uris[0],
            "https://voice.example/app/dialogs/scripts/bootstrap.js");
        check_equal(
            probe.uris[1],
            "https://voice.example/app/dialogs/dynamic.js");

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("rejects non-string srcexpr before resource provider admission") {
        static const char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script srcexpr='42'/></block>"
            "</form></vxml>";
        const vxml_quickjs_compile_options_v1 compile =
            vxml_quickjs_default_compile_options();
        quickjs_script_probe probe = {0};
        vxml_document_store store = {0};
        vxml_quickjs_session_options_v1 options;
        vxml_program program = {0};
        vxml_session session = {0};
        const char *event = NULL;
        size_t event_size = 0u;

        quickjs_init_resolver(&store);
        options = quickjs_session_options(
            &store, &probe,
            "https://voice.example/app/current.vxml");
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
            VXML_SEMANTIC_ERROR);
        check_equal(
            vxml_session_get_state(&session),
            VXML_SESSION_FAILED);
        check_equal(probe.open_calls, (size_t)0u);
        check_equal(probe.close_calls, (size_t)0u);
        check_equal(
            vxml_quickjs_session_last_event(
                &session, &event, &event_size),
            VXML_OK);
        check_equal(event_size, sizeof("error.semantic") - 1u);
        check_equal(
            memcmp(event, "error.semantic", event_size), 0);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("closes a provider-published failure lease exactly once and preserves badfetch Event") {
        static const char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script src='scripts/bootstrap.js'/></block>"
            "</form></vxml>";
        const vxml_quickjs_compile_options_v1 compile =
            vxml_quickjs_default_compile_options();
        quickjs_script_probe probe = {
            .failure_status =
                VXML_SCRIPT_RESOURCE_PROVIDER_ERROR,
            .publish_on_failure = true};
        vxml_document_store store = {0};
        vxml_quickjs_session_options_v1 options;
        vxml_program program = {0};
        vxml_session session = {0};
        const char *event = NULL;
        size_t event_size = 0u;

        quickjs_init_resolver(&store);
        options = quickjs_session_options(
            &store, &probe,
            "https://voice.example/app/current.vxml");
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
            VXML_SEMANTIC_ERROR);
        check_equal(probe.open_calls, (size_t)1u);
        check_equal(probe.close_calls, (size_t)1u);
        check_equal(
            vxml_quickjs_session_last_event(
                &session, &event, &event_size),
            VXML_OK);
        check_equal(event_size, sizeof("error.badfetch") - 1u);
        check_equal(
            memcmp(event, "error.badfetch", event_size), 0);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("rejects dynamic URI semantic and deadline failures before provider open") {
        static const char *const expressions[] = {
            "''",
            "42",
            "(()=>{throw new Error('expr boom')})()",
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
            quickjs_script_probe probe = {0};
            vxml_document_store store = {0};
            vxml_quickjs_session_options_v1 options;
            vxml_program program = {0};
            vxml_session session = {0};
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
            quickjs_init_resolver(&store);
            options = quickjs_session_options(
                &store, &probe,
                "https://voice.example/app/current.vxml");
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
            check_equal(probe.open_calls, (size_t)0u);
            check_equal(probe.close_calls, (size_t)0u);
            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
            check_equal(
                vxml_document_store_destroy(&store),
                VXML_DOCUMENT_STORE_OK);
        }
    }

    it("rejects oversized dynamic URI before provider open") {
        static const char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script srcexpr=\"'abcde'\"/></block>"
            "</form></vxml>";
        vxml_quickjs_compile_options_v1 compile =
            vxml_quickjs_default_compile_options();
        quickjs_script_probe probe = {0};
        vxml_document_store store = {0};
        vxml_quickjs_session_options_v1 options;
        vxml_program program = {0};
        vxml_session session = {0};

        compile.max_dynamic_script_uri_bytes = 4u;
        quickjs_init_resolver(&store);
        options = quickjs_session_options(
            &store, &probe,
            "https://voice.example/app/current.vxml");
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
        check_equal(probe.open_calls, (size_t)0u);
        check_equal(probe.close_calls, (size_t)0u);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("closes a successfully acquired script before publishing execution failure") {
        static const char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script src='scripts/throw.js'/></block>"
            "</form></vxml>";
        const vxml_quickjs_compile_options_v1 compile =
            vxml_quickjs_default_compile_options();
        quickjs_script_probe probe = {0};
        vxml_document_store store = {0};
        vxml_quickjs_session_options_v1 options;
        vxml_program program = {0};
        vxml_session session = {0};

        quickjs_init_resolver(&store);
        options = quickjs_session_options(
            &store, &probe,
            "https://voice.example/app/current.vxml");
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
            VXML_SEMANTIC_ERROR);
        check_equal(probe.open_calls, (size_t)1u);
        check_equal(probe.close_calls, (size_t)1u);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }


}
