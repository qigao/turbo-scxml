#ifndef SCXML_COMPONENT_INTERCEPTOR_PROBE_H
#define SCXML_COMPONENT_INTERCEPTOR_PROBE_H

#include <scxml/component.h>
#include <cmeta/ace_interceptor.h>
#include <cmeta/function.h>
#include <string.h>

/* A test-only, native-typed ACE Interceptor for exactly one SCXML Event I/O
 * prepare_send boundary. The target preserves the existing SCXML adapter ABI,
 * and only its final ACCEPTED result transfers a move-only CFlow ticket.
 * The Component Scope, not this intercept chain, pins a Plugin DSO. */
typedef struct scxml_intercept_send_result {
    scxml_adapter_status provider_status;
    cflow_statechart_effect_ticket ticket;
    const char *error;
} scxml_intercept_send_result;

CMETA_INTERCEPTOR_TYPE(
    scxml_test_send_intercept, scxml_send_request, scxml_intercept_send_result);

typedef struct scxml_intercept_probe scxml_intercept_probe;

typedef struct scxml_intercept_stage {
    scxml_intercept_probe *owner;
    unsigned id;
} scxml_intercept_stage;

struct scxml_intercept_probe {
    const scxml_event_io_adapter *provider;
    void *provider_user; /* borrowed through an external Component Scope */
    scxml_test_send_intercept chain;
    scxml_test_send_intercept_hook hooks[2];
    scxml_intercept_stage stages[2];
    unsigned trace[32];
    size_t trace_count;
    unsigned target_calls;
    unsigned discarded_on_failure;
    unsigned reject_stage;
    unsigned error_stage;
    bool fail_before_provider;
    bool fail_after_provider;
    cmeta_status last_error;
};

static const cmeta_type_desc scxml_intercept_status_desc = {
    "cmeta_status", sizeof(cmeta_status), CMETA_ALIGNOF(cmeta_status),
    CMETA_T_INTEGER, NULL, NULL, NULL
};

/* Exact metadata comes from CMeta, not a guessed or erased call ABI.
 * const request and writable borrowed result are native C pointer contracts. */
CMETA_FUNCTION_METADATA_AS_ABI_RESULT(
    scxml_intercept_target_contract, "scxml.event_io.prepare_send.interceptor",
    io, &scxml_intercept_status_desc, CMETA_ABI_ENUM, CMETA_RESULT_VALUE,
    (void *, context, CMETA_PARAM_IN | CMETA_PARAM_BORROWED,
     &cmeta_type_void_ptr, CMETA_ABI_OBJECT_POINTER),
    (const scxml_send_request *, request, CMETA_PARAM_IN | CMETA_PARAM_BORROWED,
     &cmeta_type_void_ptr, CMETA_ABI_OBJECT_POINTER),
    (scxml_intercept_send_result *, result, CMETA_PARAM_OUT | CMETA_PARAM_BORROWED,
     &cmeta_type_void_ptr, CMETA_ABI_OBJECT_POINTER));

static void scxml_intercept_trace(scxml_intercept_probe *probe, unsigned code) {
    if (probe->trace_count < sizeof(probe->trace) / sizeof(probe->trace[0]))
        probe->trace[probe->trace_count++] = code;
}

static cmeta_status scxml_intercept_target(
    void *context, const scxml_send_request *request,
    scxml_intercept_send_result *response) {
    scxml_intercept_probe *probe = (scxml_intercept_probe *)context;
    ++probe->target_calls;
    scxml_intercept_trace(probe, 9u);
    if (probe->fail_before_provider)
        return CMETA_CALLBACK_ERROR;
    response->provider_status = probe->provider->prepare_send(
        probe->provider_user, request, &response->ticket, &response->error);
    if (response->provider_status != SCXML_ADAPTER_ACCEPTED) {
        /* A rejected provider cannot leak a partially prepared ticket. */
        if (response->ticket.discard != NULL) {
            response->ticket.discard(response->ticket.user);
            ++probe->discarded_on_failure;
            response->ticket = (cflow_statechart_effect_ticket){0};
        }
        return CMETA_CALLBACK_ERROR;
    }
    if (probe->fail_after_provider) {
        /* Explicitly roll back a move-only ticket if a target-stage check
           rejects AFTER the provider's real prepare callback succeeded. */
        if (response->ticket.discard != NULL) {
            response->ticket.discard(response->ticket.user);
            ++probe->discarded_on_failure;
            response->ticket = (cflow_statechart_effect_ticket){0};
        }
        return CMETA_CALLBACK_ERROR;
    }
    return CMETA_OK;
}

CMETA_STATIC_ASSERT(
    CMETA_TYPE_MATCHES(&scxml_intercept_target,
                       scxml_test_send_intercept_target_fn),
    "SCXML Interceptor must match its exact const-native target signature");

static cmeta_status scxml_intercept_before(
    void *context, const scxml_send_request *request, bool *proceed) {
    scxml_intercept_stage *stage = (scxml_intercept_stage *)context;
    scxml_intercept_probe *probe = stage->owner;
    (void)request;
    scxml_intercept_trace(probe, 10u + stage->id);
    *proceed = probe->reject_stage != stage->id;
    return probe->error_stage == stage->id
        ? CMETA_CALLBACK_ERROR : CMETA_OK;
}

static void scxml_intercept_after(
    void *context, const scxml_send_request *request,
    const scxml_intercept_send_result *response) {
    scxml_intercept_stage *stage = (scxml_intercept_stage *)context;
    (void)request;
    (void)response;
    scxml_intercept_trace(stage->owner, 20u + stage->id);
}

static void scxml_intercept_error(
    void *context, const scxml_send_request *request,
    cmeta_status status) {
    scxml_intercept_stage *stage = (scxml_intercept_stage *)context;
    (void)request;
    stage->owner->last_error = status;
    scxml_intercept_trace(stage->owner, 30u + stage->id);
}

static cmeta_status scxml_intercept_probe_init(
    scxml_intercept_probe *probe, const scxml_event_io_adapter *provider,
    void *provider_user) {
    const cmeta_function_abi_desc *expected =
        &scxml_intercept_target_contract__function_abi_meta;
    if (probe == NULL || provider == NULL || provider->prepare_send == NULL)
        return CMETA_INVALID_ARGUMENT;
    memset(probe, 0, sizeof(*probe));
    probe->provider = provider;
    probe->provider_user = provider_user;
    for (size_t i = 0u; i < 2u; ++i) {
        probe->stages[i] = (scxml_intercept_stage){
            probe, (unsigned)i + 1u
        };
        probe->hooks[i] = (scxml_test_send_intercept_hook){
            &probe->stages[i], scxml_intercept_before,
            scxml_intercept_after, scxml_intercept_error
        };
    }
    return scxml_test_send_intercept_admit(
        &probe->chain, probe, scxml_intercept_target,
        probe->hooks, 2u, expected, expected);
}

/* This is the one test-only SCXML adapter callback, with SCXML's original
 * ticket and failure semantics. A hook rejection transfers no ticket.
 * Provider callbacks never occur during Interceptor metadata admission. */
static scxml_adapter_status scxml_intercept_prepare_send(
    scxml_intercept_probe *probe, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    scxml_intercept_send_result result = {0};
    cmeta_status status;
    if (out_ticket != NULL) *out_ticket = (cflow_statechart_effect_ticket){0};
    if (out_error != NULL) *out_error = NULL;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    status = scxml_test_send_intercept_invoke(
        &probe->chain, request, &result);
    if (status != CMETA_OK) {
        *out_error = result.error != NULL
            ? result.error : "interceptor-denied";
        return result.provider_status == SCXML_ADAPTER_ACCEPTED
            ? SCXML_ADAPTER_ERROR_EXECUTION : result.provider_status;
    }
    if (result.provider_status != SCXML_ADAPTER_ACCEPTED ||
        result.ticket.commit == NULL || result.ticket.discard == NULL) {
        if (result.ticket.discard != NULL) {
            result.ticket.discard(result.ticket.user);
            ++probe->discarded_on_failure;
        }
        *out_error = "interceptor-ticket-contract";
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    *out_ticket = result.ticket; /* exactly one transfer */
    *out_error = result.error;
    return SCXML_ADAPTER_ACCEPTED;
}

static void scxml_intercept_probe_reset_trace(scxml_intercept_probe *probe) {
    probe->trace_count = 0u;
    memset(probe->trace, 0, sizeof(probe->trace));
}

#endif /* SCXML_COMPONENT_INTERCEPTOR_PROBE_H */
