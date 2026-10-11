#ifndef TURBOSCXML_INSTALL_CONSUMER_CCXML_QUICKJS_CASES_H
#define TURBOSCXML_INSTALL_CONSUMER_CCXML_QUICKJS_CASES_H

#include <ccxml/quickjs.h>
#include <cmeta/data_reflect.h>
#include <string.h>

typedef struct installed_ccxml_state { int value; } installed_ccxml_state;
cmeta_reflect_data(installed_ccxml_state, "turboscxml.install.ccxml.state",
    cmeta_field(int, value)
);

typedef struct installed_ccxml_ticket {
    size_t prepared, committed, discarded;
    bool pending;
} installed_ccxml_ticket;

typedef struct installed_ccxml_provider {
    installed_ccxml_ticket send, cancel;
    const char *target, *name, *type, *cancel_id;
    uint64_t delay;
    size_t closed;
} installed_ccxml_provider;

static bool installed_ccxml_matches(const char *data, size_t size, const char *expected) {
    return data != NULL && size == strlen(expected) && memcmp(data, expected, size) == 0;
}

static void installed_ccxml_commit(void *user) {
    installed_ccxml_ticket *ticket = (installed_ccxml_ticket *)user;
    ++ticket->committed;
    ticket->pending = false;
}

static void installed_ccxml_discard(void *user) {
    installed_ccxml_ticket *ticket = (installed_ccxml_ticket *)user;
    ++ticket->discarded;
    ticket->pending = false;
}

/* One outstanding ticket per operation; dispatch resolves it before reuse.
 * Request views are inspected during prepare and never retained. */
static scxml_adapter_status installed_ccxml_prepare(
    installed_ccxml_ticket *ticket, cflow_statechart_effect_ticket *out) {
    if (ticket->pending) return SCXML_ADAPTER_FULL;
    ++ticket->prepared;
    ticket->pending = true;
    out->commit = installed_ccxml_commit;
    out->discard = installed_ccxml_discard;
    out->user = ticket;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status installed_ccxml_send(void *user,
    const scxml_send_request *request, cflow_statechart_effect_ticket *out,
    const char **error) {
    installed_ccxml_provider *provider = (installed_ccxml_provider *)user;
    if (error != NULL) *error = NULL;
    if (!installed_ccxml_matches(request->target, request->target_size, provider->target) ||
        !installed_ccxml_matches(request->event, request->event_size, provider->name) ||
        !installed_ccxml_matches(request->type, request->type_size, provider->type) ||
        request->delay_ms != provider->delay)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    return installed_ccxml_prepare(&provider->send, out);
}

static scxml_adapter_status installed_ccxml_cancel(void *user,
    const scxml_cancel_request *request, cflow_statechart_effect_ticket *out,
    const char **error) {
    installed_ccxml_provider *provider = (installed_ccxml_provider *)user;
    if (error != NULL) *error = NULL;
    if (!installed_ccxml_matches(request->send_id, request->send_id_size, provider->cancel_id))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    return installed_ccxml_prepare(&provider->cancel, out);
}

static scxml_adapter_status installed_ccxml_unexpected_accept(void *user,
    const ccxml_accept_request *request, cflow_statechart_effect_ticket *out,
    const char **error) {
    (void)user; (void)request; (void)out;
    if (error != NULL) *error = "unexpected telephony action";
    return SCXML_ADAPTER_INVALID_CONTRACT;
}

static void installed_ccxml_close(void *user) {
    ++((installed_ccxml_provider *)user)->closed;
}

static bool installed_ccxml_quiescent(void *user) {
    const installed_ccxml_provider *provider = (const installed_ccxml_provider *)user;
    return !provider->send.pending && !provider->cancel.pending;
}

/* Compiled independently as C11 and C++17, against installed public targets. */
static int installed_ccxml_quickjs_cases(void) {
    static const char source[] =
        "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
        "<eventprocessor><transition event='go' cond='value >= 0'>"
        "<send target=\"('session:' + value + '/' + event$.connectionid)\" "
        "name=\"('notice.' + event$.name)\" targettype=\"('x-' + event$.name)\" "
        "delay=\"(value + 'ms')\"/>"
        "<cancel sendid=\"(value === 0 ? 42 : 'timer.' + value)\"/>"
        "</transition></eventprocessor></ccxml>";
    installed_ccxml_state state = {7};
    installed_ccxml_provider provider = {0};
    ccxml_quickjs_datamodel owner = {0};
    ccxml_program program = {0};
    ccxml_session session = {0};
    ccxml_diagnostic diagnostic = {0};
    ccxml_quickjs_datamodel_config_v1 config = ccxml_quickjs_default_datamodel_config();
    ccxml_session_config session_config = {0};
    ccxml_telephony_adapter_v1 telephony = {0};
    scxml_event_io_adapter event_io = {0};
    ccxml_event event = {0};
    int result = 10;

    config.state_config.root = cmeta_reflected_data(installed_ccxml_state);
    config.state_config.state = &state;
    if (ccxml_quickjs_datamodel_init(&owner, &config) != CCXML_OK) goto cleanup;
    result = 11;
    if (ccxml_compile(&program, source, sizeof(source) - 1u, NULL, &diagnostic) != CCXML_OK)
        goto cleanup;

    telephony.abi_version = CCXML_TELEPHONY_ADAPTER_ABI_V1;
    telephony.struct_size = sizeof(telephony);
    telephony.prepare_accept = installed_ccxml_unexpected_accept;
    telephony.close = installed_ccxml_close;
    telephony.is_quiescent = installed_ccxml_quiescent;
    event_io.abi_version = SCXML_ADAPTER_ABI;
    event_io.struct_size = sizeof(event_io);
    event_io.capabilities = SCXML_EVENT_IO_CAP_SEND | SCXML_EVENT_IO_CAP_DELAYED_SEND |
        SCXML_EVENT_IO_CAP_CANCEL;
    event_io.prepare_send = installed_ccxml_send;
    event_io.prepare_cancel = installed_ccxml_cancel;
    event_io.close = installed_ccxml_close;
    event_io.is_quiescent = installed_ccxml_quiescent;
    session_config.program = &program;
    session_config.telephony = &telephony;
    session_config.telephony_user = &provider;
    session_config.event_io = &event_io;
    session_config.event_io_user = &provider;
    session_config.datamodel = ccxml_quickjs_datamodel_adapter();
    session_config.datamodel_user = &owner;
    result = 12;
    if (ccxml_session_init(&session, &session_config) != CCXML_OK) goto cleanup;

    event.name = "go";
    event.name_size = 2u;
    event.connection_id = "call-7";
    event.connection_id_size = 6u;
    provider.target = "session:7/call-7";
    provider.name = "notice.go";
    provider.type = "x-go";
    provider.cancel_id = "timer.7";
    provider.delay = 7u;
    result = 13;
    if (ccxml_session_dispatch(&session, &event) != CCXML_OK ||
        provider.send.prepared != 1u || provider.send.committed != 1u ||
        provider.cancel.prepared != 1u || provider.cancel.committed != 1u ||
        provider.send.discarded != 0u || provider.cancel.discarded != 0u ||
        !installed_ccxml_quiescent(&provider)) goto cleanup;

    /* A false JS condition must not prepare any provider action. */
    state.value = -1;
    result = 14;
    if (ccxml_session_dispatch(&session, &event) != CCXML_OK ||
        provider.send.prepared != 1u || provider.cancel.prepared != 1u) goto cleanup;

    /* Invalid cancel output rolls back the already prepared send. */
    state.value = 0;
    provider.target = "session:0/call-7";
    provider.delay = 0u;
    result = 15;
    if (ccxml_session_dispatch(&session, &event) != CCXML_INVALID_CONTRACT ||
        provider.send.prepared != 2u || provider.send.committed != 1u ||
        provider.send.discarded != 1u || provider.cancel.prepared != 1u ||
        provider.cancel.committed != 1u || provider.cancel.discarded != 0u ||
        !installed_ccxml_quiescent(&provider)) goto cleanup;

    /* The same Session reimports current native state and Event on retry. */
    state.value = 8;
    event.connection_id = "call-8";
    provider.target = "session:8/call-8";
    provider.cancel_id = "timer.8";
    provider.delay = 8u;
    result = 16;
    if (ccxml_session_dispatch(&session, &event) != CCXML_OK ||
        provider.send.prepared != 3u || provider.send.committed != 2u ||
        provider.send.discarded != 1u || provider.cancel.prepared != 2u ||
        provider.cancel.committed != 2u || provider.cancel.discarded != 0u ||
        !installed_ccxml_quiescent(&provider) || state.value != 8) goto cleanup;
    result = 17;
    ccxml_session_close(&session);
    ccxml_session_close(&session);
    if (provider.closed != 2u ||
        ccxml_session_dispatch(&session, &event) != CCXML_CLOSED) goto cleanup;
    result = 0;

cleanup:
    /* A failed drain must not release the owner still borrowed by the Session. */
    if (ccxml_session_destroy(&session) != CCXML_OK) return result != 0 ? result : 18;
    ccxml_quickjs_datamodel_destroy(&owner);
    ccxml_program_destroy(&program);
    if (result == 0 && (provider.closed != 2u || owner.impl != NULL || session.impl != NULL))
        result = 19;
    return result;
}

#endif
