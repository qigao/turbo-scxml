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

static vxml_cmeta_compile_options_v1 menu_compile_options(void) {
    vxml_cmeta_compile_options_v1 options = compile_options();
    options.max_event_handlers = 8u;
    options.max_event_name_bytes = 64u;
    options.max_menus = 4u;
    options.max_menu_choices = 16u;
    options.max_menu_choice_bytes = 16u;
    options.max_menu_target_bytes = 256u;
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
    it("compiles an immutable anonymous static DTMF menu without a root field") {
        char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><menu id='main' dtmf='true'>"
            "<choice event='menu.one'/>"
            "<choice dtmf=' 0 ' event='menu.zero'/>"
            "<choice event='menu.two'/>"
            "</menu></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            menu_compile_options();
        vxml_program program = {0};
        const vxml_program_impl *impl;
        const vxml_cmeta_program_data *profile;

        check_equal(
            vxml_compile_cmeta(
                source, strlen(source), NULL, &options, &program, NULL),
            VXML_OK);
        impl = (const vxml_program_impl *)program.impl;
        profile = impl != NULL
            ? (const vxml_cmeta_program_data *)impl->profile_data : NULL;
        memset(source, 'x', sizeof(source) - 1u);

        check_not_null(impl);
        check_not_null(profile);
        check_equal(profile->form_count, (size_t)1u);
        check_equal(profile->field_count, (size_t)0u);
        check_equal(profile->block_count, (size_t)0u);
        check_equal(profile->menu_count, (size_t)1u);
        check_equal(profile->menu_choice_count, (size_t)3u);
        check_equal(profile->forms[0].menu, (size_t)0u);
        check_equal(profile->menus[0].form, (size_t)0u);
        check_equal(profile->menus[0].first_choice, (size_t)0u);
        check_equal(profile->menus[0].choice_count, (size_t)3u);
        check_equal(impl->forms[0].id_size, sizeof("main") - 1u);
        check_equal(
            memcmp(impl->forms[0].id, "main", sizeof("main") - 1u), 0);

        check_equal(profile->menu_choices[0].dtmf.size, (size_t)1u);
        check_equal(profile->menu_choices[0].dtmf.data[0], '1');
        check_equal(profile->menu_choices[1].dtmf.data[0], '0');
        check_equal(profile->menu_choices[2].dtmf.data[0], '2');
        check_null(profile->menu_choices[0].speech.data);
        check_equal(profile->menu_choices[0].speech.size, (size_t)0u);
        check_equal(
            profile->menu_choice_targets[0].kind,
            VXML_CMETA_MENU_CHOICE_EVENT);
        check_equal(
            profile->menu_choice_targets[0].target_size,
            sizeof("menu.one") - 1u);
        check_equal(
            memcmp(
                profile->menu_choice_targets[0].target,
                "menu.one", sizeof("menu.one") - 1u), 0);
        check_equal(
            memcmp(
                profile->menu_choice_targets[2].target,
                "menu.two", sizeof("menu.two") - 1u), 0);

        vxml_program_destroy(&program);
    }

    it("retains normalized exact menu speech phrases in immutable Program storage") {
        char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><menu id='speech' accept='exact'>"
            "<choice event='menu.stars'>  Stargazer\n"
            " \t news  </choice>"
            "<choice dtmf='0' accept='exact' event='menu.zero'> zero </choice>"
            "</menu></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            menu_compile_options();
        vxml_program program = {0};
        const vxml_program_impl *impl;
        const vxml_cmeta_program_data *profile;

        check_equal(
            vxml_compile_cmeta(
                source, strlen(source), NULL, &options, &program, NULL),
            VXML_OK);
        impl = (const vxml_program_impl *)program.impl;
        profile = impl != NULL
            ? (const vxml_cmeta_program_data *)impl->profile_data : NULL;
        memset(source, 'x', sizeof(source) - 1u);

        check_not_null(profile);
        check_equal(profile->menu_choice_count, (size_t)2u);
        check_null(profile->menu_choices[0].dtmf.data);
        check_equal(profile->menu_choices[0].dtmf.size, (size_t)0u);
        check_equal(
            profile->menu_choices[0].speech.size,
            sizeof("Stargazer news") - 1u);
        check_equal(
            memcmp(
                profile->menu_choices[0].speech.data,
                "Stargazer news",
                sizeof("Stargazer news") - 1u),
            0);
        check_equal(profile->menu_choices[1].dtmf.size, (size_t)1u);
        check_equal(profile->menu_choices[1].dtmf.data[0], '0');
        check_equal(
            profile->menu_choices[1].speech.size,
            sizeof("zero") - 1u);
        check_equal(
            memcmp(
                profile->menu_choices[1].speech.data,
                "zero", sizeof("zero") - 1u),
            0);
        check_true(profile->menu_choices[0].speech.data >= impl->storage);
        check_true(
            profile->menu_choices[0].speech.data <
                impl->storage + impl->storage_size);

        vxml_program_destroy(&program);
    }

    it("retains literal menu next targets in immutable Program storage") {
        char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><menu id='main' dtmf='true'>"
            "<choice next='#target'/>"
            "<choice dtmf='0' next='leaf.vxml#target'/>"
            "</menu><form id='target'><block><exit/></block></form></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            menu_compile_options();
        vxml_program program = {0};
        const vxml_program_impl *impl;
        const vxml_cmeta_program_data *profile;

        check_equal(
            vxml_compile_cmeta(
                source, strlen(source), NULL, &options, &program, NULL),
            VXML_OK);
        impl = (const vxml_program_impl *)program.impl;
        profile = impl != NULL
            ? (const vxml_cmeta_program_data *)impl->profile_data : NULL;
        memset(source, 'x', sizeof(source) - 1u);

        check_not_null(impl);
        check_not_null(profile);
        check_equal(profile->menu_count, (size_t)1u);
        check_equal(profile->menu_choice_count, (size_t)2u);
        check_equal(
            profile->menu_choice_targets[0].kind,
            VXML_CMETA_MENU_CHOICE_NEXT);
        check_equal(
            profile->menu_choice_targets[0].target_size,
            sizeof("#target") - 1u);
        check_equal(
            memcmp(
                profile->menu_choice_targets[0].target,
                "#target", sizeof("#target") - 1u), 0);
        check_equal(
            profile->menu_choice_targets[1].kind,
            VXML_CMETA_MENU_CHOICE_NEXT);
        check_equal(
            profile->menu_choice_targets[1].target_size,
            sizeof("leaf.vxml#target") - 1u);
        check_equal(
            memcmp(
                profile->menu_choice_targets[1].target,
                "leaf.vxml#target",
                sizeof("leaf.vxml#target") - 1u), 0);
        check_true(
            profile->menu_choice_targets[0].target >= impl->storage);
        check_true(
            profile->menu_choice_targets[0].target <
                impl->storage + impl->storage_size);

        vxml_program_destroy(&program);
    }

    it("leaves implicit choices after nine without a DTMF assignment") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><menu dtmf='true'>"
            "<choice event='e1'/><choice event='e2'/>"
            "<choice event='e3'/><choice event='e4'/>"
            "<choice event='e5'/><choice event='e6'/>"
            "<choice event='e7'/><choice event='e8'/>"
            "<choice event='e9'/><choice event='e10'/>"
            "</menu></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            menu_compile_options();
        vxml_program program = {0};
        const vxml_cmeta_program_data *profile;

        check_equal(
            vxml_compile_cmeta(
                source, sizeof(source) - 1u, NULL,
                &options, &program, NULL),
            VXML_OK);
        profile = program.impl != NULL
            ? (const vxml_cmeta_program_data *)
                ((const vxml_program_impl *)program.impl)->profile_data
            : NULL;
        check_not_null(profile);
        check_equal(profile->menu_choice_count, (size_t)10u);
        check_equal(profile->menu_choices[8].dtmf.size, (size_t)1u);
        check_equal(profile->menu_choices[8].dtmf.data[0], '9');
        check_null(profile->menu_choices[9].dtmf.data);
        check_equal(profile->menu_choices[9].dtmf.size, (size_t)0u);

        vxml_program_destroy(&program);
    }

    it("fails closed for invalid or deferred static menu syntax") {
        static const struct {
            const char *source;
            vxml_status expected;
        } cases[] = {
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><menu><choice dtmf='1' event='a'/>"
             "<choice dtmf='1' event='b'/></menu></vxml>",
             VXML_INVALID_STRUCTURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><menu dtmf='true'>"
             "<choice dtmf='1' event='a'/></menu></vxml>",
             VXML_INVALID_STRUCTURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><menu><choice dtmf='1' next='#x' event='a'/>"
             "</menu></vxml>", VXML_INVALID_STRUCTURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><menu><choice dtmf='1' next='#'/>"
             "</menu></vxml>", VXML_INVALID_STRUCTURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><menu><choice dtmf='1'/></menu></vxml>",
             VXML_INVALID_STRUCTURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><menu accept='approximate'>"
             "<choice event='a'>sports news</choice></menu></vxml>",
             VXML_UNSUPPORTED_FEATURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><menu><choice accept='approximate' event='a'>"
             "sports news</choice></menu></vxml>",
             VXML_UNSUPPORTED_FEATURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><menu><choice event='a'>"
             "<grammar type='application/srgs+xml' src='sports.grxml'/>"
             "sports</choice></menu></vxml>",
             VXML_UNSUPPORTED_FEATURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><menu dtmf='maybe'>"
             "<choice dtmf='1' event='a'/></menu></vxml>",
             VXML_INVALID_STRUCTURE}
        };
        const vxml_cmeta_compile_options_v1 options =
            menu_compile_options();
        size_t index;
        for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
            vxml_program program = {0};
            check_equal(
                vxml_compile_cmeta(
                    cases[index].source, strlen(cases[index].source),
                    NULL, &options, &program, NULL),
                cases[index].expected);
            check_null(program.impl);
        }
        {
            static const char target[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
                "datamodel='cmeta'><menu><choice dtmf='1' "
                "next='leaf.vxml'/></menu></vxml>";
            vxml_program program = {0};
            vxml_cmeta_compile_options_v1 bounded =
                menu_compile_options();
            bounded.max_menu_target_bytes = 4u;
            check_equal(
                vxml_compile_cmeta(
                    target, sizeof(target) - 1u, NULL,
                    &bounded, &program, NULL),
                VXML_LIMIT_EXCEEDED);
            check_null(program.impl);
        }
        {
            static const char speech[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
                "datamodel='cmeta'><menu><choice event='a'>"
                "long phrase</choice></menu></vxml>";
            vxml_program program = {0};
            vxml_cmeta_compile_options_v1 bounded =
                menu_compile_options();
            bounded.max_menu_choice_bytes = 4u;
            check_equal(
                vxml_compile_cmeta(
                    speech, sizeof(speech) - 1u, NULL,
                    &bounded, &program, NULL),
                VXML_LIMIT_EXCEEDED);
            check_null(program.impl);
        }
        {
            static const char event_only[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
                "datamodel='cmeta'><menu><choice dtmf='1' event='a'/>"
                "</menu></vxml>";
            static const char next_only[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
                "datamodel='cmeta'><menu><choice dtmf='1' next='#x'/>"
                "</menu></vxml>";
            vxml_program program = {0};
            vxml_cmeta_compile_options_v1 prefix =
                menu_compile_options();
            prefix.struct_size =
                offsetof(vxml_cmeta_compile_options_v1,
                         max_menu_target_bytes);
            check_equal(
                vxml_compile_cmeta(
                    event_only, sizeof(event_only) - 1u, NULL,
                    &prefix, &program, NULL),
                VXML_OK);
            vxml_program_destroy(&program);
            check_equal(
                vxml_compile_cmeta(
                    next_only, sizeof(next_only) - 1u, NULL,
                    &prefix, &program, NULL),
                VXML_INVALID_CONTRACT);
            check_null(program.impl);
        }
        {
            static const char disabled[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
                "datamodel='cmeta'><menu><choice dtmf='1' event='a'/>"
                "</menu></vxml>";
            vxml_program program = {0};
            const vxml_cmeta_compile_options_v1 disabled_options =
                compile_options();
            check_equal(
                vxml_compile_cmeta(
                    disabled, sizeof(disabled) - 1u, NULL,
                    &disabled_options, &program, NULL),
                VXML_INVALID_CONTRACT);
            check_null(program.impl);
        }
    }

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

    it("compiles audio alternate content as a conditional fallback range") {
        char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><field name='value'>"
            "<prompt>Before<audio src='welcome.wav'>Fallback "
            "<emphasis>voice</emphasis></audio>After</prompt>"
            "<grammar type='application/srgs+xml' src='a'/>"
            "</field></form></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            prompt_compile_options();
        vxml_program program = {0};
        const vxml_cmeta_program_data *profile;
        const vxml_cmeta_prompt_row *prompt;
        const vxml_cmeta_prompt_media_fallback_v1 *fallback;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &options,
                        &program, NULL),
                    VXML_OK);
        memset(source, 'X', sizeof(source) - 1u);

        profile = (const vxml_cmeta_program_data *)
            ((const vxml_program_impl *)program.impl)->profile_data;
        check_not_null(profile);
        check_equal(profile->prompt_segment_count, (size_t)5u);
        check_equal(profile->prompt_fallback_count, (size_t)1u);
        prompt = &profile->prompts[0];
        check_equal(prompt->segment_count, (size_t)5u);
        check_equal(prompt->fallback_count, (size_t)1u);
        check_equal(prompt->required_capabilities,
                    VXML_CMETA_PROMPT_MEDIA_CAP_TEXT |
                    VXML_CMETA_PROMPT_MEDIA_CAP_SSML |
                    VXML_CMETA_PROMPT_MEDIA_CAP_AUDIO |
                    VXML_CMETA_PROMPT_MEDIA_CAP_BATCH |
                    VXML_CMETA_PROMPT_MEDIA_CAP_AUDIO_FALLBACK);
        check_equal(profile->prompt_segments[0].kind,
                    VXML_CMETA_PROMPT_MEDIA_TEXT);
        check_equal(profile->prompt_segments[1].kind,
                    VXML_CMETA_PROMPT_MEDIA_AUDIO);
        check_equal(profile->prompt_segments[2].kind,
                    VXML_CMETA_PROMPT_MEDIA_TEXT);
        check_equal(profile->prompt_segments[3].kind,
                    VXML_CMETA_PROMPT_MEDIA_SSML);
        check_equal(profile->prompt_segments[4].kind,
                    VXML_CMETA_PROMPT_MEDIA_TEXT);
        fallback = &profile->prompt_fallbacks[0];
        check_equal(fallback->audio_segment_index, (size_t)1u);
        check_equal(fallback->first_fallback_segment, (size_t)2u);
        check_equal(fallback->fallback_segment_count, (size_t)2u);
        check_equal(memcmp(
                        profile->prompt_segments[2].payload.data,
                        "Fallback ", sizeof("Fallback ") - 1u), 0);
        check_not_null(strstr(
            profile->prompt_segments[3].payload.data, "<emphasis"));
        check_equal(memcmp(
                        profile->prompt_segments[4].payload.data,
                        "After", sizeof("After") - 1u), 0);

        vxml_program_destroy(&program);
    }

    it("compiles static SSML subtrees into Program-owned ordered segments") {
        char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><field name='value'>"
            "<prompt>Hello <emphasis level='strong'>very "
            "<prosody rate='slow'>careful</prosody></emphasis>"
            "<audio src='tone.wav'/></prompt>"
            "<grammar type='application/srgs+xml' src='a'/>"
            "</field></form></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            prompt_compile_options();
        vxml_program program = {0};
        const vxml_cmeta_program_data *profile;
        const vxml_cmeta_prompt_row *prompt;
        const vxml_cmeta_prompt_media_segment_v1 *ssml;

        check_equal(vxml_compile_cmeta(
                        source, strlen(source), NULL, &options,
                        &program, NULL),
                    VXML_OK);
        memset(source, 'X', sizeof(source) - 1u);

        profile = (const vxml_cmeta_program_data *)
            ((const vxml_program_impl *)program.impl)->profile_data;
        check_not_null(profile);
        check_equal(profile->prompt_segment_count, (size_t)3u);
        prompt = &profile->prompts[0];
        check_equal(prompt->segment_count, (size_t)3u);
        check_equal(prompt->required_capabilities,
                    VXML_CMETA_PROMPT_MEDIA_CAP_TEXT |
                    VXML_CMETA_PROMPT_MEDIA_CAP_SSML |
                    VXML_CMETA_PROMPT_MEDIA_CAP_AUDIO |
                    VXML_CMETA_PROMPT_MEDIA_CAP_BATCH);
        check_equal(profile->prompt_segments[0].kind,
                    VXML_CMETA_PROMPT_MEDIA_TEXT);
        check_equal(profile->prompt_segments[1].kind,
                    VXML_CMETA_PROMPT_MEDIA_SSML);
        check_equal(profile->prompt_segments[2].kind,
                    VXML_CMETA_PROMPT_MEDIA_AUDIO);
        ssml = &profile->prompt_segments[1];
        check_not_null(ssml->payload.data);
        check_true(ssml->payload.size != 0u);
        check_not_null(strstr(ssml->payload.data, "<emphasis"));
        check_not_null(strstr(ssml->payload.data, "<prosody"));
        check_not_null(strstr(ssml->payload.data, "careful"));
        check_equal(ssml->media_type.size,
                    sizeof("application/ssml+xml") - 1u);
        check_equal(memcmp(
                        ssml->media_type.data, "application/ssml+xml",
                        ssml->media_type.size), 0);

        vxml_program_destroy(&program);
    }

    it("rejects dynamic or unsupported speech markup instead of flattening it") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><field name='value'>"
            "<prompt><metadata><x xmlns='urn:test'>y</x></metadata></prompt>"
            "<grammar type='application/srgs+xml' src='a'/>"
            "</field></form></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            prompt_compile_options();
        vxml_program program = {0};

        check_equal(vxml_compile_cmeta(
                        source, sizeof(source) - 1u, NULL, &options,
                        &program, NULL),
                    VXML_UNSUPPORTED_FEATURE);
        check_null(program.impl);
    }

    it("compiles literal marks into immutable prompt segment order") {
        char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><field name='value'>"
            "<prompt>Hello<mark name='ad_start'/><audio src='a.wav'/>"
            "<mark name='ad_end'/></prompt>"
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
        check_equal(profile->prompt_segment_count, (size_t)4u);
        check_not_null(profile->prompt_segments);
        prompt = &profile->prompts[0];
        check_equal(prompt->segment_count, (size_t)4u);
        check_equal(prompt->required_capabilities,
                    VXML_CMETA_PROMPT_MEDIA_CAP_TEXT |
                    VXML_CMETA_PROMPT_MEDIA_CAP_AUDIO |
                    VXML_CMETA_PROMPT_MEDIA_CAP_MARK |
                    VXML_CMETA_PROMPT_MEDIA_CAP_BATCH);
        check_equal(profile->prompt_segments[0].kind,
                    VXML_CMETA_PROMPT_MEDIA_TEXT);
        check_equal(profile->prompt_segments[1].kind,
                    VXML_CMETA_PROMPT_MEDIA_MARK);
        check_equal(profile->prompt_segments[2].kind,
                    VXML_CMETA_PROMPT_MEDIA_AUDIO);
        check_equal(profile->prompt_segments[3].kind,
                    VXML_CMETA_PROMPT_MEDIA_MARK);
        check_equal(profile->prompt_segments[0].payload.size,
                    sizeof("Hello") - 1u);
        check_equal(memcmp(
                        profile->prompt_segments[0].payload.data,
                        "Hello", sizeof("Hello") - 1u), 0);
        check_equal(profile->prompt_segments[1].payload.size,
                    sizeof("ad_start") - 1u);
        check_equal(memcmp(
                        profile->prompt_segments[1].payload.data,
                        "ad_start", sizeof("ad_start") - 1u), 0);
        check_equal(profile->prompt_segments[2].payload.size,
                    sizeof("a.wav") - 1u);
        check_equal(memcmp(
                        profile->prompt_segments[2].payload.data,
                        "a.wav", sizeof("a.wav") - 1u), 0);
        check_equal(profile->prompt_segments[3].payload.size,
                    sizeof("ad_end") - 1u);
        check_equal(memcmp(
                        profile->prompt_segments[3].payload.data,
                        "ad_end", sizeof("ad_end") - 1u), 0);

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

    it("rejects missing empty and dynamic mark names in the literal profile") {
        static const struct {
            const char *body;
            vxml_status expected;
        } cases[] = {
            {
                "<form><field name='value'><prompt><mark/></prompt>"
                "<grammar type='application/srgs+xml' src='a'/></field></form>",
                VXML_INVALID_STRUCTURE
            },
            {
                "<form><field name='value'><prompt><mark name=''/></prompt>"
                "<grammar type='application/srgs+xml' src='a'/></field></form>",
                VXML_INVALID_STRUCTURE
            },
            {
                "<form><field name='value'><prompt><mark nameexpr='x'/></prompt>"
                "<grammar type='application/srgs+xml' src='a'/></field></form>",
                VXML_UNSUPPORTED_FEATURE
            },
            {
                "<form><field name='value'><prompt>"
                "<mark name='x' nameexpr='y'/></prompt>"
                "<grammar type='application/srgs+xml' src='a'/></field></form>",
                VXML_UNSUPPORTED_FEATURE
            }
        };
        static const char prefix[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'>";
        const vxml_cmeta_compile_options_v1 options =
            prompt_compile_options();
        size_t index;

        for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
            char source[768];
            vxml_program program = {0};
            const int written = snprintf(
                source, sizeof(source), "%s%s</vxml>",
                prefix, cases[index].body);
            check_true(written > 0 && (size_t)written < sizeof(source));
            check_equal(vxml_compile_cmeta(
                            source, (size_t)written, NULL, &options,
                            &program, NULL),
                        cases[index].expected);
            check_null(program.impl);
        }
    }

    it("compiles exact literal prompt time designations to microseconds") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><field name='value'>"
            "<prompt timeout='250ms'>a</prompt>"
            "<prompt count='2' timeout='1s'>b</prompt>"
            "<prompt count='3' timeout='1.5s'>c</prompt>"
            "<prompt count='4' timeout='0ms'>d</prompt>"
            "<prompt count='5'>e</prompt>"
            "<grammar type='application/srgs+xml' src='a'/>"
            "</field></form></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            prompt_compile_options();
        vxml_program program = {0};
        const vxml_cmeta_program_data *profile;

        check_equal(vxml_compile_cmeta(
                        source, sizeof(source) - 1u, NULL, &options,
                        &program, NULL),
                    VXML_OK);
        profile = (const vxml_cmeta_program_data *)
            ((const vxml_program_impl *)program.impl)->profile_data;
        check_not_null(profile);
        check_equal(profile->prompt_count, (size_t)5u);
        check_true(profile->prompts[0].has_timeout);
        check_equal(profile->prompts[0].timeout_us, UINT64_C(250000));
        check_true(profile->prompts[1].has_timeout);
        check_equal(profile->prompts[1].timeout_us, UINT64_C(1000000));
        check_true(profile->prompts[2].has_timeout);
        check_equal(profile->prompts[2].timeout_us, UINT64_C(1500000));
        check_true(profile->prompts[3].has_timeout);
        check_equal(profile->prompts[3].timeout_us, UINT64_C(0));
        check_false(profile->prompts[4].has_timeout);
        check_equal(profile->prompts[4].timeout_us, UINT64_C(0));

        vxml_program_destroy(&program);
    }

    it("rejects malformed overflowing or over-precise prompt timeouts") {
        static const struct {
            const char *timeout;
            vxml_status expected;
        } cases[] = {
            {"1", VXML_INVALID_STRUCTURE},
            {"-1s", VXML_INVALID_STRUCTURE},
            {"NaNs", VXML_INVALID_STRUCTURE},
            {"1.s", VXML_INVALID_STRUCTURE},
            {"1.0000001s", VXML_LIMIT_EXCEEDED},
            {"18446744073709551615s", VXML_LIMIT_EXCEEDED}
        };
        static const char prefix[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><field name='value'><prompt timeout='";
        static const char suffix[] =
            "'>x</prompt><grammar type='application/srgs+xml' src='a'/>"
            "</field></form></vxml>";
        const vxml_cmeta_compile_options_v1 options =
            prompt_compile_options();
        size_t index;

        for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
            char source[768];
            vxml_program program = {0};
            const int written = snprintf(
                source, sizeof(source), "%s%s%s",
                prefix, cases[index].timeout, suffix);
            check_true(written > 0 && (size_t)written < sizeof(source));
            check_equal(vxml_compile_cmeta(
                            source, (size_t)written, NULL, &options,
                            &program, NULL),
                        cases[index].expected);
            check_null(program.impl);
        }
    }

    it("rejects invalid prompt barge-in policy at compile time") {
        static const struct {
            const char *body;
            vxml_status expected;
        } cases[] = {
            {
                "<form><field name='value'>"
                "<prompt bargein='maybe'>x</prompt>"
                "<grammar type='application/srgs+xml' src='a'/>"
                "</field></form>",
                VXML_INVALID_STRUCTURE
            },
            {
                "<form><field name='value'>"
                "<prompt bargeintype='other'>x</prompt>"
                "<grammar type='application/srgs+xml' src='a'/>"
                "</field></form>",
                VXML_INVALID_STRUCTURE
            },
            {
                "<form><field name='value'>"
                "<prompt bargein='false' bargeintype='speech'>x</prompt>"
                "<grammar type='application/srgs+xml' src='a'/>"
                "</field></form>",
                VXML_INVALID_STRUCTURE
            }
        };
        static const char prefix[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'>";
        const vxml_cmeta_compile_options_v1 options =
            prompt_compile_options();
        size_t index;

        for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
            char source[768];
            vxml_program program = {0};
            const int written = snprintf(
                source, sizeof(source), "%s%s</vxml>",
                prefix, cases[index].body);
            check_true(written > 0 && (size_t)written < sizeof(source));
            check_equal(vxml_compile_cmeta(
                            source, (size_t)written, NULL, &options,
                            &program, NULL),
                        cases[index].expected);
            check_null(program.impl);
        }
    }

    it("rejects invalid prompt count text limits and child markup") {
        static const char *const bodies[] = {
            "<form><field name='value'><prompt count='0'>x</prompt>"
            "<grammar type='application/srgs+xml' src='a'/></field></form>",
            "<form><field name='value'><prompt>toolong</prompt>"
            "<grammar type='application/srgs+xml' src='a'/></field></form>",
            "<form><field name='value'><prompt><audio expr='x'/></prompt>"
            "<grammar type='application/srgs+xml' src='a'/></field></form>"
        };
        static const vxml_status expected[] = {
            VXML_INVALID_STRUCTURE,
            VXML_LIMIT_EXCEEDED,
            VXML_UNSUPPORTED_FEATURE
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

    it("admits bounded return/disconnect shapes and rejects unsupported return forms") {
        static const char *const accepted[] = {
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><var name='value' expr='7'/>"
            "<form><block><return namelist='value'/></block></form></vxml>",
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block><return event='child.failed'/>"
            "</block></form></vxml>",
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
            "datamodel='cmeta'><form><block><disconnect/>"
            "</block></form></vxml>"
        };
        static const struct {
            const char *source;
            vxml_status expected;
        } rejected[] = {
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><form><block><return/>"
             "</block></form></vxml>", VXML_INVALID_STRUCTURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><var name='value'/>"
             "<form><block><return event='x' namelist='value'/>"
             "</block></form></vxml>", VXML_INVALID_STRUCTURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><form><block><return eventexpr='x'/>"
             "</block></form></vxml>", VXML_UNSUPPORTED_FEATURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><form><block><return event='x' message='m'/>"
             "</block></form></vxml>", VXML_UNSUPPORTED_FEATURE},
            {"<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
             "datamodel='cmeta'><form><block><disconnect reason='x'/>"
             "</block></form></vxml>", VXML_UNSUPPORTED_FEATURE}
        };
        vxml_cmeta_compile_options_v1 options = compile_options();
        size_t index;
        options.max_event_handlers = 4u;
        options.max_event_name_bytes = 64u;

        for (index = 0u; index < sizeof(accepted) / sizeof(accepted[0]); ++index) {
            vxml_program program = {0};
            check_equal(
                vxml_compile_cmeta(
                    accepted[index], strlen(accepted[index]), NULL,
                    &options, &program, NULL),
                VXML_OK);
            vxml_program_destroy(&program);
        }
        for (index = 0u; index < sizeof(rejected) / sizeof(rejected[0]); ++index) {
            vxml_program program = {0};
            check_equal(
                vxml_compile_cmeta(
                    rejected[index].source, strlen(rejected[index].source),
                    NULL, &options, &program, NULL),
                rejected[index].expected);
            check_null(program.impl);
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
                    index == 7u || index == 8u ||
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
