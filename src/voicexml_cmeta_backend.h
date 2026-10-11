#ifndef TURBO_VOICEXML_CMETA_BACKEND_H
#define TURBO_VOICEXML_CMETA_BACKEND_H

#include "voicexml_cmeta_expr.h"

typedef enum vxml_cmeta_code_kind {
    VXML_CMETA_CODE_VALUE = 0,
    VXML_CMETA_CODE_CONDITION,
    VXML_CMETA_CODE_SCRIPT
} vxml_cmeta_code_kind;
struct vxml_session_document_context;
struct vxml_cmeta_program_data;

/* Explicit write authority supplied only by the Session owner. The QueryVM
 * evaluator's read-only runtime carrier remains unchanged. Scope views expose
 * mutable slot storage/bound bits; declarations and schemas remain read-only. */
typedef struct vxml_cmeta_backend_runtime {
    void *root;
    unsigned char *root_bound;
    size_t root_bound_count;
    const vxml_cmeta_expr_runtime_scope *scopes;
    size_t scope_count;
    const void *session_user;
    const struct vxml_session_document_context *document;
    /* Optional static Event name; survives rollback without borrowing engine data. */
    vxml_cmeta_name_view *failure_event;
    /* Borrowed immutable scope/form ownership; never a second state owner. */
    const struct vxml_cmeta_program_data *program;
} vxml_cmeta_backend_runtime;

/* Private composition seam. The compiler copies this table; user is borrowed
 * only during compilation. Each expression owns its immutable backend handle.
 * No interpreter or mutable state is stored in a shared Program. */
typedef struct vxml_cmeta_expression_backend {
    vxml_status (*compile)(
        const void *user, const char *source, size_t source_size, vxml_cmeta_code_kind kind,
        const cmeta_data_desc *root, void **out,
        vxml_cmeta_expr_diagnostic *diagnostic);
    void (*destroy)(void *expression);
    size_t (*scratch_bytes)(const void *expression);
    /* The supplied runtime is exclusively borrowed on the Session owner.
     * Writes publish atomically to that runtime only after evaluation and all
     * conversions succeed. A staged runtime remains under the FIA transaction.
     * String results live in scratch, never in a destroyed engine context. */
    vxml_status (*evaluate)(
        const void *expression, vxml_cmeta_value_kind expected,
        const vxml_cmeta_backend_runtime *runtime,
        vxml_cmeta_expr_scratch *scratch, vxml_cmeta_value_view *out);
    vxml_status (*compile_external_script)(
        const void *user, const char *reference, size_t reference_size,
        bool dynamic, const char *charset, size_t charset_size,
        const cmeta_data_desc *root, void **out,
        vxml_cmeta_expr_diagnostic *diagnostic);
} vxml_cmeta_expression_backend;

/* Internal until QuickJS script handoffs, media prepare rollback and installed
 * consumers qualify the complete combined profile. Default CMeta is unchanged. */
vxml_status vxml_compile_cmeta_backend(
    const void *bytes, size_t size, const vxml_limits *limits,
    const vxml_cmeta_compile_options_v1 *options,
    const vxml_cmeta_expression_backend *backend, const void *backend_user,
    vxml_program *out, vxml_diagnostic *diagnostic);

#endif
