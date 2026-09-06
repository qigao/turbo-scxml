#include "voicexml_allocator.h"

#include <stdlib.h>

#if defined(TURBOSCXML_VOICEXML_TESTING)
#include "voicexml_test_allocator.h"

static void *default_malloc(size_t size) {
    return malloc(size);
}

static void *default_calloc(size_t count, size_t size) {
    return calloc(count, size);
}

static void *default_realloc(void *pointer, size_t size) {
    return realloc(pointer, size);
}

static void default_free(void *pointer) {
    free(pointer);
}

static const vxml_test_allocator default_allocator = {
    default_malloc,
    default_calloc,
    default_realloc,
    default_free};

static vxml_test_allocator active_allocator = {
    default_malloc,
    default_calloc,
    default_realloc,
    default_free};

void vxml_test_allocator_set(const vxml_test_allocator *allocator) {
    if (allocator == NULL || allocator->malloc_fn == NULL ||
        allocator->calloc_fn == NULL || allocator->realloc_fn == NULL ||
        allocator->free_fn == NULL) {
        active_allocator = default_allocator;
        return;
    }
    active_allocator = *allocator;
}

void vxml_test_allocator_reset(void) {
    active_allocator = default_allocator;
}

void *vxml_malloc(size_t size) {
    return active_allocator.malloc_fn(size);
}

void *vxml_calloc(size_t count, size_t size) {
    return active_allocator.calloc_fn(count, size);
}

void *vxml_realloc(void *pointer, size_t size) {
    return active_allocator.realloc_fn(pointer, size);
}

void vxml_free(void *pointer) {
    active_allocator.free_fn(pointer);
}

#else

void *vxml_malloc(size_t size) {
    return malloc(size);
}

void *vxml_calloc(size_t count, size_t size) {
    return calloc(count, size);
}

void *vxml_realloc(void *pointer, size_t size) {
    return realloc(pointer, size);
}

void vxml_free(void *pointer) {
    free(pointer);
}

#endif
