#ifndef TURBO_SCXML_HOST_EVENT_IO_H
#define TURBO_SCXML_HOST_EVENT_IO_H

#include <scxml/host_router.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCXML_HOST_EVENT_PROCESSOR_URI \
    "http://www.w3.org/TR/scxml/#SCXMLEventProcessor"

/*
 * Session-facing ACE Host Strategy, borrowed through the ordinary SCXML
 * Event I/O adapter ABI and compatible with static or CMeta Component
 * projection. This is not a new Statechart or NativeIO owner.
 *
 * Declare Host endpoints and parent/invoke relations BEFORE Session init:
 *
 *   host_router_reserve -> binding_init -> session_init (onentry can send)
 *   -> host_router_activate -> host_router_drain
 *
 * Failed session_init requires binding close and quiescence, then
 * host_router_abort after explicit unlink of parent/invoke relationships.
 *
 * The base adapter advertises SEND + bounded CONTENT (TEXT_UTF8/XML_UTF8).
 * The separately opted-in delayed adapter advertises DELAYED_SEND|CANCEL
 * only with a borrowed CFlow Clock and finite timer/cancel reservations.
 * Neither adapter creates a Scheduler. Named/scalar/CMETA remain unsupported.
 * Empty type chooses the canonical W3C SCXML Event Processor;
 * unsupported types/targets and over-limit content fail closed.
 * It does not claim complete W3C SCXML Event I/O specification coverage.
 */
typedef struct scxml_host_event_io_binding { void *impl; }
    scxml_host_event_io_binding;

int scxml_host_event_io_binding_init(
    scxml_host_event_io_binding *binding, scxml_host_router *router,
    scxml_host_session_ref source);

/* Explicitly opt in to the existing SCXML delayed-send/sendid cancellation
 * contract. Requires a borrowed valid CFlow Clock, positive timer_capacity
 * and positive cancel_capacity in the owning Host Router. No fallback to
 * system wall-clock or a second Scheduler.
 * Use scxml_host_event_io_binding_delayed_adapter() in Session config;
 * the base adapter continues advertising only SEND|CONTENT.
 */
int scxml_host_event_io_binding_init_delayed(
    scxml_host_event_io_binding *binding, scxml_host_router *router,
    scxml_host_session_ref source);
const scxml_event_io_adapter *
scxml_host_event_io_binding_delayed_adapter(void);

/* A canonical adapter with one versioned, exact shape. */
const scxml_event_io_adapter *scxml_host_event_io_binding_adapter(void);
void *scxml_host_event_io_binding_user(
    scxml_host_event_io_binding *binding);

/* Called only AFTER the borrowing Session has closed and stopped callbacks. */
int scxml_host_event_io_binding_destroy(
    scxml_host_event_io_binding *binding);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_SCXML_HOST_EVENT_IO_H */
