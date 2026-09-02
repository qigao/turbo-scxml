#ifndef SCXML_ANALYZE_H
#define SCXML_ANALYZE_H

#include "scxml_impl.h"

bool scxml_analyze_checked_add(size_t left, size_t right, size_t *out);
bool scxml_analyze_checked_multiply(size_t left, size_t right, size_t *out);
bool scxml_analyze_attach_event_content(
    scxml_session_impl *session,
    scxml_external_event_metadata_row *row,
    const scxml_content_view *content);
bool scxml_analyze_view_equal_raw(
    turbo_xml_string_view view, const char *raw);
int scxml_analyze_compare_view(
    turbo_xml_string_view left, turbo_xml_string_view right);
int scxml_analyze_compare_name_ref(const void *left, const void *right);
int scxml_analyze_compare_node_ref(const void *left, const void *right);
int scxml_analyze_compare_program_name(const void *left, const void *right);
const scxml_name_ref *scxml_analyze_find_earliest_duplicate(
    scxml_name_ref *names, size_t count);
bool scxml_analyze_bind_current_event_system_values(
    const scxml_expr_system_values *base,
    const scxml_program_name *const *event_names_by_id,
    size_t event_name_count, const cflow_event_view *event,
    scxml_expr_system_values *out);
scxml_status scxml_analyze_fail(
    scxml_build *build, scxml_status status,
    turbo_xml_location location, const char *message);
bool scxml_analyze_is_xml_nmtoken(turbo_xml_string_view token);
bool scxml_analyze_parse_null_in_condition(
    turbo_xml_string_view value, turbo_xml_string_view *out_state);
scxml_element_kind scxml_analyze_element_kind(scxml_syntax_node node);
bool scxml_analyze_is_state_element(scxml_element_kind kind);
scxml_syntax_attribute scxml_analyze_find_attribute(
    scxml_syntax_node node, const char *local_name);
const scxml_cmeta_custom_action_v1 *scxml_analyze_find_custom_action(
    const scxml_build *build, scxml_syntax_node node);
scxml_status scxml_analyze_validate_element_attributes(
    scxml_build *build, scxml_syntax_node node, scxml_element_kind kind);
size_t scxml_analyze_element_child_count(
    scxml_syntax_node node, scxml_element_kind wanted);
bool scxml_analyze_token_next(
    turbo_xml_string_view value, size_t *cursor,
    turbo_xml_string_view *token);
bool scxml_analyze_event_descriptor_matches(
    turbo_xml_string_view descriptor, turbo_xml_string_view event_name);
bool scxml_analyze_completion_token(
    turbo_xml_string_view token, turbo_xml_string_view *state_name);
bool scxml_analyze_completion_descriptor_matches(
    turbo_xml_string_view descriptor, turbo_xml_string_view state_name);
bool scxml_analyze_parse_delay_ms(
    turbo_xml_string_view value, uint64_t *out_ms);
bool scxml_analyze_cmeta_content_kind_is_scalar(cmeta_data_kind kind);
scxml_status scxml_analyze_inspect_inline_content(
    scxml_build *build, scxml_syntax_node content,
    scxml_content_kind *out_kind, size_t *out_size);
scxml_status scxml_analyze_state(
    scxml_build *build, scxml_syntax_node node,
    scxml_element_kind kind, bool is_root, scxml_counts *counts);
scxml_status scxml_analyze_emit_state(
    scxml_build *build, scxml_syntax_node node,
    cflow_machine_state_id parent, bool is_root);
const scxml_name_ref *scxml_analyze_find_name_ref(
    const scxml_name_ref *names, size_t count,
    turbo_xml_string_view name);
cflow_machine_state_id scxml_analyze_node_id(
    const scxml_build *build, scxml_syntax_node node, size_t node_count);
scxml_status scxml_analyze_emit_invocation_declarations(
    scxml_build *build, scxml_syntax_node node, size_t node_count);
scxml_status scxml_analyze_resolve_invocation_events(scxml_build *build);
scxml_status scxml_analyze_collect_transition_events(
    scxml_build *build, scxml_syntax_node node);
void scxml_analyze_collect_reserved_error_events(
    scxml_build *build, turbo_xml_location location);
scxml_status scxml_analyze_build_event_names(
    scxml_build *build, size_t occurrence_count);

#endif
