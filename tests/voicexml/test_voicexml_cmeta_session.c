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
} vxml_cmeta_session_root;

static const cmeta_type_identity session_root_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.voicexml.cmeta.session.root");
static const cmeta_type_traits session_root_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY
};
static size_t session_text_copy_calls;
static size_t session_text_assign_calls;
static size_t session_text_live_resources;
static size_t session_text_invalid_operations;
static size_t session_text_fail_copy_call = SIZE_MAX;
static size_t session_text_fail_assign_call = SIZE_MAX;

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
     _Alignof(vxml_cmeta_session_text), &session_text_type, NULL}
};
static const cmeta_struct_desc session_root_layout = {
    .name = "vxml_cmeta_session_root",
    .size = sizeof(vxml_cmeta_session_root),
    .align = _Alignof(vxml_cmeta_session_root),
    .fields = session_root_layout_fields,
    .field_count = 5u
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
     offsetof(vxml_cmeta_session_root, text), &session_text_data}
};
static const cmeta_data_struct_shape session_root_shape = {
    .layout = &session_root_layout,
    .fields = session_root_fields,
    .field_count = 5u
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

static void reset_session_text_probe(void) {
    session_text_copy_calls = 0u;
    session_text_assign_calls = 0u;
    session_text_invalid_operations = 0u;
    session_text_fail_copy_call = SIZE_MAX;
    session_text_fail_assign_call = SIZE_MAX;
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

spec("VoiceXML CMeta session execution") {
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
            "<assign name='value' expr='late + 1'/></block></form></vxml>";
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

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
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

    it("revalidates each borrowed root field after compilation") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block expr='true'/></form></vxml>";
        cmeta_type_traits text_traits = session_text_traits;
        cmeta_type_desc text_type = session_text_type;
        cmeta_data_buffer_ops text_ops = session_text_ops;
        cmeta_data_desc text_data = session_text_data;
        cmeta_field_desc layout_fields[5];
        cmeta_struct_desc layout = session_root_layout;
        cmeta_data_field_desc fields[5];
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
