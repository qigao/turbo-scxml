#ifndef TURBO_VOICEXML_ALLOCATOR_H
#define TURBO_VOICEXML_ALLOCATOR_H

#include <stddef.h>

void *vxml_malloc(size_t size);
void *vxml_calloc(size_t count, size_t size);
void *vxml_realloc(void *pointer, size_t size);
void vxml_free(void *pointer);

#endif /* TURBO_VOICEXML_ALLOCATOR_H */
