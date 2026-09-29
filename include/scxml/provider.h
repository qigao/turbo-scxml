#ifndef TURBO_SCXML_PROVIDER_H
#define TURBO_SCXML_PROVIDER_H

#include <scxml/scxml.h>
#include <cmeta/interface.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical provider reflection for the existing SCXML adapter ABIs.
 *
 * These methods intentionally use CMeta's exact-ABI interface rows rather than
 * pretending pointer-rich request/ticket callbacks are VALUE callables.
 * Method names, dispatch arity, vtable shape and capabilities are reflected;
 * request ownership and effect-ticket semantics remain defined by scxml.h.
 */
#define SCXML_EVENT_IO_PROVIDER_METHODS(X, I) \
    X(I, R3, scxml_adapter_status, prepare_send, \
      const scxml_send_request *, request, \
      cflow_statechart_effect_ticket *, out_ticket, \
      const char **, out_error) \
    X(I, R3, scxml_adapter_status, prepare_cancel, \
      const scxml_cancel_request *, request, \
      cflow_statechart_effect_ticket *, out_ticket, \
      const char **, out_error) \
    X(I, V0, void, close, _) \
    X(I, R0, bool, is_quiescent, _)

CMETA_INTERFACE(scxml_event_io_provider, SCXML_EVENT_IO_PROVIDER_METHODS);

#define SCXML_INVOKE_PROVIDER_METHODS(X, I) \
    X(I, R3, scxml_adapter_status, prepare_start, \
      const scxml_invoke_start_request *, request, \
      cflow_statechart_effect_ticket *, out_ticket, \
      const char **, out_error) \
    X(I, R3, scxml_adapter_status, prepare_cancel, \
      const scxml_invoke_cancel_request *, request, \
      cflow_statechart_effect_ticket *, out_ticket, \
      const char **, out_error) \
    X(I, R3, scxml_adapter_status, prepare_forward, \
      const scxml_invoke_forward_request *, request, \
      cflow_statechart_effect_ticket *, out_ticket, \
      const char **, out_error) \
    X(I, V0, void, close, _) \
    X(I, R0, bool, is_quiescent, _)

CMETA_INTERFACE(scxml_invoke_provider, SCXML_INVOKE_PROVIDER_METHODS);

/*
 * Static adapter -> canonical CMeta Interface bridge.
 *
 * The adapter ops are copied. adapter_user remains borrowed through every
 * interface call. The bridge object must therefore outlive all borrowers.
 */
typedef struct scxml_event_io_provider_bridge {
    scxml_event_io_adapter adapter;
    void *adapter_user;
    scxml_event_io_provider_vtable vtable;
    scxml_event_io_provider provider;
} scxml_event_io_provider_bridge;

bool scxml_event_io_provider_bridge_init(
    scxml_event_io_provider_bridge *bridge,
    const scxml_event_io_adapter *adapter,
    void *adapter_user);

scxml_event_io_provider *scxml_event_io_provider_bridge_get(
    scxml_event_io_provider_bridge *bridge);

typedef struct scxml_invoke_provider_bridge {
    scxml_invoke_adapter adapter;
    void *adapter_user;
    scxml_invoke_provider_vtable vtable;
    scxml_invoke_provider provider;
} scxml_invoke_provider_bridge;

bool scxml_invoke_provider_bridge_init(
    scxml_invoke_provider_bridge *bridge,
    const scxml_invoke_adapter *adapter,
    void *adapter_user);

scxml_invoke_provider *scxml_invoke_provider_bridge_get(
    scxml_invoke_provider_bridge *bridge);

/*
 * Canonical CMeta Interface -> existing session-facing adapter bridge.
 *
 * The provider handle is copied but its self/vtable remain borrowed. The
 * bridge must outlive the SCXML session because session initialization copies
 * adapter ops and retains the returned user pointer through destruction.
 */
typedef struct scxml_event_io_adapter_bridge {
    scxml_event_io_provider provider;
    scxml_event_io_adapter adapter;
} scxml_event_io_adapter_bridge;

bool scxml_event_io_adapter_bridge_init(
    scxml_event_io_adapter_bridge *bridge,
    const scxml_event_io_provider *provider);

const scxml_event_io_adapter *scxml_event_io_adapter_bridge_get(
    const scxml_event_io_adapter_bridge *bridge);

void *scxml_event_io_adapter_bridge_user(
    scxml_event_io_adapter_bridge *bridge);

typedef struct scxml_invoke_adapter_bridge {
    scxml_invoke_provider provider;
    scxml_invoke_adapter adapter;
} scxml_invoke_adapter_bridge;

bool scxml_invoke_adapter_bridge_init(
    scxml_invoke_adapter_bridge *bridge,
    const scxml_invoke_provider *provider);

const scxml_invoke_adapter *scxml_invoke_adapter_bridge_get(
    const scxml_invoke_adapter_bridge *bridge);

void *scxml_invoke_adapter_bridge_user(
    scxml_invoke_adapter_bridge *bridge);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_SCXML_PROVIDER_H */
