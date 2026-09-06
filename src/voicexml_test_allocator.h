#ifndef TURBO_VOICEXML_TEST_ALLOCATOR_H
#define TURBO_VOICEXML_TEST_ALLOCATOR_H

#if !defined(TURBOSCXML_VOICEXML_TESTING)
#error "VoiceXML allocator injection is available only in test builds"
#endif

#include <stddef.h>

typedef struct vxml_test_allocator {
    void *(*malloc_fn)(size_t size);
    void *(*calloc_fn)(size_t count, size_t size);
    void *(*realloc_fn)(void *pointer, size_t size);
    void (*free_fn)(void *pointer);
} vxml_test_allocator;

/** Install only while no VoiceXML-owned allocation from another allocator lives. */
void vxml_test_allocator_set(const vxml_test_allocator *allocator);

/** Restore the production CRT allocator after all injected allocations are freed. */
void vxml_test_allocator_reset(void);

#endif /* TURBO_VOICEXML_TEST_ALLOCATOR_H */
