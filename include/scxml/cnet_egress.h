#ifndef TURBO_SCXML_CNET_EGRESS_H
#define TURBO_SCXML_CNET_EGRESS_H

#include <cnet/cnet.h>
#include <scxml/scxml.h>
#include <salts/error_codes.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Explicit NONSTANDARD raw-byte transport strategy. This is not the W3C
 * SCXML Event I/O Processor and is not an SCXML-over-TCP framing protocol.
 * Empty/default type and other targets are rejected, not reinterpreted.
 *
 * A send with no payload writes the event-name UTF-8 bytes; TEXT_UTF8 content
 * writes its borrowed content bytes verbatim. Other payloads, delay and
 * cancellation are not advertised or accepted.
 */
#define SCXML_CNET_RAW_PROCESSOR_URI "urn:turboscxml:cnet-raw:1"
#define SCXML_CNET_BOUND_TARGET "cnet://bound"

/*
 * Salts::CNet owns NativeIO/connection progress. A CFlow SerialExecutor
 * produces nonblocking effect tickets; only the CNet owner calls pump.
 *
 * One CNet connection has one observer. This outbound observer is separate
 * from scxml_cnet_ingress_observer; hosts combining both directions must
 * provide a domain-owned observer that forwards both callback sets.
 */
typedef struct scxml_cnet_egress {
    void *impl;
} scxml_cnet_egress;

typedef struct scxml_cnet_egress_config {
    cnet_client *client;       /* borrowed and owner-progressed externally */
    size_t capacity;           /* bounded RESERVED/READY/SUBMITTED rows */
    size_t max_payload_bytes;  /* 1..SCXML_EVENT_METADATA_CAPACITY */
} scxml_cnet_egress_config;

typedef struct scxml_cnet_egress_stats {
    cnet_connection connection;
    size_t capacity;
    size_t pending;
    size_t high_water;
    bool bound;
    bool connected;
    bool terminal;
    bool closed;
    bool failed;
    int first_error;
    uint64_t prepared;
    uint64_t committed;
    uint64_t discarded;
    uint64_t submitted;
    uint64_t completed;
    uint64_t cancelled;
    uint64_t rejected_full;
    uint64_t transient_full;
    uint64_t stale_callbacks;
    uint64_t invariant_failures;
} scxml_cnet_egress_stats;

/* Initialize once; all storage is bounded and rows are address-stable. */
int scxml_cnet_egress_init(
    scxml_cnet_egress *egress, const scxml_cnet_egress_config *config);

/* Observer and borrowed user remain stable until real connection terminal. */
cnet_observer scxml_cnet_egress_observer(scxml_cnet_egress *egress);

/* Bind the nonzero-generation connection before progressing its callbacks. */
int scxml_cnet_egress_bind(
    scxml_cnet_egress *egress, cnet_connection connection);

/*
 * Session-facing adapter only advertises SEND + CONTENT. Its prepare callback
 * reserves an owned row and copies request bytes. commit/discard are
 * nonblocking and never invoke CNet or publish network effects themselves.
 */
const scxml_event_io_adapter *scxml_cnet_egress_adapter(void);
void *scxml_cnet_egress_adapter_user(scxml_cnet_egress *egress);

/*
 * Caller is the CNet owner thread. Explicitly submit up to max_writes READY
 * rows, ordered by committed sequence. A full CNet command queue returns
 * SALTS_ENOBUFS and retains the earliest READY row for a later explicit pump.
 * An admitted CNet write remains live until on_send or connection terminal.
 * out_submitted is set on all valid calls, including a partial failure.
 */
int scxml_cnet_egress_pump(
    scxml_cnet_egress *egress, size_t max_writes, size_t *out_submitted);

bool scxml_cnet_egress_get_stats(
    const scxml_cnet_egress *egress, scxml_cnet_egress_stats *out);

/*
 * Stop new prepare/pump admission and cancel committed rows that have not
 * reached CNet. Accepted writes await CNet's authoritative completion or
 * terminal. The domain owner closes/stops/drains the real CNet connection.
 */
int scxml_cnet_egress_close(scxml_cnet_egress *egress);
bool scxml_cnet_egress_is_quiescent(const scxml_cnet_egress *egress);

/* EBUSY preserves callback/ticket storage while still borrowed by CFlow/CNet. */
int scxml_cnet_egress_destroy(scxml_cnet_egress *egress);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_SCXML_CNET_EGRESS_H */
