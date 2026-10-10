#include <scxml/cnet_domain_fence.h>

#include <salts/thread.h>

#include <stdlib.h>

typedef struct scxml_cnet_domain_fence_impl {
    const void *owner_thread;
    uint64_t current;
    uint64_t staged;
    uint64_t draining;
    uint64_t latest;
    uint64_t epoch;
    uint64_t accepted;
    uint64_t rejected;
    uint64_t switches;
    uint64_t retired;
    uint64_t failed_submissions;
    bool closed;
    bool submitting;
} scxml_cnet_domain_fence_impl;

static scxml_cnet_domain_fence_impl *fence_impl(
    const scxml_cnet_domain_fence *fence) {
    return fence != NULL
        ? (scxml_cnet_domain_fence_impl *)fence->impl : NULL;
}

static int check_owner(const scxml_cnet_domain_fence_impl *impl) {
    return impl != NULL &&
        impl->owner_thread == cmeta_thread_current_token()
        ? SALTS_OK : SALTS_EINVAL;
}

static size_t attached(const scxml_cnet_domain_fence_impl *impl) {
    return (size_t)(impl->current != 0u) +
        (size_t)(impl->staged != 0u) +
        (size_t)(impl->draining != 0u);
}

int scxml_cnet_domain_fence_init(scxml_cnet_domain_fence *fence) {
    scxml_cnet_domain_fence_impl *impl;
    const void *owner;
    if (fence == NULL || fence->impl != NULL) return SALTS_EINVAL;
    owner = cmeta_thread_current_token();
    if (owner == NULL) return SALTS_EINVAL;
    impl = (scxml_cnet_domain_fence_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return SALTS_ENOMEM;
    impl->owner_thread = owner;
    fence->impl = impl;
    return SALTS_OK;
}

int scxml_cnet_domain_fence_attach(scxml_cnet_domain_fence *fence,
                                    uint64_t generation) {
    scxml_cnet_domain_fence_impl *impl = fence_impl(fence);
    if (check_owner(impl) != SALTS_OK || generation == 0u)
        return SALTS_EINVAL;
    if (impl->submitting) return SALTS_EBUSY;
    if (impl->closed) return SALTS_ESHUTDOWN;
    if (generation <= impl->latest) return SALTS_EALREADY;
    if (attached(impl) == 2u || impl->draining != 0u ||
        impl->staged != 0u) return SALTS_EBUSY;

    if (impl->current == 0u) {
        if (impl->epoch == UINT64_MAX) return SALTS_ERANGE;
        impl->current = generation;
        ++impl->epoch;
    } else {
        impl->staged = generation;
    }
    impl->latest = generation;
    return SALTS_OK;
}

int scxml_cnet_domain_fence_activate(scxml_cnet_domain_fence *fence,
                                      uint64_t staged_generation) {
    scxml_cnet_domain_fence_impl *impl = fence_impl(fence);
    if (check_owner(impl) != SALTS_OK || staged_generation == 0u)
        return SALTS_EINVAL;
    if (impl->submitting || impl->draining != 0u) return SALTS_EBUSY;
    if (impl->closed) return SALTS_ESHUTDOWN;
    if (impl->staged != staged_generation) return SALTS_ENOENT;
    if (impl->current == 0u || impl->epoch == UINT64_MAX)
        return SALTS_EPROTO;

    /* This CNet owner is the ONLY caller: check + switch form one serial
       atomic admission boundary. A CNet accepted request is not migrated. */
    impl->draining = impl->current;
    impl->current = impl->staged;
    impl->staged = 0u;
    ++impl->epoch;
    ++impl->switches;
    return SALTS_OK;
}

int scxml_cnet_domain_fence_try_submit(
    scxml_cnet_domain_fence *fence, uint64_t generation,
    scxml_cnet_domain_operation_fn submit, void *user) {
    scxml_cnet_domain_fence_impl *impl = fence_impl(fence);
    int status;
    if (check_owner(impl) != SALTS_OK || generation == 0u ||
        submit == NULL) return SALTS_EINVAL;
    if (impl->submitting) return SALTS_EBUSY;
    if (impl->closed) return SALTS_ESHUTDOWN;
    if (generation != impl->current) {
        ++impl->rejected;
        return SALTS_EPERM;
    }

    /* Reentrancy/owner checks prevent a nested activation or close while
       the real CNet command admission callback is running. The callback
       borrows no new Plugin/NativeIO lifetime from this fence. */
    impl->submitting = true;
    status = submit(user);
    impl->submitting = false;
    if (status == SALTS_OK) ++impl->accepted;
    else ++impl->failed_submissions;
    return status;
}

int scxml_cnet_domain_fence_retire(
    scxml_cnet_domain_fence *fence, uint64_t generation,
    scxml_cnet_domain_quiescent_fn is_quiescent, void *user) {
    scxml_cnet_domain_fence_impl *impl = fence_impl(fence);
    uint64_t *selected = NULL;
    bool ready;
    if (check_owner(impl) != SALTS_OK || generation == 0u)
        return SALTS_EINVAL;
    if (impl->submitting) return SALTS_EBUSY;
    if (generation == impl->draining) selected = &impl->draining;
    else if (generation == impl->staged) selected = &impl->staged;
    else if (generation == impl->current) {
        if (!impl->closed) return SALTS_EBUSY;
        selected = &impl->current;
    }
    if (selected == NULL) return SALTS_ENOENT;

    /* Staged candidates have not admitted work; rollback needs no native
       completion evidence. Live or draining generations MUST prove the CNet
       terminal and existing Plugin Scope quiescence at the actual domain. */
    if (selected != &impl->staged) {
        if (is_quiescent == NULL) return SALTS_EINVAL;
        impl->submitting = true;
        ready = is_quiescent(user);
        impl->submitting = false;
        if (!ready) return SALTS_EBUSY;
    } else if (is_quiescent != NULL) {
        impl->submitting = true;
        ready = is_quiescent(user);
        impl->submitting = false;
        if (!ready) return SALTS_EBUSY;
    }
    *selected = 0u;
    ++impl->retired;
    return SALTS_OK;
}

int scxml_cnet_domain_fence_close(scxml_cnet_domain_fence *fence) {
    scxml_cnet_domain_fence_impl *impl = fence_impl(fence);
    if (check_owner(impl) != SALTS_OK) return SALTS_EINVAL;
    if (impl->submitting) return SALTS_EBUSY;
    if (impl->closed) return SALTS_EALREADY;
    impl->closed = true;
    return SALTS_OK;
}

bool scxml_cnet_domain_fence_get_stats(
    const scxml_cnet_domain_fence *fence,
    scxml_cnet_domain_fence_stats *out) {
    scxml_cnet_domain_fence_impl *impl = fence_impl(fence);
    if (check_owner(impl) != SALTS_OK || out == NULL) return false;
    *out = (scxml_cnet_domain_fence_stats){
        .current_generation = impl->current,
        .staged_generation = impl->staged,
        .draining_generation = impl->draining,
        .latest_generation = impl->latest,
        .admission_epoch = impl->epoch,
        .attached = attached(impl),
        .closed = impl->closed,
        .submitting = impl->submitting,
        .accepted = impl->accepted,
        .rejected = impl->rejected,
        .switches = impl->switches,
        .retired = impl->retired,
        .failed_submissions = impl->failed_submissions
    };
    return true;
}

int scxml_cnet_domain_fence_destroy(scxml_cnet_domain_fence *fence) {
    scxml_cnet_domain_fence_impl *impl = fence_impl(fence);
    if (fence == NULL) return SALTS_EINVAL;
    if (impl == NULL) return SALTS_OK;
    if (check_owner(impl) != SALTS_OK) return SALTS_EINVAL;
    if (impl->submitting || !impl->closed || attached(impl) != 0u)
        return SALTS_EBUSY;
    free(impl);
    fence->impl = NULL;
    return SALTS_OK;
}
