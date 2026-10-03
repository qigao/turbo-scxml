#include "quickjs_sandbox.h"
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

static quickjs_sandbox_options sandbox_options(void) {
    return (quickjs_sandbox_options){
        .max_source_bytes = 4096u,
        .max_string_bytes = 1024u,
        .max_heap_bytes = 4u * 1024u * 1024u,
        .max_stack_bytes = 256u * 1024u,
        .max_eval_milliseconds = UINT64_C(50)};
}

static quickjs_sandbox_status recreate_from_deep_host_stack(
    quickjs_sandbox_runtime *runtime,
    size_t depth,
    char *diagnostic, size_t diagnostic_capacity) {
    volatile unsigned char host_stack_pad[2048];
    quickjs_sandbox_status status;
    host_stack_pad[0] = (unsigned char)depth;
    host_stack_pad[sizeof(host_stack_pad) - 1u] =
        (unsigned char)(depth ^ 0x5au);
    if (depth != 0u)
        status = recreate_from_deep_host_stack(
            runtime, depth - 1u,
            diagnostic, diagnostic_capacity);
    else
        status = quickjs_sandbox_context_recreate(
            runtime, diagnostic, diagnostic_capacity);
    if (host_stack_pad[0] != (unsigned char)depth ||
        host_stack_pad[sizeof(host_stack_pad) - 1u] !=
            (unsigned char)(depth ^ 0x5au))
        return QUICKJS_SANDBOX_EXCEPTION;
    return status;
}

spec("private QuickJS sandbox kernel") {
    it("validates hard positive limits and preserves max-string overflow status") {
        quickjs_sandbox_options options = sandbox_options();
        quickjs_sandbox_runtime runtime = {0};
        char diagnostic[128] = {0};

        check_true(quickjs_sandbox_limits_valid(&options));
        options.max_heap_bytes = 0u;
        check_false(quickjs_sandbox_limits_valid(&options));
        options = sandbox_options();
        options.max_string_bytes = SIZE_MAX;
        check_true(quickjs_sandbox_limits_valid(&options));
        check_equal(
            quickjs_sandbox_runtime_init(
                &runtime, &options,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_LIMIT_EXCEEDED);
        check_null(runtime.runtime);
        check_null(runtime.context);
        quickjs_sandbox_runtime_destroy(&runtime);
    }

    it("hardens host surfaces and recreates a clean context") {
        static const char hardened[] =
            "if (typeof fetch !== 'undefined' || "
            "typeof std !== 'undefined' || "
            "typeof os !== 'undefined' || "
            "typeof process !== 'undefined' || "
            "typeof require !== 'undefined' || "
            "typeof XMLHttpRequest !== 'undefined' || "
            "typeof WebSocket !== 'undefined' || "
            "typeof eval !== 'undefined' || "
            "typeof Function !== 'undefined') "
            "throw new Error('host surface exposed');"
            "globalThis.__sandbox_probe = 7;";
        static const char clean[] =
            "if (typeof __sandbox_probe !== 'undefined') "
            "throw new Error('context state leaked');";
        quickjs_sandbox_options options = sandbox_options();
        quickjs_sandbox_runtime runtime = {0};
        char diagnostic[256] = {0};

        check_equal(
            quickjs_sandbox_runtime_init(
                &runtime, &options,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_equal(
            quickjs_sandbox_runtime_eval(
                &runtime,
                hardened, sizeof(hardened) - 1u,
                "<kernel-hardening>",
                options.max_eval_milliseconds,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_equal(
            quickjs_sandbox_context_recreate(
                &runtime, diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_equal(
            quickjs_sandbox_runtime_eval(
                &runtime,
                clean, sizeof(clean) - 1u,
                "<kernel-fresh-context>",
                options.max_eval_milliseconds,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        quickjs_sandbox_runtime_destroy(&runtime);
        check_null(runtime.runtime);
        check_null(runtime.context);
    }

    it("refreshes QuickJS stack top across deep host call stacks") {
        static const char after[] =
            "globalThis.deepHostEntry = 1;";
        quickjs_sandbox_options options = sandbox_options();
        quickjs_sandbox_runtime runtime = {0};
        char diagnostic[256] = {0};

        check_equal(
            quickjs_sandbox_runtime_init(
                &runtime, &options,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_equal(
            recreate_from_deep_host_stack(
                &runtime, 48u,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_equal(
            quickjs_sandbox_runtime_eval(
                &runtime,
                after, sizeof(after) - 1u,
                "<deep-host-entry>",
                options.max_eval_milliseconds,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        quickjs_sandbox_runtime_destroy(&runtime);
    }

    it("validates source and expression syntax without executing them") {
        static const char empty[] = "";
        static const char source[] = "globalThis.answer = 42;";
        static const char bad_source[] = "let =";
        static const char expression[] = "1 + 2";
        static const char bad_expression[] = "(";
        quickjs_sandbox_options options = sandbox_options();
        char diagnostic[256] = {0};

        check_equal(
            quickjs_sandbox_validate_source(
                &options, empty, 0u, "<empty>",
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_equal(
            quickjs_sandbox_validate_source(
                &options, source, sizeof(source) - 1u,
                "<source>", diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_equal(
            quickjs_sandbox_validate_source(
                &options, bad_source, sizeof(bad_source) - 1u,
                "<bad-source>", diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_EXCEPTION);
        check_equal(
            quickjs_sandbox_validate_expression(
                &options, expression, sizeof(expression) - 1u,
                "<expression>", diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_equal(
            quickjs_sandbox_validate_expression(
                &options,
                bad_expression, sizeof(bad_expression) - 1u,
                "<bad-expression>",
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_EXCEPTION);
    }

    it("interrupts an infinite evaluation and recovers with a fresh context") {
        static const char spin[] = "for (;;) {}";
        static const char after[] = "globalThis.afterTimeout = 1;";
        quickjs_sandbox_options options = sandbox_options();
        quickjs_sandbox_runtime runtime = {0};
        char diagnostic[256] = {0};

        check_equal(
            quickjs_sandbox_runtime_init(
                &runtime, &options,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_equal(
            quickjs_sandbox_runtime_eval(
                &runtime,
                spin, sizeof(spin) - 1u,
                "<deadline>", UINT64_C(5),
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_LIMIT_EXCEEDED);
        check_true(runtime.interrupted);
        check_equal(
            quickjs_sandbox_context_recreate(
                &runtime, diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_equal(
            quickjs_sandbox_runtime_eval(
                &runtime,
                after, sizeof(after) - 1u,
                "<after-timeout>",
                options.max_eval_milliseconds,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        quickjs_sandbox_runtime_destroy(&runtime);
    }
}
