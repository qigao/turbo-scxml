#include "quickjs_sandbox.h"

#include <salts/clock.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if TURBOSCXML_HAS_QUICKJS
#include <quickjs.h>
#endif

#if TURBOSCXML_HAS_QUICKJS
static const char quickjs_sandbox_hardening_source[] =
    "(function(){"
    "const deny=['fetch','std','os','process','require','module',"
    "'Deno','Bun','XMLHttpRequest','WebSocket'];"
    "for(const name of deny)Object.defineProperty(globalThis,name,{"
    "value:undefined,writable:false,configurable:false});"
    "Object.defineProperty(Function.prototype,'constructor',{"
    "value:undefined,writable:false,configurable:false});"
    "for(const value of [Object.prototype,Array.prototype,"
    "Function.prototype,Number.prototype,String.prototype,"
    "Boolean.prototype])Object.freeze(value);"
    "Object.defineProperty(globalThis,'eval',{value:undefined,"
    "writable:false,configurable:false});"
    "Object.defineProperty(globalThis,'Function',{value:undefined,"
    "writable:false,configurable:false});"
    "})();";
#endif

bool quickjs_sandbox_limits_valid(
    const quickjs_sandbox_options *options) {
    return options != NULL &&
        options->max_source_bytes != 0u &&
        options->max_string_bytes != 0u &&
        options->max_heap_bytes != 0u &&
        options->max_stack_bytes != 0u &&
        options->max_eval_milliseconds != 0u;
}

void quickjs_sandbox_diagnostic(
    char *diagnostic, size_t capacity, const char *message) {
    if (diagnostic == NULL || capacity == 0u) return;
    (void)snprintf(
        diagnostic, capacity, "%s",
        message != NULL ? message : "");
}

bool quickjs_sandbox_deadline_expired(
    quickjs_sandbox_runtime *runtime) {
    if (runtime == NULL || runtime->deadline_ms == 0u)
        return false;
    if (salts_monotonic_ms() < runtime->deadline_ms)
        return false;
    runtime->interrupted = true;
    return true;
}

#if TURBOSCXML_HAS_QUICKJS
static int quickjs_sandbox_interrupt(
    JSRuntime *js_runtime, void *opaque) {
    (void)js_runtime;
    return quickjs_sandbox_deadline_expired(
        (quickjs_sandbox_runtime *)opaque) ? 1 : 0;
}
#endif

bool quickjs_sandbox_deadline_begin(
    quickjs_sandbox_runtime *runtime, uint64_t milliseconds) {
    uint64_t now;
    if (runtime == NULL || milliseconds == 0u)
        return false;
#if TURBOSCXML_HAS_QUICKJS
    if (runtime->runtime != NULL)
        JS_UpdateStackTop((JSRuntime *)runtime->runtime);
#endif
    if (runtime->deadline_ms != 0u)
        return false;
    now = salts_monotonic_ms();
    runtime->deadline_ms =
        now > UINT64_MAX - milliseconds
            ? UINT64_MAX : now + milliseconds;
    runtime->interrupted = false;
    return true;
}

void quickjs_sandbox_deadline_end(
    quickjs_sandbox_runtime *runtime, bool owned) {
    if (runtime != NULL && owned)
        runtime->deadline_ms = 0u;
}

quickjs_sandbox_status quickjs_sandbox_exception(
    quickjs_sandbox_runtime *runtime,
    char *diagnostic, size_t diagnostic_capacity) {
#if !TURBOSCXML_HAS_QUICKJS
    (void)runtime;
    quickjs_sandbox_diagnostic(
        diagnostic, diagnostic_capacity,
        "TurboSCXML was built without quickjs-sandbox support");
    return QUICKJS_SANDBOX_INVALID_ARGUMENT;
#else
    JSContext *context;
    JSValue exception;
    const char *message;
    quickjs_sandbox_status status;
    if (runtime == NULL || runtime->context == NULL)
        return QUICKJS_SANDBOX_INVALID_ARGUMENT;
    context = (JSContext *)runtime->context;
    exception = JS_GetException(context);
    message = JS_ToCString(context, exception);
    status = runtime->interrupted
        ? QUICKJS_SANDBOX_LIMIT_EXCEEDED
        : QUICKJS_SANDBOX_EXCEPTION;
    quickjs_sandbox_diagnostic(
        diagnostic, diagnostic_capacity,
        runtime->interrupted
            ? "QuickJS evaluation deadline exceeded"
            : (message != NULL ? message : "QuickJS exception"));
    if (message != NULL) JS_FreeCString(context, message);
    JS_FreeValue(context, exception);
    return status;
#endif
}

void quickjs_sandbox_context_destroy(
    quickjs_sandbox_runtime *runtime) {
    if (runtime == NULL) return;
#if TURBOSCXML_HAS_QUICKJS
    if (runtime->context != NULL)
        JS_FreeContext((JSContext *)runtime->context);
#endif
    runtime->context = NULL;
}

#if TURBOSCXML_HAS_QUICKJS
static quickjs_sandbox_status quickjs_sandbox_context_create(
    quickjs_sandbox_runtime *runtime,
    char *diagnostic, size_t diagnostic_capacity) {
    JSContext *context;
    quickjs_sandbox_status status;
    if (runtime == NULL || runtime->runtime == NULL ||
        runtime->context != NULL)
        return QUICKJS_SANDBOX_INVALID_ARGUMENT;
    context = JS_NewContextRaw((JSRuntime *)runtime->runtime);
    if (context == NULL)
        return QUICKJS_SANDBOX_ALLOCATION_FAILED;
    JS_AddIntrinsicBaseObjects(context);
    JS_AddIntrinsicEval(context);
    JS_AddIntrinsicJSON(context);
    runtime->context = context;
    status = quickjs_sandbox_runtime_eval(
        runtime,
        quickjs_sandbox_hardening_source,
        sizeof(quickjs_sandbox_hardening_source) - 1u,
        "<sandbox-init>",
        runtime->options.max_eval_milliseconds,
        diagnostic, diagnostic_capacity);
    if (status != QUICKJS_SANDBOX_OK)
        quickjs_sandbox_context_destroy(runtime);
    return status;
}
#endif

quickjs_sandbox_status quickjs_sandbox_context_recreate(
    quickjs_sandbox_runtime *runtime,
    char *diagnostic, size_t diagnostic_capacity) {
#if !TURBOSCXML_HAS_QUICKJS
    (void)runtime;
    quickjs_sandbox_diagnostic(
        diagnostic, diagnostic_capacity,
        "TurboSCXML was built without quickjs-sandbox support");
    return QUICKJS_SANDBOX_INVALID_ARGUMENT;
#else
    if (runtime == NULL)
        return QUICKJS_SANDBOX_INVALID_ARGUMENT;
    quickjs_sandbox_context_destroy(runtime);
    return quickjs_sandbox_context_create(
        runtime, diagnostic, diagnostic_capacity);
#endif
}

quickjs_sandbox_status quickjs_sandbox_runtime_init(
    quickjs_sandbox_runtime *runtime,
    const quickjs_sandbox_options *options,
    char *diagnostic, size_t diagnostic_capacity) {
    if (runtime == NULL || runtime->runtime != NULL ||
        runtime->context != NULL ||
        !quickjs_sandbox_limits_valid(options)) {
        quickjs_sandbox_diagnostic(
            diagnostic, diagnostic_capacity,
            "invalid QuickJS runtime options");
        return QUICKJS_SANDBOX_INVALID_ARGUMENT;
    }
#if !TURBOSCXML_HAS_QUICKJS
    quickjs_sandbox_diagnostic(
        diagnostic, diagnostic_capacity,
        "TurboSCXML was built without quickjs-sandbox support");
    return QUICKJS_SANDBOX_INVALID_ARGUMENT;
#else
    {
        JSRuntime *js_runtime;
        quickjs_sandbox_status status;
        if (options->max_string_bytes == SIZE_MAX)
            return QUICKJS_SANDBOX_LIMIT_EXCEEDED;
        js_runtime = JS_NewRuntime();
        if (js_runtime == NULL)
            return QUICKJS_SANDBOX_ALLOCATION_FAILED;
        JS_SetMemoryLimit(js_runtime, options->max_heap_bytes);
        JS_SetMaxStackSize(js_runtime, options->max_stack_bytes);
        JS_SetCanBlock(js_runtime, false);
        JS_SetInterruptHandler(
            js_runtime, quickjs_sandbox_interrupt, runtime);
        runtime->runtime = js_runtime;
        runtime->options = *options;
        runtime->max_diagnostic_bytes = diagnostic_capacity;
        runtime->result_string_capacity =
            options->max_string_bytes + 1u;
        runtime->result_string =
            (char *)malloc(runtime->result_string_capacity);
        if (runtime->result_string == NULL) {
            quickjs_sandbox_runtime_destroy(runtime);
            return QUICKJS_SANDBOX_ALLOCATION_FAILED;
        }
        status = quickjs_sandbox_context_create(
            runtime, diagnostic, diagnostic_capacity);
        if (status != QUICKJS_SANDBOX_OK) {
            quickjs_sandbox_runtime_destroy(runtime);
            return status;
        }
    }
    return QUICKJS_SANDBOX_OK;
#endif
}

quickjs_sandbox_status quickjs_sandbox_runtime_eval(
    quickjs_sandbox_runtime *runtime,
    const char *source, size_t source_size,
    const char *filename,
    uint64_t max_eval_milliseconds,
    char *diagnostic, size_t diagnostic_capacity) {
    if (runtime == NULL || runtime->runtime == NULL ||
        runtime->context == NULL ||
        source == NULL || source_size == 0u ||
        source_size > runtime->options.max_source_bytes ||
        source_size == SIZE_MAX || filename == NULL ||
        max_eval_milliseconds == 0u) {
        quickjs_sandbox_diagnostic(
            diagnostic, diagnostic_capacity,
            "invalid QuickJS evaluation request");
        return QUICKJS_SANDBOX_INVALID_ARGUMENT;
    }
#if !TURBOSCXML_HAS_QUICKJS
    return QUICKJS_SANDBOX_INVALID_ARGUMENT;
#else
    {
        JSContext *context = (JSContext *)runtime->context;
        JSValue result;
        char *terminated_source =
            (char *)malloc(source_size + 1u);
        quickjs_sandbox_status status;
        bool owns_deadline;
        if (terminated_source == NULL) {
            quickjs_sandbox_diagnostic(
                diagnostic, diagnostic_capacity,
                "QuickJS evaluation source allocation failed");
            return QUICKJS_SANDBOX_ALLOCATION_FAILED;
        }
        memcpy(terminated_source, source, source_size);
        terminated_source[source_size] = '\0';
        owns_deadline = quickjs_sandbox_deadline_begin(
            runtime, max_eval_milliseconds);
        result = JS_Eval(
            context, terminated_source, source_size,
            filename, JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) {
            status = quickjs_sandbox_exception(
                runtime, diagnostic, diagnostic_capacity);
            quickjs_sandbox_deadline_end(
                runtime, owns_deadline);
            free(terminated_source);
            return status;
        }
        JS_FreeValue(context, result);
        if (quickjs_sandbox_deadline_expired(runtime)) {
            quickjs_sandbox_deadline_end(
                runtime, owns_deadline);
            free(terminated_source);
            quickjs_sandbox_diagnostic(
                diagnostic, diagnostic_capacity,
                "QuickJS evaluation deadline exceeded");
            return QUICKJS_SANDBOX_LIMIT_EXCEEDED;
        }
        quickjs_sandbox_deadline_end(runtime, owns_deadline);
        free(terminated_source);
        quickjs_sandbox_diagnostic(
            diagnostic, diagnostic_capacity, "");
        return QUICKJS_SANDBOX_OK;
    }
#endif
}

quickjs_sandbox_status quickjs_sandbox_eval_expression_string(
    quickjs_sandbox_runtime *runtime,
    const char *source, size_t source_size,
    const char *filename,
    uint64_t max_eval_milliseconds,
    const char **out_string, size_t *out_size,
    char *diagnostic, size_t diagnostic_capacity) {
    static const char prefix[] = "(\n";
    static const char suffix[] = "\n)";
    if (out_string != NULL) *out_string = NULL;
    if (out_size != NULL) *out_size = 0u;
    if (runtime == NULL || runtime->runtime == NULL ||
        runtime->context == NULL ||
        source == NULL || source_size == 0u ||
        source_size > runtime->options.max_source_bytes ||
        source_size > SIZE_MAX - (sizeof(prefix) - 1u) -
                          (sizeof(suffix) - 1u) - 1u ||
        filename == NULL || max_eval_milliseconds == 0u ||
        out_string == NULL || out_size == NULL ||
        runtime->result_string == NULL ||
        runtime->result_string_capacity == 0u) {
        quickjs_sandbox_diagnostic(
            diagnostic, diagnostic_capacity,
            "invalid QuickJS string-expression request");
        return QUICKJS_SANDBOX_INVALID_ARGUMENT;
    }
#if !TURBOSCXML_HAS_QUICKJS
    return QUICKJS_SANDBOX_INVALID_ARGUMENT;
#else
    {
        JSContext *context = (JSContext *)runtime->context;
        char *wrapped = NULL;
        size_t wrapped_size;
        JSValue result = JS_UNDEFINED;
        const char *text = NULL;
        size_t text_size = 0u;
        bool owns_deadline;
        quickjs_sandbox_status status = QUICKJS_SANDBOX_OK;

        wrapped_size =
            sizeof(prefix) - 1u + source_size + sizeof(suffix) - 1u;
        wrapped = (char *)malloc(wrapped_size + 1u);
        if (wrapped == NULL)
            return QUICKJS_SANDBOX_ALLOCATION_FAILED;
        memcpy(wrapped, prefix, sizeof(prefix) - 1u);
        memcpy(wrapped + sizeof(prefix) - 1u, source, source_size);
        memcpy(
            wrapped + sizeof(prefix) - 1u + source_size,
            suffix, sizeof(suffix) - 1u);
        wrapped[wrapped_size] = '\0';

        owns_deadline = quickjs_sandbox_deadline_begin(
            runtime, max_eval_milliseconds);
        JS_UpdateStackTop((JSRuntime *)runtime->runtime);
        result = JS_Eval(
            context, wrapped, wrapped_size,
            filename, JS_EVAL_TYPE_GLOBAL);
        free(wrapped);
        if (JS_IsException(result)) {
            status = quickjs_sandbox_exception(
                runtime, diagnostic, diagnostic_capacity);
            goto done;
        }
        if (!JS_IsString(result)) {
            quickjs_sandbox_diagnostic(
                diagnostic, diagnostic_capacity,
                "QuickJS expression result is not a string");
            status = QUICKJS_SANDBOX_TYPE_MISMATCH;
            goto done;
        }
        text = JS_ToCStringLen(context, &text_size, result);
        if (text == NULL) {
            status = quickjs_sandbox_exception(
                runtime, diagnostic, diagnostic_capacity);
            goto done;
        }
        if (text_size >= runtime->result_string_capacity) {
            quickjs_sandbox_diagnostic(
                diagnostic, diagnostic_capacity,
                "QuickJS expression string exceeds result bound");
            status = QUICKJS_SANDBOX_LIMIT_EXCEEDED;
            goto done;
        }
        memcpy(runtime->result_string, text, text_size);
        runtime->result_string[text_size] = '\0';
        *out_string = runtime->result_string;
        *out_size = text_size;
        if (quickjs_sandbox_deadline_expired(runtime)) {
            *out_string = NULL;
            *out_size = 0u;
            quickjs_sandbox_diagnostic(
                diagnostic, diagnostic_capacity,
                "QuickJS evaluation deadline exceeded");
            status = QUICKJS_SANDBOX_LIMIT_EXCEEDED;
            goto done;
        }
        quickjs_sandbox_diagnostic(
            diagnostic, diagnostic_capacity, "");

done:
        if (text != NULL) JS_FreeCString(context, text);
        if (!JS_IsUndefined(result))
            JS_FreeValue(context, result);
        quickjs_sandbox_deadline_end(runtime, owns_deadline);
        if (status != QUICKJS_SANDBOX_OK) {
            *out_string = NULL;
            *out_size = 0u;
        }
        return status;
    }
#endif
}


quickjs_sandbox_status quickjs_sandbox_eval_expression_scalar_string(
    quickjs_sandbox_runtime *runtime,
    const char *source, size_t source_size,
    const char *filename,
    uint64_t max_eval_milliseconds,
    const char **out_string, size_t *out_size,
    char *diagnostic, size_t diagnostic_capacity) {
    static const char prefix[] = "(\n";
    static const char suffix[] = "\n)";
    if (out_string != NULL) *out_string = NULL;
    if (out_size != NULL) *out_size = 0u;
    if (runtime == NULL || runtime->runtime == NULL ||
        runtime->context == NULL ||
        source == NULL || source_size == 0u ||
        source_size > runtime->options.max_source_bytes ||
        source_size > SIZE_MAX - (sizeof(prefix) - 1u) -
                          (sizeof(suffix) - 1u) - 1u ||
        filename == NULL || max_eval_milliseconds == 0u ||
        out_string == NULL || out_size == NULL ||
        runtime->result_string == NULL ||
        runtime->result_string_capacity == 0u) {
        quickjs_sandbox_diagnostic(
            diagnostic, diagnostic_capacity,
            "invalid QuickJS scalar-expression request");
        return QUICKJS_SANDBOX_INVALID_ARGUMENT;
    }
#if !TURBOSCXML_HAS_QUICKJS
    return QUICKJS_SANDBOX_INVALID_ARGUMENT;
#else
    {
        JSContext *context = (JSContext *)runtime->context;
        char *wrapped = NULL;
        size_t wrapped_size;
        JSValue result = JS_UNDEFINED;
        const char *text = NULL;
        size_t text_size = 0u;
        bool owns_deadline;
        quickjs_sandbox_status status = QUICKJS_SANDBOX_OK;

        wrapped_size =
            sizeof(prefix) - 1u + source_size + sizeof(suffix) - 1u;
        wrapped = (char *)malloc(wrapped_size + 1u);
        if (wrapped == NULL)
            return QUICKJS_SANDBOX_ALLOCATION_FAILED;
        memcpy(wrapped, prefix, sizeof(prefix) - 1u);
        memcpy(wrapped + sizeof(prefix) - 1u, source, source_size);
        memcpy(
            wrapped + sizeof(prefix) - 1u + source_size,
            suffix, sizeof(suffix) - 1u);
        wrapped[wrapped_size] = '\0';

        owns_deadline = quickjs_sandbox_deadline_begin(
            runtime, max_eval_milliseconds);
        JS_UpdateStackTop((JSRuntime *)runtime->runtime);
        result = JS_Eval(
            context, wrapped, wrapped_size,
            filename, JS_EVAL_TYPE_GLOBAL);
        free(wrapped);
        if (JS_IsException(result)) {
            status = quickjs_sandbox_exception(
                runtime, diagnostic, diagnostic_capacity);
            goto done;
        }
        if (JS_IsNumber(result)) {
            double number;
            if (JS_ToFloat64(context, &number, result) != 0 ||
                !isfinite(number)) {
                quickjs_sandbox_diagnostic(
                    diagnostic, diagnostic_capacity,
                    "QuickJS scalar number must be finite");
                status = QUICKJS_SANDBOX_TYPE_MISMATCH;
                goto done;
            }
        } else if (!JS_IsString(result) && !JS_IsBool(result)) {
            quickjs_sandbox_diagnostic(
                diagnostic, diagnostic_capacity,
                "QuickJS expression result is not a supported scalar");
            status = QUICKJS_SANDBOX_TYPE_MISMATCH;
            goto done;
        }

        text = JS_ToCStringLen(context, &text_size, result);
        if (text == NULL) {
            status = quickjs_sandbox_exception(
                runtime, diagnostic, diagnostic_capacity);
            goto done;
        }
        if (text_size >= runtime->result_string_capacity) {
            quickjs_sandbox_diagnostic(
                diagnostic, diagnostic_capacity,
                "QuickJS scalar string exceeds result bound");
            status = QUICKJS_SANDBOX_LIMIT_EXCEEDED;
            goto done;
        }
        memcpy(runtime->result_string, text, text_size);
        runtime->result_string[text_size] = '\0';
        *out_string = runtime->result_string;
        *out_size = text_size;
        if (quickjs_sandbox_deadline_expired(runtime)) {
            *out_string = NULL;
            *out_size = 0u;
            quickjs_sandbox_diagnostic(
                diagnostic, diagnostic_capacity,
                "QuickJS evaluation deadline exceeded");
            status = QUICKJS_SANDBOX_LIMIT_EXCEEDED;
            goto done;
        }
        quickjs_sandbox_diagnostic(
            diagnostic, diagnostic_capacity, "");

done:
        if (text != NULL) JS_FreeCString(context, text);
        if (!JS_IsUndefined(result))
            JS_FreeValue(context, result);
        quickjs_sandbox_deadline_end(runtime, owns_deadline);
        if (status != QUICKJS_SANDBOX_OK) {
            *out_string = NULL;
            *out_size = 0u;
        }
        return status;
    }
#endif
}


quickjs_sandbox_status quickjs_sandbox_get_global_scalar_string(
    quickjs_sandbox_runtime *runtime,
    const char *name, size_t name_size,
    uint64_t max_eval_milliseconds,
    const char **out_string, size_t *out_size,
    char *diagnostic, size_t diagnostic_capacity) {
    if (out_string != NULL) *out_string = NULL;
    if (out_size != NULL) *out_size = 0u;
    if (runtime == NULL || runtime->runtime == NULL ||
        runtime->context == NULL ||
        name == NULL || name_size == 0u ||
        name_size == SIZE_MAX ||
        memchr(name, '\0', name_size) != NULL ||
        max_eval_milliseconds == 0u ||
        out_string == NULL || out_size == NULL ||
        runtime->result_string == NULL ||
        runtime->result_string_capacity == 0u) {
        quickjs_sandbox_diagnostic(
            diagnostic, diagnostic_capacity,
            "invalid QuickJS global scalar request");
        return QUICKJS_SANDBOX_INVALID_ARGUMENT;
    }
#if !TURBOSCXML_HAS_QUICKJS
    return QUICKJS_SANDBOX_INVALID_ARGUMENT;
#else
    {
        JSContext *context = (JSContext *)runtime->context;
        JSValue global = JS_UNDEFINED;
        JSValue value = JS_UNDEFINED;
        char *terminated_name = NULL;
        const char *text = NULL;
        size_t text_size = 0u;
        bool owns_deadline;
        quickjs_sandbox_status status = QUICKJS_SANDBOX_OK;

        terminated_name = (char *)malloc(name_size + 1u);
        if (terminated_name == NULL)
            return QUICKJS_SANDBOX_ALLOCATION_FAILED;
        memcpy(terminated_name, name, name_size);
        terminated_name[name_size] = '\0';

        owns_deadline = quickjs_sandbox_deadline_begin(
            runtime, max_eval_milliseconds);
        JS_UpdateStackTop((JSRuntime *)runtime->runtime);
        global = JS_GetGlobalObject(context);
        if (JS_IsException(global)) {
            status = quickjs_sandbox_exception(
                runtime, diagnostic, diagnostic_capacity);
            goto done;
        }
        value = JS_GetPropertyStr(
            context, global, terminated_name);
        if (JS_IsException(value)) {
            status = quickjs_sandbox_exception(
                runtime, diagnostic, diagnostic_capacity);
            goto done;
        }

        if (JS_IsNumber(value)) {
            double number;
            if (JS_ToFloat64(context, &number, value) != 0 ||
                !isfinite(number)) {
                quickjs_sandbox_diagnostic(
                    diagnostic, diagnostic_capacity,
                    "QuickJS global scalar number must be finite");
                status = QUICKJS_SANDBOX_TYPE_MISMATCH;
                goto done;
            }
        } else if (!JS_IsString(value) && !JS_IsBool(value)) {
            quickjs_sandbox_diagnostic(
                diagnostic, diagnostic_capacity,
                "QuickJS global property is not a supported scalar");
            status = QUICKJS_SANDBOX_TYPE_MISMATCH;
            goto done;
        }

        text = JS_ToCStringLen(
            context, &text_size, value);
        if (text == NULL) {
            status = quickjs_sandbox_exception(
                runtime, diagnostic, diagnostic_capacity);
            goto done;
        }
        if (text_size >= runtime->result_string_capacity) {
            quickjs_sandbox_diagnostic(
                diagnostic, diagnostic_capacity,
                "QuickJS global scalar exceeds result bound");
            status = QUICKJS_SANDBOX_LIMIT_EXCEEDED;
            goto done;
        }
        memcpy(runtime->result_string, text, text_size);
        runtime->result_string[text_size] = '\0';
        *out_string = runtime->result_string;
        *out_size = text_size;

        if (quickjs_sandbox_deadline_expired(runtime)) {
            *out_string = NULL;
            *out_size = 0u;
            quickjs_sandbox_diagnostic(
                diagnostic, diagnostic_capacity,
                "QuickJS evaluation deadline exceeded");
            status = QUICKJS_SANDBOX_LIMIT_EXCEEDED;
            goto done;
        }
        quickjs_sandbox_diagnostic(
            diagnostic, diagnostic_capacity, "");

done:
        if (text != NULL)
            JS_FreeCString(context, text);
        if (!JS_IsUndefined(value))
            JS_FreeValue(context, value);
        if (!JS_IsUndefined(global))
            JS_FreeValue(context, global);
        quickjs_sandbox_deadline_end(
            runtime, owns_deadline);
        free(terminated_name);
        if (status != QUICKJS_SANDBOX_OK) {
            *out_string = NULL;
            *out_size = 0u;
        }
        return status;
    }
#endif
}

void quickjs_sandbox_runtime_destroy(
    quickjs_sandbox_runtime *runtime) {
    if (runtime == NULL) return;
#if TURBOSCXML_HAS_QUICKJS
    quickjs_sandbox_context_destroy(runtime);
    if (runtime->runtime != NULL)
        JS_FreeRuntime((JSRuntime *)runtime->runtime);
#endif
    free(runtime->result_string);
    memset(runtime, 0, sizeof(*runtime));
}

quickjs_sandbox_status quickjs_sandbox_validate_source(
    const quickjs_sandbox_options *options,
    const char *source, size_t source_size,
    const char *filename,
    char *diagnostic, size_t diagnostic_capacity) {
    if (!quickjs_sandbox_limits_valid(options) ||
        source == NULL ||
        source_size > options->max_source_bytes ||
        filename == NULL) {
        quickjs_sandbox_diagnostic(
            diagnostic, diagnostic_capacity,
            "invalid or oversized QuickJS source");
        return source_size > 0u && options != NULL &&
                       source_size > options->max_source_bytes
            ? QUICKJS_SANDBOX_LIMIT_EXCEEDED
            : QUICKJS_SANDBOX_INVALID_ARGUMENT;
    }
#if !TURBOSCXML_HAS_QUICKJS
    return QUICKJS_SANDBOX_INVALID_ARGUMENT;
#else
    {
        quickjs_sandbox_runtime runtime = {0};
        JSContext *context;
        JSValue compiled;
        char *terminated_source;
        quickjs_sandbox_status status =
            quickjs_sandbox_runtime_init(
                &runtime, options,
                diagnostic, diagnostic_capacity);
        if (status != QUICKJS_SANDBOX_OK)
            return status;
        if (source_size == SIZE_MAX) {
            quickjs_sandbox_runtime_destroy(&runtime);
            return QUICKJS_SANDBOX_LIMIT_EXCEEDED;
        }
        terminated_source =
            (char *)malloc(source_size + 1u);
        if (terminated_source == NULL) {
            quickjs_sandbox_runtime_destroy(&runtime);
            return QUICKJS_SANDBOX_ALLOCATION_FAILED;
        }
        memcpy(terminated_source, source, source_size);
        terminated_source[source_size] = '\0';
        context = (JSContext *)runtime.context;
        JS_UpdateStackTop((JSRuntime *)runtime.runtime);
        compiled = JS_Eval(
            context, terminated_source, source_size,
            filename,
            JS_EVAL_TYPE_GLOBAL |
                JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(compiled))
            status = quickjs_sandbox_exception(
                &runtime, diagnostic, diagnostic_capacity);
        else {
            JS_FreeValue(context, compiled);
            status = QUICKJS_SANDBOX_OK;
        }
        free(terminated_source);
        quickjs_sandbox_runtime_destroy(&runtime);
        return status;
    }
#endif
}

quickjs_sandbox_status quickjs_sandbox_validate_expression(
    const quickjs_sandbox_options *options,
    const char *source, size_t source_size,
    const char *filename,
    char *diagnostic, size_t diagnostic_capacity) {
    static const char prefix[] = "(\n";
    static const char suffix[] = "\n)";
    size_t wrapped_size;
    char *wrapped;
    quickjs_sandbox_runtime runtime = {0};
    quickjs_sandbox_status status;
    if (!quickjs_sandbox_limits_valid(options) ||
        source == NULL || source_size == 0u ||
        source_size > options->max_source_bytes ||
        source_size >
            SIZE_MAX - (sizeof(prefix) - 1u) -
            (sizeof(suffix) - 1u) - 1u ||
        filename == NULL) {
        quickjs_sandbox_diagnostic(
            diagnostic, diagnostic_capacity,
            "invalid or oversized QuickJS expression");
        return source_size > 0u && options != NULL &&
                       source_size > options->max_source_bytes
            ? QUICKJS_SANDBOX_LIMIT_EXCEEDED
            : QUICKJS_SANDBOX_INVALID_ARGUMENT;
    }
#if !TURBOSCXML_HAS_QUICKJS
    return QUICKJS_SANDBOX_INVALID_ARGUMENT;
#else
    wrapped_size =
        sizeof(prefix) - 1u +
        source_size +
        sizeof(suffix) - 1u;
    wrapped = (char *)malloc(wrapped_size + 1u);
    if (wrapped == NULL)
        return QUICKJS_SANDBOX_ALLOCATION_FAILED;
    memcpy(wrapped, prefix, sizeof(prefix) - 1u);
    memcpy(
        wrapped + sizeof(prefix) - 1u,
        source, source_size);
    memcpy(
        wrapped + sizeof(prefix) - 1u + source_size,
        suffix, sizeof(suffix) - 1u);
    wrapped[wrapped_size] = '\0';
    status = quickjs_sandbox_runtime_init(
        &runtime, options,
        diagnostic, diagnostic_capacity);
    if (status == QUICKJS_SANDBOX_OK) {
        JSValue compiled;
        JS_UpdateStackTop((JSRuntime *)runtime.runtime);
        compiled = JS_Eval(
            (JSContext *)runtime.context,
            wrapped, wrapped_size,
            filename,
            JS_EVAL_TYPE_GLOBAL |
                JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(compiled))
            status = quickjs_sandbox_exception(
                &runtime, diagnostic, diagnostic_capacity);
        else
            JS_FreeValue(
                (JSContext *)runtime.context, compiled);
    }
    quickjs_sandbox_runtime_destroy(&runtime);
    free(wrapped);
    return status;
#endif
}
