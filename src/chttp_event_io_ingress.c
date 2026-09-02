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
    if (content_type == NULL || strlen(content_type) < media_size ||
        !ascii_equal_fold(content_type, FORM_MEDIA_TYPE, media_size))
        return false;
    return content_type[media_size] == '\0' ||
           content_type[media_size] == ';';
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

static int ingress_handler(
    void *user, const chttp_server_request_view *request,
    chttp_server_response *response) {
    scxml_chttp_processor_impl *processor =
        (scxml_chttp_processor_impl *)user;
    scxml_chttp_binding_impl *binding = NULL;
    scxml_session *session = NULL;
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
    unsigned int http_status = 500u;
    size_t endpoint_size;
    size_t index;
    int reply_status;

    if (processor == NULL || request == NULL || response == NULL)
        return TURBO_EINVAL;
    endpoint = chttp_server_request_param(request, "endpoint");
    if (endpoint == NULL)
        return chttp_server_reply(response, 404u, NULL, NULL, 0u);
    endpoint_size = strlen(endpoint);
    turbo_mutex_lock(&processor->mutex);
    ++processor->stats.ingress_requests;
    for (index = 0u; index < processor->endpoint_capacity; ++index) {
        scxml_chttp_binding_impl *candidate = &processor->bindings[index];
        if (candidate->state != SCXML_CHTTP_BINDING_FREE &&
            candidate->endpoint_size == endpoint_size &&
            memcmp(candidate->endpoint, endpoint, endpoint_size) == 0) {
            binding = candidate;
            break;
        }
    }
    if (binding == NULL) {
        ++processor->stats.ingress_rejected;
        turbo_mutex_unlock(&processor->mutex);
        return chttp_server_reply(response, 404u, NULL, NULL, 0u);
    }
    if (binding->state != SCXML_CHTTP_BINDING_ACTIVE ||
        binding->session == NULL || binding->program == NULL) {
        ++processor->stats.ingress_rejected;
        turbo_mutex_unlock(&processor->mutex);
        return chttp_server_reply(response, 410u, NULL, NULL, 0u);
    }
    ++binding->active_handlers;
    session = binding->session;
    decode = binding->decode;
    decode_user = binding->decode_user;
    turbo_mutex_unlock(&processor->mutex);

    content_type = chttp_server_request_header(request, "Content-Type");
    ingress.request = request;
    ingress.content = request->body;
    ingress.content_size = request->body_size;
    if (is_form_content_type(content_type)) {
        scxml_chttp_decode_status decode_status =
            scxml_chttp_decode_form_body(
                request->body, request->body_size,
                processor->ingress_storage,
                processor->ingress_storage_capacity,
                processor->ingress_entries,
                processor->max_form_entry_count,
                processor->max_event_name_bytes,
                processor->max_form_name_bytes,
                processor->max_form_value_bytes, &form);
        if (decode_status != SCXML_CHTTP_DECODE_OK) {
            http_status = decode_status_to_http(decode_status);
            goto reply;
        }
        event_name = form.event_name;
        event_name_size = form.event_name_size;
        ingress.entries = form.entries;
        ingress.entry_count = form.entry_count;
    }
    if (!scxml_analyze_is_xml_nmtoken((turbo_xml_string_view){
            event_name, event_name_size})) {
        http_status = 400u;
        goto reply;
    }
    if (decode != NULL) {
        scxml_chttp_decode_status decode_status =
            decode(decode_user, &ingress, &data);
        if (decode_status != SCXML_CHTTP_DECODE_OK) {
            http_status = decode_status_to_http(decode_status);
            goto reply;
        }
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
    turbo_mutex_lock(&processor->mutex);
    if (http_status == 204u) {
        ++processor->stats.ingress_admitted;
    } else {
        ++processor->stats.ingress_rejected;
    }
    if (binding->active_handlers != 0u) {
        --binding->active_handlers;
    } else {
        ++processor->stats.invariant_failures;
    }
    turbo_cond_broadcast(&processor->condition);
    turbo_mutex_unlock(&processor->mutex);
    return reply_status;
}

int scxml_chttp_ingress_register(scxml_chttp_processor_impl *processor) {
    if (processor == NULL || processor->route_path == NULL)
        return TURBO_EINVAL;
    return chttp_server_post(
        &processor->server, processor->route_path,
        ingress_handler, processor);
}
