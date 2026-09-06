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
} vxml_cmeta_compile_options_v1;

typedef struct vxml_cmeta_session_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const void *initial_root;
    const vxml_cmeta_name_view *initially_undefined;
    size_t initially_undefined_count;
    size_t max_transaction_bytes;
    size_t max_execution_steps;
} vxml_cmeta_session_options_v1;

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
