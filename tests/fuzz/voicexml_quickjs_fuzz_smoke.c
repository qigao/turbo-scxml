#include "voicexml_fuzz_common.h"

#include <voicexml/quickjs.h>

#include <stdio.h>

#ifndef VOICEXML_QUICKJS_FUZZ_CORPUS_DIR
#error "VOICEXML_QUICKJS_FUZZ_CORPUS_DIR must be defined"
#endif

static bool compile_matches(
    const char *source, size_t size,
    const vxml_quickjs_compile_options_v1 *options, vxml_status expected) {
    vxml_program program = {0};
    const vxml_status status = vxml_compile_quickjs_script_profile(
        source, size, NULL, options, &program, NULL);
    const bool matched = status == expected &&
        ((status == VXML_OK) == (program.impl != NULL));
    vxml_program_destroy(&program);
    if (!matched)
        fprintf(stderr, "QuickJS option size=%zu: status=%d expected=%d\n",
                options->struct_size, (int)status, (int)expected);
    return matched && program.impl == NULL;
}

static int run_option_contracts(void) {
    static const char script[] =
        "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
        "<form><block><script src='worker.js'/></block></form></vxml>";
    static const char data[] =
        "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
        "<data src='data.json'/><form><block/></form></vxml>";
    static const char dynamic_script[] =
        "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
        "<form><block><script srcexpr=\"'x.js'\"/></block></form></vxml>";
    const size_t historical_prefix =
        offsetof(vxml_quickjs_compile_options_v1, root);
    const size_t data_tail =
        offsetof(vxml_quickjs_compile_options_v1, max_data_namelist_fields) +
        sizeof(size_t);
    vxml_quickjs_compile_options_v1 options =
        vxml_quickjs_default_compile_options();

    /* Full backing storage remains valid throughout. Advertised prefixes must
     * never enable a capability whose complete tail is not present. */
    for (size_t bytes = 0u; bytes <= sizeof(options); ++bytes) {
        options.struct_size = bytes;
        const vxml_status script_status = bytes < historical_prefix
            ? VXML_INVALID_ARGUMENT : VXML_OK;
        const vxml_status data_status = bytes < historical_prefix
            ? VXML_INVALID_ARGUMENT : bytes < data_tail
            ? VXML_UNSUPPORTED_FEATURE : VXML_OK;
        if (!compile_matches(script, sizeof(script) - 1u, &options, script_status) ||
            !compile_matches(data, sizeof(data) - 1u, &options, data_status))
            return 1;
    }
    for (unsigned mask = 0u; mask != 8u; ++mask) {
        options = vxml_quickjs_default_compile_options();
        options.max_data_rows = (mask & 1u) != 0u ? 1u : 0u;
        options.max_data_uri_bytes = (mask & 2u) != 0u ? 64u : 0u;
        options.max_data_namelist_fields = (mask & 4u) != 0u ? 1u : 0u;
        const vxml_status expected = mask == 7u ? VXML_OK
            : mask == 0u ? VXML_UNSUPPORTED_FEATURE : VXML_INVALID_ARGUMENT;
        if (!compile_matches(data, sizeof(data) - 1u, &options, expected))
            return 1;
    }
    options = vxml_quickjs_default_compile_options();
    options.max_expression_bytes = sizeof("'x.js'") - 2u;
    if (!compile_matches(dynamic_script, sizeof(dynamic_script) - 1u,
            &options, VXML_LIMIT_EXCEEDED)) return 1;
    ++options.max_expression_bytes;
    if (!compile_matches(dynamic_script, sizeof(dynamic_script) - 1u,
            &options, VXML_OK)) return 1;
    return 0;
}

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
    int result = run_option_contracts();
    if (result == 0) result = voicexml_fuzz_run(
        VOICEXML_QUICKJS_FUZZ_CORPUS_DIR,
        seeds, sizeof(seeds) / sizeof(seeds[0]),
        16u, 4096u, run_case, NULL);
    if (result != 0)
        fprintf(stderr,
                "VoiceXML QuickJS metadata fuzz smoke failed\n");
    return result;
}
