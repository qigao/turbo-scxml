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
    std::is_standard_layout<vxml_cmeta_collect_completion_v1>::value,
    "collect completion must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_collect_result_slot_v1>::value,
    "collect result slot must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_collect_completion_v2>::value,
    "collect completion V2 must remain C-compatible");
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
    vxml_cmeta_prompt_view_v1 prompt_view{};
    vxml_cmeta_prompt_media_segment_v1 prompt_media_segment{};
    vxml_cmeta_prompt_media_fallback_v1 prompt_media_fallback{
        1u, 2u, 1u};
    vxml_cmeta_prompt_media_ticket_v1 prompt_media_ticket{};
    vxml_cmeta_prompt_media_request_v1 prompt_media_request{};
    vxml_cmeta_prompt_media_batch_request_v1 prompt_media_batch{};
    vxml_cmeta_prompt_media_adapter_v1 prompt_media_adapter{};
    vxml_cmeta_prompt_mark_view_v1 prompt_mark{};
    vxml_cmeta_prompt_media_completion_v1 prompt_media_completion{};
    vxml_cmeta_collect_request_v1 collect_request{};
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
    auto query_mark = &vxml_session_cmeta_prompt_media_last_mark;
    auto query_terminal = &vxml_session_cmeta_terminal_kind;
    auto query_terminal_event = &vxml_session_cmeta_terminal_event;
    auto query_menu = &vxml_session_cmeta_menu_collect_request;
    auto query_menu_v2 = &vxml_session_cmeta_menu_collect_request_v2;
    auto query_initial = &vxml_session_cmeta_initial_collect_request;
    auto complete_menu = &vxml_session_cmeta_menu_try_complete;
    int result = 1;

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
    prompt_media_completion.abi_version =
        VXML_CMETA_PROMPT_MEDIA_COMPLETION_ABI_V1;
    prompt_media_completion.struct_size = sizeof(prompt_media_completion);
    prompt_media_completion.outcome = VXML_CMETA_PROMPT_MEDIA_OUTCOME_FAILED;
    prompt_media_completion.failure =
        VXML_CMETA_PROMPT_MEDIA_FAILURE_UNSUPPORTED_FORMAT;
    if (raise_event == nullptr || query_prompt == nullptr ||
        report_mark == nullptr || query_mark == nullptr ||
        query_terminal == nullptr || query_terminal_event == nullptr ||
        query_menu == nullptr || query_menu_v2 == nullptr ||
        query_initial == nullptr || complete_menu == nullptr ||
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
    collect_completion_v2.abi_version =
        VXML_CMETA_COLLECT_COMPLETION_ABI_V2;
    collect_completion_v2.struct_size = sizeof(collect_completion_v2);
    collect_completion_v2.slots = &collect_slot;
    collect_completion_v2.slot_count = 1u;
    if (collect_request.abi_version != 0u ||
        collect_request.has_timeout ||
        collect_request.timeout_us != UINT64_C(0) ||
        collect_completion.abi_version !=
            VXML_CMETA_COLLECT_COMPLETION_ABI_V1 ||
        collect_completion_v2.abi_version !=
            VXML_CMETA_COLLECT_COMPLETION_ABI_V2 ||
        collect_completion_v2.slot_count != 1u ||
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
