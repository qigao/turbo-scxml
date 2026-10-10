#ifndef TURBO_SCXML_CNET_FRAME_INGRESS_H
#define TURBO_SCXML_CNET_FRAME_INGRESS_H

#include <scxml/host_router.h>
#include <cnet/cnet.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Explicit NONSTANDARD host wire profile:
 *   uint16 big-endian, nonzero payload length; followed by that many bytes.
 *
 * This strategy reassembles TCP fragments/coalesced chunks, but DOES NOT
 * implement the W3C SCXML Event Processor or W3C Event envelope. Each
 * complete frame is a single configured external Event, whose TEXT_UTF8 data
 * contains exactly the frame bytes as lowercase hex. Its host Session target
 * is a generation-checked ref; no naked Session pointer is retained.
 *
 * CNet connection and progress belong to the caller; this bridge is a bounded
 * borrow, with at most one copied in-flight receive CHUNK, one partial/complete
 * frame and a caller-sized separate Host Router queue. No extra poller/worker.
 */
typedef struct scxml_cnet_frame_ingress { void *impl; } scxml_cnet_frame_ingress;

typedef struct scxml_cnet_frame_ingress_config {
    cnet_client *client;
    scxml_host_router *router;
    scxml_host_session_ref target;
    const char *event_name;
    size_t event_name_size;
    /* <= SCXML_EVENT_METADATA_CAPACITY / 2 (hex-encoded Event data). */
    size_t max_frame_bytes;
    /* Maximum bytes supplied by a single CNet receive callback. */
    size_t max_receive_bytes;
} scxml_cnet_frame_ingress_config;

typedef struct scxml_cnet_frame_ingress_stats {
    cnet_connection connection;
    size_t buffered_chunk_bytes;
    size_t frame_bytes;
    size_t required_frame_bytes;
    bool bound;
    bool connected;
    bool armed;
    bool terminal;
    bool closed;
    bool failed;
    int first_error;
    uint64_t received_chunks;
    uint64_t accepted_frames;
    uint64_t rejected_frames;
    uint64_t host_full;
    uint64_t cancelled_bytes;
    uint64_t stale_callbacks;
} scxml_cnet_frame_ingress_stats;

int scxml_cnet_frame_ingress_init(
    scxml_cnet_frame_ingress *ingress,
    const scxml_cnet_frame_ingress_config *config);
cnet_observer scxml_cnet_frame_ingress_observer(
    scxml_cnet_frame_ingress *ingress);
int scxml_cnet_frame_ingress_bind(
    scxml_cnet_frame_ingress *ingress, cnet_connection connection);

/* At most one outstanding CNet receive, no implicit rearm. */
int scxml_cnet_frame_ingress_arm(scxml_cnet_frame_ingress *ingress);

/*
 * Host owner decodes up to max_frames (complete messages), inserting into the
 * bounded Host Router. SALTS_ENOBUFS retains both a complete frame and any
 * unconsumed bytes from the last callback, requiring explicit Host progress.
 * To preserve bytes, arm refuses while copied input remains unprocessed.
 */
int scxml_cnet_frame_ingress_process(
    scxml_cnet_frame_ingress *ingress,
    size_t max_frames, size_t *out_accepted);

bool scxml_cnet_frame_ingress_get_stats(
    const scxml_cnet_frame_ingress *ingress,
    scxml_cnet_frame_ingress_stats *out);

/* Close cancels decoder-owned incomplete/undelivered bytes, not Host rows. */
int scxml_cnet_frame_ingress_close(scxml_cnet_frame_ingress *ingress);
bool scxml_cnet_frame_ingress_is_quiescent(
    const scxml_cnet_frame_ingress *ingress);
int scxml_cnet_frame_ingress_destroy(scxml_cnet_frame_ingress *ingress);

#ifdef __cplusplus
}
#endif
#endif /* TURBO_SCXML_CNET_FRAME_INGRESS_H */
