#ifndef SCXML_COMPONENT_INVOKE_PROBE_H
#define SCXML_COMPONENT_INVOKE_PROBE_H

/* Strictly test-only CMeta capability projected from an existing
 * Salts ComponentPlugin Scope. No public TurboSCXML or Salts ABI additions. */
#include <scxml/provider.h>
#include <cnet/cnet.h>
#include <cmeta/object_interface.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct scxml_test_cnet_observer_stats {
    uint64_t invoke_token;
    int marker;
    size_t native_sends;
    size_t native_send_bytes;
    size_t native_state_callbacks;
    size_t stale_callbacks;
    bool native_connected;
    bool native_terminal;
    bool invoke_closed;
} scxml_test_cnet_observer_stats;

/* Host-owned, address-stable C11 atomic rendezvous. The DSO borrows this
 * test-only gate until the selected CNet owner callback returns. */
typedef struct scxml_test_cnet_callback_gate {
    atomic_int entered;
    atomic_int release;
    atomic_int timed_out;
} scxml_test_cnet_callback_gate;

#define SCXML_TEST_CNET_PROBE_METHODS(X, I) \
    X(I, R1, bool, get_observer, cnet_observer *, out) \
    X(I, R1, bool, snapshot, scxml_test_cnet_observer_stats *, out) \
    X(I, R1, bool, arm_send_gate, scxml_test_cnet_callback_gate *, gate) \
    X(I, R1, bool, arm_terminal_gate, scxml_test_cnet_callback_gate *, gate) \
    X(I, R1, bool, arm_cancel_gate, scxml_test_cnet_callback_gate *, gate)

CMETA_INTERFACE(scxml_test_cnet_probe, SCXML_TEST_CNET_PROBE_METHODS);
CMETA_OBJECT_INTERFACE_ADAPTER(scxml_test_cnet_probe);

#endif /* SCXML_COMPONENT_INVOKE_PROBE_H */
