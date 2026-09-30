#include <voicexml/cmeta.h>

#include "voicexml_cmeta_internal.h"
#include "voicexml_test_allocator.h"
#include "tinytest.h"

#include <cmeta/cmeta.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct vxml_cmeta_program_nested {
    int number;
    bool ready;
} vxml_cmeta_program_nested;

typedef struct vxml_cmeta_program_root {
    int value;
    bool flag;
    vxml_cmeta_program_nested nested;
} vxml_cmeta_program_root;

static const cmeta_type_identity program_root_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.voicexml.cmeta.program.root");
static const cmeta_type_identity program_nested_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.voicexml.cmeta.program.nested");
static const cmeta_type_traits program_root_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY
};
static const cmeta_type_desc program_nested_type = {
    .name = "vxml_cmeta_program_nested",
    .size = sizeof(vxml_cmeta_program_nested),
    .align = _Alignof(vxml_cmeta_program_nested),
    .kind = CMETA_T_OBJECT,
    .traits = &program_root_traits,
    .identity = &program_nested_identity
};
static const cmeta_type_desc program_root_type = {
    .name = "vxml_cmeta_program_root",
    .size = sizeof(vxml_cmeta_program_root),
    .align = _Alignof(vxml_cmeta_program_root),
    .kind = CMETA_T_OBJECT,
    .traits = &program_root_traits,
    .identity = &program_root_identity
};
static const cmeta_field_desc program_nested_layout_fields[] = {
    {"number", "int", offsetof(vxml_cmeta_program_nested, number),
     sizeof(int), _Alignof(int), &cmeta_type_int, NULL},
    {"ready", "bool", offsetof(vxml_cmeta_program_nested, ready),
     sizeof(bool), _Alignof(bool), &cmeta_type_bool, NULL}
};
static const cmeta_struct_desc program_nested_layout = {
    .name = "vxml_cmeta_program_nested",
    .size = sizeof(vxml_cmeta_program_nested),
    .align = _Alignof(vxml_cmeta_program_nested),
    .fields = program_nested_layout_fields,
    .field_count = 2u
};
static const cmeta_data_field_desc program_nested_fields[] = {
    {"test.voicexml.cmeta.program.nested.number", "number",
     offsetof(vxml_cmeta_program_nested, number), &cmeta_data_int},
    {"test.voicexml.cmeta.program.nested.ready", "ready",
     offsetof(vxml_cmeta_program_nested, ready), &cmeta_data_bool}
};
static const cmeta_data_struct_shape program_nested_shape = {
    .layout = &program_nested_layout,
    .fields = program_nested_fields,
    .field_count = 2u
};
static const cmeta_data_desc program_nested_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.voicexml.cmeta.program.nested.data",
    .display_name = "VoiceXML CMeta program nested",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &program_nested_type,
    .shape = &program_nested_shape
};
static const cmeta_field_desc program_root_layout_fields[] = {
    {"value", "int", offsetof(vxml_cmeta_program_root, value),
     sizeof(int), _Alignof(int), &cmeta_type_int, NULL},
    {"flag", "bool", offsetof(vxml_cmeta_program_root, flag),
     sizeof(bool), _Alignof(bool), &cmeta_type_bool, NULL},
    {"nested", "vxml_cmeta_program_nested",
     offsetof(vxml_cmeta_program_root, nested),
     sizeof(vxml_cmeta_program_nested), _Alignof(vxml_cmeta_program_nested),
     &program_nested_type, NULL}
};
static const cmeta_struct_desc program_root_layout = {
    .name = "vxml_cmeta_program_root",
    .size = sizeof(vxml_cmeta_program_root),
    .align = _Alignof(vxml_cmeta_program_root),
    .fields = program_root_layout_fields,
    .field_count = 3u
};
static const cmeta_data_field_desc program_root_fields[] = {
    {"test.voicexml.cmeta.program.root.value", "value",
     offsetof(vxml_cmeta_program_root, value), &cmeta_data_int},
    {"test.voicexml.cmeta.program.root.flag", "flag",
     offsetof(vxml_cmeta_program_root, flag), &cmeta_data_bool},
    {"test.voicexml.cmeta.program.root.nested", "nested",
     offsetof(vxml_cmeta_program_root, nested), &program_nested_data}
};
static const cmeta_data_struct_shape program_root_shape = {
    .layout = &program_root_layout,
    .fields = program_root_fields,
    .field_count = sizeof(program_root_fields) /
                   sizeof(program_root_fields[0])
};
static const cmeta_data_desc program_root_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.voicexml.cmeta.program.root.data",
    .display_name = "VoiceXML CMeta program root",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &program_root_type,
    .shape = &program_root_shape
};

static vxml_cmeta_compile_options_v1 compile_options(void) {
    return (vxml_cmeta_compile_options_v1){
        .abi_version = VXML_CMETA_COMPILE_OPTIONS_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_compile_options_v1),
        .root = &program_root_data,
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

static vxml_cmeta_compile_options_v1 data_compile_options(void) {
    vxml_cmeta_compile_options_v1 options = compile_options();
    options.max_external_data_resources = 2u;
    options.max_data_uri_bytes = 64u;
    options.max_data_bind_depth = 16u;
    options.max_data_bind_items = 128u;
    return options;
}

static vxml_cmeta_compile_options_v1 field_compile_options(void) {
    vxml_cmeta_compile_options_v1 options = compile_options();
    options.max_fields = 8u;
    options.max_grammar_bytes = 256u;
    return options;
}

static vxml_cmeta_compile_options_v1 prompt_compile_options(void) {
    vxml_cmeta_compile_options_v1 options = field_compile_options();
    options.max_prompts = 8u;
    options.max_prompt_bytes = 256u;
    options.max_prompt_segments = 8u;
    return options;
}

enum { PROGRAM_ALLOCATION_CAPACITY = 256 };

typedef struct program_allocation_tracker {
    size_t calls;
    size_t fail_on_call;
    size_t live_count;
    size_t invalid_operations;
    void *live[PROGRAM_ALLOCATION_CAPACITY];
} program_allocation_tracker;

static program_allocation_tracker program_allocations;

static size_t program_allocation_find(void *pointer) {
    size_t index;
    for (index = 0u; index < program_allocations.live_count; ++index)
        if (program_allocations.live[index] == pointer) return index;
    return PROGRAM_ALLOCATION_CAPACITY;
}

static bool program_allocation_should_fail(void) {
    ++program_allocations.calls;
    return program_allocations.calls == program_allocations.fail_on_call;
}

static void program_allocation_add(void *pointer) {
    if (pointer == NULL) return;
    if (program_allocations.live_count >= PROGRAM_ALLOCATION_CAPACITY) {
        ++program_allocations.invalid_operations;
        return;
    }
    program_allocations.live[program_allocations.live_count++] = pointer;
}

static void *program_test_malloc(size_t size) {
    void *pointer;
    if (program_allocation_should_fail()) return NULL;
    pointer = malloc(size);
    program_allocation_add(pointer);
    return pointer;
}

static void *program_test_calloc(size_t count, size_t size) {
    void *pointer;
    if (program_allocation_should_fail()) return NULL;
    pointer = calloc(count, size);
    program_allocation_add(pointer);
    return pointer;
}

static void *program_test_realloc(void *pointer, size_t size) {
    const size_t old_index = pointer != NULL
        ? program_allocation_find(pointer) : PROGRAM_ALLOCATION_CAPACITY;
    void *replacement;
    if (program_allocation_should_fail()) return NULL;
    replacement = realloc(pointer, size);
    if (replacement == NULL) return NULL;
    if (pointer == NULL) {
        program_allocation_add(replacement);
    } else if (old_index < PROGRAM_ALLOCATION_CAPACITY) {
        program_allocations.live[old_index] = replacement;
    } else {
        ++program_allocations.invalid_operations;
        program_allocation_add(replacement);
    }
    return replacement;
}

static void program_test_free(void *pointer) {
    const size_t index = program_allocation_find(pointer);
    if (pointer == NULL) return;
    if (index >= PROGRAM_ALLOCATION_CAPACITY) {
        ++program_allocations.invalid_operations;
        free(pointer);
        return;
    }
    program_allocations.live[index] =
        program_allocations.live[program_allocations.live_count - 1u];
    --program_allocations.live_count;
    free(pointer);
}

static const vxml_test_allocator program_test_allocator = {
    program_test_malloc,
    program_test_calloc,
    program_test_realloc,
    program_test_free
};

static void check_program_rejected(
    const char *source, vxml_status expected) {
    const vxml_cmeta_compile_options_v1 options = compile_options();
    vxml_program program = {0};
    vxml_diagnostic diagnostic = {0};
    const vxml_status actual = vxml_compile_cmeta(
        source, strlen(source), NULL, &options, &program, &diagnostic);
    const bool produced_program = program.impl != NULL;
    const vxml_status diagnostic_status = diagnostic.status;
    vxml_program_destroy(&program);

    check_equal(actual, expected);
    check_false(produced_program);
    check_equal(diagnostic_status, expected);
}

spec("VoiceXML CMeta program compiler") {
    it("rejects reprompt outside scoped Event handler content") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block><reprompt/></block></form></vxml>";
        vxml_cmeta_compile_options_v1 options =
            compile_options();
        vxml_program program = {0};
        vxml_diagnostic diagnostic = {0};
        options.max_fields = 4u;
        options.max_grammar_bytes = 128u;
        options.max_event_handlers = 4u;
        options.max_event_name_bytes = 64u;

        check_equal(vxml_compile_cmeta(
                        source, sizeof(source) - 1u, NULL, &options,
                        &program, &diagnostic),
                    VXML_INVALID_STRUCTURE);
        check_null(program.impl);
        check_equal(diagnostic.status, VXML_INVALID_STRUCTURE);
    }

    it("compiles tapered literal prompts into immutable field-owned rows") {
        char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><field name='value'>"
            "<prompt>First prompt</prompt>"
            "<prompt count='2' cond='flag'>Second prompt</prompt>"
            "<grammar type='application/srgs+xml' src='a'/>"
            "</field></form></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            prompt_compile_options();
        vxml_program program = {0};
        const vxml_cmeta_program_data *profile;
        const vxml_cmeta_field_row *field;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &options,
                        &program, NULL),
                    VXML_OK);
        memset(source, 'X', sizeof(source) - 1u);

        profile = (const vxml_cmeta_program_data *)
            ((const vxml_program_impl *)program.impl)->profile_data;
        check_not_null(profile);
        check_equal(profile->prompt_count, (size_t)2u);
        check_not_null(profile->prompts);
        field = &profile->fields[0];
        check_equal(field->first_prompt, (size_t)0u);
        check_equal(field->prompt_count, (size_t)2u);
        check_equal(profile->prompts[0].field, (size_t)0u);
        check_equal(profile->prompts[0].count, (unsigned)1u);
        check_equal(profile->prompts[0].text_size,
                    sizeof("First prompt") - 1u);
        check_equal(memcmp(
                        profile->prompts[0].text,
                        "First prompt",
                        sizeof("First prompt") - 1u), 0);
        check_equal(profile->prompts[0].condition,
                    VXML_CMETA_NO_INDEX);
        check_equal(profile->prompts[0].media_kind,
                    VXML_CMETA_PROMPT_MEDIA_TEXT);
        check_true(profile->prompts[0].media_payload ==
                   profile->prompts[0].text);
        check_equal(profile->prompts[0].media_payload_size,
                    profile->prompts[0].text_size);
        check_equal(profile->prompts[1].count, (unsigned)2u);
        check_equal(profile->prompts[1].text_size,
                    sizeof("Second prompt") - 1u);
        check_equal(memcmp(
                        profile->prompts[1].text,
                        "Second prompt",
                        sizeof("Second prompt") - 1u), 0);
        check_true(profile->prompts[1].condition !=
                   VXML_CMETA_NO_INDEX);
        check_equal(profile->prompts[1].media_kind,
                    VXML_CMETA_PROMPT_MEDIA_TEXT);

        vxml_program_destroy(&program);
    }

    it("compiles mixed prompt media into immutable ordered segment rows") {
        char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><field name='value'>"
            "<prompt>Hello <audio src='retry.wav'/> again</prompt>"
            "<grammar type='application/srgs+xml' src='a'/>"
            "</field></form></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            prompt_compile_options();
        vxml_program program = {0};
        const vxml_cmeta_program_data *profile;
        const vxml_cmeta_prompt_row *prompt;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &options,
                        &program, NULL),
                    VXML_OK);
        memset(source, 'X', sizeof(source) - 1u);

        profile = (const vxml_cmeta_program_data *)
            ((const vxml_program_impl *)program.impl)->profile_data;
        check_not_null(profile);
        check_equal(profile->prompt_count, (size_t)1u);
        check_equal(profile->prompt_segment_count, (size_t)3u);
        check_not_null(profile->prompt_segments);
        prompt = &profile->prompts[0];
        check_equal(prompt->first_segment, (size_t)0u);
        check_equal(prompt->segment_count, (size_t)3u);
        check_equal(prompt->required_capabilities,
                    VXML_CMETA_PROMPT_MEDIA_CAP_TEXT |
                    VXML_CMETA_PROMPT_MEDIA_CAP_AUDIO |
                    VXML_CMETA_PROMPT_MEDIA_CAP_BATCH);
        check_equal(profile->prompt_segments[0].kind,
                    VXML_CMETA_PROMPT_MEDIA_TEXT);
        check_equal(profile->prompt_segments[1].kind,
                    VXML_CMETA_PROMPT_MEDIA_AUDIO);
        check_equal(profile->prompt_segments[2].kind,
                    VXML_CMETA_PROMPT_MEDIA_TEXT);
        check_equal(memcmp(
                        profile->prompt_segments[0].payload.data,
                        "Hello ", sizeof("Hello ") - 1u), 0);
        check_equal(profile->prompt_segments[0].payload.size,
                    sizeof("Hello ") - 1u);
        check_equal(memcmp(
                        profile->prompt_segments[1].payload.data,
                        "retry.wav", sizeof("retry.wav") - 1u), 0);
        check_equal(profile->prompt_segments[1].payload.size,
                    sizeof("retry.wav") - 1u);
        check_equal(memcmp(
                        profile->prompt_segments[2].payload.data,
                        " again", sizeof(" again") - 1u), 0);
        check_equal(profile->prompt_segments[2].payload.size,
                    sizeof(" again") - 1u);

        vxml_program_destroy(&program);
    }

    it("compiles a literal audio prompt into one immutable AUDIO row") {
        char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><field name='value'>"
            "<prompt count='2' cond='flag'><audio src='retry.wav'/></prompt>"
            "<grammar type='application/srgs+xml' src='a'/>"
            "</field></form></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            prompt_compile_options();
        vxml_program program = {0};
        const vxml_cmeta_program_data *profile;
        const vxml_cmeta_prompt_row *prompt;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &options,
                        &program, NULL),
                    VXML_OK);
        memset(source, 'X', sizeof(source) - 1u);

        profile = (const vxml_cmeta_program_data *)
            ((const vxml_program_impl *)program.impl)->profile_data;
        check_not_null(profile);
        check_equal(profile->prompt_count, (size_t)1u);
        prompt = &profile->prompts[0];
        check_equal(prompt->count, (unsigned)2u);
        check_true(prompt->condition != VXML_CMETA_NO_INDEX);
        check_equal(prompt->media_kind, VXML_CMETA_PROMPT_MEDIA_AUDIO);
        check_null(prompt->text);
        check_equal(prompt->text_size, (size_t)0u);
        check_equal(prompt->media_payload_size,
                    sizeof("retry.wav") - 1u);
        check_equal(memcmp(
                        prompt->media_payload,
                        "retry.wav",
                        sizeof("retry.wav") - 1u), 0);

        vxml_program_destroy(&program);
    }

    it("rejects invalid prompt count text limits and child markup") {
        static const char *const bodies[] = {
            "<form><field name='value'><prompt count='0'>x</prompt>"
            "<grammar type='application/srgs+xml' src='a'/></field></form>",
            "<form><field name='value'><prompt>toolong</prompt>"
            "<grammar type='application/srgs+xml' src='a'/></field></form>",
            "<form><field name='value'><prompt><audio expr='x'/></prompt>"
            "<grammar type='application/srgs+xml' src='a'/></field></form>",
            "<form><field name='value'><prompt><audio src='a'>fallback</audio></prompt>"
            "<grammar type='application/srgs+xml' src='a'/></field></form>"
        };
        static const vxml_status expected[] = {
            VXML_INVALID_STRUCTURE,
            VXML_LIMIT_EXCEEDED,
            VXML_UNSUPPORTED_FEATURE,
            VXML_INVALID_STRUCTURE
        };
        static const char prefix[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'>";
        size_t index;

        for (index = 0u;
             index < sizeof(bodies) / sizeof(bodies[0]);
             ++index) {
            char source[768];
            vxml_cmeta_compile_options_v1 options =
                prompt_compile_options();
            vxml_program program = {0};
            const int written = snprintf(
                source, sizeof(source), "%s%s</vxml>",
                prefix, bodies[index]);
            if (index == 1u) options.max_prompt_bytes = 3u;
            check_true(written > 0 && (size_t)written < sizeof(source));
            check_equal(vxml_compile_cmeta(
                            source, (size_t)written, NULL, &options,
                            &program, NULL),
                        expected[index]);
            check_null(program.impl);
        }
    }

    it("compiles one directed field and literal SRGS grammar into immutable rows") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form id='order'>"
            "<field name='value' cond='flag'>"
            "<grammar type='application/srgs+xml' src='value.grxml'/>"
            "</field></form></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            field_compile_options();
        vxml_program program = {0};
        const vxml_program_impl *impl;
        const vxml_cmeta_program_data *profile;
        const vxml_cmeta_field_row *field;

        check_equal(vxml_compile_cmeta(
                        source, sizeof(source) - 1u, NULL, &options,
                        &program, NULL),
                    VXML_OK);
        impl = (const vxml_program_impl *)program.impl;
        profile = (const vxml_cmeta_program_data *)impl->profile_data;
        check_not_null(profile);
        check_equal(profile->field_count, (size_t)1u);
        check_not_null(profile->fields);
        check_equal(profile->forms[0].first_field, (size_t)0u);
        check_equal(profile->forms[0].field_count, (size_t)1u);
        check_equal(profile->forms[0].block_count, (size_t)0u);
        field = &profile->fields[0];
        check_equal(field->form, (size_t)0u);
        check_equal(field->root_field, (size_t)0u);
        check_equal(field->field_offset,
                    offsetof(vxml_cmeta_program_root, value));
        check_true(field->field_data == &cmeta_data_int);
        check_equal(field->name, "value");
        check_equal(field->grammar_type, "application/srgs+xml");
        check_equal(field->grammar_src, "value.grxml");
        check_equal(field->required_capabilities,
                    VXML_CMETA_COLLECT_CAP_SRGS_XML);
        check_true(field->condition != VXML_CMETA_NO_INDEX);
        check_equal(profile->expression_count, (size_t)1u);

        vxml_program_destroy(&program);
    }

    it("compiles field and form filled handlers into immutable action ranges") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form>"
            "<field name='value'><grammar type='application/srgs+xml' src='a'/>"
            "<filled><assign name='nested.number' expr='value'/></filled>"
            "</field>"
            "<field name='flag'><grammar type='application/srgs+xml' src='b'/></field>"
            "<filled mode='all' namelist='value flag'>"
            "<exit namelist='value flag'/></filled>"
            "</form></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            field_compile_options();
        vxml_program program = {0};
        const vxml_cmeta_program_data *profile;
        const vxml_cmeta_filled_row *field_filled;
        const vxml_cmeta_filled_row *form_filled;

        check_equal(vxml_compile_cmeta(
                        source, sizeof(source) - 1u, NULL, &options,
                        &program, NULL),
                    VXML_OK);
        profile = (const vxml_cmeta_program_data *)
            ((const vxml_program_impl *)program.impl)->profile_data;
        check_not_null(profile);
        check_equal(profile->field_count, (size_t)2u);
        check_equal(profile->filled_count, (size_t)2u);
        check_not_null(profile->filled);
        check_not_null(profile->filled_root_fields);
        check_equal(profile->fields[0].filled, (size_t)0u);
        check_equal(profile->fields[1].filled, VXML_CMETA_NO_INDEX);
        check_equal(profile->forms[0].first_filled, (size_t)1u);
        check_equal(profile->forms[0].filled_count, (size_t)1u);

        field_filled = &profile->filled[0];
        check_equal(field_filled->mode, VXML_CMETA_FILLED_FIELD);
        check_equal(field_filled->field, (size_t)0u);
        check_true(field_filled->action_end > field_filled->first_action);

        form_filled = &profile->filled[1];
        check_equal(form_filled->mode, VXML_CMETA_FILLED_ALL);
        check_equal(form_filled->field, VXML_CMETA_NO_INDEX);
        check_equal(form_filled->target_count, (size_t)2u);
        check_equal(
            profile->filled_root_fields[form_filled->first_target],
            (size_t)0u);
        check_equal(
            profile->filled_root_fields[form_filled->first_target + 1u],
            (size_t)1u);
        check_true(form_filled->action_end > form_filled->first_action);

        vxml_program_destroy(&program);
    }

    it("rejects invalid filled modes targets and handler-local vars") {
        static const char *const bodies[] = {
            "<form><field name='value'><grammar type='application/srgs+xml' src='a'/></field>"
            "<filled mode='maybe'/></form>",
            "<form><field name='value'><grammar type='application/srgs+xml' src='a'/></field>"
            "<filled namelist='missing'/></form>",
            "<form><field name='value'><grammar type='application/srgs+xml' src='a'/></field>"
            "<filled namelist='value value'/></form>",
            "<form><field name='value'><grammar type='application/srgs+xml' src='a'/>"
            "<filled><var name='x' expr='1'/></filled></field></form>"
        };
        static const char prefix[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'>";
        const vxml_cmeta_compile_options_v1 options =
            field_compile_options();
        size_t index;

        for (index = 0u; index < sizeof(bodies) / sizeof(bodies[0]); ++index) {
            char source[1024];
            vxml_program program = {0};
            const int written = snprintf(
                source, sizeof(source), "%s%s</vxml>",
                prefix, bodies[index]);
            check_true(written > 0 && (size_t)written < sizeof(source));
            check_true(vxml_compile_cmeta(
                           source, (size_t)written, NULL, &options,
                           &program, NULL) != VXML_OK);
            check_null(program.impl);
        }
    }

    it("requires explicit field and grammar limits") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form>"
            "<field name='value'><grammar type='application/srgs+xml' "
            "src='value.grxml'/></field></form></vxml>";
        const vxml_cmeta_compile_options_v1 options = compile_options();
        vxml_program program = {0};

        check_equal(vxml_compile_cmeta(
                        source, sizeof(source) - 1u, NULL, &options,
                        &program, NULL),
                    VXML_INVALID_CONTRACT);
        check_null(program.impl);
    }

    it("rejects malformed or unsupported directed field grammars") {
        static const struct {
            const char *source;
            vxml_status expected;
        } cases[] = {
            {
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
                "datamodel='cmeta'><form><field name='value'/>"
                "</form></vxml>",
                VXML_INVALID_STRUCTURE
            },
            {
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
                "datamodel='cmeta'><form><field name='value'>"
                "<grammar type='text/jsgf' src='value.jsgf'/>"
                "</field></form></vxml>",
                VXML_UNSUPPORTED_FEATURE
            },
            {
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
                "datamodel='cmeta'><form><field name='value'>"
                "<grammar type='application/srgs+xml' src='a'/>"
                "<grammar type='application/srgs+xml' src='b'/>"
                "</field></form></vxml>",
                VXML_INVALID_STRUCTURE
            }
        };
        const vxml_cmeta_compile_options_v1 options =
            field_compile_options();
        size_t index;
        for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
            vxml_program program = {0};
            check_equal(vxml_compile_cmeta(
                            cases[index].source, strlen(cases[index].source),
                            NULL, &options, &program, NULL),
                        cases[index].expected);
            check_null(program.impl);
        }
    }

    it("rejects duplicate field names lexical collisions and mixed block forms") {
        static const char *const sources[] = {
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form>"
            "<field name='value'><grammar type='application/srgs+xml' src='a'/></field>"
            "<field name='value'><grammar type='application/srgs+xml' src='b'/></field>"
            "</form></vxml>",
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><var name='value'/>"
            "<field name='value'><grammar type='application/srgs+xml' src='a'/></field>"
            "</form></vxml>",
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form>"
            "<field name='value'><grammar type='application/srgs+xml' src='a'/></field>"
            "<block/>"
            "</form></vxml>"
        };
        const vxml_cmeta_compile_options_v1 options =
            field_compile_options();
        size_t index;
        for (index = 0u; index < sizeof(sources) / sizeof(sources[0]); ++index) {
            vxml_program program = {0};
            check_equal(vxml_compile_cmeta(
                            sources[index], strlen(sources[index]),
                            NULL, &options, &program, NULL),
                        VXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }
    }

    it("compiles document data into one pre-admitted NativePlan") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'>"
            "<data name='value' src='config.json'/>"
            "<form><block><exit expr='value'/></block></form></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            data_compile_options();
        vxml_program program = {0};
        const vxml_cmeta_program_data *profile;
        const vxml_cmeta_external_data_row *row;

        check_equal(vxml_compile_cmeta(
                        source, sizeof(source) - 1u, NULL, &options,
                        &program, NULL),
                    VXML_OK);
        profile = (const vxml_cmeta_program_data *)
            ((const vxml_program_impl *)program.impl)->profile_data;
        check_not_null(profile);
        check_equal(profile->external_data_count, (size_t)1u);
        check_not_null(profile->external_data);
        row = &profile->external_data[0];
        check_equal(row->name, "value");
        check_equal(row->uri, "config.json");
        check_equal(row->field_index, (size_t)0u);
        check_equal(row->field_offset,
                    offsetof(vxml_cmeta_program_root, value));
        check_true(row->field_data == &cmeta_data_int);
        check_not_null(row->plan);
        check_true(row->workspace_alignment != 0u);

        vxml_program_destroy(&program);
    }

    it("rejects data in VoiceXML 2.0 documents") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.0' "
            "datamodel='cmeta'>"
            "<data name='value' src='config.json'/>"
            "<form><block/></form></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            data_compile_options();
        vxml_program program = {0};
        vxml_diagnostic diagnostic = {0};

        check_equal(vxml_compile_cmeta(
                        source, sizeof(source) - 1u, NULL, &options,
                        &program, &diagnostic),
                    VXML_UNSUPPORTED_FEATURE);
        check_null(program.impl);
        check_equal(diagnostic.status, VXML_UNSUPPORTED_FEATURE);
    }

    it("requires explicit external-data limits when a data element is present") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'>"
            "<data name='value' src='config.json'/>"
            "<form><block/></form></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            compile_options();
        vxml_program program = {(void *)(uintptr_t)1u};
        vxml_diagnostic diagnostic = {0};

        check_equal(vxml_compile_cmeta(
                        source, sizeof(source) - 1u, NULL, &options,
                        &program, &diagnostic),
                    VXML_INVALID_CONTRACT);
        check_null(program.impl);
        check_equal(diagnostic.status, VXML_INVALID_CONTRACT);
    }

    it("rejects data names that do not map to one application-root field") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'>"
            "<data name='missing' src='config.json'/>"
            "<form><block/></form></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            data_compile_options();
        vxml_program program = {0};
        vxml_diagnostic diagnostic = {0};

        check_equal(vxml_compile_cmeta(
                        source, sizeof(source) - 1u, NULL, &options,
                        &program, &diagnostic),
                    VXML_SEMANTIC_ERROR);
        check_null(program.impl);
        check_equal(diagnostic.status, VXML_SEMANTIC_ERROR);
    }

    it("rejects duplicate document var and data names") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'>"
            "<var name='value' expr='1'/>"
            "<data name='value' src='config.json'/>"
            "<form><block/></form></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            data_compile_options();
        vxml_program program = {0};

        check_equal(vxml_compile_cmeta(
                        source, sizeof(source) - 1u, NULL, &options,
                        &program, NULL),
                    VXML_INVALID_STRUCTURE);
        check_null(program.impl);
    }

    it("admits an entity-decoded CMeta datamodel") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='c&#109;eta'><form><block/></form></vxml>";
        const vxml_cmeta_compile_options_v1 options = compile_options();
        vxml_program program = {0};
        vxml_diagnostic diagnostic = {0};

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &options,
                        &program, &diagnostic),
                    VXML_OK);
        check_not_null(program.impl);
        vxml_program_destroy(&program);
    }

    it("lowers typed declarations actions and block metadata") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'>"
            "<var name='value' expr='1'/>"
            "<form id='main'>"
            "<var name='flag' expr='true'/>"
            "<block name='ready' cond='flag'>"
            "<var name='value'/><assign name='value' expr='2'/>"
            "<exit expr='value'/>"
            "</block></form></vxml>";
        const vxml_cmeta_compile_options_v1 options = compile_options();
        vxml_program program = {0};
        vxml_diagnostic diagnostic = {0};
        const vxml_program_impl *impl;
        const vxml_cmeta_program_data *profile;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &options,
                        &program, &diagnostic),
                    VXML_OK);
        check_not_null(program.impl);
        if (program.impl == NULL) return;
        impl = (const vxml_program_impl *)program.impl;
        profile = (const vxml_cmeta_program_data *)impl->profile_data;
        check_not_null(profile);
        if (profile != NULL) {
            check_equal(profile->form_count, (size_t)1u);
            check_equal(profile->block_count, (size_t)1u);
            check_equal(profile->scope_count, (size_t)3u);
            check_equal(profile->declaration_count, (size_t)2u);
            check_equal(profile->action_count, (size_t)3u);
            check_equal(profile->expression_count, (size_t)5u);
            check_equal(profile->forms[0].scope, (size_t)1u);
            check_equal(profile->forms[0].first_declaration, (size_t)1u);
            check_equal(profile->forms[0].declaration_count, (size_t)1u);
            check_equal(profile->blocks[0].scope, (size_t)2u);
            check_equal(profile->blocks[0].form_item_slot, (size_t)1u);
            check_equal(profile->blocks[0].condition, (size_t)2u);
            check_equal(profile->scopes[0].schema.slot_count, (size_t)1u);
            check_equal(profile->scopes[1].schema.slot_count, (size_t)2u);
            check_equal(profile->scopes[2].schema.slot_count, (size_t)1u);
            check_equal(profile->actions[0].kind, VXML_CMETA_ACTION_VAR);
            check_equal(profile->actions[1].kind, VXML_CMETA_ACTION_ASSIGN);
            check_equal(profile->actions[2].kind, VXML_CMETA_ACTION_EXIT);
            check_equal(profile->actions[2].exit_kind,
                        VXML_CMETA_EXIT_EXPRESSION);
        }
        vxml_program_destroy(&program);
    }

    it("lowers repeated vars clear lists guards and namelist exits") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'>"
            "<var name='value'/><form><var name='flag'/>"
            "<block name='again' expr='false' cond='flag'>"
            "<var name='value'/><var name='value' expr='3'/>"
            "<clear/><clear namelist='value again'/>"
            "<exit namelist='value flag'/>"
            "</block></form></vxml>";
        const vxml_cmeta_compile_options_v1 options = compile_options();
        vxml_program program = {0};
        vxml_diagnostic diagnostic = {0};
        const vxml_cmeta_program_data *profile;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &options,
                        &program, &diagnostic),
                    VXML_OK);
        check_not_null(program.impl);
        if (program.impl == NULL) return;
        profile = (const vxml_cmeta_program_data *)
            ((const vxml_program_impl *)program.impl)->profile_data;
        check_not_null(profile);
        if (profile != NULL) {
            check_equal(profile->expression_count, (size_t)3u);
            check_equal(profile->location_count, (size_t)5u);
            check_equal(profile->action_count, (size_t)5u);
            check_equal(profile->blocks[0].initial_expression, (size_t)0u);
            check_equal(profile->blocks[0].condition, (size_t)1u);
            check_equal(profile->actions[0].kind, VXML_CMETA_ACTION_VAR);
            check_equal(profile->actions[1].kind, VXML_CMETA_ACTION_ASSIGN);
            check_equal(profile->actions[2].kind, VXML_CMETA_ACTION_CLEAR);
            check_true(profile->actions[2].clear_all_form_items);
            check_equal(profile->actions[3].kind, VXML_CMETA_ACTION_CLEAR);
            check_false(profile->actions[3].clear_all_form_items);
            check_equal(profile->actions[3].location_count, (size_t)2u);
            check_equal(profile->actions[4].kind, VXML_CMETA_ACTION_EXIT);
            check_equal(profile->actions[4].exit_kind,
                        VXML_CMETA_EXIT_NAMELIST);
            check_equal(profile->actions[4].location_count, (size_t)2u);
            check_equal(profile->locations[
                            profile->actions[3].first_location].name,
                        "value");
            check_equal(profile->locations[
                            profile->actions[3].first_location + 1u].name,
                        "again");
        }
        vxml_program_destroy(&program);
    }

    it("lowers nested conditional branch structure") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<if cond='flag'>"
            "<assign name='value' expr='1'/>"
            "<elseif cond='false'/><clear namelist='value'/>"
            "<else/><if cond='true'><exit/></if>"
            "</if><exit/>"
            "</block></form></vxml>";
        const vxml_cmeta_compile_options_v1 options = compile_options();
        vxml_program program = {0};
        vxml_diagnostic diagnostic = {0};
        const vxml_cmeta_program_data *profile;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &options,
                        &program, &diagnostic),
                    VXML_OK);
        check_not_null(program.impl);
        if (program.impl == NULL) return;
        profile = (const vxml_cmeta_program_data *)
            ((const vxml_program_impl *)program.impl)->profile_data;
        check_not_null(profile);
        if (profile != NULL) {
            check_equal(profile->action_count, (size_t)6u);
            check_equal(profile->branch_count, (size_t)4u);
            check_equal(profile->expression_count, (size_t)4u);
            check_equal(profile->actions[0].kind, VXML_CMETA_ACTION_IF);
            check_equal(profile->actions[0].first_branch, (size_t)0u);
            check_equal(profile->actions[0].branch_count, (size_t)3u);
            check_equal(profile->actions[0].next_action, (size_t)5u);
            check_equal(profile->branches[0].condition, (size_t)0u);
            check_equal(profile->branches[0].first_action, (size_t)1u);
            check_equal(profile->branches[0].action_end, (size_t)2u);
            check_equal(profile->branches[1].condition, (size_t)2u);
            check_equal(profile->branches[1].first_action, (size_t)2u);
            check_equal(profile->branches[1].action_end, (size_t)3u);
            check_equal(profile->branches[2].condition,
                        VXML_CMETA_NO_INDEX);
            check_equal(profile->branches[2].first_action, (size_t)3u);
            check_equal(profile->branches[2].action_end, (size_t)5u);
            check_equal(profile->actions[3].kind, VXML_CMETA_ACTION_IF);
            check_equal(profile->actions[3].first_branch, (size_t)3u);
            check_equal(profile->actions[3].branch_count, (size_t)1u);
            check_equal(profile->branches[3].condition, (size_t)3u);
            check_equal(profile->branches[3].first_action, (size_t)4u);
            check_equal(profile->branches[3].action_end, (size_t)5u);
            check_equal(profile->actions[5].kind, VXML_CMETA_ACTION_EXIT);
        }
        vxml_program_destroy(&program);
    }

    it("decodes identifiers expressions locations lists and form ids") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'>"
            "<var name='v&#97;lue' expr='1'/><form id='m&#97;in'>"
            "<var name='fl&#97;g'/><block name='ag&#97;in' "
            "expr='tr&#117;e' cond='fl&#97;g'>"
            "<assign name='v&#97;lue' expr='2'/>"
            "<clear namelist='ag&#97;in'/><exit expr='v&#97;lue'/>"
            "</block></form></vxml>";
        const vxml_cmeta_compile_options_v1 options = compile_options();
        vxml_program program = {0};
        vxml_diagnostic diagnostic = {0};
        const vxml_program_impl *impl;
        const vxml_cmeta_program_data *profile;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &options,
                        &program, &diagnostic),
                    VXML_OK);
        check_not_null(program.impl);
        if (program.impl == NULL) return;
        impl = (const vxml_program_impl *)program.impl;
        profile = (const vxml_cmeta_program_data *)impl->profile_data;
        check_equal(impl->forms[0].id, "main");
        check_equal(profile->declarations[0].name, "value");
        check_equal(profile->declarations[1].name, "flag");
        check_equal(profile->blocks[0].name, "again");
        check_equal(profile->locations[0].name, "value");
        check_equal(profile->locations[1].name, "again");
        vxml_program_destroy(&program);
    }

    it("rejects invalid placement order attributes and duplicate names") {
        static const char prefix[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'>";
        struct invalid_case {
            const char *body;
            vxml_status expected;
        };
        static const struct invalid_case cases[] = {
            {"<form><block/></form><var name='value'/>",
             VXML_INVALID_STRUCTURE},
            {"<form><block/><var name='value'/></form>",
             VXML_INVALID_STRUCTURE},
            {"<form><assign name='value' expr='1'/><block/></form>",
             VXML_INVALID_STRUCTURE},
            {"<block/>", VXML_INVALID_STRUCTURE},
            {"<form><block><elseif cond='flag'/></block></form>",
             VXML_INVALID_STRUCTURE},
            {"<form><block><else/></block></form>",
             VXML_INVALID_STRUCTURE},
            {"<form><block><var/></block></form>",
             VXML_INVALID_STRUCTURE},
            {"<form><block><assign expr='1'/></block></form>",
             VXML_INVALID_STRUCTURE},
            {"<form><block><assign name='value'/></block></form>",
             VXML_INVALID_STRUCTURE},
            {"<form><block><if><exit/></if></block></form>",
             VXML_INVALID_STRUCTURE},
            {"<form><block><if cond='flag'><elseif/></if></block></form>",
             VXML_INVALID_STRUCTURE},
            {"<form><block><if cond='flag'><else/><elseif cond='flag'/>"
             "</if></block></form>", VXML_INVALID_STRUCTURE},
            {"<form><block><if cond='flag'><else/><else/>"
             "</if></block></form>", VXML_INVALID_STRUCTURE},
            {"<form><block><clear namelist=''/></block></form>",
             VXML_INVALID_STRUCTURE},
            {"<form><block><exit expr='value' namelist='value'/>"
             "</block></form>", VXML_INVALID_STRUCTURE},
            {"<var name='value'/><var name='v&#97;lue'/>"
             "<form><block/></form>", VXML_INVALID_STRUCTURE},
            {"<form><var name='flag'/><var name='fl&#97;g'/><block/>"
             "</form>", VXML_INVALID_STRUCTURE},
            {"<form><var name='flag'/><block name='fl&#97;g'/>"
             "</form>", VXML_INVALID_STRUCTURE},
            {"<form><block name='again'/><block name='ag&#97;in'/>"
             "</form>", VXML_INVALID_STRUCTURE}
        };
        const vxml_cmeta_compile_options_v1 options = compile_options();
        size_t index;

        for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
            char source[512];
            vxml_program program = {0};
            vxml_diagnostic diagnostic = {0};
            const int written = snprintf(
                source, sizeof(source), "%s%s</vxml>", prefix,
                cases[index].body);
            check_true(written > 0 && (size_t)written < sizeof(source));
            check_equal(vxml_compile_cmeta(
                            source, (size_t)written, NULL, &options,
                            &program, &diagnostic),
                        cases[index].expected);
            check_null(program.impl);
            check_equal(diagnostic.status, cases[index].expected);
            vxml_program_destroy(&program);
        }
    }

    group("document declaration leaf validation") {
        it("rejects an unknown attribute") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
                "datamodel='cmeta'><var name='value' bogus='x'/>"
                "<form><block/></form></vxml>";
            check_program_rejected(source, VXML_UNSUPPORTED_FEATURE);
        }

        it("rejects a child executable element") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
                "datamodel='cmeta'><var name='value'><exit/></var>"
                "<form><block/></form></vxml>";
            check_program_rejected(source, VXML_INVALID_STRUCTURE);
        }

        it("rejects a prompt child element") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
                "datamodel='cmeta'><var name='value'>"
                "<prompt>spoken</prompt></var>"
                "<form><block/></form></vxml>";
            check_program_rejected(source, VXML_INVALID_STRUCTURE);
        }

        it("rejects non-whitespace PCDATA") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
                "datamodel='cmeta'><var name='value'>spoken text</var>"
                "<form><block/></form></vxml>";
            check_program_rejected(source, VXML_INVALID_STRUCTURE);
        }
    }

    group("form declaration leaf validation") {
        it("rejects an unknown attribute") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
                "datamodel='cmeta'><form>"
                "<var name='value' bogus='x'/><block/>"
                "</form></vxml>";
            check_program_rejected(source, VXML_UNSUPPORTED_FEATURE);
        }

        it("rejects a child executable element") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
                "datamodel='cmeta'><form>"
                "<var name='value'><exit/></var><block/>"
                "</form></vxml>";
            check_program_rejected(source, VXML_INVALID_STRUCTURE);
        }

        it("rejects a prompt child element") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
                "datamodel='cmeta'><form>"
                "<var name='value'><prompt>spoken</prompt></var><block/>"
                "</form></vxml>";
            check_program_rejected(source, VXML_INVALID_STRUCTURE);
        }

        it("rejects non-whitespace PCDATA") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
                "datamodel='cmeta'><form>"
                "<var name='value'>spoken text</var><block/>"
                "</form></vxml>";
            check_program_rejected(source, VXML_INVALID_STRUCTURE);
        }
    }

    it("reports error.badfetch for mutually exclusive exit data") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<exit expr='value' namelist='value'/>"
            "</block></form></vxml>";
        const vxml_cmeta_compile_options_v1 options = compile_options();
        vxml_program program = {0};
        vxml_diagnostic diagnostic = {0};

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &options,
                        &program, &diagnostic),
                    VXML_INVALID_STRUCTURE);
        check_not_null(strstr(diagnostic.message, "error.badfetch"));
        check_null(program.impl);
    }

    it("validates root form identifiers attributes and empty elements") {
        struct invalid_case {
            const char *source;
            vxml_status expected;
        };
        static const struct invalid_case cases[] = {
            {"<vxml xmlns='urn:wrong' version='2.1' datamodel='cmeta'>"
             "<form><block/></form></vxml>", VXML_INVALID_NAMESPACE},
            {"<root xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><form><block/></form></root>",
             VXML_INVALID_NAMESPACE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' datamodel='cmeta'>"
             "<form><block/></form></vxml>", VXML_INVALID_VERSION},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='3.0' "
             "datamodel='cmeta'><form><block/></form></vxml>",
             VXML_INVALID_VERSION},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta' bogus='x'><form><block/></form></vxml>",
             VXML_UNSUPPORTED_FEATURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><form bogus='x'><block/></form></vxml>",
             VXML_UNSUPPORTED_FEATURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><form><block bogus='x'/></form></vxml>",
             VXML_UNSUPPORTED_FEATURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><form><block><assign name='value' expr='1' "
             "bogus='x'/></block></form></vxml>", VXML_UNSUPPORTED_FEATURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><form id='1bad'><block/></form></vxml>",
             VXML_INVALID_STRUCTURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><form id='same'><block/></form>"
             "<form id='s&#97;me'><block/></form></vxml>", VXML_DUPLICATE_ID},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><var name='&#49;value'/>"
             "<form><block/></form></vxml>", VXML_INVALID_STRUCTURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><form><block name='bad&#58;name'/>"
             "</form></vxml>", VXML_INVALID_STRUCTURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><form><block><assign name='value' expr='1'>"
             "<exit/></assign></block></form></vxml>", VXML_INVALID_STRUCTURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><form><block><if cond='flag'>"
             "<else><exit/></else></if></block></form></vxml>",
             VXML_INVALID_STRUCTURE}
        };
        const vxml_cmeta_compile_options_v1 options = compile_options();
        size_t index;

        for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
            vxml_program program = {0};
            vxml_diagnostic diagnostic = {0};
            check_equal(vxml_compile_cmeta(
                            cases[index].source, strlen(cases[index].source),
                            NULL, &options, &program, &diagnostic),
                        cases[index].expected);
            check_null(program.impl);
            check_equal(diagnostic.status, cases[index].expected);
            vxml_program_destroy(&program);
        }
    }

    it("explicitly rejects deferred syntax and implicit prompt text") {
        static const char *const bodies[] = {
            "<form><block><value expr='value'/></block></form>",
            "<form><block><log>text</log></block></form>",
            "<form><block><prompt>text</prompt></block></form>",
            "<form><block><audio src='x'/></block></form>",
            "<form><field/></form>",
            "<form><filled/></form>",
            "<form><grammar/></form>",
            "<form><choice/></form>",
            "<form><menu/></form>",
            "<form><link/></form>",
            "<form><record/></form>",
            "<form><transfer/></form>",
            "<form><block><submit/></block></form>",
            "<form><block><data/></block></form>",
            "<form><block><script/></block></form>",
            "<form><block><object/></block></form>",
            "<form><subdialog/></form>",
            "<form><block>spoken text</block></form>"
        };
        static const char prefix[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'>";
        const vxml_cmeta_compile_options_v1 options = compile_options();
        size_t index;

        for (index = 0u; index < sizeof(bodies) / sizeof(bodies[0]); ++index) {
            char source[512];
            vxml_program program = {0};
            vxml_diagnostic diagnostic = {0};
            const int written = snprintf(
                source, sizeof(source), "%s%s</vxml>", prefix, bodies[index]);
            check_true(written > 0 && (size_t)written < sizeof(source));
            {
                const vxml_status expected =
                    index == 3u || index == 4u ||
                    index == 5u || index == 6u ||
                    index == 13u
                        ? VXML_INVALID_STRUCTURE
                        : VXML_UNSUPPORTED_FEATURE;
                check_equal(vxml_compile_cmeta(
                                source, (size_t)written, NULL, &options,
                                &program, &diagnostic),
                            expected);
                check_null(program.impl);
                check_equal(diagnostic.status, expected);
            }
            vxml_program_destroy(&program);
        }
    }

    it("lowers dotted locations through every lexical candidate") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><var name='nested'/><form>"
            "<var name='nested'/><block cond='nested.ready'>"
            "<var name='nested'/><assign name='nested.number' expr='value'/>"
            "<clear namelist='nested.ready'/><exit namelist='nested.number'/>"
            "</block></form></vxml>";
        const vxml_cmeta_compile_options_v1 options = compile_options();
        vxml_program program = {0};
        vxml_diagnostic diagnostic = {0};
        const vxml_cmeta_program_data *profile;
        const vxml_cmeta_location_row *location;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &options,
                        &program, &diagnostic),
                    VXML_OK);
        check_not_null(program.impl);
        if (program.impl == NULL) return;
        profile = (const vxml_cmeta_program_data *)
            ((const vxml_program_impl *)program.impl)->profile_data;
        check_equal(profile->location_count, (size_t)3u);
        check_equal(profile->location_candidate_count, (size_t)12u);
        location = &profile->locations[0];
        check_equal(location->name, "nested.number");
        check_true(location->value == &cmeta_data_int);
        check_equal(location->candidate_count, (size_t)4u);
        check_equal(profile->location_candidates[
                        location->first_candidate + 0u].scope,
                    profile->blocks[0].scope);
        check_equal(profile->location_candidates[
                        location->first_candidate + 1u].scope,
                    profile->forms[0].scope);
        check_equal(profile->location_candidates[
                        location->first_candidate + 2u].scope,
                    profile->document_scope);
        check_equal(profile->location_candidates[
                        location->first_candidate + 3u].root_field,
                    (size_t)2u);
        check_equal(profile->location_candidates[
                        location->first_candidate + 3u].location.offset,
                    offsetof(vxml_cmeta_program_root, nested) +
                        offsetof(vxml_cmeta_program_nested, number));
        vxml_program_destroy(&program);
    }

    it("rejects unknown incompatible and non Boolean semantics") {
        static const char prefix[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'>";
        struct semantic_case {
            const char *body;
            vxml_status expected;
        };
        static const struct semantic_case cases[] = {
            {"<var name='missing'/><form><block/></form>",
             VXML_SEMANTIC_ERROR},
            {"<form><block><assign name='missing' expr='1'/>"
             "</block></form>", VXML_SEMANTIC_ERROR},
            {"<form><block><assign name='flag' expr='1'/></block></form>",
             VXML_SEMANTIC_ERROR},
            {"<form><block><assign name='value' expr='true'/></block></form>",
             VXML_SEMANTIC_ERROR},
            {"<form><block cond='value'/></form>", VXML_SEMANTIC_ERROR},
            {"<form><block expr='value'/></form>", VXML_SEMANTIC_ERROR},
            {"<form><block><if cond='value'><exit/></if></block></form>",
             VXML_SEMANTIC_ERROR},
            {"<form><block><if cond='flag'><elseif cond='value'/>"
             "</if></block></form>", VXML_SEMANTIC_ERROR},
            {"<form><block><assign name='nested' expr='1'/></block></form>",
             VXML_SEMANTIC_ERROR},
            {"<form><block><assign name='nested.missing' expr='1'/>"
             "</block></form>", VXML_SEMANTIC_ERROR},
            {"<form><block><assign name='nested..number' expr='1'/>"
             "</block></form>", VXML_SEMANTIC_ERROR}
        };
        const vxml_cmeta_compile_options_v1 options = compile_options();
        size_t index;

        for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
            char source[512];
            vxml_program program = {0};
            vxml_diagnostic diagnostic = {0};
            const int written = snprintf(
                source, sizeof(source), "%s%s</vxml>", prefix,
                cases[index].body);
            check_true(written > 0 && (size_t)written < sizeof(source));
            check_equal(vxml_compile_cmeta(
                            source, (size_t)written, NULL, &options,
                            &program, &diagnostic),
                        cases[index].expected);
            check_null(program.impl);
            check_equal(diagnostic.status, cases[index].expected);
            vxml_program_destroy(&program);
        }
    }

    it("shares executable declarations across nested conditional branches") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<if cond='flag'><if cond='flag'><var name='value'/></if>"
            "<else/><var name='v&#97;lue' expr='2'/></if>"
            "</block></form></vxml>";
        const vxml_cmeta_compile_options_v1 options = compile_options();
        vxml_program program = {0};
        vxml_diagnostic diagnostic = {0};
        const vxml_cmeta_program_data *profile;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &options,
                        &program, &diagnostic),
                    VXML_OK);
        check_not_null(program.impl);
        if (program.impl == NULL) return;
        profile = (const vxml_cmeta_program_data *)
            ((const vxml_program_impl *)program.impl)->profile_data;
        check_equal(profile->action_count, (size_t)4u);
        check_equal(profile->branch_count, (size_t)3u);
        check_equal(profile->expression_count, (size_t)3u);
        check_equal(profile->location_count, (size_t)1u);
        check_equal(profile->scopes[profile->blocks[0].scope]
                        .schema.slot_count,
                    (size_t)1u);
        check_equal(profile->actions[2].kind, VXML_CMETA_ACTION_VAR);
        check_equal(profile->actions[3].kind, VXML_CMETA_ACTION_ASSIGN);
        vxml_program_destroy(&program);
    }

    it("enforces exact compiler and global limits") {
        static const char conditional_source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block><if cond='flag'>"
            "<if cond='flag'><exit/></if></if></block></form></vxml>";
        static const char path_source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block>"
            "<assign name='nested.number' expr='1'/>"
            "</block></form></vxml>";
        static const char slots_source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><var name='flag'/><block/>"
            "</form></vxml>";
        static const char storage_source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><var name='value'/><form><block/>"
            "</form></vxml>";
        static const char forms_source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block/></form>"
            "<form><block/></form></vxml>";
        static const char blocks_source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block/><block/></form></vxml>";
        static const char actions_source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><var name='value'/><form><block><exit/>"
            "</block></form></vxml>";
        static const char name_source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form id='abc'><block/></form></vxml>";
        vxml_cmeta_compile_options_v1 options = compile_options();
        vxml_limits limits = vxml_default_limits();
        vxml_program program = {0};
        vxml_diagnostic diagnostic = {0};

#define CHECK_LIMIT_PAIR(source_, exact_statement_, over_statement_) \
        do { \
            exact_statement_; \
            check_equal(vxml_compile_cmeta( \
                            (source_), strlen(source_), &limits, &options, \
                            &program, &diagnostic), VXML_OK); \
            vxml_program_destroy(&program); \
            over_statement_; \
            check_equal(vxml_compile_cmeta( \
                            (source_), strlen(source_), &limits, &options, \
                            &program, &diagnostic), VXML_LIMIT_EXCEEDED); \
            check_null(program.impl); \
            options = compile_options(); \
            limits = vxml_default_limits(); \
        } while (0)

        CHECK_LIMIT_PAIR(conditional_source,
                         options.max_conditional_depth = 2u,
                         options.max_conditional_depth = 1u);
        CHECK_LIMIT_PAIR(path_source,
                         options.max_path_depth = 2u,
                         options.max_path_depth = 1u);
        CHECK_LIMIT_PAIR(slots_source,
                         options.max_scope_slots = 2u,
                         options.max_scope_slots = 1u);
        CHECK_LIMIT_PAIR(storage_source,
                         options.max_scope_storage_bytes = sizeof(int),
                         options.max_scope_storage_bytes = sizeof(int) - 1u);
        CHECK_LIMIT_PAIR(forms_source,
                         limits.max_forms = 2u,
                         limits.max_forms = 1u);
        CHECK_LIMIT_PAIR(blocks_source,
                         limits.max_blocks = 2u,
                         limits.max_blocks = 1u);
        CHECK_LIMIT_PAIR(actions_source,
                         limits.max_actions = 2u,
                         limits.max_actions = 1u);
        CHECK_LIMIT_PAIR(name_source,
                         limits.max_name_bytes = 4u,
                         limits.max_name_bytes = 3u);
        CHECK_LIMIT_PAIR(name_source,
                         limits.xml.max_input_bytes = strlen(name_source),
                         limits.xml.max_input_bytes = strlen(name_source) - 1u);
#undef CHECK_LIMIT_PAIR
    }

    it("retains XML coordinates and path or expression byte offsets") {
        static const char path_source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'\n"
            " datamodel='cmeta'>\n<form>\n<block>\n"
            "<assign name='nested.missing' expr='1'/>\n"
            "</block></form></vxml>";
        static const char expression_source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'\n"
            " datamodel='cmeta'>\n<form>\n<block cond='missing + 1'/>\n"
            "</form></vxml>";
        const vxml_cmeta_compile_options_v1 options = compile_options();
        vxml_program program = {0};
        vxml_diagnostic diagnostic = {0};

        check_equal(vxml_compile_cmeta(
                        path_source, strlen(path_source), NULL, &options,
                        &program, &diagnostic),
                    VXML_SEMANTIC_ERROR);
        check_equal(diagnostic.location.line, (uint32_t)5u);
        check_not_null(strstr(diagnostic.message, "byte offset 7"));
        check_null(program.impl);

        memset(&diagnostic, 0, sizeof(diagnostic));
        check_equal(vxml_compile_cmeta(
                        expression_source, strlen(expression_source), NULL,
                        &options, &program, &diagnostic),
                    VXML_SEMANTIC_ERROR);
        check_equal(diagnostic.location.line, (uint32_t)4u);
        check_not_null(strstr(diagnostic.message, "byte offset 0"));
        check_null(program.impl);
    }

    it("destroys every partial program across allocation failures") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><var name='value' expr='1'/>"
            "<var name='nested'/><form id='main'><var name='flag'/>"
            "<var name='nested'/><block name='ready' expr='true' cond='flag'>"
            "<var name='value' expr='2'/><var name='nested'/>"
            "<assign name='nested.number' expr='value'/>"
            "<if cond='nested.ready'><clear namelist='ready nested.ready'/>"
            "<elseif cond='false'/><assign name='value' expr='3'/>"
            "<else/><exit namelist='value nested.number'/></if>"
            "</block><block><exit expr='value'/></block></form></vxml>";
        const vxml_cmeta_compile_options_v1 options = compile_options();
        bool reached_success = false;
        size_t failure;

        vxml_test_allocator_set(&program_test_allocator);
        for (failure = 1u; failure <= PROGRAM_ALLOCATION_CAPACITY; ++failure) {
            vxml_program program = {(void *)1};
            vxml_diagnostic diagnostic = {0};
            vxml_status status;
            memset(&program_allocations, 0, sizeof(program_allocations));
            program_allocations.fail_on_call = failure;
            info("program allocation failure point %zu", failure);
            status = vxml_compile_cmeta(
                source, strlen(source), NULL, &options,
                &program, &diagnostic);
            if (status == VXML_OK) {
                reached_success = true;
                check_true(program_allocations.live_count != 0u);
                vxml_program_destroy(&program);
                check_equal(program_allocations.live_count, (size_t)0u);
                check_equal(program_allocations.invalid_operations,
                            (size_t)0u);
                break;
            }
            check_equal(status, VXML_ALLOCATION_FAILED);
            check_null(program.impl);
            check_equal(diagnostic.status, VXML_ALLOCATION_FAILED);
            check_true(program_allocations.calls >= failure);
            check_equal(program_allocations.live_count, (size_t)0u);
            check_equal(program_allocations.invalid_operations, (size_t)0u);
        }
        vxml_test_allocator_reset();
        check_true(reached_success);
    }
}
