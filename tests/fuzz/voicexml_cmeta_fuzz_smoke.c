#include "voicexml_fuzz_common.h"

#include <voicexml/cmeta.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifndef VOICEXML_CMETA_FUZZ_CORPUS_DIR
#error "VOICEXML_CMETA_FUZZ_CORPUS_DIR must be defined"
#endif

Struct(voicexml_fuzz_root,
    (int, value)
);

static const cmeta_type_identity root_identity =
    CMETA_TYPE_ID_ATOM_INIT("fuzz.voicexml.cmeta.root");
static const cmeta_type_traits root_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY
};
static const cmeta_type_desc root_type = {
    .name = "voicexml_fuzz_root",
    .size = sizeof(voicexml_fuzz_root),
    .align = _Alignof(voicexml_fuzz_root),
    .kind = CMETA_T_OBJECT,
    .traits = &root_traits,
    .identity = &root_identity
};
static const cmeta_data_field_desc root_fields[] = {
    {"fuzz.voicexml.cmeta.root.value", "value",
     offsetof(voicexml_fuzz_root, value), &cmeta_data_int}
};
static const cmeta_data_struct_shape root_shape = {
    .layout = StructMeta(voicexml_fuzz_root),
    .fields = root_fields,
    .field_count = sizeof(root_fields) / sizeof(root_fields[0])
};
static const cmeta_data_desc root_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "fuzz.voicexml.cmeta.root.data",
    .display_name = "VoiceXML CMeta fuzz root",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &root_type,
    .shape = &root_shape
};

static vxml_cmeta_compile_options_v1 compile_options(void) {
    return (vxml_cmeta_compile_options_v1){
        .abi_version = VXML_CMETA_COMPILE_OPTIONS_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_compile_options_v1),
        .root = &root_data,
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
        .max_event_handlers = 16u,
        .max_event_name_bytes = 128u,
        .max_submit_fields = 8u,
        .max_submit_value_bytes = 256u,
        .max_submit_uri_bytes = 256u
    };
}

static int run_case(
    const unsigned char *data, size_t size, void *user) {
    static const voicexml_fuzz_root initial_root = {0};
    const vxml_cmeta_compile_options_v1 compile = compile_options();
    const vxml_cmeta_session_options_v1 session_options = {
        .abi_version = VXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_session_options_v1),
        .initial_root = &initial_root,
        .max_transaction_bytes = 8192u,
        .max_execution_steps = 128u,
        .max_event_counters = 16u,
        .max_event_name_bytes = 128u,
        .max_event_dispatch_depth = 8u
    };
    vxml_program program = {0};
    vxml_session session = {0};
    vxml_status status;
    (void)user;

    status = vxml_compile_cmeta(
        data, size, NULL, &compile, &program, NULL);
    if ((status == VXML_OK) != (program.impl != NULL)) {
        vxml_program_destroy(&program);
        return 1;
    }

    if (status == VXML_OK) {
        const vxml_status init_status =
            vxml_session_init_cmeta(
                &session, &program, &session_options);
        if ((init_status == VXML_OK) != (session.impl != NULL)) {
            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
            return 1;
        }
        if (init_status == VXML_OK)
            (void)vxml_session_start(&session);
    }

    vxml_session_destroy(&session);
    vxml_program_destroy(&program);
    return program.impl == NULL && session.impl == NULL ? 0 : 1;
}

int main(void) {
    static const char *const seeds[] = {
        "minimal-assign.vxml",
        "conditional-exit.vxml",
        "submit-urlencoded.vxml",
        "invalid-namelist.vxml",
        "malformed-expression.vxml",
        "partial-tree-unclosed.vxml"
    };
    const int result = voicexml_fuzz_run(
        VOICEXML_CMETA_FUZZ_CORPUS_DIR,
        seeds, sizeof(seeds) / sizeof(seeds[0]),
        16u, 4096u, run_case, NULL);
    if (result != 0)
        fprintf(stderr, "VoiceXML CMeta fuzz smoke failed\n");
    return result;
}
