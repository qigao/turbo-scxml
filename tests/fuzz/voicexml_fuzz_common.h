#ifndef TURBOSCXML_VOICEXML_FUZZ_COMMON_H
#define TURBOSCXML_VOICEXML_FUZZ_COMMON_H

#include <stddef.h>

typedef int (*voicexml_fuzz_case_fn)(
    const unsigned char *data, size_t size, void *user);

int voicexml_fuzz_run(
    const char *corpus_dir,
    const char *const *seeds,
    size_t seed_count,
    size_t iterations_per_seed,
    size_t max_bytes,
    voicexml_fuzz_case_fn run_case,
    void *user);

#endif
