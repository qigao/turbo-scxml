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
        .max_execution_steps = 128u
    };
    vxml_program program = {0};
    vxml_session session = {0};
    vxml_cmeta_value_view read_value = {0};
    vxml_cmeta_exit_kind exit_kind = VXML_CMETA_EXIT_EMPTY;
    vxml_cmeta_name_view exit_name = {0};
    vxml_cmeta_value_view exit_value = {0};
    int result = 1;

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
    if (vxml_session_cmeta_exit_kind(&session, &exit_kind) != VXML_OK ||
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
