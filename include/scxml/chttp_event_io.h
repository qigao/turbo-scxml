#ifndef SCXML_CHTTP_EVENT_IO_H
#define SCXML_CHTTP_EVENT_IO_H

#include <scxml/scxml.h>
#include <chttp/chttp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCXML_CHTTP_ABI_V1 1u
#define SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI \
    "http://www.w3.org/TR/scxml/#BasicHTTPEventProcessor"

typedef struct scxml_chttp_processor { void *impl; } scxml_chttp_processor;
typedef struct scxml_chttp_binding { void *impl; } scxml_chttp_binding;

typedef enum scxml_chttp_decode_status {
    SCXML_CHTTP_DECODE_OK = 0,
    SCXML_CHTTP_DECODE_BAD_REQUEST,
    SCXML_CHTTP_DECODE_UNSUPPORTED_MEDIA,
    SCXML_CHTTP_DECODE_LIMIT_EXCEEDED,
    SCXML_CHTTP_DECODE_FAILED
} scxml_chttp_decode_status;

typedef struct scxml_chttp_form_entry_view {
    const char *name;
    size_t name_size;
    const char *value;
    size_t value_size;
} scxml_chttp_form_entry_view;

typedef struct scxml_chttp_ingress_view {
    const chttp_server_request_view *request;
    const scxml_chttp_form_entry_view *entries;
    size_t entry_count;
    const void *content;
    size_t content_size;
} scxml_chttp_ingress_view;

typedef scxml_chttp_decode_status (*scxml_chttp_decode_fn)(
    void *user, const scxml_chttp_ingress_view *ingress,
    scxml_content_view *out_data);

typedef struct scxml_chttp_resolved_target {
    const char *connection_uri;
    const char *authority;
    const char *target;
} scxml_chttp_resolved_target;

typedef int (*scxml_chttp_resolve_fn)(
    void *user, const char *uri, size_t uri_size,
    scxml_chttp_resolved_target *out_target);

typedef struct scxml_chttp_processor_config_v1 {
    uint32_t abi_version;
    size_t struct_size;
    chttp_server_config server;
    chttp_client_config client;
    const char *advertised_authority;
    const char *base_path;
    size_t endpoint_capacity;
    size_t egress_capacity;
    size_t max_access_uri_bytes;
    size_t max_event_name_bytes;
    size_t max_form_entry_count;
    size_t max_form_name_bytes;
    size_t max_form_value_bytes;
    size_t max_encoded_body_bytes;
    uint32_t request_timeout_ms;
    uint32_t worker_poll_ms;
    scxml_chttp_resolve_fn resolve;
    void *resolve_user;
} scxml_chttp_processor_config_v1;

typedef struct scxml_chttp_binding_config_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const scxml_event_io_adapter *scxml_adapter;
    void *scxml_adapter_user;
    scxml_chttp_decode_fn decode;
    void *decode_user;
} scxml_chttp_binding_config_v1;

typedef struct scxml_chttp_processor_stats {
    uint64_t egress_accepted;
    uint64_t egress_completed;
    uint64_t egress_failed;
    uint64_t egress_cancelled;
    uint64_t ingress_requests;
    uint64_t ingress_admitted;
    uint64_t ingress_rejected;
    uint64_t invariant_failures;
    size_t active_bindings;
    size_t queued_egress;
    size_t in_flight_egress;
} scxml_chttp_processor_stats;

int scxml_chttp_processor_init(
    scxml_chttp_processor *processor,
    const scxml_chttp_processor_config_v1 *config);
int scxml_chttp_processor_start(scxml_chttp_processor *processor);
int scxml_chttp_processor_stop(
    scxml_chttp_processor *processor, uint32_t timeout_ms);
int scxml_chttp_processor_destroy(scxml_chttp_processor *processor);

int scxml_chttp_binding_init(
    scxml_chttp_binding *binding, scxml_chttp_processor *processor,
    const scxml_chttp_binding_config_v1 *config);
const scxml_event_io_adapter *scxml_chttp_event_io_adapter(void);
void *scxml_chttp_binding_adapter_user(scxml_chttp_binding *binding);
bool scxml_chttp_binding_ioprocessor(
    const scxml_chttp_binding *binding,
    scxml_ioprocessor_descriptor *out_descriptor);
int scxml_chttp_binding_activate(
    scxml_chttp_binding *binding, scxml_session *session,
    const scxml_program *program);
int scxml_chttp_binding_destroy(scxml_chttp_binding *binding);

bool scxml_chttp_processor_get_stats(
    const scxml_chttp_processor *processor,
    scxml_chttp_processor_stats *out_stats);

#ifdef __cplusplus
}
#endif

#endif
