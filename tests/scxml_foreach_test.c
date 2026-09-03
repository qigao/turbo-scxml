#include <scxml/scxml.h>
#include <cstl/typed.h>

#include "scxml_foreach.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define FOREACH_TEST_MAX_STORAGE_BYTES (1024u * 1024u)

static atomic_int foreach_action_observed;

typed_any_raw(
    CMETA_EFFECT_IO, CMETA_PROP_DETERMINISTIC,
    int, scxml_test_foreach_action, (int value)) {
    atomic_store_explicit(
        &foreach_action_observed, value, memory_order_relaxed);
    return value;
}

Struct(scxml_foreach_root,
    (TYPE(Vec, int), values),
    (int, item),
    (size_t, index),
    (int, outer_item),
    (size_t, outer_index),
    (int, total)
);

static bool foreach_root_copy(void *destination_, const void *source_) {
    scxml_foreach_root *destination = (scxml_foreach_root *)destination_;
    const scxml_foreach_root *source =
        (const scxml_foreach_root *)source_;
    size_t index;
    if (destination == NULL || source == NULL) return false;
    memset(destination, 0, sizeof(*destination));
    destination->values = VecOf(int);
    if (vec_init(&destination->values, source->values.element_limit) != STL_OK)
        return false;
    for (index = 0u; index < vec_size(&source->values); ++index) {
        const int *value = (const int *)vec_at_const(&source->values, index);
        if (value == NULL ||
            vec_push(&destination->values, value) != STL_OK) {
            vec_destroy(&destination->values);
            memset(destination, 0, sizeof(*destination));
            return false;
        }
    }
    destination->item = source->item;
    destination->index = source->index;
    destination->outer_item = source->outer_item;
    destination->outer_index = source->outer_index;
    destination->total = source->total;
    return true;
}

static void foreach_root_move(void *destination_, void *source_) {
    scxml_foreach_root *destination = (scxml_foreach_root *)destination_;
    scxml_foreach_root *source = (scxml_foreach_root *)source_;
    if (destination == NULL || source == NULL) return;
    *destination = *source;
    memset(source, 0, sizeof(*source));
}

static void foreach_root_destroy(void *value_) {
    scxml_foreach_root *value = (scxml_foreach_root *)value_;
    if (value != NULL && value->values.initialized)
        vec_destroy(&value->values);
}

static const cmeta_type_traits foreach_root_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = foreach_root_copy,
    .move_construct = foreach_root_move,
    .destroy = foreach_root_destroy
};

static const cmeta_type_desc foreach_root_type = {
    .name = "scxml_foreach_root",
    .size = sizeof(scxml_foreach_root),
    .align = _Alignof(scxml_foreach_root),
    .kind = CMETA_T_OBJECT,
    .traits = &foreach_root_traits
};

static const cmeta_data_field_desc foreach_root_fields[] = {
    {"test.scxml.foreach.values", "values",
     offsetof(scxml_foreach_root, values), &cmeta_data_sequence},
    {"test.scxml.foreach.item", "item",
     offsetof(scxml_foreach_root, item), &cmeta_data_int},
    {"test.scxml.foreach.index", "index",
     offsetof(scxml_foreach_root, index), &cmeta_data_size},
    {"test.scxml.foreach.outer-item", "outer_item",
     offsetof(scxml_foreach_root, outer_item), &cmeta_data_int},
    {"test.scxml.foreach.outer-index", "outer_index",
     offsetof(scxml_foreach_root, outer_index), &cmeta_data_size},
    {"test.scxml.foreach.total", "total",
     offsetof(scxml_foreach_root, total), &cmeta_data_int}
};

static const cmeta_data_struct_shape foreach_root_shape = {
    .layout = StructMeta(scxml_foreach_root),
    .fields = foreach_root_fields,
    .field_count = sizeof(foreach_root_fields) /
                   sizeof(foreach_root_fields[0])
};

static const cmeta_data_desc foreach_root_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.foreach.root",
    .display_name = "SCXML foreach root",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &foreach_root_type,
    .shape = &foreach_root_shape
};

typedef struct foreach_legacy_compile_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const cmeta_data_desc *root;
    size_t max_source_bytes;
    size_t max_instructions;
    size_t max_operands;
    size_t max_expression_depth;
    size_t max_path_depth;
    size_t max_literal_bytes;
    size_t max_string_bytes;
} foreach_legacy_compile_options_v1;

_Static_assert(
    sizeof(foreach_legacy_compile_options_v1) ==
        offsetof(scxml_cmeta_compile_options_v1, max_iterations),
    "legacy CMeta compile options prefix changed");

static scxml_status compile_foreach(
    const char *source, scxml_program *program,
    scxml_diagnostic *diagnostic) {
    const scxml_cmeta_compile_options_v1 options =
        scxml_cmeta_default_compile_options(&foreach_root_data);
    return scxml_compile_cmeta(
        program, source, strlen(source), NULL, &options, diagnostic);
}

static scxml_status compile_foreach_with_options(
    const char *source, scxml_program *program,
    const scxml_cmeta_compile_options_v1 *options,
    scxml_diagnostic *diagnostic) {
    return scxml_compile_cmeta(
        program, source, strlen(source), NULL, options, diagnostic);
}

static cflow_statechart_instance_stats run_foreach_with_event(
    const scxml_program *program, const int *values,
    size_t value_count, int item, size_t index, int total,
    const char *event_name) {
    scxml_foreach_root initial = {
        .values = VecOf(int), .item = item, .index = index, .total = total};
    cflow_executor executor = {0};
    scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    scxml_session_config config = {
        .program = program,
        .executor = &executor,
        .external_event_capacity = 2u,
        .internal_event_capacity = 4u,
        .completion_capacity = 2u,
        .microstep_limit = 32u,
        .effect_capacity = 1u,
        .max_storage_bytes = FOREACH_TEST_MAX_STORAGE_BYTES
    };
    scxml_cmeta_session_options_v1 data = {
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(scxml_cmeta_session_options_v1),
        .initial_state = &initial
    };
    cflow_event_view event = {0};
    size_t value_index;

    check_equal(vec_init(&initial.values, value_count), STL_OK);
    for (value_index = 0u; value_index < value_count; ++value_index)
        check_equal(vec_push(&initial.values, &values[value_index]), STL_OK);
    check_true(cflow_executor_serial_init(&executor));
    check_equal(scxml_session_init_cmeta(&session, &config, &data),
                CFLOW_STATECHART_INSTANCE_OK);
    check_true(cflow_executor_wait_idle(&executor));
    if (event_name != NULL) {
        check_true(scxml_program_event(
            program, event_name, strlen(event_name), &event));
        check_equal(scxml_session_try_send(&session, &event),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
    }
    check_true(scxml_session_get_stats(&session, &stats));
    check_equal(scxml_session_destroy(&session),
                CFLOW_STATECHART_INSTANCE_OK);
    cflow_executor_destroy(&executor);
    vec_destroy(&initial.values);
    return stats;
}

static cflow_statechart_instance_stats run_foreach(
    const scxml_program *program, const int *values,
    size_t value_count, int item, size_t index, int total) {
    return run_foreach_with_event(
        program, values, value_count, item, index, total, NULL);
}

typedef struct foreach_finalize_probe {
    uint64_t token;
    size_t starts;
    size_t cancels;
} foreach_finalize_probe;

static void foreach_finalize_ticket_done(void *user) {
    (void)user;
}

static scxml_adapter_status foreach_finalize_prepare_start(
    void *user, const scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    foreach_finalize_probe *probe = (foreach_finalize_probe *)user;
    if (probe == NULL || request == NULL || request->token == 0u ||
        out_ticket == NULL || out_error == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    probe->token = request->token;
    ++probe->starts;
    *out_ticket = (cflow_statechart_effect_ticket){
        foreach_finalize_ticket_done, foreach_finalize_ticket_done, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status foreach_finalize_prepare_cancel(
    void *user, const scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    foreach_finalize_probe *probe = (foreach_finalize_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->cancels;
    *out_ticket = (cflow_statechart_effect_ticket){
        foreach_finalize_ticket_done, foreach_finalize_ticket_done, probe};
    *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void foreach_finalize_adapter_close(void *user) {
    (void)user;
}

static bool foreach_finalize_adapter_quiescent(void *user) {
    return user != NULL;
}

spec("TurboSCXML CMeta foreach") {
  it("auto-declares missing item and index variables") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='auto_item' index='auto_index'>"
        "<assign location='total' expr='auto_item'/></foreach>"
        "</onentry><transition cond='total == 3 &amp;&amp; auto_item == 3 "
        "&amp;&amp; auto_index == 2' target='done'/></state>"
        "<final id='done'/></scxml>";
    const int values[] = {1, 2, 3};
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;

    check_equal(compile_foreach(source, &program, &diagnostic),
                SCXML_OK);
    stats = run_foreach(&program, values, 3u, 0, 0u, 0);
    check_true(stats.done);
    check_false(stats.errored);
    scxml_program_destroy(&program);
  }

  it("rejects conflicting auto-declared foreach variable types") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='auto_value'>"
        "<assign location='total' expr='auto_value'/></foreach>"
        "<foreach array='values' item='item' index='auto_value'>"
        "<assign location='total' expr='item'/></foreach>"
        "</onentry></state></scxml>";
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};

    check_equal(compile_foreach(source, &program, &diagnostic),
                SCXML_INVALID_STRUCTURE);
    check_null(program.impl);
  }

  it("rolls back auto-declared variables when their executable block fails") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='ready' datamodel='cmeta'><state id='ready'><onentry>"
        "<foreach array='values' item='auto_item' index='auto_index'>"
        "<assign location='total' expr='auto_item'/></foreach></onentry>"
        "<transition event='go' target='work'/></state>"
        "<state id='work'><onentry>"
        "<foreach array='values' item='auto_item' index='auto_index'>"
        "<assign location='total' expr='4294967295'/></foreach></onentry>"
        "<transition event='error.execution' cond='total == 3 &amp;&amp; "
        "auto_item == 3 &amp;&amp; auto_index == 2' target='done'/></state>"
        "<final id='done'/></scxml>";
    const int values[] = {1, 2, 3};
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;

    check_equal(compile_foreach(source, &program, &diagnostic), SCXML_OK);
    stats = run_foreach_with_event(
        &program, values, 3u, 0, 0u, 0, "go");
    check_true(stats.done);
    check_false(stats.errored);
    scxml_program_destroy(&program);
  }

  it("iterates the entry snapshot after the source sequence changes") {
    const int initial_values[] = {1, 2};
    const int appended = 3;
    scxml_foreach_root root = {.values = VecOf(int)};
    scxml_foreach_program program = {0};
    scxml_expr_diagnostic diagnostic = {0};
    scxml_foreach_snapshot snapshot = {0};
    scxml_foreach_value value = {0};

    check_equal(vec_init(&root.values, 3u), STL_OK);
    check_equal(vec_push(&root.values, &initial_values[0]), STL_OK);
    check_equal(vec_push(&root.values, &initial_values[1]), STL_OK);
    check_equal(scxml_foreach_compile(
                    &program, "values", strlen("values"),
                    "item", strlen("item"), "index", strlen("index"),
                    &foreach_root_data, 8u, 8u, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_foreach_open(
                    &program, &root, &snapshot, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(snapshot.length, (size_t)2u);
    check_equal(scxml_foreach_value_init(&program, &value, &diagnostic),
                SCXML_EXPR_OK);

    check_equal(scxml_foreach_next(
                    &program, &root, &snapshot, &value, 0u, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(root.item, 1);
    check_equal(root.index, (size_t)0u);

    check_equal(vec_push(&root.values, &appended), STL_OK);
    check_equal(scxml_foreach_next(
                    &program, &root, &snapshot, &value, 1u, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(root.item, 2);
    check_equal(root.index, (size_t)1u);
    check_equal(vec_size(&root.values), (size_t)3u);

    scxml_foreach_value_destroy(&program, &value);
    scxml_foreach_snapshot_destroy(&program, &snapshot);
    vec_destroy(&root.values);
  }

  it("rejects a foreach item whose late declaration has not entered") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "datamodel='cmeta' binding='late' initial='before'>"
        "<state id='before'><onentry>"
        "<foreach array='values' item='item'><raise event='observed'/>"
        "</foreach></onentry>"
        "<transition event='error.execution' target='done'/>"
        "<transition event='observed' target='failed'/></state>"
        "<state id='owner'><datamodel>"
        "<data id='item' expr='0'/></datamodel></state>"
        "<final id='done'/><state id='failed'/></scxml>";
    const int values[] = {1};
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;

    check_equal(compile_foreach(source, &program, &diagnostic),
                SCXML_OK);
    stats = run_foreach(&program, values, 1u, 9, 0u, 0);
    check_true(stats.done);
    check_false(stats.errored);
    scxml_program_destroy(&program);
  }

  it("assigns items and zero-based indexes in declared sequence order") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='item' index='index'>"
        "<assign location='total' expr='item'/></foreach>"
        "</onentry><transition cond='total == 3 &amp;&amp; item == 3 "
        "&amp;&amp; index == 2' target='done'/></state>"
        "<final id='done'/></scxml>";
    const int values[] = {1, 2, 3};
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;
    scxml_status status;

    status = compile_foreach(source, &program, &diagnostic);
    info("diagnostic=%s", diagnostic.message);
    check_equal(status, SCXML_OK);
    stats = run_foreach(&program, values, 3u, 0, 0u, 0);
    check_true(stats.done);
    check_false(stats.errored);
    scxml_program_destroy(&program);
  }

  it("invokes a CMeta custom action inside foreach and if") {
    static const char *const parameter_names[] = {"value"};
    const scxml_cmeta_custom_action_v1 actions[] = {{
        .namespace_uri = "urn:test:actions",
        .namespace_uri_size = sizeof("urn:test:actions") - 1u,
        .local_name = "record",
        .local_name_size = sizeof("record") - 1u,
        .callable = scxml_test_foreach_action,
        .parameter_names = parameter_names,
        .parameter_count = 1u}};
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
        "xmlns:a='urn:test:actions' version='1.0' initial='work' "
        "datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='item' index='index'>"
        "<if cond='index == 2'><a:record value='item'/></if>"
        "</foreach></onentry><transition target='done'/></state>"
        "<final id='done'/></scxml>";
    const int values[] = {1, 2, 3};
    scxml_cmeta_compile_options_v2 options =
        scxml_cmeta_default_compile_options_v2(&foreach_root_data);
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;

    options.actions = actions;
    options.action_count = sizeof(actions) / sizeof(actions[0]);
    atomic_store_explicit(
        &foreach_action_observed, 0, memory_order_relaxed);
    check_equal(scxml_compile_cmeta_v2(
                    &program, source, strlen(source), NULL,
                    &options, &diagnostic),
                SCXML_OK);
    stats = run_foreach(&program, values, 3u, 0, 0u, 0);
    check_true(stats.done);
    check_false(stats.errored);
    check_equal(atomic_load_explicit(
                    &foreach_action_observed, memory_order_relaxed),
                3);
    scxml_program_destroy(&program);
  }

  it("skips the body and preserves item and index for an empty sequence") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='item' index='index'>"
        "<assign location='total' expr='item'/></foreach>"
        "</onentry><transition cond='total == 5 &amp;&amp; item == 41 "
        "&amp;&amp; index == 43' target='done'/></state>"
        "<final id='done'/></scxml>";
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;
    scxml_status status;

    status = compile_foreach(source, &program, &diagnostic);
    info("diagnostic=%s", diagnostic.message);
    check_equal(status, SCXML_OK);
    stats = run_foreach(&program, NULL, 0u, 41, 43u, 5);
    check_true(stats.done);
    check_false(stats.errored);
    scxml_program_destroy(&program);
  }

  it("executes nested foreach and conditional ranges within the admitted depth") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='outer_item' index='outer_index'>"
        "<foreach array='values' item='item' index='index'>"
        "<if cond='outer_item == 2 &amp;&amp; outer_index == 1 "
        "&amp;&amp; item == 3 &amp;&amp; index == 2'>"
        "<assign location='total' expr='99'/></if></foreach></foreach>"
        "</onentry><transition cond='total == 99' target='done'/></state>"
        "<final id='done'/></scxml>";
    const int values[] = {1, 2, 3};
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;

    check_equal(compile_foreach(source, &program, &diagnostic),
                SCXML_OK);
    stats = run_foreach(&program, values, 3u, 0, 0u, 0);
    check_true(stats.done);
    check_false(stats.errored);
    scxml_program_destroy(&program);
  }

  it("retains and raises events declared inside a foreach body") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='item'><raise event='tick'/>"
        "</foreach></onentry><transition event='tick' target='done'/>"
        "</state><final id='done'/></scxml>";
    const int values[] = {1};
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;

    check_equal(compile_foreach(source, &program, &diagnostic),
                SCXML_OK);
    stats = run_foreach(&program, values, 1u, 0, 0u, 0);
    check_true(stats.done);
    check_false(stats.errored);
    scxml_program_destroy(&program);
  }

  it("accepts the original v1 options prefix and applies the bounded default") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='item'>"
        "<assign location='total' expr='item'/></foreach>"
        "</onentry><transition cond='total == 3 &amp;&amp; item == 3' "
        "target='done'/></state><final id='done'/></scxml>";
    const int values[] = {1, 2, 3};
    const scxml_cmeta_compile_options_v1 current =
        scxml_cmeta_default_compile_options(&foreach_root_data);
    foreach_legacy_compile_options_v1 legacy;
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;

    memcpy(&legacy, &current, sizeof(legacy));
    legacy.struct_size = sizeof(legacy);
    check_equal(compile_foreach_with_options(
                    source, &program,
                    (const scxml_cmeta_compile_options_v1 *)&legacy,
                    &diagnostic),
                SCXML_OK);
    stats = run_foreach(&program, values, 3u, 0, 0u, 0);
    check_true(stats.done);
    check_false(stats.errored);
    scxml_program_destroy(&program);
  }

  it("raises error.execution and rolls back the whole block above the iteration limit") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='item' index='index'>"
        "<assign location='total' expr='item'/></foreach>"
        "</onentry><transition event='error.execution' "
        "cond='total == 0 &amp;&amp; item == 9 &amp;&amp; index == 7' "
        "target='done'/></state><final id='done'/></scxml>";
    const int values[] = {1, 2, 3};
    scxml_cmeta_compile_options_v1 options =
        scxml_cmeta_default_compile_options(&foreach_root_data);
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;

    options.max_iterations = 2u;
    check_equal(compile_foreach_with_options(
                    source, &program, &options, &diagnostic),
                SCXML_OK);
    stats = run_foreach(&program, values, 3u, 9, 7u, 0);
    check_true(stats.done);
    check_false(stats.errored);
    scxml_program_destroy(&program);

    options.max_iterations = 0u;
    check_equal(compile_foreach_with_options(
                    source, &program, &options, &diagnostic),
                SCXML_INVALID_ARGUMENT);
    check_null(program.impl);
  }

  it("stops iteration and rolls back when a child assignment fails") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='item' index='index'>"
        "<assign location='total' expr='4294967295'/></foreach>"
        "</onentry><transition event='error.execution' "
        "cond='total == 0 &amp;&amp; item == 9 &amp;&amp; index == 7' "
        "target='done'/></state><final id='done'/></scxml>";
    const int values[] = {1};
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;

    check_equal(compile_foreach(source, &program, &diagnostic),
                SCXML_OK);
    stats = run_foreach(&program, values, 1u, 9, 7u, 0);
    check_true(stats.done);
    check_false(stats.errored);
    scxml_program_destroy(&program);
  }

  it("raises error.execution and aborts the block for an invalid item location") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='total.missing' index='index'>"
        "<assign location='total' expr='item'/></foreach>"
        "<raise event='continued'/></onentry>"
        "<transition event='error.execution' "
        "cond='total == 0 &amp;&amp; item == 9 &amp;&amp; index == 7' "
        "target='done'/><transition event='continued' target='failed'/>"
        "</state><final id='done'/><state id='failed'/></scxml>";
    const int values[] = {1};
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;

    check_equal(compile_foreach(source, &program, &diagnostic),
                SCXML_OK);
    stats = run_foreach(&program, values, 1u, 9, 7u, 0);
    check_true(stats.done);
    check_false(stats.errored);
    stats = run_foreach(&program, NULL, 0u, 9, 7u, 0);
    check_true(stats.done);
    check_false(stats.errored);
    scxml_program_destroy(&program);
  }

  it("rejects unresolved or mismatched foreach locations and empty bodies") {
    static const char *const invalid[] = {
        "<foreach item='item'><assign location='total' expr='item'/></foreach>",
        "<foreach array='values'><assign location='total' expr='item'/></foreach>",
        "<foreach array='total' item='item'><assign location='total' expr='item'/></foreach>",
        "<foreach array='values' item='index'><assign location='total' expr='item'/></foreach>",
        "<foreach array='values' item='item' index='total'><assign location='total' expr='item'/></foreach>",
        "<foreach array='values' item='item'></foreach>"};
    size_t invalid_index;

    for (invalid_index = 0u;
         invalid_index < sizeof(invalid) / sizeof(invalid[0]);
         ++invalid_index) {
        char source[1024];
        scxml_program program = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_status status;
        (void)snprintf(
            source, sizeof(source),
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='work' datamodel='cmeta'><state id='work'><onentry>%s"
            "</onentry></state></scxml>", invalid[invalid_index]);
        status = compile_foreach(source, &program, &diagnostic);
        info("case=%zu diagnostic=%s", invalid_index, diagnostic.message);
        check_equal(status, SCXML_INVALID_STRUCTURE);
        check_null(program.impl);
    }
  }

  it("rejects a corrupted runtime item location kind before assignment") {
    const int element = 1;
    scxml_foreach_root root = {.values = VecOf(int)};
    scxml_foreach_program program = {0};
    scxml_foreach_snapshot snapshot = {0};
    scxml_foreach_value value = {0};
    scxml_expr_diagnostic diagnostic = {0};

    check_equal(vec_init(&root.values, 1u), STL_OK);
    check_equal(vec_push(&root.values, &element), STL_OK);
    check_equal(scxml_foreach_compile(
                    &program,
                    "values", sizeof("values") - 1u,
                    "item", sizeof("item") - 1u,
                    NULL, 0u, &foreach_root_data, 4u, 4u,
                    &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_foreach_open(
                    &program, &root, &snapshot, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_foreach_value_init(
                    &program, &value, &diagnostic),
                SCXML_EXPR_OK);

    program.item.kind = (scxml_location_kind)UINT32_MAX;
    check_equal(scxml_foreach_next(
                    &program, &root, &snapshot, &value, 0u,
                    &diagnostic),
                SCXML_EXPR_INVALID_ARGUMENT);

    scxml_foreach_value_destroy(&program, &value);
    scxml_foreach_snapshot_destroy(&program, &snapshot);
    vec_destroy(&root.values);
  }

  it("rejects a corrupted runtime index location kind before assignment") {
    const int element = 1;
    scxml_foreach_root root = {.values = VecOf(int)};
    scxml_foreach_program program = {0};
    scxml_foreach_snapshot snapshot = {0};
    scxml_foreach_value value = {0};
    scxml_expr_diagnostic diagnostic = {0};

    check_equal(vec_init(&root.values, 1u), STL_OK);
    check_equal(vec_push(&root.values, &element), STL_OK);
    check_equal(scxml_foreach_compile(
                    &program,
                    "values", sizeof("values") - 1u,
                    "item", sizeof("item") - 1u,
                    "index", sizeof("index") - 1u,
                    &foreach_root_data, 4u, 4u, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_foreach_open(
                    &program, &root, &snapshot, &diagnostic),
                SCXML_EXPR_OK);
    check_equal(scxml_foreach_value_init(
                    &program, &value, &diagnostic),
                SCXML_EXPR_OK);

    program.index.kind = (scxml_location_kind)UINT32_MAX;
    check_equal(scxml_foreach_next(
                    &program, &root, &snapshot, &value, 0u,
                    &diagnostic),
                SCXML_EXPR_INVALID_ARGUMENT);

    scxml_foreach_value_destroy(&program, &value);
    scxml_foreach_snapshot_destroy(&program, &snapshot);
    vec_destroy(&root.values);
  }

  it("runs CMeta foreach and nested conditions in finalize blocks") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'>"
        "<invoke id='child'><finalize><foreach array='values' item='item'>"
        "<assign location='total' expr='item'/>"
        "<if cond='item == 3'><raise event='finalized'/></if>"
        "</foreach></finalize></invoke>"
        "<transition event='returned'/>"
        "<transition event='finalized' cond='total == 3' target='done'/>"
        "</state><final id='done'/></scxml>";
    static const int values[] = {1, 2, 3};
    const scxml_invoke_adapter invoke = {
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(invoke),
        .capabilities = SCXML_INVOKE_CAP_START | SCXML_INVOKE_CAP_CANCEL,
        .prepare_start = foreach_finalize_prepare_start,
        .prepare_cancel = foreach_finalize_prepare_cancel,
        .close = foreach_finalize_adapter_close,
        .is_quiescent = foreach_finalize_adapter_quiescent};
    scxml_foreach_root initial = {.values = VecOf(int)};
    const scxml_cmeta_session_options_v1 data = {
        .abi_version = SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(data),
        .initial_state = &initial};
    foreach_finalize_probe probe = {0};
    scxml_program program = {0};
    scxml_diagnostic diagnostic = {0};
    scxml_session session = {0};
    cflow_executor executor = {0};
    cflow_event_view returned = {0};
    cflow_statechart_instance_stats stats = {0};
    cflow_statechart_instance_status init_status;
    scxml_session_config config = {
        .program = &program,
        .executor = &executor,
        .external_event_capacity = 2u,
        .internal_event_capacity = 4u,
        .completion_capacity = 2u,
        .microstep_limit = 32u,
        .effect_capacity = 2u,
        .adapter_internal_event_capacity = 2u,
        .invocation_capacity = 1u,
        .max_storage_bytes = FOREACH_TEST_MAX_STORAGE_BYTES,
        .invoke = &invoke,
        .invoke_user = &probe};
    size_t value_index;

    check_equal(compile_foreach(source, &program, &diagnostic),
                SCXML_OK);
    check_equal(vec_init(&initial.values, 3u), STL_OK);
    for (value_index = 0u; value_index < 3u; ++value_index)
        check_equal(vec_push(&initial.values, &values[value_index]), STL_OK);
    check_true(cflow_executor_serial_init(&executor));
    init_status = scxml_session_init_cmeta(&session, &config, &data);
    if (init_status != CFLOW_STATECHART_INSTANCE_OK)
        info("finalize foreach session error=%s", scxml_session_error(&session));
    check_equal(init_status, CFLOW_STATECHART_INSTANCE_OK);
    check_true(cflow_executor_wait_idle(&executor));
    check_equal(probe.starts, (size_t)1u);
    check_true(scxml_program_event(
        &program, "returned", sizeof("returned") - 1u, &returned));
    check_equal(scxml_session_report_invoke_event(
                    &session, probe.token, &returned),
                CFLOW_MAILBOX_OK);
    check_true(cflow_executor_wait_idle(&executor));
    check_true(scxml_session_get_stats(&session, &stats));
    check_true(stats.done);
    check_false(stats.errored);
    check_equal(probe.cancels, (size_t)1u);
    check_equal(scxml_session_destroy(&session),
                CFLOW_STATECHART_INSTANCE_OK);
    cflow_executor_destroy(&executor);
    vec_destroy(&initial.values);
    scxml_program_destroy(&program);
  }
}
