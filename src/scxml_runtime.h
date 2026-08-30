#ifndef SCXML_RUNTIME_H
#define SCXML_RUNTIME_H

#include "scxml_impl.h"

scxml_delayed_send *scxml_runtime_find_delayed_send_locked(
    scxml_session_impl *session, const char *id,
    size_t id_size, size_t *out_index);
void scxml_runtime_increment_u64(uint64_t *value);
bool scxml_runtime_start_stable_invocations(
    void *user, const cflow_statechart_instance_hook_context *context,
    const char **out_error);
cflow_statechart_stable_transaction_result
scxml_runtime_start_stable_invocations_transaction(
    void *user,
    const cflow_statechart_stable_transaction_context *context,
    const char **out_error);
cflow_statechart_external_preprocess_result
scxml_runtime_preprocess_invocation_external(
    void *user, const cflow_statechart_instance_hook_context *context,
    const cflow_event_view *event, uint64_t source_token,
    const char **out_error);
void scxml_runtime_destroy_event_data_object(
    const cmeta_data_desc *schema, void *object);
void scxml_runtime_clear_current_event_metadata(scxml_session_impl *session);
bool scxml_runtime_observe_event(
    void *user, const cflow_statechart_instance_hook_context *context,
    const cflow_statechart_observed_event *event,
    const char **out_error);
bool scxml_runtime_metadata_field_valid(const char *data, size_t size);
scxml_external_event_metadata_row *scxml_runtime_reserve_event_metadata(
    scxml_session_impl *session, const scxml_event_metadata *metadata,
    uint64_t *out_token);
void scxml_runtime_release_event_metadata(void *user);
bool scxml_runtime_execute_block(
    void *user, const cflow_statechart_executable_context *context,
    const char **out_error);
bool scxml_runtime_execute_session_block(
    void *user, const cflow_statechart_executable_context *context,
    const char **out_error);
bool scxml_runtime_scalar_value_to_text(
    const scxml_expr_value *value, char *storage,
    size_t capacity, const char **out_data, size_t *out_size);
bool scxml_runtime_payload_value_from_cmeta(
    const scxml_expr_value *source,
    scxml_payload_value *destination);
bool scxml_runtime_materialize_content_descriptor(
    const scxml_content_descriptor *descriptor, const void *state,
    scxml_content_view *out);
bool scxml_runtime_payload_v2_to_v3(
    scxml_session_impl *session,
    const scxml_payload_view *source,
    scxml_payload_view_v3 *out);
bool scxml_runtime_copy_event_data_object(
    const cmeta_data_desc *schema, void *destination,
    const void *source);

#endif
