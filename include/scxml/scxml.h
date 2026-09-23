#ifndef SCXML_H
#define SCXML_H

#include <cflow/event.h>
#include <cflow/statechart.h>
#include <cflow/statechart_instance.h>
#include <cmeta/cmeta.h>
#include <cmeta/data.h>
#include <cserde/cserde.h>
#include <xml_parser/xml_parser.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCXML_DIAGNOSTIC_CAPACITY 256u
#define SCXML_ADAPTER_ABI 1u
#define SCXML_EVENT_METADATA_ABI 1u
#define SCXML_EVENT_ENVELOPE_ABI 1u
#define SCXML_CMETA_COMPILE_OPTIONS_ABI_V1 1u
#define SCXML_CMETA_COMPILE_OPTIONS_ABI_V2 2u
#define SCXML_CMETA_SESSION_OPTIONS_ABI_V1 1u
#define SCXML_CMETA_SESSION_OPTIONS_ABI_V2 2u
#define SCXML_CMETA_SESSION_OPTIONS_ABI_V3 3u
#define SCXML_CMETA_SESSION_OPTIONS_ABI_V4 4u
#define SCXML_DATA_RESOURCE_ADAPTER_ABI_V1 1u
#define SCXML_QUICKJS_COMPILE_OPTIONS_ABI_V1 1u
#define SCXML_QUICKJS_SESSION_OPTIONS_ABI_V1 1u
#define SCXML_TEXT_RESOURCE_ADAPTER_ABI_V1 1u
#define SCXML_CMETA_DEFAULT_MAX_ITERATIONS 65536u

#ifndef SCXML_PAYLOAD_MAX_ENTRIES
#define SCXML_PAYLOAD_MAX_ENTRIES 64u
#endif

typedef enum scxml_status {
    SCXML_OK = 0,
    SCXML_INVALID_ARGUMENT,
    SCXML_LIMIT_EXCEEDED,
    SCXML_ALLOCATION_FAILED,
    SCXML_XML_ERROR,
    SCXML_INVALID_NAMESPACE,
    SCXML_INVALID_VERSION,
    SCXML_UNSUPPORTED_DATAMODEL,
    SCXML_DUPLICATE_ID,
    SCXML_UNKNOWN_TARGET,
    SCXML_INVALID_STRUCTURE,
    SCXML_UNSUPPORTED_FEATURE,
    SCXML_NATIVE_IR_REJECTED
} scxml_status;

typedef struct scxml_limits {
    salts_xml_limits xml;
    size_t max_states;
    /** Maximum public compiled Event names; excludes one private routing slot. */
    size_t max_events;
    size_t max_transitions;
    size_t max_name_bytes;
} scxml_limits;

typedef struct scxml_diagnostic {
    scxml_status status;
    salts_xml_location location;
    char message[SCXML_DIAGNOSTIC_CAPACITY];
} scxml_diagnostic;

typedef struct scxml_program {
    void *impl;
} scxml_program;

/**
 * Versioned compile-time provider for the opt-in `datamodel="cmeta"` profile.
 *
 * `root` and every descriptor reachable from it remain borrowed and immutable
 * until program destruction. All limits are positive hard bounds; expression
 * programs are compiled once and owned by the resulting SCXML program.
 */
typedef struct scxml_cmeta_compile_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const cmeta_data_desc *root;
    size_t max_source_bytes;
    size_t max_instructions;
    size_t max_operands;
    size_t max_expression_depth;
    size_t max_path_depth;
    size_t max_literal_bytes;
    size_t max_string_bytes;
    /** Maximum items visited by one `<foreach>` invocation. */
    size_t max_iterations;
} scxml_cmeta_compile_options_v1;

/** One compile-scoped foreign executable element registration. */
typedef struct scxml_cmeta_custom_action_v1 {
    const char *namespace_uri;
    size_t namespace_uri_size;
    const char *local_name;
    size_t local_name_size;
    cmeta_callable callable;
    /** Unqualified XML attribute names in callable parameter order. */
    const char *const *parameter_names;
    size_t parameter_count;
} scxml_cmeta_custom_action_v1;

/**
 * CMeta compile provider with a bounded, compile-scoped custom action table.
 * Rows and strings are borrowed only until compilation returns. Bound callable
 * values are copied into the resulting program.
 */
typedef struct scxml_cmeta_compile_options_v2 {
    uint32_t abi_version;
    size_t struct_size;
    const cmeta_data_desc *root;
    size_t max_source_bytes;
    size_t max_instructions;
    size_t max_operands;
    size_t max_expression_depth;
    size_t max_path_depth;
    size_t max_literal_bytes;
    size_t max_string_bytes;
    size_t max_iterations;
    const scxml_cmeta_custom_action_v1 *actions;
    size_t action_count;
} scxml_cmeta_compile_options_v2;

/**
 * Versioned per-session state provider for a CMeta-compiled program.
 * `initial_state` is borrowed only until initialization returns; the native
 * Statechart copies it using the program root descriptor's storage type.
 */
typedef struct scxml_cmeta_session_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const void *initial_state;
} scxml_cmeta_session_options_v1;

/**
 * Borrowed name of one top-level `<data>` value supplied by the environment.
 * The location is a byte-exact CMeta path view valid until session
 * initialization returns.
 */
typedef struct scxml_cmeta_environment_override {
    const char *location;
    size_t location_size;
} scxml_cmeta_environment_override;

/**
 * Versioned CMeta session provider with explicit environment presence.
 *
 * `initial_state` owns all values. Override rows only suppress the matching
 * top-level document initializer and are borrowed until initialization
 * returns. Rows must be unique, writable, and match declared root `<data>`
 * locations exactly.
 */
typedef struct scxml_cmeta_session_options_v2 {
    uint32_t abi_version;
    size_t struct_size;
    const void *initial_state;
    const scxml_cmeta_environment_override *environment_overrides;
    size_t environment_override_count;
} scxml_cmeta_session_options_v2;

typedef enum scxml_resource_status {
    SCXML_RESOURCE_OK = 0,
    SCXML_RESOURCE_NOT_FOUND,
    SCXML_RESOURCE_TIMEOUT,
    SCXML_RESOURCE_DENIED,
    SCXML_RESOURCE_LIMIT_EXCEEDED,
    SCXML_RESOURCE_INVALID_DATA,
    SCXML_RESOURCE_FAILED
} scxml_resource_status;

/**
 * One adapter-owned CSerde stream. A successful `open` publishes a READY
 * reader and optional lease. TurboSCXML calls `close` exactly once after that
 * success, including decode failure; the callback must release every backing
 * object referenced by the reader.
 */
typedef struct scxml_data_resource {
    cserde_reader reader;
    void *lease;
} scxml_data_resource;

/**
 * Synchronous host boundary for `<data src>`. The URI and expected descriptor
 * are borrowed only for `open`; adapter operations and `user` remain borrowed
 * through successful session destruction. The adapter must enforce resource,
 * transport, authorization, and media-type policy before returning a reader.
 */
typedef struct scxml_data_resource_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    scxml_resource_status (*open)(
        void *user, const char *uri, size_t uri_size,
        const cmeta_data_desc *expected, scxml_data_resource *out);
    void (*close)(void *user, scxml_data_resource *resource);
} scxml_data_resource_adapter_v1;

/**
 * Versioned CMeta session provider with external data resources.
 *
 * V2 fields retain their semantics. This v3 record retains its published ABI
 * layout while using the DataBind terminology. New callers should use v4,
 * which exposes an explicit aggregate-owned byte bound. v3 derives that bound
 * from max_data_items * max_data_buffer_bytes with checked arithmetic.
 * Adapter operations are copied; its user pointer remains borrowed until
 * successful session destruction.
 */
typedef struct scxml_cmeta_session_options_v3 {
    uint32_t abi_version;
    size_t struct_size;
    const void *initial_state;
    const scxml_cmeta_environment_override *environment_overrides;
    size_t environment_override_count;
    const scxml_data_resource_adapter_v1 *data_resources;
    void *data_resource_user;
    size_t data_bind_workspace_bytes;
    size_t max_data_depth;
    size_t max_data_items;
    size_t max_data_buffer_bytes;
} scxml_cmeta_session_options_v3;

/**
 * Canonical DataBind-backed CMeta session provider with external resources.
 *
 * `data_bind_workspace_bytes` bounds the caller-owned DataBind native
 * workspace. `max_data_items` bounds the whole native descriptor/value graph,
 * `max_data_owned_bytes` bounds aggregate owned STRING/BYTES payload, and
 * `max_data_buffer_bytes` bounds any one owned value.
 */
typedef struct scxml_cmeta_session_options_v4 {
    uint32_t abi_version;
    size_t struct_size;
    const void *initial_state;
    const scxml_cmeta_environment_override *environment_overrides;
    size_t environment_override_count;
    const scxml_data_resource_adapter_v1 *data_resources;
    void *data_resource_user;
    size_t data_bind_workspace_bytes;
    size_t max_data_depth;
    size_t max_data_items;
    size_t max_data_owned_bytes;
    size_t max_data_buffer_bytes;
} scxml_cmeta_session_options_v4;

/** Adapter-owned immutable UTF-8 text returned during program admission. */
typedef struct scxml_text_resource {
    const char *data;
    size_t size;
    void *lease;
} scxml_text_resource;

/**
 * Synchronous compile-time boundary for `<script src>`. URI and result views
 * are borrowed only for the call. A successful `open` is paired with exactly
 * one `close`, after TurboSCXML has copied the bounded source text.
 */
typedef struct scxml_text_resource_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    scxml_resource_status (*open)(
        void *user, const char *uri, size_t uri_size,
        size_t max_bytes, scxml_text_resource *out);
    void (*close)(void *user, scxml_text_resource *resource);
} scxml_text_resource_adapter_v1;

/**
 * Versioned limits for the opt-in `datamodel="quickjs-sandbox"` profile.
 * No QuickJS ABI type crosses this boundary. Every size/time limit is a
 * positive hard bound. The CMeta schema remains the session state authority.
 * Signed and unsigned integers must stay within ECMAScript's exact safe
 * integer range. Bool, integer, float, string, enum, struct, and one-level
 * sequence values are admitted; unsupported or nested generic shapes fail
 * compilation instead of failing during session execution.
 */
typedef struct scxml_quickjs_compile_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const cmeta_data_desc *root;
    size_t max_source_bytes;
    size_t max_instructions;
    size_t max_operands;
    size_t max_expression_depth;
    size_t max_path_depth;
    size_t max_literal_bytes;
    size_t max_string_bytes;
    size_t max_iterations;
    size_t max_script_variables;
    size_t max_heap_bytes;
    size_t max_stack_bytes;
    uint64_t max_eval_milliseconds;
    size_t max_conversion_depth;
    /** Cumulative import/export properties, including supplemental slots. */
    size_t max_properties;
    size_t max_array_items;
    size_t max_snapshot_bytes;
    const scxml_text_resource_adapter_v1 *script_resources;
    void *script_resource_user;
} scxml_quickjs_compile_options_v1;

/** Initial CMeta object copied by one QuickJS-profile session. */
typedef struct scxml_quickjs_session_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const void *initial_state;
} scxml_quickjs_session_options_v1;

typedef enum scxml_program_requirement {
    SCXML_REQUIREMENT_NONE = 0u,
    SCXML_REQUIREMENT_EVENT_IO = 1u << 0u,
    SCXML_REQUIREMENT_DELAYED_SEND = 1u << 1u,
    SCXML_REQUIREMENT_CANCEL = 1u << 2u,
    SCXML_REQUIREMENT_INVOKE = 1u << 3u,
    SCXML_REQUIREMENT_PAYLOAD = 1u << 4u,
    SCXML_REQUIREMENT_INVOKE_PAYLOAD = 1u << 5u,
    SCXML_REQUIREMENT_INVOKE_IDLOCATION = 1u << 6u,
    SCXML_REQUIREMENT_CONTENT = 1u << 7u,
    SCXML_REQUIREMENT_INVOKE_CONTENT = 1u << 8u,
    SCXML_REQUIREMENT_LATE_BINDING = 1u << 9u,
    SCXML_REQUIREMENT_DATA_RESOURCE = 1u << 10u
} scxml_program_requirement;

typedef enum scxml_event_io_capability {
    SCXML_EVENT_IO_CAP_SEND = UINT64_C(1) << 0u,
    SCXML_EVENT_IO_CAP_DELAYED_SEND = UINT64_C(1) << 1u,
    SCXML_EVENT_IO_CAP_CANCEL = UINT64_C(1) << 2u,
    SCXML_EVENT_IO_CAP_PAYLOAD = UINT64_C(1) << 3u,
    SCXML_EVENT_IO_CAP_CONTENT = UINT64_C(1) << 4u
} scxml_event_io_capability;

typedef enum scxml_adapter_status {
    SCXML_ADAPTER_ACCEPTED = 0,
    SCXML_ADAPTER_ERROR_EXECUTION,
    SCXML_ADAPTER_ERROR_COMMUNICATION,
    SCXML_ADAPTER_FULL,
    SCXML_ADAPTER_CLOSED,
    SCXML_ADAPTER_INVALID_CONTRACT
} scxml_adapter_status;

typedef enum scxml_adapter_error_kind {
    SCXML_ADAPTER_ERROR_KIND_EXECUTION = 1,
    SCXML_ADAPTER_ERROR_KIND_COMMUNICATION
} scxml_adapter_error_kind;

typedef enum scxml_location_status {
    SCXML_LOCATION_OK = 0,
    SCXML_LOCATION_INVALID_ARGUMENT,
    SCXML_LOCATION_TOO_SMALL
} scxml_location_status;

/** Immutable Event I/O processor identity supplied when a session is built. */
typedef struct scxml_ioprocessor_descriptor {
    const char *name;
    size_t name_size;
    const char *type;
    size_t type_size;
    const char *location;
    size_t location_size;
} scxml_ioprocessor_descriptor;

#ifndef SCXML_EVENT_METADATA_CAPACITY
#define SCXML_EVENT_METADATA_CAPACITY 256u
#endif

#ifndef SCXML_EVENT_DATA_CAPACITY
#define SCXML_EVENT_DATA_CAPACITY 4096u
#endif

typedef enum scxml_payload_value_kind {
    SCXML_PAYLOAD_VALUE_INVALID = 0,
    SCXML_PAYLOAD_VALUE_BOOL,
    SCXML_PAYLOAD_VALUE_SINT,
    SCXML_PAYLOAD_VALUE_UINT,
    SCXML_PAYLOAD_VALUE_FLOAT,
    SCXML_PAYLOAD_VALUE_STRING
} scxml_payload_value_kind;

/** Format-neutral scalar copied or borrowed only for one prepare callback. */
typedef struct scxml_payload_value {
    scxml_payload_value_kind kind;
    union {
        bool boolean;
        int64_t sint;
        uint64_t uint;
        double number;
        struct {
            const char *data;
            size_t size;
        } string;
    } data;
} scxml_payload_value;

typedef enum scxml_payload_kind {
    SCXML_PAYLOAD_NONE = 0,
    SCXML_PAYLOAD_CONTENT,
    SCXML_PAYLOAD_NAMED
} scxml_payload_kind;

typedef enum scxml_content_kind {
    SCXML_CONTENT_INVALID = 0,
    SCXML_CONTENT_SCALAR,
    SCXML_CONTENT_TEXT_UTF8,
    SCXML_CONTENT_XML_UTF8,
    SCXML_CONTENT_CMETA
} scxml_content_kind;

/**
 * Callback-scoped format-neutral content. UTF-8 bytes are compact immutable
 * program storage. CMETA borrows one object and its schema from staged state.
 * No pointer remains valid after the prepare callback returns.
 */
typedef struct scxml_content_view {
    scxml_content_kind kind;
    scxml_payload_value scalar;
    const char *bytes;
    size_t byte_count;
    const cmeta_data_desc *schema;
    const void *object;
} scxml_content_view;

typedef struct scxml_payload_entry {
    const char *name;
    size_t name_size;
    scxml_content_view value;
} scxml_payload_entry;

/**
 * Callback-scoped payload view. Named entries preserve SCXML order and
 * duplicates. Every pointer is invalid after the prepare callback returns.
 */
typedef struct scxml_payload_view {
    scxml_payload_kind kind;
    scxml_content_view content;
    const scxml_payload_entry *entries;
    size_t entry_count;
} scxml_payload_view;

/** Borrowed request fields valid only during one prepare callback. */
typedef struct scxml_send_request {
    const char *event;
    size_t event_size;
    const char *target;
    size_t target_size;
    const char *type;
    size_t type_size;
    const char *id;
    size_t id_size;
    uint64_t delay_ms;
    scxml_payload_view payload;
} scxml_send_request;

/**
 * Owned external Event metadata copied atomically during admission. Scalar and
 * UTF-8 content is copied into the metadata bound. CMETA content must use the
 * session's compiled root schema, fit `SCXML_EVENT_DATA_CAPACITY`, and provide
 * copy/destroy traits.
 */
typedef struct scxml_event_metadata {
    uint32_t abi_version;
    size_t struct_size;
    const char *send_id;
    size_t send_id_size;
    const char *origin;
    size_t origin_size;
    const char *origin_type;
    size_t origin_type_size;
    const char *invoke_id;
    size_t invoke_id_size;
    scxml_content_view data;
} scxml_event_metadata;

/**
 * Complete borrowed SCXML Event view valid only during one adapter callback.
 * Consumers must validate `abi_version` and `struct_size`, then copy every
 * field retained after the callback returns.
 */
typedef struct scxml_event_envelope_view {
    uint32_t abi_version;
    size_t struct_size;
    const char *name;
    size_t name_size;
    const char *type;
    size_t type_size;
    const char *send_id;
    size_t send_id_size;
    const char *origin;
    size_t origin_size;
    const char *origin_type;
    size_t origin_type_size;
    const char *invoke_id;
    size_t invoke_id_size;
    scxml_content_view data;
} scxml_event_envelope_view;

typedef struct scxml_cancel_request {
    const char *send_id;
    size_t send_id_size;
} scxml_cancel_request;

/**
 * Exact-shape Event I/O reservation table copied by session initialization.
 *
 * Prepare callbacks run on the session SerialExecutor. They must reserve all
 * capacity without publishing the effect and copy every request field needed
 * after return. ACCEPTED transfers one valid move-only ticket to the session;
 * its commit or discard callback is invoked exactly once and must be
 * nonblocking and infallible. Non-ACCEPTED results transfer no ticket.
 *
 * `close` is nonblocking and called exactly once after adapter attachment,
 * including initialization failures. Once `is_quiescent` returns true after
 * close, no adapter-owned callback may reach the borrowed session or user.
 *
 * A host that implements the SCXML Event I/O Processor owns its session
 * registry, target-access policy, transport/codec boundary, and bounded
 * ingress queues. It must treat an empty request type as the canonical SCXML
 * processor URI, route an empty target back to the source external queue,
 * resolve `#_scxml_<sessionid>`, `#_parent`, and `#_<invokeid>` in source
 * context, and reject a missing or inaccessible session with
 * `ERROR_COMMUNICATION`. Unsupported nonempty processor types return
 * `ERROR_EXECUTION`. Request order is preserved per source session.
 *
 * Prepare must reserve host ingress capacity, not target-session mailbox
 * capacity that can be lost before commit. Commit publishes only that reserved
 * host row; a host dispatcher subsequently calls
 * `scxml_session_try_send_with_metadata()` and
 * retains the row on `CFLOW_MAILBOX_FULL`. Async decode or delivery failure is
 * reported with `session_report_adapter_error`. Capabilities are promises:
 * delayed send and cancellation must not be advertised unless the host owns a
 * bounded timer/id registry and implements their completion races.
 * Session initialization executes initial entry work before returning. A host
 * profile that accepts initial external sends must prepare a source endpoint
 * before init, queue those committed rows without dispatching to the still
 * initializing session, and activate/register the generated location only
 * after init succeeds. Close must discard those rows if init fails.
 */
typedef struct scxml_event_io_adapter {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t capabilities;
    scxml_adapter_status (*prepare_send)(
        void *user, const scxml_send_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    scxml_adapter_status (*prepare_cancel)(
        void *user, const scxml_cancel_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    void (*close)(void *user);
    bool (*is_quiescent)(void *user);
} scxml_event_io_adapter;

typedef enum scxml_invoke_capability {
    SCXML_INVOKE_CAP_START = UINT64_C(1) << 0u,
    SCXML_INVOKE_CAP_CANCEL = UINT64_C(1) << 1u,
    SCXML_INVOKE_CAP_FORWARD = UINT64_C(1) << 2u,
    SCXML_INVOKE_CAP_PAYLOAD = UINT64_C(1) << 3u,
    SCXML_INVOKE_CAP_CONTENT = UINT64_C(1) << 4u
} scxml_invoke_capability;

/** Borrowed invocation fields valid only during one prepare callback. */
typedef struct scxml_invoke_start_request {
    uint64_t token;
    const char *id;
    size_t id_size;
    const char *type;
    size_t type_size;
    const char *src;
    size_t src_size;
    bool autoforward;
    scxml_payload_view payload;
} scxml_invoke_start_request;

typedef struct scxml_invoke_cancel_request {
    uint64_t token;
    const char *id;
    size_t id_size;
} scxml_invoke_cancel_request;

typedef struct scxml_invoke_forward_request {
    uint64_t token;
    const char *id;
    size_t id_size;
    /** Borrowed Event view valid only for the callback duration. */
    const cflow_event_view *event;
    /** Complete borrowed SCXML Event copy, valid only for this callback. */
    const scxml_event_envelope_view *envelope;
} scxml_invoke_forward_request;

/**
 * Exact-shape invocation adapter copied by session initialization.
 *
 * Prepare callbacks run on the session SerialExecutor without the session
 * registry mutex held. ACCEPTED transfers one valid move-only effect ticket;
 * the session invokes exactly one of commit and discard. The adapter must copy
 * every borrowed request or envelope field retained after return. `close` and
 * `is_quiescent` follow the Event I/O adapter ownership contract above.
 */
typedef struct scxml_invoke_adapter {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t capabilities;
    scxml_adapter_status (*prepare_start)(
        void *user, const scxml_invoke_start_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    scxml_adapter_status (*prepare_cancel)(
        void *user, const scxml_invoke_cancel_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    scxml_adapter_status (*prepare_forward)(
        void *user, const scxml_invoke_forward_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    void (*close)(void *user);
    bool (*is_quiescent)(void *user);
} scxml_invoke_adapter;

typedef struct scxml_invoke_stats {
    uint64_t started;
    uint64_t start_failed;
    uint64_t cancelled;
    /** Committed exits whose adapter cancellation could not be published. */
    uint64_t cancel_failed;
    uint64_t completed;
    uint64_t returned_accepted;
    uint64_t returned_rejected;
    uint64_t forwarded;
    uint64_t forward_failed;
    /** Recoverable adapter errors rejected by the bounded internal ingress. */
    uint64_t adapter_error_rejected;
    size_t active;
} scxml_invoke_stats;

typedef struct scxml_session_config {
    /** Borrowed immutable program; it must outlive session destruction. */
    const scxml_program *program;
    cflow_executor *executor;
    size_t external_event_capacity;
    size_t internal_event_capacity;
    /**
     * Bounded native completion queue. Programs using `donedata` retain this
     * many queued derived payload rows plus one row borrowed by the current
     * Event. Each row retains at most `SCXML_EVENT_METADATA_CAPACITY + 1`
     * text bytes and `SCXML_EVENT_DATA_CAPACITY` aligned object bytes plus
     * row metadata.
     */
    size_t completion_capacity;
    size_t microstep_limit;
    size_t max_storage_bytes;
    cflow_clock *clock;
    size_t timer_capacity;
    /**
     * Maximum effect tickets staged by one rollback-capable microstep.
     * A CMeta late-binding session reserves one row while a microstep has
     * first-entry initializers pending.
     */
    size_t effect_capacity;
    /** Bounded MPSC ingress for asynchronous adapter error Events. */
    size_t adapter_internal_event_capacity;
    /** Maximum retained delayed sends in this session. */
    size_t delayed_send_capacity;
    /** Ops are copied; adapter_user remains borrowed through destruction. */
    const scxml_event_io_adapter *event_io;
    void *adapter_user;
    /** Fixed invocation registry rows; must cover the compiled descriptors. */
    size_t invocation_capacity;
    /** Ops are copied; invoke_user remains borrowed through destruction. */
    const scxml_invoke_adapter *invoke;
    void *invoke_user;
    /**
     * Borrowed descriptors copied during initialization. The built-in
     * `scxml` processor is always synthesized by the session. Each configured
     * name is a unique XML NCName; types are unique nonempty byte strings.
     */
    const scxml_ioprocessor_descriptor *ioprocessors;
    size_t ioprocessor_count;
} scxml_session_config;

typedef struct scxml_session {
    void *impl;
} scxml_session;

scxml_limits scxml_default_limits(void);

/** Return v1 bounded defaults with `root` installed as a borrowed schema. */
scxml_cmeta_compile_options_v1
scxml_cmeta_default_compile_options(const cmeta_data_desc *root);

/** Return V2 bounded defaults with an empty custom action table. */
scxml_cmeta_compile_options_v2
scxml_cmeta_default_compile_options_v2(const cmeta_data_desc *root);

/** Return bounded QuickJS profile defaults with a borrowed CMeta root. */
scxml_quickjs_compile_options_v1
scxml_quickjs_default_compile_options(const cmeta_data_desc *root);

/**
 * Validate and compile an SCXML Core document into one owning program.
 * `out` must be zero-initialized. Input and temporary XML/IR rows are copied;
 * failure leaves `out` empty and returns the first diagnostic detected by the
 * deterministic admission pipeline. Within a validation phase, document order
 * is preserved.
 */
scxml_status scxml_compile(
    scxml_program *out,
    const char *input,
    size_t input_size,
    const scxml_limits *limits,
    scxml_diagnostic *diagnostic);

/**
 * Compile an exact `datamodel="cmeta"` document with an explicit provider.
 * There is no implicit provider fallback. Ownership and failure guarantees
 * otherwise match `scxml_compile()`.
 */
scxml_status scxml_compile_cmeta(
    scxml_program *out,
    const char *input,
    size_t input_size,
    const scxml_limits *limits,
    const scxml_cmeta_compile_options_v1 *options,
    scxml_diagnostic *diagnostic);

/** Compile exact `datamodel="cmeta"` with custom executable actions. */
scxml_status scxml_compile_cmeta_v2(
    scxml_program *out,
    const char *input,
    size_t input_size,
    const scxml_limits *limits,
    const scxml_cmeta_compile_options_v2 *options,
    scxml_diagnostic *diagnostic);

/**
 * Compile exact `datamodel="quickjs-sandbox"`. Disabled builds return
 * `SCXML_UNSUPPORTED_FEATURE` without loading a script resource.
 */
scxml_status scxml_compile_quickjs(
    scxml_program *out,
    const char *input,
    size_t input_size,
    const scxml_limits *limits,
    const scxml_quickjs_compile_options_v1 *options,
    scxml_diagnostic *diagnostic);

/** Destroy a quiescent program and its native Statechart/name mappings. */
void scxml_program_destroy(scxml_program *program);

/** Borrowed Statechart; invalid after program destruction. */
const cflow_statechart *scxml_program_statechart(
    const scxml_program *program);

bool scxml_program_state_id(const scxml_program *program,
                                  const char *name,
                                  size_t name_size,
                                  cflow_machine_state_id *out_id);
bool scxml_program_event_id(const scxml_program *program,
                                  const char *name,
                                  size_t name_size,
                                  cflow_event_id *out_id);

/** Borrowed inert `false` value matching the program's null data-model type. */
const void *scxml_program_initial_state(
    const scxml_program *program);

/**
 * Construct a borrowed null-data-model Event view by name. The Statechart instance
 * copies the payload during successful mailbox admission.
 */
bool scxml_program_event(const scxml_program *program,
                               const char *name,
                               size_t name_size,
                               cflow_event_view *out_event);

/** Copy the program's immutable execution requirements bitmask. */
bool scxml_program_requirements(
    const scxml_program *program, uint32_t *out_requirements);

/**
 * Borrow the native executable bindings compiled for this program.
 *
 * A structural program succeeds with a NULL view and zero count. The returned
 * rows and their callback user pointers are invalidated by program destruction,
 * so the program must outlive every Statechart instance configured with them.
 * CMeta `_event.name` is the only Event field available through these
 * program-level bindings. Use an owning CMeta session for the complete
 * read-only `_event` envelope and its run-to-completion lifetime.
 * CMeta expressions that read `_sessionid` require the owning session adapters
 * installed by `scxml_session_init_cmeta()` and fail through these
 * program-level rows.
 * Invalid arguments return false without modifying either output.
 */
bool scxml_program_instance_bindings(
    const scxml_program *program,
    const cflow_statechart_executable_binding **out_bindings,
    size_t *out_count);

/**
 * Borrow the native guard bindings compiled for transition conditions.
 *
 * A program without conditioned transitions succeeds with a NULL view and
 * zero count. The returned rows and callback user pointers are invalidated by
 * program destruction, so the program must outlive every configured
 * Statechart instance. Invalid arguments return false without modifying either
 * output.
 * CMeta `_event.name` is the only Event field available through these
 * program-level guards. Use an owning CMeta session for the complete read-only
 * `_event` envelope and its run-to-completion lifetime.
 * CMeta guards that read `_sessionid` require the owning session adapters
 * installed by `scxml_session_init_cmeta()`.
 */
bool scxml_program_guard_bindings(
    const scxml_program *program,
    const cflow_statechart_guard_binding **out_bindings,
    size_t *out_count);

/**
 * Initialize one owning mutable SCXML session over an immutable program.
 * Required capacities and adapter capabilities are checked against the
 * program requirements before attachment. The program, executor, and adapter
 * user remain borrowed until successful session destruction.
 */
cflow_statechart_instance_status scxml_session_init(
    scxml_session *session,
    const scxml_session_config *config);

/**
 * Initialize a CMeta program session from one call-scoped initial object.
 * The session copies the document name and generates an immutable UUID string
 * for `_sessionid` before attaching the native Statechart instance.
 */
cflow_statechart_instance_status scxml_session_init_cmeta(
    scxml_session *session,
    const scxml_session_config *config,
    const scxml_cmeta_session_options_v1 *options);

/**
 * Initialize a CMeta session and preserve explicitly supplied top-level data.
 *
 * Returns `CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT` for an invalid V2
 * contract, unknown/non-root location, or duplicate row, and
 * `CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED` when retained index storage
 * cannot be allocated. On failure, `session` remains empty.
 */
cflow_statechart_instance_status scxml_session_init_cmeta_v2(
    scxml_session *session,
    const scxml_session_config *config,
    const scxml_cmeta_session_options_v2 *options);

/**
 * Initialize a CMeta session with an optional bounded `<data src>` provider.
 * Missing or invalid provider configuration fails before attaching the native
 * instance. Provider failures during binding raise `error.execution` and do
 * not publish a partial destination value.
 */
cflow_statechart_instance_status scxml_session_init_cmeta_v3(
    scxml_session *session,
    const scxml_session_config *config,
    const scxml_cmeta_session_options_v3 *options);

/** Initialize a CMeta session using the canonical DataBind resource profile. */
cflow_statechart_instance_status scxml_session_init_cmeta_v4(
    scxml_session *session,
    const scxml_session_config *config,
    const scxml_cmeta_session_options_v4 *options);

/** Initialize one session for a QuickJS-compiled program. */
cflow_statechart_instance_status scxml_session_init_quickjs(
    scxml_session *session,
    const scxml_session_config *config,
    const scxml_quickjs_session_options_v1 *options);

cflow_mailbox_status scxml_session_try_send(
    scxml_session *session, const cflow_event_view *event);

/**
 * Copy one external Event and its bounded metadata atomically. Metadata rows
 * are capacity-coupled to the configured external mailbox and released before
 * transition selection. Oversized or partially NULL fields fail admission.
 */
cflow_mailbox_status scxml_session_try_send_with_metadata(
    scxml_session *session, const cflow_event_view *event,
    const scxml_event_metadata *metadata);
/**
 * Copy an arbitrarily named external Event and its bounded metadata. The
 * incoming name is routed by the longest compiled hierarchical Event prefix;
 * an otherwise unmatched name can select only a `*` transition. `_event.name`
 * retains the complete incoming name during processing. Names longer than
 * `SCXML_EVENT_METADATA_CAPACITY` bytes are rejected before admission.
 */
cflow_mailbox_status scxml_session_try_send_named_with_metadata(
    scxml_session *session, const char *name, size_t name_size,
    const scxml_event_metadata *metadata);
/**
 * Copy one returned invocation Event into the external FIFO with its live
 * session token. Admission validates the token once; external preprocessing
 * revalidates it to close the admission/cancellation race. Stale tokens return
 * `INVALID_ARGUMENT` before admission or are dropped after dequeue.
 */
cflow_mailbox_status scxml_session_report_invoke_event(
    scxml_session *session, uint64_t token,
    const cflow_event_view *event);
/**
 * Admit the compiled done Event for one live invocation token. Dynamic
 * `idlocation` identity is exposed as `done.invoke.<active-id>` and through
 * `_event.invokeid`; the finite compiled Event ID remains the routing key.
 * Zero, stale, completed, or cancelled tokens return `INVALID_ARGUMENT`.
 */
cflow_mailbox_status scxml_session_report_invoke_done(
    scxml_session *session, uint64_t token);
/**
 * Concurrently admit one asynchronous adapter failure to the prioritized
 * bounded internal ingress. The exact mailbox result is returned; there is no
 * retry or external-queue fallback.
 */
cflow_mailbox_status scxml_session_report_adapter_error(
    scxml_session *session,
    scxml_adapter_error_kind kind);
/**
 * Release one committed delayed-send registry row. Returns true only when the
 * named row was active in this session and this call won the completion race.
 */
bool scxml_session_report_send_done(
    scxml_session *session, const char *send_id, size_t send_id_size);
void scxml_session_close(scxml_session *session);
/**
 * Stop admission and asynchronously execute every active state's `onexit`
 * content on the session SerialExecutor before cancelled termination. A
 * semantic microstep already reaching commit remains visible. This function
 * does not wait for the controlled exit; observe the executor or session stats.
 */
void scxml_session_cancel(scxml_session *session);
bool scxml_session_get_stats(
    const scxml_session *session,
    cflow_statechart_instance_stats *out);
/** Copy the fixed invocation registry counters under the session mutex. */
bool scxml_session_get_invoke_stats(
    const scxml_session *session, scxml_invoke_stats *out);
/**
 * Copy the immutable SCXML Event I/O address used by `_ioprocessors.scxml`.
 * `out_required_capacity` includes the trailing NUL. A short or NULL output
 * buffer returns `TOO_SMALL`, reports the required capacity, and writes no
 * partial string. The address remains stable until successful destruction.
 */
scxml_location_status scxml_session_copy_location(
    const scxml_session *session, char *out_location,
    size_t location_capacity, size_t *out_required_capacity);
/**
 * Copy the immutable Event I/O address registered for an exact processor
 * type. `out_required_capacity` includes the trailing NUL. Short and NULL
 * output buffers write no partial string.
 */
scxml_location_status scxml_session_copy_ioprocessor_location(
    const scxml_session *session,
    const char *type, size_t type_size,
    char *out_location, size_t location_capacity,
    size_t *out_required_capacity);
/**
 * Verify that a live session retained the exact program, Event I/O adapter,
 * adapter user, and processor descriptor supplied by an external binding.
 * All arguments are borrowed and must remain live for the duration of the
 * call. This query does not transfer ownership or activate the binding.
 */
bool scxml_session_matches_event_io(
    const scxml_session *session,
    const scxml_program *program,
    const scxml_event_io_adapter *adapter,
    const void *adapter_user,
    const scxml_ioprocessor_descriptor *ioprocessor);
const char *scxml_session_error(
    const scxml_session *session);

/**
 * Stop admission and close the adapter exactly once. Destruction returns
 * `WOULD_BLOCK` while the adapter reports non-quiescent and preserves the
 * owning handle for a later retry.
 */
cflow_statechart_instance_status scxml_session_destroy(
    scxml_session *session);

#ifdef __cplusplus
}
#endif

#endif /* SCXML_H */
