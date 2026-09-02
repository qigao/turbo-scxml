#ifndef SCXML_CHTTP_EVENT_IO_INTERNAL_H
#define SCXML_CHTTP_EVENT_IO_INTERNAL_H

#include <scxml/chttp_event_io.h>

#include <turbo/thread.h>
#include <turbo_uuid.h>

typedef enum scxml_chttp_processor_state {
    SCXML_CHTTP_PROCESSOR_INITIALIZED = 1,
    SCXML_CHTTP_PROCESSOR_RUNNING,
    SCXML_CHTTP_PROCESSOR_STOPPING,
    SCXML_CHTTP_PROCESSOR_STOPPED
} scxml_chttp_processor_state;

typedef enum scxml_chttp_binding_state {
    SCXML_CHTTP_BINDING_RESERVED = 1,
    SCXML_CHTTP_BINDING_ACTIVE,
    SCXML_CHTTP_BINDING_CLOSING,
    SCXML_CHTTP_BINDING_QUIESCENT
} scxml_chttp_binding_state;

typedef struct scxml_chttp_binding_impl scxml_chttp_binding_impl;
typedef struct scxml_chttp_processor_impl scxml_chttp_processor_impl;

typedef enum scxml_chttp_egress_state {
    SCXML_CHTTP_EGRESS_FREE = 0,
    SCXML_CHTTP_EGRESS_RESERVED,
    SCXML_CHTTP_EGRESS_READY,
    SCXML_CHTTP_EGRESS_SUBMITTING,
    SCXML_CHTTP_EGRESS_SUBMITTED,
    SCXML_CHTTP_EGRESS_COMPLETING
} scxml_chttp_egress_state;

typedef struct scxml_chttp_endpoint_row {
    scxml_chttp_binding_impl *binding;
    uint32_t generation;
    char endpoint[TURBO_UUID_STRING_SIZE];
    char *access_uri;
    size_t access_uri_size;
} scxml_chttp_endpoint_row;

typedef struct scxml_chttp_egress_row {
    scxml_chttp_processor_impl *processor;
    scxml_chttp_binding_impl *binding;
    uint32_t generation;
    scxml_chttp_egress_state state;
    char *connection_uri;
    char *authority;
    char *target;
    char *send_id;
    char *body;
    size_t body_size;
    const char *media_type;
    size_t media_type_size;
    size_t send_id_size;
    uint64_t delay_ms;
    uint64_t due_ms;
    uint64_t commit_sequence;
    chttp_request request;
    bool cancel_requested;
} scxml_chttp_egress_row;

typedef struct scxml_chttp_cancel_ticket {
    scxml_chttp_processor_impl *processor;
    size_t target_index;
    uint32_t target_generation;
    bool reserved;
} scxml_chttp_cancel_ticket;

struct scxml_chttp_processor_impl {
    scxml_chttp_processor_config_v1 config;
    chttp_server server;
    chttp_async_client client;
    turbo_mutex_t lock;
    turbo_cond_t wake;
    turbo_thread_t worker;
    scxml_chttp_processor_state state;
    bool stop_active;
    bool stop_requested;
    bool stop_attempt_complete;
    bool worker_started;
    bool worker_exited;
    bool server_started;
    bool server_stopped;
    bool client_stopped;
    bool client_destroyed;
    uint64_t stop_deadline_ms;
    uint64_t stop_generation;
    int worker_stop_status;
    int shutdown_terminal_status;
    uint16_t bound_port;
    void *storage;
    char *advertised_authority;
    size_t advertised_authority_size;
    char *base_path;
    size_t base_path_size;
    scxml_chttp_endpoint_row *endpoints;
    scxml_chttp_egress_row *egress;
    scxml_chttp_cancel_ticket *cancel_tickets;
    size_t live_bindings;
    size_t queued_egress;
    size_t in_flight_egress;
    uint64_t egress_accepted;
    uint64_t egress_completed;
    uint64_t egress_failed;
    uint64_t egress_cancelled;
    uint64_t ingress_requests;
    uint64_t ingress_admitted;
    uint64_t ingress_rejected;
    uint64_t invariant_failures;
};

struct scxml_chttp_binding_impl {
    scxml_chttp_processor_impl *processor;
    size_t endpoint_index;
    uint32_t endpoint_generation;
    scxml_chttp_binding_state state;
    scxml_event_io_adapter composite;
    scxml_event_io_adapter downstream;
    void *downstream_user;
    scxml_chttp_decode_fn decode;
    void *decode_user;
    scxml_session *session;
    const scxml_program *program;
    size_t active_callbacks;
    size_t outbound_references;
    uint64_t next_commit_sequence;
    bool downstream_close_called;
};

typedef struct scxml_chttp_codec_limits {
    size_t max_event_name_bytes;
    size_t max_form_entry_count;
    size_t max_form_name_bytes;
    size_t max_form_value_bytes;
    size_t max_encoded_body_bytes;
} scxml_chttp_codec_limits;

typedef struct scxml_chttp_encoded_body {
    const char *media_type;
    size_t media_type_size;
    size_t body_size;
} scxml_chttp_encoded_body;

typedef struct scxml_chttp_decoded_form {
    const char *event;
    size_t event_size;
    scxml_chttp_form_entry_view *entries;
    size_t entry_count;
} scxml_chttp_decoded_form;

void scxml_chttp_test_fail_next_worker_create(void);
void scxml_chttp_test_delay_next_worker_exit(uint32_t delay_ms);
void scxml_chttp_test_force_next_server_terminal_error(void);
void scxml_chttp_test_fail_next_cancel_admission(void);
void scxml_chttp_test_delay_next_completion_release(uint32_t delay_ms);

scxml_adapter_status scxml_chttp_codec_encode(
    const scxml_send_request *request,
    const scxml_chttp_codec_limits *limits,
    char *body, size_t body_capacity,
    size_t *out_required_body_size,
    scxml_chttp_encoded_body *out);

scxml_chttp_decode_status scxml_chttp_codec_decode_form(
    const void *body, size_t body_size,
    const scxml_chttp_codec_limits *limits,
    scxml_chttp_form_entry_view *entry_storage,
    size_t entry_capacity,
    char *text_storage, size_t text_capacity,
    size_t *out_required_text_size,
    scxml_chttp_decoded_form *out);

scxml_adapter_status scxml_chttp_egress_prepare_send(
    scxml_chttp_binding_impl *binding,
    const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error);
scxml_adapter_status scxml_chttp_egress_prepare_cancel(
    scxml_chttp_binding_impl *binding,
    const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error,
    bool *out_handled);
void scxml_chttp_egress_close_binding_locked(
    scxml_chttp_binding_impl *binding);
bool scxml_chttp_egress_cancel_one(scxml_chttp_processor_impl *processor);
bool scxml_chttp_egress_submit_one(scxml_chttp_processor_impl *processor);

#endif /* SCXML_CHTTP_EVENT_IO_INTERNAL_H */
