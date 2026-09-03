#include <ccxml/ccxml.h>

#include "tinytest.h"

#include <stddef.h>
#include <string.h>

enum { TEST_TEXT_CAPACITY = 31u };

typedef struct test_text {
    size_t size;
    char data[TEST_TEXT_CAPACITY + 1u];
} test_text;

Struct(test_conference,
    (test_text, id)
);

Struct(test_state,
    (test_conference, conference),
    (int, count)
);

static void text_move(void *destination, void *source) {
    memcpy(destination, source, sizeof(test_text));
    memset(source, 0, sizeof(test_text));
}

static void text_destroy(void *object) {
    memset(object, 0, sizeof(test_text));
}

static const cmeta_type_traits text_traits = {
    .flags = CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .move_construct = text_move,
    .destroy = text_destroy};

static const cmeta_type_desc text_type = {
    .name = "ccxml_test_text",
    .size = sizeof(test_text),
    .align = _Alignof(test_text),
    .kind = CMETA_T_OBJECT,
    .traits = &text_traits};

static bool text_is_zero(const void *object) {
    return object != NULL && ((const test_text *)object)->size == 0u;
}

static cmeta_status text_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
    test_text *text = (test_text *)object;
    if (text == NULL || (data == NULL && size != 0u))
        return CMETA_INVALID_ARGUMENT;
    if (size > max_bytes || size > TEST_TEXT_CAPACITY)
        return CMETA_CAPACITY_EXCEEDED;
    if (size != 0u) memcpy(text->data, data, size);
    text->data[size] = '\0';
    text->size = size;
    return CMETA_OK;
}

static void text_restore_zero(void *object) {
    if (object != NULL) memset(object, 0, sizeof(test_text));
}

static cmeta_status text_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
    const test_text *text = (const test_text *)object;
    if (text == NULL || out_data == NULL || out_size == NULL ||
        text->size > TEST_TEXT_CAPACITY)
        return CMETA_INVALID_ARGUMENT;
    *out_data = (const unsigned char *)text->data;
    *out_size = text->size;
    return CMETA_OK;
}

static const cmeta_data_buffer_shape text_shape = {
    .ownership = CMETA_DATA_BUFFER_OWNED};

static const cmeta_data_buffer_ops text_ops = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &text_type,
    .ownership = CMETA_DATA_BUFFER_OWNED,
    .is_zero = text_is_zero,
    .assign = text_assign,
    .restore_zero = text_restore_zero,
    .read = text_read};

static const cmeta_data_desc text_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ccxml.text",
    .display_name = "CCXML test text",
    .kind = CMETA_DATA_STRING,
    .storage_type = &text_type,
    .shape = &text_shape,
    .buffer_ops = &text_ops};

static const cmeta_type_traits aggregate_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY};

static const cmeta_type_desc conference_type = {
    .name = "ccxml_test_conference",
    .size = sizeof(test_conference),
    .align = _Alignof(test_conference),
    .kind = CMETA_T_OBJECT,
    .traits = &aggregate_traits};

static const cmeta_data_field_desc conference_fields[] = {
    {"test.ccxml.conference.id", "id",
     offsetof(test_conference, id), &text_desc}};

static const cmeta_data_struct_shape conference_shape = {
    .layout = StructMeta(test_conference),
    .fields = conference_fields,
    .field_count = sizeof(conference_fields) / sizeof(conference_fields[0])};

static const cmeta_data_desc conference_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ccxml.conference",
    .display_name = "CCXML test conference",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &conference_type,
    .shape = &conference_shape};

static const cmeta_type_desc state_type = {
    .name = "ccxml_test_state",
    .size = sizeof(test_state),
    .align = _Alignof(test_state),
    .kind = CMETA_T_OBJECT,
    .traits = &aggregate_traits};

static const cmeta_data_field_desc state_fields[] = {
    {"test.ccxml.state.conference", "conference",
     offsetof(test_state, conference), &conference_desc},
    {"test.ccxml.state.count", "count",
     offsetof(test_state, count), &cmeta_data_int}};

static const cmeta_data_struct_shape state_shape = {
    .layout = StructMeta(test_state),
    .fields = state_fields,
    .field_count = sizeof(state_fields) / sizeof(state_fields[0])};

static const cmeta_data_desc state_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ccxml.state",
    .display_name = "CCXML test state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &state_type,
    .shape = &state_shape};

typedef struct conference_provider_probe {
    bool live;
    size_t commit_count;
    size_t discard_count;
    size_t close_count;
} conference_provider_probe;

static void provider_ticket_commit(void *user) {
    conference_provider_probe *probe = (conference_provider_probe *)user;
    if (probe == NULL || !probe->live) return;
    probe->live = false;
    ++probe->commit_count;
}

static void provider_ticket_discard(void *user) {
    conference_provider_probe *probe = (conference_provider_probe *)user;
    if (probe == NULL || !probe->live) return;
    probe->live = false;
    ++probe->discard_count;
}

static scxml_adapter_status unused_prepare_accept(
    void *user, const ccxml_accept_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    (void)user;
    (void)request;
    (void)out_ticket;
    if (out_error != NULL) *out_error = "accept is not used by this test";
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static scxml_adapter_status provider_prepare_conference(
    void *user, const ccxml_create_conference_request *request,
    ccxml_string_view *out_conference_id,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    conference_provider_probe *probe = (conference_provider_probe *)user;
    (void)request;
    if (out_error != NULL) *out_error = NULL;
    probe->live = true;
    *out_conference_id = (ccxml_string_view){"conf-e2e", 8u};
    *out_ticket = (cflow_statechart_effect_ticket){
        .commit = provider_ticket_commit,
        .discard = provider_ticket_discard,
        .user = probe};
    return SCXML_ADAPTER_ACCEPTED;
}

static void provider_close(void *user) {
    ++((conference_provider_probe *)user)->close_count;
}

static bool provider_quiescent(void *user) {
    (void)user;
    return true;
}

static const ccxml_telephony_adapter_v1 conference_provider = {
    .abi_version = CCXML_TELEPHONY_ADAPTER_ABI_V1,
    .struct_size = sizeof(ccxml_telephony_adapter_v1),
    .prepare_accept = unused_prepare_accept,
    .close = provider_close,
    .is_quiescent = provider_quiescent,
    .prepare_create_conference = provider_prepare_conference};

static ccxml_status initialize(
    ccxml_cmeta_datamodel *datamodel, test_state *state,
    size_t max_string_bytes) {
    const ccxml_cmeta_datamodel_config_v1 config = {
        .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
        .struct_size = sizeof(ccxml_cmeta_datamodel_config_v1),
        .root = &state_desc,
        .state = state,
        .max_path_depth = 4u,
        .max_string_bytes = max_string_bytes};
    return ccxml_cmeta_datamodel_init(datamodel, &config);
}

spec("CCXML CMeta datamodel") {
    it("writes a provider-generated ID through a CCXML session") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='ccxml.loaded'>"
            "<createconference conferenceid='conference.id'/>"
            "</transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        conference_provider_probe provider = {0};
        ccxml_diagnostic diagnostic = {0};
        ccxml_session_config session_config;
        const ccxml_event event = {
            .name = "ccxml.loaded",
            .name_size = sizeof("ccxml.loaded") - 1u};

        check_equal(
            ccxml_compile(
                &program, source, strlen(source), NULL, &diagnostic),
            CCXML_OK);
        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        session_config = (ccxml_session_config){
            .program = &program,
            .telephony = &conference_provider,
            .telephony_user = &provider,
            .datamodel = ccxml_cmeta_datamodel_adapter(),
            .datamodel_user = &datamodel};
        check_equal(
            ccxml_session_init(&session, &session_config), CCXML_OK);
        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_equal(provider.commit_count, (size_t)1);
        check_equal(provider.discard_count, (size_t)0);
        check_equal(state.conference.id.data, "conf-e2e");

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_cmeta_datamodel_destroy(&datamodel);
        ccxml_program_destroy(&program);
    }

    it("stages and commits a nested owned-string write") {
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        cflow_statechart_effect_ticket ticket = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();
        const char *error = NULL;

        memcpy(state.conference.id.data, "old", 3u);
        state.conference.id.data[3] = '\0';
        state.conference.id.size = 3u;
        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        check_equal(
            adapter->validate_string_location(
                &datamodel, "conference.id", 13u, &error),
            SCXML_ADAPTER_ACCEPTED);
        check_equal(
            adapter->prepare_assign_string(
                &datamodel, "conference.id", 13u,
                "conf-42", 7u, &ticket, &error),
            SCXML_ADAPTER_ACCEPTED);
        check_equal(state.conference.id.data, "old");
        ticket.commit(ticket.user);
        check_equal(state.conference.id.data, "conf-42");
        check_equal(state.conference.id.size, (size_t)7);

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("discard leaves the live value unchanged") {
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        cflow_statechart_effect_ticket ticket = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();

        memcpy(state.conference.id.data, "old", 3u);
        state.conference.id.data[3] = '\0';
        state.conference.id.size = 3u;
        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        check_equal(
            adapter->prepare_assign_string(
                &datamodel, "conference.id", 13u,
                "conf-43", 7u, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.discard(ticket.user);
        check_equal(state.conference.id.data, "old");
        check_equal(state.conference.id.size, (size_t)3);

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("rejects unresolved and non-string locations") {
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();

        check_equal(initialize(&datamodel, &state, 16u), CCXML_OK);
        check_equal(
            adapter->validate_string_location(
                &datamodel, "conference.missing", 18u, NULL),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(
            adapter->validate_string_location(
                &datamodel, "count", 5u, NULL),
            SCXML_ADAPTER_ERROR_EXECUTION);

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("enforces the configured write bound during prepare") {
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        cflow_statechart_effect_ticket ticket = {0};
        const ccxml_datamodel_adapter_v1 *adapter =
            ccxml_cmeta_datamodel_adapter();

        check_equal(initialize(&datamodel, &state, 4u), CCXML_OK);
        check_equal(
            adapter->prepare_assign_string(
                &datamodel, "conference.id", 13u,
                "conf-44", 7u, &ticket, NULL),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_null(ticket.commit);
        check_equal(state.conference.id.size, (size_t)0);

        ccxml_cmeta_datamodel_destroy(&datamodel);
    }

    it("rejects invalid owner and config contracts") {
        ccxml_cmeta_datamodel datamodel = {0};
        test_state state = {0};
        ccxml_cmeta_datamodel_config_v1 config = {
            .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
            .struct_size = sizeof(ccxml_cmeta_datamodel_config_v1),
            .root = &state_desc,
            .state = &state,
            .max_path_depth = 4u,
            .max_string_bytes = 16u};

        config.max_path_depth = 0u;
        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &config),
            CCXML_INVALID_ARGUMENT);
        config.max_path_depth = 4u;
        config.abi_version = 0u;
        check_equal(
            ccxml_cmeta_datamodel_init(&datamodel, &config),
            CCXML_INVALID_ARGUMENT);
        check_null(datamodel.impl);
        ccxml_cmeta_datamodel_destroy(&datamodel);
    }
}
