#include <voicexml/subdialog_owner.h>

#include <cmeta/cmeta.h>
#include <tinytest.h>

#include <stddef.h>
#include <string.h>

typedef struct owner_result {
    int code;
    int value;
} owner_result;

typedef struct owner_root {
    int value;
    owner_result child;
    owner_result second;
    owner_result nested;
} owner_root;

static const cmeta_type_identity owner_result_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.voicexml.owner.result");
static const cmeta_type_identity owner_root_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.voicexml.owner.root");
static const cmeta_type_traits owner_trivial_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY
};
static const cmeta_type_desc owner_result_type = {
    .name = "owner_result",
    .size = sizeof(owner_result),
    .align = _Alignof(owner_result),
    .kind = CMETA_T_OBJECT,
    .traits = &owner_trivial_traits,
    .identity = &owner_result_identity
};
static const cmeta_type_desc owner_root_type = {
    .name = "owner_root",
    .size = sizeof(owner_root),
    .align = _Alignof(owner_root),
    .kind = CMETA_T_OBJECT,
    .traits = &owner_trivial_traits,
    .identity = &owner_root_identity
};
static const cmeta_field_desc owner_result_layout_fields[] = {
    {"code", "int", offsetof(owner_result, code),
     sizeof(int), _Alignof(int), &cmeta_type_int, NULL},
    {"value", "int", offsetof(owner_result, value),
     sizeof(int), _Alignof(int), &cmeta_type_int, NULL}
};
static const cmeta_struct_desc owner_result_layout = {
    .name = "owner_result",
    .size = sizeof(owner_result),
    .align = _Alignof(owner_result),
    .fields = owner_result_layout_fields,
    .field_count = 2u
};
static const cmeta_data_field_desc owner_result_fields[] = {
    {"test.voicexml.owner.result.code", "code",
     offsetof(owner_result, code), &cmeta_data_int},
    {"test.voicexml.owner.result.value", "value",
     offsetof(owner_result, value), &cmeta_data_int}
};
static const cmeta_data_struct_shape owner_result_shape = {
    .layout = &owner_result_layout,
    .fields = owner_result_fields,
    .field_count = 2u
};
static const cmeta_data_desc owner_result_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.voicexml.owner.result.data",
    .display_name = "owner result",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &owner_result_type,
    .shape = &owner_result_shape
};
static const cmeta_field_desc owner_root_layout_fields[] = {
    {"value", "int", offsetof(owner_root, value),
     sizeof(int), _Alignof(int), &cmeta_type_int, NULL},
    {"child", "owner_result", offsetof(owner_root, child),
     sizeof(owner_result), _Alignof(owner_result),
     &owner_result_type, NULL},
    {"second", "owner_result", offsetof(owner_root, second),
     sizeof(owner_result), _Alignof(owner_result),
     &owner_result_type, NULL},
    {"nested", "owner_result", offsetof(owner_root, nested),
     sizeof(owner_result), _Alignof(owner_result),
     &owner_result_type, NULL}
};
static const cmeta_struct_desc owner_root_layout = {
    .name = "owner_root",
    .size = sizeof(owner_root),
    .align = _Alignof(owner_root),
    .fields = owner_root_layout_fields,
    .field_count = 4u
};
static const cmeta_data_field_desc owner_root_fields[] = {
    {"test.voicexml.owner.root.value", "value",
     offsetof(owner_root, value), &cmeta_data_int},
    {"test.voicexml.owner.root.child", "child",
     offsetof(owner_root, child), &owner_result_data},
    {"test.voicexml.owner.root.second", "second",
     offsetof(owner_root, second), &owner_result_data},
    {"test.voicexml.owner.root.nested", "nested",
     offsetof(owner_root, nested), &owner_result_data}
};
static const cmeta_data_struct_shape owner_root_shape = {
    .layout = &owner_root_layout,
    .fields = owner_root_fields,
    .field_count = 4u
};
static const cmeta_data_desc owner_root_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.voicexml.owner.root.data",
    .display_name = "owner root",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &owner_root_type,
    .shape = &owner_root_shape
};

typedef struct owner_document {
    const char *uri;
    const char *source;
} owner_document;

typedef struct owner_provider {
    const owner_document *documents;
    size_t document_count;
    size_t open_calls;
    size_t close_calls;
    char opened[16][256];
    size_t opened_count;
} owner_provider;

static vxml_dialog_manager_status owner_document_open(
    void *user,
    const char *source, size_t source_size,
    const char *media_type, size_t media_type_size,
    size_t max_bytes,
    vxml_dialog_document *out_document) {
    owner_provider *provider = (owner_provider *)user;
    size_t index;
    static const char expected_media[] =
        "application/voicexml+xml";
    if (provider == NULL || source == NULL ||
        source_size == 0u || out_document == NULL ||
        media_type == NULL ||
        media_type_size != sizeof(expected_media) - 1u ||
        memcmp(
            media_type, expected_media,
            sizeof(expected_media) - 1u) != 0)
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;
    ++provider->open_calls;
    if (provider->opened_count <
        sizeof(provider->opened) / sizeof(provider->opened[0]) &&
        source_size < sizeof(provider->opened[0])) {
        memcpy(
            provider->opened[provider->opened_count],
            source, source_size);
        provider->opened[provider->opened_count][source_size] = '\0';
        ++provider->opened_count;
    }
    for (index = 0u; index < provider->document_count; ++index) {
        const owner_document *document =
            &provider->documents[index];
        const size_t uri_size = strlen(document->uri);
        const size_t body_size = strlen(document->source);
        if (uri_size != source_size ||
            memcmp(document->uri, source, source_size) != 0)
            continue;
        if (body_size > max_bytes)
            return VXML_DIALOG_MANAGER_DOCUMENT_ERROR;
        *out_document = (vxml_dialog_document){
            .data = document->source,
            .size = body_size,
            .lease = provider};
        return VXML_DIALOG_MANAGER_OK;
    }
    return VXML_DIALOG_MANAGER_DOCUMENT_ERROR;
}

static void owner_document_close(
    void *user, vxml_dialog_document *document) {
    owner_provider *provider = (owner_provider *)user;
    if (provider != NULL && document != NULL &&
        document->lease == provider)
        ++provider->close_calls;
    if (document != NULL)
        *document = (vxml_dialog_document){0};
}

static const vxml_dialog_document_adapter_v1 owner_document_adapter = {
    .abi_version = VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1,
    .struct_size = sizeof(vxml_dialog_document_adapter_v1),
    .open = owner_document_open,
    .close = owner_document_close
};

typedef struct owner_compiler {
    vxml_cmeta_compile_options_v1 options;
    size_t calls;
} owner_compiler;

static vxml_status owner_compile(
    void *user,
    const void *source, size_t source_size,
    vxml_program *out_program,
    vxml_diagnostic *diagnostic) {
    owner_compiler *compiler = (owner_compiler *)user;
    if (compiler == NULL)
        return VXML_INVALID_ARGUMENT;
    ++compiler->calls;
    return vxml_compile_cmeta(
        source, source_size, NULL,
        &compiler->options, out_program, diagnostic);
}

static const vxml_document_compile_adapter_v1 owner_compile_adapter = {
    .abi_version = VXML_DOCUMENT_COMPILE_ADAPTER_ABI_V1,
    .struct_size = sizeof(vxml_document_compile_adapter_v1),
    .compile = owner_compile
};

static vxml_cmeta_compile_options_v1 owner_compile_options(void) {
    vxml_cmeta_compile_options_v1 options = {
        .abi_version = VXML_CMETA_COMPILE_OPTIONS_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_compile_options_v1),
        .root = &owner_root_data,
        .max_expression_bytes = 2048u,
        .max_expression_instructions = 512u,
        .max_expression_operands = 64u,
        .max_expression_depth = 24u,
        .max_path_depth = 8u,
        .max_literal_bytes = 1024u,
        .max_string_bytes = 1024u,
        .max_scope_slots = 64u,
        .max_scope_storage_bytes = 8192u,
        .max_conditional_depth = 16u,
        .max_event_handlers = 16u,
        .max_event_name_bytes = 128u,
        .max_subdialogs = 8u,
        .max_subdialog_uri_bytes = 512u,
        .max_subdialog_params = 8u,
        .max_subdialog_param_name_bytes = 64u,
        .max_subdialog_param_value_bytes = 256u
    };
    return options;
}

static vxml_cmeta_session_options_v1 owner_session_options(
    const owner_root *root) {
    static const vxml_cmeta_name_view undefined[] = {
        {"child", sizeof("child") - 1u},
        {"second", sizeof("second") - 1u},
        {"nested", sizeof("nested") - 1u}
    };
    return (vxml_cmeta_session_options_v1){
        .abi_version = VXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_session_options_v1),
        .initial_root = root,
        .initially_undefined = undefined,
        .initially_undefined_count =
            sizeof(undefined) / sizeof(undefined[0]),
        .max_transaction_bytes = 32768u,
        .max_execution_steps = 256u,
        .max_event_counters = 32u,
        .max_event_name_bytes = 128u,
        .max_event_dispatch_depth = 16u,
        .max_subdialog_snapshot_bytes = 4096u,
        .max_subdialog_completion_entries = 16u,
        .max_subdialog_completion_bytes = 4096u
    };
}

typedef struct owner_fixture {
    owner_provider provider;
    owner_compiler compiler;
    owner_root initial_root;
    vxml_document_store store;
    vxml_cmeta_subdialog_owner owner;
    vxml_document_ref root_ref;
    bool root_ref_live;
    vxml_document_view root_view;
    vxml_session root_session;
} owner_fixture;

static bool owner_fixture_init(
    owner_fixture *fixture,
    const owner_document *documents,
    size_t document_count,
    size_t max_navigation_hops,
    size_t max_nesting_depth) {
    vxml_document_store_config_v1 store_config = {0};
    vxml_cmeta_session_options_v1 child_options;
    vxml_cmeta_session_options_v1 root_options;
    vxml_cmeta_subdialog_owner_config_v1 owner_config =
        vxml_cmeta_subdialog_owner_default_config_v1();
    vxml_document_store_error error = {0};

    memset(fixture, 0, sizeof(*fixture));
    fixture->initial_root.value = 7;
    fixture->provider.documents = documents;
    fixture->provider.document_count = document_count;
    fixture->compiler.options = owner_compile_options();

    store_config.abi_version =
        VXML_DOCUMENT_STORE_CONFIG_ABI_V1;
    store_config.struct_size = sizeof(store_config);
    store_config.application_uri =
        "https://voice.example/root.vxml";
    store_config.application_uri_size =
        sizeof("https://voice.example/root.vxml") - 1u;
    store_config.capacity = 8u;
    store_config.max_uri_bytes = 511u;
    store_config.max_document_bytes = 16384u;
    store_config.max_cache_bytes = 131072u;
    store_config.voice_limits = vxml_default_limits();
    store_config.documents = &owner_document_adapter;
    store_config.document_user = &fixture->provider;
    store_config.compiler = &owner_compile_adapter;
    store_config.compiler_user = &fixture->compiler;
    if (vxml_document_store_init(
            &fixture->store, &store_config) !=
        VXML_DOCUMENT_STORE_OK)
        return false;

    child_options =
        owner_session_options(&fixture->initial_root);
    owner_config.capacity = 8u;
    owner_config.max_uri_bytes = 511u;
    owner_config.max_params = 8u;
    owner_config.max_param_bytes = 4096u;
    owner_config.max_completion_entries = 16u;
    owner_config.max_navigation_hops =
        max_navigation_hops;
    owner_config.max_nesting_depth =
        max_nesting_depth;
    owner_config.document_store = &fixture->store;
    owner_config.child_session_options = &child_options;
    if (vxml_cmeta_subdialog_owner_init(
            &fixture->owner, &owner_config) != VXML_OK)
        return false;

    if (vxml_document_store_acquire(
            &fixture->store,
            "https://voice.example/root.vxml",
            sizeof("https://voice.example/root.vxml") - 1u,
            &fixture->root_ref, &error) !=
        VXML_DOCUMENT_STORE_OK)
        return false;
    fixture->root_ref_live = true;
    if (vxml_document_store_view(
            &fixture->store,
            fixture->root_ref,
            &fixture->root_view) !=
        VXML_DOCUMENT_STORE_OK ||
        fixture->root_view.program == NULL)
        return false;

    root_options =
        owner_session_options(&fixture->initial_root);
    root_options.subdialog =
        vxml_cmeta_subdialog_owner_adapter();
    root_options.subdialog_user =
        vxml_cmeta_subdialog_owner_root_user(
            &fixture->owner);
    if (vxml_session_init_cmeta(
            &fixture->root_session,
            fixture->root_view.program,
            &root_options) != VXML_OK)
        return false;
    if (vxml_cmeta_subdialog_owner_bind_root(
            &fixture->owner,
            &fixture->root_session,
            fixture->root_view.document_uri,
            fixture->root_view.document_uri_size) != VXML_OK)
        return false;
    return vxml_session_start(
        &fixture->root_session) == VXML_OK;
}

static void owner_fixture_destroy(
    owner_fixture *fixture) {
    size_t processed = 0u;
    size_t guard = 0u;
    if (fixture == NULL) return;
    (void)vxml_session_close(&fixture->root_session);
    vxml_cmeta_subdialog_owner_close(&fixture->owner);
    while (!vxml_cmeta_subdialog_owner_is_quiescent(
               &fixture->owner) &&
           guard++ < 64u)
        (void)vxml_cmeta_subdialog_owner_run_ready(
            &fixture->owner, 64u, &processed);
    (void)vxml_cmeta_subdialog_owner_destroy(
        &fixture->owner);
    vxml_session_destroy(&fixture->root_session);
    if (fixture->root_ref_live) {
        (void)vxml_document_store_release(
            &fixture->store, &fixture->root_ref);
        fixture->root_ref_live = false;
    }
    (void)vxml_document_store_destroy(
        &fixture->store);
}

static vxml_status owner_drive(
    owner_fixture *fixture,
    size_t max_turns) {
    size_t turn;
    for (turn = 0u; turn < max_turns; ++turn) {
        size_t processed = 0u;
        vxml_status status =
            vxml_cmeta_subdialog_owner_run_ready(
                &fixture->owner, 64u, &processed);
        if (status != VXML_OK)
            return status;
        if (vxml_session_get_state(
                &fixture->root_session) !=
            VXML_SESSION_RUNNING)
            return VXML_OK;
        if (processed == 0u)
            return VXML_INVALID_STATE;
    }
    return VXML_LIMIT_EXCEEDED;
}

static int read_root_int(
    vxml_session *session,
    const char *name) {
    vxml_cmeta_value_view value = {0};
    check_equal(
        vxml_session_cmeta_read(
            session, name, strlen(name), &value),
        VXML_OK);
    check_equal(value.kind, VXML_CMETA_VALUE_SINT);
    return (int)value.data.sint;
}

spec("VoiceXML DocumentStore subdialog owner") {
    it("reuses the current cached Program for local fragment children without provider fetch") {
        static const owner_document documents[] = {
            {
                "https://voice.example/root.vxml",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
                "<form id='parent'><subdialog name='child' src='#child'>"
                "<param name='value' expr='value'/>"
                "<filled><assign name='value' expr='child.value'/></filled>"
                "</subdialog></form>"
                "<form id='child'><var name='value'/><block>"
                "<return namelist='value'/></block></form></vxml>"
            }
        };
        owner_fixture fixture;
        vxml_document_store_stats stats = {0};

        check_true(owner_fixture_init(
            &fixture, documents, 1u, 8u, 4u));
        check_equal(fixture.provider.open_calls, (size_t)1u);
        check_equal(owner_drive(&fixture, 16u), VXML_OK);
        check_equal(
            vxml_session_get_state(&fixture.root_session),
            VXML_SESSION_EXITED);
        check_equal(read_root_int(
            &fixture.root_session, "value"), 7);
        check_equal(fixture.provider.open_calls, (size_t)1u);
        check_equal(fixture.compiler.calls, (size_t)1u);
        check_true(vxml_document_store_get_stats(
            &fixture.store, &stats));
        check_true(stats.hits >= UINT64_C(1));
        check_equal(stats.active_borrows, (size_t)1u);

        owner_fixture_destroy(&fixture);
        check_equal(
            fixture.provider.open_calls,
            fixture.provider.close_calls);
    }

    it("keeps child navigation and document identity local to each child") {
        static const owner_document documents[] = {
            {
                "https://voice.example/root.vxml",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
                "<form id='parent'>"
                "<subdialog name='child' src='child.vxml#entry'>"
                "<filled><assign name='value' expr='child.value'/></filled>"
                "</subdialog>"
                "<subdialog name='second' src='sibling.vxml#entry'>"
                "<filled><assign name='value' expr='child.value + second.value'/></filled>"
                "</subdialog></form></vxml>"
            },
            {
                "https://voice.example/child.vxml",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
                "<form id='entry'><block><assign name='value' expr='91'/>"
                "<goto next='nested/next.vxml#done'/></block></form></vxml>"
            },
            {
                "https://voice.example/nested/next.vxml",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
                "<form id='done'><block><return namelist='value'/></block></form></vxml>"
            },
            {
                "https://voice.example/sibling.vxml",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
                "<form id='entry'><block><return namelist='value'/></block></form></vxml>"
            }
        };
        owner_fixture fixture;

        check_true(owner_fixture_init(
            &fixture, documents,
            sizeof(documents) / sizeof(documents[0]),
            8u, 4u));
        check_equal(owner_drive(&fixture, 32u), VXML_OK);
        check_equal(
            vxml_session_get_state(&fixture.root_session),
            VXML_SESSION_EXITED);
        check_equal(read_root_int(
            &fixture.root_session, "value"), 14);
        check_equal(fixture.provider.opened_count, (size_t)4u);
        check_equal(
            fixture.provider.opened[0],
            "https://voice.example/root.vxml");
        check_equal(
            fixture.provider.opened[1],
            "https://voice.example/child.vxml");
        check_equal(
            fixture.provider.opened[2],
            "https://voice.example/nested/next.vxml");
        check_equal(
            fixture.provider.opened[3],
            "https://voice.example/sibling.vxml");

        owner_fixture_destroy(&fixture);
        check_equal(
            fixture.provider.open_calls,
            fixture.provider.close_calls);
    }

    it("routes RETURN_EVENT through the parent subdialog scope") {
        static const owner_document documents[] = {
            {
                "https://voice.example/root.vxml",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
                "<form><subdialog name='child' src='event.vxml#entry'>"
                "<catch event='child.fail'>"
                "<assign name='value' expr='value + 1'/>"
                "<exit expr='value'/></catch>"
                "</subdialog></form></vxml>"
            },
            {
                "https://voice.example/event.vxml",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
                "<form id='entry'><block><return event='child.fail'/></block></form></vxml>"
            }
        };
        owner_fixture fixture;
        vxml_cmeta_exit_kind exit_kind =
            VXML_CMETA_EXIT_EMPTY;
        vxml_cmeta_name_view name = {0};
        vxml_cmeta_value_view value = {0};

        check_true(owner_fixture_init(
            &fixture, documents, 2u, 8u, 4u));
        check_equal(owner_drive(&fixture, 16u), VXML_OK);
        check_equal(
            vxml_session_get_state(&fixture.root_session),
            VXML_SESSION_EXITED);
        check_equal(
            read_root_int(&fixture.root_session, "value"), 8);
        check_equal(
            vxml_session_cmeta_exit_kind(
                &fixture.root_session, &exit_kind),
            VXML_OK);
        check_equal(exit_kind, VXML_CMETA_EXIT_EXPRESSION);
        check_equal(
            vxml_session_cmeta_exit_count(
                &fixture.root_session),
            (size_t)1u);
        check_equal(
            vxml_session_cmeta_exit_at(
                &fixture.root_session, 0u, &name, &value),
            VXML_OK);
        check_null(name.data);
        check_equal(name.size, (size_t)0u);
        check_equal(value.kind, VXML_CMETA_VALUE_SINT);
        check_equal(value.data.sint, INT64_C(8));

        owner_fixture_destroy(&fixture);
    }

    it("propagates child global exit without converting it into result data") {
        static const owner_document documents[] = {
            {
                "https://voice.example/root.vxml",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
                "<form><subdialog name='child' src='exit.vxml#entry'/></form></vxml>"
            },
            {
                "https://voice.example/exit.vxml",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
                "<form id='entry'><block><exit namelist='value'/></block></form></vxml>"
            }
        };
        owner_fixture fixture;
        vxml_cmeta_terminal_kind terminal =
            VXML_CMETA_TERMINAL_NONE;
        vxml_cmeta_exit_kind exit_kind =
            VXML_CMETA_EXIT_EMPTY;

        check_true(owner_fixture_init(
            &fixture, documents, 2u, 8u, 4u));
        check_equal(owner_drive(&fixture, 16u), VXML_OK);
        check_equal(
            vxml_session_get_state(&fixture.root_session),
            VXML_SESSION_EXITED);
        check_equal(
            vxml_session_cmeta_terminal_kind(
                &fixture.root_session, &terminal),
            VXML_OK);
        check_equal(terminal, VXML_CMETA_TERMINAL_EXIT);
        check_equal(
            vxml_session_cmeta_exit_kind(
                &fixture.root_session, &exit_kind),
            VXML_OK);
        check_equal(exit_kind, VXML_CMETA_EXIT_NAMELIST);
        check_equal(
            vxml_session_cmeta_exit_count(
                &fixture.root_session),
            (size_t)1u);

        owner_fixture_destroy(&fixture);
    }

    it("bounds child navigation hops and releases the live borrow on close") {
        static const owner_document documents[] = {
            {
                "https://voice.example/root.vxml",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
                "<form><subdialog name='child' src='a.vxml#entry'/></form></vxml>"
            },
            {
                "https://voice.example/a.vxml",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
                "<form id='entry'><block><goto next='b.vxml#entry'/></block></form></vxml>"
            },
            {
                "https://voice.example/b.vxml",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
                "<form id='entry'><block><goto next='c.vxml#entry'/></block></form></vxml>"
            },
            {
                "https://voice.example/c.vxml",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
                "<form id='entry'><block><return namelist='value'/></block></form></vxml>"
            }
        };
        owner_fixture fixture;
        vxml_document_store_stats before = {0};
        vxml_document_store_stats after = {0};
        size_t processed = 0u;
        vxml_status status = VXML_OK;
        size_t turn;

        check_true(owner_fixture_init(
            &fixture, documents, 4u, 1u, 4u));
        for (turn = 0u; turn < 8u && status == VXML_OK; ++turn)
            status = vxml_cmeta_subdialog_owner_run_ready(
                &fixture.owner, 64u, &processed);
        check_equal(status, VXML_LIMIT_EXCEEDED);
        check_true(vxml_document_store_get_stats(
            &fixture.store, &before));
        check_true(before.active_borrows >= (size_t)2u);

        check_equal(
            vxml_session_close(&fixture.root_session),
            VXML_OK);
        vxml_cmeta_subdialog_owner_close(&fixture.owner);
        for (turn = 0u;
             turn < 16u &&
             !vxml_cmeta_subdialog_owner_is_quiescent(
                 &fixture.owner);
             ++turn)
            check_equal(
                vxml_cmeta_subdialog_owner_run_ready(
                    &fixture.owner, 64u, &processed),
                VXML_OK);
        check_true(vxml_cmeta_subdialog_owner_is_quiescent(
            &fixture.owner));
        check_true(vxml_document_store_get_stats(
            &fixture.store, &after));
        check_equal(after.active_borrows, (size_t)1u);

        owner_fixture_destroy(&fixture);
    }

    it("bounds nested child depth and quiesces canceled descendants") {
        static const owner_document documents[] = {
            {
                "https://voice.example/root.vxml",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
                "<form><subdialog name='child' src='nested.vxml#entry'/></form></vxml>"
            },
            {
                "https://voice.example/nested.vxml",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' datamodel='cmeta'>"
                "<form id='entry'><subdialog name='nested' src='#leaf'/></form>"
                "<form id='leaf'><block><return namelist='value'/></block></form>"
                "</vxml>"
            }
        };
        owner_fixture fixture;
        size_t processed = 0u;
        vxml_status status = VXML_OK;
        size_t turn;

        check_true(owner_fixture_init(
            &fixture, documents, 2u, 8u, 1u));
        for (turn = 0u; turn < 8u && status == VXML_OK; ++turn)
            status = vxml_cmeta_subdialog_owner_run_ready(
                &fixture.owner, 64u, &processed);
        check_equal(status, VXML_LIMIT_EXCEEDED);

        check_equal(
            vxml_session_close(&fixture.root_session),
            VXML_OK);
        vxml_cmeta_subdialog_owner_close(&fixture.owner);
        for (turn = 0u;
             turn < 16u &&
             !vxml_cmeta_subdialog_owner_is_quiescent(
                 &fixture.owner);
             ++turn)
            check_equal(
                vxml_cmeta_subdialog_owner_run_ready(
                    &fixture.owner, 64u, &processed),
                VXML_OK);
        check_true(vxml_cmeta_subdialog_owner_is_quiescent(
            &fixture.owner));

        owner_fixture_destroy(&fixture);
    }
}
