#ifndef TURBO_VOICEXML_CMETA_EXPR_H
#define TURBO_VOICEXML_CMETA_EXPR_H

#include "cmeta_scope.h"

#include <voicexml/cmeta.h>

#include <stdbool.h>
#include <stddef.h>

#define VXML_CMETA_EXPR_DIAGNOSTIC_CAPACITY 192u

typedef struct vxml_cmeta_expr_limits {
    size_t max_source_bytes;
    size_t max_instructions;
    size_t max_operands;
    size_t max_expression_depth;
    size_t max_path_depth;
    size_t max_literal_bytes;
    size_t max_string_bytes;
} vxml_cmeta_expr_limits;

typedef struct vxml_cmeta_expr_diagnostic {
    vxml_status status;
    size_t byte_offset;
    char message[VXML_CMETA_EXPR_DIAGNOSTIC_CAPACITY];
} vxml_cmeta_expr_diagnostic;

typedef struct vxml_cmeta_expr_program {
    void *impl;
} vxml_cmeta_expr_program;

/* Caller-owned bounded storage used by one evaluation. The evaluator resets
 * used to zero and never allocates from the heap. bytes may be NULL only when
 * capacity is zero. */
typedef struct vxml_cmeta_expr_scratch {
    unsigned char *bytes;
    size_t capacity;
    size_t used;
} vxml_cmeta_expr_scratch;

/* Entries are ordered from the innermost lexical scope to the outermost.
 * Each schema, its slots, and every reachable descriptor remain borrowed,
 * immutable, and alive until the compiled expression is destroyed. SIZE_MAX
 * is reserved for the application root and is not a valid lexical scope ID. */
typedef struct vxml_cmeta_expr_compile_scope {
    size_t scope_id;
    const cmeta_scope_schema *schema;
} vxml_cmeta_expr_compile_scope;

/* Declared bits are independent from the bound bits in view. An undeclared
 * inner candidate does not hide a declared outer candidate. A declared but
 * unbound candidate resolves to the typed undefined value state. */
typedef struct vxml_cmeta_expr_runtime_scope {
    size_t scope_id;
    const cmeta_scope_view *view;
    const unsigned char *declared;
    size_t declared_count;
} vxml_cmeta_expr_runtime_scope;

/* The application root's top-level fields are always declared. root_bound
 * supplies their independent bound states in descriptor field order. All
 * pointers are borrowed for the duration of one evaluation call. */
typedef struct vxml_cmeta_expr_runtime {
    const void *root;
    const unsigned char *root_bound;
    size_t root_bound_count;
    const vxml_cmeta_expr_runtime_scope *scopes;
    size_t scope_count;
} vxml_cmeta_expr_runtime;

bool vxml_cmeta_expr_limits_valid(const vxml_cmeta_expr_limits *limits);

/* Compilation initializes out to empty on every failure. The application
 * root descriptor, every descriptor reachable from it, and all compile-scope
 * schemas/descriptors are borrowed and must remain immutable and alive until
 * the compiled program is destroyed. Destroy a prior program before reusing
 * its handle. Source, literal, and candidate storage is program-owned. */
vxml_status vxml_cmeta_expr_compile_condition(
    vxml_cmeta_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    const vxml_cmeta_expr_compile_scope *scopes, size_t scope_count,
    const vxml_cmeta_expr_limits *limits,
    vxml_cmeta_expr_diagnostic *diagnostic);

vxml_status vxml_cmeta_expr_compile_value(
    vxml_cmeta_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    const vxml_cmeta_expr_compile_scope *scopes, size_t scope_count,
    const vxml_cmeta_expr_limits *limits,
    vxml_cmeta_expr_diagnostic *diagnostic);

vxml_cmeta_value_kind vxml_cmeta_expr_program_value_kind(
    const vxml_cmeta_expr_program *program);

/* Returns the capacity required to evaluate every runtime path of program,
 * or zero for an empty program. */
size_t vxml_cmeta_expr_program_scratch_bytes(
    const vxml_cmeta_expr_program *program);

/* scratch must have at least vxml_cmeta_expr_program_scratch_bytes(program)
 * bytes of capacity. Runtime CMeta strings are copied into scratch before
 * entering QueryVM registers, so concurrently live values never retain a
 * provider-owned read span. A returned string view remains valid until the
 * earlier of program destruction, scratch byte mutation, or another
 * evaluation using the same scratch. All other result kinds are by value. */
vxml_status vxml_cmeta_expr_evaluate(
    const vxml_cmeta_expr_program *program,
    const vxml_cmeta_expr_runtime *runtime,
    vxml_cmeta_expr_scratch *scratch,
    vxml_cmeta_value_view *out_value,
    vxml_cmeta_expr_diagnostic *diagnostic);

void vxml_cmeta_expr_program_destroy(vxml_cmeta_expr_program *program);

#endif /* TURBO_VOICEXML_CMETA_EXPR_H */
