#ifndef SCXML_EMIT_H
#define SCXML_EMIT_H

#include "scxml_impl.h"

bool scxml_emit_evaluate_session_transition_guard(
    void *user, const cflow_statechart_guard_context *context,
    bool *out_enabled, const char **out_error);
scxml_status scxml_emit_compile_cmeta_value_program(
    scxml_build *build, scxml_syntax_attribute attribute,
    const char *subject, scxml_expr_program *program,
    scxml_expr_value_kind required_kind);
scxml_status scxml_emit_compile_cmeta_owned_string_location(
    scxml_build *build, scxml_syntax_attribute attribute,
    const char *subject, scxml_location *out);
scxml_status scxml_emit_compile_cmeta_payload_token(
    scxml_build *build, turbo_xml_string_view source,
    turbo_xml_location location, const char *subject,
    scxml_expr_program *program);
scxml_status scxml_emit_compile_cmeta_content_expression(
    scxml_build *build, scxml_syntax_attribute expression,
    const char *subject, scxml_content_descriptor *content,
    scxml_expr_program *scalar_program);
scxml_status scxml_emit_data_initializers(
    scxml_build *build, scxml_syntax_node node, bool is_root);
scxml_status scxml_emit_root_scripts(
    scxml_build *build, scxml_syntax_node root);
scxml_status scxml_emit_collect_supplemental_scope(
    scxml_build *build, scxml_syntax_node root);
scxml_status scxml_emit_done_data(
    scxml_build *build, scxml_syntax_node node,
    size_t node_count, cflow_machine_state_id parent);
scxml_status scxml_emit_state_executables(
    scxml_build *build, scxml_syntax_node node, size_t node_count,
    bool is_root);
scxml_status scxml_emit_transitions(
    scxml_build *build, scxml_syntax_node node,
    size_t node_count, size_t synthetic_count);
void scxml_emit_destroy_guard_users(scxml_guard_user *users, size_t count);
void scxml_emit_destroy_assignments(
    scxml_assign_program *assignments, size_t count);
void scxml_emit_destroy_branches(scxml_branch *branches, size_t count);
void scxml_emit_destroy_effects(
    scxml_effect_descriptor *effects, size_t count);
void scxml_emit_destroy_payloads(
    scxml_payload_descriptor *payloads, size_t count);
void scxml_emit_destroy_invocations(
    scxml_invocation_descriptor *invocations, size_t count);
void scxml_emit_destroy_done_data(
    scxml_done_data_descriptor *descriptors, size_t count);
void scxml_emit_destroy_custom_action_arguments(
    scxml_custom_action_argument *arguments, size_t count);
void scxml_emit_free_build(scxml_build *build);
void *scxml_emit_allocate_rows(size_t count, size_t element_size);
void scxml_emit_copy_program_names(
    scxml_program_name *destination, const scxml_name_ref *source,
    size_t count, char **cursor);

#endif
