#ifndef TURBO_SCXML_CNET_DOMAIN_FENCE_H
#define TURBO_SCXML_CNET_DOMAIN_FENCE_H

#include <salts/error_codes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ACE Component Configurator / CNet Acceptor-Connector admission fence.
 *
 * This is a *domain-owned* synchronous admission gate, not a socket owner,
 * Plugin lease, NativeIO request registry, CFlow Actor, Reactor, scheduler or
 * second reference count. Caller keeps one CNet listener/client domain and
 * independently retains the authoritative Salts ComponentPlugin generation
 * Scope through Session/Invoke + native I/O terminal.
 *
 * Every method (including attach/switch/retire) runs on the SAME CNet owner
 * thread as cnet_send_buffer or the other domain operation submitted below.
 * Off-owner calls are rejected: publication is marshalled through the owner's
 * existing serial control lane, never races a CNet send on a second thread.
 *
 * The try_submit callback executes SYNCHRONOUSLY on that owner, after
 * generation/epoch validation and before any possible generation switch.
 * It must be nonblocking and must NOT re-enter this fence or poll CNet.
 * A SALTS_OK return means the underlying owner accepted the operation, NOT
 * that CNet/NativeIO completed it. On completion, only the original CNet
 * observer/request-generation and borrowed Component Scope may settle it.
 */
typedef struct scxml_cnet_domain_fence {
    void *impl;
} scxml_cnet_domain_fence;

typedef int (*scxml_cnet_domain_operation_fn)(void *user);
typedef bool (*scxml_cnet_domain_quiescent_fn)(void *user);

typedef struct scxml_cnet_domain_fence_stats {
    uint64_t current_generation;
    uint64_t staged_generation;
    uint64_t draining_generation;
    uint64_t latest_generation;
    uint64_t admission_epoch;
    size_t attached;
    bool closed;
    bool submitting;
    uint64_t accepted;
    uint64_t rejected;
    uint64_t switches;
    uint64_t retired;
    uint64_t failed_submissions;
} scxml_cnet_domain_fence_stats;

/* Address-stable domain owner must outlive its gate, CNet clients and
   CMeta ComponentScopes. No global registry or heap allocations per send. */
int scxml_cnet_domain_fence_init(scxml_cnet_domain_fence *fence);

/*
 * Attach first generation as current. Stage only one later generation while
 * current remains live; no third attached generation is ever admitted.
 * Generation IDs MUST be the exact nonzero IDs from Salts ComponentPlugin
 * scopes and strictly increasing for this domain.
 */
int scxml_cnet_domain_fence_attach(scxml_cnet_domain_fence *fence,
                                    uint64_t generation);

/* Atomically stop old exclusive I/O admission and activate the staged
   generation on this owner lane. Old in-flight CNet requests are untouched. */
int scxml_cnet_domain_fence_activate(scxml_cnet_domain_fence *fence,
                                      uint64_t staged_generation);

/* A switch already in progress can never accept a third generation while an
   older draining one remains. No automatic generation settlement/retry. */
int scxml_cnet_domain_fence_try_submit(
    scxml_cnet_domain_fence *fence, uint64_t generation,
    scxml_cnet_domain_operation_fn submit, void *user);

/* Explicitly release a staged (not yet activated) generation after failed
   candidate validation, or a draining one ONLY when the domain owner's
   authoritative CNet-terminal + ComponentScope quiescence probe succeeds.
   Current generation cannot retire while admitting new operations. */
int scxml_cnet_domain_fence_retire(
    scxml_cnet_domain_fence *fence, uint64_t generation,
    scxml_cnet_domain_quiescent_fn is_quiescent, void *user);

/* Closes further admission without cancelling already-accepted CNet/NativeIO
   work; caller explicitly retires generations and closes its one listener. */
int scxml_cnet_domain_fence_close(scxml_cnet_domain_fence *fence);
bool scxml_cnet_domain_fence_get_stats(
    const scxml_cnet_domain_fence *fence, scxml_cnet_domain_fence_stats *out);
int scxml_cnet_domain_fence_destroy(scxml_cnet_domain_fence *fence);

#ifdef __cplusplus
}
#endif
#endif /* TURBO_SCXML_CNET_DOMAIN_FENCE_H */
