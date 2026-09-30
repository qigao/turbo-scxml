#include "voicexml_cmeta_fixture.h"

#include <string.h>

int main(void) {
    static const char document[] =
        "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
        "datamodel='cmeta'><var name='value' expr='7'/><form><block>"
        "<assign name='value' expr='value + 1'/><exit namelist='value'/>"
        "</block></form></vxml>";
    const cmeta_data_desc *root =
        turboscxml_install_cmeta_root_descriptor();
    const cmeta_type_desc *layout_type =
        turboscxml_install_cmeta_layout_value_type();
    const turboscxml_install_cmeta_root initial_root = {3};
    const vxml_cmeta_compile_options_v1 compile_options = {
        .abi_version = VXML_CMETA_COMPILE_OPTIONS_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_compile_options_v1),
        .root = root,
        .max_expression_bytes = 1024u,
        .max_expression_instructions = 256u,
        .max_expression_operands = 32u,
        .max_expression_depth = 16u,
        .max_path_depth = 8u,
        .max_literal_bytes = 1024u,
        .max_string_bytes = 1024u,
        .max_scope_slots = 32u,
        .max_scope_storage_bytes = 4096u,
        .max_conditional_depth = 8u
    };
    const vxml_cmeta_session_options_v1 session_options = {
        .abi_version = VXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_session_options_v1),
        .initial_root = &initial_root,
        .max_transaction_bytes = 65536u,
        .max_execution_steps = 128u,
        .max_event_counters = 8u,
        .max_event_name_bytes = 64u,
        .max_event_dispatch_depth = 8u
    };
    vxml_program program = {0};
    vxml_session session = {0};
    vxml_cmeta_collect_ticket_v1 collect_ticket = {0};
    vxml_cmeta_collect_request_v1 collect_request = {0};
    vxml_cmeta_collect_completion_v1 collect_completion = {
        .abi_version = VXML_CMETA_COLLECT_COMPLETION_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_collect_completion_v1)};
    vxml_cmeta_collect_result_slot_v1 collect_slot = {0};
    vxml_cmeta_collect_completion_v2 collect_completion_v2 = {
        .abi_version = VXML_CMETA_COLLECT_COMPLETION_ABI_V2,
        .struct_size = sizeof(vxml_cmeta_collect_completion_v2),
        .slots = &collect_slot,
        .slot_count = 1u};
    vxml_cmeta_collect_adapter_v1 collect_adapter = {
        .abi_version = VXML_CMETA_COLLECT_ADAPTER_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_collect_adapter_v1)};
    vxml_cmeta_data_resource_v1 data_resource = {0};
    vxml_cmeta_data_resource_adapter_v1 data_adapter = {
        .abi_version = VXML_CMETA_DATA_RESOURCE_ADAPTER_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_data_resource_adapter_v1)};
    vxml_cmeta_prompt_view_v1 prompt_view = {
        .abi_version = VXML_CMETA_PROMPT_VIEW_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_prompt_view_v1)};
    vxml_cmeta_prompt_media_segment_v1 prompt_media_segment = {0};
    vxml_cmeta_prompt_media_fallback_v1 prompt_media_fallback = {
        .audio_segment_index = 1u,
        .first_fallback_segment = 2u,
        .fallback_segment_count = 1u};
    vxml_cmeta_prompt_media_ticket_v1 prompt_media_ticket = {0};
    vxml_cmeta_prompt_media_request_v1 prompt_media_request = {
        .abi_version = VXML_CMETA_PROMPT_MEDIA_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_prompt_media_request_v1)};
    vxml_cmeta_prompt_media_batch_request_v1 prompt_media_batch = {
        .abi_version = VXML_CMETA_PROMPT_MEDIA_BATCH_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_prompt_media_batch_request_v1)};
    vxml_cmeta_prompt_media_adapter_v1 prompt_media_adapter = {
        .abi_version = VXML_CMETA_PROMPT_MEDIA_ADAPTER_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_prompt_media_adapter_v1)};
    vxml_cmeta_prompt_mark_view_v1 prompt_mark = {
        .abi_version = VXML_CMETA_PROMPT_MARK_VIEW_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_prompt_mark_view_v1),
        .segment_index = SIZE_MAX};
    vxml_cmeta_prompt_media_completion_v1 prompt_media_completion = {
        .abi_version = VXML_CMETA_PROMPT_MEDIA_COMPLETION_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_prompt_media_completion_v1),
        .outcome = VXML_CMETA_PROMPT_MEDIA_OUTCOME_FAILED,
        .failure = VXML_CMETA_PROMPT_MEDIA_FAILURE_BADFETCH};
    vxml_cmeta_value_view read_value = {0};
    vxml_cmeta_terminal_kind terminal_kind = VXML_CMETA_TERMINAL_NONE;
    vxml_cmeta_exit_kind exit_kind = VXML_CMETA_EXIT_EMPTY;
    vxml_cmeta_name_view exit_name = {0};
    vxml_cmeta_value_view exit_value = {0};
    vxml_status (*raise_event)(
        vxml_session *, const char *, size_t) =
        vxml_session_cmeta_raise;
    vxml_status (*raise_noinput)(vxml_session *) =
        vxml_session_cmeta_noinput;
    vxml_status (*raise_nomatch)(vxml_session *) =
        vxml_session_cmeta_nomatch;
    vxml_status (*take_reprompt)(vxml_session *, bool *) =
        vxml_session_cmeta_take_reprompt;
    vxml_status (*query_prompt)(
        const vxml_session *, vxml_cmeta_prompt_view_v1 *) =
        vxml_session_cmeta_prompt;
    vxml_cmeta_prompt_mark_result (*report_mark)(
        vxml_session *, uint64_t, size_t) =
        vxml_session_cmeta_prompt_media_mark;
    vxml_status (*query_mark)(
        const vxml_session *, vxml_cmeta_prompt_mark_view_v1 *) =
        vxml_session_cmeta_prompt_media_last_mark;
    vxml_status (*query_terminal)(
        const vxml_session *, vxml_cmeta_terminal_kind *) =
        vxml_session_cmeta_terminal_kind;
    vxml_status (*query_terminal_event)(
        const vxml_session *, vxml_cmeta_name_view *) =
        vxml_session_cmeta_terminal_event;
    int result = 1;

    if (raise_event == NULL || query_prompt == NULL ||
        report_mark == NULL || query_mark == NULL ||
        query_terminal == NULL || query_terminal_event == NULL ||
        VXML_CMETA_PROMPT_MEDIA_CAP_MARK == 0u ||
        VXML_CMETA_PROMPT_MEDIA_CAP_AUDIO_FALLBACK == 0u ||
        VXML_CMETA_PROMPT_MEDIA_MARK == 0 ||
        prompt_mark.abi_version != VXML_CMETA_PROMPT_MARK_VIEW_ABI_V1 ||
        prompt_mark.segment_index != SIZE_MAX ||
        prompt_view.abi_version != VXML_CMETA_PROMPT_VIEW_ABI_V1 ||
        prompt_media_request.abi_version !=
            VXML_CMETA_PROMPT_MEDIA_REQUEST_ABI_V1 ||
        prompt_media_adapter.abi_version !=
            VXML_CMETA_PROMPT_MEDIA_ADAPTER_ABI_V1 ||
        prompt_media_batch.abi_version !=
            VXML_CMETA_PROMPT_MEDIA_BATCH_REQUEST_ABI_V1 ||
        prompt_media_fallback.audio_segment_index != 1u ||
        prompt_media_fallback.first_fallback_segment != 2u ||
        prompt_media_fallback.fallback_segment_count != 1u ||
        prompt_media_completion.failure !=
            VXML_CMETA_PROMPT_MEDIA_FAILURE_BADFETCH ||
        VXML_CMETA_PROMPT_MEDIA_FAILURE_UNSUPPORTED_FORMAT == 0 ||
        VXML_CMETA_PROMPT_MEDIA_FAILURE_NORESOURCE == 0 ||
        prompt_media_segment.kind != 0 ||
        prompt_media_ticket.commit != NULL)
        return 6;

    if (data_adapter.abi_version !=
            VXML_CMETA_DATA_RESOURCE_ADAPTER_ABI_V1 ||
        data_resource.format != 0)
        return 5;
    if (collect_adapter.abi_version !=
            VXML_CMETA_COLLECT_ADAPTER_ABI_V1 ||
        collect_ticket.commit != NULL ||
        collect_request.abi_version != 0u ||
        collect_request.has_timeout ||
        collect_request.timeout_us != UINT64_C(0) ||
        collect_completion.abi_version !=
            VXML_CMETA_COLLECT_COMPLETION_ABI_V1 ||
        collect_completion_v2.abi_version !=
            VXML_CMETA_COLLECT_COMPLETION_ABI_V2 ||
        collect_completion_v2.slot_count != 1u ||
        VXML_CMETA_COLLECT_INGRESS_ACCEPTED != 0)
        return 6;
    if (layout_type ==
            turboscxml_install_cmeta_peer_value_descriptor()->storage_type ||
        !cmeta_type_equal(
            layout_type,
            turboscxml_install_cmeta_peer_value_descriptor()->storage_type)) {
        result = 10;
        goto cleanup;
    }
    if (vxml_compile_cmeta(
            document, sizeof(document) - 1u, NULL, &compile_options,
            &program, NULL) != VXML_OK) {
        result = 20;
        goto cleanup;
    }
    if (vxml_session_init_cmeta(
            &session, &program, &session_options) != VXML_OK ||
        vxml_session_start(&session) != VXML_OK ||
        vxml_session_get_state(&session) != VXML_SESSION_EXITED) {
        result = 30;
        goto cleanup;
    }
    if (vxml_session_cmeta_read(
            &session, "value", 5u, &read_value) != VXML_OK ||
        read_value.kind != VXML_CMETA_VALUE_SINT ||
        read_value.data.sint != 8) {
        result = 40;
        goto cleanup;
    }
    if (vxml_session_cmeta_terminal_kind(
            &session, &terminal_kind) != VXML_OK ||
        terminal_kind != VXML_CMETA_TERMINAL_EXIT ||
        vxml_session_cmeta_exit_kind(&session, &exit_kind) != VXML_OK ||
        exit_kind != VXML_CMETA_EXIT_NAMELIST ||
        vxml_session_cmeta_exit_count(&session) != 1u ||
        vxml_session_cmeta_exit_at(
            &session, 0u, &exit_name, &exit_value) != VXML_OK ||
        exit_name.size != 5u || memcmp(exit_name.data, "value", 5u) != 0 ||
        exit_value.kind != VXML_CMETA_VALUE_SINT ||
        exit_value.data.sint != 8) {
        result = 50;
        goto cleanup;
    }
    if (vxml_session_close(&session) != VXML_OK) {
        result = 60;
        goto cleanup;
    }
    result = 0;

cleanup:
    vxml_session_destroy(&session);
    vxml_program_destroy(&program);
    return result;
}
