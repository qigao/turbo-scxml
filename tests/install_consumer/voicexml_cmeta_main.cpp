#include "voicexml_cmeta_fixture.h"

#include <cstring>

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
    vxml_cmeta_compile_options_v1 compile_options{};
    vxml_cmeta_session_options_v1 session_options{};
    vxml_program program{};
    vxml_session session{};
    vxml_cmeta_value_view read_value{};
    vxml_cmeta_exit_kind exit_kind = VXML_CMETA_EXIT_EMPTY;
    vxml_cmeta_name_view exit_name{};
    vxml_cmeta_value_view exit_value{};
    int result = 1;

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
    session_options.abi_version = VXML_CMETA_SESSION_OPTIONS_ABI_V1;
    session_options.struct_size = sizeof(session_options);
    session_options.initial_root = &initial_root;
    session_options.max_transaction_bytes = 65536u;
    session_options.max_execution_steps = 128u;

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
