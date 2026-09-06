#include <voicexml/cmeta.h>

#include "tinytest.h"

#include <stddef.h>
#include <string.h>

Struct(vxml_cmeta_test_root,
    (int, value)
);

static const cmeta_type_identity vxml_cmeta_test_root_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.voicexml.cmeta.root");
static const cmeta_type_traits vxml_cmeta_test_root_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY
};
static const cmeta_type_desc vxml_cmeta_test_root_type = {
    .name = "vxml_cmeta_test_root",
    .size = sizeof(vxml_cmeta_test_root),
    .align = _Alignof(vxml_cmeta_test_root),
    .kind = CMETA_T_OBJECT,
    .traits = &vxml_cmeta_test_root_traits,
    .identity = &vxml_cmeta_test_root_identity
};
static const cmeta_data_field_desc vxml_cmeta_test_root_fields[] = {
    {"test.voicexml.cmeta.root.value", "value",
     offsetof(vxml_cmeta_test_root, value), &cmeta_data_int}
};
static const cmeta_data_struct_shape vxml_cmeta_test_root_shape = {
    .layout = StructMeta(vxml_cmeta_test_root),
    .fields = vxml_cmeta_test_root_fields,
    .field_count = sizeof(vxml_cmeta_test_root_fields) /
                   sizeof(vxml_cmeta_test_root_fields[0])
};
static const cmeta_data_desc vxml_cmeta_test_root_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.voicexml.cmeta.root.data",
    .display_name = "VoiceXML CMeta test root",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &vxml_cmeta_test_root_type,
    .shape = &vxml_cmeta_test_root_shape
};

static vxml_cmeta_compile_options_v1 compile_options(void) {
    return (vxml_cmeta_compile_options_v1){
        .abi_version = VXML_CMETA_COMPILE_OPTIONS_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_compile_options_v1),
        .root = &vxml_cmeta_test_root_data,
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
}

static vxml_cmeta_session_options_v1 session_options(void) {
    return (vxml_cmeta_session_options_v1){
        .abi_version = VXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_session_options_v1),
        .max_transaction_bytes = 4096u,
        .max_execution_steps = 64u
    };
}

static const char cmeta_source[] =
    "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
    "datamodel='cmeta'><form><block><exit/></block></form></vxml>";

spec("VoiceXML CMeta public API") {
    it("keeps zero handles safe and appends status values") {
        vxml_program program = {0};
        vxml_session session = {0};

        check_true(VXML_INVALID_CONTRACT > VXML_CLOSED);
        check_true(VXML_SEMANTIC_ERROR > VXML_INVALID_CONTRACT);
        vxml_program_destroy(&program);
        vxml_session_destroy(&session);
        check_null(program.impl);
        check_null(session.impl);
    }

    it("accepts complete v1 option prefixes and ignores future tails") {
        struct future_compile_options {
            vxml_cmeta_compile_options_v1 v1;
            size_t future_limit;
        } future = {0};
        struct future_session_options {
            vxml_cmeta_session_options_v1 v1;
            size_t future_limit;
        } future_session = {0};
        vxml_program program = {0};
        vxml_session session = {0};

        future.v1 = compile_options();
        future.v1.struct_size = sizeof(future);
        future.future_limit = 1u;
        check_equal(vxml_compile_cmeta(
                        cmeta_source, sizeof(cmeta_source) - 1u, NULL,
                        &future.v1, &program, NULL),
                    VXML_OK);
        future_session.v1 = session_options();
        future_session.v1.struct_size = sizeof(future_session);
        future_session.future_limit = 1u;
        check_equal(vxml_session_init_cmeta(
                        &session, &program, &future_session.v1),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }

    it("rejects unsupported ABI versions and undersized prefixes atomically") {
        vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_cmeta_session_options_v1 session_options_value = session_options();
        vxml_program program = {(void *)1};
        vxml_session session = {(void *)1};

        compile.abi_version = VXML_CMETA_COMPILE_OPTIONS_ABI_V1 + 1u;
        check_equal(vxml_compile_cmeta(cmeta_source, sizeof(cmeta_source) - 1u,
                                       NULL, &compile, &program, NULL),
                    VXML_INVALID_CONTRACT);
        check_null(program.impl);
        compile = compile_options();
        compile.struct_size = offsetof(vxml_cmeta_compile_options_v1, root);
        check_equal(vxml_compile_cmeta(cmeta_source, sizeof(cmeta_source) - 1u,
                                       NULL, &compile, &program, NULL),
                    VXML_INVALID_CONTRACT);
        check_null(program.impl);

        compile = compile_options();
        check_equal(vxml_compile_cmeta(cmeta_source, sizeof(cmeta_source) - 1u,
                                       NULL, &compile, &program, NULL),
                    VXML_OK);
        session_options_value.abi_version =
            VXML_CMETA_SESSION_OPTIONS_ABI_V1 + 1u;
        check_equal(vxml_session_init_cmeta(&session, &program,
                                            &session_options_value),
                    VXML_INVALID_CONTRACT);
        check_null(session.impl);
        session_options_value = session_options();
        session_options_value.struct_size =
            offsetof(vxml_cmeta_session_options_v1, initial_root);
        check_equal(vxml_session_init_cmeta(&session, &program,
                                            &session_options_value),
                    VXML_INVALID_CONTRACT);
        check_null(session.impl);
        vxml_program_destroy(&program);
    }

    it("rejects zero hard limits and mismatched profile initializers") {
        vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_cmeta_session_options_v1 cmeta_session = session_options();
        vxml_program literal_program = {0};
        vxml_program cmeta_program = {0};
        vxml_session session = {0};
        static const char literal_source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><exit/></block></form></vxml>";
        size_t *compile_limits[] = {
            &compile.max_expression_bytes,
            &compile.max_expression_instructions,
            &compile.max_expression_operands,
            &compile.max_expression_depth,
            &compile.max_path_depth,
            &compile.max_literal_bytes,
            &compile.max_string_bytes,
            &compile.max_scope_slots,
            &compile.max_scope_storage_bytes,
            &compile.max_conditional_depth
        };
        size_t index;

        for (index = 0u; index < sizeof(compile_limits) / sizeof(compile_limits[0]);
             ++index) {
            const size_t saved = *compile_limits[index];
            *compile_limits[index] = 0u;
            check_equal(vxml_compile_cmeta(
                            cmeta_source, sizeof(cmeta_source) - 1u, NULL,
                            &compile, &cmeta_program, NULL),
                        VXML_INVALID_CONTRACT);
            check_null(cmeta_program.impl);
            *compile_limits[index] = saved;
        }
        compile = compile_options();
        check_equal(vxml_compile(literal_source, sizeof(literal_source) - 1u,
                                 NULL, &literal_program, NULL), VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &literal_program,
                                            &cmeta_session),
                    VXML_INVALID_CONTRACT);
        check_null(session.impl);
        check_equal(vxml_compile_cmeta(cmeta_source, sizeof(cmeta_source) - 1u,
                                       NULL, &compile, &cmeta_program, NULL),
                    VXML_OK);
        {
            size_t *session_limits[] = {
                &cmeta_session.max_transaction_bytes,
                &cmeta_session.max_execution_steps
            };
            for (index = 0u;
                 index < sizeof(session_limits) / sizeof(session_limits[0]);
                 ++index) {
                const size_t saved = *session_limits[index];
                *session_limits[index] = 0u;
                check_equal(vxml_session_init_cmeta(
                                &session, &cmeta_program, &cmeta_session),
                            VXML_INVALID_CONTRACT);
                check_null(session.impl);
                *session_limits[index] = saved;
            }
        }
        check_equal(vxml_session_init(&session, &cmeta_program),
                    VXML_INVALID_CONTRACT);
        check_null(session.impl);

        vxml_program_destroy(&literal_program);
        vxml_program_destroy(&cmeta_program);
    }

    it("rejects non-struct roots and duplicate semantic storage descriptors") {
        const cmeta_data_desc *semantic_data[] = {
            &cmeta_data_bool,
            &cmeta_data_bool
        };
        vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_program program = {(void *)1};

        compile.root = &cmeta_data_int;
        check_equal(vxml_compile_cmeta(cmeta_source, sizeof(cmeta_source) - 1u,
                                       NULL, &compile, &program, NULL),
                    VXML_INVALID_CONTRACT);
        check_null(program.impl);
        compile = compile_options();
        compile.semantic_data = semantic_data;
        compile.semantic_data_count = 2u;
        check_equal(vxml_compile_cmeta(cmeta_source, sizeof(cmeta_source) - 1u,
                                       NULL, &compile, &program, NULL),
                    VXML_INVALID_CONTRACT);
        check_null(program.impl);
    }

    it("rejects root fields whose descriptor storage disagrees with layout") {
        cmeta_data_field_desc fields[] = {
            vxml_cmeta_test_root_fields[0]
        };
        cmeta_data_struct_shape shape = vxml_cmeta_test_root_shape;
        cmeta_data_desc root = vxml_cmeta_test_root_data;
        vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_program program = {(void *)1};

        fields[0].value = &cmeta_data_float;
        shape.fields = fields;
        root.shape = &shape;
        compile.root = &root;
        check_equal(vxml_compile_cmeta(cmeta_source, sizeof(cmeta_source) - 1u,
                                       NULL, &compile, &program, NULL),
                    VXML_INVALID_CONTRACT);
        check_null(program.impl);
    }

    it("requires a CMeta datamodel while the plain compiler rejects it") {
        vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_program program = {0};
        vxml_diagnostic diagnostic = {0};

        check_equal(vxml_compile(cmeta_source, sizeof(cmeta_source) - 1u,
                                 NULL, &program, &diagnostic),
                    VXML_UNSUPPORTED_FEATURE);
        check_null(program.impl);
        check_equal(vxml_compile_cmeta(
                        cmeta_source, sizeof(cmeta_source) - 1u, NULL,
                        &compile, &program, &diagnostic),
                    VXML_OK);
        vxml_program_destroy(&program);
    }
}
