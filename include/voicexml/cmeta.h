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
#define VXML_CMETA_MENU_COLLECT_REQUEST_ABI_V1 1u
#define VXML_CMETA_MENU_COLLECT_REQUEST_ABI_V2 2u
#define VXML_CMETA_INITIAL_COLLECT_REQUEST_ABI_V1 1u
#define VXML_CMETA_COLLECT_COMPLETION_ABI_V1 1u
#define VXML_CMETA_COLLECT_COMPLETION_ABI_V2 2u
#define VXML_CMETA_MENU_COMPLETION_ABI_V1 1u
#define VXML_CMETA_PROMPT_MEDIA_ADAPTER_ABI_V1 1u
#define VXML_CMETA_PROMPT_MEDIA_REQUEST_ABI_V1 1u
#define VXML_CMETA_PROMPT_MEDIA_BATCH_REQUEST_ABI_V1 1u
#define VXML_CMETA_SUBDIALOG_ADAPTER_ABI_V1 1u
#define VXML_CMETA_SUBDIALOG_REQUEST_ABI_V1 1u
#define VXML_CMETA_SUBDIALOG_COMPLETION_ABI_V1 1u
#define VXML_CMETA_RECORD_COMPLETION_ABI_V1 1u
#define VXML_CMETA_RECORD_RESULT_VIEW_ABI_V1 1u

#define VXML_CMETA_COLLECT_CAP_SRGS_XML UINT64_C(1)
#define VXML_CMETA_COLLECT_CAP_MENU_CHOICE UINT64_C(2)
#define VXML_CMETA_COLLECT_CAP_MENU_SPEECH_EXACT UINT64_C(4)
#define VXML_CMETA_COLLECT_CAP_MENU_SPEECH_APPROXIMATE UINT64_C(8)
#define VXML_CMETA_COLLECT_CAP_MENU_GRAMMAR_EXTERNAL UINT64_C(16)
#define VXML_CMETA_COLLECT_CAP_INITIAL_MULTI UINT64_C(32)
#define VXML_CMETA_PROMPT_MEDIA_CAP_TEXT UINT64_C(1)
#define VXML_CMETA_PROMPT_MEDIA_CAP_SSML UINT64_C(2)
#define VXML_CMETA_PROMPT_MEDIA_CAP_AUDIO UINT64_C(4)
#define VXML_CMETA_PROMPT_MEDIA_CAP_BATCH UINT64_C(8)
#define VXML_CMETA_PROMPT_MEDIA_CAP_MARK UINT64_C(16)
#define VXML_CMETA_PROMPT_MEDIA_CAP_AUDIO_FALLBACK UINT64_C(32)

#define VXML_CMETA_RECORD_CAP_BEEP UINT64_C(1)
#define VXML_CMETA_RECORD_CAP_DTMF_TERM UINT64_C(2)
#define VXML_CMETA_RECORD_CAP_FINAL_SILENCE UINT64_C(4)
#define VXML_CMETA_RECORD_CAP_EXPLICIT_TYPE UINT64_C(8)

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

    /* Optional append-only static menu admission tail. Zero disables <menu>. */
    size_t max_menus;
    size_t max_menu_choices;
    size_t max_menu_choice_bytes;

    /*
     * Optional append-only literal menu navigation target bound.
     * Required only when a static choice uses @next; event-only menu callers
     * using the historical menu prefix remain valid.
     */
    size_t max_menu_target_bytes;

    /* Optional append-only explicit menu grammar bound. Zero disables V2 grammar refs. */
    size_t max_menu_grammar_bytes;

    /* Optional append-only mixed-initiative control bound. Zero disables <initial>. */
    size_t max_initials;

    /* Optional append-only static subdialog bounds. Zero disables <subdialog>. */
    size_t max_subdialogs;
    size_t max_subdialog_uri_bytes;

    /* Optional append-only subdialog parameter compile bounds. */
    size_t max_subdialog_params;
    size_t max_subdialog_param_name_bytes;
    size_t max_subdialog_param_value_bytes;

    /*
     * Optional append-only static record bounds. Zero max_records disables
     * <record>. Platform-specific maxtime/finalsilence defaults remain
     * provider policy but may never exceed these hard owner ceilings.
     */
    size_t max_records;
    size_t max_record_media_type_bytes;
    uint64_t max_record_duration_us;
    uint64_t max_record_final_silence_us;
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

    /* Optional append-only subdialog child-owner admission tail. */
    const struct vxml_cmeta_subdialog_adapter_v1 *subdialog;
    void *subdialog_user;
    size_t max_subdialog_snapshot_bytes;

    /* Optional append-only child completion mailbox bounds. */
    size_t max_subdialog_completion_entries;
    size_t max_subdialog_completion_bytes;

    /* Optional append-only recording provider admission tail. */
    const struct vxml_cmeta_record_adapter_v1 *record;
    void *record_user;
    size_t max_record_bytes;
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


typedef struct vxml_cmeta_record_ticket_v1 {
    void (*commit)(void *user);
    void (*discard)(void *user);
    void *user;
} vxml_cmeta_record_ticket_v1;

#define VXML_CMETA_RECORD_REQUEST_ABI_V1 1u

typedef struct vxml_cmeta_record_request_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t generation;
    uint64_t required_capabilities;

    vxml_cmeta_name_view name;

    bool modal;
    bool beep;
    bool dtmf_term;

    bool has_maxtime;
    uint64_t maxtime_us;
    uint64_t max_duration_us;

    bool has_final_silence;
    uint64_t final_silence_us;
    uint64_t max_final_silence_us;

    vxml_cmeta_name_view media_type;
    size_t max_bytes;
} vxml_cmeta_record_request_v1;

#define VXML_CMETA_RECORD_ADAPTER_ABI_V1 1u

typedef struct vxml_cmeta_record_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t capabilities;
    vxml_status (*prepare)(
        void *user,
        const vxml_cmeta_record_request_v1 *request,
        vxml_cmeta_record_ticket_v1 *out_ticket,
        const char **out_error);
    /** No-fail/nonblocking cancellation of one committed generation. */
    void (*cancel)(void *user, uint64_t generation);

    /*
     * Owner teardown barrier for one committed generation. This may block and
     * returns only after no provider callback for the generation can still
     * enter TurboSCXML.
     */
    void (*quiesce)(void *user, uint64_t generation);
} vxml_cmeta_record_adapter_v1;

typedef enum vxml_cmeta_record_outcome {
    VXML_CMETA_RECORD_OUTCOME_SUCCESS = 1,
    VXML_CMETA_RECORD_OUTCOME_NOINPUT,
    VXML_CMETA_RECORD_OUTCOME_TERMCHAR,
    VXML_CMETA_RECORD_OUTCOME_ERROR
} vxml_cmeta_record_outcome;

/*
 * Move-only provider recording lease. Before ACCEPTED it belongs to the
 * producer. ACCEPTED SUCCESS transfers it to the Session; every other ingress
 * result leaves ownership with the producer.
 */
typedef struct vxml_cmeta_recording_lease_v1 {
    const void *data;
    size_t size;
    void *lease;
    void (*release)(void *user, void *lease);
    void *release_user;
} vxml_cmeta_recording_lease_v1;

typedef struct vxml_cmeta_record_completion_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t generation;
    vxml_cmeta_record_outcome outcome;
    uint64_t duration_us;
    bool has_termchar;
    char termchar;
    vxml_cmeta_name_view media_type;
    vxml_cmeta_recording_lease_v1 recording;
} vxml_cmeta_record_completion_v1;

typedef enum vxml_cmeta_record_ingress_result {
    VXML_CMETA_RECORD_INGRESS_ACCEPTED = 0,
    VXML_CMETA_RECORD_INGRESS_FULL,
    VXML_CMETA_RECORD_INGRESS_CLOSED,
    VXML_CMETA_RECORD_INGRESS_STALE,
    VXML_CMETA_RECORD_INGRESS_INVALID_ARGUMENT,
    VXML_CMETA_RECORD_INGRESS_INCOMPATIBLE_RESULT
} vxml_cmeta_record_ingress_result;

/*
 * Borrowed view of the Session-owned result identified by record@name in the
 * active form. data and media_type remain valid until that result is replaced,
 * cleared, or the Session is closed/destroyed.
 */
typedef struct vxml_cmeta_record_result_view_v1 {
    uint32_t abi_version;
    size_t struct_size;
    vxml_cmeta_name_view name;
    vxml_cmeta_record_outcome outcome;
    uint64_t duration_us;
    bool has_termchar;
    char termchar;
    vxml_cmeta_name_view media_type;
    const void *data;
    size_t size;
} vxml_cmeta_record_result_view_v1;


typedef enum vxml_cmeta_subdialog_param_source {
    VXML_CMETA_SUBDIALOG_PARAM_TYPED = 1,
    VXML_CMETA_SUBDIALOG_PARAM_LITERAL
} vxml_cmeta_subdialog_param_source;

/*
 * Fixed-stride public parameter element. Do not tail-extend.
 * TYPED uses value; LITERAL uses literal. STRING value bytes and literal bytes
 * borrow the parent Session-owned snapshot and never the expression scratch.
 */
typedef struct vxml_cmeta_subdialog_param_v1 {
    vxml_cmeta_name_view name;
    vxml_cmeta_subdialog_param_source source;
    vxml_cmeta_value_view value;
    vxml_cmeta_name_view literal;
} vxml_cmeta_subdialog_param_v1;

typedef struct vxml_cmeta_subdialog_request_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t generation;
    vxml_cmeta_name_view src;
    const vxml_cmeta_subdialog_param_v1 *params;
    size_t param_count;
} vxml_cmeta_subdialog_request_v1;

#define VXML_CMETA_CHILD_ENTRY_ABI_V1 1u

/*
 * Public child-start request. form_id is optional: an empty view selects the
 * document entry form. Parameter views are borrowed only for the start call.
 */
typedef struct vxml_cmeta_child_entry_v1 {
    uint32_t abi_version;
    size_t struct_size;
    vxml_cmeta_name_view form_id;
    const vxml_cmeta_subdialog_param_v1 *params;
    size_t param_count;
} vxml_cmeta_child_entry_v1;

typedef struct vxml_cmeta_subdialog_ticket_v1 {
    void (*commit)(void *user);
    void (*discard)(void *user);
    void *user;
} vxml_cmeta_subdialog_ticket_v1;

typedef struct vxml_cmeta_subdialog_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    vxml_status (*prepare)(
        void *user,
        const vxml_cmeta_subdialog_request_v1 *request,
        vxml_cmeta_subdialog_ticket_v1 *out_ticket,
        const char **out_error);
    /* No-fail/nonblocking cancellation of one committed child generation. */
    void (*cancel)(void *user, uint64_t generation);
} vxml_cmeta_subdialog_adapter_v1;


typedef enum vxml_cmeta_subdialog_completion_kind {
    VXML_CMETA_SUBDIALOG_RETURN_DATA = 1,
    VXML_CMETA_SUBDIALOG_RETURN_EVENT,
    VXML_CMETA_SUBDIALOG_GLOBAL_EXIT
} vxml_cmeta_subdialog_completion_kind;

typedef enum vxml_cmeta_subdialog_global_exit_kind {
    VXML_CMETA_SUBDIALOG_GLOBAL_NATURAL = 0,
    VXML_CMETA_SUBDIALOG_GLOBAL_EXIT_EMPTY,
    VXML_CMETA_SUBDIALOG_GLOBAL_EXIT_EXPRESSION,
    VXML_CMETA_SUBDIALOG_GLOBAL_EXIT_NAMELIST,
    VXML_CMETA_SUBDIALOG_GLOBAL_DISCONNECT
} vxml_cmeta_subdialog_global_exit_kind;

/* Fixed-stride completion entry. Do not tail-extend. */
typedef struct vxml_cmeta_subdialog_result_entry_v1 {
    vxml_cmeta_name_view name;
    vxml_cmeta_value_view value;
} vxml_cmeta_subdialog_result_entry_v1;

typedef struct vxml_cmeta_subdialog_completion_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t generation;
    vxml_cmeta_subdialog_completion_kind kind;
    const vxml_cmeta_subdialog_result_entry_v1 *entries;
    size_t entry_count;
    vxml_cmeta_name_view event;
    vxml_cmeta_subdialog_global_exit_kind global_exit_kind;
} vxml_cmeta_subdialog_completion_v1;

typedef enum vxml_cmeta_subdialog_ingress_result {
    VXML_CMETA_SUBDIALOG_INGRESS_ACCEPTED = 0,
    VXML_CMETA_SUBDIALOG_INGRESS_FULL,
    VXML_CMETA_SUBDIALOG_INGRESS_CLOSED,
    VXML_CMETA_SUBDIALOG_INGRESS_STALE,
    VXML_CMETA_SUBDIALOG_INGRESS_INVALID_ARGUMENT,
    VXML_CMETA_SUBDIALOG_INGRESS_INCOMPATIBLE_RESULT
} vxml_cmeta_subdialog_ingress_result;

/*
 * Stable array element: do not append fields. Future menu metadata must use a
 * parallel side table or a new ABI, because providers traverse this array by
 * sizeof(vxml_cmeta_menu_choice_v1).
 */
typedef struct vxml_cmeta_menu_choice_v1 {
    vxml_cmeta_name_view dtmf;
    vxml_cmeta_name_view speech;
} vxml_cmeta_menu_choice_v1;

typedef struct vxml_cmeta_collect_request_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t generation;
    uint64_t required_capabilities;
    vxml_cmeta_name_view field;
    vxml_cmeta_name_view grammar_type;
    vxml_cmeta_name_view grammar_src;

    /*
     * Append-only selected-prompt input timing.
     *
     * has_timeout distinguishes an absent timeout from an explicit 0ms.
     * timeout_us is immutable compile-time policy; TurboSCXML does not create
     * a timer or consult a clock on this path.
     */
    bool has_timeout;
    uint64_t timeout_us;
} vxml_cmeta_collect_request_v1;

/**
 * Menu-specific collect request.
 *
 * This is intentionally separate from vxml_cmeta_collect_request_v1 because
 * that type is also a caller-owned output struct. Extending it would make an
 * old binary caller's allocation smaller than a new library write.
 */
typedef struct vxml_cmeta_menu_collect_request_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t generation;
    uint64_t required_capabilities;
    const vxml_cmeta_menu_choice_v1 *choices;
    size_t choice_count;
} vxml_cmeta_menu_collect_request_v1;

typedef enum vxml_cmeta_menu_accept_mode {
    VXML_CMETA_MENU_ACCEPT_EXACT = 1,
    VXML_CMETA_MENU_ACCEPT_APPROXIMATE
} vxml_cmeta_menu_accept_mode;

/*
 * Fixed-stride side-table element. Do not tail-extend.
 * choice_index is relative to the active menu's V1 choices array.
 */
typedef struct vxml_cmeta_menu_speech_policy_v1 {
    size_t choice_index;
    vxml_cmeta_menu_accept_mode mode;
} vxml_cmeta_menu_speech_policy_v1;

/*
 * Fixed-stride external grammar side-table element. Do not tail-extend.
 * media_type/src borrow immutable Program-owned storage.
 */
typedef struct vxml_cmeta_menu_grammar_ref_v1 {
    size_t choice_index;
    vxml_cmeta_name_view media_type;
    vxml_cmeta_name_view src;
} vxml_cmeta_menu_grammar_ref_v1;

/*
 * V2 is a separate caller-owned output object. V1 stays byte-for-byte stable.
 */
typedef struct vxml_cmeta_menu_collect_request_v2 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t generation;
    uint64_t required_capabilities;
    const vxml_cmeta_menu_choice_v1 *choices;
    size_t choice_count;
    const vxml_cmeta_menu_speech_policy_v1 *speech_policies;
    size_t speech_policy_count;
    const vxml_cmeta_menu_grammar_ref_v1 *grammars;
    size_t grammar_count;
} vxml_cmeta_menu_collect_request_v2;

/**
 * Mixed-initiative initial collect request.
 *
 * Separate caller-owned object: historical field/menu request sizes remain
 * byte-for-byte stable.
 */
typedef struct vxml_cmeta_initial_collect_request_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t generation;
    uint64_t required_capabilities;
    vxml_cmeta_name_view grammar_type;
    vxml_cmeta_name_view grammar_src;
} vxml_cmeta_initial_collect_request_v1;

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

    /*
     * Optional append-only menu admission tail. Historical adapters end after
     * cancel and remain valid for directed fields.
     */
    vxml_status (*prepare_menu)(
        void *user,
        const vxml_cmeta_menu_collect_request_v1 *request,
        vxml_cmeta_collect_ticket_v1 *out_ticket,
        const char **out_error);

    /*
     * Optional append-only V2 menu admission tail. Required only for
     * approximate speech policy or explicit external grammar rows.
     */
    vxml_status (*prepare_menu_v2)(
        void *user,
        const vxml_cmeta_menu_collect_request_v2 *request,
        vxml_cmeta_collect_ticket_v1 *out_ticket,
        const char **out_error);

    /*
     * Optional append-only mixed-initiative form-level grammar admission.
     * Historical field/menu adapter prefixes remain valid.
     */
    vxml_status (*prepare_initial)(
        void *user,
        const vxml_cmeta_initial_collect_request_v1 *request,
        vxml_cmeta_collect_ticket_v1 *out_ticket,
        const char **out_error);
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

typedef struct vxml_cmeta_menu_completion_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t generation;
    size_t choice_index;
} vxml_cmeta_menu_completion_v1;

typedef enum vxml_cmeta_prompt_media_segment_kind {
    VXML_CMETA_PROMPT_MEDIA_TEXT = 1,
    VXML_CMETA_PROMPT_MEDIA_SSML,
    VXML_CMETA_PROMPT_MEDIA_AUDIO,
    VXML_CMETA_PROMPT_MEDIA_MARK
} vxml_cmeta_prompt_media_segment_kind;

typedef enum vxml_cmeta_prompt_bargein_type {
    VXML_CMETA_PROMPT_BARGEIN_UNSPECIFIED = 0,
    VXML_CMETA_PROMPT_BARGEIN_SPEECH,
    VXML_CMETA_PROMPT_BARGEIN_HOTWORD
} vxml_cmeta_prompt_bargein_type;

typedef struct vxml_cmeta_prompt_media_segment_v1 {
    vxml_cmeta_prompt_media_segment_kind kind;
    vxml_cmeta_name_view payload;
    vxml_cmeta_name_view media_type;
} vxml_cmeta_prompt_media_segment_v1;

/**
 * Batch-relative conditional alternate range for one AUDIO segment.
 *
 * The provider plays the fallback range only when the referenced AUDIO
 * segment cannot be played. The range belongs to the same immutable segments
 * array and is borrowed for prepare_batch only.
 */
typedef struct vxml_cmeta_prompt_media_fallback_v1 {
    size_t audio_segment_index;
    size_t first_fallback_segment;
    size_t fallback_segment_count;
} vxml_cmeta_prompt_media_fallback_v1;

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

    /* Append-only prompt interruption policy. */
    bool bargein;
    vxml_cmeta_prompt_bargein_type bargein_type;
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

    /* Append-only prompt interruption policy. */
    bool bargein;
    vxml_cmeta_prompt_bargein_type bargein_type;

    /* Append-only conditional AUDIO alternate-content metadata. */
    const vxml_cmeta_prompt_media_fallback_v1 *fallbacks;
    size_t fallback_count;
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

typedef enum vxml_cmeta_prompt_mark_result {
    VXML_CMETA_PROMPT_MARK_ACCEPTED = 0,
    VXML_CMETA_PROMPT_MARK_CLOSED,
    VXML_CMETA_PROMPT_MARK_STALE,
    VXML_CMETA_PROMPT_MARK_NOT_MARK,
    VXML_CMETA_PROMPT_MARK_OUT_OF_ORDER,
    VXML_CMETA_PROMPT_MARK_INVALID_ARGUMENT
} vxml_cmeta_prompt_mark_result;

typedef struct vxml_cmeta_prompt_mark_view_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t generation;
    size_t segment_index;
    vxml_cmeta_name_view name;
} vxml_cmeta_prompt_mark_view_v1;

#define VXML_CMETA_PROMPT_MARK_VIEW_ABI_V1 1u

typedef enum vxml_cmeta_prompt_barge_result {
    VXML_CMETA_PROMPT_BARGE_CANCELED = 0,
    VXML_CMETA_PROMPT_BARGE_DISABLED,
    VXML_CMETA_PROMPT_BARGE_STALE,
    VXML_CMETA_PROMPT_BARGE_TYPE_MISMATCH,
    VXML_CMETA_PROMPT_BARGE_INVALID_ARGUMENT
} vxml_cmeta_prompt_barge_result;

typedef enum vxml_cmeta_prompt_media_outcome {
    VXML_CMETA_PROMPT_MEDIA_OUTCOME_COMPLETED = 1,
    VXML_CMETA_PROMPT_MEDIA_OUTCOME_FAILED
} vxml_cmeta_prompt_media_outcome;

typedef enum vxml_cmeta_prompt_media_failure {
    VXML_CMETA_PROMPT_MEDIA_FAILURE_NONE = 0,
    VXML_CMETA_PROMPT_MEDIA_FAILURE_BADFETCH,
    VXML_CMETA_PROMPT_MEDIA_FAILURE_UNSUPPORTED_FORMAT,
    VXML_CMETA_PROMPT_MEDIA_FAILURE_NORESOURCE
} vxml_cmeta_prompt_media_failure;

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

    /* Optional append-only terminal failure classification. */
    vxml_cmeta_prompt_media_failure failure;
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

typedef enum vxml_cmeta_terminal_kind {
    VXML_CMETA_TERMINAL_NONE = 0,
    VXML_CMETA_TERMINAL_EXIT,
    VXML_CMETA_TERMINAL_RETURN,
    VXML_CMETA_TERMINAL_RETURN_EVENT,
    VXML_CMETA_TERMINAL_DISCONNECT
} vxml_cmeta_terminal_kind;

vxml_status vxml_compile_cmeta(
    const void *bytes, size_t size,
    const vxml_limits *limits,
    const vxml_cmeta_compile_options_v1 *options,
    vxml_program *out, vxml_diagnostic *diagnostic);

vxml_status vxml_session_init_cmeta(
    vxml_session *session, const vxml_program *program,
    const vxml_cmeta_session_options_v1 *options);

/**
 * Start one independent READY CMeta Session as a subdialog child.
 *
 * The selected form's form-level declarations are initialized in child-owned
 * staged storage, then entry parameters are imported in the same transaction
 * before FIA selection. No entry bytes survive the call.
 */
vxml_status vxml_session_cmeta_start_child(
    vxml_session *session,
    const vxml_cmeta_child_entry_v1 *entry);

vxml_status vxml_session_cmeta_read(
    const vxml_session *session, const char *name, size_t name_size,
    vxml_cmeta_value_view *out_value);


/**
 * Build the active subdialog's parent-owned parameter snapshot and ask the
 * configured child owner to reserve admission. Provider callbacks borrow all
 * request views only for prepare().
 */
vxml_status vxml_session_cmeta_subdialog_prepare(
    vxml_session *session, const char **out_error);

/** Commit the prepared child-owner ticket; provider callback is no-fail. */
vxml_status vxml_session_cmeta_subdialog_commit(vxml_session *session);

/**
 * Discard only the prepared child-owner admission ticket. The parent-owned
 * snapshot remains valid for the same generation so a later prepare does not
 * re-evaluate parent expressions. It is released when the generation settles
 * or the Session is destroyed.
 */
vxml_status vxml_session_cmeta_subdialog_discard(vxml_session *session);


/**
 * MPSC admission of one terminal child completion. RETURN_EVENT and GLOBAL_EXIT
 * are enabled by #190; RETURN_DATA is reserved by this V1 envelope and becomes
 * admissible with the atomic parent result binder in #191.
 *
 * Names, STRING values, and Event bytes are copied into Session-owned mailbox
 * storage before ACCEPTED is returned.
 */
vxml_cmeta_subdialog_ingress_result
vxml_session_cmeta_subdialog_try_complete(
    vxml_session *session,
    const vxml_cmeta_subdialog_completion_v1 *completion);

/**
 * Single-owner progress point. Settles at most one accepted child completion.
 */
vxml_status vxml_session_cmeta_subdialog_run_ready(
    vxml_session *session,
    bool *out_progressed);

/** Borrow the active static record request. */
vxml_status vxml_session_cmeta_record_request(
    const vxml_session *session,
    vxml_cmeta_record_request_v1 *out_request);

/** Reserve the active record provider operation without committing work. */
vxml_status vxml_session_cmeta_record_prepare(
    vxml_session *session, const char **out_error);

/** Commit the currently prepared record provider ticket. */
vxml_status vxml_session_cmeta_record_commit(vxml_session *session);

/** Discard the currently prepared record provider ticket. */
vxml_status vxml_session_cmeta_record_discard(vxml_session *session);

/**
 * MPSC admission of one terminal recording completion.
 *
 * SUCCESS transfers exactly one move-only recording lease only when ACCEPTED
 * is returned. All other outcomes carry no lease. Metadata is bounded and
 * copied; recording payload bytes are never copied by TurboSCXML.
 */
vxml_cmeta_record_ingress_result
vxml_session_cmeta_record_try_complete(
    vxml_session *session,
    const vxml_cmeta_record_completion_v1 *completion);

/**
 * Single-owner progress point. Settles at most one accepted record completion
 * and returns to Directed FIA when the item remains non-terminal.
 */
vxml_status vxml_session_cmeta_record_run_ready(
    vxml_session *session,
    bool *out_progressed);

/**
 * Borrow the Session-owned result for record@name in the active form.
 * Borrowed bytes remain valid until replacement, committed clear, or close.
 */
vxml_status vxml_session_cmeta_record_result(
    const vxml_session *session,
    const char *name,
    size_t name_size,
    vxml_cmeta_record_result_view_v1 *out_result);

/** Borrow the currently selected directed-field collect request. */
vxml_status vxml_session_cmeta_collect_request(
    const vxml_session *session,
    vxml_cmeta_collect_request_v1 *out_request);

/** Borrow the currently selected static-menu collect request. */
vxml_status vxml_session_cmeta_menu_collect_request(
    const vxml_session *session,
    vxml_cmeta_menu_collect_request_v1 *out_request);

/**
 * Borrow the active menu including V2 speech-policy / external-grammar side tables.
 * V1 callers remain valid for menus that require no V2-only semantics.
 */
vxml_status vxml_session_cmeta_menu_collect_request_v2(
    const vxml_session *session,
    vxml_cmeta_menu_collect_request_v2 *out_request);

/** Borrow the active form-level grammar request selected through <initial>. */
vxml_status vxml_session_cmeta_initial_collect_request(
    const vxml_session *session,
    vxml_cmeta_initial_collect_request_v1 *out_request);

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
 * MPSC admission of one menu choice ordinal for the active menu generation.
 * No provider-owned choice bytes survive admission.
 */
vxml_cmeta_collect_ingress_result vxml_session_cmeta_menu_try_complete(
    vxml_session *session,
    const vxml_cmeta_menu_completion_v1 *completion);

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

/**
 * Notify the prompt owner that input for the active collect generation has
 * reached a barge-in boundary.
 *
 * UNSPECIFIED signal type is invalid. Explicit prompt bargeintype must match;
 * a prompt with unspecified bargeintype accepts either supported signal type.
 * Cancellation is generation-scoped and no-fail/nonblocking.
 */
vxml_cmeta_prompt_barge_result
vxml_session_cmeta_prompt_media_barge_in(
    vxml_session *session,
    uint64_t collect_generation,
    vxml_cmeta_prompt_bargein_type signal_type);

/**
 * Single-owner progress point reporting that one Program-owned MARK segment
 * has been executed. An asynchronous provider must marshal its callback to the
 * Session owner before calling this function.
 *
 * segment_index is relative to the current prompt batch. Progress is
 * generation-scoped and monotonically increasing.
 */
vxml_cmeta_prompt_mark_result
vxml_session_cmeta_prompt_media_mark(
    vxml_session *session,
    uint64_t generation,
    size_t segment_index);

/**
 * Borrow the last MARK executed for the most recently committed prompt
 * generation. segment_index is relative to that prompt batch. The returned
 * name borrows immutable Program storage.
 */
vxml_status vxml_session_cmeta_prompt_media_last_mark(
    const vxml_session *session,
    vxml_cmeta_prompt_mark_view_v1 *out_mark);

vxml_status vxml_session_cmeta_terminal_kind(
    const vxml_session *session,
    vxml_cmeta_terminal_kind *out_kind);

vxml_status vxml_session_cmeta_terminal_event(
    const vxml_session *session,
    vxml_cmeta_name_view *out_event);

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
