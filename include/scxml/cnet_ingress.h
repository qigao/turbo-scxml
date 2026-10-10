#ifndef TURBO_SCXML_CNET_INGRESS_H
#define TURBO_SCXML_CNET_INGRESS_H

#include <scxml/scxml.h>
#include <cnet/cnet.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ACE Half-Sync/Half-Async ingress over the existing CNet owner.
 *
 * This is NOT an SCXML Event I/O Processor, a TCP message framing protocol or
 * a second CFlow Actor. Every CNet receive completion is one BYTE CHUNK, not
 * one application message. The adapter copies all borrowed bytes into a
 * caller-sized queue and represents each chunk as an external SCXML Event with
 * the configured name; _event.data is exact lowercase hexadecimal text.
 * A protocol needing message framing must perform that in a separate host
 * codec rather than interpreting these chunk boundaries as message boundaries.
 *
 * Ownership:
 *   - caller owns CNet client, connection, Session, SerialExecutor and poll;
 *   - this bridge owns only its finite staging rows and fixed event name;
 *   - callbacks, arm, drain, stats and close run on the single CNet owner lane;
 *   - Session + client must outlive successful ingress_destroy();
 *   - no Plugin/Component lease is invented: the hosting generation scope
 *     must independently outlive this bridge and its CNet callbacks.
 *
 * No hidden worker, scheduler, retry, extra transport or runtime registry.
 */
typedef struct scxml_cnet_ingress {
    void *impl;
} scxml_cnet_ingress;

typedef struct scxml_cnet_ingress_config {
    cnet_client *client;
    scxml_session *session;
    const char *event_name;
    size_t event_name_size;
    /* Fixed number of owned but not yet admitted external Event rows. */
    size_t capacity;
    /* Per-CNet-receive hard bound, 1..SCXML_EVENT_METADATA_CAPACITY / 2. */
    size_t max_chunk_bytes;
} scxml_cnet_ingress_config;

typedef struct scxml_cnet_ingress_stats {
    cnet_connection connection;
    size_t capacity;
    size_t pending;
    size_t peak_pending;
    bool bound;
    bool connected;
    bool receive_armed;
    bool closed;
    bool terminal;
    bool failed;
    int first_error;
    uint64_t received;
    uint64_t delivered;
    uint64_t cancelled;
    uint64_t rejected_full;
    uint64_t rejected_oversize;
    uint64_t stale_callbacks;
} scxml_cnet_ingress_stats;

/* Initialize fixed owned rows, copy event name, and borrow client/Session. */
int scxml_cnet_ingress_init(
    scxml_cnet_ingress *ingress,
    const scxml_cnet_ingress_config *config);

/*
 * Return the observer to pass to cnet_connect()/listener_accept().
 * Its 'user' points into ingress-owned storage, which must remain at a stable
 * address through the real CNet connection terminal callback.
 */
cnet_observer scxml_cnet_ingress_observer(scxml_cnet_ingress *ingress);

/* Bind the resulting nonzero-generation CNet connection before polling it. */
int scxml_cnet_ingress_bind(
    scxml_cnet_ingress *ingress, cnet_connection connection);

/*
 * Explicitly submit one receive credit (not a poll). Fails with ENOBUFS
 * while the owned staging rows are full; EBUSY if another credit is live.
 * No receive is rearmed automatically from callbacks or drain.
 */
int scxml_cnet_ingress_arm(scxml_cnet_ingress *ingress);

/*
 * Attempt up to max_events FIFO transfers into the Session's existing
 * external Mailbox. FULL retains the head row (and reports exact backpressure);
 * caller must re-enter only after making bounded Session progress.
 * out_delivered receives the number transferred by this call, even on FULL.
 */
cflow_mailbox_status scxml_cnet_ingress_drain(
    scxml_cnet_ingress *ingress, size_t max_events, size_t *out_delivered);

bool scxml_cnet_ingress_get_stats(
    const scxml_cnet_ingress *ingress, scxml_cnet_ingress_stats *out);

/*
 * Stop new receive admission; explicitly cancel and account for any buffered
 * rows. Does not close the caller-owned CNet connection or settle NativeIO.
 */
int scxml_cnet_ingress_close(scxml_cnet_ingress *ingress);

/* Requires close, no live receive credit, and observed CNet terminal. */
bool scxml_cnet_ingress_is_quiescent(const scxml_cnet_ingress *ingress);

/* EBUSY retains all storage until the domain owner has drained CNet. */
int scxml_cnet_ingress_destroy(scxml_cnet_ingress *ingress);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_SCXML_CNET_INGRESS_H */
