#include "voicexml_fuzz_common.h"

#include <voicexml/voicexml.h>

#include <stdio.h>

#ifndef VOICEXML_FUZZ_CORPUS_DIR
#error "VOICEXML_FUZZ_CORPUS_DIR must be defined"
#endif

static int run_case(
    const unsigned char *data, size_t size, void *user) {
    vxml_limits limits = vxml_default_limits();
    vxml_program program = {0};
    vxml_session session = {0};
    vxml_status status;
    (void)user;

    status = vxml_compile(data, size, &limits, &program, NULL);
    if ((status == VXML_OK) != (program.impl != NULL)) {
        vxml_program_destroy(&program);
        return 1;
    }

    if (status == VXML_OK) {
        const vxml_status init_status =
            vxml_session_init(&session, &program);
        if ((init_status == VXML_OK) != (session.impl != NULL)) {
            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
            return 1;
        }
        /*
         * Arbitrary mutated Programs are fuzzed through Session construction
         * and destruction only. Starting an arbitrary literal Program can
         * legitimately enter long-running control flow because the literal
         * profile has no execution-step quota. Runtime-start smoke stays on
         * fixed known-safe seeds below.
         */
    }

    vxml_session_destroy(&session);
    vxml_program_destroy(&program);
    return program.impl == NULL && session.impl == NULL ? 0 : 1;
}

static int run_safe_runtime_seed(
    const char *name) {
    unsigned char buffer[4096];
    char path[1024];
    FILE *file = NULL;
    long length;
    size_t size;
    vxml_limits limits = vxml_default_limits();
    vxml_program program = {0};
    vxml_session session = {0};
    vxml_status status;
    int written;
    int result = 1;

    written = snprintf(
        path, sizeof(path), "%s/%s",
        VOICEXML_FUZZ_CORPUS_DIR, name);
    if (written <= 0 || (size_t)written >= sizeof(path))
        return 1;
    file = fopen(path, "rb");
    if (file == NULL) return 1;
    if (fseek(file, 0L, SEEK_END) != 0)
        goto done;
    length = ftell(file);
    if (length <= 0 || (size_t)length > sizeof(buffer))
        goto done;
    if (fseek(file, 0L, SEEK_SET) != 0)
        goto done;
    size = (size_t)length;
    if (fread(buffer, 1u, size, file) != size)
        goto done;
    if (fclose(file) != 0) {
        file = NULL;
        goto done;
    }
    file = NULL;

    status = vxml_compile(
        buffer, size, &limits, &program, NULL);
    if (status != VXML_OK || program.impl == NULL)
        goto done;
    status = vxml_session_init(&session, &program);
    if (status != VXML_OK || session.impl == NULL)
        goto done;
    status = vxml_session_start(&session);
    if (status != VXML_OK)
        goto done;
    result = 0;

done:
    if (file != NULL) fclose(file);
    vxml_session_destroy(&session);
    vxml_program_destroy(&program);
    return result;
}

int main(void) {
    static const char *const runtime_seeds[] = {
        "minimal-valid.vxml",
        "navigation-submit.vxml"
    };
    static const char *const seeds[] = {
        "minimal-valid.vxml",
        "navigation-submit.vxml",
        "utf8-entities.vxml",
        "malformed-entity.vxml",
        "lexer-unterminated-attribute.vxml",
        "partial-tree-unclosed.vxml",
        "wrong-namespace.vxml"
    };
    size_t runtime_index;
    int result;

    for (runtime_index = 0u;
         runtime_index <
             sizeof(runtime_seeds) / sizeof(runtime_seeds[0]);
         ++runtime_index) {
        if (run_safe_runtime_seed(runtime_seeds[runtime_index]) != 0) {
            fprintf(
                stderr,
                "VoiceXML fixed runtime smoke failed: %s\n",
                runtime_seeds[runtime_index]);
            return 1;
        }
    }

    result = voicexml_fuzz_run(
        VOICEXML_FUZZ_CORPUS_DIR,
        seeds, sizeof(seeds) / sizeof(seeds[0]),
        16u, 4096u, run_case, NULL);
    if (result != 0)
        fprintf(stderr, "VoiceXML base fuzz smoke failed\n");
    return result;
}
