#include <ccxml/ccxml.h>

#include "tinytest.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct provider_probe provider_probe;

typedef struct provider_ticket {
    provider_probe *owner;
    size_t ordinal;
    bool live;
} provider_ticket;

struct provider_probe {
    provider_ticket tickets[4];
    size_t prepare_count;
    size_t commit_count;
    size_t discard_count;
    size_t commit_order[4];
    size_t discard_order[4];
    size_t prepare_kinds[4];
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
};

enum {
    PROVIDER_ACCEPT = 1,
    PROVIDER_CREATE_CALL,
    PROVIDER_DISCONNECT
};

static void ticket_commit(void *user) {
    provider_ticket *ticket = (provider_ticket *)user;
    if (ticket == NULL || !ticket->live) return;
    ticket->live = false;
    ticket->owner->commit_order[ticket->owner->commit_count] =
        ticket->ordinal;
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
    .prepare_disconnect = prepare_disconnect};

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
            truncated.struct_size = sizeof(ccxml_telephony_adapter_v1) - 1u;

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
}
