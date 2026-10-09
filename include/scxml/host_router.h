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
    /* Zero disables invocation aliases; positive bound allocates exact rows. */
    size_t invoke_capacity;
} scxml_host_router_config;

typedef struct scxml_host_router_stats {
    size_t endpoints;
    size_t pending;
    size_t reserved;
    size_t high_water;
    size_t invoke_bindings;
    bool closed;
    bool draining;
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
/* Reserve an endpoint before Session initialization, for initial onentry
 * <send> tickets. The reserved ref is routable source-relative but not
 * externally accessible or deliverable until activation succeeds. */
int scxml_host_router_reserve(
    scxml_host_router *router, scxml_host_session_ref *out_ref);
/* Publish the real generated location after successful Session init. */
int scxml_host_router_activate(
    scxml_host_router *router, scxml_host_session_ref ref,
    scxml_session *session);
/* Abort a never-activated endpoint after failed Session init. READY rows
 * involving it are cancelled; RESERVED tickets must first discard/commit.
 * Relation bindings must be explicitly unlinked before abortion.
 */
int scxml_host_router_abort(
    scxml_host_router *router, scxml_host_session_ref ref);

/* Shortcut for a Session that was already fully initialized and has no
 * initial Host-published effects. Distinct from reserve -> activate.
 */
int scxml_host_router_attach(scxml_host_router *router, scxml_session *session,
                             scxml_host_session_ref *out_ref);
/* BUSY if any accepted/ticket/in-flight Event or another live endpoint's
 * parent/Invoke routing binding still borrows this exact Session generation.
 * Unlink relations first; a returned SUCCESS permits Session destruction.
 */
int scxml_host_router_detach(scxml_host_router *router,
                             scxml_host_session_ref ref);

/* Define an optional relative parent relationship. Passing {0,0} as parent
 * clears it. Identities are pinned, so slot reuse cannot redirect a child.
 * A parent cannot be detached while this relationship remains live.
 */
int scxml_host_router_set_parent(scxml_host_router *router,
                                 scxml_host_session_ref child,
                                 scxml_host_session_ref parent);

/* Explicitly opt an attached Session into global #_scxml_<sessionid>
 * location lookup. Source-relative #_parent, #_<invokeid> and self routing
 * do not rely on this external-access flag.
 */
int scxml_host_router_set_external_access(
    scxml_host_router *router, scxml_host_session_ref ref, bool enabled);

/* Bind one invoke id (without "#_") on source to a target Session.
 * The table is finite: config.invoke_capacity; duplicate owner/id returns
 * EALREADY. The target/owner must both unlink before either can detach.
 */
int scxml_host_router_bind_invoke(
    scxml_host_router *router, scxml_host_session_ref source,
    const char *invoke_id, size_t invoke_id_size,
    scxml_host_session_ref target);
int scxml_host_router_unbind_invoke(
    scxml_host_router *router, scxml_host_session_ref source,
    const char *invoke_id, size_t invoke_id_size);

/* Resolve empty (self), #_parent, #_<invokeid>, or the exact *public*
 * #_scxml_<sessionid> location. An inaccessible or stale target is ENOENT.
 * No generic URI parser, redirect or unqualified fallback is installed.
 */
int scxml_host_router_resolve(
    scxml_host_router *router, scxml_host_session_ref source,
    const char *target, size_t target_size, scxml_host_session_ref *out_target);

/* One SCXML source-relative send effect, atomically resolving its target
 * and copying name, optional sendid and TEXT_UTF8/XML_UTF8 content into a RESERVED
 * Host row. The returned ticket transfers into the CFlow effect journal.
 * The source and target references remain pinned until final Host delivery
 * or explicit cancel/discard. Nonempty send_id is bounded by metadata size.
 * No network I/O or target Session admission occurs during prepare/commit.
 */
int scxml_host_router_prepare_target(
    scxml_host_router *router, scxml_host_session_ref source,
    const char *target, size_t target_size,
    const char *name, size_t name_size,
    const char *send_id, size_t send_id_size,
    /* NULL: no content. Otherwise TEXT_UTF8 or XML_UTF8, borrowed for
       this call and copied into bounded Host row before ACCEPTED. */
    const scxml_content_view *content,
    cflow_statechart_effect_ticket *out_ticket);

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
/* Close a source's Event I/O binding without cancelling unrelated targets:
 * drop only READY rows from this exact sending Session. RESERVED tickets
 * and INFLIGHT delivery remain authoritative and must settle separately. */
int scxml_host_router_cancel_source(
    scxml_host_router *router, scxml_host_session_ref source,
    size_t *out_cancelled);
/* Returns true iff this source has no RESERVED/READY/INFLIGHT rows. */
bool scxml_host_router_source_is_quiescent(
    const scxml_host_router *router, scxml_host_session_ref source);
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
