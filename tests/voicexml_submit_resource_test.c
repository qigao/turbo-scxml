#include <voicexml/submit_resource.h>
#include <tinytest.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct submit_probe {
    size_t execute_calls;
    size_t close_calls;
    vxml_submit_resource_status execute_status;
    bool publish_on_failure;
    const char *response_body;
    size_t response_body_size;
    const char *response_media_type;
    const char *response_effective_uri;
    char uri[512];
    char fragment[128];
    char content_type[96];
    char body[512];
    vxml_submit_method method;
} submit_probe;

static vxml_submit_resource_status submit_execute(
    void *user,
    const vxml_submit_wire_request_v1 *request,
    vxml_submit_response *out_response) {
    submit_probe *probe = (submit_probe *)user;
    if (probe == NULL || request == NULL || out_response == NULL ||
        request->abi_version != VXML_SUBMIT_WIRE_REQUEST_ABI_V1 ||
        request->struct_size < sizeof(*request) ||
        request->uri == NULL ||
        request->uri_size >= sizeof(probe->uri) ||
        request->fragment_size >= sizeof(probe->fragment) ||
        request->content_type_size >= sizeof(probe->content_type) ||
        request->body_size >= sizeof(probe->body))
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    ++probe->execute_calls;
    memcpy(probe->uri, request->uri, request->uri_size);
    probe->uri[request->uri_size] = '\0';
    if (request->fragment_size != 0u) {
        memcpy(
            probe->fragment,
            request->fragment,
            request->fragment_size);
    }
    probe->fragment[request->fragment_size] = '\0';
    if (request->content_type_size != 0u) {
        memcpy(
            probe->content_type,
            request->content_type,
            request->content_type_size);
    }
    probe->content_type[request->content_type_size] = '\0';
    if (request->body_size != 0u)
        memcpy(probe->body, request->body, request->body_size);
    probe->body[request->body_size] = '\0';
    probe->method = request->method;

    if (probe->execute_status != VXML_SUBMIT_RESOURCE_OK) {
        if (probe->publish_on_failure) {
            *out_response = (vxml_submit_response){
                .data = probe->response_body,
                .size = probe->response_body_size,
                .media_type = probe->response_media_type,
                .media_type_size =
                    probe->response_media_type != NULL
                        ? strlen(probe->response_media_type) : 0u,
                .effective_uri = probe->response_effective_uri,
                .effective_uri_size =
                    probe->response_effective_uri != NULL
                        ? strlen(probe->response_effective_uri) : 0u,
                .lease = probe};
        }
        return probe->execute_status;
    }

    *out_response = (vxml_submit_response){
        .data = probe->response_body,
        .size = probe->response_body_size,
        .media_type = probe->response_media_type,
        .media_type_size =
            probe->response_media_type != NULL
                ? strlen(probe->response_media_type) : 0u,
        .effective_uri = probe->response_effective_uri,
        .effective_uri_size =
            probe->response_effective_uri != NULL
                ? strlen(probe->response_effective_uri) : 0u,
        .lease = probe};
    return VXML_SUBMIT_RESOURCE_OK;
}

static void submit_close(
    void *user, vxml_submit_response *response) {
    submit_probe *probe = (submit_probe *)user;
    if (probe != NULL && response != NULL &&
        response->lease == probe)
        ++probe->close_calls;
    if (response != NULL)
        *response = (vxml_submit_response){0};
}

static const vxml_submit_resource_adapter_v1 submit_adapter = {
    .abi_version = VXML_SUBMIT_RESOURCE_ADAPTER_ABI_V1,
    .struct_size = sizeof(vxml_submit_resource_adapter_v1),
    .execute = submit_execute,
    .close = submit_close};

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
    vxml_document_store_config_v1 config = {
        .abi_version = VXML_DOCUMENT_STORE_CONFIG_ABI_V1,
        .struct_size = sizeof(vxml_document_store_config_v1),
        .application_uri = "https://voice.example/app/root.vxml",
        .application_uri_size =
            sizeof("https://voice.example/app/root.vxml") - 1u,
        .capacity = 1u,
        .max_uri_bytes = 511u,
        .max_document_bytes = 1024u,
        .max_cache_bytes = 2048u,
        .documents = &document_adapter};
    config.voice_limits = vxml_default_limits();
    check_equal(
        vxml_document_store_init(store, &config),
        VXML_DOCUMENT_STORE_OK);
}

static vxml_submit_request_v1 base_request(void) {
    vxml_submit_request_v1 request = VXML_SUBMIT_REQUEST_V1_INIT;
    request.base_document_uri =
        "https://voice.example/app/dialogs/current.vxml";
    request.base_document_uri_size =
        sizeof("https://voice.example/app/dialogs/current.vxml") - 1u;
    request.target = "submit.vxml#result";
    request.target_size = sizeof("submit.vxml#result") - 1u;
    request.max_uri_bytes = 511u;
    request.max_body_bytes = 511u;
    request.max_response_bytes = 1024u;
    return request;
}

static submit_probe successful_probe(void) {
    static const char response[] =
        "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
        "<form><block><exit/></block></form></vxml>";
    return (submit_probe){
        .execute_status = VXML_SUBMIT_RESOURCE_OK,
        .response_body = response,
        .response_body_size = sizeof(response) - 1u,
        .response_media_type = "application/voicexml+xml",
        .response_effective_uri =
            "https://voice.example/app/dialogs/result.vxml"};
}

spec("VoiceXML one-attempt submit resource") {
    it("encodes a stable ordered GET query and preserves response fragment") {
        static const vxml_submit_field_v1 fields[] = {
            {"first name", sizeof("first name") - 1u,
             "Ada Lovelace", sizeof("Ada Lovelace") - 1u},
            {"note", sizeof("note") - 1u,
             "A&B/?", sizeof("A&B/?") - 1u}};
        submit_probe probe = successful_probe();
        vxml_document_store store = {0};
        vxml_submit_request_v1 request = base_request();
        vxml_submit_response response = {0};

        request.target = "submit.vxml?existing=1#result";
        request.target_size =
            sizeof("submit.vxml?existing=1#result") - 1u;
        request.method = VXML_SUBMIT_METHOD_GET;
        request.fields = fields;
        request.field_count = 2u;

        init_resolver(&store);
        check_equal(
            vxml_submit_resource_execute(
                &store, &submit_adapter, &probe,
                &request, &response),
            VXML_SUBMIT_RESOURCE_OK);
        check_equal(probe.execute_calls, (size_t)1u);
        check_equal(probe.method, VXML_SUBMIT_METHOD_GET);
        check_equal(
            probe.uri,
            "https://voice.example/app/dialogs/submit.vxml?"
            "existing=1&first+name=Ada+Lovelace&note=A%26B%2F%3F");
        check_equal(probe.fragment, "result");
        check_equal(probe.content_type, "");
        check_equal(probe.body, "");
        check_true(response.lease == &probe);

        check_equal(
            vxml_submit_resource_close(
                &submit_adapter, &probe, &response),
            VXML_SUBMIT_RESOURCE_OK);
        check_equal(probe.close_calls, (size_t)1u);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("encodes one POST body and exact urlencoded content type") {
        static const vxml_submit_field_v1 fields[] = {
            {"name", sizeof("name") - 1u,
             "Ada", sizeof("Ada") - 1u},
            {"rank", sizeof("rank") - 1u,
             "1", sizeof("1") - 1u}};
        submit_probe probe = successful_probe();
        vxml_document_store store = {0};
        vxml_submit_request_v1 request = base_request();
        vxml_submit_response response = {0};

        request.method = VXML_SUBMIT_METHOD_POST;
        request.fields = fields;
        request.field_count = 2u;

        init_resolver(&store);
        check_equal(
            vxml_submit_resource_execute(
                &store, &submit_adapter, &probe,
                &request, &response),
            VXML_SUBMIT_RESOURCE_OK);
        check_equal(probe.execute_calls, (size_t)1u);
        check_equal(probe.method, VXML_SUBMIT_METHOD_POST);
        check_equal(
            probe.uri,
            "https://voice.example/app/dialogs/submit.vxml");
        check_equal(probe.fragment, "result");
        check_equal(
            probe.content_type,
            "application/x-www-form-urlencoded");
        check_equal(probe.body, "name=Ada&rank=1");
        check_equal(
            vxml_submit_resource_close(
                &submit_adapter, &probe, &response),
            VXML_SUBMIT_RESOURCE_OK);
        check_equal(probe.close_calls, (size_t)1u);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("supports an empty namelist without synthesizing query or body") {
        submit_probe probe = successful_probe();
        vxml_document_store store = {0};
        vxml_submit_request_v1 request = base_request();
        vxml_submit_response response = {0};

        init_resolver(&store);
        request.method = VXML_SUBMIT_METHOD_GET;
        check_equal(vxml_submit_resource_execute(
                        &store, &submit_adapter, &probe,
                        &request, &response),
                    VXML_SUBMIT_RESOURCE_OK);
        check_equal(
            probe.uri,
            "https://voice.example/app/dialogs/submit.vxml");
        check_equal(probe.body, "");
        check_equal(vxml_submit_resource_close(
                        &submit_adapter, &probe, &response),
                    VXML_SUBMIT_RESOURCE_OK);

        probe = successful_probe();
        request.method = VXML_SUBMIT_METHOD_POST;
        response = (vxml_submit_response){0};
        check_equal(vxml_submit_resource_execute(
                        &store, &submit_adapter, &probe,
                        &request, &response),
                    VXML_SUBMIT_RESOURCE_OK);
        check_equal(probe.body, "");
        check_equal(
            probe.content_type,
            "application/x-www-form-urlencoded");
        check_equal(vxml_submit_resource_close(
                        &submit_adapter, &probe, &response),
                    VXML_SUBMIT_RESOURCE_OK);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("rejects empty or duplicate names before provider admission") {
        static const vxml_submit_field_v1 empty_name[] = {
            {NULL, 0u, "x", 1u}};
        static const vxml_submit_field_v1 duplicate[] = {
            {"x", 1u, "1", 1u},
            {"x", 1u, "2", 1u}};
        submit_probe probe = successful_probe();
        vxml_document_store store = {0};
        vxml_submit_request_v1 request = base_request();
        vxml_submit_response response = {0};

        init_resolver(&store);
        request.fields = empty_name;
        request.field_count = 1u;
        check_equal(vxml_submit_resource_execute(
                        &store, &submit_adapter, &probe,
                        &request, &response),
                    VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT);
        request.fields = duplicate;
        request.field_count = 2u;
        check_equal(vxml_submit_resource_execute(
                        &store, &submit_adapter, &probe,
                        &request, &response),
                    VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT);
        check_equal(probe.execute_calls, (size_t)0u);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("rejects unsupported POST enctype before provider admission") {
        submit_probe probe = successful_probe();
        vxml_document_store store = {0};
        vxml_submit_request_v1 request = base_request();
        vxml_submit_response response = {0};

        request.method = VXML_SUBMIT_METHOD_POST;
        request.enctype = "multipart/form-data";
        request.enctype_size =
            sizeof("multipart/form-data") - 1u;
        init_resolver(&store);
        check_equal(vxml_submit_resource_execute(
                        &store, &submit_adapter, &probe,
                        &request, &response),
                    VXML_SUBMIT_RESOURCE_UNSUPPORTED_ENCODING);
        check_equal(probe.execute_calls, (size_t)0u);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("surfaces POSSIBLY_PROCESSED unchanged and never retries") {
        submit_probe probe = successful_probe();
        vxml_document_store store = {0};
        vxml_submit_request_v1 request = base_request();
        vxml_submit_response response = {0};

        probe.execute_status =
            VXML_SUBMIT_RESOURCE_POSSIBLY_PROCESSED;
        probe.publish_on_failure = true;
        request.method = VXML_SUBMIT_METHOD_POST;
        init_resolver(&store);
        check_equal(vxml_submit_resource_execute(
                        &store, &submit_adapter, &probe,
                        &request, &response),
                    VXML_SUBMIT_RESOURCE_POSSIBLY_PROCESSED);
        check_equal(probe.execute_calls, (size_t)1u);
        check_equal(probe.close_calls, (size_t)1u);
        check_null(response.lease);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("closes oversized or wrong-MIME successful responses") {
        submit_probe probe = successful_probe();
        vxml_document_store store = {0};
        vxml_submit_request_v1 request = base_request();
        vxml_submit_response response = {0};

        init_resolver(&store);
        request.max_response_bytes = 8u;
        check_equal(vxml_submit_resource_execute(
                        &store, &submit_adapter, &probe,
                        &request, &response),
                    VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED);
        check_equal(probe.close_calls, (size_t)1u);
        check_null(response.lease);

        probe = successful_probe();
        probe.response_media_type = "text/xml";
        request.max_response_bytes = 1024u;
        check_equal(vxml_submit_resource_execute(
                        &store, &submit_adapter, &probe,
                        &request, &response),
                    VXML_SUBMIT_RESOURCE_INVALID_RESPONSE);
        check_equal(probe.close_calls, (size_t)1u);
        check_null(response.lease);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("close is exact once and idempotent after clearing") {
        submit_probe probe = successful_probe();
        vxml_document_store store = {0};
        vxml_submit_request_v1 request = base_request();
        vxml_submit_response response = {0};

        init_resolver(&store);
        check_equal(vxml_submit_resource_execute(
                        &store, &submit_adapter, &probe,
                        &request, &response),
                    VXML_SUBMIT_RESOURCE_OK);
        check_equal(vxml_submit_resource_close(
                        &submit_adapter, &probe, &response),
                    VXML_SUBMIT_RESOURCE_OK);
        check_equal(vxml_submit_resource_close(
                        &submit_adapter, &probe, &response),
                    VXML_SUBMIT_RESOURCE_OK);
        check_equal(probe.close_calls, (size_t)1u);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }
}
