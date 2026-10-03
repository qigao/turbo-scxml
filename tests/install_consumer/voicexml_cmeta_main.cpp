#include "voicexml_cmeta_fixture.h"

#include <cstring>
#include <type_traits>

static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_view_v1>::value,
    "prompt view must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_media_segment_v1>::value,
    "prompt media segment must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_media_fallback_v1>::value,
    "prompt media fallback must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_media_ticket_v1>::value,
    "prompt media ticket must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_media_request_v1>::value,
    "prompt media request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_media_batch_request_v1>::value,
    "prompt media batch request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_media_adapter_v1>::value,
    "prompt media adapter must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_mark_view_v1>::value,
    "prompt mark view must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_media_completion_v1>::value,
    "prompt media completion must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_collect_request_v1>::value,
    "collect request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_subdialog_param_v1>::value,
    "subdialog param must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_record_request_v1>::value,
    "record request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_record_ticket_v1>::value,
    "record ticket must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_record_adapter_v1>::value,
    "record adapter must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_recording_lease_v1>::value,
    "recording lease must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_record_completion_v1>::value,
    "record completion must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_record_result_view_v1>::value,
    "record result view must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_transfer_request_v1>::value,
    "transfer request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_transfer_ticket_v1>::value,
    "transfer ticket must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_transfer_adapter_v1>::value,
    "transfer adapter must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_transfer_completion_v1>::value,
    "transfer completion must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_subdialog_request_v1>::value,
    "subdialog request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_child_entry_v1>::value,
    "child entry must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_subdialog_ticket_v1>::value,
    "subdialog ticket must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_subdialog_adapter_v1>::value,
    "subdialog adapter must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_subdialog_result_entry_v1>::value,
    "subdialog result entry must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_subdialog_completion_v1>::value,
    "subdialog completion must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_navigation_request_v1>::value,
    "navigation request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_collect_completion_v1>::value,
    "collect completion must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_collect_result_slot_v1>::value,
    "collect result slot must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_collect_completion_v2>::value,
    "collect completion V2 must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_collect_request_v2>::value,
    "collect request V2 must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_collect_completion_v3>::value,
    "collect completion V3 must remain C-compatible");
static_assert(
    std::is_standard_layout<
        vxml_cmeta_collect_utterance_result_view_v1>::value,
    "collect utterance result view must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_recording_ref_view_v1>::value,
    "recording shadow ref view must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_menu_choice_v1>::value,
    "menu choice must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_menu_collect_request_v1>::value,
    "menu collect request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_menu_speech_policy_v1>::value,
    "menu speech policy must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_menu_grammar_ref_v1>::value,
    "menu grammar ref must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_menu_collect_request_v2>::value,
    "menu collect request V2 must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_menu_completion_v1>::value,
    "menu completion must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_initial_collect_request_v1>::value,
    "initial collect request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_collect_adapter_v1>::value,
    "collect adapter must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_mark_progress_v2>::value,
    "prompt mark progress V2 must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_media_completion_v2>::value,
    "prompt media completion V2 must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_barge_v2>::value,
    "prompt barge V2 must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_data_resource_v1>::value,
    "external data resource must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_data_resource_adapter_v1>::value,
    "external data adapter must remain C-compatible");

int main() {
    static constexpr char document[] =
        "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.0' "
        "datamodel='cmeta'><var name='value' expr='11'/><form><block>"
        "<assign name='value' expr='value + 2'/><exit expr='value'/>"
        "</block></form></vxml>";
    const cmeta_data_desc *root =
        turboscxml_install_cmeta_root_descriptor();
    const cmeta_type_desc *layout_type =
        turboscxml_install_cmeta_layout_value_type();
    const cmeta_type_desc *peer_type =
        turboscxml_install_cmeta_peer_value_descriptor()->storage_type;
    const turboscxml_install_cmeta_root initial_root{5};
    vxml_navigation_request_v1 navigation_request{};
    vxml_cmeta_prompt_view_v1 prompt_view{};
    vxml_cmeta_prompt_media_segment_v1 prompt_media_segment{};
    vxml_cmeta_prompt_media_fallback_v1 prompt_media_fallback{
        1u, 2u, 1u};
    vxml_cmeta_prompt_media_ticket_v1 prompt_media_ticket{};
    vxml_cmeta_prompt_media_request_v1 prompt_media_request{};
    vxml_cmeta_prompt_media_batch_request_v1 prompt_media_batch{};
    vxml_cmeta_prompt_media_adapter_v1 prompt_media_adapter{};
    vxml_cmeta_prompt_mark_view_v1 prompt_mark{};
    vxml_cmeta_prompt_mark_progress_v2 prompt_mark_v2{};
    vxml_cmeta_prompt_barge_v2 prompt_barge_v2{};
    vxml_cmeta_prompt_media_completion_v1 prompt_media_completion{};
    vxml_cmeta_prompt_media_completion_v2 prompt_media_completion_v2{};
    vxml_cmeta_collect_request_v1 collect_request{};
    vxml_cmeta_collect_request_v2 collect_request_v2{};
    vxml_cmeta_subdialog_param_v1 subdialog_param{};
    vxml_cmeta_record_request_v1 record_request{};
    vxml_cmeta_record_ticket_v1 record_ticket{};
    vxml_cmeta_record_adapter_v1 record_adapter{};
    vxml_cmeta_recording_lease_v1 recording_lease{};
    vxml_cmeta_record_completion_v1 record_completion{};
    vxml_cmeta_record_result_view_v1 record_result{};
    vxml_cmeta_transfer_request_v1 transfer_request{};
    vxml_cmeta_transfer_ticket_v1 transfer_ticket{};
    vxml_cmeta_transfer_adapter_v1 transfer_adapter{};
    vxml_cmeta_transfer_completion_v1 transfer_completion{};
    vxml_cmeta_subdialog_request_v1 subdialog_request{};
    vxml_cmeta_child_entry_v1 child_entry{};
    vxml_cmeta_subdialog_result_entry_v1 subdialog_result{};
    vxml_cmeta_subdialog_completion_v1 subdialog_completion{};
    vxml_cmeta_subdialog_ticket_v1 subdialog_ticket{};
    vxml_cmeta_subdialog_adapter_v1 subdialog_adapter{};
    vxml_cmeta_collect_completion_v1 collect_completion{};
    vxml_cmeta_collect_result_slot_v1 collect_slot{};
    vxml_cmeta_menu_choice_v1 menu_choice{};
    vxml_cmeta_menu_collect_request_v1 menu_request{};
    vxml_cmeta_menu_speech_policy_v1 menu_policy{};
    vxml_cmeta_menu_grammar_ref_v1 menu_grammar{};
    vxml_cmeta_menu_collect_request_v2 menu_request_v2{};
    vxml_cmeta_menu_completion_v1 menu_completion{};
    vxml_cmeta_initial_collect_request_v1 initial_request{};
    vxml_cmeta_collect_completion_v2 collect_completion_v2{};
    vxml_cmeta_collect_completion_v3 collect_completion_v3{};
    vxml_cmeta_collect_utterance_result_view_v1 collect_utterance{};
    vxml_cmeta_recording_ref_view_v1 recording_shadow{};
    vxml_cmeta_collect_adapter_v1 collect_adapter{};
    vxml_cmeta_data_resource_v1 data_resource{};
    vxml_cmeta_data_resource_adapter_v1 data_adapter{};
    vxml_cmeta_compile_options_v1 compile_options{};
    vxml_cmeta_session_options_v1 session_options{};
    vxml_program program{};
    vxml_session session{};
    vxml_cmeta_value_view read_value{};
    vxml_cmeta_terminal_kind terminal_kind = VXML_CMETA_TERMINAL_NONE;
    vxml_cmeta_exit_kind exit_kind = VXML_CMETA_EXIT_EMPTY;
    vxml_cmeta_name_view exit_name{};
    vxml_cmeta_value_view exit_value{};
    auto raise_event = &vxml_session_cmeta_raise;
    auto raise_noinput = &vxml_session_cmeta_noinput;
    auto raise_nomatch = &vxml_session_cmeta_nomatch;
    auto take_reprompt = &vxml_session_cmeta_take_reprompt;
    auto query_prompt = &vxml_session_cmeta_prompt;
    auto report_mark = &vxml_session_cmeta_prompt_media_mark;
    auto report_mark_v2 = &vxml_session_cmeta_prompt_media_mark_v2;
    auto complete_prompt_v2 =
        &vxml_session_cmeta_prompt_media_try_complete_v2;
    auto barge_prompt_v2 =
        &vxml_session_cmeta_prompt_media_barge_in_v2;
    auto query_mark_shadow =
        &vxml_session_cmeta_mark_shadow_value;
    auto query_mark = &vxml_session_cmeta_prompt_media_last_mark;
    auto query_terminal = &vxml_session_cmeta_terminal_kind;
    auto query_terminal_event = &vxml_session_cmeta_terminal_event;
    auto start_child = &vxml_session_cmeta_start_child;
    auto query_record = &vxml_session_cmeta_record_request;
    auto prepare_record = &vxml_session_cmeta_record_prepare;
    auto commit_record = &vxml_session_cmeta_record_commit;
    auto discard_record = &vxml_session_cmeta_record_discard;
    auto complete_record = &vxml_session_cmeta_record_try_complete;
    auto run_record_completion = &vxml_session_cmeta_record_run_ready;
    auto query_record_result = &vxml_session_cmeta_record_result;
    auto query_transfer = &vxml_session_cmeta_transfer_request;
    auto prepare_transfer = &vxml_session_cmeta_transfer_prepare;
    auto commit_transfer = &vxml_session_cmeta_transfer_commit;
    auto discard_transfer = &vxml_session_cmeta_transfer_discard;
    auto complete_transfer = &vxml_session_cmeta_transfer_try_complete;
    auto run_transfer_completion = &vxml_session_cmeta_transfer_run_ready;
    auto prepare_subdialog = &vxml_session_cmeta_subdialog_prepare;
    auto commit_subdialog = &vxml_session_cmeta_subdialog_commit;
    auto discard_subdialog = &vxml_session_cmeta_subdialog_discard;
    auto complete_subdialog = &vxml_session_cmeta_subdialog_try_complete;
    auto run_subdialog_completion = &vxml_session_cmeta_subdialog_run_ready;
    auto query_navigation = &vxml_session_navigation_request;
    auto query_collect_v2 = &vxml_session_cmeta_collect_request_v2;
    auto complete_collect_v3 =
        &vxml_session_cmeta_collect_try_complete_v3;
    auto query_collect_utterance =
        &vxml_session_cmeta_collect_utterance_result;
    auto query_recording_shadow_value =
        &vxml_session_cmeta_recording_shadow_value;
    auto query_recording_shadow =
        &vxml_session_cmeta_recording_shadow;
    auto query_menu = &vxml_session_cmeta_menu_collect_request;
    auto query_menu_v2 = &vxml_session_cmeta_menu_collect_request_v2;
    auto query_initial = &vxml_session_cmeta_initial_collect_request;
    auto complete_menu = &vxml_session_cmeta_menu_try_complete;
    int result = 1;

    subdialog_param.source = VXML_CMETA_SUBDIALOG_PARAM_TYPED;
    record_request.abi_version = VXML_CMETA_RECORD_REQUEST_ABI_V1;
    record_request.struct_size = sizeof(record_request);
    record_adapter.abi_version = VXML_CMETA_RECORD_ADAPTER_ABI_V1;
    record_adapter.struct_size = sizeof(record_adapter);
    record_completion.abi_version = VXML_CMETA_RECORD_COMPLETION_ABI_V1;
    record_completion.struct_size = sizeof(record_completion);
    record_completion.outcome = VXML_CMETA_RECORD_OUTCOME_NOINPUT;
    record_result.abi_version = VXML_CMETA_RECORD_RESULT_VIEW_ABI_V1;
    record_result.struct_size = sizeof(record_result);
    transfer_request.abi_version = VXML_CMETA_TRANSFER_REQUEST_ABI_V1;
    transfer_request.struct_size = sizeof(transfer_request);
    transfer_adapter.abi_version = VXML_CMETA_TRANSFER_ADAPTER_ABI_V1;
    transfer_adapter.struct_size = sizeof(transfer_adapter);
    transfer_completion.abi_version =
        VXML_CMETA_TRANSFER_COMPLETION_ABI_V1;
    transfer_completion.struct_size = sizeof(transfer_completion);
    transfer_completion.kind =
        VXML_CMETA_TRANSFER_COMPLETION_RESULT;
    transfer_completion.result =
        VXML_CMETA_TRANSFER_RESULT_UNKNOWN;
    subdialog_request.abi_version = VXML_CMETA_SUBDIALOG_REQUEST_ABI_V1;
    subdialog_request.struct_size = sizeof(subdialog_request);
    child_entry.abi_version = VXML_CMETA_CHILD_ENTRY_ABI_V1;
    child_entry.struct_size = sizeof(child_entry);
    subdialog_completion.abi_version =
        VXML_CMETA_SUBDIALOG_COMPLETION_ABI_V1;
    subdialog_completion.struct_size = sizeof(subdialog_completion);
    subdialog_completion.kind = VXML_CMETA_SUBDIALOG_RETURN_EVENT;
    subdialog_completion.global_exit_kind =
        VXML_CMETA_SUBDIALOG_GLOBAL_NATURAL;
    subdialog_adapter.abi_version = VXML_CMETA_SUBDIALOG_ADAPTER_ABI_V1;
    subdialog_adapter.struct_size = sizeof(subdialog_adapter);
    prompt_view.abi_version = VXML_CMETA_PROMPT_VIEW_ABI_V1;
    prompt_view.struct_size = sizeof(prompt_view);
    prompt_media_request.abi_version =
        VXML_CMETA_PROMPT_MEDIA_REQUEST_ABI_V1;
    prompt_media_request.struct_size = sizeof(prompt_media_request);
    prompt_media_batch.abi_version =
        VXML_CMETA_PROMPT_MEDIA_BATCH_REQUEST_ABI_V1;
    prompt_media_batch.struct_size = sizeof(prompt_media_batch);
    prompt_media_adapter.abi_version =
        VXML_CMETA_PROMPT_MEDIA_ADAPTER_ABI_V1;
    prompt_media_adapter.struct_size = sizeof(prompt_media_adapter);
    prompt_mark.abi_version = VXML_CMETA_PROMPT_MARK_VIEW_ABI_V1;
    prompt_mark.struct_size = sizeof(prompt_mark);
    prompt_mark.segment_index = SIZE_MAX;
    prompt_mark_v2.abi_version =
        VXML_CMETA_PROMPT_MARK_PROGRESS_ABI_V2;
    prompt_mark_v2.struct_size = sizeof(prompt_mark_v2);
    prompt_barge_v2.abi_version = VXML_CMETA_PROMPT_BARGE_ABI_V2;
    prompt_barge_v2.struct_size = sizeof(prompt_barge_v2);
    prompt_media_completion_v2.abi_version =
        VXML_CMETA_PROMPT_MEDIA_COMPLETION_ABI_V2;
    prompt_media_completion_v2.struct_size =
        sizeof(prompt_media_completion_v2);
    prompt_media_completion_v2.outcome =
        VXML_CMETA_PROMPT_MEDIA_OUTCOME_COMPLETED;
    prompt_media_completion.abi_version =
        VXML_CMETA_PROMPT_MEDIA_COMPLETION_ABI_V1;
    prompt_media_completion.struct_size = sizeof(prompt_media_completion);
    prompt_media_completion.outcome = VXML_CMETA_PROMPT_MEDIA_OUTCOME_FAILED;
    prompt_media_completion.failure =
        VXML_CMETA_PROMPT_MEDIA_FAILURE_UNSUPPORTED_FORMAT;
    if (raise_event == nullptr || query_prompt == nullptr ||
        report_mark == nullptr || report_mark_v2 == nullptr ||
        complete_prompt_v2 == nullptr || barge_prompt_v2 == nullptr ||
        query_mark_shadow == nullptr || query_mark == nullptr ||
        query_terminal == nullptr || query_terminal_event == nullptr ||
        start_child == nullptr ||
        child_entry.abi_version != VXML_CMETA_CHILD_ENTRY_ABI_V1 ||
        query_record == nullptr || prepare_record == nullptr ||
        commit_record == nullptr || discard_record == nullptr ||
        complete_record == nullptr || run_record_completion == nullptr ||
        query_record_result == nullptr ||
        record_request.abi_version != VXML_CMETA_RECORD_REQUEST_ABI_V1 ||
        record_adapter.abi_version != VXML_CMETA_RECORD_ADAPTER_ABI_V1 ||
        record_completion.abi_version !=
            VXML_CMETA_RECORD_COMPLETION_ABI_V1 ||
        record_completion.outcome != VXML_CMETA_RECORD_OUTCOME_NOINPUT ||
        record_result.abi_version !=
            VXML_CMETA_RECORD_RESULT_VIEW_ABI_V1 ||
        recording_lease.data != nullptr ||
        VXML_CMETA_RECORD_INGRESS_ACCEPTED != 0 ||
        record_ticket.commit != nullptr ||
        VXML_CMETA_RECORD_CAP_BEEP == 0u ||
        VXML_CMETA_RECORD_CAP_DTMF_TERM == 0u ||
        query_transfer == nullptr || prepare_transfer == nullptr ||
        commit_transfer == nullptr || discard_transfer == nullptr ||
        complete_transfer == nullptr ||
        run_transfer_completion == nullptr ||
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
        transfer_ticket.commit != nullptr ||
        VXML_CMETA_TRANSFER_CAP_BLIND == 0u ||
        VXML_CMETA_TRANSFER_CAP_BRIDGE == 0u ||
        VXML_CMETA_TRANSFER_CAP_CONSULTATION == 0u ||
        VXML_CMETA_TRANSFER_CONSULTATION == 0 ||
        prepare_subdialog == nullptr || commit_subdialog == nullptr ||
        discard_subdialog == nullptr ||
        complete_subdialog == nullptr || run_subdialog_completion == nullptr ||
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
        subdialog_ticket.commit != nullptr ||
        query_navigation == nullptr ||
        navigation_request.abi_version !=
            VXML_NAVIGATION_REQUEST_ABI_V1 ||
        query_collect_v2 == nullptr ||
        complete_collect_v3 == nullptr ||
        query_collect_utterance == nullptr ||
        query_recording_shadow_value == nullptr ||
        query_recording_shadow == nullptr ||
        query_menu == nullptr || query_menu_v2 == nullptr ||
        query_initial == nullptr || complete_menu == nullptr ||
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
            VXML_CMETA_PROMPT_MEDIA_FAILURE_UNSUPPORTED_FORMAT ||
        VXML_CMETA_PROMPT_MEDIA_FAILURE_BADFETCH == 0 ||
        VXML_CMETA_PROMPT_MEDIA_FAILURE_NORESOURCE == 0 ||
        prompt_media_segment.kind !=
            static_cast<vxml_cmeta_prompt_media_segment_kind>(0) ||
        prompt_media_ticket.commit != nullptr)
        return 6;

    collect_adapter.abi_version = VXML_CMETA_COLLECT_ADAPTER_ABI_V1;
    collect_adapter.struct_size = sizeof(collect_adapter);
    collect_completion.abi_version =
        VXML_CMETA_COLLECT_COMPLETION_ABI_V1;
    collect_completion.struct_size = sizeof(collect_completion);
    menu_request.abi_version = VXML_CMETA_MENU_COLLECT_REQUEST_ABI_V1;
    menu_request.struct_size = sizeof(menu_request);
    menu_request_v2.abi_version = VXML_CMETA_MENU_COLLECT_REQUEST_ABI_V2;
    menu_request_v2.struct_size = sizeof(menu_request_v2);
    menu_completion.abi_version = VXML_CMETA_MENU_COMPLETION_ABI_V1;
    menu_completion.struct_size = sizeof(menu_completion);
    initial_request.abi_version =
        VXML_CMETA_INITIAL_COLLECT_REQUEST_ABI_V1;
    initial_request.struct_size = sizeof(initial_request);
    navigation_request.abi_version =
        VXML_NAVIGATION_REQUEST_ABI_V1;
    navigation_request.struct_size = sizeof(navigation_request);
    collect_request_v2.abi_version =
        VXML_CMETA_COLLECT_REQUEST_ABI_V2;
    collect_request_v2.struct_size = sizeof(collect_request_v2);
    collect_completion_v2.abi_version =
        VXML_CMETA_COLLECT_COMPLETION_ABI_V2;
    collect_completion_v2.struct_size = sizeof(collect_completion_v2);
    collect_completion_v2.slots = &collect_slot;
    collect_completion_v2.slot_count = 1u;
    collect_completion_v3.abi_version =
        VXML_CMETA_COLLECT_COMPLETION_ABI_V3;
    collect_completion_v3.struct_size = sizeof(collect_completion_v3);
    collect_completion_v3.slots = &collect_slot;
    collect_completion_v3.slot_count = 1u;
    collect_utterance.abi_version =
        VXML_CMETA_COLLECT_UTTERANCE_RESULT_VIEW_ABI_V1;
    collect_utterance.struct_size = sizeof(collect_utterance);
    recording_shadow.abi_version =
        VXML_CMETA_RECORDING_REF_VIEW_ABI_V1;
    recording_shadow.struct_size = sizeof(recording_shadow);
    if (collect_request.abi_version != 0u ||
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
        recording_shadow.abi_version !=
            VXML_CMETA_RECORDING_REF_VIEW_ABI_V1 ||
        VXML_CMETA_COLLECT_CAP_RECORD_UTTERANCE == 0u ||
        VXML_CMETA_COLLECT_CAP_RECORD_UTTERANCE_TYPE == 0u ||
        menu_request.abi_version !=
            VXML_CMETA_MENU_COLLECT_REQUEST_ABI_V1 ||
        menu_request_v2.abi_version !=
            VXML_CMETA_MENU_COLLECT_REQUEST_ABI_V2 ||
        menu_policy.mode != static_cast<vxml_cmeta_menu_accept_mode>(0) ||
        menu_grammar.media_type.data != nullptr ||
        menu_grammar.src.data != nullptr ||
        menu_completion.abi_version != VXML_CMETA_MENU_COMPLETION_ABI_V1 ||
        initial_request.abi_version !=
            VXML_CMETA_INITIAL_COLLECT_REQUEST_ABI_V1 ||
        menu_choice.dtmf.data != nullptr ||
        menu_choice.speech.data != nullptr ||
        VXML_CMETA_COLLECT_CAP_MENU_CHOICE == 0u ||
        VXML_CMETA_COLLECT_CAP_MENU_SPEECH_EXACT == 0u ||
        VXML_CMETA_COLLECT_CAP_MENU_SPEECH_APPROXIMATE == 0u ||
        VXML_CMETA_COLLECT_CAP_MENU_GRAMMAR_EXTERNAL == 0u ||
        VXML_CMETA_COLLECT_CAP_INITIAL_MULTI == 0u ||
        collect_adapter.abi_version != VXML_CMETA_COLLECT_ADAPTER_ABI_V1)
        return 4;
    data_adapter.abi_version = VXML_CMETA_DATA_RESOURCE_ADAPTER_ABI_V1;
    data_adapter.struct_size = sizeof(data_adapter);
    if (data_resource.format != static_cast<vxml_cmeta_data_format>(0) ||
        data_adapter.abi_version != VXML_CMETA_DATA_RESOURCE_ADAPTER_ABI_V1)
        return 5;
    compile_options.abi_version = VXML_CMETA_COMPILE_OPTIONS_ABI_V1;
    compile_options.struct_size = sizeof(compile_options);
    compile_options.root = root;
    compile_options.max_expression_bytes = 1024u;
    compile_options.max_expression_instructions = 256u;
    compile_options.max_expression_operands = 32u;
    compile_options.max_expression_depth = 16u;
    compile_options.max_path_depth = 8u;
    compile_options.max_literal_bytes = 1024u;
    compile_options.max_string_bytes = 1024u;
    compile_options.max_scope_slots = 32u;
    compile_options.max_scope_storage_bytes = 4096u;
    compile_options.max_conditional_depth = 8u;
    compile_options.max_subdialogs = 1u;
    compile_options.max_subdialog_uri_bytes = 128u;
    compile_options.max_prompt_foreach = 2u;
    compile_options.max_prompt_foreach_items = 4u;
    compile_options.max_prompt_foreach_snapshot_bytes = 1024u;
    compile_options.max_prompt_expanded_segments = 16u;
    compile_options.max_event_handlers = 4u;
    compile_options.max_event_name_bytes = 64u;
    session_options.abi_version = VXML_CMETA_SESSION_OPTIONS_ABI_V1;
    session_options.struct_size = sizeof(session_options);
    session_options.initial_root = &initial_root;
    session_options.max_transaction_bytes = 65536u;
    session_options.max_execution_steps = 128u;
    session_options.max_event_counters = 8u;
    session_options.max_event_name_bytes = 64u;
    session_options.max_event_dispatch_depth = 8u;

    if (layout_type == peer_type || !cmeta_type_equal(layout_type, peer_type))
        goto cleanup;
    if (vxml_compile_cmeta(
            document, std::strlen(document), nullptr, &compile_options,
            &program, nullptr) != VXML_OK)
        goto cleanup;
    if (vxml_session_init_cmeta(
            &session, &program, &session_options) != VXML_OK ||
        vxml_session_start(&session) != VXML_OK ||
        vxml_session_get_state(&session) != VXML_SESSION_EXITED)
        goto cleanup;
    if (vxml_session_cmeta_read(
            &session, "value", 5u, &read_value) != VXML_OK ||
        read_value.kind != VXML_CMETA_VALUE_SINT ||
        read_value.data.sint != 13)
        goto cleanup;
    if (vxml_session_cmeta_terminal_kind(
            &session, &terminal_kind) != VXML_OK ||
        terminal_kind != VXML_CMETA_TERMINAL_EXIT ||
        vxml_session_cmeta_exit_kind(&session, &exit_kind) != VXML_OK ||
        exit_kind != VXML_CMETA_EXIT_EXPRESSION ||
        vxml_session_cmeta_exit_count(&session) != 1u ||
        vxml_session_cmeta_exit_at(
            &session, 0u, &exit_name, &exit_value) != VXML_OK ||
        exit_name.data != nullptr || exit_name.size != 0u ||
        exit_value.kind != VXML_CMETA_VALUE_SINT ||
        exit_value.data.sint != 13)
        goto cleanup;
    if (vxml_session_close(&session) != VXML_OK)
        goto cleanup;
    result = 0;

cleanup:
    vxml_session_destroy(&session);
    vxml_program_destroy(&program);
    return result;
}
