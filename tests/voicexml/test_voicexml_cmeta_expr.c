#include "voicexml_cmeta_expr.h"
#include "voicexml_test_allocator.h"

#include "tinytest.h"

#include <cmeta/cmeta.h>
#include <cmeta/data.h>

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct expression_text {
    const unsigned char *data;
    size_t size;
} expression_text;

typedef struct expression_nested {
    int number;
    bool ready;
} expression_nested;

typedef struct expression_root {
    bool enabled;
    int number;
    size_t total;
    double ratio;
    expression_text label;
    expression_nested nested;
    bool _event;
    bool _name;
    bool _sessionid;
    bool _ioprocessors;
} expression_root;

enum expression_root_field {
    ROOT_ENABLED = 0,
    ROOT_NUMBER,
    ROOT_TOTAL,
    ROOT_RATIO,
    ROOT_LABEL,
    ROOT_NESTED,
    ROOT_EVENT,
    ROOT_NAME,
    ROOT_SESSION_ID,
    ROOT_IOPROCESSORS,
    ROOT_FIELD_COUNT
};

static size_t text_read_calls;

static bool text_is_zero(const void *object) {
    const expression_text *text = (const expression_text *)object;
    return text != NULL && text->data == NULL && text->size == 0u;
}

static cmeta_status text_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
    expression_text *text = (expression_text *)object;
    if (text == NULL || (size != 0u && data == NULL))
        return CMETA_INVALID_ARGUMENT;
    if (size > max_bytes) return CMETA_CAPACITY_EXCEEDED;
    text->data = data;
    text->size = size;
    return CMETA_OK;
}

static void text_restore_zero(void *object) {
    expression_text *text = (expression_text *)object;
    if (text != NULL) {
        text->data = NULL;
        text->size = 0u;
    }
}

static cmeta_status text_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
    const expression_text *text = (const expression_text *)object;
    ++text_read_calls;
    if (text == NULL || out_data == NULL || out_size == NULL ||
        (text->size != 0u && text->data == NULL))
        return CMETA_CALLBACK_ERROR;
    *out_data = text->data;
    *out_size = text->size;
    return CMETA_OK;
}

static const cmeta_type_traits trivial_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY};
static const cmeta_type_identity text_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.voicexml.cmeta.expression.text");
static const cmeta_type_identity nested_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.voicexml.cmeta.expression.nested");
static const cmeta_type_identity root_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.voicexml.cmeta.expression.root");
static const cmeta_type_desc text_type = {
    .name = "expression_text",
    .size = sizeof(expression_text),
    .align = _Alignof(expression_text),
    .kind = CMETA_T_OBJECT,
    .traits = &trivial_traits,
    .identity = &text_identity};
static const cmeta_type_desc nested_type = {
    .name = "expression_nested",
    .size = sizeof(expression_nested),
    .align = _Alignof(expression_nested),
    .kind = CMETA_T_OBJECT,
    .traits = &trivial_traits,
    .identity = &nested_identity};
static const cmeta_type_desc root_type = {
    .name = "expression_root",
    .size = sizeof(expression_root),
    .align = _Alignof(expression_root),
    .kind = CMETA_T_OBJECT,
    .traits = &trivial_traits,
    .identity = &root_identity};

static const cmeta_data_buffer_shape text_shape = {
    .ownership = CMETA_DATA_BUFFER_BORROWED};
static const cmeta_data_buffer_ops text_ops = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &text_type,
    .ownership = CMETA_DATA_BUFFER_BORROWED,
    .is_zero = text_is_zero,
    .assign = text_assign,
    .restore_zero = text_restore_zero,
    .read = text_read};
static const cmeta_data_desc text_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.voicexml.cmeta.expression.text.data",
    .display_name = "expression text",
    .kind = CMETA_DATA_STRING,
    .storage_type = &text_type,
    .shape = &text_shape,
    .buffer_ops = &text_ops};

static const cmeta_field_desc nested_layout_fields[] = {
    {"number", "int", offsetof(expression_nested, number), sizeof(int),
     _Alignof(int), &cmeta_type_int, NULL},
    {"ready", "bool", offsetof(expression_nested, ready), sizeof(bool),
     _Alignof(bool), &cmeta_type_bool, NULL}};
static const cmeta_struct_desc nested_layout = {
    .name = "expression_nested",
    .size = sizeof(expression_nested),
    .align = _Alignof(expression_nested),
    .fields = nested_layout_fields,
    .field_count = 2u};
static const cmeta_data_field_desc nested_fields[] = {
    {"test.voicexml.cmeta.expression.nested.number", "number",
     offsetof(expression_nested, number), &cmeta_data_int},
    {"test.voicexml.cmeta.expression.nested.ready", "ready",
     offsetof(expression_nested, ready), &cmeta_data_bool}};
static const cmeta_data_struct_shape nested_shape = {
    .layout = &nested_layout,
    .fields = nested_fields,
    .field_count = 2u};
static const cmeta_data_desc nested_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.voicexml.cmeta.expression.nested.data",
    .display_name = "expression nested",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &nested_type,
    .shape = &nested_shape};

static const cmeta_field_desc root_layout_fields[] = {
    {"enabled", "bool", offsetof(expression_root, enabled), sizeof(bool),
     _Alignof(bool), &cmeta_type_bool, NULL},
    {"number", "int", offsetof(expression_root, number), sizeof(int),
     _Alignof(int), &cmeta_type_int, NULL},
    {"total", "size_t", offsetof(expression_root, total), sizeof(size_t),
     _Alignof(size_t), &cmeta_type_size, NULL},
    {"ratio", "double", offsetof(expression_root, ratio), sizeof(double),
     _Alignof(double), &cmeta_type_double, NULL},
    {"label", "expression_text", offsetof(expression_root, label),
     sizeof(expression_text), _Alignof(expression_text), &text_type, NULL},
    {"nested", "expression_nested", offsetof(expression_root, nested),
     sizeof(expression_nested), _Alignof(expression_nested), &nested_type,
     NULL},
    {"_event", "bool", offsetof(expression_root, _event), sizeof(bool),
     _Alignof(bool), &cmeta_type_bool, NULL},
    {"_name", "bool", offsetof(expression_root, _name), sizeof(bool),
     _Alignof(bool), &cmeta_type_bool, NULL},
    {"_sessionid", "bool", offsetof(expression_root, _sessionid),
     sizeof(bool), _Alignof(bool), &cmeta_type_bool, NULL},
    {"_ioprocessors", "bool", offsetof(expression_root, _ioprocessors),
     sizeof(bool), _Alignof(bool), &cmeta_type_bool, NULL}};
static const cmeta_struct_desc root_layout = {
    .name = "expression_root",
    .size = sizeof(expression_root),
    .align = _Alignof(expression_root),
    .fields = root_layout_fields,
    .field_count = ROOT_FIELD_COUNT};
static const cmeta_data_field_desc root_fields[] = {
    {"test.voicexml.cmeta.expression.root.enabled", "enabled",
     offsetof(expression_root, enabled), &cmeta_data_bool},
    {"test.voicexml.cmeta.expression.root.number", "number",
     offsetof(expression_root, number), &cmeta_data_int},
    {"test.voicexml.cmeta.expression.root.total", "total",
     offsetof(expression_root, total), &cmeta_data_size},
    {"test.voicexml.cmeta.expression.root.ratio", "ratio",
     offsetof(expression_root, ratio), &cmeta_data_double},
    {"test.voicexml.cmeta.expression.root.label", "label",
     offsetof(expression_root, label), &text_data},
    {"test.voicexml.cmeta.expression.root.nested", "nested",
     offsetof(expression_root, nested), &nested_data},
    {"test.voicexml.cmeta.expression.root._event", "_event",
     offsetof(expression_root, _event), &cmeta_data_bool},
    {"test.voicexml.cmeta.expression.root._name", "_name",
     offsetof(expression_root, _name), &cmeta_data_bool},
    {"test.voicexml.cmeta.expression.root._sessionid", "_sessionid",
     offsetof(expression_root, _sessionid), &cmeta_data_bool},
    {"test.voicexml.cmeta.expression.root._ioprocessors", "_ioprocessors",
     offsetof(expression_root, _ioprocessors), &cmeta_data_bool}};
static const cmeta_data_struct_shape root_shape = {
    .layout = &root_layout,
    .fields = root_fields,
    .field_count = ROOT_FIELD_COUNT};
static const cmeta_data_desc root_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.voicexml.cmeta.expression.root.data",
    .display_name = "expression root",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &root_type,
    .shape = &root_shape};

/* This descriptor has a distinct address and originates in the test TU, but
 * has the same semantic identity as Salts' cmeta_type_int descriptor. */
static const cmeta_type_identity semantic_int_identity =
    CMETA_TYPE_ID_ATOM_INIT("cmeta.int");
static const cmeta_type_desc semantic_int_type = {
    .name = "semantic_int_from_test_tu",
    .size = sizeof(int),
    .align = _Alignof(int),
    .kind = CMETA_T_INTEGER,
    .traits = &trivial_traits,
    .identity = &semantic_int_identity};
static const cmeta_data_integer_shape semantic_int_shape = {
    .bits = (uint8_t)(sizeof(int) * CHAR_BIT)};
static const cmeta_data_desc semantic_int_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.voicexml.cmeta.expression.semantic-int.data",
    .display_name = "semantic int from test TU",
    .kind = CMETA_DATA_SINT,
    .storage_type = &semantic_int_type,
    .shape = &semantic_int_shape};

enum expression_slot {
    SLOT_NUMBER = 0,
    SLOT_ENABLED,
    SLOT_NESTED,
    SLOT_LATE,
    SLOT_COUNT
};

typedef struct expression_fixture {
    cmeta_scope_schema inner_schema;
    cmeta_scope_schema outer_schema;
    cmeta_scope_storage inner_storage;
    cmeta_scope_storage outer_storage;
    unsigned char inner_declared[SLOT_COUNT];
    unsigned char outer_declared[SLOT_COUNT];
    vxml_cmeta_expr_compile_scope compile_scopes[2];
    vxml_cmeta_expr_runtime_scope runtime_scopes[2];
    expression_root root;
    unsigned char root_bound[ROOT_FIELD_COUNT];
    vxml_cmeta_expr_runtime runtime;
} expression_fixture;

static bool register_fixture_slots(
    cmeta_scope_schema *schema, const cmeta_data_desc *number_data) {
    static const char *const names[SLOT_COUNT] = {
        "number", "enabled", "nested", "late"};
    const cmeta_data_desc *const values[SLOT_COUNT] = {
        number_data, &cmeta_data_bool, &nested_data, &cmeta_data_int};
    size_t index;
    for (index = 0u; index < SLOT_COUNT; ++index) {
        size_t slot = SIZE_MAX;
        bool conflict = false;
        if (!cmeta_scope_register(
                schema, names[index], strlen(names[index]), values[index],
                &slot, &conflict) || conflict || slot != index)
            return false;
    }
    return true;
}

static bool assign_fixture_values(
    cmeta_scope_storage *storage, int number, bool enabled,
    expression_nested nested, int late) {
    return cmeta_scope_view_assign(&storage->view, SLOT_NUMBER, &number) &&
        cmeta_scope_view_assign(&storage->view, SLOT_ENABLED, &enabled) &&
        cmeta_scope_view_assign(&storage->view, SLOT_NESTED, &nested) &&
        cmeta_scope_view_assign(&storage->view, SLOT_LATE, &late);
}

static void expression_fixture_destroy(expression_fixture *fixture) {
    cmeta_scope_storage_destroy(&fixture->inner_storage);
    cmeta_scope_storage_destroy(&fixture->outer_storage);
    cmeta_scope_schema_destroy(&fixture->inner_schema);
    cmeta_scope_schema_destroy(&fixture->outer_schema);
    memset(fixture, 0, sizeof(*fixture));
}

static bool expression_fixture_init(expression_fixture *fixture) {
    const expression_nested inner_nested = {41, false};
    const expression_nested outer_nested = {31, true};
    memset(fixture, 0, sizeof(*fixture));
    if (!cmeta_scope_schema_init(
            &fixture->inner_schema, SLOT_COUNT, 256u, NULL) ||
        !cmeta_scope_schema_init(
            &fixture->outer_schema, SLOT_COUNT, 256u, NULL) ||
        !register_fixture_slots(
            &fixture->inner_schema, &semantic_int_data) ||
        !register_fixture_slots(&fixture->outer_schema, &cmeta_data_int) ||
        !cmeta_scope_storage_init(
            &fixture->inner_storage, &fixture->inner_schema, NULL) ||
        !cmeta_scope_storage_init(
            &fixture->outer_storage, &fixture->outer_schema, NULL) ||
        !assign_fixture_values(
            &fixture->inner_storage, 22, false, inner_nested, 202) ||
        !assign_fixture_values(
            &fixture->outer_storage, 11, true, outer_nested, 101)) {
        expression_fixture_destroy(fixture);
        return false;
    }
    memset(fixture->outer_declared, 1, sizeof(fixture->outer_declared));
    memset(fixture->root_bound, 1, sizeof(fixture->root_bound));
    fixture->root.enabled = true;
    fixture->root.number = 7;
    fixture->root.total = 9u;
    fixture->root.ratio = 1.5;
    fixture->root.label.data = (const unsigned char *)"hello";
    fixture->root.label.size = 5u;
    fixture->root.nested.number = 5;
    fixture->root.nested.ready = true;
    fixture->compile_scopes[0] = (vxml_cmeta_expr_compile_scope){
        20u, &fixture->inner_schema};
    fixture->compile_scopes[1] = (vxml_cmeta_expr_compile_scope){
        10u, &fixture->outer_schema};
    /* Runtime order is intentionally different; scope IDs select frames. */
    fixture->runtime_scopes[0] = (vxml_cmeta_expr_runtime_scope){
        10u, &fixture->outer_storage.view,
        fixture->outer_declared, SLOT_COUNT};
    fixture->runtime_scopes[1] = (vxml_cmeta_expr_runtime_scope){
        20u, &fixture->inner_storage.view,
        fixture->inner_declared, SLOT_COUNT};
    fixture->runtime = (vxml_cmeta_expr_runtime){
        .root = &fixture->root,
        .root_bound = fixture->root_bound,
        .root_bound_count = ROOT_FIELD_COUNT,
        .scopes = fixture->runtime_scopes,
        .scope_count = 2u};
    return true;
}

static vxml_cmeta_expr_limits test_limits(void) {
    return (vxml_cmeta_expr_limits){
        .max_source_bytes = 1024u,
        .max_instructions = 128u,
        .max_operands = 64u,
        .max_expression_depth = 16u,
        .max_path_depth = 8u,
        .max_literal_bytes = 1024u,
        .max_string_bytes = 1024u};
}

static vxml_status compile_fixture_value(
    expression_fixture *fixture, vxml_cmeta_expr_program *program,
    const char *source, const vxml_cmeta_expr_limits *limits,
    vxml_cmeta_expr_diagnostic *diagnostic) {
    return vxml_cmeta_expr_compile_value(
        program, source, strlen(source), &root_data,
        fixture->compile_scopes, 2u, limits, diagnostic);
}

static vxml_status compile_fixture_condition(
    expression_fixture *fixture, vxml_cmeta_expr_program *program,
    const char *source, const vxml_cmeta_expr_limits *limits,
    vxml_cmeta_expr_diagnostic *diagnostic) {
    return vxml_cmeta_expr_compile_condition(
        program, source, strlen(source), &root_data,
        fixture->compile_scopes, 2u, limits, diagnostic);
}

spec("restricted VoiceXML CMeta scalar expressions") {
    it("evaluates a Boolean literal as a condition") {
        const vxml_cmeta_expr_limits limits = test_limits();
        const expression_root root = {.enabled = true};
        const unsigned char root_bound[ROOT_FIELD_COUNT] = {
            1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u};
        const vxml_cmeta_expr_runtime runtime = {
            .root = &root,
            .root_bound = root_bound,
            .root_bound_count = ROOT_FIELD_COUNT};
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_value_view value = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_equal(vxml_cmeta_expr_compile_condition(
                        &program, "true", 4u, &root_data,
                        NULL, 0u, &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &runtime, &value, &diagnostic),
                    VXML_OK);
        check_equal(value.kind, VXML_CMETA_VALUE_BOOL);
        check_true(value.data.boolean);
        vxml_cmeta_expr_program_destroy(&program);
    }

    it("honors arithmetic comparison equality and Boolean precedence") {
        static const char source[] =
            "!false && (2 + 3 * 4 == 14) && (9 / 3 == 3) && "
            "(10 % 4 == 2) && (5 - 2 >= 3) && (\"a\" < \"b\") && "
            "(2 != 3) && (2 <= 2) && (3 > 2) && (false || true)";
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_value_view value = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        check_equal(compile_fixture_condition(
                        &fixture, &program, source, &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_OK);
        check_equal(value.kind, VXML_CMETA_VALUE_BOOL);
        check_true(value.data.boolean);
        vxml_cmeta_expr_program_destroy(&program);
        expression_fixture_destroy(&fixture);
    }

    it("evaluates unsigned integer values without signed coercion") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_value_view value = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        check_equal(compile_fixture_value(
                        &fixture, &program, "9u - 4u", &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_program_value_kind(&program),
                    VXML_CMETA_VALUE_UINT);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_OK);
        check_equal(value.kind, VXML_CMETA_VALUE_UINT);
        check_equal(value.data.uint_value, UINT64_C(5));
        vxml_cmeta_expr_program_destroy(&program);
        expression_fixture_destroy(&fixture);
    }

    it("evaluates mixed integral and floating arithmetic as a float") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_value_view value = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        check_equal(compile_fixture_value(
                        &fixture, &program, "1.5 + 2", &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_OK);
        check_equal(value.kind, VXML_CMETA_VALUE_FLOAT);
        check_equal(value.data.number, 3.5);
        vxml_cmeta_expr_program_destroy(&program);
        expression_fixture_destroy(&fixture);
    }

    it("evaluates unary signed floating and identity operators") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_value_view value = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        check_equal(compile_fixture_value(
                        &fixture, &program, "-5 + +2", &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_OK);
        check_equal(value.kind, VXML_CMETA_VALUE_SINT);
        check_equal(value.data.sint, INT64_C(-3));
        vxml_cmeta_expr_program_destroy(&program);
        check_equal(compile_fixture_value(
                        &fixture, &program, "-1.5", &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_OK);
        check_equal(value.kind, VXML_CMETA_VALUE_FLOAT);
        check_equal(value.data.number, -1.5);
        vxml_cmeta_expr_program_destroy(&program);
        expression_fixture_destroy(&fixture);
    }

    it("rejects a non-Boolean condition at compilation") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        check_equal(compile_fixture_condition(
                        &fixture, &program, "1 + 2", &limits, &diagnostic),
                    VXML_SEMANTIC_ERROR);
        check_null(program.impl);
        expression_fixture_destroy(&fixture);
    }

    it("reads a typed dotted application-root operand") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_value_view value = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        fixture.inner_declared[SLOT_NESTED] = 0u;
        fixture.outer_declared[SLOT_NESTED] = 0u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "nested.number + 1",
                        &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_OK);
        check_equal(value.kind, VXML_CMETA_VALUE_SINT);
        check_equal(value.data.sint, INT64_C(6));
        vxml_cmeta_expr_program_destroy(&program);
        expression_fixture_destroy(&fixture);
    }

    it("reads a typed dotted lexical-scope operand") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_value_view value = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        fixture.inner_declared[SLOT_NESTED] = 1u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "nested.number + 1",
                        &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_OK);
        check_equal(value.data.sint, INT64_C(42));
        vxml_cmeta_expr_program_destroy(&program);
        expression_fixture_destroy(&fixture);
    }

    it("selects the innermost currently declared name candidate") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_value_view value = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        check_equal(compile_fixture_value(
                        &fixture, &program, "number", &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_OK);
        check_equal(value.data.sint, INT64_C(11));
        fixture.inner_declared[SLOT_NUMBER] = 1u;
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_OK);
        check_equal(value.data.sint, INT64_C(22));
        fixture.inner_declared[SLOT_NUMBER] = 0u;
        fixture.outer_declared[SLOT_NUMBER] = 0u;
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_OK);
        check_equal(value.data.sint, INT64_C(7));
        vxml_cmeta_expr_program_destroy(&program);
        expression_fixture_destroy(&fixture);
    }

    it("propagates a declared undefined binding through a direct value") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_value_view value = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        fixture.inner_declared[SLOT_NUMBER] = 1u;
        cmeta_scope_view_clear_slot(&fixture.inner_storage.view, SLOT_NUMBER);
        check_equal(compile_fixture_value(
                        &fixture, &program, "number", &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_program_value_kind(&program),
                    VXML_CMETA_VALUE_SINT);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_OK);
        check_equal(value.kind, VXML_CMETA_VALUE_UNDEFINED);
        vxml_cmeta_expr_program_destroy(&program);
        expression_fixture_destroy(&fixture);
    }

    it("propagates an undefined application binding through a direct value") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_value_view value = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        fixture.inner_declared[SLOT_NUMBER] = 0u;
        fixture.outer_declared[SLOT_NUMBER] = 0u;
        fixture.root_bound[ROOT_NUMBER] = 0u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "number", &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_OK);
        check_equal(value.kind, VXML_CMETA_VALUE_UNDEFINED);
        vxml_cmeta_expr_program_destroy(&program);
        expression_fixture_destroy(&fixture);
    }

    it("reports a semantic error when an operator receives undefined") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_value_view value = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        fixture.inner_declared[SLOT_NUMBER] = 1u;
        cmeta_scope_view_clear_slot(&fixture.inner_storage.view, SLOT_NUMBER);
        check_equal(compile_fixture_value(
                        &fixture, &program, "number + 1",
                        &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_SEMANTIC_ERROR);
        vxml_cmeta_expr_program_destroy(&program);
        expression_fixture_destroy(&fixture);
    }

    it("reports a semantic error when a condition is undefined") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_value_view value = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        fixture.inner_declared[SLOT_ENABLED] = 1u;
        cmeta_scope_view_clear_slot(&fixture.inner_storage.view, SLOT_ENABLED);
        check_equal(compile_fixture_condition(
                        &fixture, &program, "enabled", &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_SEMANTIC_ERROR);
        vxml_cmeta_expr_program_destroy(&program);
        expression_fixture_destroy(&fixture);
    }

    it("reports a semantic error when every lexical candidate is undeclared") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_value_view value = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        fixture.inner_declared[SLOT_LATE] = 0u;
        fixture.outer_declared[SLOT_LATE] = 0u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "late", &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_SEMANTIC_ERROR);
        vxml_cmeta_expr_program_destroy(&program);
        expression_fixture_destroy(&fixture);
    }

    it("rejects an overflowing signed integer literal") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        check_equal(compile_fixture_value(
                        &fixture, &program, "9223372036854775808",
                        &limits, &diagnostic),
                    VXML_SEMANTIC_ERROR);
        check_equal(diagnostic.byte_offset, (size_t)0u);
        check_null(program.impl);
        expression_fixture_destroy(&fixture);
    }

    it("reports a semantic error on checked arithmetic overflow") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_value_view value = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        check_equal(compile_fixture_value(
                        &fixture, &program,
                        "9223372036854775807 + 1", &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_SEMANTIC_ERROR);
        vxml_cmeta_expr_program_destroy(&program);
        check_equal(compile_fixture_value(
                        &fixture, &program,
                        "18446744073709551615u + 1u", &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_SEMANTIC_ERROR);
        vxml_cmeta_expr_program_destroy(&program);
        expression_fixture_destroy(&fixture);
    }

    it("owns immutable string literal bytes independently of source storage") {
        char source[] = "\"hello\"";
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_value_view value = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        check_equal(vxml_cmeta_expr_compile_value(
                        &program, source, sizeof(source) - 1u, &root_data,
                        fixture.compile_scopes, 2u, &limits, &diagnostic),
                    VXML_OK);
        memset(source, 'x', sizeof(source) - 1u);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_OK);
        check_equal(value.kind, VXML_CMETA_VALUE_STRING);
        check_equal(value.data.string.size, (size_t)5u);
        check_equal(memcmp(value.data.string.data, "hello", 5u), 0);
        vxml_cmeta_expr_program_destroy(&program);
        expression_fixture_destroy(&fixture);
    }

    it("uses bounded CMeta buffer reads for typed strings") {
        expression_fixture fixture;
        vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_value_view value = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        limits.max_string_bytes = 4u;
        text_read_calls = 0u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "label", &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_SEMANTIC_ERROR);
        check_equal(text_read_calls, (size_t)1u);
        vxml_cmeta_expr_program_destroy(&program);
        expression_fixture_destroy(&fixture);
    }

    it("accepts semantically equal scalar descriptors across translation units") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_value_view value = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        check_not_equal((uintptr_t)fixture.inner_schema.slots[SLOT_NUMBER].value,
                        (uintptr_t)root_fields[ROOT_NUMBER].value);
        check_true(cmeta_type_equal(
            fixture.inner_schema.slots[SLOT_NUMBER].value->storage_type,
            root_fields[ROOT_NUMBER].value->storage_type));
        fixture.inner_declared[SLOT_NUMBER] = 1u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "number", &limits, &diagnostic),
                    VXML_OK);
        check_equal(vxml_cmeta_expr_evaluate(
                        &program, &fixture.runtime, &value, &diagnostic),
                    VXML_OK);
        check_equal(value.data.sint, INT64_C(22));
        vxml_cmeta_expr_program_destroy(&program);
        expression_fixture_destroy(&fixture);
    }

    it("accepts exact source instruction operand and literal limits") {
        expression_fixture fixture;
        vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        limits.max_source_bytes = 3u;
        limits.max_instructions = 3u;
        limits.max_operands = 2u;
        limits.max_literal_bytes = 2u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "1+2", &limits, &diagnostic),
                    VXML_OK);
        vxml_cmeta_expr_program_destroy(&program);
        limits.max_source_bytes = 2u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "1+2", &limits, &diagnostic),
                    VXML_LIMIT_EXCEEDED);
        limits = test_limits();
        limits.max_instructions = 2u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "1+2", &limits, &diagnostic),
                    VXML_LIMIT_EXCEEDED);
        limits = test_limits();
        limits.max_operands = 1u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "1+2", &limits, &diagnostic),
                    VXML_LIMIT_EXCEEDED);
        limits = test_limits();
        limits.max_literal_bytes = 1u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "1+2", &limits, &diagnostic),
                    VXML_LIMIT_EXCEEDED);
        expression_fixture_destroy(&fixture);
    }

    it("accepts exact expression and dotted-path depth limits") {
        expression_fixture fixture;
        vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        limits.max_expression_depth = 2u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "!true", &limits, &diagnostic),
                    VXML_OK);
        vxml_cmeta_expr_program_destroy(&program);
        limits.max_expression_depth = 1u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "!true", &limits, &diagnostic),
                    VXML_LIMIT_EXCEEDED);
        limits = test_limits();
        limits.max_path_depth = 2u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "nested.number",
                        &limits, &diagnostic),
                    VXML_OK);
        vxml_cmeta_expr_program_destroy(&program);
        limits.max_path_depth = 1u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "nested.number",
                        &limits, &diagnostic),
                    VXML_LIMIT_EXCEEDED);
        expression_fixture_destroy(&fixture);
    }

    it("accepts exact literal and runtime string byte limits") {
        expression_fixture fixture;
        vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        limits.max_literal_bytes = 3u;
        limits.max_string_bytes = 3u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "\"abc\"", &limits, &diagnostic),
                    VXML_OK);
        vxml_cmeta_expr_program_destroy(&program);
        limits.max_literal_bytes = 2u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "\"abc\"", &limits, &diagnostic),
                    VXML_LIMIT_EXCEEDED);
        limits.max_literal_bytes = 3u;
        limits.max_string_bytes = 2u;
        check_equal(compile_fixture_value(
                        &fixture, &program, "\"abc\"", &limits, &diagnostic),
                    VXML_LIMIT_EXCEEDED);
        expression_fixture_destroy(&fixture);
    }

    it("rejects SCXML system names even when typed root fields exist") {
        static const char *const names[] = {
            "_event", "_name", "_sessionid", "_ioprocessors"};
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        size_t index;

        check_true(expression_fixture_init(&fixture));
        for (index = 0u; index < sizeof(names) / sizeof(names[0]); ++index) {
            vxml_cmeta_expr_program program = {0};
            vxml_cmeta_expr_diagnostic diagnostic;
            check_equal(compile_fixture_value(
                            &fixture, &program, names[index],
                            &limits, &diagnostic),
                        VXML_SEMANTIC_ERROR);
            check_null(program.impl);
        }
        expression_fixture_destroy(&fixture);
    }

    it("rejects SCXML state and binding helper functions") {
        static const char *const expressions[] = {
            "In(\"state\")", "is_bound(number)", "isBound(number)"};
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        size_t index;

        check_true(expression_fixture_init(&fixture));
        for (index = 0u;
             index < sizeof(expressions) / sizeof(expressions[0]); ++index) {
            vxml_cmeta_expr_program program = {0};
            vxml_cmeta_expr_diagnostic diagnostic;
            check_equal(compile_fixture_value(
                            &fixture, &program, expressions[index],
                            &limits, &diagnostic),
                        VXML_SEMANTIC_ERROR);
            check_null(program.impl);
        }
        expression_fixture_destroy(&fixture);
    }

    it("rejects runtime-missing dotted paths during compilation") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        check_equal(compile_fixture_value(
                        &fixture, &program, "nested.missing",
                        &limits, &diagnostic),
                    VXML_SEMANTIC_ERROR);
        check_null(program.impl);
        expression_fixture_destroy(&fixture);
    }

    it("does not admit an external evaluation form") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        check_equal(compile_fixture_value(
                        &fixture, &program, "external(\"value\")",
                        &limits, &diagnostic),
                    VXML_SEMANTIC_ERROR);
        check_null(program.impl);
        expression_fixture_destroy(&fixture);
    }

    it("rejects a structured terminal instead of treating it as a scalar") {
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        vxml_cmeta_expr_program program = {0};
        vxml_cmeta_expr_diagnostic diagnostic;

        check_true(expression_fixture_init(&fixture));
        check_equal(compile_fixture_value(
                        &fixture, &program, "nested", &limits, &diagnostic),
                    VXML_SEMANTIC_ERROR);
        check_null(program.impl);
        expression_fixture_destroy(&fixture);
    }
}

typedef struct allocation_probe {
    size_t calls;
    size_t fail_on_call;
    size_t live;
} allocation_probe;

static allocation_probe allocation_state;

static void *probe_malloc(size_t size) {
    void *pointer;
    ++allocation_state.calls;
    if (allocation_state.calls == allocation_state.fail_on_call) return NULL;
    pointer = malloc(size);
    if (pointer != NULL) ++allocation_state.live;
    return pointer;
}

static void *probe_calloc(size_t count, size_t size) {
    void *pointer;
    ++allocation_state.calls;
    if (allocation_state.calls == allocation_state.fail_on_call) return NULL;
    pointer = calloc(count, size);
    if (pointer != NULL) ++allocation_state.live;
    return pointer;
}

static void *probe_realloc(void *old_pointer, size_t size) {
    void *pointer;
    ++allocation_state.calls;
    if (allocation_state.calls == allocation_state.fail_on_call) return NULL;
    pointer = realloc(old_pointer, size);
    if (pointer != NULL && old_pointer == NULL) ++allocation_state.live;
    return pointer;
}

static void probe_free(void *pointer) {
    if (pointer == NULL) return;
    free(pointer);
    --allocation_state.live;
}

spec("restricted VoiceXML CMeta expression ownership") {
    it("destroys every allocation after each partial compile failure") {
        const vxml_test_allocator allocator = {
            probe_malloc, probe_calloc, probe_realloc, probe_free};
        expression_fixture fixture;
        const vxml_cmeta_expr_limits limits = test_limits();
        bool reached_success = false;
        size_t fail_on_call;

        check_true(expression_fixture_init(&fixture));
        vxml_test_allocator_set(&allocator);
        for (fail_on_call = 1u; fail_on_call <= 16u; ++fail_on_call) {
            vxml_cmeta_expr_program program = {0};
            vxml_cmeta_expr_diagnostic diagnostic;
            vxml_status status;
            memset(&allocation_state, 0, sizeof(allocation_state));
            allocation_state.fail_on_call = fail_on_call;
            status = compile_fixture_value(
                &fixture, &program, "number + 1", &limits, &diagnostic);
            if (status == VXML_OK) {
                reached_success = true;
                check_true(allocation_state.live != 0u);
                vxml_cmeta_expr_program_destroy(&program);
                vxml_cmeta_expr_program_destroy(&program);
                check_equal(allocation_state.live, (size_t)0u);
                break;
            }
            check_equal(status, VXML_ALLOCATION_FAILED);
            check_null(program.impl);
            check_equal(allocation_state.live, (size_t)0u);
        }
        vxml_test_allocator_reset();
        check_true(reached_success);
        expression_fixture_destroy(&fixture);
    }
}
