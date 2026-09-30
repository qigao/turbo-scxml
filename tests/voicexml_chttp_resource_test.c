#include <voicexml/chttp_resource.h>

#include "scxml_chttp_resource_internal.h"

#include <salts/error_codes.h>
#include <tinytest.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct voice_chttp_probe {
    scxml_resource_status resolve_status;
    scxml_chttp_resolution_v1 resolution;
    size_t resolve_calls;
    size_t get_calls;
    size_t response_destroy_calls;
    int transport_status;
    unsigned int response_status;
    const char *response_media_type;
    chttp_header response_headers[2];
    size_t response_header_count;
    const void *response_body;
    size_t response_body_size;
    uint32_t observed_timeout_ms;
} voice_chttp_probe;

static const char voice_document[] =
    "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
    "<form id='main'><block><exit/></block></form></vxml>";

static scxml_resource_status voice_resolve(
    void *user, const char *uri, size_t uri_size,
    scxml_chttp_resource_kind kind,
    scxml_chttp_resolution_v1 *out_resolution,
    const scxml_chttp_data_decoder_v1 **out_decoder,
    void **out_decoder_user) {
    voice_chttp_probe *probe = (voice_chttp_probe *)user;
    static const char logical[] = "voice:main";
    if (probe == NULL || out_resolution == NULL ||
        out_decoder == NULL || out_decoder_user == NULL ||
        uri == NULL || uri_size != sizeof(logical) - 1u ||
        memcmp(uri, logical, uri_size) != 0 ||
        kind != SCXML_CHTTP_RESOURCE_TEXT)
        return SCXML_RESOURCE_FAILED;
    ++probe->resolve_calls;
    if (probe->resolve_status != SCXML_RESOURCE_OK)
        return probe->resolve_status;
    *out_resolution = probe->resolution;
    *out_decoder = NULL;
    *out_decoder_user = NULL;
    return SCXML_RESOURCE_OK;
}

static int voice_get(
    void *user, chttp_client *client,
    const chttp_options *options,
    chttp_response *out_response,
    chttp_error *out_error) {
    voice_chttp_probe *probe = (voice_chttp_probe *)user;
    (void)client;
    if (probe == NULL || options == NULL ||
        out_response == NULL || out_error == NULL)
        return SALTS_EINVAL;
    ++probe->get_calls;
    probe->observed_timeout_ms = options->timeout_ms;
    if (probe->transport_status != SALTS_OK) {
        out_error->status = probe->transport_status;
        out_error->stage = "voice-chttp-test";
        return probe->transport_status;
    }
    if (probe->response_header_count != 0u)
        probe->response_headers[0].value =
            probe->response_media_type;
    *out_response = (chttp_response){
        .status_code = probe->response_status,
        .headers = probe->response_headers,
        .header_count = probe->response_header_count,
        .body = (void *)probe->response_body,
        .body_size = probe->response_body_size};
    return SALTS_OK;
}

static void voice_response_destroy(
    void *user, chttp_response *response) {
    voice_chttp_probe *probe = (voice_chttp_probe *)user;
    if (probe != NULL) ++probe->response_destroy_calls;
    if (response != NULL) memset(response, 0, sizeof(*response));
}

static const scxml_chttp_transport_v1 voice_transport = {
    .get = voice_get,
    .response_destroy = voice_response_destroy};

static voice_chttp_probe default_probe(void) {
    voice_chttp_probe probe = {
        .resolve_status = SCXML_RESOURCE_OK,
        .transport_status = SALTS_OK,
        .response_status = 200u,
        .response_media_type = "application/voicexml+xml",
        .response_body = voice_document,
        .response_body_size = sizeof(voice_document) - 1u};
    probe.resolution = (scxml_chttp_resolution_v1){
        .abi_version = SCXML_CHTTP_RESOLUTION_ABI_V1,
        .struct_size = sizeof(scxml_chttp_resolution_v1),
        .connection_uri = "tcp://127.0.0.1:8080",
        .connection_uri_size = sizeof("tcp://127.0.0.1:8080") - 1u,
        .authority = "voice.internal",
        .authority_size = sizeof("voice.internal") - 1u,
        .target = "/dialogs/main.vxml",
        .target_size = sizeof("/dialogs/main.vxml") - 1u,
        .media_type = "application/voicexml+xml",
        .media_type_size =
            sizeof("application/voicexml+xml") - 1u};
    probe.response_headers[0] = (chttp_header){
        .name = "Content-Type",
        .value = "application/voicexml+xml"};
    probe.response_header_count = 1u;
    return probe;
}

static scxml_chttp_resource_config_v1 base_config(
    voice_chttp_probe *probe) {
    static chttp_client borrowed_client;
    borrowed_client.impl = &borrowed_client;
    return (scxml_chttp_resource_config_v1){
        .abi_version = SCXML_CHTTP_RESOURCE_CONFIG_ABI_V1,
        .struct_size = sizeof(scxml_chttp_resource_config_v1),
        .client = &borrowed_client,
        .resolve = voice_resolve,
        .resolver_user = probe,
        .timeout_ms = 250u,
        .max_connection_uri_bytes = 64u,
        .max_authority_bytes = 64u,
        .max_target_bytes = 128u,
        .max_media_type_bytes = 64u,
        .max_response_body_bytes = 512u};
}

static void init_bridge(
    scxml_chttp_resource *base,
    vxml_chttp_resource *voice,
    voice_chttp_probe *probe) {
    const scxml_chttp_resource_config_v1 config =
        base_config(probe);
    const vxml_chttp_resource_config_v1 voice_config = {
        .abi_version = VXML_CHTTP_RESOURCE_CONFIG_ABI_V1,
        .struct_size = sizeof(vxml_chttp_resource_config_v1),
        .resource = base};
    check_equal(
        scxml_chttp_resource_init(base, &config),
        SCXML_OK);
    check_equal(
        scxml_chttp_resource_set_transport_for_test(
            base, &voice_transport, probe),
        SCXML_OK);
    check_equal(
        vxml_chttp_resource_init(voice, &voice_config),
        VXML_CHTTP_RESOURCE_OK);
}

spec("VoiceXML CHTTP document resource bridge") {
    it("borrows the underlying CHTTP text lease until document close") {
        static const char source[] = "voice:main";
        static const char media[] = "application/voicexml+xml";
        voice_chttp_probe probe = default_probe();
        scxml_chttp_resource base = {0};
        vxml_chttp_resource voice = {0};
        const vxml_dialog_document_adapter_v1 *adapter;
        vxml_dialog_document document = {0};
        scxml_resource_status status = SCXML_RESOURCE_FAILED;

        init_bridge(&base, &voice, &probe);
        adapter = vxml_chttp_resource_document_adapter(&voice);
        check_not_null(adapter);

        check_equal(
            adapter->open(
                &voice, source, sizeof(source) - 1u,
                media, sizeof(media) - 1u,
                512u, &document),
            VXML_DIALOG_MANAGER_OK);
        check_equal(probe.resolve_calls, (size_t)1u);
        check_equal(probe.get_calls, (size_t)1u);
        check_equal(probe.observed_timeout_ms, (uint32_t)250u);
        check_equal(probe.response_destroy_calls, (size_t)0u);
        check_equal(document.size, sizeof(voice_document) - 1u);
        check_equal(
            memcmp(document.data, voice_document, document.size), 0);
        check_true(vxml_chttp_resource_last_status(&voice, &status));
        check_equal(status, SCXML_RESOURCE_OK);

        check_equal(
            vxml_chttp_resource_destroy(&voice),
            VXML_CHTTP_RESOURCE_BUSY);
        check_equal(
            scxml_chttp_resource_destroy(&base),
            SCXML_INVALID_ARGUMENT);

        adapter->close(&voice, &document);
        check_equal(probe.response_destroy_calls, (size_t)1u);
        check_null(document.data);
        check_null(document.lease);

        check_equal(
            vxml_chttp_resource_destroy(&voice),
            VXML_CHTTP_RESOURCE_OK);
        check_equal(
            scxml_chttp_resource_destroy(&base),
            SCXML_OK);
    }

    it("fails closed when a per-request timeout cannot be honored") {
        static const char source[] = "voice:main";
        static const char media[] = "application/voicexml+xml";
        voice_chttp_probe probe = default_probe();
        scxml_chttp_resource base = {0};
        vxml_chttp_resource voice = {0};
        const vxml_dialog_document_adapter_v1 *adapter;
        vxml_dialog_document document = {0};
        vxml_document_fetch_policy_v1 policy =
            VXML_DOCUMENT_FETCH_POLICY_V1_INIT;
        scxml_resource_status status = SCXML_RESOURCE_OK;

        policy.has_timeout = true;
        policy.timeout_us = UINT64_C(250000);
        init_bridge(&base, &voice, &probe);
        adapter = vxml_chttp_resource_document_adapter(&voice);
        check_not_null(adapter);
        check_not_null(adapter->open_with_policy);
        check_equal(
            adapter->open_with_policy(
                &voice, source, sizeof(source) - 1u,
                media, sizeof(media) - 1u,
                512u, &policy, &document),
            VXML_DIALOG_MANAGER_DOCUMENT_ERROR);
        check_equal(probe.resolve_calls, (size_t)0u);
        check_equal(probe.get_calls, (size_t)0u);
        check_null(document.lease);
        check_true(vxml_chttp_resource_last_status(&voice, &status));
        check_equal(status, SCXML_RESOURCE_FAILED);
        check_equal(vxml_chttp_resource_destroy(&voice),
                    VXML_CHTTP_RESOURCE_OK);
        check_equal(scxml_chttp_resource_destroy(&base), SCXML_OK);
    }

    it("preserves exact Content-Type mismatch as underlying invalid data") {
        static const char source[] = "voice:main";
        static const char media[] = "application/voicexml+xml";
        voice_chttp_probe probe = default_probe();
        scxml_chttp_resource base = {0};
        vxml_chttp_resource voice = {0};
        vxml_dialog_document document = {0};
        scxml_resource_status status = SCXML_RESOURCE_OK;

        probe.response_media_type = "text/xml";
        init_bridge(&base, &voice, &probe);
        check_equal(
            vxml_chttp_resource_document_adapter(&voice)->open(
                &voice, source, sizeof(source) - 1u,
                media, sizeof(media) - 1u,
                512u, &document),
            VXML_DIALOG_MANAGER_DOCUMENT_ERROR);
        check_true(vxml_chttp_resource_last_status(&voice, &status));
        check_equal(status, SCXML_RESOURCE_INVALID_DATA);
        check_equal(probe.response_destroy_calls, (size_t)1u);
        check_null(document.lease);
        check_equal(
            vxml_chttp_resource_destroy(&voice),
            VXML_CHTTP_RESOURCE_OK);
        check_equal(
            scxml_chttp_resource_destroy(&base),
            SCXML_OK);
    }

    it("preserves transport timeout without publishing a document lease") {
        static const char source[] = "voice:main";
        static const char media[] = "application/voicexml+xml";
        voice_chttp_probe probe = default_probe();
        scxml_chttp_resource base = {0};
        vxml_chttp_resource voice = {0};
        vxml_dialog_document document = {0};
        scxml_resource_status status = SCXML_RESOURCE_OK;

        probe.transport_status = SALTS_ETIMEDOUT;
        init_bridge(&base, &voice, &probe);
        check_equal(
            vxml_chttp_resource_document_adapter(&voice)->open(
                &voice, source, sizeof(source) - 1u,
                media, sizeof(media) - 1u,
                512u, &document),
            VXML_DIALOG_MANAGER_DOCUMENT_ERROR);
        check_true(vxml_chttp_resource_last_status(&voice, &status));
        check_equal(status, SCXML_RESOURCE_TIMEOUT);
        check_equal(probe.response_destroy_calls, (size_t)0u);
        check_null(document.lease);
        check_equal(
            vxml_chttp_resource_destroy(&voice),
            VXML_CHTTP_RESOURCE_OK);
        check_equal(
            scxml_chttp_resource_destroy(&base),
            SCXML_OK);
    }

    it("preserves caller document byte bounds before publishing a lease") {
        static const char source[] = "voice:main";
        static const char media[] = "application/voicexml+xml";
        voice_chttp_probe probe = default_probe();
        scxml_chttp_resource base = {0};
        vxml_chttp_resource voice = {0};
        vxml_dialog_document document = {0};
        scxml_resource_status status = SCXML_RESOURCE_OK;

        init_bridge(&base, &voice, &probe);
        check_equal(
            vxml_chttp_resource_document_adapter(&voice)->open(
                &voice, source, sizeof(source) - 1u,
                media, sizeof(media) - 1u,
                16u, &document),
            VXML_DIALOG_MANAGER_DOCUMENT_ERROR);
        check_true(vxml_chttp_resource_last_status(&voice, &status));
        check_equal(status, SCXML_RESOURCE_LIMIT_EXCEEDED);
        check_equal(probe.response_destroy_calls, (size_t)1u);
        check_null(document.lease);
        check_equal(
            vxml_chttp_resource_destroy(&voice),
            VXML_CHTTP_RESOURCE_OK);
        check_equal(
            scxml_chttp_resource_destroy(&base),
            SCXML_OK);
    }

    it("retains the underlying logical-URI authorization failure") {
        static const char source[] = "https://voice.invalid/main.vxml";
        static const char media[] = "application/voicexml+xml";
        voice_chttp_probe probe = default_probe();
        scxml_chttp_resource base = {0};
        vxml_chttp_resource voice = {0};
        vxml_dialog_document document = {0};
        scxml_resource_status status = SCXML_RESOURCE_OK;

        init_bridge(&base, &voice, &probe);
        check_equal(
            vxml_chttp_resource_document_adapter(&voice)->open(
                &voice, source, sizeof(source) - 1u,
                media, sizeof(media) - 1u,
                512u, &document),
            VXML_DIALOG_MANAGER_DOCUMENT_ERROR);
        check_true(vxml_chttp_resource_last_status(&voice, &status));
        check_equal(status, SCXML_RESOURCE_DENIED);
        check_equal(probe.resolve_calls, (size_t)0u);
        check_equal(probe.get_calls, (size_t)0u);
        check_null(document.lease);
        check_equal(
            vxml_chttp_resource_destroy(&voice),
            VXML_CHTTP_RESOURCE_OK);
        check_equal(
            scxml_chttp_resource_destroy(&base),
            SCXML_OK);
    }

    it("rejects a second active document without disturbing the first lease") {
        static const char source[] = "voice:main";
        static const char media[] = "application/voicexml+xml";
        voice_chttp_probe probe = default_probe();
        scxml_chttp_resource base = {0};
        vxml_chttp_resource voice = {0};
        const vxml_dialog_document_adapter_v1 *adapter;
        vxml_dialog_document first = {0};
        vxml_dialog_document second = {0};

        init_bridge(&base, &voice, &probe);
        adapter = vxml_chttp_resource_document_adapter(&voice);
        check_equal(
            adapter->open(
                &voice, source, sizeof(source) - 1u,
                media, sizeof(media) - 1u,
                512u, &first),
            VXML_DIALOG_MANAGER_OK);
        check_equal(
            adapter->open(
                &voice, source, sizeof(source) - 1u,
                media, sizeof(media) - 1u,
                512u, &second),
            VXML_DIALOG_MANAGER_INVALID_ARGUMENT);
        check_equal(probe.resolve_calls, (size_t)1u);
        check_equal(probe.get_calls, (size_t)1u);
        check_null(second.lease);

        adapter->close(&voice, &first);
        check_equal(probe.response_destroy_calls, (size_t)1u);
        check_equal(
            vxml_chttp_resource_destroy(&voice),
            VXML_CHTTP_RESOURCE_OK);
        check_equal(
            scxml_chttp_resource_destroy(&base),
            SCXML_OK);
    }
}
