#ifndef TURBO_CCXML_H
#define TURBO_CCXML_H

#include <scxml/scxml.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CCXML_DIAGNOSTIC_CAPACITY 256u
#define CCXML_TELEPHONY_ADAPTER_ABI_V1 1u
#define CCXML_DATAMODEL_ADAPTER_ABI_V1 1u
#define CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1 1u

typedef enum ccxml_status {
    CCXML_OK = 0,
    CCXML_INVALID_ARGUMENT,
    CCXML_XML_ERROR,
    CCXML_ALLOCATION_FAILED,
    CCXML_LIMIT_EXCEEDED,
    CCXML_INVALID_NAMESPACE,
    CCXML_INVALID_VERSION,
    CCXML_INVALID_STRUCTURE,
    CCXML_UNSUPPORTED_FEATURE,
    CCXML_INVALID_EVENT,
    CCXML_ADAPTER_ERROR,
    CCXML_INVALID_CONTRACT,
    CCXML_CLOSED,
    CCXML_BUSY
} ccxml_status;

typedef struct ccxml_limits {
    turbo_xml_limits xml;
    size_t max_transitions;
    size_t max_actions;
    size_t max_name_bytes;
} ccxml_limits;

typedef struct ccxml_diagnostic {
    ccxml_status status;
    turbo_xml_location location;
    char message[CCXML_DIAGNOSTIC_CAPACITY];
} ccxml_diagnostic;

typedef struct ccxml_program {
    void *impl;
} ccxml_program;

ccxml_limits ccxml_default_limits(void);

ccxml_status ccxml_compile(
    ccxml_program *out,
    const char *input,
    size_t input_size,
    const ccxml_limits *limits,
    ccxml_diagnostic *diagnostic);

void ccxml_program_destroy(ccxml_program *program);

/** Borrowed event fields valid only for one synchronous dispatch call. */
typedef struct ccxml_event {
    const char *name;
    size_t name_size;
    const char *connection_id;
    size_t connection_id_size;
    const char *conference_id;
    size_t conference_id_size;
} ccxml_event;

typedef struct ccxml_string_view {
    const char *data;
    size_t size;
} ccxml_string_view;

/** Borrowed request fields valid only during one prepare callback. */
typedef struct ccxml_accept_request {
    const char *connection_id;
    size_t connection_id_size;
} ccxml_accept_request;

/** Borrowed request fields valid only during one prepare callback. */
typedef struct ccxml_create_call_request {
    const char *destination;
    size_t destination_size;
} ccxml_create_call_request;

/** Borrowed request fields valid only during one prepare callback. */
typedef struct ccxml_disconnect_request {
    const char *connection_id;
    size_t connection_id_size;
} ccxml_disconnect_request;

/** Borrowed request fields valid only during one prepare callback. */
typedef struct ccxml_reject_request {
    const char *connection_id;
    size_t connection_id_size;
} ccxml_reject_request;

/** Borrowed request fields valid only during one prepare callback. */
typedef struct ccxml_redirect_request {
    const char *connection_id;
    size_t connection_id_size;
    const char *destination;
    size_t destination_size;
} ccxml_redirect_request;

/** Borrowed request fields valid only during one prepare callback. */
typedef struct ccxml_join_request {
    const char *id1;
    size_t id1_size;
    const char *id2;
    size_t id2_size;
} ccxml_join_request;

/** Borrowed request fields valid only during one prepare callback. */
typedef struct ccxml_unjoin_request {
    const char *id1;
    size_t id1_size;
    const char *id2;
    size_t id2_size;
} ccxml_unjoin_request;

/** Borrowed request fields valid only during one prepare callback. */
typedef struct ccxml_merge_request {
    const char *connection_id1;
    size_t connection_id1_size;
    const char *connection_id2;
    size_t connection_id2_size;
} ccxml_merge_request;

/** Borrowed request fields valid only during one prepare callback. */
typedef struct ccxml_create_conference_request {
    /** Optional provider lookup/attachment name; empty when omitted. */
    const char *conference_name;
    size_t conference_name_size;
} ccxml_create_conference_request;

/**
 * Synchronous left-value boundary copied by session initialization.
 *
 * Validation runs once per compiled write location before the session is
 * published. Assignment prepare must copy location/value bytes it retains and
 * must not mutate live state. ACCEPTED transfers one move-only effect ticket;
 * commit performs the already-prepared write without failure or allocation.
 * Neither operation may retain pointers supplied by the core after returning.
 */
typedef struct ccxml_datamodel_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    scxml_adapter_status (*validate_string_location)(
        void *user, const char *location, size_t location_size,
        const char **out_error);
    scxml_adapter_status (*prepare_assign_string)(
        void *user, const char *location, size_t location_size,
        const char *value, size_t value_size,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
} ccxml_datamodel_adapter_v1;

/** Opaque owner for the built-in synchronous CMeta datamodel adapter. */
typedef struct ccxml_cmeta_datamodel {
    void *impl;
} ccxml_cmeta_datamodel;

/**
 * Borrowed CMeta state configuration. Root schema and mutable state must
 * outlive this owner and every session that uses it. Both limits are positive
 * hard bounds. The root must describe the supplied state as a struct.
 */
typedef struct ccxml_cmeta_datamodel_config_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const cmeta_data_desc *root;
    void *state;
    size_t max_path_depth;
    size_t max_string_bytes;
} ccxml_cmeta_datamodel_config_v1;

ccxml_status ccxml_cmeta_datamodel_init(
    ccxml_cmeta_datamodel *datamodel,
    const ccxml_cmeta_datamodel_config_v1 *config);

/** Static immutable operations; use the datamodel owner as adapter user. */
const ccxml_datamodel_adapter_v1 *ccxml_cmeta_datamodel_adapter(void);

/** Release adapter bookkeeping; borrowed schema and state are untouched. */
void ccxml_cmeta_datamodel_destroy(ccxml_cmeta_datamodel *datamodel);

/**
 * Versioned telephony bridge copied by session initialization.
 *
 * ACCEPTED transfers one move-only ticket with non-NULL commit and discard
 * callbacks. All retained request bytes must be copied before prepare returns.
 * Close is called exactly once after attachment. Destroy remains busy until
 * is_quiescent reports that no callback can reach the borrowed session/user.
 */
typedef struct ccxml_telephony_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    scxml_adapter_status (*prepare_accept)(
        void *user,
        const ccxml_accept_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    void (*close)(void *user);
    bool (*is_quiescent)(void *user);
    /** Optional tail operation, required by programs containing createcall. */
    scxml_adapter_status (*prepare_create_call)(
        void *user,
        const ccxml_create_call_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    /** Optional tail operation, required by programs containing disconnect. */
    scxml_adapter_status (*prepare_disconnect)(
        void *user,
        const ccxml_disconnect_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    /** Optional tail operation, required by programs containing reject. */
    scxml_adapter_status (*prepare_reject)(
        void *user,
        const ccxml_reject_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    /** Optional tail operation, required by programs containing redirect. */
    scxml_adapter_status (*prepare_redirect)(
        void *user,
        const ccxml_redirect_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    /** Optional tail operation, required by programs containing join. */
    scxml_adapter_status (*prepare_join)(
        void *user,
        const ccxml_join_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    /** Optional tail operation, required by programs containing unjoin. */
    scxml_adapter_status (*prepare_unjoin)(
        void *user,
        const ccxml_unjoin_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    /** Optional tail operation, required by programs containing merge. */
    scxml_adapter_status (*prepare_merge)(
        void *user,
        const ccxml_merge_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    /**
     * Optional tail operation, required by createconference programs.
     * ACCEPTED must publish a nonempty borrowed identifier as well as a valid
     * ticket. The identifier remains valid until that ticket is committed or
     * discarded; the core asks its datamodel adapter to copy the identifier
     * before resolving the provider ticket.
     */
    scxml_adapter_status (*prepare_create_conference)(
        void *user,
        const ccxml_create_conference_request *request,
        ccxml_string_view *out_conference_id,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
} ccxml_telephony_adapter_v1;

typedef struct ccxml_session_config {
    /** Borrowed immutable program; it must outlive the session. */
    const ccxml_program *program;
    /** Operations are copied; user remains borrowed through destruction. */
    const ccxml_telephony_adapter_v1 *telephony;
    void *telephony_user;
    /** Required only when the program writes provider-generated identifiers. */
    const ccxml_datamodel_adapter_v1 *datamodel;
    void *datamodel_user;
} ccxml_session_config;

typedef struct ccxml_session {
    void *impl;
} ccxml_session;

ccxml_status ccxml_session_init(
    ccxml_session *session, const ccxml_session_config *config);

/** Dispatch one event and commit or roll back the selected transition. */
ccxml_status ccxml_session_dispatch(
    ccxml_session *session, const ccxml_event *event);

/** Stop accepting events and close the adapter exactly once. */
void ccxml_session_close(ccxml_session *session);

bool ccxml_session_is_terminated(const ccxml_session *session);

/** Close and destroy when the adapter is quiescent; otherwise return BUSY. */
ccxml_status ccxml_session_destroy(ccxml_session *session);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_CCXML_H */
