#ifndef SCXML_CHTTP_EVENT_IO_INTERNAL_H
#define SCXML_CHTTP_EVENT_IO_INTERNAL_H

#include <scxml/chttp_event_io.h>

#include <turbo/thread.h>

typedef enum scxml_chttp_processor_state {
    SCXML_CHTTP_PROCESSOR_INITIALIZED = 1,
    SCXML_CHTTP_PROCESSOR_RUNNING,
    SCXML_CHTTP_PROCESSOR_STOPPING,
    SCXML_CHTTP_PROCESSOR_STOPPED
} scxml_chttp_processor_state;

typedef enum scxml_chttp_binding_state {
    SCXML_CHTTP_BINDING_FREE = 0,
    SCXML_CHTTP_BINDING_RESERVED,
    SCXML_CHTTP_BINDING_ACTIVE,
    SCXML_CHTTP_BINDING_CLOSING,
    SCXML_CHTTP_BINDING_QUIESCENT
} scxml_chttp_binding_state;

typedef enum scxml_chttp_egress_state {
    SCXML_CHTTP_EGRESS_FREE = 0,
    SCXML_CHTTP_EGRESS_RESERVED,
    SCXML_CHTTP_EGRESS_READY,
    SCXML_CHTTP_EGRESS_SUBMITTING,
    SCXML_CHTTP_EGRESS_SUBMITTED,
    SCXML_CHTTP_EGRESS_COMPLETING
} scxml_chttp_egress_state;

typedef struct scxml_chttp_processor_impl scxml_chttp_processor_impl;

typedef struct scxml_chttp_binding_impl {
    scxml_chttp_processor_impl *processor;
    scxml_chttp_binding_state state;
    size_t slot;
    scxml_event_io_adapter downstream;
    void *downstream_user;
    scxml_chttp_decode_fn decode;
    void *decode_user;
    scxml_session *session;
    const scxml_program *program;
    char *access_uri;
    size_t access_uri_size;
    const char *endpoint;
    size_t endpoint_size;
    size_t active_handlers;
    size_t outbound_refs;
    uint64_t next_commit_sequence;
    bool downstream_closed;
} scxml_chttp_binding_impl;

typedef struct scxml_chttp_egress_row {
    scxml_chttp_processor_impl *processor;
    scxml_chttp_egress_state state;
    uint32_t generation;
    scxml_chttp_binding_impl *binding;
    char *connection_uri;
    char *authority;
    char *target;
    char *body;
    size_t body_size;
    const char *content_type;
    size_t content_type_size;
    char send_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    size_t send_id_size;
    uint64_t delay_ms;
    uint64_t due_ms;
    uint64_t commit_sequence;
    chttp_request request;
    bool cancel_requested;
} scxml_chttp_egress_row;

typedef struct scxml_chttp_cancel_ticket {
    scxml_chttp_processor_impl *processor;
    size_t target_slot;
    uint32_t target_generation;
    bool reserved;
} scxml_chttp_cancel_ticket;

struct scxml_chttp_processor_impl {
    turbo_mutex_t mutex;
    turbo_cond_t condition;
    turbo_thread_t worker;
    scxml_chttp_processor_state state;
    chttp_server server;
    chttp_async_client client;
    bool worker_started;
    bool stop_requested;
    bool client_destroyed;
    bool server_stopped;
    int worker_status;
    uint32_t stop_timeout_ms;
    uint32_t worker_poll_ms;
    uint32_t request_timeout_ms;
    scxml_chttp_resolve_fn resolve;
    void *resolve_user;
    char *advertised_authority;
    size_t advertised_authority_size;
    char *base_path;
    size_t base_path_size;
    char *route_path;
    char *ingress_storage;
    size_t ingress_storage_capacity;
    scxml_chttp_form_entry_view *ingress_entries;
    size_t endpoint_capacity;
    size_t egress_capacity;
    size_t max_access_uri_bytes;
    size_t max_event_name_bytes;
    size_t max_form_entry_count;
    size_t max_form_name_bytes;
    size_t max_form_value_bytes;
    size_t max_encoded_body_bytes;
    size_t access_uri_stride;
    size_t egress_string_stride;
    scxml_chttp_binding_impl *bindings;
    scxml_chttp_egress_row *egress;
    scxml_chttp_cancel_ticket *cancel_tickets;
    void *storage;
    size_t live_bindings;
    scxml_chttp_processor_stats stats;
};

typedef struct scxml_chttp_decoded_form {
    const char *event_name;
    size_t event_name_size;
    const scxml_chttp_form_entry_view *entries;
    size_t entry_count;
    size_t storage_size;
} scxml_chttp_decoded_form;

scxml_adapter_status scxml_chttp_encode_send_body(
    const scxml_send_request *request,
    void *out_body, size_t body_capacity, size_t *out_body_size,
    const char **out_content_type, size_t *out_content_type_size);

scxml_chttp_decode_status scxml_chttp_decode_form_body(
    const void *body, size_t body_size,
    char *storage, size_t storage_capacity,
    scxml_chttp_form_entry_view *entries, size_t entry_capacity,
    size_t max_event_name_bytes, size_t max_form_name_bytes,
    size_t max_form_value_bytes, scxml_chttp_decoded_form *out);

scxml_adapter_status scxml_chttp_egress_prepare_send(
    scxml_chttp_binding_impl *binding, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error);
scxml_adapter_status scxml_chttp_egress_prepare_cancel(
    scxml_chttp_binding_impl *binding, const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error,
    bool *out_handled);
void scxml_chttp_egress_close_binding_locked(
    scxml_chttp_binding_impl *binding);
void scxml_chttp_egress_worker(void *user);
int scxml_chttp_ingress_register(scxml_chttp_processor_impl *processor);

#endif
