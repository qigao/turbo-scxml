#include "voicexml_quickjs_fia.h"
#include "voicexml_allocator.h"
#include "voicexml_cmeta_internal.h"

#include <tstr.h>
#include <math.h>
#include <string.h>

typedef struct fia_expression {
    vxml_quickjs_fia_limits limits;
    const cmeta_data_desc *root;
    tstr source;
    vxml_cmeta_code_kind kind;
    bool external;
    bool dynamic_reference;
} fia_expression;

typedef struct fia_session {
    vxml_script_resource_adapter_v1 resources;
    void *user;
    size_t max_uri_bytes;
    size_t max_source_bytes;
} fia_session;

enum { FIA_APPLICATION, FIA_DOCUMENT, FIA_DIALOG, FIA_ANONYMOUS, FIA_NAMED_SCOPES = FIA_ANONYMOUS };
static const char *const scope_names[FIA_NAMED_SCOPES] = {"application", "document", "dialog"};

/* One typed slot has one JS cell, shared by its qualified and unqualified
 * accessors. Hidden outer slots remain reachable through their owning scope. */
typedef struct fia_binding {
    const char *name;
    size_t name_size;
    const cmeta_data_desc *data;
    const cmeta_type_desc *type;
    const cmeta_declared_type *declared_type;
    void *destination;
    const void *prototype;
    unsigned char *bound;
    const cmeta_scope_view *scope;
    size_t slot;
    size_t rank;
    unsigned namespace_id;
    bool dialog_result;
    bool defined;
    quickjs_cmeta_state_scratch pending;
} fia_binding;

static vxml_status engine_status(quickjs_sandbox_status status) {
    switch (status) {
    case QUICKJS_SANDBOX_OK: return VXML_OK;
    case QUICKJS_SANDBOX_ALLOCATION_FAILED: return VXML_ALLOCATION_FAILED;
    case QUICKJS_SANDBOX_LIMIT_EXCEEDED: return VXML_LIMIT_EXCEEDED;
    case QUICKJS_SANDBOX_INVALID_ARGUMENT: return VXML_INVALID_CONTRACT;
    default: return VXML_SEMANTIC_ERROR;
    }
}

static void expression_destroy(void *opaque) {
    fia_expression *expression = opaque;
    if (expression == NULL) return;
    tstr_free(expression->source);
    vxml_free(expression);
}

static bool expression_limits_valid(const vxml_quickjs_fia_limits *limits,
    const cmeta_data_desc *root) {
    return limits != NULL && quickjs_sandbox_limits_valid(&limits->engine) &&
        quickjs_cmeta_limits_valid(&limits->values) &&
        quickjs_cmeta_schema_supported(root, &limits->values,
            quickjs_cmeta_default_collection_adapter(), NULL, 0u) &&
        quickjs_cmeta_state_lifecycle_supported(root);
}

static vxml_status expression_compile(
    const void *user, const char *source, size_t size, vxml_cmeta_code_kind kind,
    const cmeta_data_desc *root, void **out,
    vxml_cmeta_expr_diagnostic *diagnostic) {
    const vxml_quickjs_fia_limits *limits = user;
    fia_expression *expression;
    quickjs_sandbox_status status;
    *out = NULL;
    if ((kind != VXML_CMETA_CODE_VALUE && kind != VXML_CMETA_CODE_CONDITION &&
         kind != VXML_CMETA_CODE_SCRIPT) || !expression_limits_valid(limits, root))
        return VXML_INVALID_CONTRACT;
    if (size > SIZE_MAX - 5u || size > limits->engine.max_source_bytes ||
        root->storage_type->size > limits->values.max_snapshot_bytes)
        return VXML_LIMIT_EXCEEDED;
    status = kind == VXML_CMETA_CODE_SCRIPT
        ? quickjs_sandbox_validate_source(&limits->engine, source, size,
            "<voicexml-fia-script>", diagnostic->message, sizeof(diagnostic->message))
        : quickjs_sandbox_validate_expression(&limits->engine, source, size,
            "<voicexml-fia-expression>", diagnostic->message, sizeof(diagnostic->message));
    if (status != QUICKJS_SANDBOX_OK) return engine_status(status);
    expression = vxml_calloc(1u, sizeof(*expression));
    if (expression == NULL) return VXML_ALLOCATION_FAILED;
    /* The engine requires a NUL-terminated source. tstr owns the wrapper and
     * checks its allocation; the bytes are immutable after publication. */
    expression->source = kind == VXML_CMETA_CODE_SCRIPT
        ? tstr_new_len(source, size) : tstr_new_len(NULL, size + 4u);
    if (expression->source == NULL) {
        expression_destroy(expression);
        return VXML_ALLOCATION_FAILED;
    }
    if (kind != VXML_CMETA_CODE_SCRIPT) {
        memcpy(expression->source, "(\n", 2u);
        memcpy(expression->source + 2u, source, size);
        memcpy(expression->source + 2u + size, "\n)", 2u);
    }
    expression->root = root;
    expression->limits = *limits;
    expression->kind = kind;
    *out = expression;
    return VXML_OK;
}

static vxml_status external_script_compile(const void *user,
    const char *reference, size_t size, bool dynamic,
    const char *charset, size_t charset_size,
    const cmeta_data_desc *root, void **out, vxml_cmeta_expr_diagnostic *diagnostic) {
    const vxml_quickjs_fia_limits *limits = user;
    fia_expression *expression;
    *out = NULL;
    if (!expression_limits_valid(limits, root) || reference == NULL || size == 0u ||
        memchr(reference, '\0', size) != NULL || charset == NULL ||
        charset_size != 5u || memcmp(charset, "UTF-8", 5u) != 0)
        return VXML_INVALID_CONTRACT;
    if (size > limits->engine.max_source_bytes ||
        root->storage_type->size > limits->values.max_snapshot_bytes)
        return VXML_LIMIT_EXCEEDED;
    if (dynamic) {
        const vxml_status status = expression_compile(user, reference, size,
            VXML_CMETA_CODE_VALUE, root, out, diagnostic);
        if (status != VXML_OK) return status;
        expression = *out;
    } else {
        if (memchr(reference, '#', size) != NULL) return VXML_INVALID_STRUCTURE;
        expression = vxml_calloc(1u, sizeof(*expression));
        if (expression == NULL) return VXML_ALLOCATION_FAILED;
        expression->source = tstr_new_len(reference, size);
        if (expression->source == NULL) {
            expression_destroy(expression);
            return VXML_ALLOCATION_FAILED;
        }
        expression->root = root;
        expression->limits = *limits;
        *out = expression;
    }
    expression->kind = VXML_CMETA_CODE_SCRIPT;
    expression->external = true;
    expression->dynamic_reference = dynamic;
    return VXML_OK;
}

static vxml_status script_resource_status(
    const vxml_cmeta_backend_runtime *runtime, vxml_script_resource_status status) {
    if (runtime->failure_event != NULL && status != VXML_SCRIPT_RESOURCE_OK) {
        runtime->failure_event->data = vxml_script_resource_failure_event(
            status, &runtime->failure_event->size);
    }
    switch (status) {
    case VXML_SCRIPT_RESOURCE_OK: return VXML_OK;
    case VXML_SCRIPT_RESOURCE_ALLOCATION_FAILED: return VXML_ALLOCATION_FAILED;
    case VXML_SCRIPT_RESOURCE_LIMIT_EXCEEDED: return VXML_LIMIT_EXCEEDED;
    case VXML_SCRIPT_RESOURCE_INVALID_ARGUMENT: return VXML_INVALID_CONTRACT;
    default: return VXML_SEMANTIC_ERROR;
    }
}

static vxml_status external_script_evaluate(const fia_expression *expression,
    const vxml_cmeta_backend_runtime *runtime, quickjs_sandbox_runtime *engine,
    bool *owns_deadline, JSValue *out) {
    const fia_session *session = runtime->session_user;
    const vxml_session_document_context *document = runtime->document;
    JSContext *context = engine->context;
    JSValue target = JS_UNDEFINED;
    const char *dynamic = NULL;
    const char *reference = expression->source;
    size_t size = tstr_len(expression->source);
    vxml_script_source source = {0};
    tstr code = NULL;
    vxml_status status = VXML_OK;
    if (session == NULL || document == NULL || document->resolver == NULL ||
        document->uri == NULL || document->uri_size == 0u) return VXML_INVALID_CONTRACT;
    if (expression->dynamic_reference) {
        target = JS_Eval(context, reference, size, "<voicexml-script-reference>",
            JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT);
        if (JS_IsException(target)) {
            status = engine_status(quickjs_sandbox_exception(engine, NULL, 0u));
            goto done;
        }
        if (!JS_IsString(target)) { status = VXML_SEMANTIC_ERROR; goto done; }
        dynamic = JS_ToCStringLen(context, &size, target);
        if (dynamic == NULL) {
            status = engine_status(quickjs_sandbox_exception(engine, NULL, 0u));
            goto done;
        }
        reference = dynamic;
    }
    if (size == 0u || memchr(reference, '\0', size) != NULL) {
        status = VXML_SEMANTIC_ERROR;
        goto done;
    }
    if (size > session->max_uri_bytes || size > expression->limits.engine.max_string_bytes) {
        status = script_resource_status(runtime, VXML_SCRIPT_RESOURCE_LIMIT_EXCEEDED);
        goto done;
    }
    if (quickjs_sandbox_deadline_expired(engine)) {
        status = VXML_LIMIT_EXCEEDED;
        goto done;
    }
    /* Each JavaScript evaluation has its own deadline. Transport time belongs
     * to the synchronous provider and is never presented as an engine quota. */
    quickjs_sandbox_deadline_end(engine, *owns_deadline);
    *owns_deadline = false;
    vxml_script_request_v1 request = VXML_SCRIPT_REQUEST_V1_INIT;
    request.base_document_uri = document->uri;
    request.base_document_uri_size = document->uri_size;
    request.reference = reference;
    request.reference_size = size;
    request.charset = "UTF-8";
    request.charset_size = 5u;
    request.max_uri_bytes = session->max_uri_bytes;
    request.max_source_bytes = session->max_source_bytes < expression->limits.engine.max_source_bytes
        ? session->max_source_bytes : expression->limits.engine.max_source_bytes;
    status = script_resource_status(runtime, vxml_script_resource_acquire(document->resolver,
        &session->resources, session->user, &request, &source));
    if (status != VXML_OK) goto done;
    if (source.size != 0u && memchr(source.data, '\0', source.size) != NULL) {
        status = script_resource_status(runtime, VXML_SCRIPT_RESOURCE_INVALID_DATA);
        goto done;
    }
    code = source.size != 0u ? tstr_new_len(source.data, source.size) : tstr_dup(";");
    if (code == NULL) { status = VXML_ALLOCATION_FAILED; goto done; }
    /* The bounded copy separates provider lifetime from engine execution.
     * Provider close, like acquisition, is outside the JavaScript deadline. */
    status = script_resource_status(runtime, vxml_script_resource_close(&session->resources, session->user, &source));
    if (status != VXML_OK) goto done;
    *owns_deadline = quickjs_sandbox_deadline_begin(engine, expression->limits.engine.max_eval_milliseconds);
    *out = JS_Eval(context, code, tstr_len(code), "<voicexml-external-script>",
        JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT);
done:
    tstr_free(code);
    if (source.lease != NULL) {
        const vxml_script_resource_status closed =
            vxml_script_resource_close(&session->resources, session->user, &source);
        if (status == VXML_OK) status = script_resource_status(runtime, closed);
    }
    if (dynamic != NULL) JS_FreeCString(context, dynamic);
    JS_FreeValue(context, target);
    return status;
}

static size_t expression_scratch_bytes(const void *opaque) {
    return ((const fia_expression *)opaque)->limits.values.max_string_bytes;
}

static bool binding_exists(const fia_binding *bindings, size_t count,
    const char *name, size_t size) {
    for (size_t i = 0u; i < count; ++i)
        if (bindings[i].name_size == size &&
            memcmp(bindings[i].name, name, size) == 0) return true;
    return false;
}

static vxml_status bindings_create(const fia_expression *expression,
    const vxml_cmeta_backend_runtime *runtime, fia_binding **out, size_t *out_count,
    size_t ranks[FIA_NAMED_SCOPES]) {
    const cmeta_data_struct_shape *shape = expression->root->shape;
    const size_t capacity = expression->limits.values.max_properties;
    size_t count = 0u, bytes = 0u;
    fia_binding *bindings;
    size_t form = VXML_CMETA_NO_INDEX;
    *out = NULL;
    *out_count = 0u;
    if (runtime == NULL || runtime->root == NULL || runtime->root_bound == NULL ||
        runtime->root_bound_count != shape->field_count ||
        (runtime->scope_count != 0u && runtime->scopes == NULL))
        return VXML_INVALID_CONTRACT;
    if (capacity > UINT32_MAX || capacity > SIZE_MAX / sizeof(*bindings)) return VXML_LIMIT_EXCEEDED;
    bindings = vxml_calloc(capacity, sizeof(*bindings));
    if (bindings == NULL) return VXML_ALLOCATION_FAILED;
    *out = bindings;
    ranks[FIA_APPLICATION] = runtime->scope_count;
    ranks[FIA_DOCUMENT] = ranks[FIA_DIALOG] = SIZE_MAX;
    for (size_t i = 0u; i < runtime->scope_count; ++i) {
        const vxml_cmeta_expr_runtime_scope *scope = &runtime->scopes[i];
        const cmeta_scope_view *view = scope->view;
        unsigned namespace_id = FIA_ANONYMOUS;
        if (runtime->program == NULL || scope->scope_id >= runtime->program->scope_count)
            return VXML_INVALID_CONTRACT;
        const vxml_cmeta_scope_row *row = &runtime->program->scopes[scope->scope_id];
        if (row->kind == VXML_CMETA_SCOPE_DOCUMENT) namespace_id = FIA_DOCUMENT;
        else if (row->kind == VXML_CMETA_SCOPE_FORM) {
            namespace_id = FIA_DIALOG;
            form = row->owner;
        }
        if (namespace_id != FIA_ANONYMOUS) {
            if (ranks[namespace_id] != SIZE_MAX) return VXML_INVALID_CONTRACT;
            ranks[namespace_id] = i;
        }
        if (!cmeta_scope_view_valid(view) ||
            scope->declared_count != view->schema->slot_count ||
            (scope->declared_count != 0u && scope->declared == NULL))
            return VXML_INVALID_CONTRACT;
        for (size_t j = 0u; j < scope->declared_count; ++j) {
            const cmeta_scope_slot *slot = &view->schema->slots[j];
            if (!scope->declared[j]) continue;
            if (count == capacity) return VXML_LIMIT_EXCEEDED;
            bindings[count++] = (fia_binding){
                .name = slot->name, .name_size = slot->name_size,
                .data = slot->value, .type = slot->value->storage_type,
                .destination = view->storage + slot->offset,
                .prototype = view->bound[j] ? view->storage + slot->offset : NULL,
                .bound = &view->bound[j], .scope = view, .slot = j,
                .rank = i, .namespace_id = namespace_id};
        }
    }
    for (size_t i = 0u; i < shape->field_count; ++i) {
        const cmeta_data_field_desc *field = &shape->fields[i];
        const cmeta_field_desc *layout = cmeta_struct_field(shape->layout, i);
        const size_t name_size = strlen(field->name);
        if (count == capacity) return VXML_LIMIT_EXCEEDED;
        bool dialog_result = false;
        if (form != VXML_CMETA_NO_INDEX) {
            const vxml_cmeta_program_data *program = runtime->program;
            if (form >= program->form_count) return VXML_INVALID_CONTRACT;
            dialog_result = vxml_cmeta_form_uses_root_field(program, form, i);
        }
        bindings[count++] = (fia_binding){
            .name = field->name, .name_size = name_size, .data = field->value,
            .type = layout->type, .declared_type = layout->declared_type,
            .destination = (unsigned char *)runtime->root + field->offset,
            .prototype = runtime->root_bound[i]
                ? (const unsigned char *)runtime->root + field->offset : NULL,
            .bound = runtime->root_bound + i, .rank = runtime->scope_count,
            .namespace_id = FIA_APPLICATION, .dialog_result = dialog_result};
    }
    *out_count = count;
    for (size_t i = 0u; i < count; ++i) {
        const cmeta_type_desc *type = bindings[i].type;
        size_t properties = 0u;
        if (!quickjs_cmeta_value_lifecycle_supported(bindings[i].data) ||
            !quickjs_cmeta_value_schema_supported(expression->root, bindings[i].data,
                bindings[i].declared_type, 1u, &expression->limits.values,
                quickjs_cmeta_default_collection_adapter(), NULL, false, &properties))
            return VXML_INVALID_CONTRACT;
        if (type->size > SIZE_MAX - (type->align - 1u) ||
            type->size + type->align - 1u > expression->limits.values.max_snapshot_bytes - bytes)
            return VXML_LIMIT_EXCEEDED;
        bytes += type->size + type->align - 1u;
    }
    return VXML_OK;
}

static JSValue cell_get(JSContext *context, JSValueConst self, int argc,
    JSValueConst *argv, int magic, JSValue *data) {
    uint32_t index;
    (void)self; (void)argc; (void)argv; (void)magic;
    if (JS_ToUint32(context, &index, data[1]) != 0) return JS_EXCEPTION;
    return JS_GetPropertyUint32(context, data[0], index);
}
static JSValue cell_set(JSContext *context, JSValueConst self, int argc,
    JSValueConst *argv, int magic, JSValue *data) {
    uint32_t index;
    (void)self; (void)magic;
    if (JS_ToUint32(context, &index, data[1]) != 0) return JS_EXCEPTION;
    if (JS_SetPropertyUint32(context, data[0], index,
        JS_DupValue(context, argc != 0 ? argv[0] : JS_UNDEFINED)) < 0) return JS_EXCEPTION;
    return JS_UNDEFINED;
}
static bool define_cell(JSContext *context, JSValueConst object, const fia_binding *binding,
    JSValueConst cells, size_t index) {
    JSValue data[] = {cells, JS_NewUint32(context, (uint32_t)index)};
    JSValue get = JS_NewCFunctionData(context, cell_get, 0, 0, 2, data);
    if (JS_IsException(get)) return false;
    JSValue set = JS_NewCFunctionData(context, cell_set, 1, 0, 2, data);
    if (JS_IsException(set)) {
        JS_FreeValue(context, get);
        return false;
    }
    JSAtom name = JS_NewAtomLen(context, binding->name, binding->name_size);
    if (name == JS_ATOM_NULL) {
        JS_FreeValue(context, get);
        JS_FreeValue(context, set);
        return false;
    }
    const int result = JS_DefinePropertyGetSet(context, object, name, get, set, JS_PROP_ENUMERABLE);
    JS_FreeAtom(context, name);
    return result > 0;
}
static bool name_is(const fia_binding *binding, const char *name) {
    return binding->name_size == strlen(name) && memcmp(binding->name, name, binding->name_size) == 0;
}
static bool project_bindings(quickjs_cmeta_bridge *bridge, JSValueConst global,
    JSValueConst cells, JSValue scopes[FIA_NAMED_SCOPES], const size_t ranks[FIA_NAMED_SCOPES],
    const fia_binding *bindings, size_t count) {
    JSContext *context = bridge->context;
    for (unsigned s = 0u; s < FIA_NAMED_SCOPES; ++s) {
        if (ranks[s] == SIZE_MAX) continue;
        scopes[s] = JS_NewObjectProto(context, JS_NULL);
        if (JS_IsException(scopes[s])) return false;
    }
    bridge->properties = count;
    for (size_t i = 0u; i < count; ++i) {
        const fia_binding *binding = &bindings[i];
        JSValue imported = *binding->bound ? quickjs_cmeta_import_value(bridge,
            binding->data, binding->type, binding->declared_type, binding->destination, 1u) : JS_UNDEFINED;
        if (JS_IsException(imported)) return false;
        if (JS_SetPropertyUint32(context, cells, (uint32_t)i, imported) < 0) return false;
        if (binding->namespace_id != FIA_ANONYMOUS &&
            !define_cell(context, scopes[binding->namespace_id], binding, cells, i)) return false;
        if (binding->dialog_result && !define_cell(context, scopes[FIA_DIALOG], binding, cells, i)) return false;
        bool visible = !binding_exists(bindings, i, binding->name, binding->name_size);
        for (unsigned s = 0u; s < FIA_NAMED_SCOPES; ++s)
            if (ranks[s] < binding->rank && name_is(binding, scope_names[s])) visible = false;
        if (visible && !define_cell(context, global, binding, cells, i)) return false;
    }
    for (unsigned s = 0u; s < FIA_NAMED_SCOPES; ++s) {
        if (ranks[s] == SIZE_MAX) continue;
        bool hidden = false, self_hidden = false;
        for (size_t i = 0u; i < count; ++i) {
            const fia_binding *binding = &bindings[i];
            if (!name_is(binding, scope_names[s])) continue;
            if (binding->rank <= ranks[s]) hidden = true;
            if (binding->namespace_id == s || (s == FIA_DIALOG && binding->dialog_result)) self_hidden = true;
        }
        if (!self_hidden && JS_DefinePropertyValueStr(context, scopes[s], scope_names[s],
            JS_DupValue(context, scopes[s]), JS_PROP_ENUMERABLE) <= 0) return false;
        if (!hidden && JS_DefinePropertyValueStr(context, global, scope_names[s],
            JS_DupValue(context, scopes[s]), JS_PROP_ENUMERABLE) <= 0) return false;
        /* Typed scopes cannot acquire undeclared slots or mutable prototypes. */
        if (JS_PreventExtensions(context, scopes[s]) < 0) return false;
    }
    return true;
}

static vxml_status result_export(const fia_expression *expression,
    JSContext *context, JSValueConst result, vxml_cmeta_value_kind expected,
    vxml_cmeta_expr_scratch *scratch, vxml_cmeta_value_view *out) {
    vxml_cmeta_value_view value = {0};
    if (expression->kind == VXML_CMETA_CODE_SCRIPT) {
        *out = value; /* Script completion values are not VoiceXML exit values. */
        return VXML_OK;
    }
    if (expression->kind == VXML_CMETA_CODE_CONDITION) {
        const int boolean = JS_ToBool(context, result);
        if (boolean < 0) return VXML_SEMANTIC_ERROR;
        value.kind = VXML_CMETA_VALUE_BOOL;
        value.data.boolean = boolean != 0;
    } else if (JS_IsUndefined(result)) {
        value.kind = VXML_CMETA_VALUE_UNDEFINED;
    } else if (JS_IsBool(result)) {
        value.kind = VXML_CMETA_VALUE_BOOL;
        value.data.boolean = JS_ToBool(context, result) != 0;
    } else if (JS_IsString(result)) {
        size_t size = 0u;
        const char *text = JS_ToCStringLen(context, &size, result);
        if (text == NULL) return VXML_SEMANTIC_ERROR;
        if (size > scratch->capacity || size > expression->limits.values.max_string_bytes) {
            JS_FreeCString(context, text);
            return VXML_LIMIT_EXCEEDED;
        }
        if (size != 0u) memcpy(scratch->bytes, text, size);
        JS_FreeCString(context, text);
        scratch->used = size;
        value.kind = VXML_CMETA_VALUE_STRING;
        value.data.string.data = (const char *)scratch->bytes;
        value.data.string.size = size;
    } else if (JS_IsNumber(result)) {
        double number;
        if (JS_ToFloat64(context, &number, result) != 0 || !isfinite(number))
            return VXML_SEMANTIC_ERROR;
        if (expected == VXML_CMETA_VALUE_FLOAT) {
            value.kind = VXML_CMETA_VALUE_FLOAT;
            value.data.number = number;
        } else if (number >= -9007199254740991.0 && number <= 9007199254740991.0 &&
            number == trunc(number)) {
            if (expected == VXML_CMETA_VALUE_UINT) {
                if (number < 0.0) return VXML_SEMANTIC_ERROR;
                value.kind = VXML_CMETA_VALUE_UINT;
                value.data.uint_value = (uint64_t)number;
            } else {
                value.kind = VXML_CMETA_VALUE_SINT;
                value.data.sint = (int64_t)number;
            }
        } else {
            value.kind = VXML_CMETA_VALUE_FLOAT;
            value.data.number = number;
        }
    } else return VXML_SEMANTIC_ERROR;
    if (expected != VXML_CMETA_VALUE_UNDEFINED && expected != value.kind)
        return VXML_SEMANTIC_ERROR;
    *out = value;
    return VXML_OK;
}

static vxml_status expression_evaluate(const void *opaque, vxml_cmeta_value_kind expected,
    const vxml_cmeta_backend_runtime *runtime, vxml_cmeta_expr_scratch *scratch,
    vxml_cmeta_value_view *out) {
    const fia_expression *expression = opaque;
    fia_binding *bindings = NULL;
    size_t count = 0u;
    quickjs_sandbox_runtime engine = {0};
    quickjs_cmeta_bridge bridge = {0};
    JSContext *context = NULL;
    JSValue global = JS_UNDEFINED, result = JS_UNDEFINED, cells = JS_UNDEFINED;
    JSValue scopes[FIA_NAMED_SCOPES] = {JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED};
    size_t ranks[FIA_NAMED_SCOPES];
    bool owns_deadline = false;
    vxml_cmeta_value_view value = {0};
    vxml_status status;
    if (scratch == NULL || out == NULL ||
        (scratch->capacity != 0u && scratch->bytes == NULL)) return VXML_INVALID_ARGUMENT;
    *out = (vxml_cmeta_value_view){0};
    if (runtime != NULL && runtime->failure_event != NULL)
        *runtime->failure_event = (vxml_cmeta_name_view){0};
    scratch->used = 0u;
    status = bindings_create(expression, runtime, &bindings, &count, ranks);
    if (status != VXML_OK) goto done;
    status = engine_status(quickjs_sandbox_runtime_init(&engine, &expression->limits.engine, NULL, 0u));
    if (status != VXML_OK) goto done;
    context = engine.context;
    if (!quickjs_cmeta_bridge_init(&bridge, &engine, expression->root,
        &expression->limits.values, quickjs_cmeta_default_collection_adapter(), NULL)) {
        status = VXML_INVALID_CONTRACT;
        goto done;
    }
    owns_deadline = quickjs_sandbox_deadline_begin(&engine, expression->limits.engine.max_eval_milliseconds);
    global = JS_GetGlobalObject(context);
    cells = JS_NewObjectProto(context, JS_NULL);
    if (JS_IsException(cells) || !project_bindings(&bridge, global, cells, scopes, ranks, bindings, count))
        goto exception;
    JS_UpdateStackTop((JSRuntime *)engine.runtime);
    if (expression->external) {
        status = external_script_evaluate(expression, runtime, &engine, &owns_deadline, &result);
        if (status != VXML_OK) goto done;
    } else {
        result = JS_Eval(context, expression->source, tstr_len(expression->source),
            "<voicexml-fia-expression>", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT);
    }
    if (JS_IsException(result)) goto exception;
    status = result_export(expression, context, result, expected, scratch, &value);
    if (status != VXML_OK) goto done;
    bridge.properties = count;
    for (size_t i = 0u; i < count; ++i) {
        fia_binding *binding = &bindings[i];
        JSValue exported = JS_GetPropertyUint32(context, cells, (uint32_t)i);
        if (JS_IsException(exported)) goto exception;
        binding->defined = !JS_IsUndefined(exported);
        if (binding->defined) {
            const bool initialized = binding->prototype != NULL
                ? quickjs_cmeta_value_snapshot(&binding->pending, binding->data,
                    binding->prototype, expression->limits.values.max_snapshot_bytes)
                : quickjs_cmeta_value_zero_snapshot(&binding->pending, binding->data,
                    expression->limits.values.max_snapshot_bytes);
            if (!initialized) {
                JS_FreeValue(context, exported);
                status = VXML_ALLOCATION_FAILED;
                goto done;
            }
            const bool converted = quickjs_cmeta_export_value(&bridge, binding->data,
                binding->type, binding->declared_type, exported, binding->pending.value, 1u);
            JS_FreeValue(context, exported);
            if (!converted) {
                status = quickjs_sandbox_deadline_expired(&engine)
                    ? VXML_LIMIT_EXCEEDED : VXML_SEMANTIC_ERROR;
                goto done;
            }
        } else JS_FreeValue(context, exported);
    }
    if (quickjs_sandbox_deadline_expired(&engine)) {
        status = VXML_LIMIT_EXCEEDED;
        goto done;
    }
    /* Every operation below is a previously admitted no-fail move. All reads,
     * allocations, user getters and conversions have already succeeded. */
    for (size_t i = 0u; i < count; ++i) {
        fia_binding *binding = &bindings[i];
        if (!binding->defined) {
            if (binding->scope != NULL) {
                cmeta_scope_view view = *binding->scope;
                cmeta_scope_view_clear_slot(&view, binding->slot);
            } else {
                if (*binding->bound) {
                    quickjs_cmeta_state_scratch old = {.value = binding->destination, .live = true};
                    quickjs_cmeta_state_scratch_destroy(&old, binding->data);
                }
                memset(binding->destination, 0, binding->type->size);
                *binding->bound = 0u;
            }
        } else if (!*binding->bound) {
            (void)quickjs_cmeta_value_move_construct(binding->data, binding->destination, &binding->pending);
            *binding->bound = 1u;
        } else {
            (void)quickjs_cmeta_state_publish(binding->data, binding->destination, &binding->pending);
            *binding->bound = 1u;
        }
    }
    *out = value;
    status = VXML_OK;
    goto done;
exception:
    status = engine_status(quickjs_sandbox_exception(&engine, NULL, 0u));
done:
    for (size_t i = 0u; i < count; ++i)
        quickjs_cmeta_state_scratch_destroy(&bindings[i].pending, bindings[i].data);
    vxml_free(bindings);
    if (context != NULL) {
        JS_FreeValue(context, result);
        JS_FreeValue(context, global);
        JS_FreeValue(context, cells);
        for (unsigned s = 0u; s < FIA_NAMED_SCOPES; ++s) JS_FreeValue(context, scopes[s]);
    }
    if (expression != NULL && runtime != NULL &&
        expression->kind == VXML_CMETA_CODE_SCRIPT && runtime->failure_event != NULL &&
        runtime->failure_event->data == NULL) {
        if (status == VXML_SEMANTIC_ERROR)
            *runtime->failure_event = (vxml_cmeta_name_view){"error.semantic", sizeof("error.semantic") - 1u};
        else if (status == VXML_ALLOCATION_FAILED)
            *runtime->failure_event = (vxml_cmeta_name_view){"error.noresource", sizeof("error.noresource") - 1u};
    }
    quickjs_sandbox_deadline_end(&engine, owns_deadline);
    quickjs_sandbox_runtime_destroy(&engine);
    return status;
}

const vxml_cmeta_expression_backend vxml_quickjs_fia_backend = {
    expression_compile, expression_destroy, expression_scratch_bytes, expression_evaluate,
    external_script_compile, true};

vxml_status vxml_session_init_quickjs_fia(
    vxml_session *session, const vxml_program *program,
    const vxml_cmeta_session_options_v1 *options, const vxml_quickjs_fia_scripts *scripts) {
    fia_session *context = NULL;
    if (session == NULL || program == NULL || program->impl == NULL) return VXML_INVALID_ARGUMENT;
    if (session->impl != NULL) return VXML_INVALID_STATE;
    const vxml_program_impl *impl = program->impl;
    if (impl->profile_kind != VXML_PROFILE_CMETA || impl->profile_data == NULL ||
        ((const vxml_cmeta_program_data *)impl->profile_data)->expression_backend.evaluate != expression_evaluate)
        return VXML_INVALID_CONTRACT;
    if (scripts != NULL) {
        const vxml_script_resource_adapter_v1 *provider = scripts->resources;
        if (provider == NULL || provider->abi_version != VXML_SCRIPT_RESOURCE_ADAPTER_ABI_V1 ||
            provider->struct_size < sizeof(*provider) || provider->open == NULL || provider->close == NULL ||
            scripts->max_uri_bytes == 0u || scripts->max_uri_bytes == SIZE_MAX || scripts->max_source_bytes == 0u)
            return VXML_INVALID_CONTRACT;
        context = vxml_calloc(1u, sizeof(*context));
        if (context == NULL) return VXML_ALLOCATION_FAILED;
        context->resources = *provider;
        context->user = scripts->user;
        context->max_uri_bytes = scripts->max_uri_bytes;
        context->max_source_bytes = scripts->max_source_bytes;
    }
    const vxml_status status = vxml_session_init_cmeta(session, program, options);
    if (status != VXML_OK) { vxml_free(context); return status; }
    vxml_cmeta_session_data *profile = ((vxml_session_impl *)session->impl)->profile_data;
    profile->backend_session = context;
    profile->backend_session_destroy = vxml_free;
    return VXML_OK;
}
