#include <scxml/scxml.h>
#include "scxml_quickjs.h"
#include "tinytest.h"
#include <cstl/typed.h>
#include <salts/thread.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct quickjs_test_state {
    int value;
    int marker;
} quickjs_test_state;

static const cmeta_type_identity quickjs_test_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.quickjs.state");
static const cmeta_type_traits quickjs_test_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY
};
static const cmeta_type_desc quickjs_test_type = {
    .name = "quickjs_test_state",
    .size = sizeof(quickjs_test_state),
    .align = _Alignof(quickjs_test_state),
    .kind = CMETA_T_OBJECT,
    .traits = &quickjs_test_traits,
    .identity = &quickjs_test_identity
};
static const cmeta_field_desc quickjs_test_layout_fields[] = {
    {"value", "int", offsetof(quickjs_test_state, value), sizeof(int),
     _Alignof(int), &cmeta_type_int, NULL},
    {"marker", "int", offsetof(quickjs_test_state, marker), sizeof(int),
     _Alignof(int), &cmeta_type_int, NULL}
};
static const cmeta_struct_desc quickjs_test_layout = {
    "quickjs_test_state", sizeof(quickjs_test_state),
    _Alignof(quickjs_test_state), quickjs_test_layout_fields, 2u
};
static const cmeta_data_field_desc quickjs_test_data_fields[] = {
    {"test.scxml.quickjs.state.value", "value",
     offsetof(quickjs_test_state, value), &cmeta_data_int},
    {"test.scxml.quickjs.state.marker", "marker",
     offsetof(quickjs_test_state, marker), &cmeta_data_int}
};
static const cmeta_data_struct_shape quickjs_test_shape = {
    &quickjs_test_layout, quickjs_test_data_fields, 2u
};
static const cmeta_data_desc quickjs_test_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.quickjs.state",
    .display_name = "quickjs_test_state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &quickjs_test_type,
    .shape = &quickjs_test_shape
};

typedef struct quickjs_nested_state {
    quickjs_test_state nested;
} quickjs_nested_state;

static const cmeta_type_identity quickjs_nested_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.quickjs.nested-state");
static const cmeta_type_desc quickjs_nested_type = {
    .name = "quickjs_nested_state",
    .size = sizeof(quickjs_nested_state),
    .align = _Alignof(quickjs_nested_state),
    .kind = CMETA_T_OBJECT,
    .traits = &quickjs_test_traits,
    .identity = &quickjs_nested_identity};
static const cmeta_field_desc quickjs_nested_layout_fields[] = {
    {"nested", "quickjs_test_state", offsetof(quickjs_nested_state, nested),
     sizeof(quickjs_test_state), _Alignof(quickjs_test_state),
     &quickjs_test_type, NULL}};
static const cmeta_struct_desc quickjs_nested_layout = {
    "quickjs_nested_state", sizeof(quickjs_nested_state),
    _Alignof(quickjs_nested_state), quickjs_nested_layout_fields, 1u};
static const cmeta_data_field_desc quickjs_nested_fields[] = {
    {"test.scxml.quickjs.nested-state.nested", "nested",
     offsetof(quickjs_nested_state, nested), &quickjs_test_data}};
static const cmeta_data_struct_shape quickjs_nested_shape = {
    &quickjs_nested_layout, quickjs_nested_fields, 1u};
static const cmeta_data_desc quickjs_nested_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.quickjs.nested-state",
    .display_name = "quickjs_nested_state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &quickjs_nested_type,
    .shape = &quickjs_nested_shape};

enum {
    QUICKJS_SLOW_TEXT_CAPACITY = 16u,
    QUICKJS_SLOW_ASSIGN_MILLISECONDS = 250u,
    QUICKJS_TEST_DEADLINE_MILLISECONDS = 100u
};

typedef struct quickjs_slow_text {
    char data[QUICKJS_SLOW_TEXT_CAPACITY + 1u];
    size_t size;
} quickjs_slow_text;

typedef struct quickjs_slow_state {
    quickjs_slow_text text;
    int marker;
} quickjs_slow_state;

static const cmeta_type_identity quickjs_slow_text_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.quickjs.slow-text");
static const cmeta_type_desc quickjs_slow_text_type = {
    .name = "quickjs_slow_text",
    .size = sizeof(quickjs_slow_text),
    .align = _Alignof(quickjs_slow_text),
    .kind = CMETA_T_OBJECT,
    .traits = &quickjs_test_traits,
    .identity = &quickjs_slow_text_identity};

static bool quickjs_slow_text_is_zero(const void *object) {
    const quickjs_slow_text *text = (const quickjs_slow_text *)object;
    return text != NULL && text->size == 0u;
}

static cmeta_status quickjs_slow_text_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
    quickjs_slow_text *text = (quickjs_slow_text *)object;
    salts_sleep_ms(QUICKJS_SLOW_ASSIGN_MILLISECONDS);
    if (text == NULL || (size != 0u && data == NULL))
        return CMETA_INVALID_ARGUMENT;
    if (size > max_bytes || size > QUICKJS_SLOW_TEXT_CAPACITY)
        return CMETA_CAPACITY_EXCEEDED;
    if (size != 0u) memcpy(text->data, data, size);
    text->data[size] = '\0';
    text->size = size;
    return CMETA_OK;
}

static void quickjs_slow_text_restore_zero(void *object) {
    if (object != NULL) memset(object, 0, sizeof(quickjs_slow_text));
}

static cmeta_status quickjs_slow_text_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
    const quickjs_slow_text *text = (const quickjs_slow_text *)object;
    if (text == NULL || out_data == NULL || out_size == NULL ||
        text->size > QUICKJS_SLOW_TEXT_CAPACITY)
        return CMETA_INVALID_ARGUMENT;
    *out_data = (const unsigned char *)text->data;
    *out_size = text->size;
    return CMETA_OK;
}

static const cmeta_data_buffer_shape quickjs_slow_text_shape = {
    .ownership = CMETA_DATA_BUFFER_OWNED};
static const cmeta_data_buffer_ops quickjs_slow_text_ops = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &quickjs_slow_text_type,
    .ownership = CMETA_DATA_BUFFER_OWNED,
    .is_zero = quickjs_slow_text_is_zero,
    .assign = quickjs_slow_text_assign,
    .restore_zero = quickjs_slow_text_restore_zero,
    .read = quickjs_slow_text_read};
static const cmeta_data_desc quickjs_slow_text_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.quickjs.slow-text",
    .display_name = "quickjs_slow_text",
    .kind = CMETA_DATA_STRING,
    .storage_type = &quickjs_slow_text_type,
    .shape = &quickjs_slow_text_shape,
    .buffer_ops = &quickjs_slow_text_ops};

static const cmeta_type_identity quickjs_slow_state_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.quickjs.slow-state");
static const cmeta_type_desc quickjs_slow_state_type = {
    .name = "quickjs_slow_state",
    .size = sizeof(quickjs_slow_state),
    .align = _Alignof(quickjs_slow_state),
    .kind = CMETA_T_OBJECT,
    .traits = &quickjs_test_traits,
    .identity = &quickjs_slow_state_identity};
static const cmeta_field_desc quickjs_slow_state_layout_fields[] = {
    {"text", "quickjs_slow_text", offsetof(quickjs_slow_state, text),
     sizeof(quickjs_slow_text), _Alignof(quickjs_slow_text),
     &quickjs_slow_text_type, NULL},
    {"marker", "int", offsetof(quickjs_slow_state, marker), sizeof(int),
     _Alignof(int), &cmeta_type_int, NULL}};
static const cmeta_struct_desc quickjs_slow_state_layout = {
    "quickjs_slow_state", sizeof(quickjs_slow_state),
    _Alignof(quickjs_slow_state), quickjs_slow_state_layout_fields, 2u};
static const cmeta_data_field_desc quickjs_slow_state_fields[] = {
    {"test.scxml.quickjs.slow-state.text", "text",
     offsetof(quickjs_slow_state, text), &quickjs_slow_text_data},
    {"test.scxml.quickjs.slow-state.marker", "marker",
     offsetof(quickjs_slow_state, marker), &cmeta_data_int}};
static const cmeta_data_struct_shape quickjs_slow_state_shape = {
    &quickjs_slow_state_layout, quickjs_slow_state_fields, 2u};
static const cmeta_data_desc quickjs_slow_state_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.quickjs.slow-state",
    .display_name = "quickjs_slow_state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &quickjs_slow_state_type,
    .shape = &quickjs_slow_state_shape};

Enum(quickjs_test_mode,
    (QUICKJS_TEST_MODE_ONE, 1, "one"),
    (QUICKJS_TEST_MODE_TWO, 2, "two")
);

typedef struct quickjs_numeric_state {
    int64_t signed_value;
    quickjs_test_mode mode;
    int marker;
} quickjs_numeric_state;

static const cmeta_type_identity quickjs_int64_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.quickjs.int64");
static const cmeta_type_desc quickjs_int64_type = {
    .name = "int64_t",
    .size = sizeof(int64_t),
    .align = _Alignof(int64_t),
    .kind = CMETA_T_INTEGER,
    .traits = &quickjs_test_traits,
    .identity = &quickjs_int64_identity};
static const cmeta_data_integer_shape quickjs_int64_shape = {64u};
static const cmeta_data_desc quickjs_int64_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.quickjs.int64",
    .display_name = "int64_t",
    .kind = CMETA_DATA_SINT,
    .storage_type = &quickjs_int64_type,
    .shape = &quickjs_int64_shape};

static const cmeta_type_identity quickjs_mode_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.quickjs.mode");
static const cmeta_type_desc quickjs_mode_type = {
    .name = "quickjs_test_mode",
    .size = sizeof(quickjs_test_mode),
    .align = _Alignof(quickjs_test_mode),
    .kind = CMETA_T_INTEGER,
    .traits = &quickjs_test_traits,
    .identity = &quickjs_mode_identity};

static bool quickjs_mode_is_zero(const void *object) {
    quickjs_test_mode value;
    if (object == NULL) return false;
    memcpy(&value, object, sizeof(value));
    return value == (quickjs_test_mode)0;
}

static cmeta_status quickjs_mode_read(const void *object, int64_t *out) {
    quickjs_test_mode value;
    if (object == NULL || out == NULL) return CMETA_INVALID_ARGUMENT;
    memcpy(&value, object, sizeof(value));
    *out = (int64_t)value;
    return CMETA_OK;
}

static cmeta_status quickjs_mode_assign(void *object, int64_t value) {
    const quickjs_test_mode native = (quickjs_test_mode)value;
    if (object == NULL) return CMETA_INVALID_ARGUMENT;
    memcpy(object, &native, sizeof(native));
    return CMETA_OK;
}

static void quickjs_mode_restore_zero(void *object) {
    const quickjs_test_mode value = (quickjs_test_mode)0;
    if (object != NULL) memcpy(object, &value, sizeof(value));
}

static const cmeta_data_enum_shape quickjs_mode_shape = {
    .meta = EnumMeta(quickjs_test_mode)};
static const cmeta_data_enum_ops quickjs_mode_ops = {
    .struct_size = sizeof(cmeta_data_enum_ops),
    .abi_version = CMETA_DATA_ENUM_OPS_ABI_VERSION,
    .storage_type = &quickjs_mode_type,
    .is_zero = quickjs_mode_is_zero,
    .read = quickjs_mode_read,
    .assign = quickjs_mode_assign,
    .restore_zero = quickjs_mode_restore_zero};
static const cmeta_data_desc quickjs_mode_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.quickjs.mode",
    .display_name = "quickjs_test_mode",
    .kind = CMETA_DATA_ENUM,
    .storage_type = &quickjs_mode_type,
    .shape = &quickjs_mode_shape,
    .enum_ops = &quickjs_mode_ops};

static const cmeta_type_identity quickjs_numeric_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.quickjs.numeric-state");
static const cmeta_type_desc quickjs_numeric_type = {
    .name = "quickjs_numeric_state",
    .size = sizeof(quickjs_numeric_state),
    .align = _Alignof(quickjs_numeric_state),
    .kind = CMETA_T_OBJECT,
    .traits = &quickjs_test_traits,
    .identity = &quickjs_numeric_identity};
static const cmeta_field_desc quickjs_numeric_layout_fields[] = {
    {"signed_value", "int64_t", offsetof(quickjs_numeric_state, signed_value),
     sizeof(int64_t), _Alignof(int64_t), &quickjs_int64_type, NULL},
    {"mode", "quickjs_test_mode", offsetof(quickjs_numeric_state, mode),
     sizeof(quickjs_test_mode), _Alignof(quickjs_test_mode),
     &quickjs_mode_type, NULL},
    {"marker", "int", offsetof(quickjs_numeric_state, marker), sizeof(int),
     _Alignof(int), &cmeta_type_int, NULL}};
static const cmeta_struct_desc quickjs_numeric_layout = {
    "quickjs_numeric_state", sizeof(quickjs_numeric_state),
    _Alignof(quickjs_numeric_state), quickjs_numeric_layout_fields, 3u};
static const cmeta_data_field_desc quickjs_numeric_fields[] = {
    {"test.scxml.quickjs.numeric.signed", "signed_value",
     offsetof(quickjs_numeric_state, signed_value), &quickjs_int64_data},
    {"test.scxml.quickjs.numeric.mode", "mode",
     offsetof(quickjs_numeric_state, mode), &quickjs_mode_data},
    {"test.scxml.quickjs.numeric.marker", "marker",
     offsetof(quickjs_numeric_state, marker), &cmeta_data_int}};
static const cmeta_data_struct_shape quickjs_numeric_shape = {
    &quickjs_numeric_layout, quickjs_numeric_fields, 3u};
static const cmeta_data_desc quickjs_numeric_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.quickjs.numeric-state",
    .display_name = "quickjs_numeric_state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &quickjs_numeric_type,
    .shape = &quickjs_numeric_shape};

Struct(quickjs_sequence_state,
    (TYPE(Vec, int), values),
    (int, value)
);

static bool quickjs_sequence_state_copy(
    void *destination_, const void *source_) {
    quickjs_sequence_state *destination =
        (quickjs_sequence_state *)destination_;
    const quickjs_sequence_state *source =
        (const quickjs_sequence_state *)source_;
    size_t index;
    if (destination == NULL || source == NULL) return false;
    memset(destination, 0, sizeof(*destination));
    destination->values = VecOf(int);
    if (vec_init(&destination->values, source->values.element_limit) != STL_OK)
        return false;
    for (index = 0u; index < vec_size(&source->values); ++index) {
        const int *value = (const int *)vec_at_const(&source->values, index);
        if (value == NULL || vec_push(&destination->values, value) != STL_OK) {
            vec_destroy(&destination->values);
            memset(destination, 0, sizeof(*destination));
            return false;
        }
    }
    destination->value = source->value;
    return true;
}

static void quickjs_sequence_state_move(
    void *destination_, void *source_) {
    quickjs_sequence_state *destination =
        (quickjs_sequence_state *)destination_;
    quickjs_sequence_state *source =
        (quickjs_sequence_state *)source_;
    if (destination == NULL || source == NULL) return;
    *destination = *source;
    memset(source, 0, sizeof(*source));
}

static void quickjs_sequence_state_destroy(void *value_) {
    quickjs_sequence_state *value = (quickjs_sequence_state *)value_;
    if (value != NULL && value->values.initialized)
        vec_destroy(&value->values);
}

static const cmeta_type_traits quickjs_sequence_state_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = quickjs_sequence_state_copy,
    .move_construct = quickjs_sequence_state_move,
    .destroy = quickjs_sequence_state_destroy};

static const cmeta_type_desc quickjs_sequence_state_type = {
    .name = "quickjs_sequence_state",
    .size = sizeof(quickjs_sequence_state),
    .align = _Alignof(quickjs_sequence_state),
    .kind = CMETA_T_OBJECT,
    .traits = &quickjs_sequence_state_traits};

static const cmeta_data_field_desc quickjs_sequence_state_fields[] = {
    {"test.scxml.quickjs.sequence.values", "values",
     offsetof(quickjs_sequence_state, values), &cmeta_data_sequence},
    {"test.scxml.quickjs.sequence.value", "value",
     offsetof(quickjs_sequence_state, value), &cmeta_data_int}};

static const cmeta_data_struct_shape quickjs_sequence_state_shape = {
    .layout = StructMeta(quickjs_sequence_state),
    .fields = quickjs_sequence_state_fields,
    .field_count = sizeof(quickjs_sequence_state_fields) /
                   sizeof(quickjs_sequence_state_fields[0])};

static const cmeta_data_desc quickjs_sequence_state_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.quickjs.sequence.state",
    .display_name = "QuickJS sequence state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &quickjs_sequence_state_type,
    .shape = &quickjs_sequence_state_shape};

typedef struct quickjs_text_probe {
    const char *source;
    scxml_resource_status status;
    size_t open_count;
    size_t close_count;
} quickjs_text_probe;

static scxml_resource_status quickjs_text_open(
    void *user, const char *uri, size_t uri_size, size_t max_bytes,
    scxml_text_resource *out) {
    quickjs_text_probe *probe = (quickjs_text_probe *)user;
    (void)max_bytes;
    if (probe == NULL || out == NULL || uri == NULL || uri_size == 0u)
        return SCXML_RESOURCE_FAILED;
    ++probe->open_count;
    if (probe->status != SCXML_RESOURCE_OK) return probe->status;
    *out = (scxml_text_resource){
        probe->source, strlen(probe->source), probe};
    return SCXML_RESOURCE_OK;
}

static void quickjs_text_close(
    void *user, scxml_text_resource *resource) {
    quickjs_text_probe *probe = (quickjs_text_probe *)user;
    if (probe != NULL && resource != NULL && resource->lease == probe)
        ++probe->close_count;
}

static const scxml_text_resource_adapter_v1 quickjs_text_adapter = {
    SCXML_TEXT_RESOURCE_ADAPTER_ABI_V1,
    sizeof(scxml_text_resource_adapter_v1),
    quickjs_text_open,
    quickjs_text_close
};

#if TURBOSCXML_HAS_QUICKJS
static cflow_statechart_instance_stats quickjs_run_to_idle_with_options(
    const char *source,
    const scxml_quickjs_compile_options_v1 *compile_options,
    const void *initial,
    cflow_statechart_instance_status *out_init_status,
    const scxml_ioprocessor_descriptor *ioprocessors,
    size_t ioprocessor_count) {
    const scxml_quickjs_session_options_v1 session_options = {
        SCXML_QUICKJS_SESSION_OPTIONS_ABI_V1,
        sizeof(scxml_quickjs_session_options_v1),
        initial};
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    scxml_session session = {0};
    cflow_executor executor = {0};
    cflow_statechart_instance_stats stats = {0};
    scxml_session_config config = {
        .program = &program,
        .executor = &executor,
        .external_event_capacity = 2u,
        .internal_event_capacity = 4u,
        .completion_capacity = 2u,
        .microstep_limit = 32u,
        .effect_capacity = 2u,
        .max_storage_bytes = 64u * 1024u,
        .ioprocessors = ioprocessors,
        .ioprocessor_count = ioprocessor_count};

    {
        const scxml_status compile_status = scxml_compile_quickjs(
            &program, source, strlen(source), NULL, compile_options,
            &diagnostic);
        info("QuickJS compile diagnostic: %s", diagnostic.message);
        check_equal(compile_status, SCXML_OK);
    }
    check_true(cflow_executor_serial_init(&executor));
    *out_init_status =
        scxml_session_init_quickjs(&session, &config, &session_options);
    if (*out_init_status == CFLOW_STATECHART_INSTANCE_OK) {
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_equal(
            scxml_session_destroy(&session), CFLOW_STATECHART_INSTANCE_OK);
    }
    cflow_executor_destroy(&executor);
    scxml_program_destroy(&program);
    return stats;
}

static cflow_statechart_instance_stats quickjs_run_to_idle_with_root(
    const char *source, const cmeta_data_desc *root, const void *initial,
    cflow_statechart_instance_status *out_init_status) {
    const scxml_quickjs_compile_options_v1 compile_options =
        scxml_quickjs_default_compile_options(root);
    return quickjs_run_to_idle_with_options(
        source, &compile_options, initial, out_init_status, NULL, 0u);
}

static cflow_statechart_instance_stats quickjs_run_to_idle(
    const char *source, quickjs_test_state initial,
    cflow_statechart_instance_status *out_init_status) {
    return quickjs_run_to_idle_with_root(
        source, &quickjs_test_data, &initial, out_init_status);
}

static char *quickjs_read_w3c_fixture(
    const char *fixture, size_t *out_size) {
    char path[512];
    const int written = fixture != NULL
        ? snprintf(path, sizeof(path), "%s/%s",
                   SCXML_W3C_FIXTURE_DIR, fixture)
        : -1;
    if (written < 0 || (size_t)written >= sizeof(path)) return NULL;
    return tt_read_file(path, out_size);
}
#endif

spec("SCXML optional QuickJS sandbox profile") {
    it("keeps exact quickjs-sandbox admission disabled by default") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><state id='s'/></scxml>";
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_quickjs_compile_options_v1 options =
            scxml_quickjs_default_compile_options(&quickjs_test_data);

#if TURBOSCXML_HAS_QUICKJS
        (void)source;
        (void)program;
        (void)diagnostic;
        (void)options;
#else
        check_equal(
            scxml_compile_quickjs(
                &program, source, strlen(source), NULL, &options,
                &diagnostic),
            SCXML_UNSUPPORTED_FEATURE);
        check_null(program.impl);
        check_not_null(strstr(diagnostic.message, "without quickjs-sandbox"));
#endif
    }

#if TURBOSCXML_HAS_QUICKJS
    it("admits root and executable scripts with compile-time source loading") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><script>var loaded=1;</script>"
            "<state id='s'><onentry><script src='memory:test'/></onentry>"
            "</state></scxml>";
        quickjs_text_probe probe = {
            "value = 2;", SCXML_RESOURCE_OK, 0u, 0u};
        scxml_quickjs_compile_options_v1 options =
            scxml_quickjs_default_compile_options(&quickjs_test_data);
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        options.script_resources = &quickjs_text_adapter;
        options.script_resource_user = &probe;

        check_equal(scxml_compile_quickjs(
                        &program, source, strlen(source), NULL, &options,
                        &diagnostic),
                    SCXML_OK);
        check_not_null(program.impl);
        check_equal(probe.open_count, (size_t)1u);
        check_equal(probe.close_count, (size_t)1u);
        scxml_program_destroy(&program);
    }

    it("rejects bad or unavailable script source before publication") {
        static const char syntax_error[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><script>if (</script>"
            "<state id='s'/></scxml>";
        static const char missing_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><script src='memory:missing'/>"
            "<state id='s'/></scxml>";
        quickjs_text_probe probe = {
            "", SCXML_RESOURCE_NOT_FOUND, 0u, 0u};
        scxml_quickjs_compile_options_v1 options =
            scxml_quickjs_default_compile_options(&quickjs_test_data);
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        check_equal(scxml_compile_quickjs(
                        &program, syntax_error, strlen(syntax_error), NULL,
                        &options, &diagnostic),
                    SCXML_INVALID_STRUCTURE);
        check_null(program.impl);
        options.script_resources = &quickjs_text_adapter;
        options.script_resource_user = &probe;
        check_equal(scxml_compile_quickjs(
                        &program, missing_source, strlen(missing_source), NULL,
                        &options, &diagnostic),
                    SCXML_INVALID_STRUCTURE);
        check_null(program.impl);
        check_equal(probe.open_count, (size_t)1u);
        check_equal(probe.close_count, (size_t)0u);
    }

    it("executes root script after data initialization and before configuration") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'>"
            "<datamodel><data id='value' expr='1'/></datamodel>"
            "<script>value = value + 1;</script>"
            "<state id='start'>"
            "<transition cond='value == 2' target='pass'/>"
            "<transition target='fail'/></state>"
            "<final id='pass'/><state id='fail'/></scxml>";
        cflow_statechart_instance_status init_status =
            CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        const cflow_statechart_instance_stats stats = quickjs_run_to_idle(
            source, (quickjs_test_state){0}, &init_status);

        check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
        check_true(stats.done);
        check_false(stats.errored);
    }

    it("evaluates ECMAScript data and guards with read-only SCXML system values") {
        static const scxml_ioprocessor_descriptor basic_http = {
            .name = "basichttp",
            .name_size = sizeof("basichttp") - 1u,
            .type = "http://www.w3.org/TR/scxml/#BasicHTTPEventProcessor",
            .type_size = sizeof(
                "http://www.w3.org/TR/scxml/#BasicHTTPEventProcessor") - 1u,
            .location = "http://127.0.0.1:43123/scxml/session-a",
            .location_size = sizeof(
                "http://127.0.0.1:43123/scxml/session-a") - 1u};
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "name='sandbox-machine' datamodel='quickjs-sandbox'>"
            "<datamodel><data id='value' "
            "expr='[1,2,3].map(x =&gt; x * 2)[1]'/></datamodel>"
            "<script>_name=0; _ioprocessors.scxml.location=0; "
            "_ioprocessors.basichttp.location=0; "
            "if(_name === 0 || "
            "_ioprocessors.scxml.location === 0 || "
            "_ioprocessors.basichttp.location === 0) throw 1;"
            "</script>"
            "<state id='start'><transition "
            "cond='value === 4 &amp;&amp; _name === &quot;sandbox-machine&quot; "
            "&amp;&amp; typeof _sessionid === &quot;string&quot; "
            "&amp;&amp; _sessionid.length &gt; 0 "
            "&amp;&amp; typeof _event === &quot;undefined&quot; "
            "&amp;&amp; _ioprocessors.scxml.location.indexOf(&quot;#_scxml_&quot;) === 0 "
            "&amp;&amp; _ioprocessors.basichttp.location === "
            "&quot;http://127.0.0.1:43123/scxml/session-a&quot; "
            "&amp;&amp; In(&quot;start&quot;)' target='pass'/>"
            "<transition target='fail'/></state>"
            "<final id='pass'/><state id='fail'/></scxml>";
        cflow_statechart_instance_status init_status =
            CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        const quickjs_test_state initial = {0};
        const scxml_quickjs_compile_options_v1 compile_options =
            scxml_quickjs_default_compile_options(&quickjs_test_data);
        const cflow_statechart_instance_stats stats =
            quickjs_run_to_idle_with_options(
                source, &compile_options, &initial, &init_status,
                &basic_http, 1u);

        check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
        check_true(stats.done);
        check_false(stats.errored);
    }

    it("isolates successful script globals between working contexts") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'>"
            "<script>globalThis.Hidden = 9;</script>"
            "<state id='start'><onentry><script>"
            "value = globalThis.Hidden ? 0 : 1;"
            "</script></onentry>"
            "<transition cond='value === 1' target='pass'/>"
            "<transition target='fail'/></state>"
            "<final id='pass'/><state id='fail'/></scxml>";
        cflow_statechart_instance_status init_status =
            CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        const cflow_statechart_instance_stats stats = quickjs_run_to_idle(
            source, (quickjs_test_state){0}, &init_status);

        check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
        check_true(stats.done);
        check_false(stats.errored);
    }

    it("rolls back a failed early initializer and continues its siblings") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'>"
            "<datamodel><data id='value' expr='missingName()'/>"
            "<data id='marker' expr='21 * 2'/></datamodel>"
            "<state id='start'><transition event='error.execution' "
            "cond='value == 7 &amp;&amp; marker == 42' target='pass'/>"
            "<transition event='error.execution' target='fail'/></state>"
            "<final id='pass'/><state id='fail'/></scxml>";
        cflow_statechart_instance_status init_status =
            CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        const cflow_statechart_instance_stats stats = quickjs_run_to_idle(
            source, (quickjs_test_state){.value = 7}, &init_status);

        check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
        check_true(stats.done);
        check_false(stats.errored);
    }

    it("executes nested script in its executable block") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'>"
            "<state id='start'><onentry><script>value = 2;</script>"
            "</onentry><transition cond='value == 2' target='pass'/>"
            "<transition target='fail'/></state>"
            "<final id='pass'/><state id='fail'/></scxml>";
        cflow_statechart_instance_status init_status =
            CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        const cflow_statechart_instance_stats stats = quickjs_run_to_idle(
            source, (quickjs_test_state){0}, &init_status);

        check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
        check_true(stats.done);
        check_false(stats.errored);
    }

    it("uses a script variable as an SCXML assignment location") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><script>var Var1 = 1;</script>"
            "<state id='start'><onentry>"
            "<assign location='Var1' expr='2'/></onentry>"
            "<transition cond='Var1 == 2' target='pass'/>"
            "<transition target='fail'/></state>"
            "<final id='pass'/><state id='fail'/></scxml>";
        cflow_statechart_instance_status init_status =
            CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        const cflow_statechart_instance_stats stats = quickjs_run_to_idle(
            source, (quickjs_test_state){0}, &init_status);

        check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
        check_true(stats.done);
        check_false(stats.errored);
    }

    it("counts unique script variables against the configured bound") {
        static const char duplicate[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><script>var A=1; var A=1;</script>"
            "<state id='s'/></scxml>";
        static const char overflow[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><script>var A=1; var B=2;</script>"
            "<state id='s'/></scxml>";
        scxml_quickjs_compile_options_v1 options =
            scxml_quickjs_default_compile_options(&quickjs_test_data);
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        options.max_script_variables = 1u;

        check_equal(
            scxml_compile_quickjs(
                &program, duplicate, sizeof(duplicate) - 1u,
                NULL, &options, &diagnostic),
            SCXML_OK);
        scxml_program_destroy(&program);
        check_equal(
            scxml_compile_quickjs(
                &program, overflow, sizeof(overflow) - 1u,
                NULL, &options, &diagnostic),
            SCXML_LIMIT_EXCEEDED);
        check_null(program.impl);
    }

    it("rejects a statically impossible property budget") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><script>var Extra=1;</script>"
            "<state id='s'/></scxml>";
        scxml_quickjs_compile_options_v1 options =
            scxml_quickjs_default_compile_options(&quickjs_test_data);
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        options.max_properties = 2u;

        {
            scxml_quickjs_compile_options_v1 nested_options =
                scxml_quickjs_default_compile_options(&quickjs_nested_data);
            nested_options.max_properties = 2u;
            check_equal(
                scxml_compile_quickjs(
                    &program, source, sizeof(source) - 1u,
                    NULL, &nested_options, &diagnostic),
                SCXML_INVALID_ARGUMENT);
            check_null(program.impl);
        }

        check_equal(
            scxml_compile_quickjs(
                &program, source, sizeof(source) - 1u,
                NULL, &options, &diagnostic),
            SCXML_LIMIT_EXCEEDED);
        check_null(program.impl);
    }

    it("rejects nested generic sequence shapes during admission") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><state id='s'/></scxml>";
        const cmeta_field_desc *base_field =
            &quickjs_sequence_state_shape.layout->fields[0];
        const cmeta_type_desc *arguments[] = {base_field->type};
        cmeta_declared_type nested_declared = *base_field->declared_type;
        cmeta_field_desc layout_field = *base_field;
        cmeta_struct_desc layout = *quickjs_sequence_state_shape.layout;
        const cmeta_data_field_desc data_field = {
            "test.scxml.quickjs.nested-sequence", "values",
            offsetof(quickjs_sequence_state, values), &cmeta_data_sequence};
        cmeta_data_struct_shape shape = {&layout, &data_field, 1u};
        cmeta_data_desc root = quickjs_sequence_state_data;
        scxml_quickjs_compile_options_v1 options;
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};

        nested_declared.arguments = arguments;
        layout_field.declared_type = &nested_declared;
        layout.fields = &layout_field;
        layout.field_count = 1u;
        root.shape = &shape;
        options = scxml_quickjs_default_compile_options(&root);
        check_equal(
            scxml_compile_quickjs(
                &program, source, sizeof(source) - 1u,
                NULL, &options, &diagnostic),
            SCXML_INVALID_ARGUMENT);
        check_null(program.impl);
    }

    it("passes W3C-derived script assertions 301 through 304") {
        static const char *const runnable[] = {
            "test302.scxml", "test303.scxml", "test304.scxml"};
        size_t source_size = 0u;
        char *source = quickjs_read_w3c_fixture(
            "test301.scxml", &source_size);
        quickjs_text_probe probe = {
            "", SCXML_RESOURCE_TIMEOUT, 0u, 0u};
        scxml_quickjs_compile_options_v1 options =
            scxml_quickjs_default_compile_options(&quickjs_test_data);
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        size_t index;

        check_not_null(source);
        options.script_resources = &quickjs_text_adapter;
        options.script_resource_user = &probe;
        check_equal(
            scxml_compile_quickjs(
                &program, source, source_size, NULL, &options, &diagnostic),
            SCXML_INVALID_STRUCTURE);
        check_null(program.impl);
        check_equal(probe.open_count, (size_t)1u);
        check_equal(probe.close_count, (size_t)0u);
        free(source);

        for (index = 0u;
             index < sizeof(runnable) / sizeof(runnable[0]); ++index) {
            cflow_statechart_instance_status init_status =
                CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
            cflow_statechart_instance_stats stats;
            source = quickjs_read_w3c_fixture(
                runnable[index], &source_size);
            check_not_null(source);
            stats = quickjs_run_to_idle(
                source, (quickjs_test_state){0}, &init_status);
            check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
            check_true(stats.done);
            check_false(stats.errored);
            free(source);
        }
    }

    it("rolls back state when a nested script throws") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><script>var Var1 = 3;</script>"
            "<state id='start'><onentry><script>globalThis.Hidden = 9; "
            "value = 9; Var1 = 9; throw 1;</script></onentry>"
            "<transition event='error.execution' target='recover'/></state>"
            "<state id='recover'><onentry>"
            "<script>value = globalThis.Hidden || 3;</script></onentry>"
            "<transition "
            "cond='value == 3 &amp;&amp; Var1 == 3' "
            "target='pass'/></state><final id='pass'/></scxml>";
        cflow_statechart_instance_status init_status =
            CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        const cflow_statechart_instance_stats stats = quickjs_run_to_idle(
            source, (quickjs_test_state){3}, &init_status);

        check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
        check_true(stats.done);
        check_false(stats.errored);
    }

    it("bounds accessor export and rebuilds the runtime for recovery") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><state id='start'><onentry>"
            "<script>(function(){Object.defineProperty(globalThis,'value',"
            "{get:function(){while(true){}}});})()</script></onentry>"
            "<transition event='error.execution' target='recover'/></state>"
            "<state id='recover'><onentry><script>value=2;</script></onentry>"
            "<transition cond='value===2' target='pass'/></state>"
            "<final id='pass'/></scxml>";
        cflow_statechart_instance_status init_status =
            CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        const cflow_statechart_instance_stats stats = quickjs_run_to_idle(
            source, (quickjs_test_state){.value = 7}, &init_status);

        check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
        check_true(stats.done);
        check_false(stats.errored);
    }

    it("rejects a transaction whose CMeta export crosses the deadline") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><state id='start'><onentry>"
            "<script>text='changed'; marker=9;</script></onentry>"
            "<transition event='error.execution' target='pass'/>"
            "</state><final id='pass'/></scxml>";
        const quickjs_slow_state initial = {0};
        scxml_quickjs_compile_options_v1 options =
            scxml_quickjs_default_compile_options(&quickjs_slow_state_data);
        cflow_statechart_instance_status init_status =
            CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        cflow_statechart_instance_stats stats;
        options.max_eval_milliseconds = QUICKJS_TEST_DEADLINE_MILLISECONDS;

        stats = quickjs_run_to_idle_with_options(
            source, &options, &initial, &init_status, NULL, 0u);
        check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
        check_true(stats.done);
        check_false(stats.errored);
    }

    it("rejects fractional signed writes without publishing them") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><state id='start'><onentry>"
            "<script>value=1.5;</script></onentry>"
            "<transition event='error.execution' cond='value===7' "
            "target='pass'/></state><final id='pass'/></scxml>";
        cflow_statechart_instance_status init_status =
            CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        const cflow_statechart_instance_stats stats = quickjs_run_to_idle(
            source, (quickjs_test_state){.value = 7}, &init_status);

        check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
        check_true(stats.done);
        check_false(stats.errored);
    }

    it("round trips safe signed boundaries and CMeta enums exactly") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><script>"
            "signed_value=-9007199254740991; mode=2;</script>"
            "<state id='start'><transition "
            "cond='signed_value===-9007199254740991 &amp;&amp; mode===2' "
            "target='pass'/></state><final id='pass'/></scxml>";
        const quickjs_numeric_state initial = {
            INT64_C(9007199254740991), QUICKJS_TEST_MODE_ONE, 0};
        cflow_statechart_instance_status init_status =
            CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        const cflow_statechart_instance_stats stats =
            quickjs_run_to_idle_with_root(
                source, &quickjs_numeric_data, &initial, &init_status);

        check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
        check_true(stats.done);
        check_false(stats.errored);
    }

    it("reports signed state outside the safe integer range") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><script>marker=1;</script>"
            "<state id='start'><transition event='error.execution' "
            "target='pass'/></state><final id='pass'/></scxml>";
        const quickjs_numeric_state initial = {
            INT64_C(9007199254740992), QUICKJS_TEST_MODE_ONE, 0};
        cflow_statechart_instance_status init_status =
            CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        const cflow_statechart_instance_stats stats =
            quickjs_run_to_idle_with_root(
                source, &quickjs_numeric_data, &initial, &init_status);

        check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
        check_true(stats.done);
        check_false(stats.errored);
    }

    it("rebuilds the session runtime after a QuickJS heap failure") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><state id='start'><onentry><script>"
            "(function(){const held=[]; for(let i=0;i!==1000000;++i) "
            "held.push({index:i,copy:i});})()</script></onentry>"
            "<transition event='error.execution' target='recover'/></state>"
            "<state id='recover'><onentry><script>value=2;</script></onentry>"
            "<transition cond='value===2' target='pass'/></state>"
            "<final id='pass'/></scxml>";
        const quickjs_test_state initial = {0};
        scxml_quickjs_compile_options_v1 options =
            scxml_quickjs_default_compile_options(&quickjs_test_data);
        cflow_statechart_instance_status init_status =
            CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        cflow_statechart_instance_stats stats;
        options.max_heap_bytes = 2u * 1024u * 1024u;
        options.max_eval_milliseconds = 5000u;

        stats = quickjs_run_to_idle_with_options(
            source, &options, &initial, &init_status, NULL, 0u);
        check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
        check_true(stats.done);
        check_false(stats.errored);
    }

    it("round trips a managed sequence between ordered scripts") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><state id='start'><onentry>"
            "<script>values[0] = 5; values.push(7);</script>"
            "<script>value = values[0] + values[2];</script>"
            "</onentry><transition cond='value == 12' target='pass'/>"
            "<transition target='fail'/></state>"
            "<final id='pass'/><state id='fail'/></scxml>";
        quickjs_sequence_state initial = {.values = VecOf(int)};
        const int first = 1;
        const int second = 2;
        scxml_quickjs_compile_options_v1 compile_options =
            scxml_quickjs_default_compile_options(
                &quickjs_sequence_state_data);
        scxml_quickjs_session_options_v1 session_options = {
            SCXML_QUICKJS_SESSION_OPTIONS_ABI_V1,
            sizeof(scxml_quickjs_session_options_v1),
            &initial};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_statechart_instance_stats stats = {0};
        scxml_session_config config = {
            .program = &program,
            .executor = &executor,
            .external_event_capacity = 2u,
            .internal_event_capacity = 4u,
            .completion_capacity = 2u,
            .microstep_limit = 32u,
            .effect_capacity = 2u,
            .max_storage_bytes = 64u * 1024u};

        check_equal(vec_init(&initial.values, 8u), STL_OK);
        check_equal(vec_push(&initial.values, &first), STL_OK);
        check_equal(vec_push(&initial.values, &second), STL_OK);
        {
            const scxml_status status = scxml_compile_quickjs(
                &program, source, sizeof(source) - 1u, NULL,
                &compile_options, &diagnostic);
            if (status != SCXML_OK)
                info("managed sequence compile: %s", diagnostic.message);
            check_equal(status, SCXML_OK);
        }
        check_true(cflow_executor_serial_init(&executor));
        check_equal(
            scxml_session_init_quickjs(
                &session, &config, &session_options),
            CFLOW_STATECHART_INSTANCE_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);
        check_equal(
            scxml_session_destroy(&session),
            CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&executor);
        scxml_program_destroy(&program);
        quickjs_sequence_state_destroy(&initial);
    }

    it("bounds a sequence Proxy length trap during export") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'><state id='start'><onentry><script>"
            "values=new Proxy(values,{get:function(target,key){"
            "if(key==='length'){for(;;){}} return target[key];}});"
            "</script></onentry><transition event='error.execution' "
            "target='recover'/></state><state id='recover'><onentry>"
            "<script>value=2;</script></onentry>"
            "<transition cond='value===2' target='pass'/></state>"
            "<final id='pass'/></scxml>";
        quickjs_sequence_state initial = {.values = VecOf(int)};
        const int item = 1;
        cflow_statechart_instance_status init_status =
            CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT;
        cflow_statechart_instance_stats stats;

        check_equal(vec_init(&initial.values, 4u), STL_OK);
        check_equal(vec_push(&initial.values, &item), STL_OK);
        stats = quickjs_run_to_idle_with_root(
            source, &quickjs_sequence_state_data, &initial, &init_status);
        check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
        check_true(stats.done);
        check_false(stats.errored);
        quickjs_sequence_state_destroy(&initial);
    }

    it("isolates QuickJS globals between sessions of one program") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='quickjs-sandbox'>"
            "<script>var SessionCounter = "
            "(globalThis.SessionCounter || 0) + 1; "
            "value=SessionCounter;</script>"
            "<state id='start'><transition cond='value == 1' target='pass'/>"
            "<transition target='fail'/></state>"
            "<final id='pass'/><state id='fail'/></scxml>";
        const quickjs_test_state initial = {0};
        scxml_quickjs_compile_options_v1 compile_options =
            scxml_quickjs_default_compile_options(&quickjs_test_data);
        const scxml_quickjs_session_options_v1 session_options = {
            SCXML_QUICKJS_SESSION_OPTIONS_ABI_V1,
            sizeof(scxml_quickjs_session_options_v1),
            &initial};
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        size_t iteration;

        {
            const scxml_status status = scxml_compile_quickjs(
                &program, source, sizeof(source) - 1u, NULL,
                &compile_options, &diagnostic);
            if (status != SCXML_OK)
                info("session isolation compile: %s", diagnostic.message);
            check_equal(status, SCXML_OK);
        }
        for (iteration = 0u; iteration < 2u; ++iteration) {
            scxml_session session = {0};
            cflow_executor executor = {0};
            cflow_statechart_instance_stats stats = {0};
            scxml_session_config config = {
                .program = &program,
                .executor = &executor,
                .external_event_capacity = 2u,
                .internal_event_capacity = 4u,
                .completion_capacity = 2u,
                .microstep_limit = 32u,
                .effect_capacity = 2u,
                .max_storage_bytes = 64u * 1024u};

            check_true(cflow_executor_serial_init(&executor));
            check_equal(
                scxml_session_init_quickjs(
                    &session, &config, &session_options),
                CFLOW_STATECHART_INSTANCE_OK);
            check_true(cflow_executor_wait_idle(&executor));
            check_true(scxml_session_get_stats(&session, &stats));
            check_true(stats.done);
            check_false(stats.errored);
            check_equal(
                scxml_session_destroy(&session),
                CFLOW_STATECHART_INSTANCE_OK);
            cflow_executor_destroy(&executor);
        }
        scxml_program_destroy(&program);
    }

    it("installs only bounded non-host intrinsics") {
        scxml_quickjs_compile_options_v1 options =
            scxml_quickjs_default_compile_options(&quickjs_test_data);
        scxml_quickjs_runtime runtime = {0};
        char diagnostic[SCXML_DIAGNOSTIC_CAPACITY] = {0};
        static const char absence[] =
            "if (typeof fetch !== 'undefined' || typeof std !== 'undefined' ||"
            "typeof os !== 'undefined' || typeof process !== 'undefined' ||"
            "typeof require !== 'undefined' || typeof Function !== 'undefined' ||"
            "typeof eval !== 'undefined') throw new Error('host API exposed');";
        static const char dynamic_escape[] =
            "(function(){}).constructor('return globalThis')();";

        {
            scxml_quickjs_status status = scxml_quickjs_runtime_init(
                &runtime, &options, diagnostic, sizeof(diagnostic));
            if (status != SCXML_QUICKJS_OK)
                (void)fprintf(stderr, "sandbox init: %s\n", diagnostic);
            check_equal(status, SCXML_QUICKJS_OK);
        }
        check_equal(scxml_quickjs_runtime_eval(
                        &runtime, absence, sizeof(absence) - 1u,
                        "absence.js", options.max_eval_milliseconds,
                        diagnostic, sizeof(diagnostic)),
                    SCXML_QUICKJS_OK);
        check_equal(scxml_quickjs_runtime_eval(
                        &runtime, dynamic_escape,
                        sizeof(dynamic_escape) - 1u, "escape.js",
                        options.max_eval_milliseconds,
                        diagnostic, sizeof(diagnostic)),
                    SCXML_QUICKJS_EXCEPTION);
        scxml_quickjs_runtime_destroy(&runtime);
    }

    it("validates repeated expressions in isolated compiler runtimes") {
        scxml_quickjs_compile_options_v1 options =
            scxml_quickjs_default_compile_options(&quickjs_test_data);
        char diagnostic[SCXML_DIAGNOSTIC_CAPACITY] = {0};
        static const char expression[] = "value == 2";
        size_t iteration;

        for (iteration = 0u; iteration < 100u; ++iteration) {
            const scxml_quickjs_status status =
                scxml_quickjs_validate_expression(
                    &options, expression, sizeof(expression) - 1u,
                    diagnostic, sizeof(diagnostic));
            if (status != SCXML_QUICKJS_OK)
                info("validation iteration %zu: %s", iteration, diagnostic);
            check_equal(status, SCXML_QUICKJS_OK);
        }
    }

    it("fails an allocation that exceeds the configured QuickJS heap") {
        scxml_quickjs_compile_options_v1 options =
            scxml_quickjs_default_compile_options(&quickjs_test_data);
        scxml_quickjs_runtime runtime = {0};
        char diagnostic[SCXML_DIAGNOSTIC_CAPACITY] = {0};
        static const char exhaust[] =
            "(function(){const held=[]; for(let i=0;i<1000000;++i) "
            "held.push({index:i, copy:i});})()";
        options.max_heap_bytes = 2u * 1024u * 1024u;
        options.max_eval_milliseconds = 5000u;

        check_equal(
            scxml_quickjs_runtime_init(
                &runtime, &options, diagnostic, sizeof(diagnostic)),
            SCXML_QUICKJS_OK);
        check_equal(
            scxml_quickjs_runtime_eval(
                &runtime, exhaust, sizeof(exhaust) - 1u, "heap-limit.js",
                options.max_eval_milliseconds,
                diagnostic, sizeof(diagnostic)),
            SCXML_QUICKJS_EXCEPTION);
        scxml_quickjs_runtime_destroy(&runtime);
    }

    it("interrupts an evaluation at its positive deadline") {
        scxml_quickjs_compile_options_v1 options =
            scxml_quickjs_default_compile_options(&quickjs_test_data);
        scxml_quickjs_runtime runtime = {0};
        char diagnostic[SCXML_DIAGNOSTIC_CAPACITY] = {0};
        static const char loop[] = "for(;;){}";

        {
            scxml_quickjs_status status = scxml_quickjs_runtime_init(
                &runtime, &options, diagnostic, sizeof(diagnostic));
            if (status != SCXML_QUICKJS_OK)
                (void)fprintf(stderr, "sandbox init: %s\n", diagnostic);
            check_equal(status, SCXML_QUICKJS_OK);
        }
        check_equal(scxml_quickjs_runtime_eval(
                        &runtime, loop, sizeof(loop) - 1u, "timeout.js", 5u,
                        diagnostic, sizeof(diagnostic)),
                    SCXML_QUICKJS_LIMIT_EXCEEDED);
        check_not_null(strstr(diagnostic, "deadline"));
        scxml_quickjs_runtime_destroy(&runtime);
    }
#endif
}
