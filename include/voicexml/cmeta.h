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

#define VXML_CMETA_COLLECT_CAP_SRGS_XML UINT64_C(1)

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
