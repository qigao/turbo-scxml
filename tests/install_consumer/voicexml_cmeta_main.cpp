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
    std::is_standard_layout<vxml_cmeta_prompt_media_ticket_v1>::value,
    "prompt media ticket must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_media_request_v1>::value,
    "prompt media request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_media_adapter_v1>::value,
    "prompt media adapter must remain C-compatible");
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
    vxml_cmeta_prompt_media_ticket_v1 prompt_media_ticket{};
    vxml_cmeta_prompt_media_request_v1 prompt_media_request{};
    vxml_cmeta_prompt_media_adapter_v1 prompt_media_adapter{};
    vxml_cmeta_collect_request_v1 collect_request{};
    vxml_cmeta_collect_completion_v1 collect_completion{};
    vxml_cmeta_collect_result_slot_v1 collect_slot{};
    vxml_cmeta_collect_completion_v2 collect_completion_v2{};
    vxml_cmeta_collect_adapter_v1 collect_adapter{};
    vxml_cmeta_data_resource_v1 data_resource{};
    vxml_cmeta_data_resource_adapter_v1 data_adapter{};
    vxml_cmeta_compile_options_v1 compile_options{};
    vxml_cmeta_session_options_v1 session_options{};
    vxml_program program{};
    vxml_session session{};
    vxml_cmeta_value_view read_value{};
    vxml_cmeta_exit_kind exit_kind = VXML_CMETA_EXIT_EMPTY;
    vxml_cmeta_name_view exit_name{};
    vxml_cmeta_value_view exit_value{};
    auto raise_event = &vxml_session_cmeta_raise;
    auto raise_noinput = &vxml_session_cmeta_noinput;
    auto raise_nomatch = &vxml_session_cmeta_nomatch;
    auto take_reprompt = &vxml_session_cmeta_take_reprompt;
    auto query_prompt = &vxml_session_cmeta_prompt;
    int result = 1;

    prompt_view.abi_version = VXML_CMETA_PROMPT_VIEW_ABI_V1;
    prompt_view.struct_size = sizeof(prompt_view);
    prompt_media_request.abi_version =
        VXML_CMETA_PROMPT_MEDIA_REQUEST_ABI_V1;
    prompt_media_request.struct_size = sizeof(prompt_media_request);
    prompt_media_adapter.abi_version =
        VXML_CMETA_PROMPT_MEDIA_ADAPTER_ABI_V1;
    prompt_media_adapter.struct_size = sizeof(prompt_media_adapter);
    if (raise_event == nullptr || query_prompt == nullptr ||
        prompt_view.abi_version != VXML_CMETA_PROMPT_VIEW_ABI_V1 ||
        prompt_media_request.abi_version !=
            VXML_CMETA_PROMPT_MEDIA_REQUEST_ABI_V1 ||
        prompt_media_adapter.abi_version !=
            VXML_CMETA_PROMPT_MEDIA_ADAPTER_ABI_V1 ||
        prompt_media_segment.kind !=
            static_cast<vxml_cmeta_prompt_media_segment_kind>(0) ||
        prompt_media_ticket.commit != nullptr)
        return 6;

    collect_adapter.abi_version = VXML_CMETA_COLLECT_ADAPTER_ABI_V1;
    collect_adapter.struct_size = sizeof(collect_adapter);
    collect_completion.abi_version =
        VXML_CMETA_COLLECT_COMPLETION_ABI_V1;
    collect_completion.struct_size = sizeof(collect_completion);
    collect_completion_v2.abi_version =
        VXML_CMETA_COLLECT_COMPLETION_ABI_V2;
    collect_completion_v2.struct_size = sizeof(collect_completion_v2);
    collect_completion_v2.slots = &collect_slot;
    collect_completion_v2.slot_count = 1u;
    if (collect_request.abi_version != 0u ||
        collect_completion.abi_version !=
            VXML_CMETA_COLLECT_COMPLETION_ABI_V1 ||
        collect_completion_v2.abi_version !=
            VXML_CMETA_COLLECT_COMPLETION_ABI_V2 ||
        collect_completion_v2.slot_count != 1u ||
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
    if (vxml_session_cmeta_exit_kind(&session, &exit_kind) != VXML_OK ||
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
