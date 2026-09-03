#include <ccxml/ccxml.h>

#include "tinytest.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct provider_probe provider_probe;

static size_t transaction_commit_sequence;

typedef struct provider_ticket {
    provider_probe *owner;
    size_t ordinal;
    bool live;
} provider_ticket;

struct provider_probe {
    provider_ticket tickets[8];
    size_t prepare_count;
    size_t commit_count;
    size_t discard_count;
    size_t commit_order[8];
    size_t discard_order[8];
    size_t prepare_kinds[8];
    size_t reject_on_prepare;
    bool malformed_ticket;
    bool quiescent;
    size_t close_count;
    char connection_id[64];
    size_t connection_id_size;
    char destination[64];
    size_t destination_size;
    char disconnected_connection_id[64];
    size_t disconnected_connection_id_size;
    char rejected_connection_id[64];
    size_t rejected_connection_id_size;
    char redirected_connection_id[64];
    size_t redirected_connection_id_size;
    char redirected_destination[64];
    size_t redirected_destination_size;
    char joined_id1[64];
    size_t joined_id1_size;
    char joined_id2[64];
    size_t joined_id2_size;
    char unjoined_id1[64];
    size_t unjoined_id1_size;
    char unjoined_id2[64];
    size_t unjoined_id2_size;
    char merged_connection_id1[64];
    size_t merged_connection_id1_size;
    char merged_connection_id2[64];
    size_t merged_connection_id2_size;
    char conference_name[64];
    size_t conference_name_size;
    const char *conference_id_result;
    size_t conference_id_result_size;
    size_t create_conference_commit_sequence;
    char destroyed_conference_id[64];
    size_t destroyed_conference_id_size;
    char dialog_source[128];
    size_t dialog_source_size;
    char dialog_media_type[64];
    size_t dialog_media_type_size;
    char dialog_connection_id[64];
    size_t dialog_connection_id_size;
    const char *dialog_id_result;
    size_t dialog_id_result_size;
    size_t dialog_prepare_commit_sequence;
    size_t dialog_start_commit_sequence;
    char started_prepared_dialog_id[64];
    size_t started_prepared_dialog_id_size;
    char prepared_dialog_connection_id[64];
    size_t prepared_dialog_connection_id_size;
    size_t prepared_dialog_start_commit_sequence;
    char terminated_dialog_id[64];
    size_t terminated_dialog_id_size;
    bool dialog_terminate_immediate;
};

enum {
    PROVIDER_ACCEPT = 1,
    PROVIDER_CREATE_CALL,
    PROVIDER_DISCONNECT,
    PROVIDER_REJECT,
    PROVIDER_REDIRECT,
    PROVIDER_JOIN,
    PROVIDER_UNJOIN,
    PROVIDER_MERGE,
    PROVIDER_CREATE_CONFERENCE,
    PROVIDER_DESTROY_CONFERENCE,
    PROVIDER_DIALOG_START,
    PROVIDER_DIALOG_TERMINATE,
    PROVIDER_DIALOG_PREPARE,
    PROVIDER_PREPARED_DIALOG_START
};

static void ticket_commit(void *user) {
    provider_ticket *ticket = (provider_ticket *)user;
    if (ticket == NULL || !ticket->live) return;
    ticket->live = false;
    ticket->owner->commit_order[ticket->owner->commit_count] =
        ticket->ordinal;
    if (ticket->owner->prepare_kinds[ticket->ordinal - 1u] ==
        PROVIDER_CREATE_CONFERENCE) {
        ticket->owner->create_conference_commit_sequence =
            ++transaction_commit_sequence;
    }
    if (ticket->owner->prepare_kinds[ticket->ordinal - 1u] ==
        PROVIDER_DIALOG_START) {
        ticket->owner->dialog_start_commit_sequence =
            ++transaction_commit_sequence;
    }
    if (ticket->owner->prepare_kinds[ticket->ordinal - 1u] ==
        PROVIDER_DIALOG_PREPARE) {
        ticket->owner->dialog_prepare_commit_sequence =
            ++transaction_commit_sequence;
    }
    if (ticket->owner->prepare_kinds[ticket->ordinal - 1u] ==
        PROVIDER_PREPARED_DIALOG_START) {
        ticket->owner->prepared_dialog_start_commit_sequence =
            ++transaction_commit_sequence;
    }
    ++ticket->owner->commit_count;
}

static void ticket_discard(void *user) {
    provider_ticket *ticket = (provider_ticket *)user;
    if (ticket == NULL || !ticket->live) return;
    ticket->live = false;
    ticket->owner->discard_order[ticket->owner->discard_count] =
        ticket->ordinal;
    ++ticket->owner->discard_count;
}

static scxml_adapter_status prepare_effect(
    provider_probe *probe, size_t kind,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    provider_ticket *ticket;
    const size_t index = probe->prepare_count++;
    probe->prepare_kinds[index] = kind;
    if (out_error != NULL) *out_error = NULL;
    if (probe->reject_on_prepare != 0u &&
        probe->prepare_count == probe->reject_on_prepare) {
        if (out_error != NULL) *out_error = "rejected by test provider";
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    if (probe->malformed_ticket) {
        *out_ticket = (cflow_statechart_effect_ticket){0};
        return SCXML_ADAPTER_ACCEPTED;
    }
    ticket = &probe->tickets[index];
    ticket->owner = probe;
    ticket->ordinal = index + 1u;
    ticket->live = true;
    *out_ticket = (cflow_statechart_effect_ticket){
        .commit = ticket_commit,
        .discard = ticket_discard,
        .user = ticket};
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status prepare_accept(
    void *user, const ccxml_accept_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    probe->connection_id_size = request->connection_id_size;
    memcpy(
        probe->connection_id, request->connection_id,
        request->connection_id_size);
    probe->connection_id[request->connection_id_size] = '\0';
    return prepare_effect(
        probe, PROVIDER_ACCEPT, out_ticket, out_error);
}

static scxml_adapter_status prepare_create_call(
    void *user, const ccxml_create_call_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    probe->destination_size = request->destination_size;
    memcpy(
        probe->destination, request->destination,
        request->destination_size);
    probe->destination[request->destination_size] = '\0';
    return prepare_effect(
        probe, PROVIDER_CREATE_CALL, out_ticket, out_error);
}

static scxml_adapter_status prepare_disconnect(
    void *user, const ccxml_disconnect_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    probe->disconnected_connection_id_size = request->connection_id_size;
    memcpy(
        probe->disconnected_connection_id, request->connection_id,
        request->connection_id_size);
    probe->disconnected_connection_id[request->connection_id_size] = '\0';
    return prepare_effect(
        probe, PROVIDER_DISCONNECT, out_ticket, out_error);
}

static scxml_adapter_status prepare_reject(
    void *user, const ccxml_reject_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    probe->rejected_connection_id_size = request->connection_id_size;
    memcpy(
        probe->rejected_connection_id, request->connection_id,
        request->connection_id_size);
    probe->rejected_connection_id[request->connection_id_size] = '\0';
    return prepare_effect(probe, PROVIDER_REJECT, out_ticket, out_error);
}

static scxml_adapter_status prepare_redirect(
    void *user, const ccxml_redirect_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    probe->redirected_connection_id_size = request->connection_id_size;
    memcpy(
        probe->redirected_connection_id, request->connection_id,
        request->connection_id_size);
    probe->redirected_connection_id[request->connection_id_size] = '\0';
    probe->redirected_destination_size = request->destination_size;
    memcpy(
        probe->redirected_destination, request->destination,
        request->destination_size);
    probe->redirected_destination[request->destination_size] = '\0';
    return prepare_effect(probe, PROVIDER_REDIRECT, out_ticket, out_error);
}

static scxml_adapter_status prepare_join(
    void *user, const ccxml_join_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    probe->joined_id1_size = request->id1_size;
    memcpy(probe->joined_id1, request->id1, request->id1_size);
    probe->joined_id1[request->id1_size] = '\0';
    probe->joined_id2_size = request->id2_size;
    memcpy(probe->joined_id2, request->id2, request->id2_size);
    probe->joined_id2[request->id2_size] = '\0';
    return prepare_effect(probe, PROVIDER_JOIN, out_ticket, out_error);
}

static scxml_adapter_status prepare_unjoin(
    void *user, const ccxml_unjoin_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    probe->unjoined_id1_size = request->id1_size;
    memcpy(probe->unjoined_id1, request->id1, request->id1_size);
    probe->unjoined_id1[request->id1_size] = '\0';
    probe->unjoined_id2_size = request->id2_size;
    memcpy(probe->unjoined_id2, request->id2, request->id2_size);
    probe->unjoined_id2[request->id2_size] = '\0';
    return prepare_effect(probe, PROVIDER_UNJOIN, out_ticket, out_error);
}

static scxml_adapter_status prepare_merge(
    void *user, const ccxml_merge_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    probe->merged_connection_id1_size = request->connection_id1_size;
    memcpy(
        probe->merged_connection_id1, request->connection_id1,
        request->connection_id1_size);
    probe->merged_connection_id1[request->connection_id1_size] = '\0';
    probe->merged_connection_id2_size = request->connection_id2_size;
    memcpy(
        probe->merged_connection_id2, request->connection_id2,
        request->connection_id2_size);
    probe->merged_connection_id2[request->connection_id2_size] = '\0';
    return prepare_effect(probe, PROVIDER_MERGE, out_ticket, out_error);
}

static scxml_adapter_status prepare_create_conference(
    void *user, const ccxml_create_conference_request *request,
    ccxml_string_view *out_conference_id,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    probe->conference_name_size = request->conference_name_size;
    if (request->conference_name_size != 0u) {
        memcpy(
            probe->conference_name, request->conference_name,
            request->conference_name_size);
    }
    probe->conference_name[request->conference_name_size] = '\0';
    *out_conference_id = (ccxml_string_view){
        .data = probe->conference_id_result,
        .size = probe->conference_id_result_size};
    return prepare_effect(
        probe, PROVIDER_CREATE_CONFERENCE, out_ticket, out_error);
}

static scxml_adapter_status prepare_destroy_conference(
    void *user, const ccxml_destroy_conference_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    probe->destroyed_conference_id_size = request->conference_id_size;
    memcpy(
        probe->destroyed_conference_id, request->conference_id,
        request->conference_id_size);
    probe->destroyed_conference_id[request->conference_id_size] = '\0';
    return prepare_effect(
        probe, PROVIDER_DESTROY_CONFERENCE, out_ticket, out_error);
}

static scxml_adapter_status prepare_dialog_start(
    void *user, const ccxml_dialog_start_request *request,
    ccxml_string_view *out_dialog_id,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    probe->dialog_source_size = request->source_size;
    memcpy(
        probe->dialog_source, request->source,
        request->source_size);
    probe->dialog_source[request->source_size] = '\0';
    probe->dialog_media_type_size = request->media_type_size;
    memcpy(
        probe->dialog_media_type, request->media_type,
        request->media_type_size);
    probe->dialog_media_type[request->media_type_size] = '\0';
    probe->dialog_connection_id_size = request->connection_id_size;
    memcpy(
        probe->dialog_connection_id, request->connection_id,
        request->connection_id_size);
    probe->dialog_connection_id[request->connection_id_size] = '\0';
    *out_dialog_id = (ccxml_string_view){
        .data = probe->dialog_id_result,
        .size = probe->dialog_id_result_size};
    return prepare_effect(
        probe, PROVIDER_DIALOG_START, out_ticket, out_error);
}

static scxml_adapter_status prepare_dialog_prepare(
    void *user, const ccxml_dialog_prepare_request *request,
    ccxml_string_view *out_dialog_id,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    probe->dialog_source_size = request->source_size;
    memcpy(
        probe->dialog_source, request->source,
        request->source_size);
    probe->dialog_source[request->source_size] = '\0';
    probe->dialog_media_type_size = request->media_type_size;
    memcpy(
        probe->dialog_media_type, request->media_type,
        request->media_type_size);
    probe->dialog_media_type[request->media_type_size] = '\0';
    *out_dialog_id = (ccxml_string_view){
        .data = probe->dialog_id_result,
        .size = probe->dialog_id_result_size};
    return prepare_effect(
        probe, PROVIDER_DIALOG_PREPARE, out_ticket, out_error);
}

static scxml_adapter_status prepare_dialog_terminate(
    void *user, const ccxml_dialog_terminate_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    probe->terminated_dialog_id_size = request->dialog_id_size;
    memcpy(
        probe->terminated_dialog_id, request->dialog_id,
        request->dialog_id_size);
    probe->terminated_dialog_id[request->dialog_id_size] = '\0';
    probe->dialog_terminate_immediate = request->immediate;
    return prepare_effect(
        probe, PROVIDER_DIALOG_TERMINATE, out_ticket, out_error);
}

static scxml_adapter_status prepare_prepared_dialog_start(
    void *user, const ccxml_prepared_dialog_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    provider_probe *probe = (provider_probe *)user;
    probe->started_prepared_dialog_id_size = request->dialog_id_size;
    memcpy(
        probe->started_prepared_dialog_id, request->dialog_id,
        request->dialog_id_size);
    probe->started_prepared_dialog_id[request->dialog_id_size] = '\0';
    probe->prepared_dialog_connection_id_size =
        request->connection_id_size;
    memcpy(
        probe->prepared_dialog_connection_id, request->connection_id,
        request->connection_id_size);
    probe->prepared_dialog_connection_id[request->connection_id_size] = '\0';
    return prepare_effect(
        probe, PROVIDER_PREPARED_DIALOG_START, out_ticket, out_error);
}

static void provider_close(void *user) {
    provider_probe *probe = (provider_probe *)user;
    ++probe->close_count;
}

static bool provider_is_quiescent(void *user) {
    const provider_probe *probe = (const provider_probe *)user;
    return probe->quiescent;
}

static const ccxml_telephony_adapter_v1 provider_adapter = {
    .abi_version = CCXML_TELEPHONY_ADAPTER_ABI_V1,
    .struct_size = sizeof(ccxml_telephony_adapter_v1),
    .prepare_accept = prepare_accept,
    .close = provider_close,
    .is_quiescent = provider_is_quiescent,
    .prepare_create_call = prepare_create_call,
    .prepare_disconnect = prepare_disconnect,
    .prepare_reject = prepare_reject,
    .prepare_redirect = prepare_redirect,
    .prepare_join = prepare_join,
    .prepare_unjoin = prepare_unjoin,
    .prepare_merge = prepare_merge,
    .prepare_create_conference = prepare_create_conference,
    .prepare_destroy_conference = prepare_destroy_conference,
    .prepare_dialog_start = prepare_dialog_start,
    .prepare_dialog_terminate = prepare_dialog_terminate,
    .prepare_dialog_prepare = prepare_dialog_prepare,
    .prepare_prepared_dialog_start = prepare_prepared_dialog_start};

typedef struct datamodel_probe datamodel_probe;

typedef struct datamodel_ticket {
    datamodel_probe *owner;
    bool live;
} datamodel_ticket;

struct datamodel_probe {
    datamodel_ticket ticket;
    size_t validate_count;
    size_t readable_validate_count;
    size_t read_count;
    size_t prepare_count;
    size_t commit_count;
    size_t discard_count;
    bool reject_validation;
    bool reject_readable_validation;
    bool reject_read;
    bool reject_prepare;
    bool malformed_ticket;
    bool reject_condition_compile;
    bool reject_condition_evaluate;
    size_t reject_condition_compile_at;
    bool condition_result;
    size_t condition_compile_count;
    size_t condition_evaluate_count;
    size_t condition_destroy_count;
    char location[64];
    size_t location_size;
    char value[64];
    size_t value_size;
    size_t commit_sequence;
    const char *read_result;
    size_t read_result_size;
};

static void datamodel_commit(void *user) {
    datamodel_ticket *ticket = (datamodel_ticket *)user;
    if (ticket == NULL || !ticket->live) return;
    ticket->live = false;
    ++ticket->owner->commit_count;
    ticket->owner->commit_sequence = ++transaction_commit_sequence;
}

static void datamodel_discard(void *user) {
    datamodel_ticket *ticket = (datamodel_ticket *)user;
    if (ticket == NULL || !ticket->live) return;
    ticket->live = false;
    ++ticket->owner->discard_count;
}

static scxml_adapter_status validate_string_location(
    void *user, const char *location, size_t location_size,
    const char **out_error) {
    datamodel_probe *probe = (datamodel_probe *)user;
    ++probe->validate_count;
    probe->location_size = location_size;
    memcpy(probe->location, location, location_size);
    probe->location[location_size] = '\0';
    if (out_error != NULL) *out_error = NULL;
    if (probe->reject_validation) {
        if (out_error != NULL) *out_error = "invalid test location";
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status prepare_assign_string(
    void *user, const char *location, size_t location_size,
    const char *value, size_t value_size,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    datamodel_probe *probe = (datamodel_probe *)user;
    ++probe->prepare_count;
    probe->location_size = location_size;
    memcpy(probe->location, location, location_size);
    probe->location[location_size] = '\0';
    probe->value_size = value_size;
    memcpy(probe->value, value, value_size);
    probe->value[value_size] = '\0';
    if (out_error != NULL) *out_error = NULL;
    if (probe->reject_prepare) {
        if (out_error != NULL) *out_error = "test writeback refused";
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    if (probe->malformed_ticket) {
        *out_ticket = (cflow_statechart_effect_ticket){0};
        return SCXML_ADAPTER_ACCEPTED;
    }
    probe->ticket = (datamodel_ticket){.owner = probe, .live = true};
    *out_ticket = (cflow_statechart_effect_ticket){
        .commit = datamodel_commit,
        .discard = datamodel_discard,
        .user = &probe->ticket};
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status validate_readable_string_location(
    void *user, const char *location, size_t location_size,
    const char **out_error) {
    datamodel_probe *probe = (datamodel_probe *)user;
    ++probe->readable_validate_count;
    probe->location_size = location_size;
    memcpy(probe->location, location, location_size);
    probe->location[location_size] = '\0';
    if (out_error != NULL) *out_error = NULL;
    if (probe->reject_readable_validation) {
        if (out_error != NULL) *out_error = "unreadable test location";
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status read_string(
    void *user, const char *location, size_t location_size,
    ccxml_string_view *out_value, const char **out_error) {
    datamodel_probe *probe = (datamodel_probe *)user;
    ++probe->read_count;
    probe->location_size = location_size;
    memcpy(probe->location, location, location_size);
    probe->location[location_size] = '\0';
    if (out_error != NULL) *out_error = NULL;
    if (probe->reject_read) {
        if (out_error != NULL) *out_error = "test read refused";
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    *out_value = (ccxml_string_view){
        .data = probe->read_result,
        .size = probe->read_result_size};
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status compile_condition(
    void *user, const char *source, size_t source_size,
    ccxml_condition *out_condition, const char **out_error) {
    datamodel_probe *probe = (datamodel_probe *)user;
    ++probe->condition_compile_count;
    if (out_error != NULL) *out_error = NULL;
    if (probe->reject_condition_compile ||
        (probe->reject_condition_compile_at != 0u &&
         probe->condition_compile_count ==
             probe->reject_condition_compile_at)) {
        if (out_error != NULL) *out_error = "test condition compile refused";
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    if (source == NULL || source_size == 0u || out_condition == NULL ||
        out_condition->impl != NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    out_condition->impl = probe;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status evaluate_condition(
    void *user, const ccxml_condition *condition,
    const ccxml_event *event, bool *out_value,
    const char **out_error) {
    datamodel_probe *probe = (datamodel_probe *)user;
    ++probe->condition_evaluate_count;
    if (out_error != NULL) *out_error = NULL;
    if (probe->reject_condition_evaluate) {
        if (out_error != NULL) *out_error = "test condition evaluation refused";
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    if (condition == NULL || condition->impl != probe || event == NULL ||
        out_value == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    *out_value = probe->condition_result;
    return SCXML_ADAPTER_ACCEPTED;
}

static void destroy_condition(
    void *user, ccxml_condition *condition) {
    datamodel_probe *probe = (datamodel_probe *)user;
    if (condition == NULL || condition->impl == NULL) return;
    ++probe->condition_destroy_count;
    condition->impl = NULL;
}

static const ccxml_datamodel_adapter_v1 datamodel_adapter = {
    .abi_version = CCXML_DATAMODEL_ADAPTER_ABI_V1,
    .struct_size = sizeof(ccxml_datamodel_adapter_v1),
    .validate_string_location = validate_string_location,
    .prepare_assign_string = prepare_assign_string,
    .validate_readable_string_location =
        validate_readable_string_location,
    .read_string = read_string,
    .compile_condition = compile_condition,
    .evaluate_condition = evaluate_condition,
    .destroy_condition = destroy_condition};

static ccxml_status compile_program(
    ccxml_program *program, const char *actions) {
    char source[1024];
    ccxml_diagnostic diagnostic = {0};
    const int written = snprintf(
        source, sizeof(source),
        "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
        "<eventprocessor><transition event='connection.alerting'>%s"
        "</transition></eventprocessor></ccxml>",
        actions);
    if (written <= 0 || (size_t)written >= sizeof(source))
        return CCXML_INVALID_ARGUMENT;
    return ccxml_compile(
        program, source, (size_t)written, NULL, &diagnostic);
}

static ccxml_status compile_document(
    ccxml_program *program, const char *source) {
    ccxml_diagnostic diagnostic = {0};
    return ccxml_compile(
        program, source, strlen(source), NULL, &diagnostic);
}

static ccxml_status init_session(
    ccxml_session *session, const ccxml_program *program,
    provider_probe *probe) {
    const ccxml_session_config config = {
        .program = program,
        .telephony = &provider_adapter,
        .telephony_user = probe};
    return ccxml_session_init(session, &config);
}

static ccxml_status init_session_with_datamodel(
    ccxml_session *session, const ccxml_program *program,
    provider_probe *provider, datamodel_probe *datamodel) {
    const ccxml_session_config config = {
        .program = program,
        .telephony = &provider_adapter,
        .telephony_user = provider,
        .datamodel = &datamodel_adapter,
        .datamodel_user = datamodel};
    return ccxml_session_init(session, &config);
}

static ccxml_event alerting_event(void) {
    const ccxml_event event = {
        .name = "connection.alerting",
        .name_size = sizeof("connection.alerting") - 1u,
        .connection_id = "call-7",
        .connection_id_size = sizeof("call-7") - 1u};
    return event;
}

static ccxml_event loaded_event(void) {
    const ccxml_event event = {
        .name = "ccxml.loaded",
        .name_size = sizeof("ccxml.loaded") - 1u};
    return event;
}

spec("CCXML session") {
    it("commits an accepted telephony action") {
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe probe = {.quiescent = true};
        ccxml_event event = alerting_event();

        check_equal(compile_program(&program, "<accept/>"), CCXML_OK);
        check_equal(init_session(&session, &program, &probe), CCXML_OK);
        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_equal(probe.prepare_count, (size_t)1);
        check_equal(probe.commit_count, (size_t)1);
        check_equal(probe.discard_count, (size_t)0);
        check_equal(probe.connection_id, "call-7");

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_program_destroy(&program);
    }

    it("commits prepared effects in document order") {
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe probe = {.quiescent = true};
        ccxml_event event = alerting_event();

        check_equal(
            compile_program(&program, "<accept/><accept/>"), CCXML_OK);
        check_equal(init_session(&session, &program, &probe), CCXML_OK);
        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_equal(probe.commit_count, (size_t)2);
        check_equal(probe.commit_order[0], (size_t)1);
        check_equal(probe.commit_order[1], (size_t)2);

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_program_destroy(&program);
    }

    it("selects the first exact transition in document order") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor>"
            "<transition event='connection.alerting'><exit/></transition>"
            "<transition event='connection.alerting'><accept/></transition>"
            "</eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe probe = {.quiescent = true};
        ccxml_event event = alerting_event();

        check_equal(compile_document(&program, source), CCXML_OK);
        check_equal(init_session(&session, &program, &probe), CCXML_OK);
        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_true(ccxml_session_is_terminated(&session));
        check_equal(probe.prepare_count, (size_t)0);

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_program_destroy(&program);
    }

    it("matches event names case-insensitively in document order") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor>"
            "<transition event='Connection.Alerting'><exit/></transition>"
            "<transition event='connection.alerting'><accept/></transition>"
            "</eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe probe = {.quiescent = true};
        ccxml_event event = alerting_event();

        check_equal(compile_document(&program, source), CCXML_OK);
        check_equal(init_session(&session, &program, &probe), CCXML_OK);
        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_true(ccxml_session_is_terminated(&session));
        check_equal(probe.prepare_count, (size_t)0);

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_program_destroy(&program);
    }

    it("matches wildcard event patterns in document order") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor>"
            "<transition event='ERROR.*.NOT*'><exit/></transition>"
            "<transition event='error.dialog.notstarted'><accept/></transition>"
            "</eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe probe = {.quiescent = true};
        const ccxml_event event = {
            .name = "error.dialog.notstarted",
            .name_size = sizeof("error.dialog.notstarted") - 1u};

        check_equal(compile_document(&program, source), CCXML_OK);
        check_equal(init_session(&session, &program, &probe), CCXML_OK);
        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_true(ccxml_session_is_terminated(&session));
        check_equal(probe.prepare_count, (size_t)0);

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_program_destroy(&program);
    }

    it("ignores an unmatched event without a provider effect") {
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe probe = {.quiescent = true};
        ccxml_event event = {
            .name = "connection.connected",
            .name_size = sizeof("connection.connected") - 1u};

        check_equal(compile_program(&program, "<accept/>"), CCXML_OK);
        check_equal(init_session(&session, &program, &probe), CCXML_OK);
        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_equal(probe.prepare_count, (size_t)0);

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_program_destroy(&program);
    }

    it("terminates on an unhandled error event") {
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe probe = {.quiescent = true};
        const ccxml_event event = {
            .name = "error.provider",
            .name_size = sizeof("error.provider") - 1u};

        check_equal(compile_program(&program, "<exit/>"), CCXML_OK);
        check_equal(init_session(&session, &program, &probe), CCXML_OK);
        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_true(ccxml_session_is_terminated(&session));
        check_equal(probe.close_count, (size_t)1);

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_program_destroy(&program);
    }

    it("terminates on an unhandled ccxml kill event") {
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe probe = {.quiescent = true};
        const ccxml_event event = {
            .name = "CCXML.KILL",
            .name_size = sizeof("CCXML.KILL") - 1u};

        check_equal(compile_program(&program, "<exit/>"), CCXML_OK);
        check_equal(init_session(&session, &program, &probe), CCXML_OK);
        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_true(ccxml_session_is_terminated(&session));
        check_equal(probe.close_count, (size_t)1);

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_program_destroy(&program);
    }

    it("keeps the session live when an error transition handles the event") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor>"
            "<transition event='error.*'></transition>"
            "</eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe probe = {.quiescent = true};
        const ccxml_event event = {
            .name = "error.provider",
            .name_size = sizeof("error.provider") - 1u};

        check_equal(compile_document(&program, source), CCXML_OK);
        check_equal(init_session(&session, &program, &probe), CCXML_OK);
        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_false(ccxml_session_is_terminated(&session));
        check_equal(probe.close_count, (size_t)0);

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_program_destroy(&program);
    }

    it("stops guard selection when the statevariable read fails") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<var name='mode' expr=\"'waiting'\"/>"
            "<eventprocessor statevariable='mode'>"
            "<transition state='waiting' event='connection.alerting'>"
            "<exit/></transition>"
            "<transition event='connection.alerting'><accept/></transition>"
            "</eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe provider = {.quiescent = true};
        datamodel_probe datamodel = {.reject_read = true};
        ccxml_event event = alerting_event();

        check_equal(compile_document(&program, source), CCXML_OK);
        check_equal(
            init_session_with_datamodel(
                &session, &program, &provider, &datamodel),
            CCXML_OK);
        check_equal(
            ccxml_session_dispatch(&session, &event),
            CCXML_ADAPTER_ERROR);
        check_equal(provider.prepare_count, (size_t)0);
        check_false(ccxml_session_is_terminated(&session));

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_program_destroy(&program);
    }

    it("stops guard selection when condition evaluation fails") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor>"
            "<transition event='connection.alerting' cond='true'>"
            "<exit/></transition>"
            "<transition event='connection.alerting'><accept/></transition>"
            "</eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe provider = {.quiescent = true};
        datamodel_probe datamodel = {.reject_condition_evaluate = true};
        ccxml_event event = alerting_event();

        check_equal(compile_document(&program, source), CCXML_OK);
        check_equal(
            init_session_with_datamodel(
                &session, &program, &provider, &datamodel),
            CCXML_OK);
        check_equal(datamodel.condition_compile_count, (size_t)1);

        check_equal(
            ccxml_session_dispatch(&session, &event),
            CCXML_ADAPTER_ERROR);
        check_equal(datamodel.condition_evaluate_count, (size_t)1);
        check_equal(provider.prepare_count, (size_t)0);
        check_false(ccxml_session_is_terminated(&session));

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        check_equal(datamodel.condition_destroy_count, (size_t)1);
        ccxml_program_destroy(&program);
    }

    it("requires the appended datamodel condition operations") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='advance' cond='true'>"
            "<exit/></transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe provider = {.quiescent = true};
        datamodel_probe datamodel = {0};
        ccxml_datamodel_adapter_v1 legacy = datamodel_adapter;
        ccxml_session_config config;
        legacy.struct_size =
            offsetof(ccxml_datamodel_adapter_v1, compile_condition);

        check_equal(compile_document(&program, source), CCXML_OK);
        config = (ccxml_session_config){
            .program = &program,
            .telephony = &provider_adapter,
            .telephony_user = &provider,
            .datamodel = &legacy,
            .datamodel_user = &datamodel};
        check_equal(
            ccxml_session_init(&session, &config),
            CCXML_INVALID_ARGUMENT);
        check_null(session.impl);
        check_equal(datamodel.condition_compile_count, (size_t)0);

        ccxml_program_destroy(&program);
    }

    it("destroys compiled conditions when later admission fails") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor>"
            "<transition event='first' cond='true'><exit/></transition>"
            "<transition event='second' cond='false'><exit/></transition>"
            "</eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe provider = {.quiescent = true};
        datamodel_probe datamodel = {.reject_condition_compile_at = 2u};

        check_equal(compile_document(&program, source), CCXML_OK);
        check_equal(
            init_session_with_datamodel(
                &session, &program, &provider, &datamodel),
            CCXML_ADAPTER_ERROR);
        check_null(session.impl);
        check_equal(datamodel.condition_compile_count, (size_t)2);
        check_equal(datamodel.condition_destroy_count, (size_t)1);

        ccxml_program_destroy(&program);
    }

    it("rejects accept when the current event has no connection") {
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe probe = {.quiescent = true};
        ccxml_event event = alerting_event();
        event.connection_id = NULL;
        event.connection_id_size = 0u;

        check_equal(compile_program(&program, "<accept/>"), CCXML_OK);
        check_equal(init_session(&session, &program, &probe), CCXML_OK);
        check_equal(
            ccxml_session_dispatch(&session, &event), CCXML_INVALID_EVENT);
        check_equal(probe.prepare_count, (size_t)0);

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_program_destroy(&program);
    }

    it("discards earlier tickets when a later action is rejected") {
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe probe = {
            .reject_on_prepare = 3u,
            .quiescent = true};
        ccxml_event event = alerting_event();

        check_equal(
            compile_program(
                &program, "<accept/><accept/><accept/>"), CCXML_OK);
        check_equal(init_session(&session, &program, &probe), CCXML_OK);
        check_equal(
            ccxml_session_dispatch(&session, &event), CCXML_ADAPTER_ERROR);
        check_equal(probe.prepare_count, (size_t)3);
        check_equal(probe.commit_count, (size_t)0);
        check_equal(probe.discard_count, (size_t)2);
        check_equal(probe.discard_order[0], (size_t)2);
        check_equal(probe.discard_order[1], (size_t)1);
        check_false(ccxml_session_is_terminated(&session));

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_program_destroy(&program);
    }

    it("rejects an adapter with a missing prepare operation") {
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe probe = {.quiescent = true};
        ccxml_telephony_adapter_v1 incomplete = provider_adapter;
        ccxml_session_config config;
        incomplete.prepare_accept = NULL;

        check_equal(compile_program(&program, ""), CCXML_OK);
        config = (ccxml_session_config){
            .program = &program,
            .telephony = &incomplete,
            .telephony_user = &probe};
        check_equal(
            ccxml_session_init(&session, &config), CCXML_INVALID_ARGUMENT);
        check_null(session.impl);

        ccxml_program_destroy(&program);
    }

    it("rejects an accepted action without a complete ticket") {
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe probe = {
            .malformed_ticket = true,
            .quiescent = true};
        ccxml_event event = alerting_event();

        check_equal(compile_program(&program, "<accept/>"), CCXML_OK);
        check_equal(init_session(&session, &program, &probe), CCXML_OK);
        check_equal(
            ccxml_session_dispatch(&session, &event),
            CCXML_INVALID_CONTRACT);
        check_equal(probe.commit_count, (size_t)0);

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        ccxml_program_destroy(&program);
    }

    it("exit terminates and closes exactly once") {
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe probe = {.quiescent = true};
        ccxml_event event = alerting_event();

        check_equal(compile_program(&program, "<exit/>"), CCXML_OK);
        check_equal(init_session(&session, &program, &probe), CCXML_OK);
        check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
        check_true(ccxml_session_is_terminated(&session));
        check_equal(probe.close_count, (size_t)1);
        check_equal(ccxml_session_dispatch(&session, &event), CCXML_CLOSED);
        ccxml_session_close(&session);
        check_equal(probe.close_count, (size_t)1);

        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        check_equal(probe.close_count, (size_t)1);
        ccxml_program_destroy(&program);
    }

    it("keeps ownership while the provider is not quiescent") {
        ccxml_program program = {0};
        ccxml_session session = {0};
        provider_probe probe = {0};

        check_equal(compile_program(&program, ""), CCXML_OK);
        check_equal(init_session(&session, &program, &probe), CCXML_OK);
        check_equal(ccxml_session_destroy(&session), CCXML_BUSY);
        check_not_null(session.impl);
        check_equal(probe.close_count, (size_t)1);
        probe.quiescent = true;
        check_equal(ccxml_session_destroy(&session), CCXML_OK);
        check_null(session.impl);
        check_equal(probe.close_count, (size_t)1);

        ccxml_program_destroy(&program);
    }

    group("createcall") {
        it("commits the copied destination") {
            char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='ccxml.loaded'>"
                "<createcall dest=\"'tel:+12025550123'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = loaded_event();

            check_equal(compile_document(&program, source), CCXML_OK);
            memset(source, 'x', sizeof(source) - 1u);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(probe.prepare_count, (size_t)1);
            check_equal(probe.prepare_kinds[0], (size_t)PROVIDER_CREATE_CALL);
            check_equal(probe.destination, "tel:+12025550123");
            check_equal(probe.commit_count, (size_t)1);
            check_equal(probe.discard_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("commits mixed effects in document order") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<accept/><createcall dest=\"'tel:123'\"/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(probe.prepare_kinds[0], (size_t)PROVIDER_ACCEPT);
            check_equal(
                probe.prepare_kinds[1], (size_t)PROVIDER_CREATE_CALL);
            check_equal(probe.commit_order[0], (size_t)1);
            check_equal(probe.commit_order[1], (size_t)2);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("discards an earlier accept when call creation is rejected") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {
                .reject_on_prepare = 2u,
                .quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<accept/><createcall dest=\"'tel:123'\"/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(probe.prepare_count, (size_t)2);
            check_equal(probe.commit_count, (size_t)0);
            check_equal(probe.discard_count, (size_t)1);
            check_equal(probe.discard_order[0], (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("accepts a legacy adapter prefix for an accept-only program") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_create_call);

            check_equal(compile_program(&program, "<accept/>"), CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &probe};
            check_equal(ccxml_session_init(&session, &config), CCXML_OK);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("requires the adapter tail for a createcall program") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_create_call);

            check_equal(
                compile_program(
                    &program, "<createcall dest=\"'tel:123'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }

        it("rejects a truncated createcall callback field") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 truncated = provider_adapter;
            ccxml_session_config config;
            truncated.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_create_call) +
                sizeof(truncated.prepare_create_call) - 1u;

            check_equal(
                compile_program(
                    &program, "<createcall dest=\"'tel:123'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &truncated,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }
    }

    group("disconnect") {
        it("commits the current event connection") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(compile_program(&program, "<disconnect/>"), CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(probe.prepare_count, (size_t)1);
            check_equal(probe.prepare_kinds[0], (size_t)PROVIDER_DISCONNECT);
            check_equal(probe.disconnected_connection_id, "call-7");
            check_equal(
                probe.disconnected_connection_id_size,
                sizeof("call-7") - 1u);
            check_equal(probe.commit_count, (size_t)1);
            check_equal(probe.discard_count, (size_t)0);
            check_false(ccxml_session_is_terminated(&session));

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects a missing current event connection") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();
            event.connection_id = NULL;
            event.connection_id_size = 0u;

            check_equal(compile_program(&program, "<disconnect/>"), CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event), CCXML_INVALID_EVENT);
            check_equal(probe.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects an embedded NUL in the current connection") {
            static const char malformed_id[] = {'c', 'a', 'l', 'l', '\0', '7'};
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();
            event.connection_id = malformed_id;
            event.connection_id_size = sizeof(malformed_id);

            check_equal(compile_program(&program, "<disconnect/>"), CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event), CCXML_INVALID_EVENT);
            check_equal(probe.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("discards earlier effects when the current connection is missing") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();
            event.connection_id = NULL;
            event.connection_id_size = 0u;

            check_equal(
                compile_program(
                    &program,
                    "<createcall dest=\"'tel:123'\"/><disconnect/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event), CCXML_INVALID_EVENT);
            check_equal(probe.prepare_count, (size_t)1);
            check_equal(
                probe.prepare_kinds[0], (size_t)PROVIDER_CREATE_CALL);
            check_equal(probe.commit_count, (size_t)0);
            check_equal(probe.discard_count, (size_t)1);
            check_equal(probe.discard_order[0], (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("commits mixed effects in document order") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<createcall dest=\"'tel:123'\"/><disconnect/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(
                probe.prepare_kinds[0], (size_t)PROVIDER_CREATE_CALL);
            check_equal(probe.prepare_kinds[1], (size_t)PROVIDER_DISCONNECT);
            check_equal(probe.commit_order[0], (size_t)1);
            check_equal(probe.commit_order[1], (size_t)2);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("discards an earlier effect when disconnect is rejected") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {
                .reject_on_prepare = 2u,
                .quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<createcall dest=\"'tel:123'\"/><disconnect/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(probe.prepare_count, (size_t)2);
            check_equal(probe.commit_count, (size_t)0);
            check_equal(probe.discard_count, (size_t)1);
            check_equal(probe.discard_order[0], (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("accepts the createcall adapter prefix for older programs") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_disconnect);

            check_equal(
                compile_program(
                    &program, "<createcall dest=\"'tel:123'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &probe};
            check_equal(ccxml_session_init(&session, &config), CCXML_OK);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("requires the appended operation for a disconnect program") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_disconnect);

            check_equal(compile_program(&program, "<disconnect/>"), CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }

        it("rejects a null disconnect operation") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 incomplete = provider_adapter;
            ccxml_session_config config;
            incomplete.prepare_disconnect = NULL;

            check_equal(compile_program(&program, "<disconnect/>"), CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &incomplete,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }

        it("rejects a truncated disconnect callback field") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 truncated = provider_adapter;
            ccxml_session_config config;
            truncated.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_disconnect) +
                sizeof(truncated.prepare_disconnect) - 1u;

            check_equal(compile_program(&program, "<disconnect/>"), CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &truncated,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }
    }

    group("reject") {
        it("commits the current event connection") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(compile_program(&program, "<reject/>"), CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(probe.prepare_count, (size_t)1);
            check_equal(probe.prepare_kinds[0], (size_t)PROVIDER_REJECT);
            check_equal(probe.rejected_connection_id, "call-7");
            check_equal(
                probe.rejected_connection_id_size, sizeof("call-7") - 1u);
            check_equal(probe.commit_count, (size_t)1);
            check_equal(probe.discard_count, (size_t)0);
            check_false(ccxml_session_is_terminated(&session));

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects a missing current event connection") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();
            event.connection_id = NULL;
            event.connection_id_size = 0u;

            check_equal(compile_program(&program, "<reject/>"), CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event), CCXML_INVALID_EVENT);
            check_equal(probe.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects an embedded NUL in the current connection") {
            static const char malformed_id[] = {'c', 'a', 'l', 'l', '\0', '7'};
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();
            event.connection_id = malformed_id;
            event.connection_id_size = sizeof(malformed_id);

            check_equal(compile_program(&program, "<reject/>"), CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event), CCXML_INVALID_EVENT);
            check_equal(probe.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("discards earlier effects when the current connection is missing") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();
            event.connection_id = NULL;
            event.connection_id_size = 0u;

            check_equal(
                compile_program(
                    &program,
                    "<createcall dest=\"'tel:123'\"/><reject/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event), CCXML_INVALID_EVENT);
            check_equal(probe.prepare_count, (size_t)1);
            check_equal(
                probe.prepare_kinds[0], (size_t)PROVIDER_CREATE_CALL);
            check_equal(probe.commit_count, (size_t)0);
            check_equal(probe.discard_count, (size_t)1);
            check_equal(probe.discard_order[0], (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("commits mixed effects in document order") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<createcall dest=\"'tel:123'\"/><reject/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(
                probe.prepare_kinds[0], (size_t)PROVIDER_CREATE_CALL);
            check_equal(probe.prepare_kinds[1], (size_t)PROVIDER_REJECT);
            check_equal(probe.commit_order[0], (size_t)1);
            check_equal(probe.commit_order[1], (size_t)2);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("discards an earlier effect when rejection is refused") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {
                .reject_on_prepare = 2u,
                .quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<createcall dest=\"'tel:123'\"/><reject/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(probe.prepare_count, (size_t)2);
            check_equal(probe.commit_count, (size_t)0);
            check_equal(probe.discard_count, (size_t)1);
            check_equal(probe.discard_order[0], (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("accepts the disconnect adapter prefix for older programs") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_reject);

            check_equal(compile_program(&program, "<disconnect/>"), CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &probe};
            check_equal(ccxml_session_init(&session, &config), CCXML_OK);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("requires the appended operation for a reject program") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_reject);

            check_equal(compile_program(&program, "<reject/>"), CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }

        it("rejects a null reject operation") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 incomplete = provider_adapter;
            ccxml_session_config config;
            incomplete.prepare_reject = NULL;

            check_equal(compile_program(&program, "<reject/>"), CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &incomplete,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }

        it("rejects a truncated reject callback field") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 truncated = provider_adapter;
            ccxml_session_config config;
            truncated.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_reject) +
                sizeof(truncated.prepare_reject) - 1u;

            check_equal(compile_program(&program, "<reject/>"), CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &truncated,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }
    }

    group("redirect") {
        it("commits the current connection and retained destination") {
            char actions[] = "<redirect dest=\"'tel:+12025550123'\"/>";
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(compile_program(&program, actions), CCXML_OK);
            memset(actions, 'x', sizeof(actions) - 1u);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(probe.prepare_count, (size_t)1);
            check_equal(probe.prepare_kinds[0], (size_t)PROVIDER_REDIRECT);
            check_equal(probe.redirected_connection_id, "call-7");
            check_equal(
                probe.redirected_connection_id_size, sizeof("call-7") - 1u);
            check_equal(probe.redirected_destination, "tel:+12025550123");
            check_equal(
                probe.redirected_destination_size,
                sizeof("tel:+12025550123") - 1u);
            check_equal(probe.commit_count, (size_t)1);
            check_equal(probe.discard_count, (size_t)0);
            check_false(ccxml_session_is_terminated(&session));

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects a missing current event connection") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();
            event.connection_id = NULL;
            event.connection_id_size = 0u;

            check_equal(
                compile_program(&program, "<redirect dest=\"'tel:123'\"/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event), CCXML_INVALID_EVENT);
            check_equal(probe.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects an embedded NUL in the current connection") {
            static const char malformed_id[] = {'c', 'a', 'l', 'l', '\0', '7'};
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();
            event.connection_id = malformed_id;
            event.connection_id_size = sizeof(malformed_id);

            check_equal(
                compile_program(&program, "<redirect dest=\"'tel:123'\"/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event), CCXML_INVALID_EVENT);
            check_equal(probe.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("discards earlier effects when the current connection is missing") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();
            event.connection_id = NULL;
            event.connection_id_size = 0u;

            check_equal(
                compile_program(
                    &program,
                    "<createcall dest=\"'tel:123'\"/>"
                    "<redirect dest=\"'tel:456'\"/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event), CCXML_INVALID_EVENT);
            check_equal(probe.prepare_count, (size_t)1);
            check_equal(
                probe.prepare_kinds[0], (size_t)PROVIDER_CREATE_CALL);
            check_equal(probe.commit_count, (size_t)0);
            check_equal(probe.discard_count, (size_t)1);
            check_equal(probe.discard_order[0], (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("commits mixed effects in document order") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<createcall dest=\"'tel:123'\"/>"
                    "<redirect dest=\"'tel:456'\"/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(
                probe.prepare_kinds[0], (size_t)PROVIDER_CREATE_CALL);
            check_equal(probe.prepare_kinds[1], (size_t)PROVIDER_REDIRECT);
            check_equal(probe.commit_order[0], (size_t)1);
            check_equal(probe.commit_order[1], (size_t)2);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("discards an earlier effect when redirect is refused") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {
                .reject_on_prepare = 2u,
                .quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<createcall dest=\"'tel:123'\"/>"
                    "<redirect dest=\"'tel:456'\"/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event), CCXML_ADAPTER_ERROR);
            check_equal(probe.prepare_count, (size_t)2);
            check_equal(probe.commit_count, (size_t)0);
            check_equal(probe.discard_count, (size_t)1);
            check_equal(probe.discard_order[0], (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("accepts the reject adapter prefix for older programs") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_redirect);

            check_equal(compile_program(&program, "<reject/>"), CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &probe};
            check_equal(ccxml_session_init(&session, &config), CCXML_OK);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("requires the appended operation for a redirect program") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_redirect);

            check_equal(
                compile_program(&program, "<redirect dest=\"'tel:123'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }

        it("rejects a null redirect operation") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 incomplete = provider_adapter;
            ccxml_session_config config;
            incomplete.prepare_redirect = NULL;

            check_equal(
                compile_program(&program, "<redirect dest=\"'tel:123'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &incomplete,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }

        it("rejects a truncated redirect callback field") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 truncated = provider_adapter;
            ccxml_session_config config;
            truncated.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_redirect) +
                sizeof(truncated.prepare_redirect) - 1u;

            check_equal(
                compile_program(&program, "<redirect dest=\"'tel:123'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &truncated,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }
    }

    group("join") {
        it("commits the ordered resource identifiers from program storage") {
            char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.alerting'>"
                "<join id1=\"'call-a'\" id2=\"'conference-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(compile_document(&program, source), CCXML_OK);
            memset(source, 'x', sizeof(source) - 1u);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(probe.prepare_count, (size_t)1);
            check_equal(probe.prepare_kinds[0], (size_t)PROVIDER_JOIN);
            check_equal(probe.joined_id1, "call-a");
            check_equal(probe.joined_id1_size, sizeof("call-a") - 1u);
            check_equal(probe.joined_id2, "conference-b");
            check_equal(
                probe.joined_id2_size, sizeof("conference-b") - 1u);
            check_equal(probe.commit_count, (size_t)1);
            check_equal(probe.discard_count, (size_t)0);
            check_false(ccxml_session_is_terminated(&session));

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("commits mixed effects in document order") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<createcall dest=\"'tel:123'\"/>"
                    "<join id1=\"'call-a'\" id2=\"'call-b'\"/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(
                probe.prepare_kinds[0], (size_t)PROVIDER_CREATE_CALL);
            check_equal(probe.prepare_kinds[1], (size_t)PROVIDER_JOIN);
            check_equal(probe.commit_order[0], (size_t)1);
            check_equal(probe.commit_order[1], (size_t)2);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("discards earlier effects in reverse order when join is refused") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {
                .reject_on_prepare = 3u,
                .quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<createcall dest=\"'tel:123'\"/><disconnect/>"
                    "<join id1=\"'call-a'\" id2=\"'call-b'\"/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event), CCXML_ADAPTER_ERROR);
            check_equal(probe.prepare_count, (size_t)3);
            check_equal(probe.commit_count, (size_t)0);
            check_equal(probe.discard_count, (size_t)2);
            check_equal(probe.discard_order[0], (size_t)2);
            check_equal(probe.discard_order[1], (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("accepts the redirect adapter prefix for older programs") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_join);

            check_equal(
                compile_program(&program, "<redirect dest=\"'tel:123'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &probe};
            check_equal(ccxml_session_init(&session, &config), CCXML_OK);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("requires the appended operation for a join program") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_join);

            check_equal(
                compile_program(
                    &program,
                    "<join id1=\"'call-a'\" id2=\"'call-b'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }

        it("rejects a null join operation") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 incomplete = provider_adapter;
            ccxml_session_config config;
            incomplete.prepare_join = NULL;

            check_equal(
                compile_program(
                    &program,
                    "<join id1=\"'call-a'\" id2=\"'call-b'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &incomplete,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }

        it("rejects a truncated join callback field") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 truncated = provider_adapter;
            ccxml_session_config config;
            truncated.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_join) +
                sizeof(truncated.prepare_join) - 1u;

            check_equal(
                compile_program(
                    &program,
                    "<join id1=\"'call-a'\" id2=\"'call-b'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &truncated,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }
    }

    group("unjoin") {
        it("commits ordered program-owned identifiers without event connection") {
            char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='ccxml.loaded'>"
                "<unjoin id1=\"'call-a'\" id2=\"'conference-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = loaded_event();

            check_equal(compile_document(&program, source), CCXML_OK);
            memset(source, 'x', sizeof(source) - 1u);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(probe.prepare_count, (size_t)1);
            check_equal(probe.prepare_kinds[0], (size_t)PROVIDER_UNJOIN);
            check_equal(probe.unjoined_id1, "call-a");
            check_equal(probe.unjoined_id1_size, sizeof("call-a") - 1u);
            check_equal(probe.unjoined_id2, "conference-b");
            check_equal(
                probe.unjoined_id2_size, sizeof("conference-b") - 1u);
            check_equal(probe.commit_count, (size_t)1);
            check_equal(probe.discard_count, (size_t)0);
            check_false(ccxml_session_is_terminated(&session));

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("commits join and unjoin in document order") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<join id1=\"'call-a'\" id2=\"'conference-b'\"/>"
                    "<unjoin id1=\"'call-a'\" id2=\"'conference-b'\"/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(probe.prepare_kinds[0], (size_t)PROVIDER_JOIN);
            check_equal(probe.prepare_kinds[1], (size_t)PROVIDER_UNJOIN);
            check_equal(probe.commit_order[0], (size_t)1);
            check_equal(probe.commit_order[1], (size_t)2);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("discards earlier effects in reverse order when unjoin is refused") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {
                .reject_on_prepare = 3u,
                .quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<createcall dest=\"'tel:123'\"/>"
                    "<join id1=\"'call-a'\" id2=\"'conference-b'\"/>"
                    "<unjoin id1=\"'call-a'\" id2=\"'conference-b'\"/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event), CCXML_ADAPTER_ERROR);
            check_equal(probe.prepare_count, (size_t)3);
            check_equal(probe.commit_count, (size_t)0);
            check_equal(probe.discard_count, (size_t)2);
            check_equal(probe.discard_order[0], (size_t)2);
            check_equal(probe.discard_order[1], (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("accepts the join adapter prefix for older programs") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_unjoin);

            check_equal(
                compile_program(
                    &program,
                    "<join id1=\"'call-a'\" id2=\"'conference-b'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &probe};
            check_equal(ccxml_session_init(&session, &config), CCXML_OK);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("requires the appended operation for an unjoin program") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_unjoin);

            check_equal(
                compile_program(
                    &program,
                    "<unjoin id1=\"'call-a'\" id2=\"'conference-b'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }

        it("rejects a null unjoin operation") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 incomplete = provider_adapter;
            ccxml_session_config config;
            incomplete.prepare_unjoin = NULL;

            check_equal(
                compile_program(
                    &program,
                    "<unjoin id1=\"'call-a'\" id2=\"'conference-b'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &incomplete,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }

        it("rejects a truncated unjoin callback field") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 truncated = provider_adapter;
            ccxml_session_config config;
            truncated.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_unjoin) +
                sizeof(truncated.prepare_unjoin) - 1u;

            check_equal(
                compile_program(
                    &program,
                    "<unjoin id1=\"'call-a'\" id2=\"'conference-b'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &truncated,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }
    }

    group("merge") {
        it("commits two program-owned IDs without event connection data") {
            char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='ccxml.loaded'>"
                "<merge connectionid1=\"'call-a'\" "
                "connectionid2=\"'call-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = loaded_event();

            check_equal(compile_document(&program, source), CCXML_OK);
            memset(source, 'x', sizeof(source) - 1u);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(probe.prepare_count, (size_t)1);
            check_equal(probe.prepare_kinds[0], (size_t)PROVIDER_MERGE);
            check_equal(probe.merged_connection_id1, "call-a");
            check_equal(
                probe.merged_connection_id1_size, sizeof("call-a") - 1u);
            check_equal(probe.merged_connection_id2, "call-b");
            check_equal(
                probe.merged_connection_id2_size, sizeof("call-b") - 1u);
            check_equal(probe.commit_count, (size_t)1);
            check_equal(probe.discard_count, (size_t)0);
            check_false(ccxml_session_is_terminated(&session));

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("commits join and merge effects in document order") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<join id1=\"'call-a'\" id2=\"'call-b'\"/>"
                    "<merge connectionid1=\"'call-a'\" "
                    "connectionid2=\"'call-b'\"/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(probe.prepare_kinds[0], (size_t)PROVIDER_JOIN);
            check_equal(probe.prepare_kinds[1], (size_t)PROVIDER_MERGE);
            check_equal(probe.commit_order[0], (size_t)1);
            check_equal(probe.commit_order[1], (size_t)2);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("discards earlier effects in reverse order when merge is refused") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {
                .reject_on_prepare = 3u,
                .quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<createcall dest=\"'tel:123'\"/>"
                    "<join id1=\"'call-a'\" id2=\"'call-b'\"/>"
                    "<merge connectionid1=\"'call-a'\" "
                    "connectionid2=\"'call-b'\"/>"),
                CCXML_OK);
            check_equal(init_session(&session, &program, &probe), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event), CCXML_ADAPTER_ERROR);
            check_equal(probe.prepare_count, (size_t)3);
            check_equal(probe.commit_count, (size_t)0);
            check_equal(probe.discard_count, (size_t)2);
            check_equal(probe.discard_order[0], (size_t)2);
            check_equal(probe.discard_order[1], (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("accepts the unjoin adapter prefix for older programs") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_merge);

            check_equal(
                compile_program(
                    &program,
                    "<unjoin id1=\"'call-a'\" id2=\"'call-b'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &probe};
            check_equal(ccxml_session_init(&session, &config), CCXML_OK);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("requires the appended operation for a merge program") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_merge);

            check_equal(
                compile_program(
                    &program,
                    "<merge connectionid1=\"'call-a'\" "
                    "connectionid2=\"'call-b'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }

        it("rejects a null merge operation") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 incomplete = provider_adapter;
            ccxml_session_config config;
            incomplete.prepare_merge = NULL;

            check_equal(
                compile_program(
                    &program,
                    "<merge connectionid1=\"'call-a'\" "
                    "connectionid2=\"'call-b'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &incomplete,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }

        it("rejects a truncated merge callback field") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe probe = {.quiescent = true};
            ccxml_telephony_adapter_v1 truncated = provider_adapter;
            ccxml_session_config config;
            truncated.struct_size =
                offsetof(ccxml_telephony_adapter_v1, prepare_merge) +
                sizeof(truncated.prepare_merge) - 1u;

            check_equal(
                compile_program(
                    &program,
                    "<merge connectionid1=\"'call-a'\" "
                    "connectionid2=\"'call-b'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &truncated,
                .telephony_user = &probe};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }
    }

    group("createconference") {
        it("passes only the name to telephony and writes its returned ID") {
            char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='ccxml.loaded'>"
                "<createconference conferenceid='conference.id' "
                "confname=\"'support'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .quiescent = true,
                .conference_id_result = "conf-42",
                .conference_id_result_size = sizeof("conf-42") - 1u};
            datamodel_probe datamodel = {0};
            ccxml_event event = loaded_event();

            check_equal(
                compile_document(&program, source), CCXML_OK);
            memset(source, 'x', sizeof(source) - 1u);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(datamodel.validate_count, (size_t)1);
            check_equal(datamodel.location, "conference.id");
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(provider.prepare_count, (size_t)1);
            check_equal(
                provider.prepare_kinds[0],
                (size_t)PROVIDER_CREATE_CONFERENCE);
            check_equal(provider.conference_name, "support");
            check_equal(datamodel.prepare_count, (size_t)1);
            check_equal(datamodel.location, "conference.id");
            check_equal(datamodel.value, "conf-42");
            check_equal(provider.commit_count, (size_t)1);
            check_equal(datamodel.commit_count, (size_t)1);
            check_true(
                datamodel.commit_sequence <
                provider.create_conference_commit_sequence);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("passes an absent conference name as an empty view") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .quiescent = true,
                .conference_id_result = "conf-43",
                .conference_id_result_size = sizeof("conf-43") - 1u};
            datamodel_probe datamodel = {0};
            ccxml_event event = loaded_event();

            check_equal(
                compile_program(
                    &program,
                    "<createconference conferenceid='conference_id'/>") ,
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            event = alerting_event();
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(provider.conference_name_size, (size_t)0);
            check_equal(datamodel.value, "conf-43");

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rolls back the provider reservation when writeback is refused") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .quiescent = true,
                .conference_id_result = "conf-44",
                .conference_id_result_size = sizeof("conf-44") - 1u};
            datamodel_probe datamodel = {.reject_prepare = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<createconference conferenceid='conference_id'/>") ,
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(provider.commit_count, (size_t)0);
            check_equal(provider.discard_count, (size_t)1);
            check_equal(datamodel.commit_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects a malformed provider conference ID and discards its ticket") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {0};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<createconference conferenceid='conference_id'/>") ,
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_CONTRACT);
            check_equal(provider.discard_count, (size_t)1);
            check_equal(datamodel.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects a createconference session without a datamodel") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .quiescent = true,
                .conference_id_result = "conf-45",
                .conference_id_result_size = sizeof("conf-45") - 1u};

            check_equal(
                compile_program(
                    &program,
                    "<createconference conferenceid='conference_id'/>") ,
                CCXML_OK);
            check_equal(
                init_session(&session, &program, &provider),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }

        it("rejects a location refused during session validation") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .quiescent = true,
                .conference_id_result = "conf-46",
                .conference_id_result_size = sizeof("conf-46") - 1u};
            datamodel_probe datamodel = {.reject_validation = true};

            check_equal(
                compile_program(
                    &program,
                    "<createconference conferenceid='missing'/>") ,
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);
            check_equal(datamodel.validate_count, (size_t)1);

            ccxml_program_destroy(&program);
        }

        it("requires the appended telephony operation") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {0};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(
                    ccxml_telephony_adapter_v1,
                    prepare_create_conference);

            check_equal(
                compile_program(
                    &program,
                    "<createconference conferenceid='conference_id'/>") ,
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &provider,
                .datamodel = &datamodel_adapter,
                .datamodel_user = &datamodel};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);

            ccxml_program_destroy(&program);
        }

        it("requires a complete datamodel operation table") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {0};
            ccxml_datamodel_adapter_v1 truncated = datamodel_adapter;
            ccxml_session_config config;
            truncated.struct_size =
                offsetof(ccxml_datamodel_adapter_v1, prepare_assign_string) +
                sizeof(truncated.prepare_assign_string) - 1u;

            check_equal(
                compile_program(
                    &program,
                    "<createconference conferenceid='conference_id'/>") ,
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &provider_adapter,
                .telephony_user = &provider,
                .datamodel = &truncated,
                .datamodel_user = &datamodel};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);

            ccxml_program_destroy(&program);
        }
    }

    group("destroyconference") {
        it("passes a literal conference identifier without a datamodel") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<destroyconference "
                    "conferenceid=\"'conference-42'\"/>"),
                CCXML_OK);
            check_equal(
                init_session(&session, &program, &provider), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(provider.prepare_count, (size_t)1);
            check_equal(
                provider.prepare_kinds[0],
                (size_t)PROVIDER_DESTROY_CONFERENCE);
            check_equal(
                provider.destroyed_conference_id, "conference-42");
            check_equal(provider.commit_count, (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("reads a location value before preparing telephony") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {
                .read_result = "conference-from-model",
                .read_result_size = sizeof("conference-from-model") - 1u};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<destroyconference conferenceid='conference.id'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(datamodel.readable_validate_count, (size_t)1);
            check_equal(datamodel.location, "conference.id");
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(datamodel.read_count, (size_t)1);
            check_equal(
                provider.destroyed_conference_id,
                "conference-from-model");
            check_equal(provider.commit_count, (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("requires a datamodel only for location expressions") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};

            check_equal(
                compile_program(
                    &program,
                    "<destroyconference conferenceid='conference.id'/>"),
                CCXML_OK);
            check_equal(
                init_session(&session, &program, &provider),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }

        it("rejects an unreadable location during session initialization") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {
                .reject_readable_validation = true};

            check_equal(
                compile_program(
                    &program,
                    "<destroyconference conferenceid='conference.id'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);
            check_equal(datamodel.readable_validate_count, (size_t)1);

            ccxml_program_destroy(&program);
        }

        it("rolls back earlier effects when the datamodel read is refused") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {.reject_read = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<accept/><destroyconference "
                    "conferenceid='conference.id'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(provider.prepare_count, (size_t)1);
            check_equal(provider.commit_count, (size_t)0);
            check_equal(provider.discard_count, (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects an empty datamodel result before telephony prepare") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {0};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<destroyconference conferenceid='conference.id'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_CONTRACT);
            check_equal(provider.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rolls back earlier effects when telephony refuses destruction") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .reject_on_prepare = 2u,
                .quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<accept/><destroyconference "
                    "conferenceid=\"'conference-42'\"/>"),
                CCXML_OK);
            check_equal(
                init_session(&session, &program, &provider), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(provider.commit_count, (size_t)0);
            check_equal(provider.discard_count, (size_t)1);
            check_equal(provider.discard_order[0], (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects a malformed destroy ticket") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .malformed_ticket = true,
                .quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<destroyconference "
                    "conferenceid=\"'conference-42'\"/>"),
                CCXML_OK);
            check_equal(
                init_session(&session, &program, &provider), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_CONTRACT);
            check_equal(provider.commit_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("requires the appended telephony operation") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size = offsetof(
                ccxml_telephony_adapter_v1,
                prepare_destroy_conference);

            check_equal(
                compile_program(
                    &program,
                    "<destroyconference "
                    "conferenceid=\"'conference-42'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &provider};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);

            ccxml_program_destroy(&program);
        }

        it("requires the appended datamodel read operations") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {0};
            ccxml_datamodel_adapter_v1 legacy = datamodel_adapter;
            ccxml_session_config config;
            legacy.struct_size = offsetof(
                ccxml_datamodel_adapter_v1,
                validate_readable_string_location);

            check_equal(
                compile_program(
                    &program,
                    "<destroyconference conferenceid='conference.id'/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &provider_adapter,
                .telephony_user = &provider,
                .datamodel = &legacy,
                .datamodel_user = &datamodel};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);

            ccxml_program_destroy(&program);
        }

        it("preserves the old datamodel prefix for createconference") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .quiescent = true,
                .conference_id_result = "conference-43",
                .conference_id_result_size = sizeof("conference-43") - 1u};
            datamodel_probe datamodel = {0};
            ccxml_datamodel_adapter_v1 legacy = datamodel_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(ccxml_datamodel_adapter_v1, prepare_assign_string) +
                sizeof(legacy.prepare_assign_string);

            check_equal(
                compile_program(
                    &program,
                    "<createconference conferenceid='conference.id'/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &provider_adapter,
                .telephony_user = &provider,
                .datamodel = &legacy,
                .datamodel_user = &datamodel};
            check_equal(ccxml_session_init(&session, &config), CCXML_OK);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }
    }

    group("dialogprepare") {
        it("prepares detached VoiceXML and writes its ID before publication") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .quiescent = true,
                .dialog_id_result = "prepared-42",
                .dialog_id_result_size = sizeof("prepared-42") - 1u};
            datamodel_probe datamodel = {0};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<dialogprepare dialogid='dialog.prepared' "
                    "src=\"'app.vxml'\"/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(datamodel.validate_count, (size_t)1);
            check_equal(datamodel.location, "dialog.prepared");
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(provider.prepare_count, (size_t)1);
            check_equal(
                provider.prepare_kinds[0],
                (size_t)PROVIDER_DIALOG_PREPARE);
            check_equal(provider.dialog_source, "app.vxml");
            check_equal(
                provider.dialog_media_type,
                "application/voicexml+xml");
            check_equal(provider.dialog_connection_id_size, (size_t)0);
            check_equal(datamodel.prepare_count, (size_t)1);
            check_equal(datamodel.value, "prepared-42");
            check_equal(provider.commit_count, (size_t)1);
            check_equal(datamodel.commit_count, (size_t)1);
            check_true(
                datamodel.commit_sequence <
                provider.dialog_prepare_commit_sequence);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rolls back the provider reservation when writeback is refused") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .quiescent = true,
                .dialog_id_result = "prepared-43",
                .dialog_id_result_size = sizeof("prepared-43") - 1u};
            datamodel_probe datamodel = {.reject_prepare = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<dialogprepare dialogid='dialog.prepared' "
                    "src=\"'app.vxml'\"/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(provider.commit_count, (size_t)0);
            check_equal(provider.discard_count, (size_t)1);
            check_equal(datamodel.commit_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects a malformed provider dialog ID and discards its ticket") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {0};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<dialogprepare dialogid='dialog.prepared' "
                    "src=\"'app.vxml'\"/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_CONTRACT);
            check_equal(provider.discard_count, (size_t)1);
            check_equal(datamodel.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rolls back earlier effects when the provider refuses prepare") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .reject_on_prepare = 2u,
                .quiescent = true,
                .dialog_id_result = "prepared-44",
                .dialog_id_result_size = sizeof("prepared-44") - 1u};
            datamodel_probe datamodel = {0};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<accept/><dialogprepare dialogid='dialog.prepared' "
                    "src=\"'app.vxml'\"/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(provider.commit_count, (size_t)0);
            check_equal(provider.discard_count, (size_t)1);
            check_equal(provider.discard_order[0], (size_t)1);
            check_equal(datamodel.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects malformed provider and datamodel tickets") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .malformed_ticket = true,
                .quiescent = true,
                .dialog_id_result = "prepared-45",
                .dialog_id_result_size = sizeof("prepared-45") - 1u};
            datamodel_probe datamodel = {0};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<dialogprepare dialogid='dialog.prepared' "
                    "src=\"'app.vxml'\"/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_CONTRACT);
            check_equal(datamodel.prepare_count, (size_t)0);

            provider = (provider_probe){
                .quiescent = true,
                .dialog_id_result = "prepared-46",
                .dialog_id_result_size = sizeof("prepared-46") - 1u};
            datamodel = (datamodel_probe){.malformed_ticket = true};
            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_CONTRACT);
            check_equal(provider.discard_count, (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects a refused write location during initialization") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {.reject_validation = true};

            check_equal(
                compile_program(
                    &program,
                    "<dialogprepare dialogid='dialog.prepared' "
                    "src=\"'app.vxml'\"/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_INVALID_ARGUMENT);
            check_equal(datamodel.validate_count, (size_t)1);
            check_equal(provider.prepare_count, (size_t)0);

            ccxml_program_destroy(&program);
        }

        it("requires both its datamodel and appended provider operation") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {0};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;

            check_equal(
                compile_program(
                    &program,
                    "<dialogprepare dialogid='dialog.prepared' "
                    "src=\"'app.vxml'\"/>"),
                CCXML_OK);
            check_equal(
                init_session(&session, &program, &provider),
                CCXML_INVALID_ARGUMENT);
            legacy.struct_size = offsetof(
                ccxml_telephony_adapter_v1, prepare_dialog_prepare);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &provider,
                .datamodel = &datamodel_adapter,
                .datamodel_user = &datamodel};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);

            ccxml_program_destroy(&program);
        }

        it("preserves the provider prefix through dialogterminate") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(
                    ccxml_telephony_adapter_v1,
                    prepare_dialog_terminate) +
                sizeof(legacy.prepare_dialog_terminate);

            check_equal(
                compile_program(
                    &program,
                    "<dialogterminate dialogid=\"'dialog-42'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &provider};
            check_equal(ccxml_session_init(&session, &config), CCXML_OK);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }
    }

    group("dialogstart") {
        it("starts VoiceXML on the current connection and writes its ID") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .quiescent = true,
                .dialog_id_result = "dialog-42",
                .dialog_id_result_size = sizeof("dialog-42") - 1u};
            datamodel_probe datamodel = {0};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<dialogstart dialogid='dialog.id' "
                    "src=\"'app.vxml'\" "
                    "connectionid='event$.connectionid'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(datamodel.validate_count, (size_t)1);
            check_equal(datamodel.location, "dialog.id");
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(provider.prepare_count, (size_t)1);
            check_equal(
                provider.prepare_kinds[0],
                (size_t)PROVIDER_DIALOG_START);
            check_equal(provider.dialog_source, "app.vxml");
            check_equal(
                provider.dialog_media_type,
                "application/voicexml+xml");
            check_equal(provider.dialog_connection_id, "call-7");
            check_equal(datamodel.prepare_count, (size_t)1);
            check_equal(datamodel.value, "dialog-42");
            check_equal(provider.commit_count, (size_t)1);
            check_equal(datamodel.commit_count, (size_t)1);
            check_true(
                datamodel.commit_sequence <
                provider.dialog_start_commit_sequence);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rolls back the provider reservation when writeback is refused") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .quiescent = true,
                .dialog_id_result = "dialog-43",
                .dialog_id_result_size = sizeof("dialog-43") - 1u};
            datamodel_probe datamodel = {.reject_prepare = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<dialogstart dialogid='dialog.id' "
                    "src=\"'app.vxml'\" "
                    "connectionid='event$.connectionid'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(provider.commit_count, (size_t)0);
            check_equal(provider.discard_count, (size_t)1);
            check_equal(datamodel.commit_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects a missing current connection before provider prepare") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .quiescent = true,
                .dialog_id_result = "dialog-44",
                .dialog_id_result_size = sizeof("dialog-44") - 1u};
            datamodel_probe datamodel = {0};
            ccxml_event event = {
                .name = "connection.alerting",
                .name_size = sizeof("connection.alerting") - 1u};

            check_equal(
                compile_program(
                    &program,
                    "<dialogstart dialogid='dialog.id' "
                    "src=\"'app.vxml'\" "
                    "connectionid='event$.connectionid'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_EVENT);
            check_equal(provider.prepare_count, (size_t)0);
            check_equal(datamodel.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects a malformed provider dialog ID and discards its ticket") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {0};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<dialogstart dialogid='dialog.id' "
                    "src=\"'app.vxml'\" "
                    "connectionid='event$.connectionid'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_CONTRACT);
            check_equal(provider.discard_count, (size_t)1);
            check_equal(datamodel.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rolls back earlier effects when the provider refuses startup") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .reject_on_prepare = 2u,
                .quiescent = true,
                .dialog_id_result = "dialog-45",
                .dialog_id_result_size = sizeof("dialog-45") - 1u};
            datamodel_probe datamodel = {0};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<accept/><dialogstart dialogid='dialog.id' "
                    "src=\"'app.vxml'\" "
                    "connectionid='event$.connectionid'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(provider.commit_count, (size_t)0);
            check_equal(provider.discard_count, (size_t)1);
            check_equal(provider.discard_order[0], (size_t)1);
            check_equal(datamodel.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects a malformed provider ticket") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .malformed_ticket = true,
                .quiescent = true,
                .dialog_id_result = "dialog-46",
                .dialog_id_result_size = sizeof("dialog-46") - 1u};
            datamodel_probe datamodel = {0};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<dialogstart dialogid='dialog.id' "
                    "src=\"'app.vxml'\" "
                    "connectionid='event$.connectionid'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_CONTRACT);
            check_equal(datamodel.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("requires both its datamodel and appended provider operation") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {0};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;

            check_equal(
                compile_program(
                    &program,
                    "<dialogstart dialogid='dialog.id' "
                    "src=\"'app.vxml'\" "
                    "connectionid='event$.connectionid'/>"),
                CCXML_OK);
            check_equal(
                init_session(&session, &program, &provider),
                CCXML_INVALID_ARGUMENT);
            legacy.struct_size = offsetof(
                ccxml_telephony_adapter_v1, prepare_dialog_start);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &provider,
                .datamodel = &datamodel_adapter,
                .datamodel_user = &datamodel};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);

            ccxml_program_destroy(&program);
        }

        it("preserves the provider prefix through destroyconference") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(
                    ccxml_telephony_adapter_v1,
                    prepare_destroy_conference) +
                sizeof(legacy.prepare_destroy_conference);

            check_equal(
                compile_program(
                    &program,
                    "<destroyconference "
                    "conferenceid=\"'conference-42'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &provider};
            check_equal(ccxml_session_init(&session, &config), CCXML_OK);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }
    }

    group("prepared dialogstart") {
        it("reads the prepared ID and starts it on the current connection") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {
                .read_result = "prepared-42",
                .read_result_size = sizeof("prepared-42") - 1u};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<dialogstart prepareddialogid='dialog.prepared' "
                    "connectionid='event$.connectionid'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(datamodel.readable_validate_count, (size_t)1);
            check_equal(datamodel.location, "dialog.prepared");
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(datamodel.read_count, (size_t)1);
            check_equal(provider.prepare_count, (size_t)1);
            check_equal(
                provider.prepare_kinds[0],
                (size_t)PROVIDER_PREPARED_DIALOG_START);
            check_equal(provider.started_prepared_dialog_id, "prepared-42");
            check_equal(
                provider.started_prepared_dialog_id_size,
                sizeof("prepared-42") - 1u);
            check_equal(provider.prepared_dialog_connection_id, "call-7");
            check_equal(
                provider.prepared_dialog_connection_id_size,
                sizeof("call-7") - 1u);
            check_equal(provider.commit_count, (size_t)1);
            check_true(provider.prepared_dialog_start_commit_sequence > 0u);
            check_equal(datamodel.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects a missing connection before reading the datamodel") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {
                .read_result = "prepared-43",
                .read_result_size = sizeof("prepared-43") - 1u};
            ccxml_event event = alerting_event();
            event.connection_id = NULL;
            event.connection_id_size = 0u;

            check_equal(
                compile_program(
                    &program,
                    "<dialogstart prepareddialogid='dialog.prepared' "
                    "connectionid='event$.connectionid'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_EVENT);
            check_equal(datamodel.read_count, (size_t)0);
            check_equal(provider.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rolls back earlier effects when the datamodel read is refused") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {.reject_read = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<accept/><dialogstart "
                    "prepareddialogid='dialog.prepared' "
                    "connectionid='event$.connectionid'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(provider.prepare_count, (size_t)1);
            check_equal(provider.commit_count, (size_t)0);
            check_equal(provider.discard_count, (size_t)1);
            check_equal(provider.discard_order[0], (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects empty and embedded-NUL prepared IDs") {
            static const char embedded_nul[] = {'b', 'a', 'd', '\0', 'i', 'd'};
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {0};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<dialogstart prepareddialogid='dialog.prepared' "
                    "connectionid='event$.connectionid'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_CONTRACT);
            check_equal(provider.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            datamodel = (datamodel_probe){
                .read_result = embedded_nul,
                .read_result_size = sizeof(embedded_nul)};
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_CONTRACT);
            check_equal(provider.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rolls back earlier effects when the provider refuses startup") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .reject_on_prepare = 2u,
                .quiescent = true};
            datamodel_probe datamodel = {
                .read_result = "prepared-44",
                .read_result_size = sizeof("prepared-44") - 1u};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<accept/><dialogstart "
                    "prepareddialogid='dialog.prepared' "
                    "connectionid='event$.connectionid'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(provider.prepare_count, (size_t)2);
            check_equal(provider.commit_count, (size_t)0);
            check_equal(provider.discard_count, (size_t)1);
            check_equal(provider.discard_order[0], (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects a malformed provider ticket") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .malformed_ticket = true,
                .quiescent = true};
            datamodel_probe datamodel = {
                .read_result = "prepared-45",
                .read_result_size = sizeof("prepared-45") - 1u};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<dialogstart prepareddialogid='dialog.prepared' "
                    "connectionid='event$.connectionid'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_CONTRACT);
            check_equal(provider.commit_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("validates the readable location during initialization") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {
                .reject_readable_validation = true};

            check_equal(
                compile_program(
                    &program,
                    "<dialogstart prepareddialogid='dialog.prepared' "
                    "connectionid='event$.connectionid'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);
            check_equal(datamodel.readable_validate_count, (size_t)1);
            check_equal(datamodel.location, "dialog.prepared");

            ccxml_program_destroy(&program);
        }

        it("requires readable datamodel and appended provider capabilities") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {0};
            ccxml_telephony_adapter_v1 legacy_provider = provider_adapter;
            ccxml_datamodel_adapter_v1 legacy_datamodel = datamodel_adapter;
            ccxml_session_config config;

            check_equal(
                compile_program(
                    &program,
                    "<dialogstart prepareddialogid='dialog.prepared' "
                    "connectionid='event$.connectionid'/>"),
                CCXML_OK);
            check_equal(
                init_session(&session, &program, &provider),
                CCXML_INVALID_ARGUMENT);

            legacy_provider.struct_size = offsetof(
                ccxml_telephony_adapter_v1,
                prepare_prepared_dialog_start);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy_provider,
                .telephony_user = &provider,
                .datamodel = &datamodel_adapter,
                .datamodel_user = &datamodel};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);

            legacy_datamodel.struct_size = offsetof(
                ccxml_datamodel_adapter_v1,
                validate_readable_string_location);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &provider_adapter,
                .telephony_user = &provider,
                .datamodel = &legacy_datamodel,
                .datamodel_user = &datamodel};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);

            ccxml_program_destroy(&program);
        }

        it("preserves the provider prefix through dialogprepare") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {0};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(
                    ccxml_telephony_adapter_v1,
                    prepare_dialog_prepare) +
                sizeof(legacy.prepare_dialog_prepare);

            check_equal(
                compile_program(
                    &program,
                    "<dialogprepare dialogid='dialog.prepared' "
                    "src=\"'app.vxml'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &provider,
                .datamodel = &datamodel_adapter,
                .datamodel_user = &datamodel};
            check_equal(ccxml_session_init(&session, &config), CCXML_OK);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }
    }

    group("dialogterminate") {
        it("commits a literal dialog identifier in normal mode") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<dialogterminate dialogid=\"'dialog-42'\"/>"),
                CCXML_OK);
            check_equal(
                init_session(&session, &program, &provider), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(provider.prepare_count, (size_t)1);
            check_equal(
                provider.prepare_kinds[0],
                (size_t)PROVIDER_DIALOG_TERMINATE);
            check_equal(provider.terminated_dialog_id, "dialog-42");
            check_false(provider.dialog_terminate_immediate);
            check_equal(provider.commit_count, (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("reads a dialog identifier location before provider prepare") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {
                .read_result = "dialog-from-model",
                .read_result_size = sizeof("dialog-from-model") - 1u};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<dialogterminate dialogid='dialog.id'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(datamodel.readable_validate_count, (size_t)1);
            check_equal(datamodel.location, "dialog.id");
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(datamodel.read_count, (size_t)1);
            check_equal(
                provider.terminated_dialog_id,
                "dialog-from-model");
            check_false(provider.dialog_terminate_immediate);
            check_equal(provider.commit_count, (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("requires a datamodel only for location expressions") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};

            check_equal(
                compile_program(
                    &program,
                    "<dialogterminate dialogid='dialog.id'/>"),
                CCXML_OK);
            check_equal(
                init_session(&session, &program, &provider),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);

            ccxml_program_destroy(&program);
        }

        it("rejects an unreadable location during session initialization") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {
                .reject_readable_validation = true};

            check_equal(
                compile_program(
                    &program,
                    "<dialogterminate dialogid='dialog.id'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_INVALID_ARGUMENT);
            check_null(session.impl);
            check_equal(datamodel.readable_validate_count, (size_t)1);

            ccxml_program_destroy(&program);
        }

        it("rolls back earlier effects when the datamodel read is refused") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {.reject_read = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<accept/><dialogterminate dialogid='dialog.id'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(provider.prepare_count, (size_t)1);
            check_equal(provider.commit_count, (size_t)0);
            check_equal(provider.discard_count, (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects an empty datamodel result before provider prepare") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            datamodel_probe datamodel = {0};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<dialogterminate dialogid='dialog.id'/>"),
                CCXML_OK);
            check_equal(
                init_session_with_datamodel(
                    &session, &program, &provider, &datamodel),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_CONTRACT);
            check_equal(provider.prepare_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rolls back earlier effects when the provider refuses termination") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .reject_on_prepare = 2u,
                .quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<accept/><dialogterminate "
                    "dialogid=\"'dialog-42'\"/>"),
                CCXML_OK);
            check_equal(
                init_session(&session, &program, &provider), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(provider.commit_count, (size_t)0);
            check_equal(provider.discard_count, (size_t)1);
            check_equal(provider.discard_order[0], (size_t)1);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects a malformed provider ticket") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .malformed_ticket = true,
                .quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<dialogterminate dialogid=\"'dialog-42'\"/>"),
                CCXML_OK);
            check_equal(
                init_session(&session, &program, &provider), CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_CONTRACT);
            check_equal(provider.commit_count, (size_t)0);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("commits mixed effects in document order") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_program(
                    &program,
                    "<accept/><dialogterminate "
                    "dialogid=\"'dialog-42'\"/>"),
                CCXML_OK);
            check_equal(
                init_session(&session, &program, &provider), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(provider.commit_count, (size_t)2);
            check_equal(provider.commit_order[0], (size_t)1);
            check_equal(provider.commit_order[1], (size_t)2);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("requires its appended provider operation") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {.quiescent = true};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size = offsetof(
                ccxml_telephony_adapter_v1,
                prepare_dialog_terminate);

            check_equal(
                compile_program(
                    &program,
                    "<dialogterminate dialogid=\"'dialog-42'\"/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &provider};
            check_equal(
                ccxml_session_init(&session, &config),
                CCXML_INVALID_ARGUMENT);

            ccxml_program_destroy(&program);
        }

        it("preserves the provider prefix through dialogstart") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            provider_probe provider = {
                .quiescent = true,
                .dialog_id_result = "dialog-47",
                .dialog_id_result_size = sizeof("dialog-47") - 1u};
            datamodel_probe datamodel = {0};
            ccxml_telephony_adapter_v1 legacy = provider_adapter;
            ccxml_session_config config;
            legacy.struct_size =
                offsetof(
                    ccxml_telephony_adapter_v1,
                    prepare_dialog_start) +
                sizeof(legacy.prepare_dialog_start);

            check_equal(
                compile_program(
                    &program,
                    "<dialogstart dialogid='dialog.id' "
                    "src=\"'app.vxml'\" "
                    "connectionid='event$.connectionid'/>"),
                CCXML_OK);
            config = (ccxml_session_config){
                .program = &program,
                .telephony = &legacy,
                .telephony_user = &provider,
                .datamodel = &datamodel_adapter,
                .datamodel_user = &datamodel};
            check_equal(ccxml_session_init(&session, &config), CCXML_OK);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }
    }
}
