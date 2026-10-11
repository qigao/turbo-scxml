#include "voicexml_fuzz_common.h"

#include <voicexml/quickjs.h>

#include <stdio.h>

#ifndef VOICEXML_QUICKJS_FUZZ_CORPUS_DIR
#error "VOICEXML_QUICKJS_FUZZ_CORPUS_DIR must be defined"
#endif

static int run_case(
    const unsigned char *data,
    size_t size,
    void *user) {
    vxml_limits limits = vxml_default_limits();
    vxml_quickjs_compile_options_v1 options =
        vxml_quickjs_default_compile_options();
    vxml_program program = {0};
    vxml_status status;
    (void)user;

    status = vxml_compile_quickjs_script_profile(
        data, size, &limits, &options,
        &program, NULL);
    if ((status == VXML_OK) !=
        (program.impl != NULL)) {
        vxml_program_destroy(&program);
        return 1;
    }

    /*
     * Metadata fuzz is compile-only. Arbitrary JavaScript is never executed
     * here; runtime hardening remains covered by fixed sandbox/profile tests.
     */
    vxml_program_destroy(&program);
    return program.impl == NULL ? 0 : 1;
}

int main(void) {
    static const char *const seeds[] = {
        "script-static.vxml",
        "script-dynamic.vxml",
        "data-static.vxml",
        "data-dynamic.vxml",
        "malformed-expression.vxml"
    };
    const int result = voicexml_fuzz_run(
        VOICEXML_QUICKJS_FUZZ_CORPUS_DIR,
        seeds, sizeof(seeds) / sizeof(seeds[0]),
        16u, 4096u, run_case, NULL);
    if (result != 0)
        fprintf(stderr,
                "VoiceXML QuickJS metadata fuzz smoke failed\n");
    return result;
}
