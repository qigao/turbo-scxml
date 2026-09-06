#include <voicexml/cmeta.h>

#include "tinytest.h"
#include "voicexml_cmeta_internal.h"
#include "voicexml_test_allocator.h"

#include <cmeta/cmeta.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct vxml_cmeta_session_text {
    unsigned char bytes[16];
    size_t size;
    void *resource;
} vxml_cmeta_session_text;

typedef struct vxml_cmeta_session_root {
    int value;
    int other;
    int late;
    bool flag;
    vxml_cmeta_session_text text;
    size_t total;
    double ratio;
} vxml_cmeta_session_root;

static const cmeta_type_identity session_root_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.voicexml.cmeta.session.root");
static const cmeta_type_traits session_root_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY
};
static size_t session_text_copy_calls;
static size_t session_text_assign_calls;
static size_t session_text_read_calls;
static size_t session_text_live_resources;
static size_t session_text_invalid_operations;
static size_t session_text_fail_copy_call = SIZE_MAX;
static size_t session_text_fail_assign_call = SIZE_MAX;
static size_t session_text_fail_read_call = SIZE_MAX;

static bool session_text_copy(void *destination, const void *source) {
    vxml_cmeta_session_text *out = (vxml_cmeta_session_text *)destination;
    const vxml_cmeta_session_text *in =
        (const vxml_cmeta_session_text *)source;
    if (out == NULL || in == NULL || in->size > sizeof(in->bytes))
        return false;
    memset(out, 0, sizeof(*out));
    ++session_text_copy_calls;
    if (session_text_copy_calls == session_text_fail_copy_call)
        return false;
    if (in->size != 0u) {
        out->resource = malloc(1u);
        if (out->resource == NULL) return false;
        ++session_text_live_resources;
        memcpy(out->bytes, in->bytes, in->size);
        out->size = in->size;
    }
    return true;
}

static void session_text_restore_zero(void *object);

static void session_text_move(void *destination, void *source) {
    vxml_cmeta_session_text *out = (vxml_cmeta_session_text *)destination;
    vxml_cmeta_session_text *in = (vxml_cmeta_session_text *)source;
    if (out == NULL || in == NULL) return;
    *out = *in;
    memset(in, 0, sizeof(*in));
}

static void session_text_destroy(void *object) {
    session_text_restore_zero(object);
}

static const cmeta_type_traits session_text_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = session_text_copy,
    .move_construct = session_text_move,
    .destroy = session_text_destroy
};
static const cmeta_type_identity session_text_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.voicexml.cmeta.session.text");
static const cmeta_type_desc session_text_type = {
    .name = "vxml_cmeta_session_text",
    .size = sizeof(vxml_cmeta_session_text),
    .align = _Alignof(vxml_cmeta_session_text),
    .kind = CMETA_T_OBJECT,
    .traits = &session_text_traits,
    .identity = &session_text_identity
};
static const cmeta_type_desc session_root_type = {
    .name = "vxml_cmeta_session_root",
    .size = sizeof(vxml_cmeta_session_root),
    .align = _Alignof(vxml_cmeta_session_root),
    .kind = CMETA_T_OBJECT,
    .traits = &session_root_traits,
    .identity = &session_root_identity
};
static const cmeta_data_buffer_shape session_text_shape = {
    .ownership = CMETA_DATA_BUFFER_OWNED
};
static bool session_text_is_zero(const void *object) {
    return object != NULL &&
        ((const vxml_cmeta_session_text *)object)->size == 0u &&
        ((const vxml_cmeta_session_text *)object)->resource == NULL;
}
static cmeta_status session_text_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
    vxml_cmeta_session_text *text = (vxml_cmeta_session_text *)object;
    if (text == NULL || (size != 0u && data == NULL))
        return CMETA_INVALID_ARGUMENT;
    if (size > max_bytes || size > sizeof(text->bytes))
        return CMETA_CAPACITY_EXCEEDED;
    ++session_text_assign_calls;
    if (session_text_assign_calls == session_text_fail_assign_call) {
        text->resource = malloc(1u);
        if (text->resource != NULL) ++session_text_live_resources;
        text->size = size;
        return CMETA_OUT_OF_MEMORY;
    }
    if (size != 0u) {
        text->resource = malloc(1u);
        if (text->resource == NULL) return CMETA_OUT_OF_MEMORY;
        ++session_text_live_resources;
    }
    if (size != 0u) memcpy(text->bytes, data, size);
    text->size = size;
    return CMETA_OK;
}
static void session_text_restore_zero(void *object) {
    vxml_cmeta_session_text *text = (vxml_cmeta_session_text *)object;
    if (text == NULL) return;
    if (text->resource != NULL) {
        if (session_text_live_resources == 0u)
            ++session_text_invalid_operations;
        else
            --session_text_live_resources;
        free(text->resource);
    }
    memset(text->bytes, 0, sizeof(text->bytes));
    text->size = 0u;
    text->resource = NULL;
}
static cmeta_status session_text_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
    const vxml_cmeta_session_text *text =
        (const vxml_cmeta_session_text *)object;
    if (text == NULL || out_data == NULL || out_size == NULL ||
        text->size > sizeof(text->bytes))
        return CMETA_CALLBACK_ERROR;
    ++session_text_read_calls;
    if (session_text_read_calls == session_text_fail_read_call)
        return CMETA_CALLBACK_ERROR;
    *out_data = text->bytes;
    *out_size = text->size;
    return CMETA_OK;
}
static const cmeta_data_buffer_ops session_text_ops = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &session_text_type,
    .ownership = CMETA_DATA_BUFFER_OWNED,
    .is_zero = session_text_is_zero,
    .assign = session_text_assign,
    .restore_zero = session_text_restore_zero,
    .read = session_text_read
};
static const cmeta_data_desc session_text_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.voicexml.cmeta.session.text.data",
    .display_name = "VoiceXML CMeta session text",
    .kind = CMETA_DATA_STRING,
    .storage_type = &session_text_type,
    .shape = &session_text_shape,
    .buffer_ops = &session_text_ops
};
static const cmeta_field_desc session_root_layout_fields[] = {
    {"value", "int", offsetof(vxml_cmeta_session_root, value),
     sizeof(int), _Alignof(int), &cmeta_type_int, NULL},
    {"other", "int", offsetof(vxml_cmeta_session_root, other),
     sizeof(int), _Alignof(int), &cmeta_type_int, NULL},
    {"late", "int", offsetof(vxml_cmeta_session_root, late),
     sizeof(int), _Alignof(int), &cmeta_type_int, NULL},
    {"flag", "bool", offsetof(vxml_cmeta_session_root, flag),
     sizeof(bool), _Alignof(bool), &cmeta_type_bool, NULL},
    {"text", "vxml_cmeta_session_text",
     offsetof(vxml_cmeta_session_root, text), sizeof(vxml_cmeta_session_text),
     _Alignof(vxml_cmeta_session_text), &session_text_type, NULL},
    {"total", "size_t", offsetof(vxml_cmeta_session_root, total),
     sizeof(size_t), _Alignof(size_t), &cmeta_type_size, NULL},
    {"ratio", "double", offsetof(vxml_cmeta_session_root, ratio),
     sizeof(double), _Alignof(double), &cmeta_type_double, NULL}
};
static const cmeta_struct_desc session_root_layout = {
    .name = "vxml_cmeta_session_root",
    .size = sizeof(vxml_cmeta_session_root),
    .align = _Alignof(vxml_cmeta_session_root),
    .fields = session_root_layout_fields,
    .field_count = 7u
};
static const cmeta_data_field_desc session_root_fields[] = {
    {"test.voicexml.cmeta.session.root.value", "value",
     offsetof(vxml_cmeta_session_root, value), &cmeta_data_int},
    {"test.voicexml.cmeta.session.root.other", "other",
     offsetof(vxml_cmeta_session_root, other), &cmeta_data_int},
    {"test.voicexml.cmeta.session.root.late", "late",
     offsetof(vxml_cmeta_session_root, late), &cmeta_data_int},
    {"test.voicexml.cmeta.session.root.flag", "flag",
     offsetof(vxml_cmeta_session_root, flag), &cmeta_data_bool},
    {"test.voicexml.cmeta.session.root.text", "text",
     offsetof(vxml_cmeta_session_root, text), &session_text_data},
    {"test.voicexml.cmeta.session.root.total", "total",
     offsetof(vxml_cmeta_session_root, total), &cmeta_data_size},
    {"test.voicexml.cmeta.session.root.ratio", "ratio",
     offsetof(vxml_cmeta_session_root, ratio), &cmeta_data_double}
};
static const cmeta_data_struct_shape session_root_shape = {
    .layout = &session_root_layout,
    .fields = session_root_fields,
    .field_count = 7u
};
static const cmeta_data_desc session_root_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.voicexml.cmeta.session.root.data",
    .display_name = "VoiceXML CMeta session root",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &session_root_type,
    .shape = &session_root_shape
};

typedef struct mutable_session_root_contract {
    cmeta_type_traits text_traits;
    cmeta_type_desc text_type;
    cmeta_data_buffer_ops text_ops;
    cmeta_data_desc text_data;
    cmeta_field_desc layout_fields[7];
    cmeta_struct_desc layout;
    cmeta_data_field_desc fields[7];
    cmeta_data_struct_shape shape;
    cmeta_type_desc root_type;
    cmeta_data_desc root_data;
} mutable_session_root_contract;

static void mutable_session_root_contract_init(
    mutable_session_root_contract *contract) {
    memset(contract, 0, sizeof(*contract));
    contract->text_traits = session_text_traits;
    contract->text_type = session_text_type;
    contract->text_ops = session_text_ops;
    contract->text_data = session_text_data;
    memcpy(contract->layout_fields, session_root_layout_fields,
           sizeof(contract->layout_fields));
    contract->layout = session_root_layout;
    memcpy(contract->fields, session_root_fields, sizeof(contract->fields));
    contract->shape = session_root_shape;
    contract->root_type = session_root_type;
    contract->root_data = session_root_data;
    contract->text_type.traits = &contract->text_traits;
    contract->text_ops.storage_type = &contract->text_type;
    contract->text_data.storage_type = &contract->text_type;
    contract->text_data.buffer_ops = &contract->text_ops;
    contract->layout_fields[4].type = &contract->text_type;
    contract->layout.fields = contract->layout_fields;
    contract->fields[4].value = &contract->text_data;
    contract->shape.layout = &contract->layout;
    contract->shape.fields = contract->fields;
    contract->root_data.storage_type = &contract->root_type;
    contract->root_data.shape = &contract->shape;
}

static void reset_session_text_probe(void) {
    session_text_copy_calls = 0u;
    session_text_assign_calls = 0u;
    session_text_read_calls = 0u;
    session_text_invalid_operations = 0u;
    session_text_fail_copy_call = SIZE_MAX;
    session_text_fail_assign_call = SIZE_MAX;
    session_text_fail_read_call = SIZE_MAX;
}

enum { SESSION_ALLOCATION_CAPACITY = 512 };

typedef struct session_allocation_probe {
    size_t calls;
    size_t fail_on_call;
    size_t live_count;
    size_t invalid_operations;
    void *live[SESSION_ALLOCATION_CAPACITY];
} session_allocation_probe;

static session_allocation_probe session_allocations;

static size_t session_allocation_find(void *pointer) {
    size_t index;
    for (index = 0u; index < session_allocations.live_count; ++index)
        if (session_allocations.live[index] == pointer) return index;
    return SESSION_ALLOCATION_CAPACITY;
}

static bool session_allocation_should_fail(void) {
    ++session_allocations.calls;
    return session_allocations.calls == session_allocations.fail_on_call;
}

static void session_allocation_add(void *pointer) {
    if (pointer == NULL) return;
    if (session_allocations.live_count >= SESSION_ALLOCATION_CAPACITY) {
        ++session_allocations.invalid_operations;
        return;
    }
    session_allocations.live[session_allocations.live_count++] = pointer;
}

static void *session_test_malloc(size_t size) {
    void *pointer;
    if (session_allocation_should_fail()) return NULL;
    pointer = malloc(size);
    session_allocation_add(pointer);
    return pointer;
}

static void *session_test_calloc(size_t count, size_t size) {
    void *pointer;
    if (session_allocation_should_fail()) return NULL;
    pointer = calloc(count, size);
    session_allocation_add(pointer);
    return pointer;
}

static void *session_test_realloc(void *pointer, size_t size) {
    const size_t old_index = pointer != NULL
        ? session_allocation_find(pointer) : SESSION_ALLOCATION_CAPACITY;
    void *replacement;
    if (session_allocation_should_fail()) return NULL;
    replacement = realloc(pointer, size);
    if (replacement == NULL) return NULL;
    if (pointer == NULL) {
        session_allocation_add(replacement);
    } else if (old_index < SESSION_ALLOCATION_CAPACITY) {
        session_allocations.live[old_index] = replacement;
    } else {
        ++session_allocations.invalid_operations;
        session_allocation_add(replacement);
    }
    return replacement;
}

static void session_test_free(void *pointer) {
    const size_t index = session_allocation_find(pointer);
    if (pointer == NULL) return;
    if (index >= SESSION_ALLOCATION_CAPACITY) {
        ++session_allocations.invalid_operations;
        free(pointer);
        return;
    }
    session_allocations.live[index] =
        session_allocations.live[session_allocations.live_count - 1u];
    --session_allocations.live_count;
    free(pointer);
}

static const vxml_test_allocator session_test_allocator = {
    session_test_malloc,
    session_test_calloc,
    session_test_realloc,
    session_test_free
};

static vxml_cmeta_compile_options_v1 compile_options(void) {
    return (vxml_cmeta_compile_options_v1){
        .abi_version = VXML_CMETA_COMPILE_OPTIONS_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_compile_options_v1),
        .root = &session_root_data,
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

static vxml_cmeta_session_options_v1 session_options(
    const vxml_cmeta_session_root *root) {
    return (vxml_cmeta_session_options_v1){
        .abi_version = VXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_session_options_v1),
        .initial_root = root,
        .max_transaction_bytes = 4096u,
        .max_execution_steps = 8u
    };
}

static vxml_cmeta_session_data *session_data(vxml_session *session) {
    return (vxml_cmeta_session_data *)
        ((vxml_session_impl *)session->impl)->profile_data;
}

static const vxml_cmeta_program_data *program_data(
    const vxml_program *program) {
    return (const vxml_cmeta_program_data *)
        ((const vxml_program_impl *)program->impl)->profile_data;
}

static void check_session_init_rejected(
    const vxml_program *program,
    const vxml_cmeta_session_options_v1 *options,
    vxml_status expected) {
    vxml_session session = {(void *)(uintptr_t)1u};
    check_equal(vxml_session_init_cmeta(&session, program, options), expected);
    check_null(session.impl);
    vxml_session_destroy(&session);
}

static void check_root_contract_rejected_before_allocation(
    const vxml_program *program,
    const vxml_cmeta_session_options_v1 *options) {
    vxml_session session = {(void *)(uintptr_t)1u};
    const size_t copy_calls = session_text_copy_calls;
    vxml_status status;
    bool published;
    size_t allocation_calls;
    size_t final_live_count;
    size_t invalid_operations;
    memset(&session_allocations, 0, sizeof(session_allocations));
    session_allocations.fail_on_call = SIZE_MAX;
    vxml_test_allocator_set(&session_test_allocator);

    status = vxml_session_init_cmeta(&session, program, options);
    published = session.impl != NULL;
    allocation_calls = session_allocations.calls;
    vxml_session_destroy(&session);
    final_live_count = session_allocations.live_count;
    invalid_operations = session_allocations.invalid_operations;
    vxml_test_allocator_reset();

    check_equal(status, VXML_INVALID_CONTRACT);
    check_false(published);
    check_equal(allocation_calls, (size_t)0u);
    check_equal(session_text_copy_calls, copy_calls);
    check_equal(final_live_count, (size_t)0u);
    check_equal(invalid_operations, (size_t)0u);
}

static bool scope_int(
    const vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program, size_t scope,
    const char *name, bool *out_declared, bool *out_bound, int *out_value) {
    const cmeta_scope_schema *schema = &program->scopes[scope].schema;
    const cmeta_scope_view *view = &session->committed_scopes[scope].view;
    size_t slot = SIZE_MAX;
    if (cmeta_scope_find(schema, name, strlen(name), &slot) == NULL)
        return false;
    *out_declared = session->committed_declared[
        session->declared_offsets[scope] + slot] != 0u;
    *out_bound = view->bound[slot] != 0u;
    if (*out_bound)
        memcpy(out_value, view->storage + schema->slots[slot].offset,
               sizeof(*out_value));
    return true;
}

static bool scope_state(
    const vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program, size_t scope,
    const char *name, bool *out_declared, bool *out_bound) {
    const cmeta_scope_schema *schema = &program->scopes[scope].schema;
    const cmeta_scope_view *view = &session->committed_scopes[scope].view;
    size_t slot = SIZE_MAX;
    if (cmeta_scope_find(schema, name, strlen(name), &slot) == NULL)
        return false;
    *out_declared = session->committed_declared[
        session->declared_offsets[scope] + slot] != 0u;
    *out_bound = view->bound[slot] != 0u;
    return true;
}

static bool scope_text(
    const vxml_cmeta_session_data *session,
    const vxml_cmeta_program_data *program, size_t scope,
    const char *name, bool *out_declared, bool *out_bound,
    const vxml_cmeta_session_text **out_text) {
    const cmeta_scope_schema *schema = &program->scopes[scope].schema;
    const cmeta_scope_view *view = &session->committed_scopes[scope].view;
    size_t slot = SIZE_MAX;
    if (cmeta_scope_find(schema, name, strlen(name), &slot) == NULL)
        return false;
    *out_declared = session->committed_declared[
        session->declared_offsets[scope] + slot] != 0u;
    *out_bound = view->bound[slot] != 0u;
    *out_text = *out_bound
        ? (const vxml_cmeta_session_text *)(
            view->storage + schema->slots[slot].offset)
        : NULL;
    return true;
}

static bool value_view_is_clear(vxml_cmeta_value_view value) {
    return value.kind == VXML_CMETA_VALUE_UNDEFINED &&
        value.data.string.data == NULL && value.data.string.size == 0u;
}

spec("VoiceXML CMeta session execution") {
    it("reads an application scalar and clears output in a wrong state") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block expr='true'/></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {.value = 7};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_cmeta_value_view value = {
            .kind = VXML_CMETA_VALUE_STRING,
            .data.string = {(const char *)(uintptr_t)1u, 99u}};

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_cmeta_read(
                        &session, "value", sizeof("value") - 1u, &value),
                    VXML_OK);
        check_equal(value.kind, VXML_CMETA_VALUE_SINT);
        check_equal(value.data.sint, (int64_t)7);

        ((vxml_session_impl *)session.impl)->state = VXML_SESSION_RUNNING;
        value.kind = VXML_CMETA_VALUE_STRING;
        value.data.string.data = (const char *)(uintptr_t)1u;
        value.data.string.size = 99u;
        check_equal(vxml_session_cmeta_read(
                        &session, "value", sizeof("value") - 1u, &value),
                    VXML_INVALID_STATE);
        check_true(value_view_is_clear(value));
        ((vxml_session_impl *)session.impl)->state = VXML_SESSION_READY;

        check_equal(vxml_session_close(&session), VXML_OK);
        value.kind = VXML_CMETA_VALUE_STRING;
        value.data.string.data = (const char *)(uintptr_t)1u;
        value.data.string.size = 99u;
        check_equal(vxml_session_cmeta_read(
                        &session, "value", sizeof("value") - 1u, &value),
                    VXML_INVALID_STATE);
        check_equal(value.kind, VXML_CMETA_VALUE_UNDEFINED);
        check_null(value.data.string.data);
        check_equal(value.data.string.size, (size_t)0u);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }

    it("rejects invalid public read names and clears stale output") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block expr='true'/></form></vxml>";
        static const char embedded_nul[] = {'v', 'a', 'l', '\0', 'u', 'e'};
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {.value = 7};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_status statuses[6];
        bool cleared[5];
        vxml_cmeta_value_view value;
        size_t index;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
#define CHECK_INVALID_READ(call_index, call) \
        do { \
            value.kind = VXML_CMETA_VALUE_STRING; \
            value.data.string.data = (const char *)(uintptr_t)1u; \
            value.data.string.size = 99u; \
            statuses[(call_index)] = (call); \
            cleared[(call_index)] = value_view_is_clear(value); \
        } while (0)
        CHECK_INVALID_READ(0u, vxml_session_cmeta_read(
            &session, NULL, 5u, &value));
        CHECK_INVALID_READ(1u, vxml_session_cmeta_read(
            &session, "value", 0u, &value));
        CHECK_INVALID_READ(2u, vxml_session_cmeta_read(
            &session, "value.member", sizeof("value.member") - 1u, &value));
        CHECK_INVALID_READ(3u, vxml_session_cmeta_read(
            &session, embedded_nul, sizeof(embedded_nul), &value));
        CHECK_INVALID_READ(4u, vxml_session_cmeta_read(
            &session, "missing", sizeof("missing") - 1u, &value));
#undef CHECK_INVALID_READ
        statuses[5] = vxml_session_cmeta_read(
            &session, "value", sizeof("value") - 1u, NULL);
        vxml_session_destroy(&session);
        vxml_program_destroy(&program);

        for (index = 0u; index < 4u; ++index) {
            check_equal(statuses[index], VXML_INVALID_ARGUMENT);
            check_true(cleared[index]);
        }
        check_equal(statuses[4], VXML_SEMANTIC_ERROR);
        check_true(cleared[4]);
        check_equal(statuses[5], VXML_INVALID_ARGUMENT);
    }

    it("clears exit query outputs and validates state kind and index") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<exit expr='value'/></block></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {.value = 7};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_cmeta_exit_kind kind = VXML_CMETA_EXIT_NAMELIST;
        vxml_cmeta_name_view name = {
            (const char *)(uintptr_t)1u, 99u};
        vxml_cmeta_value_view value = {
            .kind = VXML_CMETA_VALUE_STRING,
            .data.string = {(const char *)(uintptr_t)1u, 99u}};
        vxml_status ready_kind;
        vxml_status ready_at;
        vxml_status range_at;
        vxml_status named_expression_at;
        vxml_status corrupt_kind;
        vxml_status corrupt_at;
        bool ready_outputs_clear;
        bool range_outputs_clear;
        bool named_expression_outputs_clear;
        bool corrupt_outputs_clear;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        ready_kind = vxml_session_cmeta_exit_kind(&session, &kind);
        ready_at = vxml_session_cmeta_exit_at(&session, 0u, &name, &value);
        ready_outputs_clear = kind == VXML_CMETA_EXIT_EMPTY &&
            name.data == NULL && name.size == 0u &&
            value_view_is_clear(value);

        check_equal(vxml_session_start(&session), VXML_OK);
        name = (vxml_cmeta_name_view){
            (const char *)(uintptr_t)1u, 99u};
        value.kind = VXML_CMETA_VALUE_STRING;
        value.data.string.data = (const char *)(uintptr_t)1u;
        value.data.string.size = 99u;
        range_at = vxml_session_cmeta_exit_at(&session, 1u, &name, &value);
        range_outputs_clear = name.data == NULL && name.size == 0u &&
            value_view_is_clear(value);

        session_data(&session)->terminal_exit.entries[0].name =
            (vxml_cmeta_name_view){
                (const char *)(uintptr_t)1u, 1u};
        name = (vxml_cmeta_name_view){
            (const char *)(uintptr_t)1u, 99u};
        value.kind = VXML_CMETA_VALUE_STRING;
        value.data.string.data = (const char *)(uintptr_t)1u;
        value.data.string.size = 99u;
        named_expression_at = vxml_session_cmeta_exit_at(
            &session, 0u, &name, &value);
        named_expression_outputs_clear =
            name.data == NULL && name.size == 0u &&
            value_view_is_clear(value);
        session_data(&session)->terminal_exit.entries[0].name =
            (vxml_cmeta_name_view){0};

        session_data(&session)->terminal_exit.kind =
            (vxml_cmeta_exit_kind)99;
        kind = VXML_CMETA_EXIT_NAMELIST;
        corrupt_kind = vxml_session_cmeta_exit_kind(&session, &kind);
        name = (vxml_cmeta_name_view){
            (const char *)(uintptr_t)1u, 99u};
        value.kind = VXML_CMETA_VALUE_STRING;
        value.data.string.data = (const char *)(uintptr_t)1u;
        value.data.string.size = 99u;
        corrupt_at = vxml_session_cmeta_exit_at(
            &session, 1u, &name, &value);
        corrupt_outputs_clear = kind == VXML_CMETA_EXIT_EMPTY &&
            name.data == NULL && name.size == 0u &&
            value_view_is_clear(value);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);

        check_equal(ready_kind, VXML_INVALID_STATE);
        check_equal(ready_at, VXML_INVALID_STATE);
        check_true(ready_outputs_clear);
        check_equal(range_at, VXML_INVALID_ARGUMENT);
        check_true(range_outputs_clear);
        check_equal(named_expression_at, VXML_INVALID_STRUCTURE);
        check_true(named_expression_outputs_clear);
        check_equal(corrupt_kind, VXML_INVALID_STRUCTURE);
        check_equal(corrupt_at, VXML_INVALID_STRUCTURE);
        check_true(corrupt_outputs_clear);
    }

    it("rejects wrong-profile and null-output public queries") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><exit/></block></form></vxml>";
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_cmeta_exit_kind kind = VXML_CMETA_EXIT_NAMELIST;
        vxml_cmeta_name_view name = {
            (const char *)(uintptr_t)1u, 99u};
        vxml_cmeta_value_view value = {
            .kind = VXML_CMETA_VALUE_STRING,
            .data.string = {(const char *)(uintptr_t)1u, 99u}};
        vxml_status wrong_read;
        vxml_status wrong_kind;
        vxml_status wrong_at;
        size_t wrong_count;
        vxml_status null_read_output;
        vxml_status null_kind_output;
        vxml_status null_name_output;
        vxml_status null_value_output;
        bool wrong_outputs_clear;
        bool surviving_outputs_clear;

        check_equal(vxml_compile(
                        source, strlen(source), NULL, &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init(&session, &program), VXML_OK);
        wrong_read = vxml_session_cmeta_read(
            &session, "value", sizeof("value") - 1u, &value);
        wrong_kind = vxml_session_cmeta_exit_kind(&session, &kind);
        wrong_count = vxml_session_cmeta_exit_count(&session);
        wrong_at = vxml_session_cmeta_exit_at(
            &session, 0u, &name, &value);
        wrong_outputs_clear = kind == VXML_CMETA_EXIT_EMPTY &&
            name.data == NULL && name.size == 0u &&
            value_view_is_clear(value);

        null_read_output = vxml_session_cmeta_read(
            &session, "value", sizeof("value") - 1u, NULL);
        null_kind_output = vxml_session_cmeta_exit_kind(&session, NULL);
        value.kind = VXML_CMETA_VALUE_STRING;
        value.data.string.data = (const char *)(uintptr_t)1u;
        value.data.string.size = 99u;
        null_name_output = vxml_session_cmeta_exit_at(
            &session, 0u, NULL, &value);
        name = (vxml_cmeta_name_view){
            (const char *)(uintptr_t)1u, 99u};
        null_value_output = vxml_session_cmeta_exit_at(
            &session, 0u, &name, NULL);
        surviving_outputs_clear = value_view_is_clear(value) &&
            name.data == NULL && name.size == 0u;

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);

        check_equal(wrong_read, VXML_INVALID_CONTRACT);
        check_equal(wrong_kind, VXML_INVALID_CONTRACT);
        check_equal(wrong_count, (size_t)0u);
        check_equal(wrong_at, VXML_INVALID_CONTRACT);
        check_true(wrong_outputs_clear);
        check_equal(null_read_output, VXML_INVALID_ARGUMENT);
        check_equal(null_kind_output, VXML_INVALID_ARGUMENT);
        check_equal(null_name_output, VXML_INVALID_ARGUMENT);
        check_equal(null_value_output, VXML_INVALID_ARGUMENT);
        check_true(surviving_outputs_clear);
    }

    it("copies an application string into session-owned read scratch") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block expr='true'/></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_cmeta_session_root root = {0};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_cmeta_value_view value = {0};
        const char *first_view = NULL;
        vxml_status first_status;
        vxml_status second_status = VXML_INVALID_STATE;
        bool first_bytes_match = false;
        bool source_independent = false;
        bool committed_independent = false;
        bool stable_before_next_read = false;
        bool scratch_reused = false;
        bool second_bytes_match = false;
        memcpy(root.text.bytes, "host", 4u);
        root.text.size = 4u;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        first_status = vxml_session_cmeta_read(
            &session, "text", sizeof("text") - 1u, &value);
        if (first_status == VXML_OK &&
            value.kind == VXML_CMETA_VALUE_STRING &&
            value.data.string.size == 4u && value.data.string.data != NULL) {
            first_view = value.data.string.data;
            first_bytes_match = memcmp(first_view, "host", 4u) == 0;
            source_independent = first_view != (const char *)root.text.bytes;
            committed_independent = first_view != (const char *)(
                (const vxml_cmeta_session_root *)session_data(&session)
                    ->committed_root.storage)->text.bytes;
        }
        memcpy(root.text.bytes, "gone", 4u);
        stable_before_next_read = first_view != NULL &&
            memcmp(first_view, "host", 4u) == 0;
        second_status = vxml_session_cmeta_read(
            &session, "text", sizeof("text") - 1u, &value);
        if (second_status == VXML_OK &&
            value.kind == VXML_CMETA_VALUE_STRING &&
            value.data.string.size == 4u && value.data.string.data != NULL) {
            scratch_reused = value.data.string.data == first_view;
            second_bytes_match =
                memcmp(value.data.string.data, "host", 4u) == 0;
        }

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);

        check_equal(first_status, VXML_OK);
        check_equal(value.kind, VXML_CMETA_VALUE_STRING);
        check_equal(value.data.string.size, (size_t)4u);
        check_true(first_bytes_match);
        check_true(source_independent);
        check_true(committed_independent);
        check_true(stable_before_next_read);
        check_equal(second_status, VXML_OK);
        check_true(scratch_reused);
        check_true(second_bytes_match);
    }

    it("returns every defined application scalar with its exact kind") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block expr='true'/></form></vxml>";
        static const char *const names[] = {
            "flag", "value", "total", "ratio"};
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = -7, .flag = true, .total = 9u, .ratio = 1.5};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_status statuses[4];
        vxml_cmeta_value_view values[4] = {{0}};
        size_t index;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        for (index = 0u; index < 4u; ++index)
            statuses[index] = vxml_session_cmeta_read(
                &session, names[index], strlen(names[index]), &values[index]);
        vxml_session_destroy(&session);
        vxml_program_destroy(&program);

        for (index = 0u; index < 4u; ++index)
            check_equal(statuses[index], VXML_OK);
        check_equal(values[0].kind, VXML_CMETA_VALUE_BOOL);
        check_true(values[0].data.boolean);
        check_equal(values[1].kind, VXML_CMETA_VALUE_SINT);
        check_equal(values[1].data.sint, INT64_C(-7));
        check_equal(values[2].kind, VXML_CMETA_VALUE_UINT);
        check_equal(values[2].data.uint_value, UINT64_C(9));
        check_equal(values[3].kind, VXML_CMETA_VALUE_FLOAT);
        check_equal(values[3].data.number, 1.5);
    }

    it("reads the nearest committed binding across every active scope") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><var name='late' expr='6'/>"
            "<var name='flag'/><form><var name='other' expr='3'/>"
            "<var name='value' expr='4'/><block>"
            "<var name='value' expr='5'/><exit/>"
            "</block></form></vxml>";
        static const char *const names[] = {
            "total", "late", "other", "value", "flag", "missing"};
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = 1, .other = 2, .late = 30, .flag = true, .total = 9u};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_status statuses[6];
        vxml_cmeta_value_view values[6] = {{0}};
        size_t index;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        for (index = 0u; index < 6u; ++index) {
            values[index].kind = VXML_CMETA_VALUE_STRING;
            values[index].data.string.data = (const char *)(uintptr_t)1u;
            values[index].data.string.size = 99u;
            statuses[index] = vxml_session_cmeta_read(
                &session, names[index], strlen(names[index]), &values[index]);
        }
        vxml_session_destroy(&session);
        vxml_program_destroy(&program);

        for (index = 0u; index < 5u; ++index)
            check_equal(statuses[index], VXML_OK);
        check_equal(values[0].kind, VXML_CMETA_VALUE_UINT);
        check_equal(values[0].data.uint_value, UINT64_C(9));
        check_equal(values[1].kind, VXML_CMETA_VALUE_SINT);
        check_equal(values[1].data.sint, INT64_C(6));
        check_equal(values[2].kind, VXML_CMETA_VALUE_SINT);
        check_equal(values[2].data.sint, INT64_C(3));
        check_equal(values[3].kind, VXML_CMETA_VALUE_SINT);
        check_equal(values[3].data.sint, INT64_C(5));
        check_equal(values[4].kind, VXML_CMETA_VALUE_UNDEFINED);
        check_null(values[4].data.string.data);
        check_equal(values[4].data.string.size, (size_t)0u);
        check_equal(statuses[5], VXML_SEMANTIC_ERROR);
        check_equal(values[5].kind, VXML_CMETA_VALUE_UNDEFINED);
        check_null(values[5].data.string.data);
        check_equal(values[5].data.string.size, (size_t)0u);
    }

    it("drops the anonymous scope after normal FIA exhaustion") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><var name='value' expr='4'/>"
            "<block><var name='value' expr='5'/></block>"
            "</form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {.value = 1};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_cmeta_value_view value = {0};
        vxml_status read_status;
        vxml_session_state state;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        state = vxml_session_get_state(&session);
        read_status = vxml_session_cmeta_read(
            &session, "value", sizeof("value") - 1u, &value);
        vxml_session_destroy(&session);
        vxml_program_destroy(&program);

        check_equal(state, VXML_SESSION_EXITED);
        check_equal(read_status, VXML_OK);
        check_equal(value.kind, VXML_CMETA_VALUE_SINT);
        check_equal(value.data.sint, INT64_C(4));
    }

    it("publishes one unnamed scalar for an exit expression") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<assign name='value' expr='5'/><exit expr='value + 1'/>"
            "</block></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {.value = 1};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_cmeta_exit_kind kind = VXML_CMETA_EXIT_EMPTY;
        vxml_cmeta_name_view name = {
            (const char *)(uintptr_t)1u, 99u};
        vxml_cmeta_value_view value = {
            .kind = VXML_CMETA_VALUE_STRING,
            .data.string = {(const char *)(uintptr_t)1u, 99u}};
        vxml_status kind_status;
        vxml_status value_status;
        size_t count;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        kind_status = vxml_session_cmeta_exit_kind(&session, &kind);
        count = vxml_session_cmeta_exit_count(&session);
        value_status = vxml_session_cmeta_exit_at(
            &session, 0u, &name, &value);
        vxml_session_destroy(&session);
        vxml_program_destroy(&program);

        check_equal(kind_status, VXML_OK);
        check_equal(kind, VXML_CMETA_EXIT_EXPRESSION);
        check_equal(count, (size_t)1u);
        check_equal(value_status, VXML_OK);
        check_null(name.data);
        check_equal(name.size, (size_t)0u);
        check_equal(value.kind, VXML_CMETA_VALUE_SINT);
        check_equal(value.data.sint, INT64_C(6));
    }

    it("publishes ordered staged namelist values in terminal-owned storage") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<var name='value' expr='4'/><var name='flag'/>"
            "<assign name='other' expr='6'/>"
            "<exit namelist='value flag other text'/>"
            "</block></form></vxml>";
        static const char *const expected_names[] = {
            "value", "flag", "other", "text"};
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_cmeta_session_root root = {
            .value = 1, .other = 2, .flag = true};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        const vxml_cmeta_program_data *compiled;
        const vxml_cmeta_action_row *exit_action;
        vxml_cmeta_exit_kind kind = VXML_CMETA_EXIT_EMPTY;
        vxml_status kind_status;
        vxml_status entry_statuses[4] = {
            VXML_INVALID_STATE, VXML_INVALID_STATE,
            VXML_INVALID_STATE, VXML_INVALID_STATE};
        vxml_cmeta_name_view names[4] = {{0}};
        vxml_cmeta_value_view values[4] = {{0}};
        vxml_cmeta_value_view ordinary = {0};
        vxml_cmeta_name_view repeated_name = {0};
        vxml_cmeta_value_view repeated_value = {0};
        vxml_status ordinary_status = VXML_INVALID_STATE;
        vxml_status repeated_status = VXML_INVALID_STATE;
        size_t count;
        size_t index;
        bool names_match[4] = {false, false, false, false};
        bool names_are_terminal_owned = true;
        bool terminal_text_matches = false;
        bool terminal_and_read_are_distinct = false;
        bool terminal_text_survives_reads_and_storage_mutation = false;
        memcpy(root.text.bytes, "term", 4u);
        root.text.size = 4u;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        compiled = program_data(&program);
        exit_action = &compiled->actions[compiled->blocks[0].action_end - 1u];
        kind_status = vxml_session_cmeta_exit_kind(&session, &kind);
        count = vxml_session_cmeta_exit_count(&session);
        if (count == 4u) {
            for (index = 0u; index < 4u; ++index) {
                entry_statuses[index] = vxml_session_cmeta_exit_at(
                    &session, index, &names[index], &values[index]);
                if (entry_statuses[index] == VXML_OK) {
                    names_match[index] =
                        names[index].size == strlen(expected_names[index]) &&
                        memcmp(names[index].data, expected_names[index],
                               names[index].size) == 0;
                    names_are_terminal_owned = names_are_terminal_owned &&
                        names[index].data != compiled->locations[
                            exit_action->first_location + index].name;
                }
            }
        }
        if (entry_statuses[3] == VXML_OK &&
            values[3].kind == VXML_CMETA_VALUE_STRING &&
            values[3].data.string.size == 4u &&
            values[3].data.string.data != NULL) {
            terminal_text_matches =
                memcmp(values[3].data.string.data, "term", 4u) == 0;
            ordinary_status = vxml_session_cmeta_read(
                &session, "text", sizeof("text") - 1u, &ordinary);
            terminal_and_read_are_distinct = ordinary_status == VXML_OK &&
                ordinary.data.string.data != values[3].data.string.data;
            memcpy(((vxml_cmeta_session_root *)session_data(&session)
                        ->committed_root.storage)->text.bytes,
                   "gone", 4u);
            repeated_status = vxml_session_cmeta_exit_at(
                &session, 3u, &repeated_name, &repeated_value);
            terminal_text_survives_reads_and_storage_mutation =
                repeated_status == VXML_OK &&
                repeated_value.data.string.data == values[3].data.string.data &&
                repeated_value.data.string.size == 4u &&
                memcmp(repeated_value.data.string.data, "term", 4u) == 0;
        }
        vxml_session_destroy(&session);
        vxml_program_destroy(&program);

        check_equal(kind_status, VXML_OK);
        check_equal(kind, VXML_CMETA_EXIT_NAMELIST);
        check_equal(count, (size_t)4u);
        for (index = 0u; index < 4u; ++index) {
            check_equal(entry_statuses[index], VXML_OK);
            check_true(names_match[index]);
        }
        check_true(names_are_terminal_owned);
        check_equal(values[0].kind, VXML_CMETA_VALUE_SINT);
        check_equal(values[0].data.sint, INT64_C(4));
        check_equal(values[1].kind, VXML_CMETA_VALUE_UNDEFINED);
        check_equal(values[2].kind, VXML_CMETA_VALUE_SINT);
        check_equal(values[2].data.sint, INT64_C(6));
        check_equal(values[3].kind, VXML_CMETA_VALUE_STRING);
        check_true(terminal_text_matches);
        check_equal(ordinary_status, VXML_OK);
        check_true(terminal_and_read_are_distinct);
        check_equal(repeated_status, VXML_OK);
        check_true(terminal_text_survives_reads_and_storage_mutation);
    }

    it("rejects corrupted namelist entry views before publishing them") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<exit namelist='value text'/></block></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_cmeta_session_root root = {.value = 7};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_cmeta_session_data *runtime;
        vxml_cmeta_exit_entry saved_value;
        vxml_cmeta_exit_entry saved_text;
        vxml_status statuses[7];
        bool cleared[7];
        vxml_cmeta_exit_kind kind = VXML_CMETA_EXIT_NAMELIST;
        vxml_status kind_status;
        size_t invalid_count;
        size_t index;
        memcpy(root.text.bytes, "abc", 3u);
        root.text.size = 3u;
        reset_session_text_probe();

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        runtime = session_data(&session);
        saved_value = runtime->terminal_exit.entries[0];
        saved_text = runtime->terminal_exit.entries[1];

#define CAPTURE_CORRUPT_AT(case_index, query_index) \
        do { \
            vxml_cmeta_name_view name = { \
                (const char *)(uintptr_t)1u, 99u}; \
            vxml_cmeta_value_view value = { \
                .kind = VXML_CMETA_VALUE_STRING, \
                .data.string = {(const char *)(uintptr_t)1u, 99u}}; \
            statuses[(case_index)] = vxml_session_cmeta_exit_at( \
                &session, (query_index), &name, &value); \
            cleared[(case_index)] = name.data == NULL && name.size == 0u && \
                value_view_is_clear(value); \
        } while (0)

        runtime->terminal_exit.entries[0].value.kind =
            (vxml_cmeta_value_kind)99;
        kind_status = vxml_session_cmeta_exit_kind(&session, &kind);
        invalid_count = vxml_session_cmeta_exit_count(&session);
        CAPTURE_CORRUPT_AT(0u, SIZE_MAX);
        runtime->terminal_exit.entries[0] = saved_value;

        runtime->terminal_exit.entries[0].name =
            (vxml_cmeta_name_view){0};
        CAPTURE_CORRUPT_AT(1u, 0u);
        runtime->terminal_exit.entries[0] = saved_value;

        runtime->terminal_exit.entries[0].name =
            (vxml_cmeta_name_view){
                (const char *)(uintptr_t)1u, 1u};
        CAPTURE_CORRUPT_AT(2u, 0u);
        runtime->terminal_exit.entries[0] = saved_value;

        runtime->terminal_exit.entries[0].name =
            (vxml_cmeta_name_view){
                runtime->terminal_exit.names +
                    runtime->terminal_exit.name_size - 1u,
                2u};
        CAPTURE_CORRUPT_AT(3u, 0u);
        runtime->terminal_exit.entries[0] = saved_value;

        runtime->terminal_exit.entries[1].value.data.string.data =
            (const char *)(uintptr_t)1u;
        runtime->terminal_exit.entries[1].value.data.string.size = 1u;
        CAPTURE_CORRUPT_AT(4u, 1u);
        runtime->terminal_exit.entries[1] = saved_text;

        runtime->terminal_exit.entries[1].value.data.string.data =
            runtime->terminal_exit.strings +
                runtime->terminal_exit.string_size - 1u;
        runtime->terminal_exit.entries[1].value.data.string.size = 2u;
        CAPTURE_CORRUPT_AT(5u, 1u);
        runtime->terminal_exit.entries[1] = saved_text;

        runtime->terminal_exit.entries[1].value.data.string.data = NULL;
        runtime->terminal_exit.entries[1].value.data.string.size = 1u;
        CAPTURE_CORRUPT_AT(6u, 1u);
        runtime->terminal_exit.entries[1] = saved_text;
#undef CAPTURE_CORRUPT_AT

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);

        check_equal(kind_status, VXML_INVALID_STRUCTURE);
        check_equal(kind, VXML_CMETA_EXIT_EMPTY);
        check_equal(invalid_count, (size_t)0u);
        for (index = 0u; index < 7u; ++index) {
            check_equal(statuses[index], VXML_INVALID_STRUCTURE);
            check_true(cleared[index]);
        }
        check_equal(session_text_live_resources, (size_t)0u);
        check_equal(session_text_invalid_operations, (size_t)0u);
    }

    it("reports empty exhaustion and explicit empty exit identically") {
        static const char *const sources[] = {
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block/></form></vxml>",
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block><exit/></block></form></vxml>"};
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {.value = 1};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        size_t index;

        for (index = 0u; index < 2u; ++index) {
            vxml_program program = {0};
            vxml_session session = {0};
            vxml_cmeta_exit_kind kind = VXML_CMETA_EXIT_NAMELIST;
            vxml_cmeta_name_view name = {
                (const char *)(uintptr_t)1u, 99u};
            vxml_cmeta_value_view value = {
                .kind = VXML_CMETA_VALUE_STRING,
                .data.string = {(const char *)(uintptr_t)1u, 99u}};

            check_equal(vxml_compile_cmeta(
                            sources[index], strlen(sources[index]),
                            NULL, &compile, &program, NULL),
                        VXML_OK);
            check_equal(vxml_session_init_cmeta(
                            &session, &program, &options),
                        VXML_OK);
            check_equal(vxml_session_start(&session), VXML_OK);
            check_equal(vxml_session_get_state(&session),
                        VXML_SESSION_EXITED);
            check_equal(vxml_session_cmeta_exit_kind(&session, &kind),
                        VXML_OK);
            check_equal(kind, VXML_CMETA_EXIT_EMPTY);
            check_equal(vxml_session_cmeta_exit_count(&session),
                        (size_t)0u);
            check_equal(vxml_session_cmeta_exit_at(
                            &session, 0u, &name, &value),
                        VXML_INVALID_ARGUMENT);
            check_null(name.data);
            check_equal(name.size, (size_t)0u);
            check_true(value_view_is_clear(value));

            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }
    }

    it("fails a self-clearing FIA loop at the execution step limit") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block name='again'>"
            "<clear namelist='again'/></block></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {7};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_LIMIT_EXCEEDED);
        check_equal(vxml_session_get_state(&session), VXML_SESSION_FAILED);
        check_equal(vxml_session_error(&session), VXML_LIMIT_EXCEEDED);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }

    it("initializes four layers and exposes staged writes to siblings") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><var name='value' expr='2'/>"
            "<form><var name='value' expr='3'/><block>"
            "<var name='value' expr='4'/>"
            "<assign name='other' expr='value'/><exit/>"
            "</block></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = 1, .other = 0, .late = 9, .flag = false};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        const vxml_cmeta_program_data *compiled;
        vxml_cmeta_session_data *runtime;
        bool declared;
        bool bound;
        int value = 0;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(vxml_session_get_state(&session), VXML_SESSION_EXITED);
        compiled = program_data(&program);
        runtime = session_data(&session);
        check_equal(
            ((const vxml_cmeta_session_root *)
                runtime->committed_root.storage)->value,
            1);
        check_equal(
            ((const vxml_cmeta_session_root *)
                runtime->committed_root.storage)->other,
            4);
        check_true(scope_int(
            runtime, compiled, compiled->document_scope, "value",
            &declared, &bound, &value));
        check_true(declared);
        check_true(bound);
        check_equal(value, 2);
        check_true(scope_int(
            runtime, compiled, compiled->forms[0].scope, "value",
            &declared, &bound, &value));
        check_true(declared);
        check_true(bound);
        check_equal(value, 3);
        check_true(scope_int(
            runtime, compiled, compiled->blocks[0].scope, "value",
            &declared, &bound, &value));
        check_true(declared);
        check_true(bound);
        check_equal(value, 4);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }

    it("takes the first matching branch and declares only executed vars") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><var name='value' expr='1'/><form>"
            "<var name='other' expr='2'/><block>"
            "<if cond='false'><var name='flag' expr='true'/>"
            "<var name='late' expr='90'/><elseif cond='true'/>"
            "<assign name='other' expr='4'/><var name='late' expr='5'/>"
            "<else/><assign name='other' expr='6'/></if>"
            "<assign name='value' expr='late'/><exit/>"
            "</block></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = 10, .other = 20, .late = 9, .flag = false};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        const vxml_cmeta_program_data *compiled;
        vxml_cmeta_session_data *runtime;
        bool declared;
        bool bound;
        int value = 0;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(vxml_session_get_state(&session), VXML_SESSION_EXITED);
        compiled = program_data(&program);
        runtime = session_data(&session);
        check_true(scope_int(
            runtime, compiled, compiled->forms[0].scope, "other",
            &declared, &bound, &value));
        check_true(declared);
        check_true(bound);
        check_equal(value, 4);
        check_true(scope_int(
            runtime, compiled, compiled->blocks[0].scope, "late",
            &declared, &bound, &value));
        check_true(declared);
        check_true(bound);
        check_equal(value, 5);
        check_true(scope_state(
            runtime, compiled, compiled->blocks[0].scope, "flag",
            &declared, &bound));
        check_false(declared);
        check_false(bound);
        check_equal(
            ((const vxml_cmeta_session_root *)
                runtime->committed_root.storage)->late,
            9);
        check_true(scope_int(
            runtime, compiled, compiled->document_scope, "value",
            &declared, &bound, &value));
        check_true(bound);
        check_equal(value, 5);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }

    it("rejects an IF branch that escapes into another block") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form>"
            "<block name='first'><if cond='true'>"
            "<assign name='value' expr='99'/></if></block>"
            "<block name='second' expr='true'>"
            "<assign name='other' expr='77'/></block>"
            "</form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = 1, .other = 2, .late = 3, .flag = false};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_cmeta_program_data *compiled;
        vxml_cmeta_action_row *conditional;
        vxml_cmeta_branch_row *branch;
        const vxml_cmeta_session_root *committed;
        vxml_status status;
        vxml_status repeated_start;
        vxml_status stable_error;
        vxml_session_state state;
        int committed_value;
        int committed_other;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        compiled = (vxml_cmeta_program_data *)(void *)program_data(&program);
        check_equal(compiled->block_count, (size_t)2u);
        conditional = &compiled->actions[compiled->blocks[0].first_action];
        check_equal(conditional->kind, VXML_CMETA_ACTION_IF);
        branch = &compiled->branches[conditional->first_branch];
        branch->first_action = compiled->blocks[1].first_action;
        branch->action_end = compiled->blocks[1].action_end;

        status = vxml_session_start(&session);
        state = vxml_session_get_state(&session);
        stable_error = vxml_session_error(&session);
        committed = (const vxml_cmeta_session_root *)
            session_data(&session)->committed_root.storage;
        committed_value = committed->value;
        committed_other = committed->other;
        repeated_start = vxml_session_start(&session);
        vxml_session_destroy(&session);
        vxml_program_destroy(&program);

        check_equal(status, VXML_INVALID_STRUCTURE);
        check_equal(state, VXML_SESSION_FAILED);
        check_equal(stable_error, VXML_INVALID_STRUCTURE);
        check_equal(repeated_start, VXML_INVALID_STATE);
        check_equal(committed_value, 1);
        check_equal(committed_other, 2);
    }

    it("rejects an out-of-range repeated-var assignment scope") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<if cond='false'><var name='late' expr='1'/></if>"
            "<var name='late' expr='2'/></block></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = 1, .other = 2, .late = 3, .flag = false};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_cmeta_program_data *compiled;
        vxml_cmeta_action_row *repeated;
        const vxml_cmeta_session_root *committed;
        vxml_status status;
        vxml_status stable_error;
        vxml_status repeated_start;
        vxml_session_state state;
        bool form_item_bound;
        int committed_late;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        compiled = (vxml_cmeta_program_data *)(void *)program_data(&program);
        repeated = &compiled->actions[compiled->blocks[0].action_end - 1u];
        check_equal(repeated->kind, VXML_CMETA_ACTION_ASSIGN);
        check_equal(repeated->scope, compiled->blocks[0].scope);
        repeated->scope = compiled->scope_count;

        status = vxml_session_start(&session);
        state = vxml_session_get_state(&session);
        stable_error = vxml_session_error(&session);
        repeated_start = vxml_session_start(&session);
        committed = (const vxml_cmeta_session_root *)
            session_data(&session)->committed_root.storage;
        committed_late = committed->late;
        form_item_bound = session_data(&session)->committed_scopes[
            compiled->forms[0].scope].view.bound[
                compiled->blocks[0].form_item_slot] != 0u;
        vxml_session_destroy(&session);
        vxml_program_destroy(&program);

        check_equal(status, VXML_INVALID_STRUCTURE);
        check_equal(state, VXML_SESSION_FAILED);
        check_equal(stable_error, VXML_INVALID_STRUCTURE);
        check_equal(repeated_start, VXML_INVALID_STATE);
        check_equal(committed_late, 3);
        check_false(form_item_bound);
    }

    it("initializes block items and skips false FIA guards in document order") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form>"
            "<block name='initialized' expr='true'>"
            "<assign name='value' expr='90'/></block>"
            "<block name='guarded' cond='false'><exit/></block>"
            "<block name='third'><assign name='flag' expr='third'/>"
            "<assign name='value' expr='3'/></block>"
            "<block name='fourth'><assign name='other' expr='value + 1'/>"
            "</block></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = 1, .other = 2, .late = 3, .flag = false};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        const vxml_cmeta_program_data *compiled;
        vxml_cmeta_session_data *runtime;
        const vxml_cmeta_session_root *committed;
        bool declared;
        bool bound;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(vxml_session_get_state(&session), VXML_SESSION_EXITED);
        compiled = program_data(&program);
        runtime = session_data(&session);
        committed = (const vxml_cmeta_session_root *)
            runtime->committed_root.storage;
        check_equal(committed->value, 3);
        check_equal(committed->other, 4);
        check_true(committed->flag);
        check_true(scope_state(
            runtime, compiled, compiled->forms[0].scope, "initialized",
            &declared, &bound));
        check_true(declared);
        check_true(bound);
        check_true(scope_state(
            runtime, compiled, compiled->forms[0].scope, "guarded",
            &declared, &bound));
        check_true(declared);
        check_false(bound);
        check_true(scope_state(
            runtime, compiled, compiled->forms[0].scope, "third",
            &declared, &bound));
        check_true(bound);
        check_true(scope_state(
            runtime, compiled, compiled->forms[0].scope, "fourth",
            &declared, &bound));
        check_true(bound);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }

    it("keeps anonymous values across a named clear revisit") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block name='again'>"
            "<var name='late'/>"
            "<if cond='value == 1'><assign name='late' expr='7'/>"
            "<clear namelist='again'/></if>"
            "<if cond='value == 2'><assign name='other' expr='late'/></if>"
            "<assign name='value' expr='value + 1'/>"
            "</block></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = 1, .other = 0, .late = 9, .flag = false};
        vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        const vxml_cmeta_program_data *compiled;
        vxml_cmeta_session_data *runtime;
        const vxml_cmeta_session_root *committed;
        bool declared;
        bool bound;
        int value = 0;
        options.max_execution_steps = 32u;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(vxml_session_get_state(&session), VXML_SESSION_EXITED);
        compiled = program_data(&program);
        runtime = session_data(&session);
        committed = (const vxml_cmeta_session_root *)
            runtime->committed_root.storage;
        check_equal(committed->value, 3);
        check_equal(committed->other, 7);
        check_true(scope_int(
            runtime, compiled, compiled->blocks[0].scope, "late",
            &declared, &bound, &value));
        check_true(declared);
        check_true(bound);
        check_equal(value, 7);
        check_true(scope_state(
            runtime, compiled, compiled->forms[0].scope, "again",
            &declared, &bound));
        check_true(bound);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }

    it("empty clear resets every form item but no ordinary variables") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><var name='other' expr='7'/>"
            "<block name='first' cond='value == 0'>"
            "<assign name='value' expr='1'/></block>"
            "<block expr='true' cond='false'/>"
            "<block name='reset' cond='value == 1'><clear/>"
            "<assign name='value' expr='2'/></block>"
            "</form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = 0, .other = 0, .late = 0, .flag = false};
        vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        const vxml_cmeta_program_data *compiled;
        vxml_cmeta_session_data *runtime;
        bool declared;
        bool bound;
        int value = 0;
        size_t block_offset;
        options.max_execution_steps = 32u;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(vxml_session_get_state(&session), VXML_SESSION_EXITED);
        compiled = program_data(&program);
        runtime = session_data(&session);
        check_equal(
            ((const vxml_cmeta_session_root *)
                runtime->committed_root.storage)->value,
            2);
        check_true(scope_int(
            runtime, compiled, compiled->forms[0].scope, "other",
            &declared, &bound, &value));
        check_true(declared);
        check_true(bound);
        check_equal(value, 7);
        for (block_offset = 0u;
             block_offset < compiled->forms[0].block_count;
             ++block_offset) {
            const vxml_cmeta_block_row *block = &compiled->blocks[
                compiled->forms[0].first_block + block_offset];
            check_false(runtime->committed_scopes[
                compiled->forms[0].scope].view.bound[
                    block->form_item_slot] != 0u);
        }

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }

    it("keeps undefined distinct from false zero and an empty string") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><var name='value' expr='other'/>"
            "<var name='flag' expr='false'/><var name='text' expr='&quot;&quot;'/>"
            "<var name='late'/><form><block expr='true'/></form></vxml>";
        static const char late_name[] = "late";
        const vxml_cmeta_name_view undefined[] = {
            {late_name, sizeof(late_name) - 1u}};
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_cmeta_session_root root = {
            .value = 7, .other = 0, .late = 9, .flag = true};
        vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        const vxml_cmeta_program_data *compiled;
        vxml_cmeta_session_data *runtime;
        const vxml_cmeta_session_text *text_value;
        bool declared;
        bool bound;
        int value = 1;
        memcpy(root.text.bytes, "host", 4u);
        root.text.size = 4u;
        options.initially_undefined = undefined;
        options.initially_undefined_count = 1u;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        compiled = program_data(&program);
        runtime = session_data(&session);
        check_false(runtime->committed_root.bound[2] != 0u);
        check_true(runtime->committed_root.bound[1] != 0u);
        check_equal(
            ((const vxml_cmeta_session_root *)
                runtime->committed_root.storage)->other,
            0);
        check_true(scope_int(
            runtime, compiled, compiled->document_scope, "value",
            &declared, &bound, &value));
        check_true(declared);
        check_true(bound);
        check_equal(value, 0);
        check_true(scope_state(
            runtime, compiled, compiled->document_scope, "flag",
            &declared, &bound));
        check_true(declared);
        check_true(bound);
        check_false(*(const bool *)(
            runtime->committed_scopes[compiled->document_scope].view.storage +
            program_data(&program)->scopes[compiled->document_scope]
                .schema.slots[1].offset));
        check_true(scope_text(
            runtime, compiled, compiled->document_scope, "text",
            &declared, &bound, &text_value));
        check_true(declared);
        check_true(bound);
        check_not_null(text_value);
        check_equal(text_value->size, (size_t)0u);
        check_true(scope_state(
            runtime, compiled, compiled->document_scope, "late",
            &declared, &bound));
        check_true(declared);
        check_false(bound);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }

    it("rolls back source initialization after an undefined forward value") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><var name='value' expr='late + 1'/>"
            "<var name='late' expr='5'/><form><block/></form></vxml>";
        static const char late_name[] = "late";
        const vxml_cmeta_name_view undefined[] = {
            {late_name, sizeof(late_name) - 1u}};
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = 10, .other = 20, .late = 30, .flag = false};
        vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        const vxml_cmeta_program_data *compiled;
        vxml_cmeta_session_data *runtime;
        bool declared;
        bool bound;
        options.initially_undefined = undefined;
        options.initially_undefined_count = 1u;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_SEMANTIC_ERROR);
        check_equal(vxml_session_get_state(&session), VXML_SESSION_FAILED);
        check_equal(vxml_session_error(&session), VXML_SEMANTIC_ERROR);
        check_equal(vxml_session_error(&session), VXML_SEMANTIC_ERROR);
        compiled = program_data(&program);
        runtime = session_data(&session);
        check_equal(
            ((const vxml_cmeta_session_root *)
                runtime->committed_root.storage)->value,
            10);
        check_true(scope_state(
            runtime, compiled, compiled->document_scope, "value",
            &declared, &bound));
        check_false(declared);
        check_false(bound);
        check_equal(vxml_session_start(&session), VXML_INVALID_STATE);
        check_equal(vxml_session_error(&session), VXML_SEMANTIC_ERROR);
        check_equal(vxml_session_close(&session), VXML_OK);
        check_equal(vxml_session_close(&session), VXML_OK);
        vxml_session_destroy(&session);
        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }

    it("rolls back earlier turn writes after an expression failure") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block name='turn'>"
            "<assign name='other' expr='5'/>"
            "<exit expr='late + 1'/></block></form></vxml>";
        static const char late_name[] = "late";
        const vxml_cmeta_name_view undefined[] = {
            {late_name, sizeof(late_name) - 1u}};
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = 1, .other = 2, .late = 3, .flag = false};
        vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        const vxml_cmeta_program_data *compiled;
        vxml_cmeta_session_data *runtime;
        const vxml_cmeta_session_root *committed;
        vxml_cmeta_exit_kind kind = VXML_CMETA_EXIT_NAMELIST;
        vxml_status kind_status;
        size_t exit_count;
        options.initially_undefined = undefined;
        options.initially_undefined_count = 1u;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_SEMANTIC_ERROR);
        check_equal(vxml_session_get_state(&session), VXML_SESSION_FAILED);
        check_equal(vxml_session_error(&session), VXML_SEMANTIC_ERROR);
        compiled = program_data(&program);
        runtime = session_data(&session);
        committed = (const vxml_cmeta_session_root *)
            runtime->committed_root.storage;
        check_equal(committed->value, 1);
        check_equal(committed->other, 2);
        check_false(runtime->committed_scopes[
            compiled->forms[0].scope].view.bound[
                compiled->blocks[0].form_item_slot] != 0u);
        kind_status = vxml_session_cmeta_exit_kind(&session, &kind);
        exit_count = vxml_session_cmeta_exit_count(&session);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
        check_equal(kind_status, VXML_INVALID_STATE);
        check_equal(kind, VXML_CMETA_EXIT_EMPTY);
        check_equal(exit_count, (size_t)0u);
    }

    it("rolls back a partial namelist when its string adapter read fails") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<assign name='value' expr='5'/>"
            "<exit namelist='value text'/></block></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_cmeta_session_root root = {.value = 1};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_cmeta_value_view committed = {0};
        vxml_cmeta_exit_kind kind = VXML_CMETA_EXIT_NAMELIST;
        vxml_cmeta_name_view name = {
            (const char *)(uintptr_t)1u, 99u};
        vxml_cmeta_value_view entry = {
            .kind = VXML_CMETA_VALUE_STRING,
            .data.string = {(const char *)(uintptr_t)1u, 99u}};
        vxml_status status;
        vxml_status read_status;
        vxml_status kind_status;
        vxml_status at_status;
        vxml_session_state state;
        vxml_status stable_error;
        size_t count;
        bool outputs_clear;
        memcpy(root.text.bytes, "old", 3u);
        root.text.size = 3u;
        reset_session_text_probe();

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        session_text_fail_read_call = session_text_read_calls + 1u;
        status = vxml_session_start(&session);
        state = vxml_session_get_state(&session);
        stable_error = vxml_session_error(&session);
        read_status = vxml_session_cmeta_read(
            &session, "value", sizeof("value") - 1u, &committed);
        kind_status = vxml_session_cmeta_exit_kind(&session, &kind);
        count = vxml_session_cmeta_exit_count(&session);
        at_status = vxml_session_cmeta_exit_at(
            &session, 0u, &name, &entry);
        outputs_clear = kind == VXML_CMETA_EXIT_EMPTY &&
            name.data == NULL && name.size == 0u &&
            value_view_is_clear(entry);

        session_text_fail_read_call = SIZE_MAX;
        vxml_session_destroy(&session);
        vxml_program_destroy(&program);

        check_equal(status, VXML_SEMANTIC_ERROR);
        check_equal(state, VXML_SESSION_FAILED);
        check_equal(stable_error, VXML_SEMANTIC_ERROR);
        check_equal(read_status, VXML_OK);
        check_equal(committed.kind, VXML_CMETA_VALUE_SINT);
        check_equal(committed.data.sint, INT64_C(1));
        check_equal(kind_status, VXML_INVALID_STATE);
        check_equal(count, (size_t)0u);
        check_equal(at_status, VXML_INVALID_STATE);
        check_true(outputs_clear);
        check_equal(session_text_live_resources, (size_t)0u);
        check_equal(session_text_invalid_operations, (size_t)0u);
    }

    it("rolls back earlier writes for a runtime-undeclared namelist") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<if cond='false'><var name='late' expr='9'/></if>"
            "<assign name='other' expr='5'/>"
            "<exit namelist='late'/></block></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {.late = 3, .other = 2};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_cmeta_program_data *compiled;
        vxml_cmeta_action_row *exit_action;
        vxml_cmeta_location_row *exit_location;
        vxml_cmeta_exit_kind kind = VXML_CMETA_EXIT_NAMELIST;
        vxml_status status;
        vxml_status stable_error;
        vxml_status repeated_start;
        vxml_status kind_status;
        vxml_session_state state;
        int committed_other;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        compiled = (vxml_cmeta_program_data *)(void *)program_data(&program);
        exit_action = &compiled->actions[compiled->blocks[0].action_end - 1u];
        check_equal(exit_action->kind, VXML_CMETA_ACTION_EXIT);
        exit_location = &compiled->locations[exit_action->first_location];
        check_true(exit_location->candidate_count > 1u);
        check_equal(compiled->location_candidates[
                        exit_location->first_candidate].scope,
                    compiled->blocks[0].scope);
        exit_location->candidate_count = 1u;

        status = vxml_session_start(&session);
        state = vxml_session_get_state(&session);
        stable_error = vxml_session_error(&session);
        repeated_start = vxml_session_start(&session);
        committed_other = ((const vxml_cmeta_session_root *)
            session_data(&session)->committed_root.storage)->other;
        kind_status = vxml_session_cmeta_exit_kind(&session, &kind);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);

        check_equal(status, VXML_SEMANTIC_ERROR);
        check_equal(state, VXML_SESSION_FAILED);
        check_equal(stable_error, VXML_SEMANTIC_ERROR);
        check_equal(repeated_start, VXML_INVALID_STATE);
        check_equal(committed_other, 2);
        check_equal(kind_status, VXML_INVALID_STATE);
        check_equal(kind, VXML_CMETA_EXIT_EMPTY);
    }

    it("rolls back earlier turn writes after an exact narrowing failure") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<assign name='other' expr='5'/>"
            "<assign name='value' expr='2147483648'/></block>"
            "</form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = 1, .other = 2, .late = 3, .flag = false};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        const vxml_cmeta_session_root *committed;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_SEMANTIC_ERROR);
        check_equal(vxml_session_get_state(&session), VXML_SESSION_FAILED);
        committed = (const vxml_cmeta_session_root *)
            session_data(&session)->committed_root.storage;
        check_equal(committed->value, 1);
        check_equal(committed->other, 2);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }

    it("rolls back a turn when its deterministic step budget is exhausted") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<assign name='other' expr='5'/>"
            "<assign name='value' expr='6'/></block></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = 1, .other = 2, .late = 3, .flag = false};
        vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        const vxml_cmeta_session_root *committed;
        options.max_execution_steps = 2u;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_LIMIT_EXCEEDED);
        check_equal(vxml_session_get_state(&session), VXML_SESSION_FAILED);
        check_equal(vxml_session_error(&session), VXML_LIMIT_EXCEEDED);
        committed = (const vxml_cmeta_session_root *)
            session_data(&session)->committed_root.storage;
        check_equal(committed->value, 1);
        check_equal(committed->other, 2);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }

    it("unwinds an initial managed field copy failure before publication") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block expr='true'/></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_cmeta_session_root root = {
            .value = 1, .other = 2, .late = 3, .flag = false};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        memcpy(root.text.bytes, "old", 3u);
        root.text.size = 3u;
        check_equal(session_text_live_resources, (size_t)0u);
        reset_session_text_probe();
        session_text_fail_copy_call = 1u;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_ALLOCATION_FAILED);
        check_null(session.impl);
        check_equal(session_text_live_resources, (size_t)0u);
        check_equal(session_text_invalid_operations, (size_t)0u);

        session_text_fail_copy_call = SIZE_MAX;
        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }

    it("rolls back a managed copy failure at turn admission") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<assign name='value' expr='2'/></block></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_cmeta_session_root root = {
            .value = 1, .other = 2, .late = 3, .flag = false};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        const vxml_cmeta_session_text *committed_text;
        memcpy(root.text.bytes, "old", 3u);
        root.text.size = 3u;
        check_equal(session_text_live_resources, (size_t)0u);
        reset_session_text_probe();

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(session_text_live_resources, (size_t)1u);
        session_text_fail_copy_call = session_text_copy_calls + 2u;
        check_equal(vxml_session_start(&session), VXML_ALLOCATION_FAILED);
        check_equal(vxml_session_get_state(&session), VXML_SESSION_FAILED);
        check_equal(vxml_session_error(&session), VXML_ALLOCATION_FAILED);
        check_equal(
            ((const vxml_cmeta_session_root *)
                session_data(&session)->committed_root.storage)->value,
            1);
        committed_text = &((const vxml_cmeta_session_root *)
            session_data(&session)->committed_root.storage)->text;
        check_equal(committed_text->size, (size_t)3u);
        check_equal(memcmp(committed_text->bytes, "old", 3u), 0);
        check_equal(session_text_live_resources, (size_t)1u);
        check_equal(session_text_invalid_operations, (size_t)0u);

        session_text_fail_copy_call = SIZE_MAX;
        vxml_session_destroy(&session);
        check_equal(session_text_live_resources, (size_t)0u);
        check_equal(session_text_invalid_operations, (size_t)0u);
        vxml_program_destroy(&program);
    }

    it("rolls back a managed assignment failure during initialization") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><var name='text' expr='&quot;doc&quot;'/>"
            "<form><block expr='true'/></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_cmeta_session_root root = {
            .value = 1, .other = 2, .late = 3, .flag = false};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        bool declared;
        bool bound;
        memcpy(root.text.bytes, "old", 3u);
        root.text.size = 3u;
        check_equal(session_text_live_resources, (size_t)0u);
        reset_session_text_probe();

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        session_text_fail_assign_call = 1u;
        check_equal(vxml_session_start(&session), VXML_ALLOCATION_FAILED);
        check_equal(vxml_session_get_state(&session), VXML_SESSION_FAILED);
        check_true(scope_state(
            session_data(&session), program_data(&program),
            program_data(&program)->document_scope, "text",
            &declared, &bound));
        check_false(declared);
        check_false(bound);
        check_equal(session_text_live_resources, (size_t)1u);
        check_equal(session_text_invalid_operations, (size_t)0u);

        session_text_fail_assign_call = SIZE_MAX;
        vxml_session_destroy(&session);
        check_equal(session_text_live_resources, (size_t)0u);
        check_equal(session_text_invalid_operations, (size_t)0u);
        vxml_program_destroy(&program);
    }

    it("rolls back earlier writes after a managed assignment failure") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<assign name='value' expr='2'/>"
            "<assign name='text' expr='&quot;new&quot;'/></block>"
            "</form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_cmeta_session_root root = {
            .value = 1, .other = 2, .late = 3, .flag = false};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        const vxml_cmeta_session_root *committed;
        memcpy(root.text.bytes, "old", 3u);
        root.text.size = 3u;
        check_equal(session_text_live_resources, (size_t)0u);
        reset_session_text_probe();

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        session_text_fail_assign_call = 1u;
        check_equal(vxml_session_start(&session), VXML_ALLOCATION_FAILED);
        check_equal(vxml_session_get_state(&session), VXML_SESSION_FAILED);
        committed = (const vxml_cmeta_session_root *)
            session_data(&session)->committed_root.storage;
        check_equal(committed->value, 1);
        check_equal(committed->text.size, (size_t)3u);
        check_equal(memcmp(committed->text.bytes, "old", 3u), 0);
        check_equal(session_text_live_resources, (size_t)1u);
        check_equal(session_text_invalid_operations, (size_t)0u);

        session_text_fail_assign_call = SIZE_MAX;
        vxml_session_destroy(&session);
        check_equal(session_text_live_resources, (size_t)0u);
        check_equal(session_text_invalid_operations, (size_t)0u);
        vxml_program_destroy(&program);
    }

    it("unwinds every core allocation failure during session init") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><var name='value' expr='1'/><form>"
            "<var name='other' expr='2'/><block>"
            "<var name='late' expr='3'/><assign name='value' expr='4'/>"
            "</block></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = 1, .other = 2, .late = 3, .flag = false};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        size_t program_live;
        size_t failure_point;
        bool reached_success = false;
        memset(&session_allocations, 0, sizeof(session_allocations));
        session_allocations.fail_on_call = SIZE_MAX;
        vxml_test_allocator_set(&session_test_allocator);

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        program_live = session_allocations.live_count;
        check_true(program_live != 0u);
        for (failure_point = 1u; failure_point <= 64u; ++failure_point) {
            vxml_session session = {0};
            vxml_status status;
            session_allocations.fail_on_call =
                session_allocations.calls + failure_point;
            info("session init allocation point %zu", failure_point);
            status = vxml_session_init_cmeta(&session, &program, &options);
            if (status == VXML_OK) {
                reached_success = true;
                vxml_session_destroy(&session);
                check_equal(session_allocations.live_count, program_live);
                break;
            }
            check_equal(status, VXML_ALLOCATION_FAILED);
            check_null(session.impl);
            check_equal(session_allocations.live_count, program_live);
            check_equal(session_allocations.invalid_operations, (size_t)0u);
        }
        check_true(reached_success);
        check_true(failure_point > 1u);
        session_allocations.fail_on_call = SIZE_MAX;
        vxml_program_destroy(&program);
        check_equal(session_allocations.live_count, (size_t)0u);
        check_equal(session_allocations.invalid_operations, (size_t)0u);
        vxml_test_allocator_reset();
    }

    it("rolls back every exit snapshot allocation before publication") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<assign name='value' expr='5'/>"
            "<exit namelist='value text'/></block></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_cmeta_session_root root = {.value = 1};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        size_t program_live;
        size_t failure_point;
        bool reached_success = false;
        memcpy(root.text.bytes, "old", 3u);
        root.text.size = 3u;
        reset_session_text_probe();
        memset(&session_allocations, 0, sizeof(session_allocations));
        session_allocations.fail_on_call = SIZE_MAX;
        vxml_test_allocator_set(&session_test_allocator);

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        program_live = session_allocations.live_count;
        for (failure_point = 1u; failure_point <= 4u; ++failure_point) {
            vxml_session session = {0};
            vxml_cmeta_value_view committed = {0};
            vxml_cmeta_exit_kind kind = VXML_CMETA_EXIT_NAMELIST;
            vxml_cmeta_name_view name = {
                (const char *)(uintptr_t)1u, 99u};
            vxml_cmeta_value_view entry = {
                .kind = VXML_CMETA_VALUE_STRING,
                .data.string = {(const char *)(uintptr_t)1u, 99u}};
            size_t before_start;
            vxml_status status;

            session_allocations.fail_on_call = SIZE_MAX;
            check_equal(vxml_session_init_cmeta(
                            &session, &program, &options),
                        VXML_OK);
            before_start = session_allocations.live_count;
            session_allocations.fail_on_call =
                session_allocations.calls + failure_point;
            info("exit snapshot allocation point %zu", failure_point);
            status = vxml_session_start(&session);
            if (status == VXML_OK) {
                reached_success = true;
                check_equal(failure_point, (size_t)4u);
                check_equal(vxml_session_cmeta_exit_count(&session),
                            (size_t)2u);
            } else {
                check_equal(status, VXML_ALLOCATION_FAILED);
                check_equal(vxml_session_get_state(&session),
                            VXML_SESSION_FAILED);
                check_equal(vxml_session_error(&session),
                            VXML_ALLOCATION_FAILED);
                check_equal(session_allocations.live_count, before_start);
                check_equal(vxml_session_cmeta_read(
                                &session, "value",
                                sizeof("value") - 1u, &committed),
                            VXML_OK);
                check_equal(committed.kind, VXML_CMETA_VALUE_SINT);
                check_equal(committed.data.sint, INT64_C(1));
                check_equal(vxml_session_cmeta_exit_kind(&session, &kind),
                            VXML_INVALID_STATE);
                check_equal(vxml_session_cmeta_exit_count(&session),
                            (size_t)0u);
                check_equal(vxml_session_cmeta_exit_at(
                                &session, 0u, &name, &entry),
                            VXML_INVALID_STATE);
                check_equal(kind, VXML_CMETA_EXIT_EMPTY);
                check_null(name.data);
                check_equal(name.size, (size_t)0u);
                check_true(value_view_is_clear(entry));
            }
            session_allocations.fail_on_call = SIZE_MAX;
            vxml_session_destroy(&session);
            check_equal(session_allocations.live_count, program_live);
            check_equal(session_text_live_resources, (size_t)0u);
            check_equal(session_allocations.invalid_operations, (size_t)0u);
            check_equal(session_text_invalid_operations, (size_t)0u);
        }
        check_true(reached_success);
        vxml_program_destroy(&program);
        check_equal(session_allocations.live_count, (size_t)0u);
        vxml_test_allocator_reset();
    }

    it("releases read and terminal storage exactly once on close") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<exit namelist='text'/></block></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_cmeta_session_root root = {0};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        size_t program_live;
        size_t before_close;
        size_t after_close;
        size_t after_second_close;
        size_t after_destroy;
        size_t final_live;
        size_t text_after_close;
        size_t invalid_operations;
        bool profile_released;
        memcpy(root.text.bytes, "owned", 5u);
        root.text.size = 5u;
        reset_session_text_probe();
        memset(&session_allocations, 0, sizeof(session_allocations));
        session_allocations.fail_on_call = SIZE_MAX;
        vxml_test_allocator_set(&session_test_allocator);

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        program_live = session_allocations.live_count;
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        before_close = session_allocations.live_count;
        check_equal(vxml_session_close(&session), VXML_OK);
        after_close = session_allocations.live_count;
        text_after_close = session_text_live_resources;
        profile_released =
            ((const vxml_session_impl *)session.impl)->profile_data == NULL;
        check_equal(vxml_session_close(&session), VXML_OK);
        after_second_close = session_allocations.live_count;
        vxml_session_destroy(&session);
        after_destroy = session_allocations.live_count;
        vxml_program_destroy(&program);
        final_live = session_allocations.live_count;
        invalid_operations = session_allocations.invalid_operations;
        vxml_test_allocator_reset();

        check_true(before_close > program_live + 1u);
        check_equal(after_close, program_live + 1u);
        check_equal(text_after_close, (size_t)0u);
        check_true(profile_released);
        check_equal(after_second_close, after_close);
        check_equal(after_destroy, program_live);
        check_equal(final_live, (size_t)0u);
        check_equal(invalid_operations, (size_t)0u);
        check_equal(session_text_invalid_operations, (size_t)0u);
    }

    it("admits the exact transaction budget with aligned bounded frames") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><var name='other' expr='1'/><form>"
            "<block><if cond='true'><if cond='true'>"
            "<assign name='value' expr='2'/></if></if></block>"
            "</form></vxml>";
        vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = 0, .other = 0, .late = 0, .flag = false};
        vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        const vxml_cmeta_program_data *compiled;
        vxml_cmeta_session_data *runtime;
        size_t expected;
        size_t declared_count = 0u;
        size_t scope;
        compile.max_conditional_depth = 2u;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        compiled = program_data(&program);
        runtime = session_data(&session);
        expected = compiled->root->storage_type->size +
            compiled->root->storage_type->align - 1u +
            session_root_shape.field_count;
        for (scope = 0u; scope < compiled->scope_count; ++scope) {
            const cmeta_scope_schema *schema =
                &compiled->scopes[scope].schema;
            declared_count += schema->slot_count;
            if (schema->slot_count != 0u)
                expected += schema->storage_size + schema->storage_align - 1u +
                    schema->slot_count;
        }
        expected += declared_count + compiled->expression_scratch_bytes +
            (compile.max_conditional_depth + 1u) *
                sizeof(vxml_cmeta_exec_frame) +
            3u * sizeof(vxml_cmeta_expr_runtime_scope);
        check_equal(runtime->transaction_bytes_required, expected);
        check_equal(runtime->exec_frame_capacity, (size_t)3u);
        check_equal((uintptr_t)runtime->committed_root.storage %
                        compiled->root->storage_type->align,
                    (uintptr_t)0u);
        check_equal((uintptr_t)runtime->staged_root.storage %
                        compiled->root->storage_type->align,
                    (uintptr_t)0u);
        for (scope = 0u; scope < compiled->scope_count; ++scope) {
            const cmeta_scope_schema *schema =
                &compiled->scopes[scope].schema;
            if (schema->slot_count == 0u) continue;
            check_equal((uintptr_t)runtime->committed_scopes[scope]
                            .view.storage % schema->storage_align,
                        (uintptr_t)0u);
            check_equal((uintptr_t)runtime->staged_scopes[scope]
                            .view.storage % schema->storage_align,
                        (uintptr_t)0u);
        }
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(
            ((const vxml_cmeta_session_root *)
                runtime->committed_root.storage)->value,
            2);
        vxml_session_destroy(&session);

        options.max_transaction_bytes = expected;
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        check_equal(
            session_data(&session)->transaction_bytes_required, expected);
        vxml_session_destroy(&session);

        options.max_transaction_bytes = expected - 1u;
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_LIMIT_EXCEEDED);
        check_null(session.impl);
        vxml_program_destroy(&program);
    }

    it("accounts exact exit and read-string capacity in the session budget") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<exit namelist='value text'/></block></form></vxml>";
        vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_cmeta_session_root root = {.value = 7};
        vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        const vxml_cmeta_program_data *compiled;
        vxml_cmeta_session_data *runtime;
        vxml_cmeta_value_view read_value = {0};
        vxml_cmeta_name_view exit_name = {0};
        vxml_cmeta_value_view exit_value = {0};
        size_t expected;
        size_t measured;
        size_t declared_count = 0u;
        size_t scope;
        size_t entry_capacity;
        size_t name_capacity;
        size_t string_capacity;
        size_t name_size;
        size_t string_size;
        vxml_status exact_status;
        vxml_status read_status;
        vxml_status start_status;
        vxml_status exit_status;
        vxml_status below_status;
        bool read_matches;
        bool exit_matches;
        compile.max_string_bytes = 4u;
        memcpy(root.text.bytes, "four", 4u);
        root.text.size = 4u;
        reset_session_text_probe();

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        compiled = program_data(&program);
        runtime = session_data(&session);
        expected = compiled->root->storage_type->size +
            compiled->root->storage_type->align - 1u +
            session_root_shape.field_count;
        for (scope = 0u; scope < compiled->scope_count; ++scope) {
            const cmeta_scope_schema *schema =
                &compiled->scopes[scope].schema;
            declared_count += schema->slot_count;
            if (schema->slot_count != 0u)
                expected += schema->storage_size + schema->storage_align - 1u +
                    schema->slot_count;
        }
        expected += declared_count + compiled->expression_scratch_bytes +
            (compile.max_conditional_depth + 1u) *
                sizeof(vxml_cmeta_exec_frame) +
            3u * sizeof(vxml_cmeta_expr_runtime_scope) +
            2u * sizeof(vxml_cmeta_exit_entry) +
            (sizeof("value") - 1u) + (sizeof("text") - 1u) +
            compile.max_string_bytes;
        measured = runtime->transaction_bytes_required;
        vxml_session_destroy(&session);

        options.max_transaction_bytes = expected;
        exact_status = vxml_session_init_cmeta(&session, &program, &options);
        read_status = exact_status == VXML_OK
            ? vxml_session_cmeta_read(
                &session, "text", sizeof("text") - 1u, &read_value)
            : exact_status;
        read_matches = read_status == VXML_OK &&
            read_value.kind == VXML_CMETA_VALUE_STRING &&
            read_value.data.string.size == 4u &&
            memcmp(read_value.data.string.data, "four", 4u) == 0;
        start_status = exact_status == VXML_OK
            ? vxml_session_start(&session) : exact_status;
        runtime = exact_status == VXML_OK ? session_data(&session) : NULL;
        entry_capacity = runtime != NULL
            ? runtime->terminal_exit.entry_capacity : 0u;
        name_capacity = runtime != NULL
            ? runtime->terminal_exit.name_capacity : 0u;
        string_capacity = runtime != NULL
            ? runtime->terminal_exit.string_capacity : 0u;
        name_size = runtime != NULL ? runtime->terminal_exit.name_size : 0u;
        string_size = runtime != NULL
            ? runtime->terminal_exit.string_size : 0u;
        exit_status = start_status == VXML_OK
            ? vxml_session_cmeta_exit_at(
                &session, 1u, &exit_name, &exit_value)
            : start_status;
        exit_matches = exit_status == VXML_OK &&
            exit_name.size == sizeof("text") - 1u &&
            memcmp(exit_name.data, "text", exit_name.size) == 0 &&
            exit_value.kind == VXML_CMETA_VALUE_STRING &&
            exit_value.data.string.size == 4u &&
            memcmp(exit_value.data.string.data, "four", 4u) == 0;
        vxml_session_destroy(&session);

        options.max_transaction_bytes = expected - 1u;
        below_status = vxml_session_init_cmeta(&session, &program, &options);
        vxml_session_destroy(&session);
        vxml_program_destroy(&program);

        check_equal(measured, expected);
        check_equal(exact_status, VXML_OK);
        check_true(read_matches);
        check_equal(start_status, VXML_OK);
        check_equal(entry_capacity, (size_t)2u);
        check_equal(name_capacity, (size_t)9u);
        check_equal(string_capacity, (size_t)4u);
        check_equal(name_size, (size_t)9u);
        check_equal(string_size, (size_t)4u);
        check_true(exit_matches);
        check_equal(below_status, VXML_LIMIT_EXCEEDED);
        check_equal(session_text_live_resources, (size_t)0u);
        check_equal(session_text_invalid_operations, (size_t)0u);
    }

    it("rejects strings beyond read and terminal snapshot capacity") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<assign name='value' expr='5'/>"
            "<exit namelist='text'/></block></form></vxml>";
        vxml_cmeta_compile_options_v1 compile = compile_options();
        vxml_cmeta_session_root root = {.value = 1};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_cmeta_value_view read_value = {
            .kind = VXML_CMETA_VALUE_STRING,
            .data.string = {(const char *)(uintptr_t)1u, 99u}};
        vxml_cmeta_exit_kind kind = VXML_CMETA_EXIT_NAMELIST;
        vxml_status read_status;
        vxml_status start_status;
        vxml_status kind_status;
        vxml_session_state state;
        vxml_status stable_error;
        int committed_value;
        bool outputs_clear;
        compile.max_string_bytes = 4u;
        memcpy(root.text.bytes, "large", 5u);
        root.text.size = 5u;
        reset_session_text_probe();

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        read_status = vxml_session_cmeta_read(
            &session, "text", sizeof("text") - 1u, &read_value);
        outputs_clear = value_view_is_clear(read_value);
        start_status = vxml_session_start(&session);
        state = vxml_session_get_state(&session);
        stable_error = vxml_session_error(&session);
        committed_value = ((const vxml_cmeta_session_root *)
            session_data(&session)->committed_root.storage)->value;
        kind_status = vxml_session_cmeta_exit_kind(&session, &kind);
        outputs_clear = outputs_clear && kind == VXML_CMETA_EXIT_EMPTY;

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);

        check_equal(read_status, VXML_LIMIT_EXCEEDED);
        check_true(outputs_clear);
        check_equal(start_status, VXML_LIMIT_EXCEEDED);
        check_equal(state, VXML_SESSION_FAILED);
        check_equal(stable_error, VXML_LIMIT_EXCEEDED);
        check_equal(committed_value, 1);
        check_equal(kind_status, VXML_INVALID_STATE);
        check_equal(session_text_live_resources, (size_t)0u);
        check_equal(session_text_invalid_operations, (size_t)0u);
    }

    it("rejects overflowing terminal string capacity before publication") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<exit namelist='text'/></block></form></vxml>";
        vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {0};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {(void *)(uintptr_t)1u};
        vxml_status status;
        bool published;
        compile.max_string_bytes = SIZE_MAX;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        status = vxml_session_init_cmeta(&session, &program, &options);
        published = session.impl != NULL;
        vxml_session_destroy(&session);
        vxml_program_destroy(&program);

        check_equal(status, VXML_LIMIT_EXCEEDED);
        check_false(published);
    }

    it("rejects every invalid session option and undefined-name form") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block expr='true'/></form></vxml>";
        static const char value_name[] = "value";
        static const char missing_name[] = "missing";
        static const char dotted_name[] = "value.member";
        static const char encoded_name[] = "v&#97;lue";
        const vxml_cmeta_name_view present[] = {
            {value_name, sizeof(value_name) - 1u}};
        const vxml_cmeta_name_view missing[] = {
            {missing_name, sizeof(missing_name) - 1u}};
        const vxml_cmeta_name_view dotted[] = {
            {dotted_name, sizeof(dotted_name) - 1u}};
        const vxml_cmeta_name_view encoded[] = {
            {encoded_name, sizeof(encoded_name) - 1u}};
        const vxml_cmeta_name_view duplicate[] = {
            {value_name, sizeof(value_name) - 1u},
            {value_name, sizeof(value_name) - 1u}};
        const vxml_cmeta_name_view null_data[] = {{NULL, 1u}};
        const vxml_cmeta_name_view empty[] = {{value_name, 0u}};
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = 1, .other = 2, .late = 3, .flag = false};
        vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        options.abi_version = 0u;
        check_session_init_rejected(&program, &options, VXML_INVALID_CONTRACT);
        options = session_options(&root);
        options.struct_size = sizeof(options) - 1u;
        check_session_init_rejected(&program, &options, VXML_INVALID_CONTRACT);
        options = session_options(&root);
        options.max_transaction_bytes = 0u;
        check_session_init_rejected(&program, &options, VXML_INVALID_CONTRACT);
        options = session_options(&root);
        options.max_execution_steps = 0u;
        check_session_init_rejected(&program, &options, VXML_INVALID_CONTRACT);
        options = session_options(NULL);
        check_session_init_rejected(&program, &options, VXML_INVALID_CONTRACT);
        options = session_options(&root);
        options.initially_undefined = present;
        check_session_init_rejected(&program, &options, VXML_INVALID_CONTRACT);
        options = session_options(&root);
        options.initially_undefined_count = 1u;
        check_session_init_rejected(&program, &options, VXML_INVALID_CONTRACT);

        options = session_options(&root);
        options.initially_undefined = null_data;
        options.initially_undefined_count = 1u;
        check_session_init_rejected(&program, &options, VXML_INVALID_CONTRACT);
        options.initially_undefined = empty;
        check_session_init_rejected(&program, &options, VXML_INVALID_CONTRACT);
        options.initially_undefined = missing;
        check_session_init_rejected(&program, &options, VXML_INVALID_CONTRACT);
        options.initially_undefined = dotted;
        check_session_init_rejected(&program, &options, VXML_INVALID_CONTRACT);
        options.initially_undefined = encoded;
        check_session_init_rejected(&program, &options, VXML_INVALID_CONTRACT);
        options.initially_undefined = duplicate;
        options.initially_undefined_count = 2u;
        check_session_init_rejected(&program, &options, VXML_INVALID_CONTRACT);
        options.initially_undefined = present;
        options.initially_undefined_count =
            session_root_shape.field_count + 1u;
        check_session_init_rejected(&program, &options, VXML_INVALID_CONTRACT);

        vxml_program_destroy(&program);
    }

    it("rejects a misaligned root field before allocation or copy") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block expr='true'/></form></vxml>";
        mutable_session_root_contract contract;
        vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {0};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        mutable_session_root_contract_init(&contract);
        compile.root = &contract.root_data;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        contract.fields[0].offset = 1u;
        contract.layout_fields[0].offset = 1u;
        reset_session_text_probe();
        check_root_contract_rejected_before_allocation(&program, &options);
        vxml_program_destroy(&program);
    }

    it("rejects aggregate alignment below a root field requirement") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block expr='true'/></form></vxml>";
        mutable_session_root_contract contract;
        vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {0};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        mutable_session_root_contract_init(&contract);
        compile.root = &contract.root_data;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        contract.root_type.align = 1u;
        contract.layout.align = 1u;
        reset_session_text_probe();
        check_root_contract_rejected_before_allocation(&program, &options);
        vxml_program_destroy(&program);
    }

    it("rejects partial and full managed root-field overlap before copy") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block expr='true'/></form></vxml>";
        mutable_session_root_contract contract;
        vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {0};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        mutable_session_root_contract_init(&contract);
        compile.root = &contract.root_data;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        contract.fields[0].value = &contract.text_data;
        contract.layout_fields[0].size = contract.text_type.size;
        contract.layout_fields[0].align = contract.text_type.align;
        contract.layout_fields[0].type = &contract.text_type;
        reset_session_text_probe();
        check_root_contract_rejected_before_allocation(&program, &options);

        contract.fields[0].offset = offsetof(vxml_cmeta_session_root, text);
        contract.layout_fields[0].offset =
            offsetof(vxml_cmeta_session_root, text);
        reset_session_text_probe();
        check_root_contract_rejected_before_allocation(&program, &options);
        vxml_program_destroy(&program);
    }

    it("revalidates each borrowed root field after compilation") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block expr='true'/></form></vxml>";
        cmeta_type_traits text_traits = session_text_traits;
        cmeta_type_desc text_type = session_text_type;
        cmeta_data_buffer_ops text_ops = session_text_ops;
        cmeta_data_desc text_data = session_text_data;
        cmeta_field_desc layout_fields[7];
        cmeta_struct_desc layout = session_root_layout;
        cmeta_data_field_desc fields[7];
        cmeta_data_struct_shape shape = session_root_shape;
        cmeta_data_desc root_data = session_root_data;
        vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = 1, .other = 2, .late = 3, .flag = false};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session valid_session = {0};
        memcpy(layout_fields, session_root_layout_fields,
               sizeof(layout_fields));
        memcpy(fields, session_root_fields, sizeof(fields));
        text_type.traits = &text_traits;
        text_ops.storage_type = &text_type;
        text_data.storage_type = &text_type;
        text_data.buffer_ops = &text_ops;
        layout_fields[4].type = &text_type;
        layout.fields = layout_fields;
        fields[4].value = &text_data;
        shape.layout = &layout;
        shape.fields = fields;
        root_data.shape = &shape;
        compile.root = &root_data;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        ++fields[0].offset;
        check_session_init_rejected(
            &program, &options, VXML_INVALID_CONTRACT);
        --fields[0].offset;
        text_traits.destroy = NULL;
        check_session_init_rejected(
            &program, &options, VXML_INVALID_CONTRACT);
        text_traits.destroy = session_text_destroy;
        check_equal(vxml_session_init_cmeta(
                        &valid_session, &program, &options),
                    VXML_OK);
        vxml_session_destroy(&valid_session);
        vxml_program_destroy(&program);
    }

    it("never mutates compiled scope schemas while creating storage") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><var name='value' expr='1'/><form>"
            "<var name='other' expr='2'/><block><var name='late'/>"
            "</block></form></vxml>";
        const vxml_cmeta_compile_options_v1 compile = compile_options();
        const vxml_cmeta_session_root root = {
            .value = 1, .other = 2, .late = 3, .flag = false};
        const vxml_cmeta_session_options_v1 options = session_options(&root);
        vxml_program program = {0};
        vxml_session session = {0};
        const vxml_cmeta_program_data *compiled;
        bool frozen_before[3];
        size_t scope;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &compile,
                        &program, NULL),
                    VXML_OK);
        compiled = program_data(&program);
        check_equal(compiled->scope_count, (size_t)3u);
        for (scope = 0u; scope < compiled->scope_count; ++scope)
            frozen_before[scope] = compiled->scopes[scope].schema.frozen;
        check_equal(vxml_session_init_cmeta(&session, &program, &options),
                    VXML_OK);
        for (scope = 0u; scope < compiled->scope_count; ++scope)
            check_equal(compiled->scopes[scope].schema.frozen,
                        frozen_before[scope]);
        vxml_session_destroy(&session);
        for (scope = 0u; scope < compiled->scope_count; ++scope)
            check_equal(compiled->scopes[scope].schema.frozen,
                        frozen_before[scope]);
        vxml_program_destroy(&program);
    }
}
