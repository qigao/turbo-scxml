#ifndef TURBO_VOICEXML_CMETA_H
#define TURBO_VOICEXML_CMETA_H

#include <voicexml/voicexml.h>

#include <cmeta/data.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VXML_CMETA_COMPILE_OPTIONS_ABI_V1 1u
#define VXML_CMETA_SESSION_OPTIONS_ABI_V1 1u
#define VXML_CMETA_DATA_RESOURCE_ADAPTER_ABI_V1 1u
#define VXML_CMETA_COLLECT_ADAPTER_ABI_V1 1u
#define VXML_CMETA_COLLECT_REQUEST_ABI_V1 1u
#define VXML_CMETA_COLLECT_COMPLETION_ABI_V1 1u
#define VXML_CMETA_COLLECT_COMPLETION_ABI_V2 2u
#define VXML_CMETA_PROMPT_MEDIA_ADAPTER_ABI_V1 1u
#define VXML_CMETA_PROMPT_MEDIA_REQUEST_ABI_V1 1u
#define VXML_CMETA_PROMPT_MEDIA_BATCH_REQUEST_ABI_V1 1u

#define VXML_CMETA_COLLECT_CAP_SRGS_XML UINT64_C(1)
#define VXML_CMETA_PROMPT_MEDIA_CAP_TEXT UINT64_C(1)
#define VXML_CMETA_PROMPT_MEDIA_CAP_SSML UINT64_C(2)
#define VXML_CMETA_PROMPT_MEDIA_CAP_AUDIO UINT64_C(4)
#define VXML_CMETA_PROMPT_MEDIA_CAP_BATCH UINT64_C(8)

typedef struct vxml_cmeta_name_view {
    const char *data;
    size_t size;
} vxml_cmeta_name_view;

typedef enum vxml_cmeta_value_kind {
    VXML_CMETA_VALUE_UNDEFINED = 0,
    VXML_CMETA_VALUE_BOOL,
    VXML_CMETA_VALUE_SINT,
    VXML_CMETA_VALUE_UINT,
    VXML_CMETA_VALUE_FLOAT,
    VXML_CMETA_VALUE_STRING
} vxml_cmeta_value_kind;

typedef struct vxml_cmeta_value_view {
    vxml_cmeta_value_kind kind;
    union {
        bool boolean;
        int64_t sint;
        uint64_t uint_value;
        double number;
        struct { const char *data; size_t size; } string;
    } data;
} vxml_cmeta_value_view;

typedef struct vxml_cmeta_compile_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const cmeta_data_desc *root;
    const cmeta_data_desc *const *semantic_data;
    size_t semantic_data_count;
    size_t max_expression_bytes;
    size_t max_expression_instructions;
    size_t max_expression_operands;
    size_t max_expression_depth;
    size_t max_path_depth;
    size_t max_literal_bytes;
    size_t max_string_bytes;
    size_t max_scope_slots;
    size_t max_scope_storage_bytes;
    size_t max_conditional_depth;

    /* Optional append-only external-data admission tail. Zero disables <data>. */
    size_t max_external_data_resources;
    size_t max_data_uri_bytes;
    size_t max_data_bind_depth;
    size_t max_data_bind_items;

    /* Optional append-only directed-field admission tail. */
    size_t max_fields;
    size_t max_grammar_bytes;

    /* Optional append-only scoped-Event admission tail. */
    size_t max_event_handlers;
    size_t max_event_name_bytes;

    /* Optional append-only literal prompt admission tail. */
    size_t max_prompts;
    size_t max_prompt_bytes;

    /* Optional append-only mixed prompt batch bound. Zero keeps V1-only. */
    size_t max_prompt_segments;
} vxml_cmeta_compile_options_v1;

typedef struct vxml_cmeta_session_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const void *initial_root;
    const vxml_cmeta_name_view *initially_undefined;
    size_t initially_undefined_count;
    size_t max_transaction_bytes;
    size_t max_execution_steps;

    /* Optional append-only runtime data-resource tail. */
    const struct vxml_cmeta_data_resource_adapter_v1 *data_resources;
    void *data_resource_user;
    size_t max_data_bytes;
    size_t max_data_owned_bytes;

    /* Optional append-only directed collect provider tail. */
    const struct vxml_cmeta_collect_adapter_v1 *collect;
    void *collect_user;

    /* Optional append-only V2 multi-slot completion capacity; zero keeps V1. */
    size_t max_collect_result_slots;

    /* Optional append-only scoped Event runtime bounds. */
    size_t max_event_counters;
    size_t max_event_name_bytes;
    size_t max_event_dispatch_depth;

    /* Optional append-only prompt-media provider tail. */
    const struct vxml_cmeta_prompt_media_adapter_v1 *prompt_media;
    void *prompt_media_user;
} vxml_cmeta_session_options_v1;

typedef enum vxml_cmeta_data_format {
    VXML_CMETA_DATA_JSON = 1,
    VXML_CMETA_DATA_YAML,
    VXML_CMETA_DATA_CSV,
    VXML_CMETA_DATA_XML
} vxml_cmeta_data_format;

typedef struct vxml_cmeta_data_resource_v1 {
    const void *data;
    size_t size;
    vxml_cmeta_data_format format;
    void *lease;
} vxml_cmeta_data_resource_v1;

typedef struct vxml_cmeta_data_resource_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    vxml_status (*open)(
        void *user,
        const char *uri, size_t uri_size,
        size_t max_bytes,
        vxml_cmeta_data_resource_v1 *out);
    void (*close)(
        void *user,
        vxml_cmeta_data_resource_v1 *resource);
} vxml_cmeta_data_resource_adapter_v1;

typedef struct vxml_cmeta_collect_ticket_v1 {
    void (*commit)(void *user);
    void (*discard)(void *user);
    void *user;
} vxml_cmeta_collect_ticket_v1;

typedef struct vxml_cmeta_collect_request_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t generation;
    uint64_t required_capabilities;
    vxml_cmeta_name_view field;
    vxml_cmeta_name_view grammar_type;
    vxml_cmeta_name_view grammar_src;
} vxml_cmeta_collect_request_v1;

typedef struct vxml_cmeta_collect_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t capabilities;
    vxml_status (*prepare)(
        void *user,
        const vxml_cmeta_collect_request_v1 *request,
        vxml_cmeta_collect_ticket_v1 *out_ticket,
        const char **out_error);
    /**
     * No-fail/nonblocking cancellation of one previously committed generation.
     * The provider must ignore an already-settled generation.
     */
    void (*cancel)(void *user, uint64_t generation);
} vxml_cmeta_collect_adapter_v1;

typedef enum vxml_cmeta_collect_ingress_result {
    VXML_CMETA_COLLECT_INGRESS_ACCEPTED = 0,
    VXML_CMETA_COLLECT_INGRESS_FULL,
    VXML_CMETA_COLLECT_INGRESS_CLOSED,
    VXML_CMETA_COLLECT_INGRESS_STALE,
    VXML_CMETA_COLLECT_INGRESS_INVALID_ARGUMENT,
    VXML_CMETA_COLLECT_INGRESS_INCOMPATIBLE_RESULT
} vxml_cmeta_collect_ingress_result;

typedef struct vxml_cmeta_collect_completion_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t generation;
    const cmeta_data_desc *data;
    const void *value;
} vxml_cmeta_collect_completion_v1;

typedef struct vxml_cmeta_collect_result_slot_v1 {
    vxml_cmeta_name_view name;
    const cmeta_data_desc *data;
    const void *value;
} vxml_cmeta_collect_result_slot_v1;

typedef struct vxml_cmeta_collect_completion_v2 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t generation;
    const vxml_cmeta_collect_result_slot_v1 *slots;
    size_t slot_count;
} vxml_cmeta_collect_completion_v2;

typedef enum vxml_cmeta_prompt_media_segment_kind {
    VXML_CMETA_PROMPT_MEDIA_TEXT = 1,
    VXML_CMETA_PROMPT_MEDIA_SSML,
    VXML_CMETA_PROMPT_MEDIA_AUDIO
} vxml_cmeta_prompt_media_segment_kind;

typedef struct vxml_cmeta_prompt_media_segment_v1 {
    vxml_cmeta_prompt_media_segment_kind kind;
    vxml_cmeta_name_view payload;
    vxml_cmeta_name_view media_type;
} vxml_cmeta_prompt_media_segment_v1;

typedef struct vxml_cmeta_prompt_media_ticket_v1 {
    void (*commit)(void *user);
    void (*discard)(void *user);
    void *user;
} vxml_cmeta_prompt_media_ticket_v1;

typedef struct vxml_cmeta_prompt_media_request_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t generation;
    uint64_t required_capabilities;
    vxml_cmeta_name_view field;
    unsigned prompt_count;
    unsigned selected_count;
    size_t segment_count;
    vxml_cmeta_prompt_media_segment_v1 segment;
} vxml_cmeta_prompt_media_request_v1;

typedef struct vxml_cmeta_prompt_media_batch_request_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t generation;
    uint64_t required_capabilities;
    vxml_cmeta_name_view field;
    unsigned prompt_count;
    unsigned selected_count;
    const vxml_cmeta_prompt_media_segment_v1 *segments;
    size_t segment_count;
} vxml_cmeta_prompt_media_batch_request_v1;

typedef struct vxml_cmeta_prompt_media_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t capabilities;
    vxml_status (*prepare)(
        void *user,
        const vxml_cmeta_prompt_media_request_v1 *request,
        vxml_cmeta_prompt_media_ticket_v1 *out_ticket,
        const char **out_error);
    /** No-fail/nonblocking cancellation of one committed generation. */
    void (*cancel)(void *user, uint64_t generation);

    /**
     * Optional append-only atomic batch reservation.
     *
     * The segments array and all views are borrowed for this callback only.
     * Success returns one commit/discard ticket owning the whole batch.
     */
    vxml_status (*prepare_batch)(
        void *user,
        const vxml_cmeta_prompt_media_batch_request_v1 *request,
        vxml_cmeta_prompt_media_ticket_v1 *out_ticket,
        const char **out_error);
} vxml_cmeta_prompt_media_adapter_v1;

typedef enum vxml_cmeta_prompt_media_outcome {
    VXML_CMETA_PROMPT_MEDIA_OUTCOME_COMPLETED = 1,
    VXML_CMETA_PROMPT_MEDIA_OUTCOME_FAILED
} vxml_cmeta_prompt_media_outcome;

typedef enum vxml_cmeta_prompt_media_ingress_result {
    VXML_CMETA_PROMPT_MEDIA_INGRESS_ACCEPTED = 0,
    VXML_CMETA_PROMPT_MEDIA_INGRESS_FULL,
    VXML_CMETA_PROMPT_MEDIA_INGRESS_CLOSED,
    VXML_CMETA_PROMPT_MEDIA_INGRESS_STALE,
    VXML_CMETA_PROMPT_MEDIA_INGRESS_INVALID_ARGUMENT
} vxml_cmeta_prompt_media_ingress_result;

typedef struct vxml_cmeta_prompt_media_completion_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t generation;
    vxml_cmeta_prompt_media_outcome outcome;
} vxml_cmeta_prompt_media_completion_v1;

#define VXML_CMETA_PROMPT_MEDIA_COMPLETION_ABI_V1 1u

typedef struct vxml_cmeta_prompt_view_v1 {
    uint32_t abi_version;
    size_t struct_size;
    vxml_cmeta_name_view field;
    vxml_cmeta_name_view text;
    unsigned count;
    unsigned prompt_count;
    uint64_t generation;
} vxml_cmeta_prompt_view_v1;

#define VXML_CMETA_PROMPT_VIEW_ABI_V1 1u

typedef enum vxml_cmeta_exit_kind {
    VXML_CMETA_EXIT_EMPTY = 0,
    VXML_CMETA_EXIT_EXPRESSION,
    VXML_CMETA_EXIT_NAMELIST
} vxml_cmeta_exit_kind;

vxml_status vxml_compile_cmeta(
    const void *bytes, size_t size,
    const vxml_limits *limits,
    const vxml_cmeta_compile_options_v1 *options,
    vxml_program *out, vxml_diagnostic *diagnostic);

vxml_status vxml_session_init_cmeta(
    vxml_session *session, const vxml_program *program,
    const vxml_cmeta_session_options_v1 *options);

vxml_status vxml_session_cmeta_read(
    const vxml_session *session, const char *name, size_t name_size,
    vxml_cmeta_value_view *out_value);

/** Borrow the currently selected directed-field collect request. */
vxml_status vxml_session_cmeta_collect_request(
    const vxml_session *session,
    vxml_cmeta_collect_request_v1 *out_request);

/**
 * Ask the configured provider to reserve the selected collect operation.
 * Success stores the provider ticket inside the session; no provider work is
 * committed until vxml_session_cmeta_collect_commit().
 */
vxml_status vxml_session_cmeta_collect_prepare(
    vxml_session *session, const char **out_error);

/** Commit the currently prepared provider ticket; no-fail provider callback. */
vxml_status vxml_session_cmeta_collect_commit(vxml_session *session);

/** Discard the currently prepared provider ticket and restore admission. */
vxml_status vxml_session_cmeta_collect_discard(vxml_session *session);

/**
 * MPSC admission of one copied fixed-scalar completion.
 *
 * The session must outlive this call. Producers must stop before session
 * destroy. The selected field descriptor must exactly match completion.data.
 * BOOL/SINT/UINT/FLOAT native storage is copied into one fixed mailbox slot.
 */
vxml_cmeta_collect_ingress_result vxml_session_cmeta_collect_try_complete(
    vxml_session *session,
    const vxml_cmeta_collect_completion_v1 *completion);

/**
 * MPSC admission of one bounded multi-slot fixed-scalar completion.
 *
 * Slot names are borrowed only for this call and are resolved immediately to
 * immutable application-root field indices. No borrowed name/value survives
 * successful admission.
 */
vxml_cmeta_collect_ingress_result vxml_session_cmeta_collect_try_complete_v2(
    vxml_session *session,
    const vxml_cmeta_collect_completion_v2 *completion);

/**
 * Single-owner progress point. Applies at most one accepted completion through
 * the CMeta transaction boundary and then returns to Directed FIA SELECT.
 *
 * No ready completion is not an error and reports *out_progressed == false.
 */
vxml_status vxml_session_cmeta_collect_run_ready(
    vxml_session *session,
    bool *out_progressed);

/**
 * Synchronously inject one byte-counted VoiceXML Event into the active CMeta
 * session. The complete catch/throw chain is one bounded transaction.
 *
 * Event bytes are borrowed only for this call. Uncaught or cyclic rethrow
 * fails the Session without partially committing handler effects.
 */
vxml_status vxml_session_cmeta_raise(
    vxml_session *session,
    const char *event_name,
    size_t event_name_size);

/** Raise the standard field recovery Events through the scoped selector. */
vxml_status vxml_session_cmeta_noinput(vxml_session *session);
vxml_status vxml_session_cmeta_nomatch(vxml_session *session);

/**
 * Consume the one-shot reprompt control bit published by the last successful
 * Event handler. A successful call clears the bit.
 */
vxml_status vxml_session_cmeta_take_reprompt(
    vxml_session *session, bool *out_requested);

/**
 * Borrow the tapered prompt selected for the current directed field/retry
 * state. Text bytes are immutable Program-owned storage.
 */
vxml_status vxml_session_cmeta_prompt(
    const vxml_session *session,
    vxml_cmeta_prompt_view_v1 *out_prompt);

/**
 * Build the current tapered prompt-media request.
 *
 * V1 returns either zero segments (no eligible prompt) or one literal TEXT
 * segment. All views borrow immutable Program storage.
 */
vxml_status vxml_session_cmeta_prompt_media_request(
    const vxml_session *session,
    vxml_cmeta_prompt_media_request_v1 *out_request);

/** Borrow the selected prompt as one ordered immutable segment batch. */
vxml_status vxml_session_cmeta_prompt_media_batch_request(
    const vxml_session *session,
    vxml_cmeta_prompt_media_batch_request_v1 *out_request);

/** Reserve the current prompt with the configured media provider. */
vxml_status vxml_session_cmeta_prompt_media_prepare(
    vxml_session *session, const char **out_error);

/** Commit the prepared media ticket; provider callback is no-fail. */
vxml_status vxml_session_cmeta_prompt_media_commit(vxml_session *session);

/** Discard the prepared media ticket and restore admission. */
vxml_status vxml_session_cmeta_prompt_media_discard(vxml_session *session);

/**
 * MPSC admission of one generation-scoped prompt playback completion.
 *
 * The record contains no borrowed provider data. The Session must outlive
 * concurrent producers; stop producers before close/destroy.
 */
vxml_cmeta_prompt_media_ingress_result
vxml_session_cmeta_prompt_media_try_complete(
    vxml_session *session,
    const vxml_cmeta_prompt_media_completion_v1 *completion);

/**
 * Single-owner progress point. Settles at most one ready playback completion.
 * A completed provider generation is not canceled again.
 */
vxml_status vxml_session_cmeta_prompt_media_run_ready(
    vxml_session *session,
    bool *out_progressed,
    vxml_cmeta_prompt_media_outcome *out_outcome);

vxml_status vxml_session_cmeta_exit_kind(
    const vxml_session *session, vxml_cmeta_exit_kind *out_kind);

size_t vxml_session_cmeta_exit_count(const vxml_session *session);

vxml_status vxml_session_cmeta_exit_at(
    const vxml_session *session, size_t index,
    vxml_cmeta_name_view *out_name,
    vxml_cmeta_value_view *out_value);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_VOICEXML_CMETA_H */
