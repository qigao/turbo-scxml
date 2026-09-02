#include "scxml_expr.h"
#include "scxml_assign.h"

#include <scxml/scxml.h>
#include <cmeta/data.h>
#include "tinytest.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

Enum(scxml_expr_stage,
    (SCXML_EXPR_IDLE, 1, "idle"),
    (SCXML_EXPR_READY, 2, "ready")
);

Struct(scxml_expr_order,
    (bool, ready),
    (int, count),
    (size_t, total),
    (double, ratio),
    (scxml_expr_stage, stage)
);

typedef struct scxml_expr_text {
    const unsigned char *data;
    size_t size;
} scxml_expr_text;

Struct(scxml_expr_root,
    (bool, enabled),
    (scxml_expr_order, order),
    (scxml_expr_text, label)
);

static const cmeta_type_identity order_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.expr.order");
static const cmeta_type_desc order_type = {
    .name = "scxml_expr_order",
    .size = sizeof(scxml_expr_order),
    .align = _Alignof(scxml_expr_order),
    .kind = CMETA_T_OBJECT,
    .identity = &order_identity
};

static const cmeta_type_identity root_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.expr.root");
static const cmeta_type_desc root_type = {
    .name = "scxml_expr_root",
    .size = sizeof(scxml_expr_root),
    .align = _Alignof(scxml_expr_root),
    .kind = CMETA_T_OBJECT,
    .identity = &root_identity
};

static const cmeta_type_identity stage_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.expr.stage");
static const cmeta_type_desc stage_type = {
    .name = "scxml_expr_stage",
    .size = sizeof(scxml_expr_stage),
    .align = _Alignof(scxml_expr_stage),
    .kind = CMETA_T_INTEGER,
    .identity = &stage_identity
};

static const cmeta_type_identity text_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.expr.text");
static const cmeta_type_desc text_type = {
    .name = "scxml_expr_text",
    .size = sizeof(scxml_expr_text),
    .align = _Alignof(scxml_expr_text),
    .kind = CMETA_T_OBJECT,
    .identity = &text_identity
};

static bool text_is_zero(const void *object) {
    const scxml_expr_text *text = (const scxml_expr_text *)object;
    return text != NULL && text->data == NULL && text->size == 0u;
}

static cmeta_status text_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
    const scxml_expr_text *text = (const scxml_expr_text *)object;
    if (text == NULL || out_data == NULL || out_size == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (text->size != 0u && text->data == NULL)
        return CMETA_CALLBACK_ERROR;
    *out_data = text->data;
    *out_size = text->size;
    return CMETA_OK;
}

static cmeta_status text_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
    scxml_expr_text *text = (scxml_expr_text *)object;
    if (text == NULL || (size != 0u && data == NULL))
        return CMETA_INVALID_ARGUMENT;
    if (size > max_bytes)
        return CMETA_CAPACITY_EXCEEDED;
    text->data = data;
    text->size = size;
    return CMETA_OK;
}

static void text_restore_zero(void *object) {
    scxml_expr_text *text = (scxml_expr_text *)object;
    if (text != NULL) {
        text->data = NULL;
        text->size = 0u;
    }
}

static const cmeta_data_buffer_shape text_shape = {
    .ownership = CMETA_DATA_BUFFER_BORROWED
};

static const cmeta_data_buffer_ops text_ops = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &text_type,
    .ownership = CMETA_DATA_BUFFER_BORROWED,
    .is_zero = text_is_zero,
    .assign = text_assign,
    .restore_zero = text_restore_zero,
    .read = text_read
};

static const cmeta_data_desc text_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.expr.text.data",
    .display_name = "Text",
    .kind = CMETA_DATA_STRING,
    .storage_type = &text_type,
    .shape = &text_shape,
    .buffer_ops = &text_ops
};

static bool stage_is_zero(const void *object) {
    scxml_expr_stage value;
    memcpy(&value, object, sizeof(value));
    return value == 0;
}

static cmeta_status stage_read(const void *object, int64_t *out) {
    scxml_expr_stage value;
    if (object == NULL || out == NULL) return CMETA_INVALID_ARGUMENT;
    memcpy(&value, object, sizeof(value));
    *out = (int64_t)value;
    return CMETA_OK;
}

static cmeta_status stage_assign(void *object, int64_t value) {
    scxml_expr_stage native = (scxml_expr_stage)value;
    if (object == NULL) return CMETA_INVALID_ARGUMENT;
    memcpy(object, &native, sizeof(native));
    return CMETA_OK;
}

static void stage_restore_zero(void *object) {
    scxml_expr_stage value = (scxml_expr_stage)0;
    if (object != NULL) memcpy(object, &value, sizeof(value));
}

static const cmeta_data_enum_shape stage_shape = {
    .meta = EnumMeta(scxml_expr_stage)
};

static const cmeta_data_enum_ops stage_ops = {
    .struct_size = sizeof(cmeta_data_enum_ops),
    .abi_version = CMETA_DATA_ENUM_OPS_ABI_VERSION,
    .storage_type = &stage_type,
    .is_zero = stage_is_zero,
    .read = stage_read,
    .assign = stage_assign,
    .restore_zero = stage_restore_zero
};

static const cmeta_data_desc stage_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.expr.stage.data",
    .display_name = "Stage",
    .kind = CMETA_DATA_ENUM,
    .storage_type = &stage_type,
    .shape = &stage_shape,
    .enum_ops = &stage_ops
};

static const cmeta_data_field_desc order_fields[] = {
    {"test.scxml.expr.order.ready", "ready",
     offsetof(scxml_expr_order, ready), &cmeta_data_bool},
    {"test.scxml.expr.order.count", "count",
     offsetof(scxml_expr_order, count), &cmeta_data_int},
    {"test.scxml.expr.order.total", "total",
     offsetof(scxml_expr_order, total), &cmeta_data_size},
    {"test.scxml.expr.order.ratio", "ratio",
     offsetof(scxml_expr_order, ratio), &cmeta_data_double},
    {"test.scxml.expr.order.stage", "stage",
     offsetof(scxml_expr_order, stage), &stage_data}
};

static const cmeta_data_struct_shape order_shape = {
    .layout = StructMeta(scxml_expr_order),
    .fields = order_fields,
    .field_count = sizeof(order_fields) / sizeof(order_fields[0])
};

static const cmeta_data_desc order_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.expr.order.data",
    .display_name = "Order",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &order_type,
    .shape = &order_shape
};

static const cmeta_data_field_desc root_fields[] = {
    {"test.scxml.expr.root.enabled", "enabled",
     offsetof(scxml_expr_root, enabled), &cmeta_data_bool},
    {"test.scxml.expr.root.order", "order",
     offsetof(scxml_expr_root, order), &order_data},
    {"test.scxml.expr.root.label", "label",
     offsetof(scxml_expr_root, label), &text_data}
};

static const cmeta_data_struct_shape root_shape = {
    .layout = StructMeta(scxml_expr_root),
    .fields = root_fields,
    .field_count = sizeof(root_fields) / sizeof(root_fields[0])
};

static const cmeta_data_desc root_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.expr.root.data",
    .display_name = "Root",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &root_type,
    .shape = &root_shape
};

static const cmeta_data_field_desc event_order_fields[] = {
    {"test.scxml.expr.event.order", "order",
     offsetof(scxml_expr_root, order), &order_data}
};

static const cmeta_data_struct_shape event_order_shape = {
    .layout = StructMeta(scxml_expr_root),
    .fields = event_order_fields,
    .field_count = sizeof(event_order_fields) / sizeof(event_order_fields[0])
};

static const cmeta_data_desc event_order_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.expr.event.order.data",
    .display_name = "OrderEvent",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &root_type,
    .shape = &event_order_shape
};

typedef struct state_fixture {
    bool fail_active;
} state_fixture;

static bool resolve_state(void *user, const char *name, size_t size,
                          cflow_machine_state_id *out) {
    (void)user;
    if (name == NULL || out == NULL) return false;
    if (size == 6u && memcmp(name, "active", size) == 0) {
        *out = 7u;
        return true;
    }
    if (size == 5u && memcmp(name, "other", size) == 0) {
        *out = 9u;
        return true;
    }
    return false;
}

static bool state_is_active(void *user, cflow_machine_state_id state,
                            bool *out_active) {
    state_fixture *fixture = (state_fixture *)user;
    if (fixture == NULL || out_active == NULL || fixture->fail_active)
        return false;
    *out_active = state == 7u;
    return true;
}

static bool data_is_never_bound(
    void *user, size_t offset, size_t storage_size, bool *out_bound) {
    (void)user;
    (void)offset;
    if (storage_size == 0u || out_bound == NULL) return false;
    *out_bound = false;
    return true;
}

static scxml_expr_status compile_expression(
    scxml_expr_program *program, const char *source,
    const scxml_expr_limits *limits,
    scxml_expr_diagnostic *diagnostic) {
    return scxml_expr_compile(
        program, source, strlen(source), &root_data,
        resolve_state, NULL, limits, diagnostic);
}

static scxml_expr_status compile_value_expression(
    scxml_expr_program *program, const char *source,
    scxml_expr_diagnostic *diagnostic) {
    return scxml_expr_compile_value(
        program, source, strlen(source), &root_data,
        resolve_state, NULL, NULL, diagnostic);
}

static bool evaluate_expression(const char *source,
                                const scxml_expr_root *root) {
    scxml_expr_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    state_fixture states = {false};
    bool result = false;
    check_equal(compile_expression(&program, source, NULL, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate(
                    &program, root, state_is_active, &states,
                    &result, &diagnostic),
                SCXML_EXPR_OK);
    scxml_expr_program_destroy(&program);
    return result;
}

static scxml_expr_status evaluate_value_expression(
    const char *source, const scxml_expr_root *root,
    scxml_expr_value *out_value, scxml_expr_diagnostic *diagnostic) {
    scxml_expr_program program = {0};
    state_fixture states = {false};
    scxml_expr_status status = compile_value_expression(
        &program, source, diagnostic);
    if (status == SCXML_EXPR_OK) {
        status = scxml_expr_evaluate_value(
            &program, root, state_is_active, &states,
            out_value, diagnostic);
    }
    scxml_expr_program_destroy(&program);
    return status;
}

spec("TurboSCXML private SCXML expressions") {
  static scxml_expr_root root;

  before_each() {
    static const unsigned char label[] = {'r', 'e', 'a', 'd', 'y', 0, 'x'};
    memset(&root, 0, sizeof(root));
    root.enabled = true;
    root.order.ready = true;
    root.order.count = -3;
    root.order.total = SIZE_MAX;
    root.order.ratio = 1.5;
    root.order.stage = SCXML_EXPR_READY;
    root.label.data = label;
    root.label.size = 5u;
  }

  it("exposes finite nonzero default limits") {
    const scxml_expr_limits limits =
        scxml_expr_default_limits();
    check_true(limits.max_source_bytes > 0u);
    check_true(limits.max_instructions > 0u);
    check_true(limits.max_operands > 0u);
    check_true(limits.max_expression_depth > 0u);
    check_true(limits.max_path_depth > 0u);
    check_true(limits.max_literal_bytes > 0u);
    check_true(limits.max_string_bytes > 0u);
  }

  it("rejects invalid compile arguments without publishing a program") {
    static const char expression[] = "true";
    scxml_expr_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    scxml_expr_limits limits =
        scxml_expr_default_limits();

    check_equal(scxml_expr_compile(
                    NULL, expression, sizeof(expression) - 1u, &root_data,
                    resolve_state, NULL, NULL, &diagnostic),
                SCXML_EXPR_INVALID_ARGUMENT);
    check_equal(scxml_expr_compile(
                    &program, NULL, 0u, &root_data, resolve_state, NULL,
                    NULL, &diagnostic),
                SCXML_EXPR_INVALID_ARGUMENT);
    limits.max_operands = 0u;
    check_equal(scxml_expr_compile(
                    &program, expression, sizeof(expression) - 1u,
                    &root_data, resolve_state, NULL, &limits, &diagnostic),
                SCXML_EXPR_INVALID_ARGUMENT);
    check_null(program.impl);
  }

  it("evaluates literals precedence and active-state predicates") {
    check_true(evaluate_expression("true && !false", &root));
    check_true(evaluate_expression("false || true && true", &root));
    check_true(evaluate_expression("(false || true) && In(\"active\")",
                                   &root));
    check_false(evaluate_expression("In(\"other\") || false", &root));
  }

  it("reads nested reflected scalar and enum locations") {
    check_true(evaluate_expression("enabled && order.ready", &root));
    check_true(evaluate_expression("order.count == -3", &root));
    check_true(evaluate_expression("order.total > order.count", &root));
    check_true(evaluate_expression("order.ratio >= 1.5", &root));
    check_true(evaluate_expression("order.stage == 2", &root));
    check_true(evaluate_expression("-1 < 0u", &root));
    check_true(evaluate_expression("0u > -1", &root));
    check_true(evaluate_expression(
        "18446744073709551615u == 18446744073709551615u", &root));
    check_true(evaluate_expression("enabled == true", &root));
  }

  it("returns typed scalar values without weakening condition admission") {
    const char *sources[] = {
        "true", "order.count", "order.total", "order.ratio",
        "label", "order.stage"};
    const scxml_expr_value_kind kinds[] = {
        SCXML_EXPR_VALUE_BOOL,
        SCXML_EXPR_VALUE_SINT,
        SCXML_EXPR_VALUE_UINT,
        SCXML_EXPR_VALUE_FLOAT,
        SCXML_EXPR_VALUE_STRING,
        SCXML_EXPR_VALUE_SINT};
    size_t index;

    for (index = 0u; index < sizeof(sources) / sizeof(sources[0]); ++index) {
      scxml_expr_program program = {0};
      scxml_expr_diagnostic diagnostic = {0};
      scxml_expr_value value = {0};
      state_fixture states = {false};

      check_equal(compile_value_expression(
                      &program, sources[index], &diagnostic),
                  SCXML_EXPR_OK);
      check_equal(scxml_expr_program_value_kind(&program),
                  kinds[index]);
      check_equal(scxml_expr_evaluate_value(
                      &program, &root, state_is_active, &states,
                      &value, &diagnostic),
                  SCXML_EXPR_OK);
      check_equal(value.kind, kinds[index]);
      if (value.kind == SCXML_EXPR_VALUE_BOOL) {
        check_true(value.data.boolean);
      } else if (value.kind == SCXML_EXPR_VALUE_SINT) {
        check_equal(value.data.sint,
                    index == 1u ? INT64_C(-3) : INT64_C(2));
      } else if (value.kind == SCXML_EXPR_VALUE_UINT) {
        check_equal(value.data.uint, (uint64_t)SIZE_MAX);
      } else if (value.kind == SCXML_EXPR_VALUE_FLOAT) {
        check_equal(value.data.number, 1.5);
      } else if (value.kind == SCXML_EXPR_VALUE_STRING) {
        check_equal(value.data.string.size, (size_t)5u);
        check_equal(value.data.string.data, "ready", (size_t)5u);
      }
      scxml_expr_program_destroy(&program);
    }

    {
      scxml_expr_program program = {0};
      scxml_expr_diagnostic diagnostic = {0};
      check_equal(compile_expression(
                      &program, "order.count", NULL, &diagnostic),
                  SCXML_EXPR_TYPE_MISMATCH);
      check_null(program.impl);
    }
  }

  it("evaluates checked arithmetic with conventional precedence") {
    const char *sources[] = {
        "order.count + 5 * 2",
        "(order.count + 5) * 2",
        "10u - 3u",
        "order.ratio * 2 + order.count",
        "-order.count",
        "+order.ratio",
        "-7 / 3",
        "-7 % 3",
        "-9223372036854775808"};
    const scxml_expr_value_kind kinds[] = {
        SCXML_EXPR_VALUE_SINT,
        SCXML_EXPR_VALUE_SINT,
        SCXML_EXPR_VALUE_UINT,
        SCXML_EXPR_VALUE_FLOAT,
        SCXML_EXPR_VALUE_SINT,
        SCXML_EXPR_VALUE_FLOAT,
        SCXML_EXPR_VALUE_SINT,
        SCXML_EXPR_VALUE_SINT,
        SCXML_EXPR_VALUE_SINT};
    const int64_t signed_results[] = {
        INT64_C(7), INT64_C(4), INT64_C(0), INT64_C(0), INT64_C(3),
        INT64_C(0), INT64_C(-2), INT64_C(-1), INT64_MIN};
    size_t index;

    for (index = 0u; index < sizeof(sources) / sizeof(sources[0]); ++index) {
      scxml_expr_diagnostic diagnostic = {0};
      scxml_expr_value value = {0};
      check_equal(evaluate_value_expression(
                      sources[index], &root, &value, &diagnostic),
                  SCXML_EXPR_OK);
      check_equal(value.kind, kinds[index]);
      if (value.kind == SCXML_EXPR_VALUE_SINT)
        check_equal(value.data.sint, signed_results[index]);
      else if (value.kind == SCXML_EXPR_VALUE_UINT)
        check_equal(value.data.uint, UINT64_C(7));
      else if (value.kind == SCXML_EXPR_VALUE_FLOAT)
        check_true(fabs(value.data.number -
                        (index == 3u ? 0.0 : 1.5)) < 1e-12);
    }

    check_true(evaluate_expression(
        "order.count + 5 * 2 == 7 && -order.count == 3", &root));
    check_true(evaluate_expression(
        "order.ratio + 0.5 == 2.0 && (10u / 2u) == 5u", &root));
  }

  it("rejects arithmetic with incompatible static operand kinds") {
    const char *sources[] = {
        "1 + 2u",
        "2u - 1",
        "\"left\" + \"right\"",
        "-1u",
        "1.0 % 1.0",
        "true * 2"};
    size_t index;

    for (index = 0u; index < sizeof(sources) / sizeof(sources[0]); ++index) {
      scxml_expr_program program = {0};
      scxml_expr_diagnostic diagnostic = {0};
      check_equal(compile_value_expression(
                      &program, sources[index], &diagnostic),
                  SCXML_EXPR_TYPE_MISMATCH);
      check_null(program.impl);
    }
  }

  it("fails arithmetic faults without changing the output value") {
    const char *sources[] = {
        "9223372036854775807 + 1",
        "-9223372036854775808 - 1",
        "9223372036854775807 * 2",
        "18446744073709551615u + 1u",
        "0u - 1u",
        "1 / 0",
        "1u % 0u",
        "1.0 / 0.0",
        "1e308 * 1e308",
        "-(-9223372036854775808)"};
    size_t index;

    for (index = 0u; index < sizeof(sources) / sizeof(sources[0]); ++index) {
      scxml_expr_diagnostic diagnostic = {0};
      scxml_expr_value value = {
          .kind = SCXML_EXPR_VALUE_BOOL,
          .data.boolean = true};
      check_equal(evaluate_value_expression(
                      sources[index], &root, &value, &diagnostic),
                  SCXML_EXPR_EVALUATION_ERROR);
      check_equal(value.kind, SCXML_EXPR_VALUE_BOOL);
      check_true(value.data.boolean);
    }
  }

  it("resolves bounded read-only SCXML name and session strings") {
    static const char condition[] =
        "_name == \"Checkout\" && _sessionid != \"\"";
    static const char session_id[] =
        "00112233-4455-4677-8899-aabbccddeeff";
    const scxml_expr_system_values system_values = {
        .name = {"Checkout", 8u},
        .session_id = {session_id, sizeof(session_id) - 1u}
    };
    const scxml_expr_system_values missing_session = {
        .name = {"Checkout", 8u}
    };
    scxml_expr_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    scxml_expr_value value = {0};
    scxml_expr_limits limits =
        scxml_expr_default_limits();
    state_fixture states = {false};
    bool result = false;

    check_equal(compile_expression(
                    &program, condition, NULL, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate_with_system(
                    &program, &root, state_is_active, &states,
                    &system_values, &result, &diagnostic),
                SCXML_EXPR_OK);
    check_true(result);
    result = true;
    check_equal(scxml_expr_evaluate_with_system(
                    &program, &root, state_is_active, &states,
                    &missing_session, &result, &diagnostic),
                SCXML_EXPR_EVALUATION_ERROR);
    check_true(result);
    scxml_expr_program_destroy(&program);

    check_equal(compile_value_expression(
                    &program, "_sessionid", &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate_value_with_system(
                    &program, &root, state_is_active, &states,
                    &system_values, &value, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(value.kind, SCXML_EXPR_VALUE_STRING);
    check_equal(value.data.string.size, sizeof(session_id) - 1u);
    check_equal(value.data.string.data, session_id,
                sizeof(session_id) - 1u);
    scxml_expr_program_destroy(&program);

    limits.max_string_bytes = 3u;
    check_equal(compile_expression(
                    &program, "_name == \"abc\"", &limits, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate_with_system(
                    &program, &root, state_is_active, &states,
                    &system_values, &result, &diagnostic),
                SCXML_EXPR_EVALUATION_ERROR);
    scxml_expr_program_destroy(&program);
  }

  it("resolves only the bounded current SCXML event name") {
    static const scxml_ioprocessor_descriptor processors[] = {
        {
            .name = "scxml",
            .name_size = sizeof("scxml") - 1u,
            .type = "http://www.w3.org/TR/scxml/#SCXMLEventProcessor",
            .type_size =
                sizeof("http://www.w3.org/TR/scxml/#SCXMLEventProcessor") - 1u,
            .location = "#_scxml_session",
            .location_size = sizeof("#_scxml_session") - 1u
        },
        {
            .name = "basichttp",
            .name_size = sizeof("basichttp") - 1u,
            .type = "http://www.w3.org/TR/scxml/#BasicHTTPEventProcessor",
            .type_size = sizeof(
                "http://www.w3.org/TR/scxml/#BasicHTTPEventProcessor") - 1u,
            .location = "http://127.0.0.1:43123/scxml/session-a",
            .location_size = sizeof(
                "http://127.0.0.1:43123/scxml/session-a") - 1u
        }
    };
    const scxml_expr_system_values system_values = {
        .event_name = {"go", 2u},
        .event_type = {"external", 8u},
        .event_send_id = {"s1", 2u},
        .event_origin = {"peer", 4u},
        .event_origin_type = {"scxml", 5u},
        .event_invoke_id = {"job", 3u},
        .event_data = {"payload", 7u},
        .ioprocessors = processors,
        .ioprocessor_count = sizeof(processors) / sizeof(processors[0])
    };
    const scxml_expr_system_values missing_event = {0};
    scxml_expr_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    scxml_expr_value value = {0};
    state_fixture states = {false};
    bool result = false;

    check_equal(compile_expression(
                    &program, "_event.name == \"go\"", NULL, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate_with_system(
                    &program, &root, state_is_active, &states,
                    &system_values, &result, &diagnostic),
                SCXML_EXPR_OK);
    check_true(result);
    result = true;
    check_equal(scxml_expr_evaluate_with_system(
                    &program, &root, state_is_active, &states,
                    &missing_event, &result, &diagnostic),
                SCXML_EXPR_EVALUATION_ERROR);
    check_true(result);
    scxml_expr_program_destroy(&program);

    check_equal(compile_expression(
                    &program,
                    "_event.type == \"external\" && "
                    "_event.sendid == \"s1\" && "
                    "_event.origin == \"peer\" && "
                    "_event.origintype == \"scxml\" && "
                    "_event.invokeid == \"job\" && "
                    "_event.data == \"payload\" && "
                    "_ioprocessors.scxml.location == \"#_scxml_session\" && "
                    "_ioprocessors.basichttp.location == "
                    "\"http://127.0.0.1:43123/scxml/session-a\"",
                    NULL, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate_with_system(
                    &program, &root, state_is_active, &states,
                    &system_values, &result, &diagnostic),
                SCXML_EXPR_OK);
    check_true(result);
    scxml_expr_program_destroy(&program);

    check_equal(compile_value_expression(
                    &program, "_ioprocessors.vendor.location", &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate_value_with_system(
                    &program, &root, state_is_active, &states,
                    &system_values, &value, &diagnostic),
                SCXML_EXPR_UNKNOWN_LOCATION);
    scxml_expr_program_destroy(&program);

    check_equal(compile_value_expression(
                    &program, "_event.name", &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate_value_with_system(
                    &program, &root, state_is_active, &states,
                    &system_values, &value, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(value.kind, SCXML_EXPR_VALUE_STRING);
    check_equal(value.data.string.size, (size_t)2u);
    check_equal(value.data.string.data, "go", (size_t)2u);
    scxml_expr_program_destroy(&program);

    check_equal(compile_value_expression(
                    &program, "_event", &diagnostic),
                SCXML_EXPR_UNKNOWN_LOCATION);
    check_null(program.impl);
    check_equal(compile_value_expression(
                    &program, "_event.unknown", &diagnostic),
                SCXML_EXPR_UNKNOWN_LOCATION);
    check_null(program.impl);
  }

  it("reports whether the current event is bound") {
    const scxml_expr_system_values unbound_event = {0};
    const scxml_expr_system_values bound_event = {
        .event_name = {"go", 2u}
    };
    scxml_expr_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    state_fixture states = {false};
    bool result = true;

    check_equal(compile_expression(
                    &program, "isBound(_event)", NULL, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate_with_system(
                    &program, &root, state_is_active, &states,
                    &unbound_event, &result, &diagnostic),
                SCXML_EXPR_OK);
    check_false(result);
    check_equal(scxml_expr_evaluate_with_system(
                    &program, &root, state_is_active, &states,
                    &bound_event, &result, &diagnostic),
                SCXML_EXPR_OK);
    check_true(result);
    check_equal(scxml_expr_evaluate_with_system(
                    &program, &root, state_is_active, &states,
                    NULL, &result, &diagnostic),
                SCXML_EXPR_EVALUATION_ERROR);
    check_true(result);
    scxml_expr_program_destroy(&program);
  }

  it("reports required current Event field bindings") {
    static const char *const expressions[] = {
        "isBound(_event.name)",
        "isBound(_event.type)",
        "isBound(_event.sendid)",
        "isBound(_event.origin)",
        "isBound(_event.origintype)",
        "isBound(_event.invokeid)",
        "isBound(_event.data)"};
    const scxml_expr_system_values unbound_event = {
        .event_send_id = {"", 0u},
        .event_origin = {"", 0u},
        .event_origin_type = {"", 0u},
        .event_invoke_id = {"", 0u},
        .event_data = {"", 0u}};
    const scxml_expr_system_values bound_event = {
        .event_name = {"go", 2u},
        .event_type = {"external", sizeof("external") - 1u},
        .event_send_id = {"", 0u},
        .event_origin = {"", 0u},
        .event_origin_type = {"", 0u},
        .event_invoke_id = {"", 0u},
        .event_data = {NULL, 0u},
        .event_data_schema = &root_data,
        .event_data_object = &root};
    state_fixture states = {false};
    size_t index;

    for (index = 0u;
         index < sizeof(expressions) / sizeof(expressions[0]); ++index) {
      scxml_expr_program program = {0};
      scxml_expr_diagnostic diagnostic = {0};
      bool result = true;

      check_equal(compile_expression(
                      &program, expressions[index], NULL, &diagnostic),
                  SCXML_EXPR_OK);
      check_equal(scxml_expr_evaluate_with_system(
                      &program, &root, state_is_active, &states,
                      &unbound_event, &result, &diagnostic),
                  SCXML_EXPR_OK);
      check_false(result);
      check_equal(scxml_expr_evaluate_with_system(
                      &program, &root, state_is_active, &states,
                      &bound_event, &result, &diagnostic),
                  SCXML_EXPR_OK);
      check_true(result);

      if (index == 0u) {
        result = true;
        check_equal(scxml_expr_evaluate_with_system(
                        &program, &root, state_is_active, &states,
                        NULL, &result, &diagnostic),
                    SCXML_EXPR_EVALUATION_ERROR);
        check_true(result);
      }
      scxml_expr_program_destroy(&program);
    }
  }

  it("reports startup system-variable bindings") {
    static const char *const expressions[] = {
        "isBound(_name)",
        "isBound(_sessionid)",
        "isBound(_ioprocessors)"};
    const scxml_expr_system_values unbound = {0};
    const scxml_expr_system_values bound = {
        .name = {"", 0u},
        .session_id = {"session", sizeof("session") - 1u},
        .ioprocessors = (const scxml_ioprocessor_descriptor *)(uintptr_t)1u,
        .ioprocessor_count = 1u
    };
    state_fixture states = {false};
    size_t index;

    for (index = 0u;
         index < sizeof(expressions) / sizeof(expressions[0]); ++index) {
      scxml_expr_program program = {0};
      scxml_expr_diagnostic diagnostic = {0};
      bool result = true;

      check_equal(compile_expression(
                      &program, expressions[index], NULL, &diagnostic),
                  SCXML_EXPR_OK);
      check_equal(scxml_expr_evaluate_with_system(
                      &program, &root, state_is_active, &states,
                      &unbound, &result, &diagnostic),
                  SCXML_EXPR_OK);
      check_false(result);
      check_equal(scxml_expr_evaluate_with_system(
                      &program, &root, state_is_active, &states,
                      &bound, &result, &diagnostic),
                  SCXML_EXPR_OK);
      check_true(result);
      scxml_expr_program_destroy(&program);
    }

    {
      scxml_expr_program program = {0};
      scxml_expr_diagnostic diagnostic = {0};
      bool result = true;

      check_equal(compile_expression(
                      &program, "isBound(_sessionid)", NULL, &diagnostic),
                  SCXML_EXPR_OK);
      check_equal(scxml_expr_evaluate_with_system(
                      &program, &root, state_is_active, &states,
                      NULL, &result, &diagnostic),
                  SCXML_EXPR_EVALUATION_ERROR);
      check_true(result);
      scxml_expr_program_destroy(&program);
    }
  }

  it("reads structured current event data through the compiled CMeta schema") {
    scxml_expr_root event_data = root;
    scxml_expr_system_values system_values = {
        .event_name = {"structured", sizeof("structured") - 1u},
        .event_type = {"external", sizeof("external") - 1u},
        .event_send_id = {"", 0u},
        .event_origin = {"", 0u},
        .event_origin_type = {"", 0u},
        .event_invoke_id = {"", 0u},
        .event_data = {"", 0u},
        .event_data_schema = &root_data,
        .event_data_object = &event_data
    };
    scxml_expr_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    state_fixture states = {false};
    bool result = false;

    event_data.order.count = 42;
    event_data.order.ready = false;
    check_equal(compile_expression(
                    &program,
                    "_event.data.order.count == 42 && "
                    "_event.data.order.ready == false",
                    NULL, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate_with_system(
                    &program, &root, state_is_active, &states,
                    &system_values, &result, &diagnostic),
                SCXML_EXPR_OK);
    check_true(result);
    system_values.event_data_schema = NULL;
    system_values.event_data_object = NULL;
    check_equal(scxml_expr_evaluate_with_system(
                    &program, &root, state_is_active, &states,
                    &system_values, &result, &diagnostic),
                SCXML_EXPR_EVALUATION_ERROR);
    scxml_expr_program_destroy(&program);
  }

  it("reads only paths admitted by a compatible event data subset schema") {
    scxml_expr_root event_data = root;
    scxml_expr_system_values system_values = {
        .event_name = {"completion", sizeof("completion") - 1u},
        .event_type = {"internal", sizeof("internal") - 1u},
        .event_data = {NULL, 0u},
        .event_data_schema = &event_order_data,
        .event_data_object = &event_data
    };
    scxml_expr_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    state_fixture states = {false};
    bool result = false;

    event_data.order.count = 42;
    check_equal(compile_expression(
                    &program, "_event.data.order.count == 42",
                    NULL, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate_with_system(
                    &program, &root, state_is_active, &states,
                    &system_values, &result, &diagnostic),
                SCXML_EXPR_OK);
    check_true(result);
    scxml_expr_program_destroy(&program);

    check_equal(compile_expression(
                    &program, "_event.data.enabled == true",
                    NULL, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate_with_system(
                    &program, &root, state_is_active, &states,
                    &system_values, &result, &diagnostic),
                SCXML_EXPR_EVALUATION_ERROR);
    scxml_expr_program_destroy(&program);
  }

  it("applies exact reflected assignments and preserves destinations on failure") {
    scxml_assign_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    state_fixture states = {false};

    check_equal(scxml_assign_compile(
                    &program, "order.count", 11u, "9", 1u,
                    &root_data, resolve_state, NULL, NULL,
                    SCXML_ASSIGN_LOCATION_STRICT, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_assign_apply(
                    &program, &root, state_is_active, &states, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(root.order.count, 9);
    scxml_assign_program_destroy(&program);

    check_equal(scxml_assign_compile(
                    &program, "order.count", 11u, "order.ratio", 11u,
                    &root_data, resolve_state, NULL, NULL,
                    SCXML_ASSIGN_LOCATION_STRICT, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_assign_apply(
                    &program, &root, state_is_active, &states, &diagnostic),
                SCXML_EXPR_TYPE_MISMATCH);
    check_equal(root.order.count, 9);
    scxml_assign_program_destroy(&program);

    check_equal(scxml_assign_compile(
                    &program, "label", 5u, "label", 5u,
                    &root_data, resolve_state, NULL, NULL,
                    SCXML_ASSIGN_LOCATION_STRICT, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_assign_apply(
                    &program, &root, state_is_active, &states, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(root.label.size, (size_t)5u);
    check_equal(root.label.data, "ready", (size_t)5u);
    scxml_assign_program_destroy(&program);

    check_equal(scxml_assign_compile(
                    &program, "order.ready", 11u, "1", 1u,
                    &root_data, resolve_state, NULL, NULL,
                    SCXML_ASSIGN_LOCATION_STRICT, &diagnostic),
                SCXML_EXPR_TYPE_MISMATCH);
    check_null(program.impl);
  }

  it("evaluates assignments from an immutable source into a separate destination") {
    scxml_expr_root source = root;
    scxml_expr_root destination = root;
    scxml_assign_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    state_fixture states = {false};

    source.order.count = 17;
    destination.order.count = -4;
    check_equal(scxml_assign_compile(
                    &program, "order.count", 11u, "order.count", 11u,
                    &root_data, resolve_state, NULL, NULL,
                    SCXML_ASSIGN_LOCATION_STRICT, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_assign_apply_from_with_system(
                    &program, &source, &destination,
                    state_is_active, &states, NULL, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(source.order.count, 17);
    check_equal(destination.order.count, 17);
    scxml_assign_program_destroy(&program);
  }

  it("defers unresolved executable assignment locations without mutating state") {
    const scxml_expr_root before = root;
    scxml_assign_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    state_fixture states = {false};

    check_equal(scxml_assign_compile(
                    &program, "missing", sizeof("missing") - 1u,
                    "1 +", sizeof("1 +") - 1u, &root_data,
                    resolve_state, NULL, NULL,
                    SCXML_ASSIGN_LOCATION_RUNTIME, &diagnostic),
                SCXML_EXPR_SYNTAX_ERROR);
    check_null(program.impl);

    check_equal(scxml_assign_compile(
                    &program, "missing", sizeof("missing") - 1u,
                    "9", sizeof("9") - 1u, &root_data,
                    resolve_state, NULL, NULL,
                    SCXML_ASSIGN_LOCATION_STRICT, &diagnostic),
                SCXML_EXPR_UNKNOWN_LOCATION);
    check_null(program.impl);

    check_equal(scxml_assign_compile(
                    &program, "missing", sizeof("missing") - 1u,
                    "_event.data.order.count",
                    sizeof("_event.data.order.count") - 1u, &root_data,
                    resolve_state, NULL, NULL,
                    SCXML_ASSIGN_LOCATION_RUNTIME, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_assign_apply(
                    &program, &root, state_is_active, &states, &diagnostic),
                SCXML_EXPR_UNKNOWN_LOCATION);
    check_equal(&root, &before, sizeof(root));
    scxml_assign_program_destroy(&program);
  }

  it("rejects corrupt assignment schemas under the runtime location policy") {
    cmeta_data_desc invalid_order = order_data;
    cmeta_data_field_desc invalid_fields[
        sizeof(root_fields) / sizeof(root_fields[0])];
    cmeta_data_struct_shape invalid_shape = root_shape;
    cmeta_data_desc invalid_root = root_data;
    scxml_assign_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};

    memcpy(invalid_fields, root_fields, sizeof(invalid_fields));
    invalid_order.storage_type = NULL;
    invalid_fields[1].value = &invalid_order;
    invalid_shape.fields = invalid_fields;
    invalid_root.shape = &invalid_shape;
    check_true(cmeta_data_desc_valid(&invalid_root));
    check_equal(scxml_assign_compile(
                    &program, "order.count", sizeof("order.count") - 1u,
                    "9", sizeof("9") - 1u, &invalid_root,
                    resolve_state, NULL, NULL,
                    SCXML_ASSIGN_LOCATION_RUNTIME, &diagnostic),
                SCXML_EXPR_INVALID_ARGUMENT);
    check_null(program.impl);
  }

  it("executes recognized system assignments as read-only failures") {
    const scxml_expr_system_values system_values = {
        .event_name = {"go", sizeof("go") - 1u},
        .event_type = {"external", sizeof("external") - 1u}
    };
    const scxml_expr_root before = root;
    scxml_assign_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    state_fixture states = {false};
    scxml_expr_status status;

    status = scxml_assign_compile(
        &program, "_event", sizeof("_event") - 1u,
        "true", sizeof("true") - 1u,
        &root_data, resolve_state, NULL, NULL,
        SCXML_ASSIGN_LOCATION_STRICT, &diagnostic);
    check_equal(status, SCXML_EXPR_OK);
    if (status == SCXML_EXPR_OK) {
      check_equal(scxml_assign_apply_with_system(
                      &program, &root, state_is_active, &states,
                      &system_values, &diagnostic),
                  SCXML_EXPR_EVALUATION_ERROR);
      scxml_assign_program_destroy(&program);
    }
    check_equal(&root, &before, sizeof(root));
  }

  it("compares borrowed string locations with byte-exact semantics") {
    check_true(evaluate_expression("label == \"ready\"", &root));
    check_true(evaluate_expression("label != \"idle\"", &root));
    check_true(evaluate_expression("label > \"read\"", &root));
    root.label.size = 7u;
    check_false(evaluate_expression("label == \"ready\"", &root));
    root.label.data = NULL;
    root.label.size = 0u;
    check_true(evaluate_expression("label == \"\"", &root));
  }

  it("retains compiled string literals independently of source") {
    char source[] = "label == \"ready\"";
    scxml_expr_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    state_fixture states = {false};
    bool result = false;
    check_equal(compile_expression(&program, source, NULL, &diagnostic),
                SCXML_EXPR_OK);
    memset(source, 'x', sizeof(source) - 1u);
    check_equal(scxml_expr_evaluate(
                    &program, &root, state_is_active, &states,
                    &result, &diagnostic),
                SCXML_EXPR_OK);
    check_true(result);
    scxml_expr_program_destroy(&program);
  }

  it("rejects string locations without a borrowed read trait") {
    static const char expression[] = "label == \"ready\"";
    cmeta_data_buffer_ops unreadable_ops = text_ops;
    cmeta_data_desc unreadable_text = text_data;
    cmeta_data_field_desc unreadable_field = root_fields[2];
    cmeta_data_struct_shape unreadable_shape = root_shape;
    cmeta_data_desc unreadable_root = root_data;
    scxml_expr_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};

    unreadable_ops.read = NULL;
    unreadable_text.buffer_ops = &unreadable_ops;
    unreadable_field.value = &unreadable_text;
    unreadable_shape.fields = &unreadable_field;
    unreadable_shape.field_count = 1u;
    unreadable_root.shape = &unreadable_shape;

    check_equal(scxml_expr_compile(
                    &program, expression, sizeof(expression) - 1u,
                    &unreadable_root, resolve_state, NULL, NULL,
                    &diagnostic),
                SCXML_EXPR_TYPE_MISMATCH);
    check_null(program.impl);
  }

  it("preserves output when a borrowed string exceeds its runtime limit") {
    scxml_expr_limits limits =
        scxml_expr_default_limits();
    scxml_expr_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    state_fixture states = {false};
    bool result = true;

    limits.max_string_bytes = 4u;
    check_equal(compile_expression(&program, "label == \"ready\"", &limits,
                                   &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate(
                    &program, &root, state_is_active, &states,
                    &result, &diagnostic),
                SCXML_EXPR_EVALUATION_ERROR);
    check_true(result);
    scxml_expr_program_destroy(&program);
  }

  it("defines unordered floating point comparisons") {
    root.order.ratio = NAN;
    check_false(evaluate_expression("order.ratio == order.ratio", &root));
    check_true(evaluate_expression("order.ratio != order.ratio", &root));
    check_false(evaluate_expression("order.ratio < 2.0", &root));
  }

  it("compares floating point and 64-bit integers without narrowing") {
    check_true(evaluate_expression(
        "9223372036854775807 < 9223372036854775808.0", &root));
    check_true(evaluate_expression(
        "9223372036854775808.0 > 9223372036854775807", &root));
    check_true(evaluate_expression(
        "18446744073709551615u < 18446744073709551616.0", &root));
    check_true(evaluate_expression(
        "18446744073709551615u != 18446744073709551616.0", &root));
    check_true(evaluate_expression(
        "-9223372036854775808 == -9223372036854775808.0", &root));
    check_true(evaluate_expression("0u == -0.0", &root));
  }

  it("retains immutable compiled operands independently of source") {
    char source[] = "order.count == -3";
    scxml_expr_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    state_fixture states = {false};
    bool result = false;
    check_equal(compile_expression(&program, source, NULL, &diagnostic),
                SCXML_EXPR_OK);
    memset(source, 'x', sizeof(source) - 1u);
    check_equal(scxml_expr_evaluate(
                    &program, &root, state_is_active, &states,
                    &result, &diagnostic),
                SCXML_EXPR_OK);
    check_true(result);
    scxml_expr_program_destroy(&program);
  }

  it("rejects unknown paths nonboolean conditions and malformed syntax") {
    static const char *const invalid[] = {
        "order.missing == 1", "order.count", "true &&", "In(active)",
        "In(\"missing\")", "label == 1", "isBound()",
        "isBound(_event.unknown)", "isBound(_event.data.sequence)",
        "isBound(other)", "isBound(_ioprocessors.scxml)"};
    static const scxml_expr_status expected[] = {
        SCXML_EXPR_UNKNOWN_LOCATION,
        SCXML_EXPR_TYPE_MISMATCH,
        SCXML_EXPR_SYNTAX_ERROR,
        SCXML_EXPR_SYNTAX_ERROR,
        SCXML_EXPR_UNKNOWN_LOCATION,
        SCXML_EXPR_TYPE_MISMATCH,
        SCXML_EXPR_SYNTAX_ERROR,
        SCXML_EXPR_SYNTAX_ERROR,
        SCXML_EXPR_SYNTAX_ERROR,
        SCXML_EXPR_SYNTAX_ERROR,
        SCXML_EXPR_SYNTAX_ERROR};
    size_t index;
    for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        scxml_expr_program program = {0};
        scxml_expr_diagnostic diagnostic = {0};
        check_equal(compile_expression(&program, invalid[index], NULL,
                                       &diagnostic), expected[index]);
        check_null(program.impl);
        check_equal(diagnostic.status, expected[index]);
        check_true(diagnostic.message[0] != '\0');
    }
  }

  it("reports unbound and runtime-missing paths with one status") {
    static const char missing_source[] = "order.missing";
    const scxml_expr_system_values unbound = {
        .is_data_bound = data_is_never_bound};
    scxml_expr_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    state_fixture states = {false};
    bool result = true;

    check_equal(scxml_expr_compile_value_with_scope_policy(
                    &program, missing_source,
                    sizeof(missing_source) - 1u, &root_data, NULL,
                    SCXML_EXPR_PATH_RUNTIME_MISSING,
                    SCXML_EXPR_VALUE_BOOL, resolve_state, NULL, NULL,
                    &diagnostic), SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate_with_system(
                    &program, &root, state_is_active, &states, NULL,
                    &result, &diagnostic),
                SCXML_EXPR_UNKNOWN_LOCATION);
    check_true(result);
    scxml_expr_program_destroy(&program);

    check_equal(compile_expression(
                    &program, "order.count == -3", NULL, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate_with_system(
                    &program, &root, state_is_active, &states, &unbound,
                    &result, &diagnostic),
                SCXML_EXPR_UNKNOWN_LOCATION);
    check_true(result);
    scxml_expr_program_destroy(&program);

    check_equal(scxml_expr_compile_value_with_scope_policy(
                    &program, "order.", sizeof("order.") - 1u,
                    &root_data, NULL, SCXML_EXPR_PATH_RUNTIME_MISSING,
                    SCXML_EXPR_VALUE_BOOL, resolve_state, NULL, NULL,
                    &diagnostic),
                SCXML_EXPR_SYNTAX_ERROR);
    check_null(program.impl);
  }

  it("rejects a selected nested scalar descriptor with inconsistent storage") {
    static const char expression[] = "order.count == 1";
    const cmeta_data_integer_shape wide_shape = {64u};
    cmeta_data_desc bad_leaf = cmeta_data_int;
    cmeta_data_field_desc bad_order_field = order_fields[1];
    cmeta_data_struct_shape bad_order_shape = order_shape;
    cmeta_data_desc bad_order = order_data;
    cmeta_data_field_desc bad_root_field = root_fields[1];
    cmeta_data_struct_shape bad_root_shape = root_shape;
    cmeta_data_desc bad_root = root_data;
    scxml_expr_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};

    bad_leaf.shape = &wide_shape;
    bad_order_field.value = &bad_leaf;
    bad_order_shape.fields = &bad_order_field;
    bad_order_shape.field_count = 1u;
    bad_order.shape = &bad_order_shape;
    bad_root_field.value = &bad_order;
    bad_root_shape.fields = &bad_root_field;
    bad_root_shape.field_count = 1u;
    bad_root.shape = &bad_root_shape;

    check_equal(scxml_expr_compile(
                    &program, expression, sizeof(expression) - 1u, &bad_root,
                    resolve_state, NULL, NULL, &diagnostic),
                SCXML_EXPR_TYPE_MISMATCH);
    check_null(program.impl);
  }

  it("fails fast on configured source instruction and depth limits") {
    scxml_expr_limits limits =
        scxml_expr_default_limits();
    scxml_expr_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};

    limits.max_source_bytes = 4u;
    check_equal(compile_expression(&program, "true && false", &limits,
                                   &diagnostic),
                SCXML_EXPR_LIMIT_EXCEEDED);
    check_null(program.impl);

    limits = scxml_expr_default_limits();
    limits.max_instructions = 1u;
    check_equal(compile_expression(&program, "true && false", &limits,
                                   &diagnostic),
                SCXML_EXPR_LIMIT_EXCEEDED);

    limits = scxml_expr_default_limits();
    limits.max_expression_depth = 2u;
    check_equal(compile_expression(&program, "!!!true", &limits,
                                   &diagnostic),
                SCXML_EXPR_LIMIT_EXCEEDED);

    limits = scxml_expr_default_limits();
    limits.max_operands = 1u;
    check_equal(compile_expression(&program, "order.count == -3", &limits,
                                   &diagnostic),
                SCXML_EXPR_LIMIT_EXCEEDED);

    limits = scxml_expr_default_limits();
    limits.max_path_depth = 1u;
    check_equal(compile_expression(&program, "order.count == -3", &limits,
                                   &diagnostic),
                SCXML_EXPR_LIMIT_EXCEEDED);

    limits = scxml_expr_default_limits();
    limits.max_literal_bytes = 1u;
    check_equal(compile_expression(&program, "In(\"active\")", &limits,
                                   &diagnostic),
                SCXML_EXPR_LIMIT_EXCEEDED);
  }

  it("preserves output when an enum provider rejects the stored value") {
    scxml_expr_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    state_fixture states = {false};
    bool result = true;
    root.order.stage = (scxml_expr_stage)99;
    check_equal(compile_expression(&program, "order.stage == 2", NULL,
                                   &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate(
                    &program, &root, state_is_active, &states,
                    &result, &diagnostic),
                SCXML_EXPR_EVALUATION_ERROR);
    check_true(result);
    scxml_expr_program_destroy(&program);
  }

  it("preserves output when runtime inputs or callbacks fail") {
    scxml_expr_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    state_fixture states = {true};
    bool result = true;
    check_equal(compile_expression(&program, "In(\"active\")", NULL,
                                   &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate(
                    &program, &root, state_is_active, &states,
                    &result, &diagnostic),
                SCXML_EXPR_EVALUATION_ERROR);
    check_true(result);
    check_equal(scxml_expr_evaluate(
                    &program, NULL, state_is_active, &states,
                    &result, &diagnostic),
                SCXML_EXPR_INVALID_ARGUMENT);
    check_true(result);
    scxml_expr_program_destroy(&program);
  }

  it("short circuits inactive Boolean branches") {
    scxml_expr_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    state_fixture states = {true};
    bool result = true;
    check_equal(compile_expression(
                    &program, "false && In(\"active\")", NULL,
                    &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate(
                    &program, &root, state_is_active, &states,
                    &result, &diagnostic),
                SCXML_EXPR_OK);
    check_false(result);
    scxml_expr_program_destroy(&program);

    check_equal(compile_expression(
                    &program, "true || In(\"active\")", NULL,
                    &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_expr_evaluate(
                    &program, &root, state_is_active, &states,
                    &result, &diagnostic),
                SCXML_EXPR_OK);
    check_true(result);
    scxml_expr_program_destroy(&program);
  }
}
