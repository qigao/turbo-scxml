#include "voicexml_fuzz_common.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t fuzz_next(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x != 0u ? x : UINT32_C(0x6d2b79f5);
    return *state;
}

static int read_seed(
    const char *corpus_dir,
    const char *name,
    unsigned char *buffer,
    size_t max_bytes,
    size_t *out_size) {
    char path[1024];
    FILE *file;
    long length;
    size_t size;
    int written;

    if (corpus_dir == NULL || name == NULL || buffer == NULL ||
        max_bytes == 0u || out_size == NULL)
        return 1;
    *out_size = 0u;
    written = snprintf(path, sizeof(path), "%s/%s", corpus_dir, name);
    if (written <= 0 || (size_t)written >= sizeof(path))
        return 1;
    file = fopen(path, "rb");
    if (file == NULL) return 1;
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return 1;
    }
    length = ftell(file);
    if (length < 0 || (uint64_t)length > (uint64_t)max_bytes) {
        fclose(file);
        return 1;
    }
    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return 1;
    }
    size = (size_t)length;
    if (size != 0u && fread(buffer, 1u, size, file) != size) {
        fclose(file);
        return 1;
    }
    if (fclose(file) != 0) return 1;
    *out_size = size;
    return 0;
}

static size_t mutate(
    unsigned char *work,
    size_t size,
    size_t max_bytes,
    uint32_t seed,
    size_t iteration) {
    static const unsigned char replacements[] = {
        '<', '>', '&', '"', '\'', '/', '=', ' ', '\n', 0u, 0xffu};
    uint32_t state =
        seed ^ (uint32_t)(iteration * UINT32_C(0x9e3779b9));
    size_t position;
    size_t count;

    if (iteration == 0u) return size;
    if (size == 0u && max_bytes != 0u) {
        work[0] = '<';
        return 1u;
    }

    switch (fuzz_next(&state) % 8u) {
    case 0u:
        position = (size_t)(fuzz_next(&state) % (uint32_t)size);
        work[position] ^= (unsigned char)(
            1u << (fuzz_next(&state) & 7u));
        break;
    case 1u:
        position = (size_t)(fuzz_next(&state) % (uint32_t)size);
        work[position] =
            replacements[fuzz_next(&state) %
                (sizeof(replacements) / sizeof(replacements[0]))];
        break;
    case 2u:
        size = (size_t)(fuzz_next(&state) % (uint32_t)(size + 1u));
        break;
    case 3u:
        if (size < max_bytes) {
            position = (size_t)(fuzz_next(&state) % (uint32_t)(size + 1u));
            memmove(work + position + 1u, work + position, size - position);
            work[position] =
                replacements[fuzz_next(&state) %
                    (sizeof(replacements) / sizeof(replacements[0]))];
            ++size;
        }
        break;
    case 4u:
        if (size != 0u) {
            position = (size_t)(fuzz_next(&state) % (uint32_t)size);
            memmove(work + position, work + position + 1u,
                    size - position - 1u);
            --size;
        }
        break;
    case 5u:
        if (size != 0u && size < max_bytes) {
            position = (size_t)(fuzz_next(&state) % (uint32_t)size);
            count = 1u + (size_t)(fuzz_next(&state) % 16u);
            if (count > size - position) count = size - position;
            if (count > max_bytes - size) count = max_bytes - size;
            if (count != 0u) {
                memmove(
                    work + position + count,
                    work + position,
                    size - position);
                memcpy(work + position, work + position + count, count);
                size += count;
            }
        }
        break;
    case 6u:
        if (size != 0u) {
            position = (size_t)(fuzz_next(&state) % (uint32_t)size);
            count = 1u + (size_t)(fuzz_next(&state) % 8u);
            if (count > size - position) count = size - position;
            memset(
                work + position,
                (fuzz_next(&state) & 1u) != 0u ? 0xff : ' ',
                count);
        }
        break;
    case 7u:
        if (size > 1u) {
            const size_t a =
                (size_t)(fuzz_next(&state) % (uint32_t)size);
            const size_t b =
                (size_t)(fuzz_next(&state) % (uint32_t)size);
            const unsigned char tmp = work[a];
            work[a] = work[b];
            work[b] = tmp;
        }
        break;
    }
    return size;
}

int voicexml_fuzz_run(
    const char *corpus_dir,
    const char *const *seeds,
    size_t seed_count,
    size_t iterations_per_seed,
    size_t max_bytes,
    voicexml_fuzz_case_fn run_case,
    void *user) {
    unsigned char *seed = NULL;
    unsigned char *work = NULL;
    size_t seed_index;
    int result = 1;

    if (corpus_dir == NULL || seeds == NULL || seed_count == 0u ||
        iterations_per_seed == 0u || max_bytes == 0u ||
        max_bytes > UINT32_MAX || run_case == NULL)
        return 1;

    seed = (unsigned char *)malloc(max_bytes);
    work = (unsigned char *)malloc(max_bytes);
    if (seed == NULL || work == NULL) goto done;

    for (seed_index = 0u; seed_index < seed_count; ++seed_index) {
        size_t seed_size = 0u;
        size_t iteration;
        if (read_seed(
                corpus_dir, seeds[seed_index],
                seed, max_bytes, &seed_size) != 0) {
            fprintf(stderr, "failed to read fuzz seed: %s\n", seeds[seed_index]);
            goto done;
        }
        for (iteration = 0u;
             iteration < iterations_per_seed;
             ++iteration) {
            size_t size;
            memcpy(work, seed, seed_size);
            size = mutate(
                work, seed_size, max_bytes,
                UINT32_C(0x811c9dc5) ^
                    (uint32_t)(seed_index * UINT32_C(0x01000193)),
                iteration);
            if (run_case(work, size, user) != 0) {
                fprintf(
                    stderr,
                    "fuzz invariant failed: seed=%s iteration=%zu size=%zu\n",
                    seeds[seed_index], iteration, size);
                goto done;
            }
        }
    }
    result = 0;

done:
    free(work);
    free(seed);
    return result;
}
