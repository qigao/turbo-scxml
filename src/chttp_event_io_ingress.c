#include "chttp_event_io_internal.h"
#include "scxml_analyze.h"

#include <turbo/error_codes.h>

#include <ctype.h>
#include <string.h>

static const char DEFAULT_EVENT_NAME[] = "HTTP.POST";
static const char FORM_MEDIA_TYPE[] =
    "application/x-www-form-urlencoded";

static bool ascii_equal_fold(
    const char *left, const char *right, size_t size) {
    size_t index;
    for (index = 0u; index < size; ++index) {
        if (tolower((unsigned char)left[index]) !=
            tolower((unsigned char)right[index]))
            return false;
    }
    return true;
}

static bool is_form_content_type(const char *content_type) {
    const size_t media_size = sizeof(FORM_MEDIA_TYPE) - 1u;
    size_t size;
    if (content_type == NULL) return false;
    size = strlen(content_type);
    if (size < media_size ||
        !ascii_equal_fold(content_type, FORM_MEDIA_TYPE, media_size))
        return false;
    content_type += media_size;
    while (*content_type == ' ' || *content_type == '\t') ++content_type;
    return *content_type == '\0' || *content_type == ';';
}

static unsigned int decode_status_to_http(
    scxml_chttp_decode_status status) {
    switch (status) {
        case SCXML_CHTTP_DECODE_BAD_REQUEST:
        case SCXML_CHTTP_DECODE_LIMIT_EXCEEDED:
            return 400u;
        case SCXML_CHTTP_DECODE_UNSUPPORTED_MEDIA:
            return 415u;
        case SCXML_CHTTP_DECODE_FAILED:
            return 422u;
        case SCXML_CHTTP_DECODE_OK:
        default:
            return 500u;
    }
}

static unsigned int mailbox_status_to_http(cflow_mailbox_status status) {
    switch (status) {
        case CFLOW_MAILBOX_OK:
            return 204u;
        case CFLOW_MAILBOX_FULL:
            return 503u;
        case CFLOW_MAILBOX_CLOSED:
        case CFLOW_MAILBOX_CANCELLED:
            return 410u;
        case CFLOW_MAILBOX_INVALID_ARGUMENT:
        case CFLOW_MAILBOX_TYPE_MISMATCH:
        case CFLOW_MAILBOX_BUFFER_TOO_SMALL:
            return 422u;
        case CFLOW_MAILBOX_ALLOCATION_FAILED:
            return 500u;
        case CFLOW_MAILBOX_EMPTY:
        default:
            return 500u;
    }
}

static int reply_without_binding(
    scxml_chttp_processor_impl *processor,
    chttp_server_response *response, unsigned int status) {
    int result;
    result = chttp_server_reply(response, status, NULL, NULL, 0u);
    turbo_mutex_lock(&processor->lock);
    ++processor->ingress_rejected;
    turbo_mutex_unlock(&processor->lock);
    return result;
}

static int ingress_handler(
    void *user, const chttp_server_request_view *request,
    chttp_server_response *response) {
    scxml_chttp_processor_impl *processor =
        (scxml_chttp_processor_impl *)user;
    scxml_chttp_binding_impl *binding = NULL;
    scxml_session *session = NULL;
    const scxml_program *program = NULL;
    scxml_chttp_decode_fn decode = NULL;
    void *decode_user = NULL;
    const char *endpoint;
    const char *event_name = DEFAULT_EVENT_NAME;
    size_t event_name_size = sizeof(DEFAULT_EVENT_NAME) - 1u;
    const char *content_type;
    scxml_chttp_ingress_view ingress = {0};
    scxml_chttp_decoded_form form = {0};
    scxml_content_view data = {0};
    scxml_event_metadata metadata = {
        .abi_version = SCXML_EVENT_METADATA_ABI,
        .struct_size = sizeof(metadata),
        .origin_type = SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI,
        .origin_type_size =
            sizeof(SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI) - 1u};
    scxml_chttp_codec_limits limits;
    unsigned int http_status = 500u;
    size_t endpoint_size;
    size_t required_text_size = 0u;
    size_t index;
    int reply_status;

    if (processor == NULL || request == NULL || response == NULL)
        return TURBO_EINVAL;
    endpoint = chttp_server_request_param(request, "endpoint");
    turbo_mutex_lock(&processor->lock);
    ++processor->ingress_requests;
    turbo_mutex_unlock(&processor->lock);
    if (endpoint == NULL)
        return reply_without_binding(processor, response, 404u);
    endpoint_size = strlen(endpoint);
    turbo_mutex_lock(&processor->lock);
    for (index = 0u; index < processor->config.endpoint_capacity; ++index) {
        scxml_chttp_endpoint_row *row = &processor->endpoints[index];
        if (row->binding != NULL &&
            strlen(row->endpoint) == endpoint_size &&
            memcmp(row->endpoint, endpoint, endpoint_size) == 0) {
            binding = row->binding;
            break;
        }
    }
    if (binding == NULL) {
        turbo_mutex_unlock(&processor->lock);
        return reply_without_binding(processor, response, 404u);
    }
    if (binding->state != SCXML_CHTTP_BINDING_ACTIVE ||
        binding->session == NULL || binding->program == NULL) {
        turbo_mutex_unlock(&processor->lock);
        return reply_without_binding(processor, response, 410u);
    }
    ++binding->active_callbacks;
    session = binding->session;
    program = binding->program;
    decode = binding->decode;
    decode_user = binding->decode_user;
    turbo_mutex_unlock(&processor->lock);

    (void)program;
    content_type = chttp_server_request_header(request, "Content-Type");
    ingress.request = request;
    ingress.content = request->body;
    ingress.content_size = request->body_size;
    if (is_form_content_type(content_type)) {
        scxml_chttp_decode_status decode_status;
        limits = (scxml_chttp_codec_limits){
            .max_event_name_bytes = processor->config.max_event_name_bytes,
            .max_form_entry_count = processor->config.max_form_entry_count,
            .max_form_name_bytes = processor->config.max_form_name_bytes,
            .max_form_value_bytes = processor->config.max_form_value_bytes,
            .max_encoded_body_bytes =
                processor->config.max_encoded_body_bytes};
        decode_status = scxml_chttp_codec_decode_form(
            request->body, request->body_size, &limits,
            processor->ingress_entries,
            processor->config.max_form_entry_count,
            processor->ingress_text, processor->ingress_text_capacity,
            &required_text_size, &form);
        if (decode_status != SCXML_CHTTP_DECODE_OK) {
            http_status = decode_status_to_http(decode_status);
            goto reply;
        }
        event_name = form.event;
        event_name_size = form.event_size;
        ingress.entries = form.entries;
        ingress.entry_count = form.entry_count;
    }
    if (!scxml_analyze_is_xml_nmtoken((turbo_xml_string_view){
            event_name, event_name_size})) {
        http_status = 400u;
        goto reply;
    }
    if (decode != NULL) {
        const scxml_chttp_decode_status decode_status =
            decode(decode_user, &ingress, &data);
        if (decode_status != SCXML_CHTTP_DECODE_OK) {
            http_status = decode_status_to_http(decode_status);
            goto reply;
        }
    } else if (!scxml_chttp_codec_utf8_valid(
                   request->body, request->body_size)) {
        http_status = 422u;
        goto reply;
    } else if (request->body_size != 0u) {
        data = (scxml_content_view){
            .kind = SCXML_CONTENT_TEXT_UTF8,
            .bytes = (const char *)request->body,
            .byte_count = request->body_size};
    }
    metadata.data = data;
    http_status = mailbox_status_to_http(
        scxml_session_try_send_named_with_metadata(
            session, event_name, event_name_size, &metadata));

reply:
    reply_status = chttp_server_reply(
        response, http_status, NULL, NULL, 0u);
    turbo_mutex_lock(&processor->lock);
    if (http_status == 204u)
        ++processor->ingress_admitted;
    else
        ++processor->ingress_rejected;
    if (binding->active_callbacks != 0u)
        --binding->active_callbacks;
    else
        ++processor->invariant_failures;
    turbo_cond_broadcast(&processor->wake);
    turbo_mutex_unlock(&processor->lock);
    return reply_status;
}

int scxml_chttp_ingress_register(scxml_chttp_processor_impl *processor) {
    if (processor == NULL || processor->route_path == NULL)
        return TURBO_EINVAL;
    return chttp_server_post(
        &processor->server, processor->route_path,
        ingress_handler, processor);
}
