#include <ccxml/quickjs.h>

#include "quickjs_sandbox.h"
#include "quickjs_cmeta_bridge.h"
#include <tstr.h>
#include <stdlib.h>
#include <string.h>

typedef struct ccxml_quickjs_impl {
    ccxml_cmeta_datamodel state;
    const cmeta_data_desc *root;
    void *object;
    quickjs_sandbox_runtime runtime;
    quickjs_cmeta_limits conversion;
    size_t expression_count;
    size_t max_expressions;
} ccxml_quickjs_impl;

typedef struct ccxml_quickjs_expression {
    ccxml_quickjs_impl *owner;
    tstr source;
} ccxml_quickjs_expression;

static ccxml_quickjs_impl *implementation(void *user) {
    return user != NULL ? ((ccxml_quickjs_datamodel *)user)->impl : NULL;
}

static scxml_adapter_status map_status(
    quickjs_sandbox_status status, const char **error) {
    if (error != NULL) *error = NULL;
    switch (status) {
    case QUICKJS_SANDBOX_OK: return SCXML_ADAPTER_ACCEPTED;
    case QUICKJS_SANDBOX_ALLOCATION_FAILED:
        if (error != NULL) *error = "CCXML QuickJS allocation failed";
        return SCXML_ADAPTER_FULL;
    case QUICKJS_SANDBOX_TYPE_MISMATCH:
    case QUICKJS_SANDBOX_INVALID_ARGUMENT:
        if (error != NULL) *error = "CCXML QuickJS invalid expression contract";
        return SCXML_ADAPTER_INVALID_CONTRACT;
    default:
        if (error != NULL) *error = "CCXML QuickJS expression failed or exceeded limits";
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
}

static scxml_adapter_status compile_expression(
    void *user, const char *source, size_t size, void **out, const char **error) {
    ccxml_quickjs_impl *impl = implementation(user);
    ccxml_quickjs_expression *expression;
    quickjs_sandbox_status status;
    if (error != NULL) *error = NULL;
    if (impl == NULL || out == NULL || *out != NULL || source == NULL ||
        size == 0u || size > impl->runtime.options.max_source_bytes ||
        memchr(source, '\0', size) != NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    if (impl->expression_count >= impl->max_expressions)
        return SCXML_ADAPTER_FULL;
    status = quickjs_sandbox_validate_expression(&impl->runtime.options,
        source, size, "<ccxml-expression>", NULL, 0u);
    if (status != QUICKJS_SANDBOX_OK) return map_status(status, error);
    expression = calloc(1u, sizeof(*expression));
    if (expression == NULL) return SCXML_ADAPTER_FULL;
    expression->source = tstr_new_len(source, size);
    if (expression->source == NULL) {
        free(expression);
        return SCXML_ADAPTER_FULL;
    }
    expression->owner = impl;
    ++impl->expression_count;
    *out = expression;
    return SCXML_ADAPTER_ACCEPTED;
}

static void destroy_expression(void *user, void **handle) {
    ccxml_quickjs_impl *impl = implementation(user);
    ccxml_quickjs_expression *expression;
    if (impl == NULL || handle == NULL || *handle == NULL) return;
    expression = *handle;
    if (expression->owner != impl) return;
    tstr_free(expression->source);
    free(expression);
    --impl->expression_count;
    *handle = NULL;
}

static bool event_view_valid(const char *data, size_t size, size_t limit) {
    return size <= limit && (size == 0u ||
        (data != NULL && memchr(data, '\0', size) == NULL));
}

static bool set_event_field(JSContext *context, JSValueConst object,
    const char *key, const char *data, size_t size) {
    JSValue value = data != NULL && size != 0u
        ? JS_NewStringLen(context, data, size) : JS_UNDEFINED;
    if (JS_IsException(value)) return false;
    return JS_SetPropertyStr(context, object, key, value) >= 0;
}

static quickjs_sandbox_status prepare_context(
    ccxml_quickjs_impl *impl, const ccxml_event *event) {
    quickjs_cmeta_bridge bridge = {0};
    quickjs_sandbox_status status = quickjs_sandbox_context_recreate(
        &impl->runtime, NULL, 0u);
    JSContext *context;
    JSValue global, current;
    bool ok;
    if (status != QUICKJS_SANDBOX_OK) return status;
    if (!quickjs_cmeta_bridge_init(&bridge, &impl->runtime, impl->root,
        &impl->conversion, NULL, NULL)) return QUICKJS_SANDBOX_INVALID_ARGUMENT;
    /* Reserve the event$ binding and its three fields in the common budget. */
    bridge.properties = 4u;
    if (!quickjs_cmeta_import_root(&bridge, impl->object))
        return quickjs_sandbox_exception(&impl->runtime, NULL, 0u);
    context = impl->runtime.context;
    global = JS_GetGlobalObject(context);
    current = JS_NewObjectProto(context, JS_NULL);
    ok = !JS_IsException(global) && !JS_IsException(current) &&
        set_event_field(context, current, "name", event->name, event->name_size) &&
        set_event_field(context, current, "connectionid", event->connection_id,
            event->connection_id_size) &&
        set_event_field(context, current, "conferenceid", event->conference_id,
            event->conference_id_size);
    if (ok) {
        ok = JS_DefinePropertyValueStr(context, global, "event$", current,
            JS_PROP_ENUMERABLE) >= 0;
    } else {
        JS_FreeValue(context, current);
    }
    JS_FreeValue(context, global);
    return ok ? QUICKJS_SANDBOX_OK
        : quickjs_sandbox_exception(&impl->runtime, NULL, 0u);
}

static scxml_adapter_status evaluate_expression(
    void *user, const void *handle, const ccxml_event *event,
    ccxml_string_view *out_string, bool *out_bool, const char **error) {
    ccxml_quickjs_impl *impl = implementation(user);
    const ccxml_quickjs_expression *expression = handle;
    quickjs_sandbox_status status;
    bool owns_deadline;
    if (out_string != NULL) *out_string = (ccxml_string_view){0};
    if (out_bool != NULL) *out_bool = false;
    if (error != NULL) *error = NULL;
    if (impl == NULL || expression == NULL || expression->owner != impl ||
        event == NULL || (out_string == NULL && out_bool == NULL) ||
        !event_view_valid(event->name, event->name_size, impl->conversion.max_string_bytes) ||
        !event_view_valid(event->connection_id, event->connection_id_size,
            impl->conversion.max_string_bytes) ||
        !event_view_valid(event->conference_id, event->conference_id_size,
            impl->conversion.max_string_bytes)) return SCXML_ADAPTER_INVALID_CONTRACT;
    owns_deadline = quickjs_sandbox_deadline_begin(&impl->runtime,
        impl->runtime.options.max_eval_milliseconds);
    status = prepare_context(impl, event);
    if (status == QUICKJS_SANDBOX_OK && quickjs_sandbox_deadline_expired(&impl->runtime))
        status = QUICKJS_SANDBOX_LIMIT_EXCEEDED;
    if (status == QUICKJS_SANDBOX_OK) {
        if (out_bool != NULL) {
            status = quickjs_sandbox_eval_expression_boolean(&impl->runtime,
                expression->source, tstr_len(expression->source), "<ccxml-condition>",
                impl->runtime.options.max_eval_milliseconds, out_bool, NULL, 0u);
        } else {
            status = quickjs_sandbox_eval_expression_string(&impl->runtime,
                expression->source, tstr_len(expression->source), "<ccxml-string>",
                impl->runtime.options.max_eval_milliseconds,
                &out_string->data, &out_string->size, NULL, 0u);
            if (status == QUICKJS_SANDBOX_OK &&
                (out_string->size == 0u || memchr(out_string->data, '\0', out_string->size)))
                status = QUICKJS_SANDBOX_TYPE_MISMATCH;
        }
    }
    /* Release the context reference; result bytes live in separate native scratch. */
    quickjs_sandbox_context_destroy(&impl->runtime);
    quickjs_sandbox_deadline_end(&impl->runtime, owns_deadline);
    if (status != QUICKJS_SANDBOX_OK) {
        if (out_string != NULL) *out_string = (ccxml_string_view){0};
        if (out_bool != NULL) *out_bool = false;
    }
    return map_status(status, error);
}

static scxml_adapter_status compile_condition(void *user, const char *source,
    size_t size, ccxml_condition *out, const char **error) {
    return out != NULL ? compile_expression(user, source, size, &out->impl, error)
        : SCXML_ADAPTER_INVALID_CONTRACT;
}
static scxml_adapter_status evaluate_condition(void *user, const ccxml_condition *condition,
    const ccxml_event *event, bool *out, const char **error) {
    return condition != NULL && out != NULL
        ? evaluate_expression(user, condition->impl, event, NULL, out, error)
        : SCXML_ADAPTER_INVALID_CONTRACT;
}
static void destroy_condition(void *user, ccxml_condition *condition) {
    if (condition != NULL) destroy_expression(user, &condition->impl);
}
static scxml_adapter_status compile_string(void *user, const char *source,
    size_t size, ccxml_string_expression *out, const char **error) {
    return out != NULL ? compile_expression(user, source, size, &out->impl, error)
        : SCXML_ADAPTER_INVALID_CONTRACT;
}
static scxml_adapter_status evaluate_string(void *user, const ccxml_string_expression *expression,
    const ccxml_event *event, ccxml_string_view *out, const char **error) {
    return expression != NULL && out != NULL
        ? evaluate_expression(user, expression->impl, event, out, NULL, error)
        : SCXML_ADAPTER_INVALID_CONTRACT;
}
static void destroy_string(void *user, ccxml_string_expression *expression) {
    if (expression != NULL) destroy_expression(user, &expression->impl);
}

static scxml_adapter_status validate_string_location(void *user, const char *location,
    size_t size, const char **error) {
    ccxml_quickjs_impl *impl = implementation(user);
    if (impl == NULL) return SCXML_ADAPTER_INVALID_CONTRACT;
    return ccxml_cmeta_datamodel_adapter()->validate_string_location(
        &impl->state, location, size, error);
}

static scxml_adapter_status validate_readable_string_location(void *user, const char *location,
    size_t size, const char **error) {
    ccxml_quickjs_impl *impl = implementation(user);
    if (impl == NULL) return SCXML_ADAPTER_INVALID_CONTRACT;
    return ccxml_cmeta_datamodel_adapter()->validate_readable_string_location(
        &impl->state, location, size, error);
}

static scxml_adapter_status validate_payload_location(void *user, const char *location,
    size_t size, const char **error) {
    ccxml_quickjs_impl *impl = implementation(user);
    if (impl == NULL) return SCXML_ADAPTER_INVALID_CONTRACT;
    return ccxml_cmeta_datamodel_adapter()->validate_payload_location(
        &impl->state, location, size, error);
}

static scxml_adapter_status read_string(void *user, const char *location,
    size_t size, ccxml_string_view *out, const char **error) {
    ccxml_quickjs_impl *impl = implementation(user);
    if (impl == NULL) return SCXML_ADAPTER_INVALID_CONTRACT;
    return ccxml_cmeta_datamodel_adapter()->read_string(
        &impl->state, location, size, out, error);
}

static scxml_adapter_status read_payload(void *user, const char *location,
    size_t size, scxml_content_view *out, const char **error) {
    ccxml_quickjs_impl *impl = implementation(user);
    if (impl == NULL) return SCXML_ADAPTER_INVALID_CONTRACT;
    return ccxml_cmeta_datamodel_adapter()->read_payload(
        &impl->state, location, size, out, error);
}

static scxml_adapter_status prepare_assign_string(void *user, const char *location,
    size_t size, const char *value, size_t value_size, cflow_statechart_effect_ticket *out, const char **error) {
    ccxml_quickjs_impl *impl = implementation(user);
    if (impl == NULL) return SCXML_ADAPTER_INVALID_CONTRACT;
    return ccxml_cmeta_datamodel_adapter()->prepare_assign_string(
        &impl->state, location, size, value, value_size, out, error);
}

static const ccxml_datamodel_adapter_v1 adapter = {
    .abi_version = CCXML_DATAMODEL_ADAPTER_ABI_V1,
    .struct_size = sizeof(ccxml_datamodel_adapter_v1),
    .validate_string_location = validate_string_location,
    .prepare_assign_string = prepare_assign_string,
    .validate_readable_string_location = validate_readable_string_location,
    .read_string = read_string,
    .compile_condition = compile_condition,
    .evaluate_condition = evaluate_condition,
    .destroy_condition = destroy_condition,
    .validate_payload_location = validate_payload_location,
    .read_payload = read_payload,
    .compile_string_expression = compile_string,
    .evaluate_string_expression = evaluate_string,
    .destroy_string_expression = destroy_string};

const ccxml_datamodel_adapter_v1 *ccxml_quickjs_datamodel_adapter(void) {
    return &adapter;
}

ccxml_quickjs_datamodel_config_v1 ccxml_quickjs_default_datamodel_config(void) {
    return (ccxml_quickjs_datamodel_config_v1){
        .abi_version = CCXML_QUICKJS_DATAMODEL_CONFIG_ABI_V1,
        .struct_size = sizeof(ccxml_quickjs_datamodel_config_v1),
        .state_config = {
            .abi_version = CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1,
            .struct_size = sizeof(ccxml_cmeta_datamodel_config_v1),
            .max_path_depth = 16u, .max_string_bytes = 4096u},
        .max_expression_bytes = 4096u, .max_expressions = 1024u,
        .max_heap_bytes = 4u * 1024u * 1024u,
        .max_stack_bytes = 256u * 1024u, .max_eval_milliseconds = 50u,
        .max_conversion_depth = 16u, .max_properties = 1024u,
        .max_array_items = 1024u, .max_snapshot_bytes = 1024u * 1024u};
}

ccxml_status ccxml_quickjs_datamodel_init(ccxml_quickjs_datamodel *datamodel,
    const ccxml_quickjs_datamodel_config_v1 *config) {
    ccxml_quickjs_impl *impl;
    ccxml_status status;
    quickjs_sandbox_status sandbox_status;
    quickjs_sandbox_options limits;
    quickjs_cmeta_limits conversion;
    const cmeta_data_struct_shape *shape;
    if (datamodel == NULL || datamodel->impl != NULL || config == NULL ||
        config->abi_version != CCXML_QUICKJS_DATAMODEL_CONFIG_ABI_V1 ||
        config->struct_size < sizeof(*config) || config->max_expressions == 0u)
        return CCXML_INVALID_ARGUMENT;
    limits = (quickjs_sandbox_options){
        .max_source_bytes = config->max_expression_bytes,
        .max_string_bytes = config->state_config.max_string_bytes,
        .max_heap_bytes = config->max_heap_bytes,
        .max_stack_bytes = config->max_stack_bytes,
        .max_eval_milliseconds = config->max_eval_milliseconds};
    conversion = (quickjs_cmeta_limits){
        .max_conversion_depth = config->max_conversion_depth,
        .max_properties = config->max_properties,
        .max_array_items = config->max_array_items,
        .max_snapshot_bytes = config->max_snapshot_bytes,
        .max_string_bytes = config->state_config.max_string_bytes};
    if (!quickjs_sandbox_limits_valid(&limits) ||
        !quickjs_cmeta_schema_supported(config->state_config.root,
            &conversion, NULL, NULL, 4u)) return CCXML_INVALID_ARGUMENT;
    if (config->state_config.root->storage_type->size > config->max_snapshot_bytes)
        return CCXML_LIMIT_EXCEEDED;
    shape = config->state_config.root->shape;
    for (size_t index = 0u; index < shape->field_count; ++index)
        if (strcmp(shape->fields[index].name, "event$") == 0)
            return CCXML_INVALID_ARGUMENT;
    impl = calloc(1u, sizeof(*impl));
    if (impl == NULL) return CCXML_ALLOCATION_FAILED;
    status = ccxml_cmeta_datamodel_init(&impl->state, &config->state_config);
    if (status != CCXML_OK) { free(impl); return status; }
    impl->root = config->state_config.root;
    impl->object = config->state_config.state;
    impl->conversion = conversion;
    impl->max_expressions = config->max_expressions;
    sandbox_status = quickjs_sandbox_runtime_init(&impl->runtime, &limits, NULL, 0u);
    if (sandbox_status != QUICKJS_SANDBOX_OK) {
        ccxml_cmeta_datamodel_destroy(&impl->state);
        free(impl);
        return sandbox_status == QUICKJS_SANDBOX_ALLOCATION_FAILED
            ? CCXML_ALLOCATION_FAILED : sandbox_status == QUICKJS_SANDBOX_LIMIT_EXCEEDED
            ? CCXML_LIMIT_EXCEEDED : CCXML_ADAPTER_ERROR;
    }
    datamodel->impl = impl;
    return CCXML_OK;
}

void ccxml_quickjs_datamodel_destroy(ccxml_quickjs_datamodel *datamodel) {
    ccxml_quickjs_impl *impl = implementation(datamodel);
    if (impl == NULL) return;
    quickjs_sandbox_runtime_destroy(&impl->runtime);
    ccxml_cmeta_datamodel_destroy(&impl->state);
    free(impl);
    datamodel->impl = NULL;
}
