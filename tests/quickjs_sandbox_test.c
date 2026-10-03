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

static quickjs_sandbox_status eval_string_from_deep_host_stack(
    quickjs_sandbox_runtime *runtime,
    size_t depth,
    const char **out_string, size_t *out_size,
    char *diagnostic, size_t diagnostic_capacity) {
    static const char expression[] = "'deep-string-entry'";
    volatile unsigned char host_stack_pad[2048];
    quickjs_sandbox_status status;
    host_stack_pad[0] = (unsigned char)depth;
    host_stack_pad[sizeof(host_stack_pad) - 1u] =
        (unsigned char)(depth ^ 0xa5u);
    if (depth != 0u) {
        status = eval_string_from_deep_host_stack(
            runtime, depth - 1u,
            out_string, out_size,
            diagnostic, diagnostic_capacity);
    } else {
        status = quickjs_sandbox_eval_expression_string(
            runtime,
            expression, sizeof(expression) - 1u,
            "<deep-string-entry>",
            UINT64_C(50),
            out_string, out_size,
            diagnostic, diagnostic_capacity);
    }
    if (host_stack_pad[0] != (unsigned char)depth ||
        host_stack_pad[sizeof(host_stack_pad) - 1u] !=
            (unsigned char)(depth ^ 0xa5u))
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
        {
            const char *value = NULL;
            size_t value_size = 0u;
            check_equal(
                eval_string_from_deep_host_stack(
                    &runtime, 48u,
                    &value, &value_size,
                    diagnostic, sizeof(diagnostic)),
                QUICKJS_SANDBOX_OK);
            check_not_null(value);
            check_equal(
                value_size,
                sizeof("deep-string-entry") - 1u);
            check_equal(
                memcmp(
                    value, "deep-string-entry",
                    value_size),
                0);
        }
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

    it("stringifies only deterministic scalar expression results") {
        static const char *const accepted[] = {
            "'alpha'", "true", "42", "-1.25"
        };
        static const char *const expected[] = {
            "alpha", "true", "42", "-1.25"
        };
        static const char *const rejected[] = {
            "null", "undefined", "({})", "(()=>{})", "NaN", "Infinity"
        };
        quickjs_sandbox_options options = sandbox_options();
        quickjs_sandbox_runtime runtime = {0};
        char diagnostic[256] = {0};
        size_t index;

        check_equal(
            quickjs_sandbox_runtime_init(
                &runtime, &options,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        for (index = 0u;
             index < sizeof(accepted) / sizeof(accepted[0]);
             ++index) {
            const char *value = NULL;
            size_t value_size = 0u;
            check_equal(
                quickjs_sandbox_eval_expression_scalar_string(
                    &runtime,
                    accepted[index], strlen(accepted[index]),
                    "<scalar>",
                    options.max_eval_milliseconds,
                    &value, &value_size,
                    diagnostic, sizeof(diagnostic)),
                QUICKJS_SANDBOX_OK);
            check_not_null(value);
            check_equal(value_size, strlen(expected[index]));
            check_equal(
                memcmp(value, expected[index], value_size),
                0);
        }
        for (index = 0u;
             index < sizeof(rejected) / sizeof(rejected[0]);
             ++index) {
            const char *value = NULL;
            size_t value_size = 0u;
            check_equal(
                quickjs_sandbox_eval_expression_scalar_string(
                    &runtime,
                    rejected[index], strlen(rejected[index]),
                    "<scalar-reject>",
                    options.max_eval_milliseconds,
                    &value, &value_size,
                    diagnostic, sizeof(diagnostic)),
                QUICKJS_SANDBOX_TYPE_MISMATCH);
            check_null(value);
            check_equal(value_size, (size_t)0u);
        }
        quickjs_sandbox_runtime_destroy(&runtime);

        options = sandbox_options();
        options.max_string_bytes = 3u;
        check_equal(
            quickjs_sandbox_runtime_init(
                &runtime, &options,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        {
            const char *value = NULL;
            size_t value_size = 0u;
            check_equal(
                quickjs_sandbox_eval_expression_scalar_string(
                    &runtime,
                    "'abcd'", sizeof("'abcd'") - 1u,
                    "<scalar-overflow>",
                    options.max_eval_milliseconds,
                    &value, &value_size,
                    diagnostic, sizeof(diagnostic)),
                QUICKJS_SANDBOX_LIMIT_EXCEEDED);
            check_null(value);
            check_equal(value_size, (size_t)0u);
        }
        quickjs_sandbox_runtime_destroy(&runtime);
    }


    it("reads exact global namelist properties without evaluating names") {
        static const char setup[] =
            "globalThis.alpha='A';"
            "globalThis.count=42;"
            "globalThis.flag=true;"
            "globalThis['na-me']='dash';"
            "globalThis.obj={};"
            "globalThis.sym=Symbol('x');";
        static const char *const names[] = {
            "alpha", "count", "flag", "na-me"
        };
        static const char *const expected[] = {
            "A", "42", "true", "dash"
        };
        static const char *const rejected[] = {
            "obj", "sym", "Object", "missing"
        };
        quickjs_sandbox_options options = sandbox_options();
        quickjs_sandbox_runtime runtime = {0};
        char diagnostic[256] = {0};
        size_t index;

        check_equal(
            quickjs_sandbox_runtime_init(
                &runtime, &options,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);
        check_equal(
            quickjs_sandbox_runtime_eval(
                &runtime,
                setup, sizeof(setup) - 1u,
                "<global-setup>",
                options.max_eval_milliseconds,
                diagnostic, sizeof(diagnostic)),
            QUICKJS_SANDBOX_OK);

        for (index = 0u;
             index < sizeof(names) / sizeof(names[0]);
             ++index) {
            const char *value = NULL;
            size_t value_size = 0u;
            check_equal(
                quickjs_sandbox_get_global_scalar_string(
                    &runtime,
                    names[index], strlen(names[index]),
                    options.max_eval_milliseconds,
                    &value, &value_size,
                    diagnostic, sizeof(diagnostic)),
                QUICKJS_SANDBOX_OK);
            check_not_null(value);
            check_equal(value_size, strlen(expected[index]));
            check_equal(
                memcmp(value, expected[index], value_size),
                0);
        }

        for (index = 0u;
             index < sizeof(rejected) / sizeof(rejected[0]);
             ++index) {
            const char *value = NULL;
            size_t value_size = 0u;
            check_equal(
                quickjs_sandbox_get_global_scalar_string(
                    &runtime,
                    rejected[index], strlen(rejected[index]),
                    options.max_eval_milliseconds,
                    &value, &value_size,
                    diagnostic, sizeof(diagnostic)),
                QUICKJS_SANDBOX_TYPE_MISMATCH);
            check_null(value);
            check_equal(value_size, (size_t)0u);
        }
        quickjs_sandbox_runtime_destroy(&runtime);
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
