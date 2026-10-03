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
        .max_conditional_depth = 8u,
        .max_subdialogs = 1u,
        .max_subdialog_uri_bytes = 128u,
        .max_prompt_foreach = 2u,
        .max_prompt_foreach_items = 4u,
        .max_prompt_foreach_snapshot_bytes = 1024u,
        .max_prompt_expanded_segments = 16u,
        .max_prompt_foreach_depth = 2u,
        .max_data_namelist_fields = 4u,
        .max_data_request_value_bytes = 256u
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
    vxml_cmeta_subdialog_param_v1 subdialog_param = {
        .source = VXML_CMETA_SUBDIALOG_PARAM_TYPED};
    vxml_cmeta_record_request_v1 record_request = {
        .abi_version = VXML_CMETA_RECORD_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_record_request_v1)};
    vxml_cmeta_record_ticket_v1 record_ticket = {0};
    vxml_cmeta_record_adapter_v1 record_adapter = {
        .abi_version = VXML_CMETA_RECORD_ADAPTER_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_record_adapter_v1)};
    vxml_cmeta_transfer_request_v1 transfer_request = {
        .abi_version = VXML_CMETA_TRANSFER_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_transfer_request_v1)};
    vxml_cmeta_transfer_ticket_v1 transfer_ticket = {0};
    vxml_cmeta_transfer_adapter_v1 transfer_adapter = {
        .abi_version = VXML_CMETA_TRANSFER_ADAPTER_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_transfer_adapter_v1)};
    vxml_cmeta_transfer_completion_v1 transfer_completion = {
        .abi_version = VXML_CMETA_TRANSFER_COMPLETION_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_transfer_completion_v1),
        .kind = VXML_CMETA_TRANSFER_COMPLETION_RESULT,
        .result = VXML_CMETA_TRANSFER_RESULT_UNKNOWN};
    vxml_cmeta_recording_lease_v1 recording_lease = {0};
    vxml_cmeta_record_completion_v1 record_completion = {
        .abi_version = VXML_CMETA_RECORD_COMPLETION_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_record_completion_v1),
        .outcome = VXML_CMETA_RECORD_OUTCOME_NOINPUT};
    vxml_cmeta_record_result_view_v1 record_result = {
        .abi_version = VXML_CMETA_RECORD_RESULT_VIEW_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_record_result_view_v1)};
    vxml_cmeta_subdialog_request_v1 subdialog_request = {
        .abi_version = VXML_CMETA_SUBDIALOG_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_subdialog_request_v1)};
    vxml_cmeta_child_entry_v1 child_entry = {
        .abi_version = VXML_CMETA_CHILD_ENTRY_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_child_entry_v1)};
    vxml_cmeta_subdialog_result_entry_v1 subdialog_result = {0};
    vxml_cmeta_subdialog_completion_v1 subdialog_completion = {
        .abi_version = VXML_CMETA_SUBDIALOG_COMPLETION_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_subdialog_completion_v1),
        .kind = VXML_CMETA_SUBDIALOG_RETURN_EVENT,
        .global_exit_kind = VXML_CMETA_SUBDIALOG_GLOBAL_NATURAL};
    vxml_cmeta_subdialog_ticket_v1 subdialog_ticket = {0};
    vxml_cmeta_subdialog_adapter_v1 subdialog_adapter = {
        .abi_version = VXML_CMETA_SUBDIALOG_ADAPTER_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_subdialog_adapter_v1)};
    vxml_cmeta_collect_request_v1 collect_request = {0};
    vxml_cmeta_collect_request_v2 collect_request_v2 = {
        .abi_version = VXML_CMETA_COLLECT_REQUEST_ABI_V2,
        .struct_size = sizeof(vxml_cmeta_collect_request_v2)};
    vxml_cmeta_collect_completion_v1 collect_completion = {
        .abi_version = VXML_CMETA_COLLECT_COMPLETION_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_collect_completion_v1)};
    vxml_cmeta_collect_result_slot_v1 collect_slot = {0};
    vxml_cmeta_menu_choice_v1 menu_choice = {0};
    vxml_cmeta_menu_collect_request_v1 menu_request = {
        .abi_version = VXML_CMETA_MENU_COLLECT_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_menu_collect_request_v1)};
    vxml_cmeta_menu_speech_policy_v1 menu_policy = {0};
    vxml_cmeta_menu_grammar_ref_v1 menu_grammar = {0};
    vxml_cmeta_menu_collect_request_v2 menu_request_v2 = {
        .abi_version = VXML_CMETA_MENU_COLLECT_REQUEST_ABI_V2,
        .struct_size = sizeof(vxml_cmeta_menu_collect_request_v2)};
    vxml_cmeta_menu_completion_v1 menu_completion = {
        .abi_version = VXML_CMETA_MENU_COMPLETION_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_menu_completion_v1)};
    vxml_cmeta_initial_collect_request_v1 initial_request = {
        .abi_version = VXML_CMETA_INITIAL_COLLECT_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_initial_collect_request_v1)};
    vxml_cmeta_collect_completion_v2 collect_completion_v2 = {
        .abi_version = VXML_CMETA_COLLECT_COMPLETION_ABI_V2,
        .struct_size = sizeof(vxml_cmeta_collect_completion_v2),
        .slots = &collect_slot,
        .slot_count = 1u};
    vxml_cmeta_collect_completion_v3 collect_completion_v3 = {
        .abi_version = VXML_CMETA_COLLECT_COMPLETION_ABI_V3,
        .struct_size = sizeof(vxml_cmeta_collect_completion_v3),
        .slots = &collect_slot,
        .slot_count = 1u};
    vxml_cmeta_collect_utterance_result_view_v1 collect_utterance = {
        .abi_version = VXML_CMETA_COLLECT_UTTERANCE_RESULT_VIEW_ABI_V1,
        .struct_size =
            sizeof(vxml_cmeta_collect_utterance_result_view_v1)};
    vxml_cmeta_recording_ref_view_v1 recording_shadow = {
        .abi_version = VXML_CMETA_RECORDING_REF_VIEW_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_recording_ref_view_v1)};
    vxml_cmeta_collect_adapter_v1 collect_adapter = {
        .abi_version = VXML_CMETA_COLLECT_ADAPTER_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_collect_adapter_v1)};
    vxml_cmeta_data_resource_v1 data_resource = {0};
    vxml_cmeta_data_request_v2 data_request_v2 =
        VXML_CMETA_DATA_REQUEST_V2_INIT;
    vxml_cmeta_data_field_v1 data_field_v1 = {0};
    vxml_cmeta_data_request_v3 data_request_v3 =
        VXML_CMETA_DATA_REQUEST_V3_INIT;
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
    vxml_cmeta_prompt_mark_progress_v2 prompt_mark_v2 = {
        .abi_version = VXML_CMETA_PROMPT_MARK_PROGRESS_ABI_V2,
        .struct_size = sizeof(vxml_cmeta_prompt_mark_progress_v2)};
    vxml_cmeta_prompt_barge_v2 prompt_barge_v2 = {
        .abi_version = VXML_CMETA_PROMPT_BARGE_ABI_V2,
        .struct_size = sizeof(vxml_cmeta_prompt_barge_v2)};
    vxml_cmeta_prompt_media_completion_v1 prompt_media_completion = {
        .abi_version = VXML_CMETA_PROMPT_MEDIA_COMPLETION_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_prompt_media_completion_v1),
        .outcome = VXML_CMETA_PROMPT_MEDIA_OUTCOME_FAILED,
        .failure = VXML_CMETA_PROMPT_MEDIA_FAILURE_BADFETCH};
    vxml_cmeta_prompt_media_completion_v2 prompt_media_completion_v2 = {
        .abi_version = VXML_CMETA_PROMPT_MEDIA_COMPLETION_ABI_V2,
        .struct_size = sizeof(vxml_cmeta_prompt_media_completion_v2),
        .outcome = VXML_CMETA_PROMPT_MEDIA_OUTCOME_COMPLETED};
    vxml_navigation_request_v1 navigation_request = {
        .abi_version = VXML_NAVIGATION_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_navigation_request_v1)};
    vxml_status (*query_navigation)(
        const vxml_session *, vxml_navigation_request_v1 *) =
        vxml_session_navigation_request;
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
    vxml_cmeta_prompt_mark_result (*report_mark_v2)(
        vxml_session *, const vxml_cmeta_prompt_mark_progress_v2 *) =
        vxml_session_cmeta_prompt_media_mark_v2;
    vxml_cmeta_prompt_media_ingress_result (*complete_prompt_v2)(
        vxml_session *,
        const vxml_cmeta_prompt_media_completion_v2 *) =
        vxml_session_cmeta_prompt_media_try_complete_v2;
    vxml_cmeta_prompt_barge_result (*barge_prompt_v2)(
        vxml_session *, const vxml_cmeta_prompt_barge_v2 *) =
        vxml_session_cmeta_prompt_media_barge_in_v2;
    vxml_status (*query_mark_shadow)(
        const vxml_session *, const char *, size_t,
        vxml_cmeta_value_view *) =
        vxml_session_cmeta_mark_shadow_value;
    vxml_status (*query_mark)(
        const vxml_session *, vxml_cmeta_prompt_mark_view_v1 *) =
        vxml_session_cmeta_prompt_media_last_mark;
    vxml_status (*query_terminal)(
        const vxml_session *, vxml_cmeta_terminal_kind *) =
        vxml_session_cmeta_terminal_kind;
    vxml_status (*query_terminal_event)(
        const vxml_session *, vxml_cmeta_name_view *) =
        vxml_session_cmeta_terminal_event;
    vxml_status (*start_child)(
        vxml_session *, const vxml_cmeta_child_entry_v1 *) =
        vxml_session_cmeta_start_child;
    vxml_status (*query_record)(
        const vxml_session *, vxml_cmeta_record_request_v1 *) =
        vxml_session_cmeta_record_request;
    vxml_status (*prepare_record)(
        vxml_session *, const char **) =
        vxml_session_cmeta_record_prepare;
    vxml_status (*commit_record)(vxml_session *) =
        vxml_session_cmeta_record_commit;
    vxml_status (*discard_record)(vxml_session *) =
        vxml_session_cmeta_record_discard;
    vxml_cmeta_record_ingress_result (*complete_record)(
        vxml_session *, const vxml_cmeta_record_completion_v1 *) =
        vxml_session_cmeta_record_try_complete;
    vxml_status (*run_record_completion)(
        vxml_session *, bool *) =
        vxml_session_cmeta_record_run_ready;
    vxml_status (*query_record_result)(
        const vxml_session *, const char *, size_t,
        vxml_cmeta_record_result_view_v1 *) =
        vxml_session_cmeta_record_result;
    vxml_status (*query_transfer)(
        const vxml_session *, vxml_cmeta_transfer_request_v1 *) =
        vxml_session_cmeta_transfer_request;
    vxml_status (*prepare_transfer)(
        vxml_session *, const char **) =
        vxml_session_cmeta_transfer_prepare;
    vxml_status (*commit_transfer)(vxml_session *) =
        vxml_session_cmeta_transfer_commit;
    vxml_status (*discard_transfer)(vxml_session *) =
        vxml_session_cmeta_transfer_discard;
    vxml_cmeta_transfer_ingress_result (*complete_transfer)(
        vxml_session *, const vxml_cmeta_transfer_completion_v1 *) =
        vxml_session_cmeta_transfer_try_complete;
    vxml_status (*run_transfer_completion)(
        vxml_session *, bool *) =
        vxml_session_cmeta_transfer_run_ready;
    vxml_status (*prepare_subdialog)(
        vxml_session *, const char **) =
        vxml_session_cmeta_subdialog_prepare;
    vxml_status (*commit_subdialog)(vxml_session *) =
        vxml_session_cmeta_subdialog_commit;
    vxml_status (*discard_subdialog)(vxml_session *) =
        vxml_session_cmeta_subdialog_discard;
    vxml_cmeta_subdialog_ingress_result (*complete_subdialog)(
        vxml_session *,
        const vxml_cmeta_subdialog_completion_v1 *) =
        vxml_session_cmeta_subdialog_try_complete;
    vxml_status (*run_subdialog_completion)(
        vxml_session *, bool *) =
        vxml_session_cmeta_subdialog_run_ready;
    vxml_status (*query_collect_v2)(
        const vxml_session *, vxml_cmeta_collect_request_v2 *) =
        vxml_session_cmeta_collect_request_v2;
    vxml_cmeta_collect_ingress_result (*complete_collect_v3)(
        vxml_session *, const vxml_cmeta_collect_completion_v3 *) =
        vxml_session_cmeta_collect_try_complete_v3;
    vxml_status (*query_collect_utterance)(
        const vxml_session *,
        vxml_cmeta_collect_utterance_result_view_v1 *) =
        vxml_session_cmeta_collect_utterance_result;
    vxml_status (*query_recording_shadow_value)(
        const vxml_session *, const char *, size_t,
        vxml_cmeta_value_view *) =
        vxml_session_cmeta_recording_shadow_value;
    vxml_status (*query_recording_shadow)(
        const vxml_session *, const char *, size_t,
        vxml_cmeta_recording_ref_view_v1 *) =
        vxml_session_cmeta_recording_shadow;
    vxml_status (*query_menu)(
        const vxml_session *, vxml_cmeta_menu_collect_request_v1 *) =
        vxml_session_cmeta_menu_collect_request;
    vxml_status (*query_menu_v2)(
        const vxml_session *, vxml_cmeta_menu_collect_request_v2 *) =
        vxml_session_cmeta_menu_collect_request_v2;
    vxml_status (*query_initial)(
        const vxml_session *, vxml_cmeta_initial_collect_request_v1 *) =
        vxml_session_cmeta_initial_collect_request;
    vxml_cmeta_collect_ingress_result (*complete_menu)(
        vxml_session *, const vxml_cmeta_menu_completion_v1 *) =
        vxml_session_cmeta_menu_try_complete;
    int result = 1;

    if (query_navigation == NULL ||
        navigation_request.abi_version != VXML_NAVIGATION_REQUEST_ABI_V1 ||
        raise_event == NULL || query_prompt == NULL ||
        report_mark == NULL || report_mark_v2 == NULL ||
        complete_prompt_v2 == NULL || barge_prompt_v2 == NULL ||
        query_mark_shadow == NULL || query_mark == NULL ||
        query_terminal == NULL || query_terminal_event == NULL ||
        start_child == NULL ||
        child_entry.abi_version != VXML_CMETA_CHILD_ENTRY_ABI_V1 ||
        query_record == NULL || prepare_record == NULL ||
        commit_record == NULL || discard_record == NULL ||
        complete_record == NULL || run_record_completion == NULL ||
        query_record_result == NULL ||
        record_request.abi_version != VXML_CMETA_RECORD_REQUEST_ABI_V1 ||
        record_adapter.abi_version != VXML_CMETA_RECORD_ADAPTER_ABI_V1 ||
        record_completion.abi_version !=
            VXML_CMETA_RECORD_COMPLETION_ABI_V1 ||
        record_completion.outcome != VXML_CMETA_RECORD_OUTCOME_NOINPUT ||
        record_result.abi_version !=
            VXML_CMETA_RECORD_RESULT_VIEW_ABI_V1 ||
        recording_lease.data != NULL ||
        VXML_CMETA_RECORD_INGRESS_ACCEPTED != 0 ||
        record_ticket.commit != NULL ||
        VXML_CMETA_RECORD_CAP_BEEP == 0u ||
        VXML_CMETA_RECORD_CAP_DTMF_TERM == 0u ||
        query_transfer == NULL || prepare_transfer == NULL ||
        commit_transfer == NULL || discard_transfer == NULL ||
        complete_transfer == NULL || run_transfer_completion == NULL ||
        transfer_request.abi_version !=
            VXML_CMETA_TRANSFER_REQUEST_ABI_V1 ||
        transfer_adapter.abi_version !=
            VXML_CMETA_TRANSFER_ADAPTER_ABI_V1 ||
        transfer_completion.abi_version !=
            VXML_CMETA_TRANSFER_COMPLETION_ABI_V1 ||
        transfer_completion.kind !=
            VXML_CMETA_TRANSFER_COMPLETION_RESULT ||
        transfer_completion.result !=
            VXML_CMETA_TRANSFER_RESULT_UNKNOWN ||
        VXML_CMETA_TRANSFER_INGRESS_ACCEPTED != 0 ||
        transfer_ticket.commit != NULL ||
        VXML_CMETA_TRANSFER_CAP_BLIND == 0u ||
        VXML_CMETA_TRANSFER_CAP_BRIDGE == 0u ||
        VXML_CMETA_TRANSFER_CAP_CONSULTATION == 0u ||
        VXML_CMETA_TRANSFER_CONSULTATION == 0 ||
        prepare_subdialog == NULL || commit_subdialog == NULL ||
        discard_subdialog == NULL ||
        complete_subdialog == NULL || run_subdialog_completion == NULL ||
        subdialog_request.abi_version !=
            VXML_CMETA_SUBDIALOG_REQUEST_ABI_V1 ||
        subdialog_adapter.abi_version !=
            VXML_CMETA_SUBDIALOG_ADAPTER_ABI_V1 ||
        subdialog_completion.abi_version !=
            VXML_CMETA_SUBDIALOG_COMPLETION_ABI_V1 ||
        subdialog_completion.kind !=
            VXML_CMETA_SUBDIALOG_RETURN_EVENT ||
        subdialog_completion.global_exit_kind !=
            VXML_CMETA_SUBDIALOG_GLOBAL_NATURAL ||
        subdialog_result.value.kind != VXML_CMETA_VALUE_UNDEFINED ||
        VXML_CMETA_SUBDIALOG_INGRESS_ACCEPTED != 0 ||
        subdialog_param.source != VXML_CMETA_SUBDIALOG_PARAM_TYPED ||
        subdialog_ticket.commit != NULL ||
        query_collect_v2 == NULL || complete_collect_v3 == NULL ||
        query_collect_utterance == NULL ||
        query_recording_shadow_value == NULL ||
        query_recording_shadow == NULL ||
        recording_shadow.abi_version !=
            VXML_CMETA_RECORDING_REF_VIEW_ABI_V1 ||
        query_menu == NULL || query_menu_v2 == NULL ||
        query_initial == NULL || complete_menu == NULL ||
        VXML_CMETA_PROMPT_MEDIA_CAP_MARK == 0u ||
        VXML_CMETA_PROMPT_MEDIA_CAP_AUDIO_FALLBACK == 0u ||
        VXML_CMETA_PROMPT_MEDIA_MARK == 0 ||
        prompt_mark.abi_version != VXML_CMETA_PROMPT_MARK_VIEW_ABI_V1 ||
        prompt_mark.segment_index != SIZE_MAX ||
        prompt_mark_v2.abi_version !=
            VXML_CMETA_PROMPT_MARK_PROGRESS_ABI_V2 ||
        prompt_barge_v2.abi_version != VXML_CMETA_PROMPT_BARGE_ABI_V2 ||
        prompt_media_completion_v2.abi_version !=
            VXML_CMETA_PROMPT_MEDIA_COMPLETION_ABI_V2 ||
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
        data_request_v2.abi_version !=
            VXML_CMETA_DATA_REQUEST_ABI_V2 ||
        data_request_v2.struct_size != sizeof(data_request_v2) ||
        data_request_v2.fetch_hint !=
            VXML_CMETA_DATA_FETCH_HINT_UNSPECIFIED ||
        data_request_v3.abi_version !=
            VXML_CMETA_DATA_REQUEST_ABI_V3 ||
        data_request_v3.struct_size != sizeof(data_request_v3) ||
        data_request_v3.method != VXML_SUBMIT_METHOD_GET ||
        data_request_v3.enctype != VXML_SUBMIT_ENCTYPE_URLENCODED ||
        data_field_v1.name != NULL ||
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
        collect_request_v2.abi_version !=
            VXML_CMETA_COLLECT_REQUEST_ABI_V2 ||
        collect_completion_v2.abi_version !=
            VXML_CMETA_COLLECT_COMPLETION_ABI_V2 ||
        collect_completion_v2.slot_count != 1u ||
        collect_completion_v3.abi_version !=
            VXML_CMETA_COLLECT_COMPLETION_ABI_V3 ||
        collect_completion_v3.slot_count != 1u ||
        collect_utterance.abi_version !=
            VXML_CMETA_COLLECT_UTTERANCE_RESULT_VIEW_ABI_V1 ||
        VXML_CMETA_COLLECT_CAP_RECORD_UTTERANCE == 0u ||
        VXML_CMETA_COLLECT_CAP_RECORD_UTTERANCE_TYPE == 0u ||
        menu_request.abi_version !=
            VXML_CMETA_MENU_COLLECT_REQUEST_ABI_V1 ||
        menu_request_v2.abi_version !=
            VXML_CMETA_MENU_COLLECT_REQUEST_ABI_V2 ||
        menu_policy.mode != 0 ||
        menu_grammar.media_type.data != NULL ||
        menu_grammar.src.data != NULL ||
        menu_completion.abi_version != VXML_CMETA_MENU_COMPLETION_ABI_V1 ||
        initial_request.abi_version !=
            VXML_CMETA_INITIAL_COLLECT_REQUEST_ABI_V1 ||
        menu_choice.dtmf.data != NULL ||
        menu_choice.speech.data != NULL ||
        VXML_CMETA_COLLECT_CAP_MENU_CHOICE == 0u ||
        VXML_CMETA_COLLECT_CAP_MENU_SPEECH_EXACT == 0u ||
        VXML_CMETA_COLLECT_CAP_MENU_SPEECH_APPROXIMATE == 0u ||
        VXML_CMETA_COLLECT_CAP_MENU_GRAMMAR_EXTERNAL == 0u ||
        VXML_CMETA_COLLECT_CAP_INITIAL_MULTI == 0u ||
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
