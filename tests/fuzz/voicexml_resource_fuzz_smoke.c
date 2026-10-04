#include "voicexml_fuzz_common.h"

#include <voicexml/document_store.h>
#include <voicexml/submit_resource.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef VOICEXML_RESOURCE_FUZZ_CORPUS_DIR
#error "VOICEXML_RESOURCE_FUZZ_CORPUS_DIR must be defined"
#endif

typedef struct resource_fuzz_probe {
    size_t compile_calls;
    size_t wire_v1_calls;
    size_t wire_v2_calls;
} resource_fuzz_probe;

static vxml_dialog_manager_status document_open(
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

static void document_close(
    void *user, vxml_dialog_document *document) {
    (void)user;
    if (document != NULL)
        *document = (vxml_dialog_document){0};
}

static const vxml_dialog_document_adapter_v1 DOCUMENTS = {
    .abi_version = VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1,
    .struct_size = sizeof(vxml_dialog_document_adapter_v1),
    .open = document_open,
    .close = document_close
};

static vxml_status compile_probe(
    void *user,
    const void *source, size_t source_size,
    vxml_program *out_program,
    vxml_diagnostic *diagnostic) {
    resource_fuzz_probe *probe =
        (resource_fuzz_probe *)user;
    (void)source;
    (void)source_size;
    if (probe == NULL || out_program == NULL)
        return VXML_INVALID_ARGUMENT;
    ++probe->compile_calls;
    if (diagnostic != NULL) {
        *diagnostic = (vxml_diagnostic){0};
        diagnostic->status = VXML_UNSUPPORTED_FEATURE;
    }
    return VXML_UNSUPPORTED_FEATURE;
}

static const vxml_document_compile_adapter_v1 COMPILER = {
    .abi_version = VXML_DOCUMENT_COMPILE_ADAPTER_ABI_V1,
    .struct_size = sizeof(vxml_document_compile_adapter_v1),
    .compile = compile_probe
};

static vxml_submit_resource_status execute_v1(
    void *user,
    const vxml_submit_wire_request_v1 *request,
    vxml_submit_response *out_response) {
    resource_fuzz_probe *probe =
        (resource_fuzz_probe *)user;
    if (out_response != NULL)
        *out_response = (vxml_submit_response){0};
    if (probe == NULL || request == NULL ||
        out_response == NULL)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    ++probe->wire_v1_calls;
    return VXML_SUBMIT_RESOURCE_PROVIDER_ERROR;
}

static vxml_submit_resource_status execute_v2(
    void *user,
    const vxml_submit_wire_request_v2 *request,
    vxml_submit_response *out_response) {
    resource_fuzz_probe *probe =
        (resource_fuzz_probe *)user;
    size_t index;
    size_t total = 0u;
    if (out_response != NULL)
        *out_response = (vxml_submit_response){0};
    if (probe == NULL || request == NULL ||
        out_response == NULL ||
        (request->segment_count != 0u &&
         request->segments == NULL))
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    for (index = 0u;
         index < request->segment_count;
         ++index) {
        if (request->segments[index].size >
            SIZE_MAX - total)
            return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
        total += request->segments[index].size;
    }
    if (total != request->body_size)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    ++probe->wire_v2_calls;
    return VXML_SUBMIT_RESOURCE_PROVIDER_ERROR;
}

static void close_response(
    void *user,
    vxml_submit_response *response) {
    (void)user;
    if (response != NULL)
        *response = (vxml_submit_response){0};
}

static const vxml_submit_resource_adapter_v1 SUBMIT = {
    .abi_version = VXML_SUBMIT_RESOURCE_ADAPTER_ABI_V1,
    .struct_size = sizeof(vxml_submit_resource_adapter_v1),
    .execute = execute_v1,
    .close = close_response,
    .execute_v2 = execute_v2,
    .capabilities = VXML_SUBMIT_RESOURCE_CAP_TIMEOUT
};

typedef struct resource_fuzz_context {
    vxml_document_store store;
    resource_fuzz_probe probe;
} resource_fuzz_context;

static int run_case(
    const unsigned char *data,
    size_t size,
    void *user) {
    static const char base[] =
        "https://voice.example/app/root.vxml";
    static const char upload[] = "upload";
    static const char text_name[] = "text";
    static const char recording_name[] = "recording";
    static const char filename[] = "recording.raw";
    static const char media_type[] =
        "application/octet-stream";
    resource_fuzz_context *context =
        (resource_fuzz_context *)user;
    char resolved_uri[512] = {0};
    char fragment[256] = {0};
    vxml_resolved_uri_v1 resolved = {
        .abi_version = 1u,
        .struct_size = sizeof(vxml_resolved_uri_v1),
        .document_uri = resolved_uri,
        .document_uri_capacity = sizeof(resolved_uri),
        .fragment = fragment,
        .fragment_capacity = sizeof(fragment)
    };
    vxml_program program = {0};
    vxml_diagnostic diagnostic = {0};
    vxml_submit_response response = {0};
    vxml_submit_field_v1 field = {
        .name = text_name,
        .name_size = sizeof(text_name) - 1u,
        .value = (const char *)data,
        .value_size = size
    };
    vxml_submit_recording_field_v1 recording = {
        .name = recording_name,
        .name_size = sizeof(recording_name) - 1u,
        .filename = filename,
        .filename_size = sizeof(filename) - 1u,
        .media_type = media_type,
        .media_type_size = sizeof(media_type) - 1u,
        .data = data,
        .size = size
    };
    const vxml_submit_multipart_part_ref_v1 parts[] = {
        {VXML_SUBMIT_MULTIPART_PART_TEXT, 0u},
        {VXML_SUBMIT_MULTIPART_PART_RECORDING, 0u}
    };
    vxml_submit_request_v1 request =
        VXML_SUBMIT_REQUEST_V1_INIT;
    vxml_submit_multipart_request_v1 multipart =
        VXML_SUBMIT_MULTIPART_REQUEST_V1_INIT;
    vxml_document_store_status store_status;
    vxml_submit_resource_status submit_status;
    size_t before;

    if (context == NULL ||
        (size != 0u && data == NULL))
        return 1;

    store_status = vxml_document_store_resolve(
        &context->store,
        base, sizeof(base) - 1u,
        (const char *)data, size,
        &resolved);
    if (store_status != VXML_DOCUMENT_STORE_OK &&
        (resolved.document_uri_size != 0u ||
         resolved.fragment_size != 0u))
        return 1;

    before = context->probe.compile_calls;
    store_status = vxml_document_store_compile_source(
        &context->store,
        data, size, NULL,
        &program, &diagnostic);
    if (size == 0u) {
        if (store_status !=
                VXML_DOCUMENT_STORE_INVALID_ARGUMENT ||
            context->probe.compile_calls != before)
            return 1;
    } else {
        if (store_status !=
                VXML_DOCUMENT_STORE_COMPILE_ERROR ||
            context->probe.compile_calls != before + 1u ||
            program.impl != NULL)
            return 1;
    }
    vxml_program_destroy(&program);

    context->probe.wire_v1_calls = 0u;
    request.base_document_uri = base;
    request.base_document_uri_size =
        sizeof(base) - 1u;
    request.target =
        size != 0u ? (const char *)data : upload;
    request.target_size =
        size != 0u ? size : sizeof(upload) - 1u;
    request.method = VXML_SUBMIT_METHOD_POST;
    request.enctype =
        "application/x-www-form-urlencoded";
    request.enctype_size =
        sizeof("application/x-www-form-urlencoded") - 1u;
    request.fields = &field;
    request.field_count = 1u;
    request.max_uri_bytes = 512u;
    request.max_body_bytes = 4096u;
    request.max_response_bytes = 4096u;
    request.has_timeout =
        size != 0u && (data[0] & 1u) != 0u;
    request.timeout_us =
        request.has_timeout ? UINT64_C(1000) : UINT64_C(0);

    submit_status = vxml_submit_resource_execute(
        &context->store, &SUBMIT,
        &context->probe, &request, &response);
    if (context->probe.wire_v1_calls > 1u ||
        response.lease != NULL)
        return 1;
    if (context->probe.wire_v1_calls == 1u &&
        submit_status != VXML_SUBMIT_RESOURCE_PROVIDER_ERROR)
        return 1;

    context->probe.wire_v2_calls = 0u;
    response = (vxml_submit_response){0};
    multipart.base_document_uri = base;
    multipart.base_document_uri_size =
        sizeof(base) - 1u;
    multipart.target = upload;
    multipart.target_size = sizeof(upload) - 1u;
    multipart.fields = &field;
    multipart.field_count = 1u;
    multipart.recordings = &recording;
    multipart.recording_count = 1u;
    multipart.max_uri_bytes = 512u;
    multipart.max_body_bytes = 8192u;
    multipart.max_response_bytes = 4096u;
    multipart.max_parts = 4u;
    multipart.max_boundary_bytes = 96u;
    multipart.max_header_bytes = 2048u;
    multipart.max_segments = 32u;
    multipart.parts = parts;
    multipart.part_count =
        sizeof(parts) / sizeof(parts[0]);
    multipart.has_timeout =
        size != 0u && (data[0] & 2u) != 0u;
    multipart.timeout_us =
        multipart.has_timeout ? UINT64_C(2000) : UINT64_C(0);

    submit_status =
        vxml_submit_resource_execute_multipart(
            &context->store, &SUBMIT,
            &context->probe, &multipart, &response);
    if (context->probe.wire_v2_calls > 1u ||
        response.lease != NULL)
        return 1;
    if (context->probe.wire_v2_calls == 1u &&
        submit_status != VXML_SUBMIT_RESOURCE_PROVIDER_ERROR)
        return 1;

    return 0;
}

int main(void) {
    static const char application_uri[] =
        "https://voice.example/app/root.vxml";
    static const char *const seeds[] = {
        "uri-relative.txt",
        "uri-query-fragment.txt",
        "field-entities.txt",
        "binary-edge.txt"
    };
    resource_fuzz_context context = {0};
    vxml_document_store_config_v1 config = {
        .abi_version = VXML_DOCUMENT_STORE_CONFIG_ABI_V1,
        .struct_size = sizeof(vxml_document_store_config_v1),
        .application_uri = application_uri,
        .application_uri_size =
            sizeof(application_uri) - 1u,
        .capacity = 1u,
        .max_uri_bytes = 511u,
        .max_document_bytes = 4096u,
        .max_cache_bytes = 8192u,
        .voice_limits = {0},
        .documents = &DOCUMENTS,
        .document_user = &context.probe,
        .compiler = &COMPILER,
        .compiler_user = &context.probe
    };
    int result;

    config.voice_limits = vxml_default_limits();
    if (vxml_document_store_init(
            &context.store, &config) !=
        VXML_DOCUMENT_STORE_OK)
        return 1;

    result = voicexml_fuzz_run(
        VOICEXML_RESOURCE_FUZZ_CORPUS_DIR,
        seeds, sizeof(seeds) / sizeof(seeds[0]),
        16u, 4096u, run_case, &context);

    if (vxml_document_store_destroy(
            &context.store) !=
        VXML_DOCUMENT_STORE_OK)
        result = 1;
    if (result != 0)
        fprintf(stderr,
                "VoiceXML resource fuzz smoke failed\n");
    return result;
}
