#ifndef TURBO_SCXML_HOST_ROUTER_H
#define TURBO_SCXML_HOST_ROUTER_H

#include <scxml/scxml.h>
#include <salts/error_codes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bounded Host delivery boundary for complete external Events.
 *
 * One scxml_session retains exclusive ownership of its CFlow Statechart and
 * external FIFO. This Host owns only endpoint identity and accepted but not
 * yet transferred Events. It is NOT a second Statechart/Actor/Plugin runtime.
 *
 * All Session pointers remain borrowed; a Session must remain valid through
 * detach. Attach/detach and router destroy must not race with running callers.
 * Concurrent producer CFlow executors and one host drain lane are serialized
 * through the router's bounded mutex. No callbacks run while it is unlocked
 * other than the CFlow effect-ticket commit/discard calls.
 */
typedef struct scxml_host_router { void *impl; } scxml_host_router;

typedef struct scxml_host_session_ref {
    uint32_t slot;
    uint32_t generation;
} scxml_host_session_ref;

typedef struct scxml_host_router_config {
    size_t endpoint_capacity;
    size_t event_capacity;
    /* 1..SCXML_EVENT_METADATA_CAPACITY; bounds copied TEXT_UTF8 data. */
    size_t max_text_bytes;
} scxml_host_router_config;

typedef struct scxml_host_router_stats {
    size_t endpoints;
    size_t pending;
    size_t reserved;
    size_t high_water;
    bool closed;
    uint64_t prepared;
    uint64_t committed;
    uint64_t discarded;
    uint64_t delivered;
    uint64_t cancelled;
    uint64_t rejected_full;
    uint64_t stale_refs;
    uint64_t delivery_errors;
    uint64_t invariant_failures;
} scxml_host_router_stats;

int scxml_host_router_init(scxml_host_router *router,
                           const scxml_host_router_config *config);
int scxml_host_router_attach(scxml_host_router *router, scxml_session *session,
                             scxml_host_session_ref *out_ref);
/* BUSY if an accepted or reserved Event still borrows the old Session. */
int scxml_host_router_detach(scxml_host_router *router,
                             scxml_host_session_ref ref);

/*
 * Copy one complete Event and UTF-8 body into bounded Host storage. No Event
 * may become visible in the destination Session before explicit drain.
 * The caller supplies a generation-checked target, not a raw Session pointer.
 * This is transport admission (not a CFlow microstep effect).
 */
int scxml_host_router_enqueue(scxml_host_router *router,
                              scxml_host_session_ref target,
                              const char *name, size_t name_size,
                              const char *text, size_t text_size);

/*
 * Reserve a Host row during an SCXML prepare_* callback. Ownership transfers
 * to the returned move-only effect ticket only on SALTS_OK; commit publishes
 * READY in commit order, discard frees, both nonblocking and infallible.
 * No CNet call, Session admission, or host delivery occurs during commit.
 */
int scxml_host_router_prepare(scxml_host_router *router,
                              scxml_host_session_ref target,
                              const char *name, size_t name_size,
                              const char *text, size_t text_size,
                              cflow_statechart_effect_ticket *out_ticket);

/*
 * Host owner dispatches committed rows to the Session's canonical external
 * FIFO. CFLOW_MAILBOX_FULL retains the head for explicit credit/progress.
 * Non-FULL errors also retain it, for explicit cancel/error handling.
 */
cflow_mailbox_status scxml_host_router_drain(
    scxml_host_router *router, size_t max_events, size_t *out_delivered);

/* Explicitly drop all READY events belonging to one target; tickets remain. */
int scxml_host_router_cancel(scxml_host_router *router,
                             scxml_host_session_ref target,
                             size_t *out_cancelled);
bool scxml_host_router_get_stats(const scxml_host_router *router,
                                 scxml_host_router_stats *out);
int scxml_host_router_close(scxml_host_router *router);
bool scxml_host_router_is_quiescent(const scxml_host_router *router);
/* BUSY until all tickets are settled and all Session references detached. */
int scxml_host_router_destroy(scxml_host_router *router);

#ifdef __cplusplus
}
#endif
#endif /* TURBO_SCXML_HOST_ROUTER_H */
