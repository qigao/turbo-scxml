#include <ccxml/ccxml.h>

#include "ccxml_internal.h"
#include "tinytest.h"

#include <stdio.h>
#include <string.h>

typedef struct effect_probe effect_probe;

typedef struct effect_ticket {
    effect_probe *owner;
    size_t ordinal;
    bool live;
} effect_ticket;

struct effect_probe {
    effect_ticket tickets[4];
    size_t prepare_count;
    size_t commit_count;
    size_t discard_count;
    size_t commit_sequence[4];
    size_t discard_sequence[4];
    bool reject;
    size_t reject_at;
    bool malformed;
    bool quiescent;
    size_t close_count;
};

typedef struct send_probe {
    effect_probe effects;
    char event[64];
    size_t event_size;
    char target[128];
    size_t target_size;
    char type[64];
    size_t type_size;
    char id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    size_t id_size;
    char ids[4][SCXML_EVENT_METADATA_CAPACITY + 1u];
    size_t id_sizes[4];
    size_t send_count;
    char cancel_id[SCXML_EVENT_METADATA_CAPACITY + 1u];
    size_t cancel_id_size;
    size_t cancel_count;
    uint64_t delay_ms;
    scxml_payload_kind payload_kind;
} send_probe;

typedef struct datamodel_probe {
    effect_probe effects;
    char value[SCXML_EVENT_METADATA_CAPACITY + 1u];
    size_t value_size;
    char pending[SCXML_EVENT_METADATA_CAPACITY + 1u];
    size_t pending_size;
    bool reject_validation;
    bool reject_read;
} datamodel_probe;

static size_t transaction_sequence;

static void effect_commit(void *user) {
    effect_ticket *ticket = (effect_ticket *)user;
    if (ticket == NULL || !ticket->live) return;
    ticket->live = false;
    ticket->owner->commit_sequence[ticket->owner->commit_count++] =
        ++transaction_sequence;
}

static void effect_discard(void *user) {
    effect_ticket *ticket = (effect_ticket *)user;
    if (ticket == NULL || !ticket->live) return;
    ticket->live = false;
    ticket->owner->discard_sequence[ticket->owner->discard_count++] =
        ++transaction_sequence;
}

static scxml_adapter_status prepare_effect(
    effect_probe *probe, cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    const size_t index = probe->prepare_count++;
    effect_ticket *ticket;
    if (out_error != NULL) *out_error = NULL;
    if (probe->reject ||
        (probe->reject_at != 0u && probe->prepare_count == probe->reject_at)) {
        if (out_error != NULL) *out_error = "test adapter rejection";
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    if (probe->malformed) {
        *out_ticket = (cflow_statechart_effect_ticket){0};
        return SCXML_ADAPTER_ACCEPTED;
    }
    ticket = &probe->tickets[index];
    ticket->owner = probe;
    ticket->ordinal = index + 1u;
    ticket->live = true;
    *out_ticket = (cflow_statechart_effect_ticket){
        .commit = effect_commit, .discard = effect_discard, .user = ticket};
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status prepare_accept(
    void *user, const ccxml_accept_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    effect_probe *probe = (effect_probe *)user;
    (void)request;
    return prepare_effect(probe, out_ticket, out_error);
}

static void effect_close(void *user) {
    effect_probe *probe = (effect_probe *)user;
    ++probe->close_count;
}

static bool effect_is_quiescent(void *user) {
    const effect_probe *probe = (const effect_probe *)user;
    return probe->quiescent;
}

static const ccxml_telephony_adapter_v1 telephony_adapter = {
    .abi_version = CCXML_TELEPHONY_ADAPTER_ABI_V1,
    .struct_size = sizeof(ccxml_telephony_adapter_v1),
    .prepare_accept = prepare_accept,
    .close = effect_close,
    .is_quiescent = effect_is_quiescent};

static scxml_adapter_status prepare_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    send_probe *probe = (send_probe *)user;
    probe->event_size = request->event_size;
    memcpy(probe->event, request->event, request->event_size);
    probe->event[request->event_size] = '\0';
    probe->target_size = request->target_size;
    memcpy(probe->target, request->target, request->target_size);
    probe->target[request->target_size] = '\0';
    probe->type_size = request->type_size;
    memcpy(probe->type, request->type, request->type_size);
    probe->type[request->type_size] = '\0';
    probe->id_size = request->id_size;
    if (request->id_size != 0u) {
        memcpy(probe->id, request->id, request->id_size);
        probe->id[request->id_size] = '\0';
    }
    if (probe->send_count < 4u) {
        probe->id_sizes[probe->send_count] = request->id_size;
        if (request->id_size != 0u) {
            memcpy(
                probe->ids[probe->send_count], request->id,
                request->id_size);
            probe->ids[probe->send_count][request->id_size] = '\0';
        }
    }
    ++probe->send_count;
    probe->delay_ms = request->delay_ms;
    probe->payload_kind = request->payload.kind;
    return prepare_effect(&probe->effects, out_ticket, out_error);
}

static scxml_adapter_status prepare_cancel(
    void *user, const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    send_probe *probe = (send_probe *)user;
    probe->cancel_id_size = request->send_id_size;
    if (request->send_id_size <= SCXML_EVENT_METADATA_CAPACITY) {
        memcpy(
            probe->cancel_id, request->send_id, request->send_id_size);
        probe->cancel_id[request->send_id_size] = '\0';
    }
    ++probe->cancel_count;
    return prepare_effect(&probe->effects, out_ticket, out_error);
}

static void send_close(void *user) {
    send_probe *probe = (send_probe *)user;
    ++probe->effects.close_count;
}

static bool send_is_quiescent(void *user) {
    const send_probe *probe = (const send_probe *)user;
    return probe->effects.quiescent;
}

static const scxml_event_io_adapter event_io_adapter = {
    .abi_version = SCXML_ADAPTER_ABI,
    .struct_size = sizeof(scxml_event_io_adapter),
    .capabilities = SCXML_EVENT_IO_CAP_SEND,
    .prepare_send = prepare_send,
    .close = send_close,
    .is_quiescent = send_is_quiescent};

static scxml_adapter_status validate_string_location(
    void *user, const char *location, size_t location_size,
    const char **out_error) {
    datamodel_probe *probe = (datamodel_probe *)user;
    if (out_error != NULL) *out_error = NULL;
    if (probe->reject_validation ||
        location_size != sizeof("request.pending") - 1u ||
        memcmp(location, "request.pending", location_size) != 0) {
        if (out_error != NULL) *out_error = "invalid test location";
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status validate_readable_string_location(
    void *user, const char *location, size_t location_size,
    const char **out_error) {
    return validate_string_location(
        user, location, location_size, out_error);
}

static void datamodel_commit(void *user) {
    effect_ticket *ticket = (effect_ticket *)user;
    datamodel_probe *probe;
    if (ticket == NULL || ticket->owner == NULL || !ticket->live) return;
    probe = (datamodel_probe *)ticket->owner;
    memcpy(probe->value, probe->pending, probe->pending_size);
    probe->value[probe->pending_size] = '\0';
    probe->value_size = probe->pending_size;
    effect_commit(user);
}

static scxml_adapter_status prepare_assign_string(
    void *user, const char *location, size_t location_size,
    const char *value, size_t value_size,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    datamodel_probe *probe = (datamodel_probe *)user;
    scxml_adapter_status status;
    if (validate_string_location(
            user, location, location_size, out_error) !=
            SCXML_ADAPTER_ACCEPTED ||
        value == NULL || value_size > SCXML_EVENT_METADATA_CAPACITY)
        return SCXML_ADAPTER_ERROR_EXECUTION;
    memcpy(probe->pending, value, value_size);
    probe->pending[value_size] = '\0';
    probe->pending_size = value_size;
    status = prepare_effect(&probe->effects, out_ticket, out_error);
    if (status == SCXML_ADAPTER_ACCEPTED)
        out_ticket->commit = datamodel_commit;
    return status;
}

static scxml_adapter_status read_string(
    void *user, const char *location, size_t location_size,
    ccxml_string_view *out_value, const char **out_error) {
    datamodel_probe *probe = (datamodel_probe *)user;
    if (probe->reject_read || out_value == NULL ||
        validate_string_location(
            user, location, location_size, out_error) !=
            SCXML_ADAPTER_ACCEPTED)
        return SCXML_ADAPTER_ERROR_EXECUTION;
    *out_value = (ccxml_string_view){probe->value, probe->value_size};
    return SCXML_ADAPTER_ACCEPTED;
}

static const ccxml_datamodel_adapter_v1 datamodel_adapter = {
    .abi_version = CCXML_DATAMODEL_ADAPTER_ABI_V1,
    .struct_size = sizeof(ccxml_datamodel_adapter_v1),
    .validate_string_location = validate_string_location,
    .prepare_assign_string = prepare_assign_string,
    .validate_readable_string_location =
        validate_readable_string_location,
    .read_string = read_string};

static scxml_adapter_status reject_condition_compile(
    void *user, const char *source, size_t source_size,
    ccxml_condition *out_condition, const char **out_error) {
    (void)user;
    (void)source;
    (void)source_size;
    (void)out_condition;
    if (out_error != NULL) *out_error = "test condition compile rejection";
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

static scxml_adapter_status unused_condition_evaluate(
    void *user, const ccxml_condition *condition,
    const ccxml_event *event, bool *out_value, const char **out_error) {
    (void)user;
    (void)condition;
    (void)event;
    (void)out_value;
    (void)out_error;
    return SCXML_ADAPTER_INVALID_CONTRACT;
}

static void unused_condition_destroy(
    void *user, ccxml_condition *condition) {
    (void)user;
    (void)condition;
}

static const ccxml_datamodel_adapter_v1 rejecting_condition_adapter = {
    .abi_version = CCXML_DATAMODEL_ADAPTER_ABI_V1,
    .struct_size = sizeof(ccxml_datamodel_adapter_v1),
    .compile_condition = reject_condition_compile,
    .evaluate_condition = unused_condition_evaluate,
    .destroy_condition = unused_condition_destroy};

static ccxml_status compile_actions(
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

static ccxml_status init_send_session(
    ccxml_session *session, const ccxml_program *program,
    effect_probe *telephony, send_probe *send,
    const scxml_event_io_adapter *adapter) {
    const ccxml_session_config config = {
        .program = program,
        .telephony = &telephony_adapter,
        .telephony_user = telephony,
        .event_io = adapter,
        .event_io_user = send};
    return ccxml_session_init(session, &config);
}

static ccxml_status init_send_session_with_datamodel(
    ccxml_session *session, const ccxml_program *program,
    effect_probe *telephony, datamodel_probe *datamodel, send_probe *send,
    const scxml_event_io_adapter *adapter) {
    const ccxml_session_config config = {
        .program = program,
        .telephony = &telephony_adapter,
        .telephony_user = telephony,
        .datamodel = &datamodel_adapter,
        .datamodel_user = datamodel,
        .event_io = adapter,
        .event_io_user = send};
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

spec("CCXML send") {
    group("compiler") {
        it("accepts literal target name and targettype") {
            ccxml_program program = {0};
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" "
                    "targettype=\"'dialog'\"/>"),
                CCXML_OK);
            check_equal(ccxml_program_action_count(&program), (size_t)1);
            ccxml_program_destroy(&program);
        }

        it("accepts an omitted targettype") {
            ccxml_program program = {0};
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\"/>"),
                CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("accepts a dotted sendid location") {
            ccxml_program program = {0};
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" sendid='request.pending'/>"),
                CCXML_OK);
            ccxml_program_destroy(&program);
            program = (ccxml_program){0};
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" "
                    "sendid='request&#46;pending'/>"),
                CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("accepts cancel identifiers from a location or literal") {
            ccxml_program program = {0};
            check_equal(
                compile_actions(
                    &program, "<cancel sendid='request.pending'/>"),
                CCXML_OK);
            ccxml_program_destroy(&program);
            program = (ccxml_program){0};
            check_equal(
                compile_actions(
                    &program,
                    "<cancel sendid='&apos;send.fixed.7&apos;'/>"),
                CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects invalid sendid locations") {
            ccxml_program program = {0};
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" sendid=''/>"),
                CCXML_INVALID_STRUCTURE);
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" sendid=\"'request.id'\"/>"),
                CCXML_UNSUPPORTED_FEATURE);
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" sendid='request..id'/>"),
                CCXML_UNSUPPORTED_FEATURE);
            check_equal(
                compile_actions(
                    &program,
                    "<send xmlns:x='urn:test' target=\"'session:callee'\" "
                    "name=\"'call.notice'\" x:sendid='request.id'/>"),
                CCXML_UNSUPPORTED_FEATURE);
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" sendid='request.id' "
                    "sendid='request.other'/>"),
                CCXML_XML_ERROR);
        }

        it("rejects malformed cancel actions") {
            ccxml_program program = {0};
            check_equal(
                compile_actions(&program, "<cancel/>"),
                CCXML_INVALID_STRUCTURE);
            check_equal(
                compile_actions(&program, "<cancel sendid=''/>"),
                CCXML_INVALID_STRUCTURE);
            check_equal(
                compile_actions(
                    &program, "<cancel sendid='&apos;&apos;'/>"),
                CCXML_INVALID_STRUCTURE);
            check_equal(
                compile_actions(&program, "<cancel sendid='request..id'/>"),
                CCXML_UNSUPPORTED_FEATURE);
            check_equal(
                compile_actions(&program, "<cancel sendid='makeId()'/>"),
                CCXML_UNSUPPORTED_FEATURE);
            check_equal(
                compile_actions(
                    &program,
                    "<cancel xmlns:x='urn:test' "
                    "x:sendid=\"'send.fixed.7'\"/>"),
                CCXML_UNSUPPORTED_FEATURE);
            check_equal(
                compile_actions(
                    &program,
                    "<cancel sendid=\"'send.fixed.7'\"><accept/></cancel>"),
                CCXML_UNSUPPORTED_FEATURE);
            check_equal(
                compile_actions(
                    &program,
                    "<cancel sendid=\"'send.fixed.7'\" "
                    "sendid=\"'send.fixed.8'\"/>"),
                CCXML_XML_ERROR);
        }

        it("rejects missing required attributes") {
            ccxml_program program = {0};
            check_equal(
                compile_actions(&program, "<send name=\"'call.notice'\"/>"),
                CCXML_INVALID_STRUCTURE);
            check_equal(
                compile_actions(
                    &program, "<send target=\"'session:callee'\"/>"),
                CCXML_INVALID_STRUCTURE);
        }

        it("rejects empty and nonliteral values") {
            ccxml_program program = {0};
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"''\" name=\"'call.notice'\"/>"),
                CCXML_INVALID_STRUCTURE);
            check_equal(
                compile_actions(
                    &program,
                    "<send target='destination' name=\"'call.notice'\"/>"),
                CCXML_UNSUPPORTED_FEATURE);
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" name=\"''\"/>"),
                CCXML_INVALID_STRUCTURE);
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" targettype=\"''\"/>"),
                CCXML_INVALID_STRUCTURE);
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" targettype='processorType'/>"),
                CCXML_UNSUPPORTED_FEATURE);
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" name='eventName'/>"),
                CCXML_UNSUPPORTED_FEATURE);
        }

        it("rejects invalid event names") {
            ccxml_program program = {0};
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" name=\"'1notice'\"/>"),
                CCXML_INVALID_STRUCTURE);
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" name=\"'call-notice'\"/>"),
                CCXML_INVALID_STRUCTURE);
        }

        it("accepts literal CSS time delays") {
            static const char *const values[] = {
                "'250ms'", "'1s'", "'1.5s'", "'.5s'", "'+1.5s'",
                "'0s'", "&apos;.5s&apos;", "'18446744073709551615ms'",
                "'18446744073709551.615s'"};
            size_t index;
            for (index = 0u; index < sizeof(values) / sizeof(values[0]);
                 ++index) {
                char action[256];
                ccxml_program program = {0};
                const int written = snprintf(
                    action, sizeof(action),
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" delay=\"%s\"/>",
                    values[index]);
                check_true(written > 0 && (size_t)written < sizeof(action));
                check_equal(compile_actions(&program, action), CCXML_OK);
                ccxml_program_destroy(&program);
            }
        }

        it("rejects malformed literal CSS time delays") {
            static const char *const values[] = {
                "''", "'-1s'", "'1'", "'1m'", "'1.0001s'", "'.s'",
                "'+'", "'18446744073709552s'",
                "'18446744073709551.616s'"};
            size_t index;
            for (index = 0u; index < sizeof(values) / sizeof(values[0]);
                 ++index) {
                char action[256];
                ccxml_program program = {0};
                const int written = snprintf(
                    action, sizeof(action),
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" delay=\"%s\"/>",
                    values[index]);
                check_true(written > 0 && (size_t)written < sizeof(action));
                check_equal(
                    compile_actions(&program, action),
                    CCXML_INVALID_STRUCTURE);
            }
        }

        it("rejects deferred attributes and inline content") {
            ccxml_program program = {0};
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" name=\"'call.notice'\" "
                    "delay='dynamic'/>"),
                CCXML_UNSUPPORTED_FEATURE);
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" name=\"'call.notice'\" "
                    "namelist='payload'/>"),
                CCXML_UNSUPPORTED_FEATURE);
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" name=\"'call.notice'\" "
                    "custom='value'/>"),
                CCXML_UNSUPPORTED_FEATURE);
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" name=\"'call.notice'\">"
                    "<payload/></send>"),
                CCXML_UNSUPPORTED_FEATURE);
        }

        it("charges retained send fields to the name-byte limit") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='go'>"
                "<send target=\"'session:callee'\" name=\"'call.notice'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_limits limits = ccxml_default_limits();
            ccxml_diagnostic diagnostic = {0};
            ccxml_program program = {0};
            limits.max_name_bytes = 4u;
            check_equal(
                ccxml_compile(
                    &program, source, strlen(source), &limits, &diagnostic),
                CCXML_LIMIT_EXCEEDED);
        }

        it("charges decoded rather than encoded bytes to the name limit") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='go'>"
                "<send target=\"'a&amp;b'\" name=\"'e&#46;x'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_limits limits = ccxml_default_limits();
            ccxml_diagnostic diagnostic = {0};
            ccxml_program program = {0};
            limits.max_name_bytes = 11u;
            check_equal(
                ccxml_compile(
                    &program, source, strlen(source), &limits, &diagnostic),
                CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("charges an explicit delay to the retained-byte limit") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='go'>"
                "<send target=\"'session:callee'\" name=\"'call.notice'\" "
                "delay=\"'250ms'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_limits limits = ccxml_default_limits();
            ccxml_diagnostic diagnostic = {0};
            ccxml_program program = {0};

            limits.max_name_bytes = 35u;
            check_equal(
                ccxml_compile(
                    &program, source, strlen(source), &limits, &diagnostic),
                CCXML_LIMIT_EXCEEDED);
            limits.max_name_bytes = 36u;
            check_equal(
                ccxml_compile(
                    &program, source, strlen(source), &limits, &diagnostic),
                CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("charges sendid and cancel operands to the retained-byte limit") {
            const char *send_source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.alerting'>"
                "<send target=\"'session:callee'\" name=\"'call.notice'\" "
                "sendid='request.pending'/>"
                "</transition></eventprocessor></ccxml>";
            const char *cancel_source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.alerting'>"
                "<cancel sendid=\"'send.fixed.7'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_limits limits = ccxml_default_limits();
            ccxml_diagnostic diagnostic = {0};
            ccxml_program program = {0};

            limits.max_name_bytes = 62u;
            check_equal(
                ccxml_compile(
                    &program, send_source, strlen(send_source),
                    &limits, &diagnostic),
                CCXML_LIMIT_EXCEEDED);
            limits.max_name_bytes = 63u;
            check_equal(
                ccxml_compile(
                    &program, send_source, strlen(send_source),
                    &limits, &diagnostic),
                CCXML_OK);
            ccxml_program_destroy(&program);

            limits.max_name_bytes = 32u;
            check_equal(
                ccxml_compile(
                    &program, cancel_source, strlen(cancel_source),
                    &limits, &diagnostic),
                CCXML_LIMIT_EXCEEDED);
            limits.max_name_bytes = 33u;
            check_equal(
                ccxml_compile(
                    &program, cancel_source, strlen(cancel_source),
                    &limits, &diagnostic),
                CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects a cancel literal above Event I/O metadata capacity") {
            char identifier[SCXML_EVENT_METADATA_CAPACITY + 2u];
            char action[SCXML_EVENT_METADATA_CAPACITY + 64u];
            ccxml_program program = {0};
            int written;

            memset(identifier, 'a', SCXML_EVENT_METADATA_CAPACITY + 1u);
            identifier[SCXML_EVENT_METADATA_CAPACITY + 1u] = '\0';
            written = snprintf(
                action, sizeof(action),
                "<cancel sendid=\"'%s'\"/>", identifier);
            check_true(written > 0 && (size_t)written < sizeof(action));
            check_equal(
                compile_actions(&program, action), CCXML_LIMIT_EXCEEDED);
        }

        it("rejects delimiters and backslashes introduced by XML entities") {
            ccxml_program program = {0};
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call&apos;notice'\"/>"),
                CCXML_UNSUPPORTED_FEATURE);
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call&#92;notice'\"/>"),
                CCXML_UNSUPPORTED_FEATURE);
        }
    }

    group("runtime") {
        it("requires a writable sendid location") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            send_probe send = {.effects.quiescent = true};
            datamodel_probe datamodel = {
                .effects.quiescent = true,
                .reject_validation = true};
            ccxml_status status;

            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" sendid='request.pending'/>"),
                CCXML_OK);
            status = init_send_session(
                &session, &program, &telephony, &send, &event_io_adapter);
            check_equal(status, CCXML_INVALID_ARGUMENT);
            if (status == CCXML_OK)
                check_equal(ccxml_session_destroy(&session), CCXML_OK);
            session = (ccxml_session){0};
            status = init_send_session_with_datamodel(
                &session, &program, &telephony, &datamodel, &send,
                &event_io_adapter);
            check_equal(status, CCXML_INVALID_ARGUMENT);
            if (status == CCXML_OK)
                check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("requires cancel capability") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            send_probe send = {.effects.quiescent = true};
            scxml_event_io_adapter capable = event_io_adapter;
            ccxml_status status;

            capable.capabilities |= SCXML_EVENT_IO_CAP_DELAYED_SEND |
                SCXML_EVENT_IO_CAP_CANCEL;
            capable.prepare_cancel = prepare_cancel;
            check_equal(
                compile_actions(
                    &program,
                    "<cancel sendid=\"'send&#46;fixed&#46;7'\"/>"),
                CCXML_OK);
            status = init_send_session(
                &session, &program, &telephony, &send, &event_io_adapter);
            check_equal(status, CCXML_INVALID_ARGUMENT);
            if (status == CCXML_OK)
                check_equal(ccxml_session_destroy(&session), CCXML_OK);
            session = (ccxml_session){0};
            check_equal(
                init_send_session(
                    &session, &program, &telephony, &send, &capable),
                CCXML_OK);
            if (session.impl != NULL)
                check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("maps cancel rejection and malformed tickets") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            send_probe send = {
                .effects = {.reject = true, .quiescent = true}};
            scxml_event_io_adapter capable = event_io_adapter;
            ccxml_event event = alerting_event();

            capable.capabilities |= SCXML_EVENT_IO_CAP_DELAYED_SEND |
                SCXML_EVENT_IO_CAP_CANCEL;
            capable.prepare_cancel = prepare_cancel;
            check_equal(
                compile_actions(
                    &program, "<cancel sendid=\"'send.fixed.7'\"/>"),
                CCXML_OK);
            check_equal(
                init_send_session(
                    &session, &program, &telephony, &send, &capable),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(ccxml_session_destroy(&session), CCXML_OK);

            session = (ccxml_session){0};
            send = (send_probe){
                .effects = {.malformed = true, .quiescent = true}};
            check_equal(
                init_send_session(
                    &session, &program, &telephony, &send, &capable),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_CONTRACT);
            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("writes a generated sendid before send commit and cancels it later") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' "
                "version='1.0'><eventprocessor>"
                "<transition event='connection.alerting'>"
                "<send target=\"'session:callee'\" name=\"'call.notice'\" "
                "delay=\"'250ms'\" sendid='request&#46;pending'/>"
                "</transition><transition event='cancel.request'>"
                "<cancel sendid='request&#46;pending'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            datamodel_probe datamodel = {.effects.quiescent = true};
            send_probe send = {.effects.quiescent = true};
            scxml_event_io_adapter capable = event_io_adapter;
            ccxml_event event = alerting_event();
            const ccxml_event cancel_event = {
                .name = "cancel.request",
                .name_size = sizeof("cancel.request") - 1u};

            capable.capabilities |= SCXML_EVENT_IO_CAP_DELAYED_SEND |
                SCXML_EVENT_IO_CAP_CANCEL;
            capable.prepare_cancel = prepare_cancel;
            check_equal(
                ccxml_compile(
                    &program, source, strlen(source), NULL, &diagnostic),
                CCXML_OK);
            check_equal(
                init_send_session_with_datamodel(
                    &session, &program, &telephony, &datamodel, &send,
                    &capable),
                CCXML_OK);
            transaction_sequence = 0u;
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_true(send.id_size > sizeof("send.") - 1u);
            check_equal(send.id_size, (size_t)43u);
            check_equal(memcmp(send.id, "send.", 5u), 0);
            check_equal(send.id[13], '-');
            check_equal(send.id[18], '-');
            check_equal(send.id[23], '-');
            check_equal(send.id[28], '-');
            check_equal(send.id[41], '.');
            check_equal(send.id[42], '1');
            check_equal(send.id, datamodel.value);
            check_equal(send.id_size, datamodel.value_size);
            check_equal(datamodel.effects.commit_sequence[0], (size_t)1u);
            check_equal(send.effects.commit_sequence[0], (size_t)2u);
            check_equal(send.delay_ms, UINT64_C(250));

            check_equal(
                ccxml_session_dispatch(&session, &cancel_event), CCXML_OK);
            check_equal(send.cancel_count, (size_t)1u);
            check_equal(send.cancel_id, datamodel.value);
            check_equal(send.cancel_id_size, datamodel.value_size);
            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("generates a distinct sendid for every dispatch") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            datamodel_probe datamodel = {.effects.quiescent = true};
            send_probe send = {.effects.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" sendid='request.pending'/>"),
                CCXML_OK);
            check_equal(
                init_send_session_with_datamodel(
                    &session, &program, &telephony, &datamodel, &send,
                    &event_io_adapter),
                CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(send.send_count, (size_t)2u);
            check_true(send.id_sizes[0] != 0u);
            check_equal(send.id_sizes[0], send.id_sizes[1]);
            check_true(
                memcmp(send.ids[0], send.ids[1], send.id_sizes[0]) != 0);
            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("consumes failed tokens and permanently exhausts at UINT64_MAX") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            datamodel_probe datamodel = {.effects.quiescent = true};
            send_probe send = {
                .effects = {.reject = true, .quiescent = true}};
            ccxml_event event = alerting_event();
            ccxml_session_impl *impl;

            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" sendid='request.pending'/>"),
                CCXML_OK);
            check_equal(
                init_send_session_with_datamodel(
                    &session, &program, &telephony, &datamodel, &send,
                    &event_io_adapter),
                CCXML_OK);
            impl = (ccxml_session_impl *)session.impl;
            check_equal(impl->next_send_token, UINT64_C(1));
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(impl->next_send_token, UINT64_C(2));
            check_equal(send.id[send.id_size - 1u], '1');
            check_equal(ccxml_session_destroy(&session), CCXML_OK);

            session = (ccxml_session){0};
            send = (send_probe){.effects.quiescent = true};
            datamodel = (datamodel_probe){.effects.quiescent = true};
            check_equal(
                init_send_session_with_datamodel(
                    &session, &program, &telephony, &datamodel, &send,
                    &event_io_adapter),
                CCXML_OK);
            impl = (ccxml_session_impl *)session.impl;
            impl->next_send_token = UINT64_MAX;
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(impl->next_send_token, UINT64_C(0));
            check_equal(send.id_size, (size_t)CCXML_SEND_ID_MAX_SIZE);
            check_equal(
                send.id + send.id_size - 20u,
                "18446744073709551615");
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_LIMIT_EXCEEDED);
            check_equal(send.send_count, (size_t)1u);
            check_equal(impl->next_send_token, UINT64_C(0));
            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("forwards a literal cancel identifier") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            send_probe send = {.effects.quiescent = true};
            scxml_event_io_adapter capable = event_io_adapter;
            ccxml_event event = alerting_event();

            capable.capabilities |= SCXML_EVENT_IO_CAP_DELAYED_SEND |
                SCXML_EVENT_IO_CAP_CANCEL;
            capable.prepare_cancel = prepare_cancel;
            check_equal(
                compile_actions(
                    &program,
                    "<cancel sendid='&apos;send.fixed.7&apos;'/>"),
                CCXML_OK);
            check_equal(
                init_send_session(
                    &session, &program, &telephony, &send, &capable),
                CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(send.cancel_count, (size_t)1u);
            check_equal(send.cancel_id, "send.fixed.7");
            check_equal(send.effects.commit_count, (size_t)1u);
            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rolls back send when sendid assignment is rejected") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            datamodel_probe datamodel = {
                .effects = {.reject = true, .quiescent = true}};
            send_probe send = {.effects.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" sendid='request.pending'/>"),
                CCXML_OK);
            check_equal(
                init_send_session_with_datamodel(
                    &session, &program, &telephony, &datamodel, &send,
                    &event_io_adapter),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(send.effects.discard_count, (size_t)1u);
            check_equal(send.effects.commit_count, (size_t)0u);
            check_equal(datamodel.value_size, (size_t)0u);
            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rejects empty or unreadable cancel locations") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            datamodel_probe datamodel = {.effects.quiescent = true};
            send_probe send = {.effects.quiescent = true};
            scxml_event_io_adapter capable = event_io_adapter;
            ccxml_event event = alerting_event();

            capable.capabilities |= SCXML_EVENT_IO_CAP_DELAYED_SEND |
                SCXML_EVENT_IO_CAP_CANCEL;
            capable.prepare_cancel = prepare_cancel;
            check_equal(
                compile_actions(
                    &program, "<cancel sendid='request.pending'/>"),
                CCXML_OK);
            check_equal(
                init_send_session_with_datamodel(
                    &session, &program, &telephony, &datamodel, &send,
                    &capable),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_CONTRACT);
            check_equal(send.cancel_count, (size_t)0u);
            check_equal(ccxml_session_destroy(&session), CCXML_OK);

            session = (ccxml_session){0};
            datamodel.reject_read = true;
            check_equal(
                init_send_session_with_datamodel(
                    &session, &program, &telephony, &datamodel, &send,
                    &capable),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("prepares and commits a default ccxml request") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            send_probe send = {.effects.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" name=\"'call.notice'\"/>"),
                CCXML_OK);
            check_equal(
                init_send_session(
                    &session, &program, &telephony, &send,
                    &event_io_adapter),
                CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(send.effects.prepare_count, (size_t)1);
            check_equal(send.effects.commit_count, (size_t)1);
            check_equal(send.event, "call.notice");
            check_equal(send.target, "session:callee");
            check_equal(send.type, "ccxml");
            check_equal(send.id_size, (size_t)0);
            check_equal(send.delay_ms, (uint64_t)0);
            check_equal(send.payload_kind, SCXML_PAYLOAD_NONE);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("requires delayed-send capability only for nonzero delays") {
            ccxml_program delayed_program = {0};
            ccxml_program zero_program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            send_probe send = {.effects.quiescent = true};
            scxml_event_io_adapter delayed_adapter = event_io_adapter;
            ccxml_event event = alerting_event();

            delayed_adapter.capabilities |= SCXML_EVENT_IO_CAP_DELAYED_SEND;
            check_equal(
                compile_actions(
                    &delayed_program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" delay=\"'250ms'\"/>"),
                CCXML_OK);
            check_equal(
                init_send_session(
                    &session, &delayed_program, &telephony, &send,
                    &event_io_adapter),
                CCXML_INVALID_ARGUMENT);
            check_equal(
                init_send_session(
                    &session, &delayed_program, &telephony, &send,
                    &delayed_adapter),
                CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(send.delay_ms, UINT64_C(250));
            check_equal(ccxml_session_destroy(&session), CCXML_OK);

            session = (ccxml_session){0};
            telephony = (effect_probe){.quiescent = true};
            send = (send_probe){.effects.quiescent = true};
            check_equal(
                compile_actions(
                    &zero_program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" delay=\"'0s'\"/>"),
                CCXML_OK);
            check_equal(
                init_send_session(
                    &session, &zero_program, &telephony, &send,
                    &event_io_adapter),
                CCXML_OK);
            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&zero_program);
            ccxml_program_destroy(&delayed_program);
        }

        it("forwards fractional seconds as exact milliseconds") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            send_probe send = {.effects.quiescent = true};
            scxml_event_io_adapter delayed_adapter = event_io_adapter;
            ccxml_event event = alerting_event();

            delayed_adapter.capabilities |= SCXML_EVENT_IO_CAP_DELAYED_SEND;
            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" "
                    "name=\"'call.notice'\" delay=\"'+1.5s'\"/>"),
                CCXML_OK);
            check_equal(
                init_send_session(
                    &session, &program, &telephony, &send,
                    &delayed_adapter),
                CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(send.delay_ms, UINT64_C(1500));
            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("retains source bytes and forwards explicit targettype") {
            char source[512] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.alerting'>"
                "<send target=\"'https://example.test/events'\" "
                "targettype=\"'basichttp'\" name=\"'call.notice'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_diagnostic diagnostic = {0};
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            send_probe send = {.effects.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                ccxml_compile(
                    &program, source, strlen(source), NULL, &diagnostic),
                CCXML_OK);
            memset(source, 'x', sizeof(source));
            check_equal(
                init_send_session(
                    &session, &program, &telephony, &send,
                    &event_io_adapter),
                CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(send.event, "call.notice");
            check_equal(send.target, "https://example.test/events");
            check_equal(send.type, "basichttp");

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("decodes XML entities in every request field") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            send_probe send = {.effects.quiescent = true};
            ccxml_event event = alerting_event();

            check_equal(
                compile_actions(
                    &program,
                    "<send "
                    "target='&quot;https://example.test/?a=1&amp;b=2&quot;' "
                    "name='&apos;call&#46;notice&apos;' "
                    "targettype='&quot;basic&#104;ttp&quot;'/>"),
                CCXML_OK);
            check_equal(
                init_send_session(
                    &session, &program, &telephony, &send,
                    &event_io_adapter),
                CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(send.target, "https://example.test/?a=1&b=2");
            check_equal(send.event, "call.notice");
            check_equal(send.type, "basichttp");

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("requires a capable Event I/O adapter") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            send_probe send = {.effects.quiescent = true};
            scxml_event_io_adapter incapable = event_io_adapter;
            scxml_event_io_adapter malformed = event_io_adapter;

            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" name=\"'call.notice'\"/>"),
                CCXML_OK);
            check_equal(
                init_send_session(
                    &session, &program, &telephony, &send, NULL),
                CCXML_INVALID_ARGUMENT);
            incapable.capabilities = 0u;
            incapable.prepare_send = NULL;
            check_equal(
                init_send_session(
                    &session, &program, &telephony, &send, &incapable),
                CCXML_INVALID_ARGUMENT);
            malformed.struct_size = sizeof(malformed) - 1u;
            check_equal(
                init_send_session(
                    &session, &program, &telephony, &send, &malformed),
                CCXML_INVALID_ARGUMENT);
            ccxml_program_destroy(&program);
        }

        it("maps rejection and malformed tickets") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            send_probe send = {
                .effects = {.reject = true, .quiescent = true}};
            ccxml_event event = alerting_event();

            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" name=\"'call.notice'\"/>"),
                CCXML_OK);
            check_equal(
                init_send_session(
                    &session, &program, &telephony, &send,
                    &event_io_adapter),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(ccxml_session_destroy(&session), CCXML_OK);

            session = (ccxml_session){0};
            send = (send_probe){.effects = {
                .malformed = true, .quiescent = true}};
            check_equal(
                init_send_session(
                    &session, &program, &telephony, &send,
                    &event_io_adapter),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_INVALID_CONTRACT);
            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("copies the Event I/O operation table during initialization") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            send_probe send = {.effects.quiescent = true};
            scxml_event_io_adapter adapter = event_io_adapter;
            ccxml_event event = alerting_event();

            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" name=\"'call.notice'\"/>"),
                CCXML_OK);
            check_equal(
                init_send_session(
                    &session, &program, &telephony, &send, &adapter),
                CCXML_OK);
            adapter.prepare_send = NULL;
            adapter.close = NULL;
            adapter.is_quiescent = NULL;
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            check_equal(send.effects.close_count, (size_t)1);
            ccxml_program_destroy(&program);
        }

        it("does not read the Event I/O tail for a legacy no-send config") {
            typedef struct legacy_session_config {
                const ccxml_program *program;
                const ccxml_telephony_adapter_v1 *telephony;
                void *telephony_user;
                const ccxml_datamodel_adapter_v1 *datamodel;
                void *datamodel_user;
            } legacy_session_config;
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            const legacy_session_config legacy = {
                .program = &program,
                .telephony = &telephony_adapter,
                .telephony_user = &telephony};

            check_equal(compile_actions(&program, "<accept/>"), CCXML_OK);
            check_equal(
                ccxml_session_init(
                    &session, (const ccxml_session_config *)&legacy),
                CCXML_OK);
            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("closes attached adapters when later initialization fails") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.alerting' "
                "cond='true'><send target=\"'session:callee'\" "
                "name=\"'call.notice'\"/></transition></eventprocessor>"
                "</ccxml>";
            ccxml_diagnostic diagnostic = {0};
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            send_probe send = {.effects.quiescent = true};
            const ccxml_session_config config = {
                .program = &program,
                .telephony = &telephony_adapter,
                .telephony_user = &telephony,
                .datamodel = &rejecting_condition_adapter,
                .event_io = &event_io_adapter,
                .event_io_user = &send};

            check_equal(
                ccxml_compile(
                    &program, source, strlen(source), NULL, &diagnostic),
                CCXML_OK);
            check_equal(
                ccxml_session_init(&session, &config), CCXML_ADAPTER_ERROR);
            check_equal(session.impl, NULL);
            check_equal(telephony.close_count, (size_t)1);
            check_equal(send.effects.close_count, (size_t)1);
            ccxml_program_destroy(&program);
        }

        it("commits mixed effects in document order") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            send_probe send = {.effects.quiescent = true};
            ccxml_event event = alerting_event();
            transaction_sequence = 0u;

            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" name=\"'call.one'\"/>"
                    "<accept/>"),
                CCXML_OK);
            check_equal(
                init_send_session(
                    &session, &program, &telephony, &send,
                    &event_io_adapter),
                CCXML_OK);
            check_equal(ccxml_session_dispatch(&session, &event), CCXML_OK);
            check_equal(send.effects.commit_sequence[0], (size_t)1);
            check_equal(telephony.commit_sequence[0], (size_t)2);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("rolls mixed effects back in reverse order") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            send_probe send = {
                .effects = {.reject_at = 2u, .quiescent = true}};
            ccxml_event event = alerting_event();
            transaction_sequence = 0u;

            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" name=\"'call.one'\"/>"
                    "<accept/>"
                    "<send target=\"'session:callee'\" name=\"'call.two'\"/>"),
                CCXML_OK);
            check_equal(
                init_send_session(
                    &session, &program, &telephony, &send,
                    &event_io_adapter),
                CCXML_OK);
            check_equal(
                ccxml_session_dispatch(&session, &event),
                CCXML_ADAPTER_ERROR);
            check_equal(telephony.discard_sequence[0], (size_t)1);
            check_equal(send.effects.discard_sequence[0], (size_t)2);

            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("closes each adapter once and waits for Event I/O quiescence") {
            ccxml_program program = {0};
            ccxml_session session = {0};
            effect_probe telephony = {.quiescent = true};
            send_probe send = {.effects.quiescent = false};

            check_equal(
                compile_actions(
                    &program,
                    "<send target=\"'session:callee'\" name=\"'call.notice'\"/>"),
                CCXML_OK);
            check_equal(
                init_send_session(
                    &session, &program, &telephony, &send,
                    &event_io_adapter),
                CCXML_OK);
            ccxml_session_close(&session);
            ccxml_session_close(&session);
            check_equal(telephony.close_count, (size_t)1);
            check_equal(send.effects.close_count, (size_t)1);
            check_equal(ccxml_session_destroy(&session), CCXML_BUSY);
            send.effects.quiescent = true;
            check_equal(ccxml_session_destroy(&session), CCXML_OK);
            ccxml_program_destroy(&program);
        }
    }
}
