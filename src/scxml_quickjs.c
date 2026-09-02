#include "scxml_quickjs.h"
#include "scxml_program.h"
#include "scxml_session.h"

#include <cmeta/container.h>
#include <turbo/clock.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if TURBOSCXML_HAS_QUICKJS
#include <quickjs.h>
#endif

enum {
    SCXML_QUICKJS_DEFAULT_HEAP_BYTES = 8u * 1024u * 1024u,
    SCXML_QUICKJS_DEFAULT_STACK_BYTES = 256u * 1024u,
    SCXML_QUICKJS_DEFAULT_EVAL_MILLISECONDS = 50u,
    SCXML_QUICKJS_DEFAULT_MAX_CONVERSION_DEPTH = 32u,
    SCXML_QUICKJS_DEFAULT_MAX_PROPERTIES = 4096u,
    SCXML_QUICKJS_DEFAULT_MAX_ARRAY_ITEMS = 4096u,
    SCXML_QUICKJS_DEFAULT_MAX_SCRIPT_VARIABLES = 256u,
    SCXML_QUICKJS_DEFAULT_MAX_SNAPSHOT_BYTES = 4u * 1024u * 1024u
};

#if TURBOSCXML_HAS_QUICKJS
static const char quickjs_hardening_source[] =
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

static scxml_status quickjs_fail(
    scxml_diagnostic *diagnostic, scxml_status status,
    const char *message) {
    if (diagnostic != NULL) {
        memset(diagnostic, 0, sizeof(*diagnostic));
        diagnostic->status = status;
        (void)snprintf(
            diagnostic->message, sizeof(diagnostic->message), "%s", message);
    }
    return status;
}

static void quickjs_diagnostic(
    char *diagnostic, size_t capacity, const char *message) {
    if (diagnostic == NULL || capacity == 0u) return;
    (void)snprintf(diagnostic, capacity, "%s", message != NULL ? message : "");
}

static bool quickjs_schema_supported(
    const cmeta_data_desc *root,
    const cmeta_data_desc *descriptor,
    const cmeta_declared_type *declared_type,
    size_t depth, size_t max_depth,
    size_t max_properties, bool inside_sequence,
    size_t *static_properties) {
    if (!cmeta_data_desc_valid(descriptor) || depth > max_depth ||
        (descriptor->kind != CMETA_DATA_SEQUENCE &&
         descriptor->storage_type == NULL))
        return false;
    switch (descriptor->kind) {
        case CMETA_DATA_BOOL:
            return descriptor->storage_type->size == sizeof(bool);
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT:
            return descriptor->storage_type->size == sizeof(uint8_t) ||
                descriptor->storage_type->size == sizeof(uint16_t) ||
                descriptor->storage_type->size == sizeof(uint32_t) ||
                descriptor->storage_type->size == sizeof(uint64_t);
        case CMETA_DATA_FLOAT:
            return descriptor->storage_type->size == sizeof(float) ||
                descriptor->storage_type->size == sizeof(double);
        case CMETA_DATA_STRING:
            return cmeta_data_buffer_ops_of(descriptor) != NULL;
        case CMETA_DATA_ENUM:
            return cmeta_data_enum_ops_of(descriptor) != NULL;
        case CMETA_DATA_STRUCT: {
            const cmeta_data_struct_shape *shape =
                (const cmeta_data_struct_shape *)descriptor->shape;
            size_t index;
            if (shape == NULL || shape->layout == NULL ||
                shape->fields == NULL ||
                shape->field_count != shape->layout->field_count ||
                shape->field_count > max_properties)
                return false;
            for (index = 0u; index < shape->field_count; ++index) {
                const cmeta_data_field_desc *field = &shape->fields[index];
                const cmeta_field_desc *layout_field;
                if (field->name == NULL || field->value == NULL ||
                    (!inside_sequence &&
                     (*static_properties >= max_properties ||
                      ++*static_properties > max_properties)) ||
                    (inside_sequence &&
                     field->value->kind == CMETA_DATA_SEQUENCE))
                    return false;
                layout_field = cmeta_struct_find_field(
                    shape->layout, field->name);
                if (layout_field == NULL ||
                    layout_field->offset != field->offset ||
                    !quickjs_schema_supported(
                        root, field->value, layout_field->declared_type,
                        depth + 1u, max_depth, max_properties,
                        inside_sequence, static_properties))
                    return false;
            }
            return true;
        }
        case CMETA_DATA_SEQUENCE: {
            const cmeta_type_desc *element_type;
            const cmeta_data_desc *element_data;
            if (!cmeta_declared_type_valid(declared_type) ||
                declared_type->arity != 1u ||
                (descriptor->storage_type != NULL &&
                 !cmeta_type_equal(
                     declared_type->storage_type,
                     descriptor->storage_type)))
                return false;
            element_type = cmeta_declared_type_argument(declared_type, 0u);
            element_data = scxml_scope_find_data_for_type(
                root, element_type, max_depth);
            /* CMeta's element descriptor does not retain nested generic type
             * arguments, so that shape cannot be rebuilt transactionally. */
            return element_data != NULL &&
                element_data->kind != CMETA_DATA_SEQUENCE &&
                quickjs_schema_supported(
                    root, element_data, NULL, depth + 1u, max_depth,
                    max_properties, true, static_properties);
        }
        default:
            return false;
    }
}

bool scxml_quickjs_static_property_budget_valid(
    const scxml_quickjs_compile_options_v1 *options,
    size_t supplemental_properties) {
    size_t static_properties = supplemental_properties;
    return options != NULL && supplemental_properties <= options->max_properties &&
        quickjs_schema_supported(
            options->root, options->root, NULL, 0u,
            options->max_conversion_depth, options->max_properties, false,
            &static_properties);
}

bool scxml_quickjs_limits_valid(
    const scxml_quickjs_compile_options_v1 *options) {
    return options != NULL &&
        options->abi_version == SCXML_QUICKJS_COMPILE_OPTIONS_ABI_V1 &&
        options->struct_size >= sizeof(*options) &&
        cmeta_data_desc_valid(options->root) &&
        options->root->kind == CMETA_DATA_STRUCT &&
        options->root->storage_type != NULL &&
        options->max_source_bytes != 0u &&
        options->max_instructions != 0u &&
        options->max_operands != 0u &&
        options->max_expression_depth != 0u &&
        options->max_path_depth != 0u &&
        options->max_literal_bytes != 0u &&
        options->max_string_bytes != 0u &&
        options->max_iterations != 0u &&
        options->max_script_variables != 0u &&
        options->max_heap_bytes != 0u &&
        options->max_stack_bytes != 0u &&
        options->max_eval_milliseconds != 0u &&
        options->max_conversion_depth != 0u &&
        options->max_properties != 0u &&
        options->max_array_items != 0u &&
        options->max_snapshot_bytes != 0u &&
        scxml_quickjs_static_property_budget_valid(options, 0u);
}

#if TURBOSCXML_HAS_QUICKJS
static const int64_t quickjs_max_safe_integer =
    INT64_C(9007199254740991);
static const int64_t quickjs_min_safe_integer =
    -INT64_C(9007199254740991);

static bool quickjs_deadline_expired(scxml_quickjs_runtime *runtime) {
    if (runtime == NULL || runtime->deadline_ms == 0u) return false;
    if (turbo_monotonic_ms() < runtime->deadline_ms) return false;
    runtime->interrupted = true;
    return true;
}

static int quickjs_interrupt(JSRuntime *js_runtime, void *opaque) {
    (void)js_runtime;
    return quickjs_deadline_expired((scxml_quickjs_runtime *)opaque) ? 1 : 0;
}

static bool quickjs_deadline_begin(
    scxml_quickjs_runtime *runtime, uint64_t milliseconds) {
    uint64_t now;
    if (runtime->deadline_ms != 0u) return false;
    now = turbo_monotonic_ms();
    runtime->deadline_ms = now > UINT64_MAX - milliseconds
        ? UINT64_MAX : now + milliseconds;
    runtime->interrupted = false;
    return true;
}

static void quickjs_deadline_end(
    scxml_quickjs_runtime *runtime, bool owned) {
    if (owned) runtime->deadline_ms = 0u;
}

static scxml_quickjs_status quickjs_exception(
    scxml_quickjs_runtime *runtime, char *diagnostic,
    size_t diagnostic_capacity) {
    JSContext *context = (JSContext *)runtime->context;
    JSValue exception = JS_GetException(context);
    const char *message = JS_ToCString(context, exception);
    const scxml_quickjs_status status = runtime->interrupted
        ? SCXML_QUICKJS_LIMIT_EXCEEDED : SCXML_QUICKJS_EXCEPTION;
    quickjs_diagnostic(
        diagnostic, diagnostic_capacity,
        runtime->interrupted ? "QuickJS evaluation deadline exceeded"
                             : (message != NULL ? message
                                                : "QuickJS exception"));
    if (message != NULL) JS_FreeCString(context, message);
    JS_FreeValue(context, exception);
    return status;
}

static void quickjs_context_destroy(scxml_quickjs_runtime *runtime) {
    if (runtime != NULL && runtime->context != NULL) {
        JS_FreeContext((JSContext *)runtime->context);
        runtime->context = NULL;
    }
}

static scxml_quickjs_status quickjs_context_create(
    scxml_quickjs_runtime *runtime,
    char *diagnostic, size_t diagnostic_capacity) {
    JSContext *context;
    scxml_quickjs_status status;
    if (runtime == NULL || runtime->runtime == NULL ||
        runtime->context != NULL)
        return SCXML_QUICKJS_INVALID_ARGUMENT;
    context = JS_NewContextRaw((JSRuntime *)runtime->runtime);
    if (context == NULL) return SCXML_QUICKJS_ALLOCATION_FAILED;
    JS_AddIntrinsicBaseObjects(context);
    JS_AddIntrinsicEval(context);
    JS_AddIntrinsicJSON(context);
    runtime->context = context;
    status = scxml_quickjs_runtime_eval(
        runtime, quickjs_hardening_source,
        sizeof(quickjs_hardening_source) - 1u, "<sandbox-init>",
        runtime->options.max_eval_milliseconds,
        diagnostic, diagnostic_capacity);
    if (status != SCXML_QUICKJS_OK) quickjs_context_destroy(runtime);
    return status;
}

static scxml_quickjs_status quickjs_context_recreate(
    scxml_quickjs_runtime *runtime,
    char *diagnostic, size_t diagnostic_capacity) {
    quickjs_context_destroy(runtime);
    return quickjs_context_create(runtime, diagnostic, diagnostic_capacity);
}
#endif

scxml_quickjs_status scxml_quickjs_runtime_init(
    scxml_quickjs_runtime *runtime,
    const scxml_quickjs_compile_options_v1 *options,
    char *diagnostic, size_t diagnostic_capacity) {
    if (runtime == NULL || runtime->runtime != NULL ||
        runtime->context != NULL || !scxml_quickjs_limits_valid(options)) {
        quickjs_diagnostic(
            diagnostic, diagnostic_capacity,
            "invalid QuickJS runtime options");
        return SCXML_QUICKJS_INVALID_ARGUMENT;
    }
#if !TURBOSCXML_HAS_QUICKJS
    quickjs_diagnostic(
        diagnostic, diagnostic_capacity,
        "TurboSCXML was built without quickjs-sandbox support");
    return SCXML_QUICKJS_INVALID_ARGUMENT;
#else
    {
        JSRuntime *js_runtime = JS_NewRuntime();
        scxml_quickjs_status status;
        if (js_runtime == NULL) return SCXML_QUICKJS_ALLOCATION_FAILED;
        JS_SetMemoryLimit(js_runtime, options->max_heap_bytes);
        JS_SetMaxStackSize(js_runtime, options->max_stack_bytes);
        JS_SetCanBlock(js_runtime, false);
        JS_SetInterruptHandler(js_runtime, quickjs_interrupt, runtime);
        if (options->max_string_bytes == SIZE_MAX) {
            JS_FreeRuntime(js_runtime);
            return SCXML_QUICKJS_LIMIT_EXCEEDED;
        }
        runtime->runtime = js_runtime;
        runtime->options = *options;
        runtime->max_diagnostic_bytes = diagnostic_capacity;
        runtime->result_string_capacity = options->max_string_bytes + 1u;
        runtime->result_string =
            (char *)malloc(runtime->result_string_capacity);
        if (runtime->result_string == NULL) {
            scxml_quickjs_runtime_destroy(runtime);
            return SCXML_QUICKJS_ALLOCATION_FAILED;
        }
        status = quickjs_context_create(
            runtime, diagnostic, diagnostic_capacity);
        if (status != SCXML_QUICKJS_OK) {
            scxml_quickjs_runtime_destroy(runtime);
            return status;
        }
    }
    return SCXML_QUICKJS_OK;
#endif
}

scxml_quickjs_status scxml_quickjs_runtime_eval(
    scxml_quickjs_runtime *runtime,
    const char *source, size_t source_size,
    const char *filename,
    uint64_t max_eval_milliseconds,
    char *diagnostic, size_t diagnostic_capacity) {
    if (runtime == NULL || runtime->runtime == NULL ||
        runtime->context == NULL || source == NULL || source_size == 0u ||
        source_size > runtime->options.max_source_bytes ||
        source_size == SIZE_MAX || filename == NULL ||
        max_eval_milliseconds == 0u) {
        quickjs_diagnostic(
            diagnostic, diagnostic_capacity,
            "invalid QuickJS evaluation request");
        return SCXML_QUICKJS_INVALID_ARGUMENT;
    }
#if !TURBOSCXML_HAS_QUICKJS
    return SCXML_QUICKJS_INVALID_ARGUMENT;
#else
    {
        JSContext *context = (JSContext *)runtime->context;
        JSValue result;
        char *terminated_source = (char *)malloc(source_size + 1u);
        scxml_quickjs_status status;
        if (terminated_source == NULL) {
            quickjs_diagnostic(
                diagnostic, diagnostic_capacity,
                "QuickJS evaluation source allocation failed");
            return SCXML_QUICKJS_ALLOCATION_FAILED;
        }
        memcpy(terminated_source, source, source_size);
        terminated_source[source_size] = '\0';
        const bool owns_deadline = quickjs_deadline_begin(
            runtime, max_eval_milliseconds);
        result = JS_Eval(
            context, terminated_source, source_size,
            filename, JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) {
            status = quickjs_exception(
                runtime, diagnostic, diagnostic_capacity);
            quickjs_deadline_end(runtime, owns_deadline);
            free(terminated_source);
            return status;
        }
        JS_FreeValue(context, result);
        if (quickjs_deadline_expired(runtime)) {
            quickjs_deadline_end(runtime, owns_deadline);
            free(terminated_source);
            quickjs_diagnostic(
                diagnostic, diagnostic_capacity,
                "QuickJS evaluation deadline exceeded");
            return SCXML_QUICKJS_LIMIT_EXCEEDED;
        }
        quickjs_deadline_end(runtime, owns_deadline);
        free(terminated_source);
        quickjs_diagnostic(diagnostic, diagnostic_capacity, "");
        return SCXML_QUICKJS_OK;
    }
#endif
}

void scxml_quickjs_runtime_destroy(scxml_quickjs_runtime *runtime) {
    if (runtime == NULL) return;
#if TURBOSCXML_HAS_QUICKJS
    quickjs_context_destroy(runtime);
    if (runtime->runtime != NULL)
        JS_FreeRuntime((JSRuntime *)runtime->runtime);
#endif
    free(runtime->result_string);
    memset(runtime, 0, sizeof(*runtime));
}

scxml_quickjs_status scxml_quickjs_validate_source(
    const scxml_quickjs_compile_options_v1 *options,
    const char *source, size_t source_size,
    char *diagnostic, size_t diagnostic_capacity) {
    if (!scxml_quickjs_limits_valid(options) || source == NULL ||
        source_size > options->max_source_bytes) {
        quickjs_diagnostic(
            diagnostic, diagnostic_capacity,
            "invalid or oversized QuickJS source");
        return source_size > 0u && options != NULL &&
                       source_size > options->max_source_bytes
                   ? SCXML_QUICKJS_LIMIT_EXCEEDED
                   : SCXML_QUICKJS_INVALID_ARGUMENT;
    }
#if !TURBOSCXML_HAS_QUICKJS
    return SCXML_QUICKJS_INVALID_ARGUMENT;
#else
    {
        scxml_quickjs_runtime runtime = {0};
        JSContext *context;
        JSValue compiled;
        char *terminated_source;
        scxml_quickjs_status status = scxml_quickjs_runtime_init(
            &runtime, options, diagnostic, diagnostic_capacity);
        if (status != SCXML_QUICKJS_OK) return status;
        if (source_size == SIZE_MAX) {
            scxml_quickjs_runtime_destroy(&runtime);
            return SCXML_QUICKJS_LIMIT_EXCEEDED;
        }
        terminated_source = (char *)malloc(source_size + 1u);
        if (terminated_source == NULL) {
            scxml_quickjs_runtime_destroy(&runtime);
            return SCXML_QUICKJS_ALLOCATION_FAILED;
        }
        memcpy(terminated_source, source, source_size);
        terminated_source[source_size] = '\0';
        context = (JSContext *)runtime.context;
        compiled = JS_Eval(
            context, terminated_source, source_size, "<scxml-script>",
            JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(compiled)) {
            status = quickjs_exception(
                &runtime, diagnostic, diagnostic_capacity);
        } else {
            JS_FreeValue(context, compiled);
            status = SCXML_QUICKJS_OK;
        }
        free(terminated_source);
        scxml_quickjs_runtime_destroy(&runtime);
        return status;
    }
#endif
}

scxml_quickjs_status scxml_quickjs_validate_expression(
    const scxml_quickjs_compile_options_v1 *options,
    const char *source, size_t source_size,
    char *diagnostic, size_t diagnostic_capacity) {
    static const char prefix[] = "(\n";
    static const char suffix[] = "\n)";
    if (!scxml_quickjs_limits_valid(options) || source == NULL ||
        source_size == 0u || source_size > options->max_source_bytes ||
        source_size > SIZE_MAX - (sizeof(prefix) - 1u) -
                          (sizeof(suffix) - 1u) - 1u) {
        quickjs_diagnostic(diagnostic, diagnostic_capacity,
                           "invalid or oversized QuickJS expression");
        return source_size > 0u && options != NULL &&
                       source_size > options->max_source_bytes
                   ? SCXML_QUICKJS_LIMIT_EXCEEDED
                   : SCXML_QUICKJS_INVALID_ARGUMENT;
    }
#if !TURBOSCXML_HAS_QUICKJS
    return SCXML_QUICKJS_INVALID_ARGUMENT;
#else
    scxml_quickjs_runtime runtime = {0};
    char *wrapped = NULL;
    size_t wrapped_size;
    scxml_quickjs_status status;
    wrapped_size = sizeof(prefix) - 1u + source_size + sizeof(suffix) - 1u;
    wrapped = (char *)malloc(wrapped_size + 1u);
    if (wrapped == NULL) return SCXML_QUICKJS_ALLOCATION_FAILED;
    memcpy(wrapped, prefix, sizeof(prefix) - 1u);
    memcpy(wrapped + sizeof(prefix) - 1u, source, source_size);
    memcpy(wrapped + sizeof(prefix) - 1u + source_size,
           suffix, sizeof(suffix) - 1u);
    wrapped[wrapped_size] = '\0';
    status = scxml_quickjs_runtime_init(
        &runtime, options, diagnostic, diagnostic_capacity);
    if (status == SCXML_QUICKJS_OK) {
        JSValue compiled = JS_Eval(
            (JSContext *)runtime.context, wrapped, wrapped_size,
            "<scxml-expression>",
            JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(compiled))
            status = quickjs_exception(
                &runtime, diagnostic, diagnostic_capacity);
        else
            JS_FreeValue((JSContext *)runtime.context, compiled);
    }
    scxml_quickjs_runtime_destroy(&runtime);
    free(wrapped);
    return status;
#endif
}

static bool quickjs_identifier_start(unsigned char value) {
    return (value >= (unsigned char)'A' && value <= (unsigned char)'Z') ||
           (value >= (unsigned char)'a' && value <= (unsigned char)'z') ||
           value == (unsigned char)'_';
}

static bool quickjs_identifier_continue(unsigned char value) {
    return quickjs_identifier_start(value) ||
           (value >= (unsigned char)'0' && value <= (unsigned char)'9');
}

static size_t quickjs_skip_space(
    const char *source, size_t source_size, size_t offset) {
    while (offset < source_size &&
           (source[offset] == ' ' || source[offset] == '\t' ||
            source[offset] == '\r' || source[offset] == '\n'))
        ++offset;
    return offset;
}

static const cmeta_data_desc *quickjs_root_field(
    const cmeta_data_desc *root, const char *name, size_t name_size) {
    const cmeta_data_struct_shape *shape;
    size_t index;
    if (root == NULL || root->kind != CMETA_DATA_STRUCT ||
        root->shape == NULL)
        return NULL;
    shape = (const cmeta_data_struct_shape *)root->shape;
    for (index = 0u; index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &shape->fields[index];
        if (field->name != NULL && strlen(field->name) == name_size &&
            memcmp(field->name, name, name_size) == 0)
            return field->value;
    }
    return NULL;
}

static const cmeta_data_desc *quickjs_infer_variable_data(
    const cmeta_data_desc *root, const char *source, size_t source_size,
    size_t offset) {
    const size_t start = quickjs_skip_space(source, source_size, offset);
    size_t end = start;
    bool floating = false;
    if (start >= source_size || source[start] != '=')
        return &cmeta_data_int;
    offset = quickjs_skip_space(source, source_size, start + 1u);
    if (offset >= source_size) return &cmeta_data_int;
    if (source_size - offset >= 4u &&
        memcmp(source + offset, "true", 4u) == 0 &&
        (offset + 4u == source_size ||
         !quickjs_identifier_continue((unsigned char)source[offset + 4u])))
        return &cmeta_data_bool;
    if (source_size - offset >= 5u &&
        memcmp(source + offset, "false", 5u) == 0 &&
        (offset + 5u == source_size ||
         !quickjs_identifier_continue((unsigned char)source[offset + 5u])))
        return &cmeta_data_bool;
    if (source[offset] == '+' || source[offset] == '-') ++offset;
    if (offset < source_size && source[offset] >= '0' &&
        source[offset] <= '9') {
        for (end = offset; end < source_size; ++end) {
            const char value = source[end];
            if (value == '.' || value == 'e' || value == 'E')
                floating = true;
            if (!(value >= '0' && value <= '9') && value != '.' &&
                value != 'e' && value != 'E' && value != '+' && value != '-')
                break;
        }
        return floating ? &cmeta_data_double : &cmeta_data_int;
    }
    if (quickjs_identifier_start((unsigned char)source[offset])) {
        const cmeta_data_desc *field;
        end = offset + 1u;
        while (end < source_size &&
               quickjs_identifier_continue((unsigned char)source[end]))
            ++end;
        field = quickjs_root_field(root, source + offset, end - offset);
        if (field != NULL) return field;
    }
    return &cmeta_data_double;
}

scxml_quickjs_status scxml_quickjs_collect_script_variables(
    scxml_scope_schema *scope, const cmeta_data_desc *root,
    const char *source, size_t source_size,
    size_t max_variables, size_t *variable_count,
    char *diagnostic, size_t diagnostic_capacity) {
    size_t offset = 0u;
    size_t brace_depth = 0u;
    char quote = '\0';
    bool escaped = false;
    bool line_comment = false;
    bool block_comment = false;
    if (scope == NULL || !cmeta_data_desc_valid(root) || source == NULL ||
        source_size == 0u || max_variables == 0u || variable_count == NULL) {
        quickjs_diagnostic(
            diagnostic, diagnostic_capacity,
            "invalid QuickJS script-variable scan context");
        return SCXML_QUICKJS_INVALID_ARGUMENT;
    }
    while (offset < source_size) {
        const char value = source[offset];
        if (line_comment) {
            if (value == '\r' || value == '\n') line_comment = false;
            ++offset;
            continue;
        }
        if (block_comment) {
            if (value == '*' && offset + 1u < source_size &&
                source[offset + 1u] == '/') {
                block_comment = false;
                offset += 2u;
            } else {
                ++offset;
            }
            continue;
        }
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (value == '\\') {
                escaped = true;
            } else if (value == quote) {
                quote = '\0';
            }
            ++offset;
            continue;
        }
        if (value == '/' && offset + 1u < source_size) {
            if (source[offset + 1u] == '/') {
                line_comment = true;
                offset += 2u;
                continue;
            }
            if (source[offset + 1u] == '*') {
                block_comment = true;
                offset += 2u;
                continue;
            }
        }
        if (value == '\'' || value == '"' || value == '`') {
            quote = value;
            ++offset;
            continue;
        }
        if (value == '{') {
            ++brace_depth;
            ++offset;
            continue;
        }
        if (value == '}') {
            if (brace_depth != 0u) --brace_depth;
            ++offset;
            continue;
        }
        if (brace_depth == 0u && source_size - offset >= 3u &&
            memcmp(source + offset, "var", 3u) == 0 &&
            (offset == 0u ||
             !quickjs_identifier_continue((unsigned char)source[offset - 1u])) &&
            (offset + 3u == source_size ||
             !quickjs_identifier_continue((unsigned char)source[offset + 3u]))) {
            const cmeta_data_desc *value_data;
            const cmeta_data_desc *root_data;
            size_t name_start = quickjs_skip_space(
                source, source_size, offset + 3u);
            size_t name_end;
            size_t slot = SIZE_MAX;
            bool conflict = false;
            if (name_start >= source_size ||
                !quickjs_identifier_start(
                    (unsigned char)source[name_start])) {
                quickjs_diagnostic(
                    diagnostic, diagnostic_capacity,
                    "global var must declare an SCXML-compatible identifier");
                return SCXML_QUICKJS_INVALID_ARGUMENT;
            }
            name_end = name_start + 1u;
            while (name_end < source_size &&
                   quickjs_identifier_continue(
                       (unsigned char)source[name_end]))
                ++name_end;
            if (source[name_start] == '_') {
                quickjs_diagnostic(
                    diagnostic, diagnostic_capacity,
                    "global var cannot shadow an SCXML system variable");
                return SCXML_QUICKJS_INVALID_ARGUMENT;
            }
            root_data = quickjs_root_field(
                root, source + name_start, name_end - name_start);
            if (root_data == NULL) {
                const scxml_scope_slot *existing;
                value_data = quickjs_infer_variable_data(
                    root, source, source_size, name_end);
                existing = scxml_scope_find(
                    scope, source + name_start,
                    name_end - name_start, NULL);
                if (existing == NULL &&
                    *variable_count >= max_variables) {
                    quickjs_diagnostic(
                        diagnostic, diagnostic_capacity,
                        "QuickJS script-variable limit exceeded");
                    return SCXML_QUICKJS_LIMIT_EXCEEDED;
                }
                {
                    const size_t before = scope->slot_count;
                    if (!scxml_scope_register(
                            scope, source + name_start,
                            name_end - name_start, value_data,
                            &slot, &conflict)) {
                        quickjs_diagnostic(
                            diagnostic, diagnostic_capacity,
                            conflict
                                ? "QuickJS script variable has conflicting types"
                                : "QuickJS script variable could not be retained");
                        return conflict ? SCXML_QUICKJS_INVALID_ARGUMENT
                                        : SCXML_QUICKJS_ALLOCATION_FAILED;
                    }
                    if (scope->slot_count != before) ++*variable_count;
                }
            }
            offset = name_end;
            continue;
        }
        ++offset;
    }
    quickjs_diagnostic(diagnostic, diagnostic_capacity, "");
    return SCXML_QUICKJS_OK;
}

#if TURBOSCXML_HAS_QUICKJS
typedef struct quickjs_conversion {
    JSContext *context;
    const scxml_quickjs_compile_options_v1 *limits;
    const cmeta_data_desc *root;
    size_t properties;
} quickjs_conversion;

typedef struct quickjs_aligned_storage {
    void *allocation;
    void *value;
} quickjs_aligned_storage;

static bool quickjs_aligned_storage_init(
    quickjs_aligned_storage *storage, const cmeta_type_desc *type) {
    uintptr_t address;
    uintptr_t aligned;
    size_t allocation_size;
    if (storage == NULL || storage->allocation != NULL ||
        storage->value != NULL || !cmeta_type_desc_valid(type) ||
        type->size == 0u || type->align == 0u ||
        (type->align & (type->align - 1u)) != 0u ||
        type->size > SIZE_MAX - (type->align - 1u))
        return false;
    allocation_size = type->size + type->align - 1u;
    storage->allocation = calloc(1u, allocation_size);
    if (storage->allocation == NULL) return false;
    address = (uintptr_t)storage->allocation;
    if (address > UINTPTR_MAX - (type->align - 1u)) {
        free(storage->allocation);
        memset(storage, 0, sizeof(*storage));
        return false;
    }
    aligned = (address + type->align - 1u) &
              ~((uintptr_t)type->align - 1u);
    storage->value = (void *)aligned;
    return true;
}

static void quickjs_aligned_storage_destroy(
    quickjs_aligned_storage *storage,
    const cmeta_type_desc *type, bool live) {
    if (storage == NULL) return;
    if (live && storage->value != NULL && type != NULL &&
        type->traits != NULL && type->traits->destroy != NULL)
        type->traits->destroy(storage->value);
    free(storage->allocation);
    memset(storage, 0, sizeof(*storage));
}

static bool quickjs_property_budget(quickjs_conversion *conversion) {
    return conversion->properties < conversion->limits->max_properties &&
           ++conversion->properties <= conversion->limits->max_properties;
}

static bool quickjs_read_signed(
    const cmeta_data_desc *descriptor, const void *object, int64_t *out) {
    const size_t size = descriptor->storage_type->size;
    if (size == sizeof(int8_t)) {
        int8_t value;
        memcpy(&value, object, size);
        *out = value;
    } else if (size == sizeof(int16_t)) {
        int16_t value;
        memcpy(&value, object, size);
        *out = value;
    } else if (size == sizeof(int32_t)) {
        int32_t value;
        memcpy(&value, object, size);
        *out = value;
    } else if (size == sizeof(int64_t)) {
        memcpy(out, object, size);
    } else {
        return false;
    }
    return true;
}

static bool quickjs_read_unsigned(
    const cmeta_data_desc *descriptor, const void *object, uint64_t *out) {
    const size_t size = descriptor->storage_type->size;
    if (size == sizeof(uint8_t)) {
        uint8_t value;
        memcpy(&value, object, size);
        *out = value;
    } else if (size == sizeof(uint16_t)) {
        uint16_t value;
        memcpy(&value, object, size);
        *out = value;
    } else if (size == sizeof(uint32_t)) {
        uint32_t value;
        memcpy(&value, object, size);
        *out = value;
    } else if (size == sizeof(uint64_t)) {
        memcpy(out, object, size);
    } else {
        return false;
    }
    return true;
}

static bool quickjs_write_signed(
    const cmeta_data_desc *descriptor, void *object, int64_t value) {
    const size_t size = descriptor->storage_type->size;
    const cmeta_data_integer_shape *shape =
        (const cmeta_data_integer_shape *)descriptor->shape;
    const uint8_t bits = shape != NULL ? shape->bits : (uint8_t)(size * 8u);
    if (bits == 0u || bits > 64u ||
        (bits < 64u &&
         (value < -(INT64_C(1) << (bits - 1u)) ||
          value > (INT64_C(1) << (bits - 1u)) - 1)))
        return false;
    if (size == sizeof(int8_t)) {
        const int8_t native = (int8_t)value;
        memcpy(object, &native, size);
    } else if (size == sizeof(int16_t)) {
        const int16_t native = (int16_t)value;
        memcpy(object, &native, size);
    } else if (size == sizeof(int32_t)) {
        const int32_t native = (int32_t)value;
        memcpy(object, &native, size);
    } else if (size == sizeof(int64_t)) {
        memcpy(object, &value, size);
    } else {
        return false;
    }
    return true;
}

static bool quickjs_write_unsigned(
    const cmeta_data_desc *descriptor, void *object, uint64_t value) {
    const size_t size = descriptor->storage_type->size;
    const cmeta_data_integer_shape *shape =
        (const cmeta_data_integer_shape *)descriptor->shape;
    const uint8_t bits = shape != NULL ? shape->bits : (uint8_t)(size * 8u);
    if (bits == 0u || bits > 64u ||
        (bits < 64u && value > (UINT64_C(1) << bits) - 1u))
        return false;
    if (size == sizeof(uint8_t)) {
        const uint8_t native = (uint8_t)value;
        memcpy(object, &native, size);
    } else if (size == sizeof(uint16_t)) {
        const uint16_t native = (uint16_t)value;
        memcpy(object, &native, size);
    } else if (size == sizeof(uint32_t)) {
        const uint32_t native = (uint32_t)value;
        memcpy(object, &native, size);
    } else if (size == sizeof(uint64_t)) {
        memcpy(object, &value, size);
    } else {
        return false;
    }
    return true;
}

static JSValue quickjs_import_value(
    quickjs_conversion *conversion, const cmeta_data_desc *descriptor,
    const void *object, size_t depth) {
    JSContext *context = conversion->context;
    if (descriptor == NULL || object == NULL ||
        depth > conversion->limits->max_conversion_depth)
        return JS_EXCEPTION;
    switch (descriptor->kind) {
        case CMETA_DATA_BOOL: {
            bool value;
            memcpy(&value, object, sizeof(value));
            return JS_NewBool(context, value);
        }
        case CMETA_DATA_SINT: {
            int64_t value;
            if (!quickjs_read_signed(descriptor, object, &value) ||
                value < quickjs_min_safe_integer ||
                value > quickjs_max_safe_integer)
                return JS_EXCEPTION;
            return JS_NewFloat64(context, (double)value);
        }
        case CMETA_DATA_UINT: {
            uint64_t value;
            if (!quickjs_read_unsigned(descriptor, object, &value) ||
                value > UINT64_C(9007199254740991))
                return JS_EXCEPTION;
            return JS_NewFloat64(context, (double)value);
        }
        case CMETA_DATA_ENUM: {
            int64_t value;
            if (cmeta_data_enum_read(descriptor, object, &value) != CMETA_OK ||
                value < quickjs_min_safe_integer ||
                value > quickjs_max_safe_integer)
                return JS_EXCEPTION;
            return JS_NewFloat64(context, (double)value);
        }
        case CMETA_DATA_FLOAT: {
            double value;
            if (descriptor->storage_type->size == sizeof(float)) {
                float native;
                memcpy(&native, object, sizeof(native));
                value = native;
            } else if (descriptor->storage_type->size == sizeof(double)) {
                memcpy(&value, object, sizeof(value));
            } else {
                return JS_EXCEPTION;
            }
            return JS_NewFloat64(context, value);
        }
        case CMETA_DATA_STRING: {
            const unsigned char *data = NULL;
            size_t size = 0u;
            if (cmeta_data_buffer_read(
                    descriptor, object,
                    conversion->limits->max_string_bytes,
                    &data, &size) != CMETA_OK)
                return JS_EXCEPTION;
            return JS_NewStringLen(context, (const char *)data, size);
        }
        case CMETA_DATA_STRUCT: {
            const cmeta_data_struct_shape *shape =
                (const cmeta_data_struct_shape *)descriptor->shape;
            JSValue result;
            size_t index;
            if (shape == NULL || shape->fields == NULL ||
                shape->field_count > conversion->limits->max_properties)
                return JS_EXCEPTION;
            result = JS_NewObject(context);
            if (JS_IsException(result)) return result;
            for (index = 0u; index < shape->field_count; ++index) {
                const cmeta_data_field_desc *field = &shape->fields[index];
                JSValue value;
                if (field->name == NULL || field->value == NULL ||
                    !quickjs_property_budget(conversion)) {
                    JS_FreeValue(context, result);
                    return JS_EXCEPTION;
                }
                value = quickjs_import_value(
                    conversion, field->value,
                    (const unsigned char *)object + field->offset,
                    depth + 1u);
                if (JS_IsException(value) ||
                    JS_SetPropertyStr(context, result, field->name, value) < 0) {
                    if (JS_IsException(value)) JS_FreeValue(context, value);
                    JS_FreeValue(context, result);
                    return JS_EXCEPTION;
                }
            }
            return result;
        }
        case CMETA_DATA_SEQUENCE: {
            const cmeta_data_desc *semantic =
                cmeta_container_data(object);
            const cmeta_type_desc *element_type;
            const cmeta_data_desc *element_data;
            cmeta_range range = {0};
            cmeta_range_cursor cursor = {0};
            quickjs_aligned_storage element = {0};
            JSValue result;
            size_t length;
            size_t index;
            bool element_live = false;
            const bool managed =
                cmeta_container_range_constructs_values(
                    cmeta_container_type_argument(object, 0u));
            if (semantic == NULL || semantic->kind != CMETA_DATA_SEQUENCE ||
                !cmeta_container_type_application_valid(object) ||
                cmeta_container_type_arity(object) != 1u ||
                !cmeta_container_range_view(
                    object, CMETA_CONTAINER_VIEW_DEFAULT, &range) ||
                range.size == NULL ||
                (range.flags & (CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED)) !=
                    (CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED))
                return JS_EXCEPTION;
            element_type = cmeta_container_type_argument(object, 0u);
            element_data = scxml_scope_find_data_for_type(
                conversion->root, element_type,
                conversion->limits->max_conversion_depth);
            length = cmeta_range_size(&range);
            if (element_data == NULL || length > UINT32_MAX ||
                length > conversion->limits->max_array_items ||
                (managed &&
                 (range.flags & CMETA_RANGE_CONSTRUCTS_VALUES) == 0u) ||
                !quickjs_aligned_storage_init(&element, element_type))
                return JS_EXCEPTION;
            result = JS_NewArray(conversion->context);
            if (JS_IsException(result)) {
                quickjs_aligned_storage_destroy(
                    &element, element_type, false);
                return result;
            }
            for (index = 0u; index < length; ++index) {
                const cmeta_gen_status generated = cmeta_range_next(
                    &range, &cursor, element.value);
                JSValue value;
                if ((generated != CMETA_GEN_VALUE &&
                     generated != CMETA_GEN_VALUE_AND_DONE) ||
                    !quickjs_property_budget(conversion)) {
                    JS_FreeValue(conversion->context, result);
                    quickjs_aligned_storage_destroy(
                        &element, element_type, element_live);
                    return JS_EXCEPTION;
                }
                element_live = managed;
                value = quickjs_import_value(
                    conversion, element_data, element.value, depth + 1u);
                if (element_live) {
                    element_type->traits->destroy(element.value);
                    memset(element.value, 0, element_type->size);
                    element_live = false;
                }
                if (JS_IsException(value) ||
                    JS_SetPropertyUint32(
                        conversion->context, result,
                        (uint32_t)index, value) < 0) {
                    if (JS_IsException(value))
                        JS_FreeValue(conversion->context, value);
                    JS_FreeValue(conversion->context, result);
                    quickjs_aligned_storage_destroy(
                        &element, element_type, false);
                    return JS_EXCEPTION;
                }
                if (generated == CMETA_GEN_VALUE_AND_DONE &&
                    index + 1u != length) {
                    JS_FreeValue(conversion->context, result);
                    quickjs_aligned_storage_destroy(
                        &element, element_type, false);
                    return JS_EXCEPTION;
                }
            }
            quickjs_aligned_storage_destroy(
                &element, element_type, false);
            return result;
        }
        default: return JS_EXCEPTION;
    }
}

static bool quickjs_export_value(
    quickjs_conversion *conversion, const cmeta_data_desc *descriptor,
    const cmeta_type_desc *storage_type,
    const cmeta_declared_type *declared_type,
    JSValueConst value, void *object, size_t depth) {
    JSContext *context = conversion->context;
    if (descriptor == NULL || object == NULL ||
        !cmeta_type_desc_valid(storage_type) ||
        (descriptor->storage_type != NULL &&
         !cmeta_type_equal(descriptor->storage_type, storage_type)) ||
        depth > conversion->limits->max_conversion_depth)
        return false;
    switch (descriptor->kind) {
        case CMETA_DATA_BOOL: {
            const int native = JS_ToBool(context, value);
            const bool result = native != 0;
            if (native < 0 || !JS_IsBool(value)) return false;
            memcpy(object, &result, sizeof(result));
            return true;
        }
        case CMETA_DATA_SINT: {
            double number;
            int64_t native;
            if (!JS_IsNumber(value) ||
                JS_ToFloat64(context, &number, value) != 0 ||
                !isfinite(number) ||
                number < (double)quickjs_min_safe_integer ||
                number > (double)quickjs_max_safe_integer ||
                number != (double)(native = (int64_t)number))
                return false;
            return quickjs_write_signed(descriptor, object, native);
        }
        case CMETA_DATA_UINT: {
            double number;
            uint64_t native;
            if (!JS_IsNumber(value) ||
                JS_ToFloat64(context, &number, value) != 0 ||
                !isfinite(number) || number < 0.0 ||
                number > (double)quickjs_max_safe_integer ||
                number != (double)(native = (uint64_t)number))
                return false;
            return quickjs_write_unsigned(descriptor, object, native);
        }
        case CMETA_DATA_ENUM: {
            double number;
            int64_t native;
            if (!JS_IsNumber(value) ||
                JS_ToFloat64(context, &number, value) != 0 ||
                !isfinite(number) ||
                number < (double)quickjs_min_safe_integer ||
                number > (double)quickjs_max_safe_integer ||
                number != (double)(native = (int64_t)number) ||
                cmeta_data_enum_restore_zero(descriptor, object) != CMETA_OK)
                return false;
            return cmeta_data_enum_assign(descriptor, object, native) ==
                CMETA_OK;
        }
        case CMETA_DATA_FLOAT: {
            double number;
            if (!JS_IsNumber(value) ||
                JS_ToFloat64(context, &number, value) != 0)
                return false;
            if (descriptor->storage_type->size == sizeof(float)) {
                const float native = (float)number;
                memcpy(object, &native, sizeof(native));
                return true;
            }
            if (descriptor->storage_type->size == sizeof(double)) {
                memcpy(object, &number, sizeof(number));
                return true;
            }
            return false;
        }
        case CMETA_DATA_STRING: {
            const char *text;
            size_t size;
            cmeta_status status;
            if (!JS_IsString(value)) return false;
            text = JS_ToCStringLen(context, &size, value);
            if (text == NULL) return false;
            status = cmeta_data_buffer_assign(
                descriptor, object, (const unsigned char *)text, size,
                conversion->limits->max_string_bytes);
            JS_FreeCString(context, text);
            return status == CMETA_OK;
        }
        case CMETA_DATA_STRUCT: {
            const cmeta_data_struct_shape *shape =
                (const cmeta_data_struct_shape *)descriptor->shape;
            size_t index;
            if (!JS_IsObject(value) || shape == NULL || shape->fields == NULL ||
                shape->field_count > conversion->limits->max_properties)
                return false;
            for (index = 0u; index < shape->field_count; ++index) {
                const cmeta_data_field_desc *field = &shape->fields[index];
                const cmeta_field_desc *layout_field;
                JSValue property;
                bool ok;
                if (field->name == NULL || field->value == NULL ||
                    !quickjs_property_budget(conversion))
                    return false;
                layout_field = cmeta_struct_find_field(
                    shape->layout, field->name);
                if (layout_field == NULL ||
                    layout_field->offset != field->offset ||
                    !cmeta_type_desc_valid(layout_field->type))
                    return false;
                property = JS_GetPropertyStr(context, value, field->name);
                if (JS_IsException(property)) return false;
                ok = quickjs_export_value(
                    conversion, field->value, layout_field->type,
                    layout_field->declared_type, property,
                    (unsigned char *)object + field->offset, depth + 1u);
                JS_FreeValue(context, property);
                if (!ok) return false;
            }
            return true;
        }
        case CMETA_DATA_SEQUENCE: {
            const cmeta_container_desc *container =
                cmeta_container_descriptor(object);
            const cmeta_type_desc *element_type;
            const cmeta_data_desc *element_data;
            quickjs_aligned_storage element = {0};
            cmeta_collector collector;
            JSValue length_value;
            uint32_t length = 0u;
            uint32_t index;
            bool element_live = false;
            bool ok = false;
            if (!JS_IsArray(value) || container == NULL ||
                container->collector == NULL ||
                cmeta_container_data(object) == NULL ||
                cmeta_container_data(object)->kind != CMETA_DATA_SEQUENCE ||
                !cmeta_container_type_application_valid(object) ||
                cmeta_container_type_arity(object) != 1u ||
                !cmeta_declared_type_valid(declared_type) ||
                declared_type->arity != 1u ||
                !cmeta_type_equal(
                    declared_type->storage_type, storage_type))
                return false;
            element_type = cmeta_container_type_argument(object, 0u);
            element_data = scxml_scope_find_data_for_type(
                conversion->root, element_type,
                conversion->limits->max_conversion_depth);
            if (element_data == NULL ||
                !quickjs_aligned_storage_init(&element, element_type))
                goto sequence_cleanup;
            length_value = JS_GetPropertyStr(
                conversion->context, value, "length");
            if (JS_IsException(length_value) ||
                JS_ToUint32(
                    conversion->context, &length, length_value) != 0) {
                if (!JS_IsException(length_value))
                    JS_FreeValue(conversion->context, length_value);
                goto sequence_cleanup;
            }
            JS_FreeValue(conversion->context, length_value);
            if (length > conversion->limits->max_array_items)
                goto sequence_cleanup;
            if (cmeta_container_restore_zero(
                    object, declared_type) != CMETA_OK ||
                cmeta_container_bind_types(
                    object, declared_type) != CMETA_OK)
                goto sequence_cleanup;
            collector = container->collector(
                object, conversion->limits->max_array_items);
            if (cmeta_collector_begin(&collector) != CMETA_OK)
                goto sequence_cleanup;
            for (index = 0u; index < length; ++index) {
                JSValue item;
                if (!quickjs_property_budget(conversion)) {
                    cmeta_collector_abort(&collector);
                    goto sequence_cleanup;
                }
                item = JS_GetPropertyUint32(
                    conversion->context, value, index);
                if (JS_IsException(item) ||
                    !quickjs_export_value(
                        conversion, element_data, element_type, NULL, item,
                        element.value, depth + 1u)) {
                    if (!JS_IsException(item))
                        JS_FreeValue(conversion->context, item);
                    cmeta_collector_abort(&collector);
                    goto sequence_cleanup;
                }
                JS_FreeValue(conversion->context, item);
                element_live = cmeta_type_require_traits(
                    element_type, CMETA_TRAIT_TRIVIAL_DESTROY) != CMETA_OK;
                if (cmeta_collector_accept(
                        &collector, element_type,
                        element.value) != CMETA_OK) {
                    cmeta_collector_abort(&collector);
                    goto sequence_cleanup;
                }
                if (element_live) {
                    element_type->traits->destroy(element.value);
                    element_live = false;
                }
                memset(element.value, 0, element_type->size);
            }
            if (cmeta_collector_finish(&collector) != CMETA_OK)
                goto sequence_cleanup;
            ok = true;
sequence_cleanup:
            quickjs_aligned_storage_destroy(
                &element, element_type, element_live);
            return ok;
        }
        default: return false;
    }
}

static bool quickjs_import_root(
    quickjs_conversion *conversion, const cmeta_data_desc *root,
    const void *state) {
    const cmeta_data_struct_shape *shape =
        (const cmeta_data_struct_shape *)root->shape;
    JSValue global = JS_GetGlobalObject(conversion->context);
    size_t index;
    bool ok = !JS_IsException(global) && shape != NULL;
    for (index = 0u; ok && index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &shape->fields[index];
        JSValue value;
        if (field->name == NULL || !quickjs_property_budget(conversion)) {
            ok = false;
            break;
        }
        value = quickjs_import_value(
            conversion, field->value,
            (const unsigned char *)state + field->offset, 1u);
        if (JS_IsException(value) ||
            JS_SetPropertyStr(
                conversion->context, global, field->name, value) < 0) {
            if (JS_IsException(value)) JS_FreeValue(conversion->context, value);
            ok = false;
        }
    }
    JS_FreeValue(conversion->context, global);
    return ok;
}

static bool quickjs_export_root(
    quickjs_conversion *conversion, const cmeta_data_desc *root,
    void *state) {
    const cmeta_data_struct_shape *shape =
        (const cmeta_data_struct_shape *)root->shape;
    JSValue global = JS_GetGlobalObject(conversion->context);
    size_t index;
    bool ok = !JS_IsException(global) && shape != NULL;
    for (index = 0u; ok && index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &shape->fields[index];
        const cmeta_field_desc *layout_field;
        JSValue value;
        if (field->name == NULL || !quickjs_property_budget(conversion)) {
            ok = false;
            break;
        }
        layout_field = cmeta_struct_find_field(shape->layout, field->name);
        if (layout_field == NULL ||
            layout_field->offset != field->offset ||
            !cmeta_type_desc_valid(layout_field->type)) {
            ok = false;
            break;
        }
        value = JS_GetPropertyStr(conversion->context, global, field->name);
        if (JS_IsException(value)) {
            ok = false;
            break;
        }
        ok = quickjs_export_value(
            conversion, field->value, layout_field->type,
            layout_field->declared_type, value,
            (unsigned char *)state + field->offset, 1u);
        JS_FreeValue(conversion->context, value);
    }
    JS_FreeValue(conversion->context, global);
    return ok;
}

static bool quickjs_scope_property_set(
    quickjs_conversion *conversion, JSValueConst global,
    const scxml_scope_slot *slot, JSValue value) {
    JSAtom atom;
    int result;
    atom = JS_NewAtomLen(
        conversion->context, slot->name, slot->name_size);
    if (atom == JS_ATOM_NULL) {
        JS_FreeValue(conversion->context, value);
        return false;
    }
    result = JS_SetProperty(conversion->context, global, atom, value);
    JS_FreeAtom(conversion->context, atom);
    return result >= 0;
}

static JSValue quickjs_scope_property_get(
    quickjs_conversion *conversion, JSValueConst global,
    const scxml_scope_slot *slot) {
    JSAtom atom;
    JSValue value;
    atom = JS_NewAtomLen(
        conversion->context, slot->name, slot->name_size);
    if (atom == JS_ATOM_NULL) return JS_EXCEPTION;
    value = JS_GetProperty(conversion->context, global, atom);
    JS_FreeAtom(conversion->context, atom);
    return value;
}

static bool quickjs_import_scope(
    quickjs_conversion *conversion, const scxml_scope_view *scope) {
    JSValue global;
    size_t index;
    bool ok;
    if (!scxml_scope_view_valid(scope)) return false;
    global = JS_GetGlobalObject(conversion->context);
    ok = !JS_IsException(global);
    for (index = 0u; ok && index < scope->schema->slot_count; ++index) {
        const scxml_scope_slot *slot = &scope->schema->slots[index];
        const cmeta_data_desc *descriptor;
        const void *object;
        JSValue value;
        if (!scxml_scope_view_read(
                scope, index, &descriptor, &object))
            continue;
        if (!quickjs_property_budget(conversion)) {
            ok = false;
            break;
        }
        value = quickjs_import_value(
            conversion, descriptor, object, 1u);
        if (JS_IsException(value) ||
            !quickjs_scope_property_set(conversion, global, slot, value)) {
            if (JS_IsException(value))
                JS_FreeValue(conversion->context, value);
            ok = false;
        }
    }
    JS_FreeValue(conversion->context, global);
    return ok;
}

static bool quickjs_export_scope(
    quickjs_conversion *conversion, scxml_scope_view *scope) {
    JSValue global;
    size_t index;
    bool ok;
    if (!scxml_scope_view_valid(scope)) return false;
    global = JS_GetGlobalObject(conversion->context);
    ok = !JS_IsException(global);
    for (index = 0u; ok && index < scope->schema->slot_count; ++index) {
        const scxml_scope_slot *slot = &scope->schema->slots[index];
        JSValue value;
        void *object;
        if (!quickjs_property_budget(conversion)) {
            ok = false;
            break;
        }
        value = quickjs_scope_property_get(conversion, global, slot);
        if (JS_IsException(value)) {
            ok = false;
            break;
        }
        if (JS_IsUndefined(value)) {
            JS_FreeValue(conversion->context, value);
            continue;
        }
        object = scope->storage + slot->offset;
        if (scope->bound[index] == 0u && slot->managed) {
            JS_FreeValue(conversion->context, value);
            ok = false;
            break;
        }
        ok = quickjs_export_value(
            conversion, slot->value, slot->value->storage_type,
            NULL, value, object, 1u);
        JS_FreeValue(conversion->context, value);
        if (ok) scope->bound[index] = 1u;
    }
    JS_FreeValue(conversion->context, global);
    return ok;
}

typedef struct quickjs_active_context {
    const scxml_session_impl *session;
    scxml_expr_is_active_fn is_active;
    void *active_user;
} quickjs_active_context;

static int quickjs_define_read_only(
    JSContext *context, JSValueConst object,
    const char *name, JSValue value) {
    const int flags = JS_PROP_HAS_VALUE | JS_PROP_HAS_WRITABLE |
                      JS_PROP_HAS_CONFIGURABLE | JS_PROP_HAS_ENUMERABLE |
                      JS_PROP_ENUMERABLE;
    return JS_DefinePropertyValueStr(context, object, name, value, flags);
}

static JSValue quickjs_system_string(
    JSContext *context, scxml_expr_string_view value) {
    return value.data != NULL
        ? JS_NewStringLen(context, value.data, value.size)
        : JS_UNDEFINED;
}

static JSValue quickjs_in_state(
    JSContext *context, JSValueConst this_value,
    int argument_count, JSValueConst *arguments) {
    quickjs_active_context *active =
        (quickjs_active_context *)JS_GetContextOpaque(context);
    const char *wanted;
    size_t wanted_size;
    size_t index;
    bool enabled = false;
    (void)this_value;
    if (active == NULL || active->session == NULL ||
        active->session->program == NULL || active->is_active == NULL ||
        argument_count != 1 || !JS_IsString(arguments[0]))
        return JS_ThrowTypeError(context, "In() requires one state id string");
    wanted = JS_ToCStringLen(context, &wanted_size, arguments[0]);
    if (wanted == NULL) return JS_EXCEPTION;
    for (index = 0u;
         index < active->session->program->state_name_count; ++index) {
        const scxml_program_name *candidate =
            &active->session->program->state_names[index];
        if (candidate->size == wanted_size &&
            memcmp(candidate->name, wanted, wanted_size) == 0) {
            if (!active->is_active(
                    active->active_user,
                    (cflow_machine_state_id)candidate->id, &enabled)) {
                JS_FreeCString(context, wanted);
                return JS_ThrowInternalError(
                    context, "In() state query failed");
            }
            JS_FreeCString(context, wanted);
            return JS_NewBool(context, enabled);
        }
    }
    JS_FreeCString(context, wanted);
    return JS_ThrowReferenceError(context, "In() state id is unknown");
}

static bool quickjs_install_system_values(
    quickjs_conversion *conversion,
    const scxml_expr_system_values *values,
    quickjs_active_context *active) {
    JSContext *context = conversion->context;
    JSValue global = JS_GetGlobalObject(context);
    JSValue event = JS_UNDEFINED;
    JSValue processors = JS_UNDEFINED;
    JSValue processor = JS_UNDEFINED;
    JSValue in_function = JS_UNDEFINED;
    size_t processor_index;
    bool ok = false;
    if (JS_IsException(global) || values == NULL || active == NULL)
        goto cleanup;
    JS_SetContextOpaque(context, active);
    if (quickjs_define_read_only(
            context, global, "_name",
            quickjs_system_string(context, values->name)) < 0 ||
        quickjs_define_read_only(
            context, global, "_sessionid",
            quickjs_system_string(context, values->session_id)) < 0)
        goto cleanup;
    if (values->event_name.data != NULL && values->event_name.size != 0u) {
        event = JS_NewObject(context);
        if (JS_IsException(event) ||
            quickjs_define_read_only(
                context, event, "name",
                quickjs_system_string(context, values->event_name)) < 0 ||
            quickjs_define_read_only(
                context, event, "type",
                quickjs_system_string(context, values->event_type)) < 0 ||
            quickjs_define_read_only(
                context, event, "sendid",
                quickjs_system_string(context, values->event_send_id)) < 0 ||
            quickjs_define_read_only(
                context, event, "origin",
                quickjs_system_string(context, values->event_origin)) < 0 ||
            quickjs_define_read_only(
                context, event, "origintype",
                quickjs_system_string(context, values->event_origin_type)) < 0 ||
            quickjs_define_read_only(
                context, event, "invokeid",
                quickjs_system_string(context, values->event_invoke_id)) < 0)
            goto cleanup;
        if (values->event_data_schema != NULL &&
            values->event_data_object != NULL) {
            JSValue data = quickjs_import_value(
                conversion, values->event_data_schema,
                values->event_data_object, 1u);
            if (JS_IsException(data) ||
                quickjs_define_read_only(context, event, "data", data) < 0) {
                if (JS_IsException(data)) JS_FreeValue(context, data);
                goto cleanup;
            }
        } else if (quickjs_define_read_only(
                       context, event, "data",
                       quickjs_system_string(context, values->event_data)) < 0) {
            goto cleanup;
        }
    }
    {
        const int defined =
            quickjs_define_read_only(context, global, "_event", event);
        event = JS_UNDEFINED;
        if (defined < 0) goto cleanup;
    }
    processors = JS_NewObject(context);
    if (JS_IsException(processors) ||
        (values->ioprocessor_count != 0u &&
         values->ioprocessors == NULL))
        goto cleanup;
    for (processor_index = 0u;
         processor_index < values->ioprocessor_count;
         ++processor_index) {
        const scxml_ioprocessor_descriptor *descriptor =
            &values->ioprocessors[processor_index];
        const scxml_expr_string_view location = {
            descriptor->location, descriptor->location_size};
        processor = JS_NewObject(context);
        if (JS_IsException(processor) || descriptor->name == NULL ||
            quickjs_define_read_only(
                context, processor, "location",
                quickjs_system_string(context, location)) < 0)
            goto cleanup;
        {
            const int defined = quickjs_define_read_only(
                context, processors, descriptor->name, processor);
            processor = JS_UNDEFINED;
            if (defined < 0) goto cleanup;
        }
    }
    {
        const int defined = quickjs_define_read_only(
            context, global, "_ioprocessors", processors);
        processors = JS_UNDEFINED;
        if (defined < 0) goto cleanup;
    }
    in_function = JS_NewCFunction(context, quickjs_in_state, "In", 1);
    if (JS_IsException(in_function))
        goto cleanup;
    {
        const int defined =
            quickjs_define_read_only(context, global, "In", in_function);
        in_function = JS_UNDEFINED;
        if (defined < 0) goto cleanup;
    }
    ok = true;
cleanup:
    if (!JS_IsUndefined(event)) JS_FreeValue(context, event);
    if (!JS_IsUndefined(processors)) JS_FreeValue(context, processors);
    if (!JS_IsUndefined(processor)) JS_FreeValue(context, processor);
    if (!JS_IsUndefined(in_function)) JS_FreeValue(context, in_function);
    JS_FreeValue(context, global);
    return ok;
}

static scxml_expr_status quickjs_expression_report(
    scxml_expr_diagnostic *diagnostic, scxml_expr_status status,
    const char *message) {
    if (diagnostic != NULL) {
        memset(diagnostic, 0, sizeof(*diagnostic));
        diagnostic->status = status;
        (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                       "%s", message != NULL ? message : "");
    }
    return status;
}

static bool quickjs_convert_scalar_result(
    scxml_quickjs_runtime *runtime, JSValueConst result,
    scxml_expr_value_kind expected_kind,
    scxml_expr_value *out_value) {
    JSContext *context = (JSContext *)runtime->context;
    scxml_expr_value value = {0};
    if (expected_kind == SCXML_EXPR_VALUE_INVALID) {
        if (JS_IsBool(result)) expected_kind = SCXML_EXPR_VALUE_BOOL;
        else if (JS_IsNumber(result)) expected_kind = SCXML_EXPR_VALUE_FLOAT;
        else if (JS_IsString(result)) expected_kind = SCXML_EXPR_VALUE_STRING;
        else return false;
    }
    value.kind = expected_kind;
    if (expected_kind == SCXML_EXPR_VALUE_BOOL) {
        const int boolean = JS_ToBool(context, result);
        if (!JS_IsBool(result) || boolean < 0) return false;
        value.data.boolean = boolean != 0;
    } else if (expected_kind == SCXML_EXPR_VALUE_SINT) {
        int64_t number;
        double exact;
        if (!JS_IsNumber(result) ||
            JS_ToInt64(context, &number, result) != 0 ||
            JS_ToFloat64(context, &exact, result) != 0 ||
            !isfinite(exact) ||
            exact < (double)quickjs_min_safe_integer ||
            exact > (double)quickjs_max_safe_integer ||
            exact != (double)number)
            return false;
        value.data.sint = number;
    } else if (expected_kind == SCXML_EXPR_VALUE_UINT) {
        double number;
        uint64_t converted;
        if (!JS_IsNumber(result) ||
            JS_ToFloat64(context, &number, result) != 0 ||
            !isfinite(number) || number < 0.0 ||
            number > (double)quickjs_max_safe_integer ||
            number != (double)(converted = (uint64_t)number))
            return false;
        value.data.uint = converted;
    } else if (expected_kind == SCXML_EXPR_VALUE_FLOAT) {
        if (!JS_IsNumber(result) ||
            JS_ToFloat64(context, &value.data.number, result) != 0)
            return false;
    } else if (expected_kind == SCXML_EXPR_VALUE_STRING) {
        const char *text;
        size_t size;
        if (!JS_IsString(result)) return false;
        text = JS_ToCStringLen(context, &size, result);
        if (text == NULL || size >= runtime->result_string_capacity) {
            if (text != NULL) JS_FreeCString(context, text);
            return false;
        }
        memcpy(runtime->result_string, text, size);
        runtime->result_string[size] = '\0';
        JS_FreeCString(context, text);
        value.data.string.data = runtime->result_string;
        value.data.string.size = size;
    } else {
        return false;
    }
    *out_value = value;
    return true;
}
#endif

scxml_expr_status scxml_quickjs_evaluate_expression(
    const char *source, size_t source_size,
    scxml_expr_value_kind expected_kind,
    const void *root_object,
    scxml_expr_is_active_fn is_active, void *active_user,
    const scxml_expr_system_values *system_values,
    scxml_expr_value *out_value,
    scxml_expr_diagnostic *diagnostic) {
#if !TURBOSCXML_HAS_QUICKJS
    (void)source;
    (void)source_size;
    (void)expected_kind;
    (void)root_object;
    (void)is_active;
    (void)active_user;
    (void)system_values;
    (void)out_value;
    if (diagnostic != NULL) {
        memset(diagnostic, 0, sizeof(*diagnostic));
        diagnostic->status = SCXML_EXPR_EVALUATION_ERROR;
        (void)snprintf(
            diagnostic->message, sizeof(diagnostic->message), "%s",
            "quickjs-sandbox is not available");
    }
    return SCXML_EXPR_EVALUATION_ERROR;
#else
    static const char prefix[] = "(\n";
    static const char suffix[] = "\n)";
    scxml_session_impl *session = system_values != NULL
        ? (scxml_session_impl *)system_values->datamodel_user : NULL;
    const scxml_program_impl *program =
        session != NULL ? session->program : NULL;
    scxml_quickjs_runtime *runtime =
        session != NULL ? session->quickjs_runtime : NULL;
    quickjs_conversion conversion;
    quickjs_active_context active;
    JSValue result = JS_UNDEFINED;
    char *wrapped = NULL;
    size_t wrapped_size;
    bool owns_deadline = false;
    char quickjs_diagnostic_text[SCXML_DIAGNOSTIC_CAPACITY] = {0};
    scxml_quickjs_status status;
    scxml_expr_status expression_status = SCXML_EXPR_EVALUATION_ERROR;
    if (source == NULL || source_size == 0u || root_object == NULL ||
        is_active == NULL || system_values == NULL || out_value == NULL ||
        session == NULL || program == NULL || runtime == NULL ||
        !program->quickjs_profile || program->cmeta_root == NULL ||
        system_values->supplemental == NULL ||
        !scxml_scope_view_valid(system_values->supplemental) ||
        source_size > SIZE_MAX - (sizeof(prefix) - 1u) -
                          (sizeof(suffix) - 1u) - 1u)
        return quickjs_expression_report(
            diagnostic, SCXML_EXPR_INVALID_ARGUMENT,
            "invalid QuickJS expression context");
    wrapped_size = sizeof(prefix) - 1u + source_size + sizeof(suffix) - 1u;
    wrapped = (char *)malloc(wrapped_size + 1u);
    if (wrapped == NULL)
        return quickjs_expression_report(
            diagnostic, SCXML_EXPR_ALLOCATION_FAILED,
            "QuickJS expression source allocation failed");
    memcpy(wrapped, prefix, sizeof(prefix) - 1u);
    memcpy(wrapped + sizeof(prefix) - 1u, source, source_size);
    memcpy(wrapped + sizeof(prefix) - 1u + source_size,
           suffix, sizeof(suffix) - 1u);
    wrapped[wrapped_size] = '\0';
    status = quickjs_context_recreate(
        runtime, quickjs_diagnostic_text,
        sizeof(quickjs_diagnostic_text));
    if (status != SCXML_QUICKJS_OK) goto cleanup;
    conversion = (quickjs_conversion){
        (JSContext *)runtime->context, &program->quickjs_options,
        program->cmeta_root, 0u};
    active = (quickjs_active_context){session, is_active, active_user};
    owns_deadline = quickjs_deadline_begin(
        runtime, program->quickjs_options.max_eval_milliseconds);
    if (!quickjs_import_root(
            &conversion, program->cmeta_root, root_object) ||
        !quickjs_import_scope(
            &conversion, system_values->supplemental) ||
        !quickjs_install_system_values(
            &conversion, system_values, &active)) {
        quickjs_diagnostic(
            quickjs_diagnostic_text, sizeof(quickjs_diagnostic_text),
            "QuickJS expression environment import failed");
        goto cleanup;
    }
    result = JS_Eval(
        (JSContext *)runtime->context, wrapped, wrapped_size,
        "<scxml-expression>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        status = quickjs_exception(
            runtime, quickjs_diagnostic_text,
            sizeof(quickjs_diagnostic_text));
        result = JS_UNDEFINED;
        expression_status = status == SCXML_QUICKJS_LIMIT_EXCEEDED
            ? SCXML_EXPR_LIMIT_EXCEEDED : SCXML_EXPR_EVALUATION_ERROR;
        goto cleanup;
    }
    if (!quickjs_convert_scalar_result(
            runtime, result, expected_kind, out_value)) {
        quickjs_diagnostic(
            quickjs_diagnostic_text, sizeof(quickjs_diagnostic_text),
            "QuickJS expression result is not an exact admitted scalar");
        expression_status = SCXML_EXPR_TYPE_MISMATCH;
        goto cleanup;
    }
    if (quickjs_deadline_expired(runtime)) {
        expression_status = SCXML_EXPR_LIMIT_EXCEEDED;
        quickjs_diagnostic(
            quickjs_diagnostic_text, sizeof(quickjs_diagnostic_text),
            "QuickJS expression transaction deadline exceeded");
        goto cleanup;
    }
    expression_status = SCXML_EXPR_OK;
cleanup:
    if (runtime != NULL && runtime->interrupted &&
        expression_status != SCXML_EXPR_OK) {
        expression_status = SCXML_EXPR_LIMIT_EXCEEDED;
        quickjs_diagnostic(
            quickjs_diagnostic_text, sizeof(quickjs_diagnostic_text),
            "QuickJS expression transaction deadline exceeded");
    }
    if (runtime != NULL) quickjs_deadline_end(runtime, owns_deadline);
    if (runtime != NULL && runtime->context != NULL) {
        JS_SetContextOpaque((JSContext *)runtime->context, NULL);
        if (!JS_IsUndefined(result))
            JS_FreeValue((JSContext *)runtime->context, result);
        quickjs_context_destroy(runtime);
    }
    if (runtime != NULL && expression_status != SCXML_EXPR_OK) {
        scxml_quickjs_runtime_destroy(runtime);
        if (scxml_quickjs_runtime_init(
                runtime, &program->quickjs_options,
                NULL, 0u) != SCXML_QUICKJS_OK) {
            quickjs_diagnostic(
                quickjs_diagnostic_text, sizeof(quickjs_diagnostic_text),
                "QuickJS expression runtime recovery failed");
            expression_status = SCXML_EXPR_EVALUATION_ERROR;
        }
    }
    free(wrapped);
    return quickjs_expression_report(
        diagnostic, expression_status,
        expression_status == SCXML_EXPR_OK ? NULL
            : quickjs_diagnostic_text[0] != '\0'
                ? quickjs_diagnostic_text
                : "QuickJS expression evaluation failed");
#endif
}

bool scxml_quickjs_session_runtime_init(
    scxml_session_impl *session, const char **out_error) {
    const scxml_program_impl *program =
        session != NULL ? session->program : NULL;
    scxml_quickjs_runtime *runtime;
    char diagnostic[SCXML_DIAGNOSTIC_CAPACITY] = {0};
    scxml_quickjs_status status;
    if (out_error != NULL) *out_error = NULL;
    if (session == NULL || program == NULL || out_error == NULL ||
        !program->quickjs_profile || session->quickjs_runtime != NULL) {
        if (out_error != NULL)
            *out_error = "invalid QuickJS session runtime context";
        return false;
    }
    runtime = (scxml_quickjs_runtime *)calloc(1u, sizeof(*runtime));
    if (runtime == NULL) {
        *out_error = "QuickJS session runtime allocation failed";
        return false;
    }
    status = scxml_quickjs_runtime_init(
        runtime, &program->quickjs_options,
        diagnostic, sizeof(diagnostic));
    if (status != SCXML_QUICKJS_OK) {
        free(runtime);
        *out_error = status == SCXML_QUICKJS_LIMIT_EXCEEDED
            ? "QuickJS session runtime limit exceeded"
            : "QuickJS session runtime initialization failed";
        return false;
    }
    session->quickjs_runtime = runtime;
    return true;
}

void scxml_quickjs_session_runtime_destroy(scxml_session_impl *session) {
    if (session == NULL || session->quickjs_runtime == NULL) return;
    scxml_quickjs_runtime_destroy(session->quickjs_runtime);
    free(session->quickjs_runtime);
    session->quickjs_runtime = NULL;
}

bool scxml_quickjs_execute_script(
    scxml_session_impl *session,
    const scxml_script_descriptor *script,
    void *state,
    scxml_scope_view *supplemental,
    scxml_expr_is_active_fn is_active, void *active_user,
    const scxml_expr_system_values *system_values,
    const char **out_error) {
    const scxml_program_impl *program =
        session != NULL ? session->program : NULL;
    if (out_error != NULL) *out_error = NULL;
    if (program == NULL || session->quickjs_runtime == NULL ||
        script == NULL || state == NULL || is_active == NULL ||
        system_values == NULL ||
        out_error == NULL || !program->quickjs_profile ||
        program->cmeta_root == NULL ||
        !scxml_scope_view_valid(supplemental) ||
        supplemental->schema != &program->supplemental_scope) {
        if (out_error != NULL) *out_error = "invalid QuickJS execution context";
        return false;
    }
#if !TURBOSCXML_HAS_QUICKJS
    *out_error = "quickjs-sandbox is not available";
    return false;
#else
    {
        scxml_quickjs_runtime *runtime = session->quickjs_runtime;
        quickjs_conversion conversion;
        quickjs_active_context active;
        const cmeta_type_desc *type = program->cmeta_root->storage_type;
        quickjs_aligned_storage state_scratch = {0};
        void *scope_scratch_allocation = NULL;
        scxml_scope_view scope_scratch = {0};
        size_t scope_scratch_bytes = 0u;
        size_t snapshot_bytes;
        size_t scope_align;
        uintptr_t scope_address;
        uintptr_t scope_aligned;
        bool managed;
        bool scratch_live = false;
        bool owns_deadline = false;
        char diagnostic[SCXML_DIAGNOSTIC_CAPACITY] = {0};
        scxml_quickjs_status status;
        status = quickjs_context_recreate(
            runtime, diagnostic, sizeof(diagnostic));
        if (status != SCXML_QUICKJS_OK) {
            *out_error = "QuickJS working context initialization failed";
            goto cleanup;
        }
        conversion = (quickjs_conversion){
            (JSContext *)runtime->context, &program->quickjs_options,
            program->cmeta_root, 0u};
        active = (quickjs_active_context){session, is_active, active_user};
        owns_deadline = quickjs_deadline_begin(
            runtime, program->quickjs_options.max_eval_milliseconds);
        scope_align = supplemental->schema->storage_align;
        if (type == NULL || type->size == 0u ||
            scope_align == 0u ||
            (scope_align & (scope_align - 1u)) != 0u ||
            supplemental->schema->storage_size > SIZE_MAX - (scope_align - 1u) ||
            supplemental->schema->storage_size + (scope_align - 1u) >
                SIZE_MAX - supplemental->schema->slot_count ||
            type->size > SIZE_MAX - supplemental->schema->storage_size ||
            type->size + supplemental->schema->storage_size >
                SIZE_MAX - (scope_align - 1u) -
                    supplemental->schema->slot_count) {
            *out_error = "QuickJS snapshot shape is invalid";
            goto cleanup;
        }
        scope_scratch_bytes = supplemental->schema->storage_size +
                              (scope_align - 1u) +
                              supplemental->schema->slot_count;
        snapshot_bytes = type->size + scope_scratch_bytes;
        if (snapshot_bytes > program->quickjs_options.max_snapshot_bytes) {
            *out_error = "QuickJS snapshot limit exceeded";
            goto cleanup;
        }
        scope_scratch.schema = supplemental->schema;
        if (scope_scratch_bytes != 0u) {
            scope_scratch_allocation = calloc(1u, scope_scratch_bytes);
            if (scope_scratch_allocation == NULL) {
                *out_error = "QuickJS scope scratch allocation failed";
                goto cleanup;
            }
            scope_address = (uintptr_t)scope_scratch_allocation;
            if (scope_address > UINTPTR_MAX - (scope_align - 1u)) {
                *out_error = "QuickJS scope scratch address overflow";
                goto cleanup;
            }
            scope_aligned = (scope_address + scope_align - 1u) &
                            ~((uintptr_t)scope_align - 1u);
            scope_scratch.storage = (unsigned char *)scope_aligned;
            scope_scratch.bound = scope_scratch.storage +
                                  supplemental->schema->storage_size;
            if (!scxml_scope_view_copy(&scope_scratch, supplemental)) {
                *out_error = "QuickJS scope snapshot failed";
                goto cleanup;
            }
        }
        if (!quickjs_import_root(
                &conversion, program->cmeta_root, state) ||
            !quickjs_import_scope(&conversion, supplemental) ||
            !quickjs_install_system_values(
                &conversion, system_values, &active)) {
            *out_error = "QuickJS CMeta import failed";
            goto cleanup;
        }
        status = scxml_quickjs_runtime_eval(
            runtime, script->source, script->source_size,
            script->root ? "<scxml-root-script>" : "<scxml-script>",
            program->quickjs_options.max_eval_milliseconds,
            diagnostic, sizeof(diagnostic));
        if (status != SCXML_QUICKJS_OK) {
            *out_error = status == SCXML_QUICKJS_LIMIT_EXCEEDED
                ? "QuickJS script limit exceeded" : "QuickJS script exception";
            goto cleanup;
        }
        if (!quickjs_aligned_storage_init(&state_scratch, type)) {
            *out_error = "QuickJS state scratch allocation failed";
            goto cleanup;
        }
        managed = cmeta_type_require_traits(
                      type, CMETA_TRAIT_TRIVIAL_COPY |
                                CMETA_TRAIT_TRIVIAL_DESTROY) != CMETA_OK;
        if (managed) {
            if (cmeta_type_require_traits(
                    type, CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                              CMETA_TRAIT_DESTROY) != CMETA_OK ||
                !type->traits->copy_construct(state_scratch.value, state)) {
                *out_error = "QuickJS state snapshot failed";
                goto cleanup;
            }
            scratch_live = true;
        } else {
            memcpy(state_scratch.value, state, type->size);
        }
        conversion.properties = 0u;
        if (!quickjs_export_root(
                &conversion, program->cmeta_root, state_scratch.value) ||
            !quickjs_export_scope(&conversion, &scope_scratch)) {
            *out_error = "QuickJS CMeta export failed";
            goto cleanup;
        }
        if (quickjs_deadline_expired(runtime)) {
            *out_error = "QuickJS script transaction deadline exceeded";
            goto cleanup;
        }
        if (!scxml_scope_view_move_replace(
                supplemental, &scope_scratch)) {
            *out_error = "QuickJS scope publication failed";
            goto cleanup;
        }
        if (managed) {
            type->traits->destroy(state);
            type->traits->move_construct(state, state_scratch.value);
            scratch_live = false;
        } else {
            memcpy(state, state_scratch.value, type->size);
        }
cleanup:
        if (runtime->interrupted && *out_error != NULL)
            *out_error = "QuickJS script transaction deadline exceeded";
        quickjs_deadline_end(runtime, owns_deadline);
        if (runtime->context != NULL) {
            JS_SetContextOpaque((JSContext *)runtime->context, NULL);
            quickjs_context_destroy(runtime);
        }
        scxml_scope_view_clear(&scope_scratch);
        free(scope_scratch_allocation);
        quickjs_aligned_storage_destroy(
            &state_scratch, type, scratch_live);
        if (*out_error != NULL) {
            scxml_quickjs_runtime_destroy(runtime);
            if (scxml_quickjs_runtime_init(
                    runtime, &program->quickjs_options,
                    NULL, 0u) != SCXML_QUICKJS_OK)
                *out_error = "QuickJS session runtime recovery failed";
        }
        return *out_error == NULL;
    }
#endif
}

scxml_quickjs_compile_options_v1
scxml_quickjs_default_compile_options_impl(const cmeta_data_desc *root) {
    const scxml_cmeta_compile_options_v1 cmeta =
        scxml_cmeta_default_compile_options(root);
    const scxml_quickjs_compile_options_v1 options = {
        .abi_version = SCXML_QUICKJS_COMPILE_OPTIONS_ABI_V1,
        .struct_size = sizeof(scxml_quickjs_compile_options_v1),
        .root = root,
        .max_source_bytes = cmeta.max_source_bytes,
        .max_instructions = cmeta.max_instructions,
        .max_operands = cmeta.max_operands,
        .max_expression_depth = cmeta.max_expression_depth,
        .max_path_depth = cmeta.max_path_depth,
        .max_literal_bytes = cmeta.max_literal_bytes,
        .max_string_bytes = cmeta.max_string_bytes,
        .max_iterations = cmeta.max_iterations,
        .max_script_variables = SCXML_QUICKJS_DEFAULT_MAX_SCRIPT_VARIABLES,
        .max_heap_bytes = SCXML_QUICKJS_DEFAULT_HEAP_BYTES,
        .max_stack_bytes = SCXML_QUICKJS_DEFAULT_STACK_BYTES,
        .max_eval_milliseconds = SCXML_QUICKJS_DEFAULT_EVAL_MILLISECONDS,
        .max_conversion_depth = SCXML_QUICKJS_DEFAULT_MAX_CONVERSION_DEPTH,
        .max_properties = SCXML_QUICKJS_DEFAULT_MAX_PROPERTIES,
        .max_array_items = SCXML_QUICKJS_DEFAULT_MAX_ARRAY_ITEMS,
        .max_snapshot_bytes = SCXML_QUICKJS_DEFAULT_MAX_SNAPSHOT_BYTES
    };
    return options;
}

scxml_status scxml_quickjs_compile(
    scxml_program *out, const char *input, size_t input_size,
    const scxml_limits *limits,
    const scxml_quickjs_compile_options_v1 *options,
    scxml_diagnostic *diagnostic) {
    (void)limits;
    if (out == NULL || out->impl != NULL || input == NULL || input_size == 0u ||
        options == NULL ||
        options->abi_version != SCXML_QUICKJS_COMPILE_OPTIONS_ABI_V1 ||
        !scxml_quickjs_limits_valid(options)) {
        return quickjs_fail(
            diagnostic, SCXML_INVALID_ARGUMENT,
            "QuickJS compile options and CMeta root must be valid");
    }
#if !TURBOSCXML_HAS_QUICKJS
    return quickjs_fail(
        diagnostic, SCXML_UNSUPPORTED_FEATURE,
        "TurboSCXML was built without quickjs-sandbox support");
#else
    return scxml_program_compile_quickjs_model(
        out, input, input_size, limits, options, diagnostic);
#endif
}

cflow_statechart_instance_status scxml_quickjs_session_init(
    scxml_session *session, const scxml_session_config *config,
    const scxml_quickjs_session_options_v1 *options) {
    return scxml_session_init_quickjs_model(session, config, options);
}
