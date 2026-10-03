#include "quickjs_cmeta_bridge.h"
#include "tinytest.h"

#include <cmeta/cmeta.h>
#include <cmeta/data.h>
#include <quickjs.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct bridge_int_sequence {
    int values[8];
    size_t count;
} bridge_int_sequence;

typedef struct bridge_state {
    int marker;
    bridge_int_sequence values;
} bridge_state;

static const cmeta_type_traits bridge_trivial_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY};

static const cmeta_type_identity bridge_sequence_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.quickjs.bridge.sequence");
static const cmeta_type_desc bridge_sequence_type = {
    .name = "bridge_int_sequence",
    .size = sizeof(bridge_int_sequence),
    .align = _Alignof(bridge_int_sequence),
    .kind = CMETA_T_OBJECT,
    .traits = &bridge_trivial_traits,
    .identity = &bridge_sequence_identity};

static cmeta_status bridge_sequence_init_zero(void *object) {
    if (object == NULL) return CMETA_INVALID_ARGUMENT;
    memset(object, 0, sizeof(bridge_int_sequence));
    return CMETA_OK;
}

static void bridge_sequence_restore_zero(void *object) {
    if (object != NULL)
        memset(object, 0, sizeof(bridge_int_sequence));
}

static void bridge_sequence_move(
    void *destination, void *source) {
    if (destination == NULL || source == NULL) return;
    memcpy(destination, source, sizeof(bridge_int_sequence));
    memset(source, 0, sizeof(bridge_int_sequence));
}

static const cmeta_data_construct_ops bridge_sequence_construct_ops = {
    .struct_size = sizeof(cmeta_data_construct_ops),
    .abi_version = CMETA_DATA_CONSTRUCT_OPS_ABI_VERSION,
    .storage_type = &bridge_sequence_type,
    .init_zero = bridge_sequence_init_zero,
    .restore_zero = bridge_sequence_restore_zero,
    .move = bridge_sequence_move};

static const cmeta_data_desc *bridge_sequence_element(
    const void *object) {
    (void)object;
    return &cmeta_data_int;
}

static size_t bridge_sequence_borrow_size(
    const void *object) {
    const bridge_int_sequence *sequence =
        (const bridge_int_sequence *)object;
    return sequence != NULL ? sequence->count : 0u;
}

static cmeta_gen_status bridge_sequence_borrow_next(
    const void *object,
    cmeta_range_cursor *cursor,
    const void **out_element) {
    const bridge_int_sequence *sequence =
        (const bridge_int_sequence *)object;
    if (sequence == NULL || cursor == NULL ||
        out_element == NULL ||
        sequence->count > 8u)
        return CMETA_GEN_ERROR;
    *out_element = NULL;
    if (cursor->index >= sequence->count)
        return CMETA_GEN_DONE;
    *out_element = &sequence->values[cursor->index++];
    return cursor->index == sequence->count
        ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
}

static const cmeta_data_collection_borrow_ops
bridge_sequence_borrow_ops = {
    .struct_size = sizeof(cmeta_data_collection_borrow_ops),
    .abi_version = CMETA_DATA_COLLECTION_BORROW_OPS_ABI_VERSION,
    .size = bridge_sequence_borrow_size,
    .next = bridge_sequence_borrow_next,
    .current_version = NULL};

static cmeta_status bridge_collector_begin(
    void *context, const cmeta_type_desc *input, size_t limit) {
    bridge_int_sequence *sequence =
        (bridge_int_sequence *)context;
    if (sequence == NULL || input == NULL ||
        !cmeta_type_equal(input, &cmeta_type_int) ||
        limit > 8u)
        return CMETA_INVALID_ARGUMENT;
    memset(sequence, 0, sizeof(*sequence));
    return CMETA_OK;
}

static cmeta_status bridge_collector_accept(
    void *context, const void *value) {
    bridge_int_sequence *sequence =
        (bridge_int_sequence *)context;
    if (sequence == NULL || value == NULL ||
        sequence->count >= 8u)
        return CMETA_CAPACITY_EXCEEDED;
    memcpy(
        &sequence->values[sequence->count++],
        value, sizeof(int));
    return CMETA_OK;
}

static cmeta_status bridge_collector_finish(void *context) {
    return context != NULL ? CMETA_OK : CMETA_INVALID_ARGUMENT;
}

static void bridge_collector_abort(void *context) {
    if (context != NULL)
        memset(context, 0, sizeof(bridge_int_sequence));
}

static const cmeta_collector_ops bridge_collector_ops = {
    bridge_collector_begin,
    bridge_collector_accept,
    bridge_collector_finish,
    bridge_collector_abort};

static cmeta_collector bridge_sequence_collector(
    void *zero_output, size_t limit) {
    return (cmeta_collector){
        .ops = &bridge_collector_ops,
        .context = zero_output,
        .zero_output = zero_output,
        .input_type = &cmeta_type_int,
        .limit = limit,
        .count = 0u,
        .state = CMETA_COLLECTOR_ZERO,
        .status = CMETA_OK};
}

static const cmeta_data_collection_ops bridge_sequence_ops = {
    .struct_size = sizeof(cmeta_data_collection_ops),
    .abi_version = CMETA_DATA_COLLECTION_OPS_ABI_VERSION,
    .storage_type = &bridge_sequence_type,
    .flags = CMETA_DATA_COLLECTION_ORDERED |
             CMETA_DATA_COLLECTION_RANDOM_ACCESS,
    .element = bridge_sequence_element,
    .read = NULL,
    .foreach = NULL,
    .collector = bridge_sequence_collector,
    .borrow = &bridge_sequence_borrow_ops,
    .element_data = &cmeta_data_int};

static const cmeta_data_desc bridge_sequence_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.quickjs.bridge.sequence.data",
    .display_name = "bridge_int_sequence",
    .kind = CMETA_DATA_SEQUENCE,
    .storage_type = &bridge_sequence_type,
    .collection_ops = &bridge_sequence_ops,
    .construct_ops = &bridge_sequence_construct_ops};

static const cmeta_type_identity bridge_state_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.quickjs.bridge.state");
static const cmeta_type_desc bridge_state_type = {
    .name = "bridge_state",
    .size = sizeof(bridge_state),
    .align = _Alignof(bridge_state),
    .kind = CMETA_T_OBJECT,
    .traits = &bridge_trivial_traits,
    .identity = &bridge_state_identity};

static const cmeta_field_desc bridge_state_layout_fields[] = {
    {"marker", "int", offsetof(bridge_state, marker),
     sizeof(int), _Alignof(int), &cmeta_type_int, NULL},
    {"values", "bridge_int_sequence",
     offsetof(bridge_state, values),
     sizeof(bridge_int_sequence),
     _Alignof(bridge_int_sequence),
     &bridge_sequence_type, NULL}
};

static const cmeta_struct_desc bridge_state_layout = {
    .name = "bridge_state",
    .size = sizeof(bridge_state),
    .align = _Alignof(bridge_state),
    .fields = bridge_state_layout_fields,
    .field_count = 2u};

static const cmeta_data_field_desc bridge_state_fields[] = {
    {"test.quickjs.bridge.state.marker", "marker",
     offsetof(bridge_state, marker), &cmeta_data_int},
    {"test.quickjs.bridge.state.values", "values",
     offsetof(bridge_state, values), &bridge_sequence_data}
};

static const cmeta_data_struct_shape bridge_state_shape = {
    .layout = &bridge_state_layout,
    .fields = bridge_state_fields,
    .field_count = 2u};

static const cmeta_data_desc bridge_state_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.quickjs.bridge.state.data",
    .display_name = "bridge_state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &bridge_state_type,
    .shape = &bridge_state_shape};

static quickjs_sandbox_options sandbox_options(void) {
    return (quickjs_sandbox_options){
        .max_source_bytes = 4096u,
        .max_string_bytes = 1024u,
        .max_heap_bytes = 4u * 1024u * 1024u,
        .max_stack_bytes = 256u * 1024u,
        .max_eval_milliseconds = UINT64_C(50)};
}

static quickjs_cmeta_limits bridge_limits(void) {
    return (quickjs_cmeta_limits){
        .max_conversion_depth = 8u,
        .max_properties = 64u,
        .max_array_items = 8u,
        .max_snapshot_bytes = 4096u,
        .max_string_bytes = 1024u};
}

spec("private QuickJS CMeta bridge") {
    it("imports canonical collection state and publishes one transactional snapshot") {
        static const char script[] =
            "marker = 7; values = [4, 5, 6];";
        quickjs_sandbox_options sandbox = sandbox_options();
        quickjs_cmeta_limits limits = bridge_limits();
        quickjs_sandbox_runtime runtime = {0};
        quickjs_cmeta_bridge bridge = {0};
        quickjs_cmeta_state_scratch scratch = {0};
        bridge_state committed = {
            .marker = 1,
            .values = {{2, 3}, 2u}};
        char diagnostic[256] = {0};

        check_true(quickjs_cmeta_schema_supported(
            &bridge_state_data, &limits, NULL, NULL, 0u));
        check_equal(
            quickjs_sandbox_runtime_init(
                &runtime, &sandbox,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_true(quickjs_cmeta_bridge_init(
            &bridge, &runtime, &bridge_state_data,
            &limits, NULL, NULL));
        check_true(quickjs_cmeta_import_root(
            &bridge, &committed));
        check_equal(
            quickjs_sandbox_runtime_eval(
                &runtime, script, sizeof(script) - 1u,
                "<bridge-transaction>",
                sandbox.max_eval_milliseconds,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);

        check_true(quickjs_cmeta_state_snapshot(
            &scratch, &bridge_state_data,
            &committed, limits.max_snapshot_bytes));
        bridge.properties = 0u;
        check_true(quickjs_cmeta_export_root(
            &bridge, scratch.value));

        check_equal(committed.marker, 1);
        check_equal(committed.values.count, (size_t)2u);
        check_equal(committed.values.values[0], 2);
        check_equal(committed.values.values[1], 3);

        check_true(quickjs_cmeta_state_publish(
            &bridge_state_data, &committed, &scratch));
        check_equal(committed.marker, 7);
        check_equal(committed.values.count, (size_t)3u);
        check_equal(committed.values.values[0], 4);
        check_equal(committed.values.values[1], 5);
        check_equal(committed.values.values[2], 6);

        quickjs_cmeta_state_scratch_destroy(
            &scratch, &bridge_state_data);
        quickjs_sandbox_runtime_destroy(&runtime);
    }

    it("fails property and snapshot bounds before committed-state mutation") {
        quickjs_cmeta_limits limits = bridge_limits();
        quickjs_cmeta_state_scratch scratch = {0};
        bridge_state committed = {
            .marker = 5,
            .values = {{1, 2}, 2u}};

        limits.max_properties = 1u;
        check_false(quickjs_cmeta_schema_supported(
            &bridge_state_data, &limits, NULL, NULL, 0u));

        limits = bridge_limits();
        check_false(quickjs_cmeta_state_snapshot(
            &scratch, &bridge_state_data, &committed,
            sizeof(bridge_state) - 1u));
        check_null(scratch.allocation);
        check_null(scratch.value);
        check_equal(committed.marker, 5);
        check_equal(committed.values.count, (size_t)2u);
        check_equal(committed.values.values[0], 1);
        check_equal(committed.values.values[1], 2);
    }

    it("rejects canonical array overflow without publishing partial state") {
        static const char script[] =
            "marker = 8; values = [4, 5, 6];";
        quickjs_sandbox_options sandbox = sandbox_options();
        quickjs_cmeta_limits limits = bridge_limits();
        quickjs_sandbox_runtime runtime = {0};
        quickjs_cmeta_bridge bridge = {0};
        quickjs_cmeta_state_scratch scratch = {0};
        bridge_state committed = {
            .marker = 2,
            .values = {{9}, 1u}};
        char diagnostic[256] = {0};

        limits.max_array_items = 2u;
        check_equal(
            quickjs_sandbox_runtime_init(
                &runtime, &sandbox,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_true(quickjs_cmeta_bridge_init(
            &bridge, &runtime, &bridge_state_data,
            &limits, NULL, NULL));
        check_true(quickjs_cmeta_import_root(
            &bridge, &committed));
        check_equal(
            quickjs_sandbox_runtime_eval(
                &runtime, script, sizeof(script) - 1u,
                "<bridge-array-overflow>",
                sandbox.max_eval_milliseconds,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_true(quickjs_cmeta_state_snapshot(
            &scratch, &bridge_state_data,
            &committed, limits.max_snapshot_bytes));
        bridge.properties = 0u;
        check_false(quickjs_cmeta_export_root(
            &bridge, scratch.value));

        check_equal(committed.marker, 2);
        check_equal(committed.values.count, (size_t)1u);
        check_equal(committed.values.values[0], 9);

        quickjs_cmeta_state_scratch_destroy(
            &scratch, &bridge_state_data);
        quickjs_sandbox_runtime_destroy(&runtime);
    }

    it("rejects non-exact integer export transactionally") {
        static const char script[] =
            "marker = 9007199254740992;";
        quickjs_sandbox_options sandbox = sandbox_options();
        quickjs_cmeta_limits limits = bridge_limits();
        quickjs_sandbox_runtime runtime = {0};
        quickjs_cmeta_bridge bridge = {0};
        quickjs_cmeta_state_scratch scratch = {0};
        bridge_state committed = {
            .marker = 11,
            .values = {{3}, 1u}};
        char diagnostic[256] = {0};

        check_equal(
            quickjs_sandbox_runtime_init(
                &runtime, &sandbox,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_true(quickjs_cmeta_bridge_init(
            &bridge, &runtime, &bridge_state_data,
            &limits, NULL, NULL));
        check_true(quickjs_cmeta_import_root(
            &bridge, &committed));
        check_equal(
            quickjs_sandbox_runtime_eval(
                &runtime, script, sizeof(script) - 1u,
                "<bridge-exact-integer>",
                sandbox.max_eval_milliseconds,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_true(quickjs_cmeta_state_snapshot(
            &scratch, &bridge_state_data,
            &committed, limits.max_snapshot_bytes));
        bridge.properties = 0u;
        check_false(quickjs_cmeta_export_root(
            &bridge, scratch.value));

        check_equal(committed.marker, 11);
        check_equal(committed.values.count, (size_t)1u);
        check_equal(committed.values.values[0], 3);

        quickjs_cmeta_state_scratch_destroy(
            &scratch, &bridge_state_data);
        quickjs_sandbox_runtime_destroy(&runtime);
    }

    it("rolls back type-invalid JavaScript export without mutating committed state") {
        static const char script[] =
            "marker = 9; values = ['bad'];";
        quickjs_sandbox_options sandbox = sandbox_options();
        quickjs_cmeta_limits limits = bridge_limits();
        quickjs_sandbox_runtime runtime = {0};
        quickjs_cmeta_bridge bridge = {0};
        quickjs_cmeta_state_scratch scratch = {0};
        bridge_state committed = {
            .marker = 3,
            .values = {{8}, 1u}};
        char diagnostic[256] = {0};

        check_equal(
            quickjs_sandbox_runtime_init(
                &runtime, &sandbox,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_true(quickjs_cmeta_bridge_init(
            &bridge, &runtime, &bridge_state_data,
            &limits, NULL, NULL));
        check_true(quickjs_cmeta_import_root(
            &bridge, &committed));
        check_equal(
            quickjs_sandbox_runtime_eval(
                &runtime, script, sizeof(script) - 1u,
                "<bridge-rollback>",
                sandbox.max_eval_milliseconds,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_true(quickjs_cmeta_state_snapshot(
            &scratch, &bridge_state_data,
            &committed, limits.max_snapshot_bytes));
        bridge.properties = 0u;
        check_false(quickjs_cmeta_export_root(
            &bridge, scratch.value));

        check_equal(committed.marker, 3);
        check_equal(committed.values.count, (size_t)1u);
        check_equal(committed.values.values[0], 8);

        quickjs_cmeta_state_scratch_destroy(
            &scratch, &bridge_state_data);
        quickjs_sandbox_runtime_destroy(&runtime);
    }
}
