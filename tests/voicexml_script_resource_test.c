#include <voicexml/script_resource.h>
#include <tinytest.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct script_probe {
    size_t open_calls;
    size_t close_calls;
    vxml_script_resource_status open_status;
    const char *body;
    size_t body_size;
    bool publish_on_failure;
    char uri[256];
    size_t uri_size;
    char charset[32];
    size_t charset_size;
    size_t max_bytes;
} script_probe;

static vxml_script_resource_status script_open(
    void *user,
    const char *resolved_uri,
    size_t resolved_uri_size,
    const char *charset,
    size_t charset_size,
    size_t max_bytes,
    vxml_script_source *out_source) {
    script_probe *probe = (script_probe *)user;
    if (probe == NULL || resolved_uri == NULL ||
        resolved_uri_size == 0u ||
        resolved_uri_size >= sizeof(probe->uri) ||
        charset == NULL || charset_size == 0u ||
        charset_size >= sizeof(probe->charset) ||
        out_source == NULL)
        return VXML_SCRIPT_RESOURCE_INVALID_ARGUMENT;
    ++probe->open_calls;
    memcpy(probe->uri, resolved_uri, resolved_uri_size);
    probe->uri[resolved_uri_size] = '\0';
    probe->uri_size = resolved_uri_size;
    memcpy(probe->charset, charset, charset_size);
    probe->charset[charset_size] = '\0';
    probe->charset_size = charset_size;
    probe->max_bytes = max_bytes;
    if (probe->open_status != VXML_SCRIPT_RESOURCE_OK) {
        if (probe->publish_on_failure) {
            *out_source = (vxml_script_source){
                .data = probe->body,
                .size = probe->body_size,
                .lease = probe};
        }
        return probe->open_status;
    }
    *out_source = (vxml_script_source){
        .data = probe->body,
        .size = probe->body_size,
        .lease = probe};
    return VXML_SCRIPT_RESOURCE_OK;
}

static void script_close(
    void *user, vxml_script_source *source) {
    script_probe *probe = (script_probe *)user;
    if (probe != NULL && source != NULL &&
        source->lease == probe)
        ++probe->close_calls;
    if (source != NULL)
        *source = (vxml_script_source){0};
}

static const vxml_script_resource_adapter_v1 script_adapter = {
    .abi_version = VXML_SCRIPT_RESOURCE_ADAPTER_ABI_V1,
    .struct_size = sizeof(vxml_script_resource_adapter_v1),
    .open = script_open,
    .close = script_close};

static vxml_dialog_manager_status unused_document_open(
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

static void unused_document_close(
    void *user, vxml_dialog_document *document) {
    (void)user;
    if (document != NULL)
        *document = (vxml_dialog_document){0};
}

static const vxml_dialog_document_adapter_v1 document_adapter = {
    .abi_version = VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1,
    .struct_size = sizeof(vxml_dialog_document_adapter_v1),
    .open = unused_document_open,
    .close = unused_document_close};

static void init_resolver(vxml_document_store *store) {
    const vxml_document_store_config_v1 config = {
        .abi_version = VXML_DOCUMENT_STORE_CONFIG_ABI_V1,
        .struct_size = sizeof(vxml_document_store_config_v1),
        .application_uri = "https://voice.example/app/root.vxml",
        .application_uri_size =
            sizeof("https://voice.example/app/root.vxml") - 1u,
        .capacity = 1u,
        .max_uri_bytes = 255u,
        .max_document_bytes = 1024u,
        .max_cache_bytes = 2048u,
        .voice_limits = {0},
        .documents = &document_adapter,
        .document_user = NULL};
    vxml_document_store_config_v1 copy = config;
    copy.voice_limits = vxml_default_limits();
    check_equal(
        vxml_document_store_init(store, &copy),
        VXML_DOCUMENT_STORE_OK);
}

static vxml_script_request_v1 request_for(
    const char *reference,
    const char *charset) {
    vxml_script_request_v1 request = VXML_SCRIPT_REQUEST_V1_INIT;
    request.base_document_uri =
        "https://voice.example/app/dialogs/current.vxml";
    request.base_document_uri_size =
        sizeof("https://voice.example/app/dialogs/current.vxml") - 1u;
    request.reference = reference;
    request.reference_size = strlen(reference);
    request.charset = charset;
    request.charset_size =
        charset != NULL ? strlen(charset) : 0u;
    request.max_uri_bytes = 255u;
    request.max_source_bytes = 64u;
    return request;
}

spec("VoiceXML external script resource") {
    it("keeps base VoiceXML fail-closed while explicit profile yields immutable external script handoff") {
        char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='main'><block>"
            "<script src='scripts/main.js' charset='utf-8'/>"
            "</block></form></vxml>";
        static const char body[] = "globalThis.answer = 42;";
        script_probe probe = {
            .open_status = VXML_SCRIPT_RESOURCE_OK,
            .body = body,
            .body_size = sizeof(body) - 1u};
        vxml_program base = {0};
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_external_script_target_v1 target = {0};
        vxml_document_store store = {0};
        vxml_script_request_v1 request = VXML_SCRIPT_REQUEST_V1_INIT;
        vxml_script_source source = {0};

        check_equal(
            vxml_compile(
                document, strlen(document), NULL, &base, NULL),
            VXML_UNSUPPORTED_FEATURE);
        check_null(base.impl);

        check_equal(
            vxml_compile_external_script_profile(
                document, strlen(document), NULL, &program, NULL),
            VXML_OK);
        check_not_null(program.impl);
        memset(document, 'x', sizeof(document) - 1u);

        check_equal(vxml_session_init(&session, &program), VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(
            vxml_session_get_state(&session),
            VXML_SESSION_SCRIPTING);
        check_equal(
            vxml_session_script(&session, &target), VXML_OK);
        check_equal(
            target.abi_version,
            VXML_EXTERNAL_SCRIPT_TARGET_ABI_V1);
        check_equal(
            target.struct_size,
            sizeof(vxml_external_script_target_v1));
        check_equal(
            target.src_size, sizeof("scripts/main.js") - 1u);
        check_equal(
            memcmp(
                target.src, "scripts/main.js",
                target.src_size), 0);
        check_equal(
            target.charset_size, sizeof("UTF-8") - 1u);
        check_equal(
            memcmp(
                target.charset, "UTF-8",
                target.charset_size), 0);

        init_resolver(&store);
        request.base_document_uri =
            "https://voice.example/app/dialogs/current.vxml";
        request.base_document_uri_size =
            sizeof("https://voice.example/app/dialogs/current.vxml") - 1u;
        request.reference = target.src;
        request.reference_size = target.src_size;
        request.charset = target.charset;
        request.charset_size = target.charset_size;
        request.max_uri_bytes = 255u;
        request.max_source_bytes = 64u;

        check_equal(
            vxml_script_resource_acquire(
                &store, &script_adapter, &probe,
                &request, &source),
            VXML_SCRIPT_RESOURCE_OK);
        check_equal(probe.open_calls, (size_t)1u);
        check_equal(
            probe.uri,
            "https://voice.example/app/dialogs/scripts/main.js");
        check_equal(probe.charset, "UTF-8");
        check_equal(source.size, sizeof(body) - 1u);
        check_equal(memcmp(source.data, body, source.size), 0);
        check_equal(
            vxml_script_resource_close(
                &script_adapter, &probe, &source),
            VXML_SCRIPT_RESOURCE_OK);
        check_equal(probe.close_calls, (size_t)1u);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("rejects invalid external script language shapes before resource admission") {
        static const char *const documents[] = {
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script/></block></form></vxml>",
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script>inline()</script></block></form></vxml>",
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script src='a.js'>inline()</script></block></form>"
            "</vxml>",
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script src='a.js' charset='UTF-16'/></block></form>"
            "</vxml>",
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script srcexpr='x'/></block></form></vxml>"
        };
        static const vxml_status expected[] = {
            VXML_INVALID_STRUCTURE,
            VXML_UNSUPPORTED_FEATURE,
            VXML_INVALID_STRUCTURE,
            VXML_UNSUPPORTED_FEATURE,
            VXML_UNSUPPORTED_FEATURE
        };
        size_t index;

        for (index = 0u;
             index < sizeof(documents) / sizeof(documents[0]);
             ++index) {
            vxml_program program = {0};
            check_equal(
                vxml_compile_external_script_profile(
                    documents[index], strlen(documents[index]),
                    NULL, &program, NULL),
                expected[index]);
            check_null(program.impl);
        }
    }

    it("maps bounded resource failures to exact VoiceXML Events") {
        static const struct {
            vxml_script_resource_status status;
            const char *event;
            size_t event_size;
        } cases[] = {
            {VXML_SCRIPT_RESOURCE_INVALID_URI,
             "error.badfetch", sizeof("error.badfetch") - 1u},
            {VXML_SCRIPT_RESOURCE_LIMIT_EXCEEDED,
             "error.badfetch", sizeof("error.badfetch") - 1u},
            {VXML_SCRIPT_RESOURCE_PROVIDER_ERROR,
             "error.badfetch", sizeof("error.badfetch") - 1u},
            {VXML_SCRIPT_RESOURCE_INVALID_DATA,
             "error.badfetch", sizeof("error.badfetch") - 1u},
            {VXML_SCRIPT_RESOURCE_UNSUPPORTED_CHARSET,
             "error.unsupported.format",
             sizeof("error.unsupported.format") - 1u},
            {VXML_SCRIPT_RESOURCE_ALLOCATION_FAILED,
             "error.noresource", sizeof("error.noresource") - 1u}
        };
        size_t index;

        for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
            size_t event_size = 99u;
            const char *event =
                vxml_script_resource_failure_event(
                    cases[index].status, &event_size);
            check_not_null(event);
            check_equal(event_size, cases[index].event_size);
            check_equal(
                memcmp(event, cases[index].event, event_size), 0);
        }

        {
            size_t event_size = 99u;
            check_null(
                vxml_script_resource_failure_event(
                    VXML_SCRIPT_RESOURCE_OK, &event_size));
            check_equal(event_size, (size_t)0u);
        }
        {
            size_t event_size = 99u;
            check_null(
                vxml_script_resource_failure_event(
                    VXML_SCRIPT_RESOURCE_INVALID_ARGUMENT, &event_size));
            check_equal(event_size, (size_t)0u);
        }
    }

    it("resolves a relative URI and retains one provider lease until close") {
        static const char body[] = "var answer = 42;";
        script_probe probe = {
            .open_status = VXML_SCRIPT_RESOURCE_OK,
            .body = body,
            .body_size = sizeof(body) - 1u};
        vxml_document_store store = {0};
        vxml_script_source source = {0};
        vxml_script_request_v1 request =
            request_for("scripts/main.js", NULL);

        init_resolver(&store);
        check_equal(
            vxml_script_resource_acquire(
                &store, &script_adapter, &probe,
                &request, &source),
            VXML_SCRIPT_RESOURCE_OK);
        check_equal(probe.open_calls, (size_t)1u);
        check_equal(
            probe.uri,
            "https://voice.example/app/dialogs/scripts/main.js");
        check_equal(probe.charset, "UTF-8");
        check_equal(probe.max_bytes, (size_t)64u);
        check_true(source.lease == &probe);
        check_equal(source.size, sizeof(body) - 1u);
        check_equal(
            memcmp(source.data, body, source.size), 0);

        check_equal(
            vxml_script_resource_close(
                &script_adapter, &probe, &source),
            VXML_SCRIPT_RESOURCE_OK);
        check_equal(probe.close_calls, (size_t)1u);
        check_null(source.lease);
        check_equal(
            vxml_script_resource_close(
                &script_adapter, &probe, &source),
            VXML_SCRIPT_RESOURCE_OK);
        check_equal(probe.close_calls, (size_t)1u);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("accepts an absolute URI and explicit UTF-8 case-insensitively") {
        static const char body[] = "";
        script_probe probe = {
            .open_status = VXML_SCRIPT_RESOURCE_OK,
            .body = body,
            .body_size = 0u};
        vxml_document_store store = {0};
        vxml_script_source source = {0};
        vxml_script_request_v1 request =
            request_for("https://cdn.example/x.js", "utf-8");

        init_resolver(&store);
        check_equal(
            vxml_script_resource_acquire(
                &store, &script_adapter, &probe,
                &request, &source),
            VXML_SCRIPT_RESOURCE_OK);
        check_equal(probe.uri, "https://cdn.example/x.js");
        check_equal(probe.charset, "utf-8");
        check_equal(
            vxml_script_resource_close(
                &script_adapter, &probe, &source),
            VXML_SCRIPT_RESOURCE_OK);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("rejects unsupported charset before provider admission") {
        script_probe probe = {
            .open_status = VXML_SCRIPT_RESOURCE_OK};
        vxml_document_store store = {0};
        vxml_script_source source = {0};
        vxml_script_request_v1 request =
            request_for("scripts/main.js", "UTF-16");

        init_resolver(&store);
        check_equal(
            vxml_script_resource_acquire(
                &store, &script_adapter, &probe,
                &request, &source),
            VXML_SCRIPT_RESOURCE_UNSUPPORTED_CHARSET);
        check_equal(probe.open_calls, (size_t)0u);
        check_null(source.lease);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("rejects script URI fragments before provider admission") {
        script_probe probe = {
            .open_status = VXML_SCRIPT_RESOURCE_OK};
        vxml_document_store store = {0};
        vxml_script_source source = {0};
        vxml_script_request_v1 request =
            request_for("scripts/main.js#part", NULL);

        init_resolver(&store);
        check_equal(
            vxml_script_resource_acquire(
                &store, &script_adapter, &probe,
                &request, &source),
            VXML_SCRIPT_RESOURCE_INVALID_URI);
        check_equal(probe.open_calls, (size_t)0u);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("closes a provider lease published on provider failure") {
        static const char body[] = "bad";
        script_probe probe = {
            .open_status = VXML_SCRIPT_RESOURCE_PROVIDER_ERROR,
            .body = body,
            .body_size = sizeof(body) - 1u,
            .publish_on_failure = true};
        vxml_document_store store = {0};
        vxml_script_source source = {0};
        vxml_script_request_v1 request =
            request_for("scripts/main.js", NULL);

        init_resolver(&store);
        check_equal(
            vxml_script_resource_acquire(
                &store, &script_adapter, &probe,
                &request, &source),
            VXML_SCRIPT_RESOURCE_PROVIDER_ERROR);
        check_equal(probe.open_calls, (size_t)1u);
        check_equal(probe.close_calls, (size_t)1u);
        check_null(source.lease);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("closes an oversized successful provider lease before returning") {
        static const char body[] =
            "012345678901234567890123456789012345678901234567890123456789"
            "0123456789";
        script_probe probe = {
            .open_status = VXML_SCRIPT_RESOURCE_OK,
            .body = body,
            .body_size = sizeof(body) - 1u};
        vxml_document_store store = {0};
        vxml_script_source source = {0};
        vxml_script_request_v1 request =
            request_for("scripts/main.js", NULL);
        request.max_source_bytes = 16u;

        init_resolver(&store);
        check_equal(
            vxml_script_resource_acquire(
                &store, &script_adapter, &probe,
                &request, &source),
            VXML_SCRIPT_RESOURCE_LIMIT_EXCEEDED);
        check_equal(probe.close_calls, (size_t)1u);
        check_null(source.lease);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }
}
