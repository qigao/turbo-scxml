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
        if (init_status == VXML_OK)
            (void)vxml_session_start(&session);
    }

    vxml_session_destroy(&session);
    vxml_program_destroy(&program);
    return program.impl == NULL && session.impl == NULL ? 0 : 1;
}

int main(void) {
    static const char *const seeds[] = {
        "minimal-valid.vxml",
        "navigation-submit.vxml",
        "utf8-entities.vxml",
        "malformed-entity.vxml",
        "wrong-namespace.vxml"
    };
    const int result = voicexml_fuzz_run(
        VOICEXML_FUZZ_CORPUS_DIR,
        seeds, sizeof(seeds) / sizeof(seeds[0]),
        32u, 4096u, run_case, NULL);
    if (result != 0)
        fprintf(stderr, "VoiceXML base fuzz smoke failed\n");
    return result;
}
