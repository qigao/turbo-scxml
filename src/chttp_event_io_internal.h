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

typedef struct scxml_chttp_endpoint_row {
    scxml_chttp_binding_impl *binding;
    uint32_t generation;
    char endpoint[TURBO_UUID_STRING_SIZE];
    char *access_uri;
    size_t access_uri_size;
} scxml_chttp_endpoint_row;

typedef struct scxml_chttp_egress_row {
    scxml_chttp_binding_impl *binding;
    uint32_t generation;
    uint32_t state;
    char *connection_uri;
    char *authority;
    char *target;
    char *send_id;
    char *body;
} scxml_chttp_egress_row;

typedef struct scxml_chttp_processor_impl {
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
    size_t live_bindings;
    uint64_t egress_completed;
    uint64_t egress_failed;
    uint64_t ingress_admitted;
    uint64_t ingress_rejected;
    uint64_t invariant_failures;
} scxml_chttp_processor_impl;

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

#endif /* SCXML_CHTTP_EVENT_IO_INTERNAL_H */
