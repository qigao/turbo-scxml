#include "scxml_quickjs.h"
#include "quickjs_cmeta_bridge.h"
#include "scxml_program.h"
#include "scxml_session.h"

#include <cmeta/container.h>
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

static quickjs_sandbox_options quickjs_sandbox_options_from_scxml(
    const scxml_quickjs_compile_options_v1 *options) {
    return (quickjs_sandbox_options){
        .max_source_bytes =
            options != NULL ? options->max_source_bytes : 0u,
        .max_string_bytes =
            options != NULL ? options->max_string_bytes : 0u,
        .max_heap_bytes =
            options != NULL ? options->max_heap_bytes : 0u,
        .max_stack_bytes =
            options != NULL ? options->max_stack_bytes : 0u,
        .max_eval_milliseconds =
            options != NULL ? options->max_eval_milliseconds : 0u};
}

static scxml_quickjs_status quickjs_sandbox_status_to_scxml(
    quickjs_sandbox_status status) {
    switch (status) {
    case QUICKJS_SANDBOX_OK:
        return SCXML_QUICKJS_OK;
    case QUICKJS_SANDBOX_INVALID_ARGUMENT:
        return SCXML_QUICKJS_INVALID_ARGUMENT;
    case QUICKJS_SANDBOX_ALLOCATION_FAILED:
        return SCXML_QUICKJS_ALLOCATION_FAILED;
    case QUICKJS_SANDBOX_LIMIT_EXCEEDED:
        return SCXML_QUICKJS_LIMIT_EXCEEDED;
    case QUICKJS_SANDBOX_EXCEPTION:
        return SCXML_QUICKJS_EXCEPTION;
    default:
        return SCXML_QUICKJS_INVALID_ARGUMENT;
    }
}

static JSValue scxml_quickjs_legacy_import_collection(
    quickjs_cmeta_bridge *bridge,
    const cmeta_data_desc *descriptor,
    const cmeta_type_desc *storage_type,
    const cmeta_declared_type *declared_type,
    const void *object,
    size_t depth,
    void *user);

static bool scxml_quickjs_legacy_export_collection(
    quickjs_cmeta_bridge *bridge,
    const cmeta_data_desc *descriptor,
    const cmeta_type_desc *storage_type,
    const cmeta_declared_type *declared_type,
    JSValueConst value,
    void *object,
    size_t depth,
    void *user);

static const quickjs_cmeta_collection_adapter
scxml_quickjs_legacy_collection_adapter = {
    scxml_quickjs_legacy_collection_schema_supported,
    scxml_quickjs_legacy_import_collection,
    scxml_quickjs_legacy_export_collection};

static bool quickjs_conversion_init(
    quickjs_conversion *conversion,
    scxml_quickjs_runtime *runtime,
    const scxml_quickjs_compile_options_v1 *options,
    const cmeta_data_desc *root) {
    quickjs_cmeta_limits limits;
    if (conversion == NULL || runtime == NULL ||
        options == NULL || root == NULL ||
        runtime->core.context == NULL)
        return false;
    memset(conversion, 0, sizeof(*conversion));
    conversion->context = (JSContext *)runtime->core.context;
    conversion->limits = options;
    conversion->root = root;
    limits = quickjs_cmeta_limits_from_scxml(options);
    return quickjs_cmeta_bridge_init(
        &conversion->bridge, &runtime->core,
        root, &limits,
        &scxml_quickjs_legacy_collection_adapter,
        NULL);
}

static void quickjs_conversion_to_bridge(
    quickjs_conversion *conversion) {
    conversion->bridge.properties = conversion->properties;
}

static void quickjs_conversion_from_bridge(
    quickjs_conversion *conversion) {
    conversion->properties = conversion->bridge.properties;
}

static JSValue quickjs_import_value(
    quickjs_conversion *conversion,
    const cmeta_data_desc *descriptor,
    const void *object, size_t depth) {
    JSValue result;
    if (conversion == NULL || descriptor == NULL)
        return JS_EXCEPTION;
    quickjs_conversion_to_bridge(conversion);
    result = quickjs_cmeta_import_value(
        &conversion->bridge,
        descriptor,
        descriptor->kind == CMETA_DATA_SEQUENCE
            ? NULL : descriptor->storage_type,
        NULL, object, depth);
    quickjs_conversion_from_bridge(conversion);
    return result;
}

static bool quickjs_export_value(
    quickjs_conversion *conversion,
    const cmeta_data_desc *descriptor,
    const cmeta_type_desc *storage_type,
    const cmeta_declared_type *declared_type,
    JSValueConst value, void *object, size_t depth) {
    bool result;
    if (conversion == NULL) return false;
    quickjs_conversion_to_bridge(conversion);
    result = quickjs_cmeta_export_value(
        &conversion->bridge,
        descriptor, storage_type, declared_type,
        value, object, depth);
    quickjs_conversion_from_bridge(conversion);
    return result;
}

static bool quickjs_import_root(
    quickjs_conversion *conversion,
    const cmeta_data_desc *root,
    const void *state) {
    bool result;
    if (conversion == NULL || root != conversion->root)
        return false;
    quickjs_conversion_to_bridge(conversion);
    result = quickjs_cmeta_import_root(
        &conversion->bridge, state);
    quickjs_conversion_from_bridge(conversion);
    return result;
}

static bool quickjs_export_root(
    quickjs_conversion *conversion,
    const cmeta_data_desc *root,
    void *state) {
    bool result;
    if (conversion == NULL || root != conversion->root)
        return false;
    quickjs_conversion_to_bridge(conversion);
    result = quickjs_cmeta_export_root(
        &conversion->bridge, state);
    quickjs_conversion_from_bridge(conversion);
    return result;
}

static JSValue scxml_quickjs_legacy_import_collection(
    quickjs_cmeta_bridge *bridge,
    const cmeta_data_desc *descriptor,
    const cmeta_type_desc *storage_type,
    const cmeta_declared_type *declared_type,
    const void *object,
    size_t depth,
    void *user) {
    const cmeta_data_desc *semantic;
    const cmeta_type_desc *element_type;
    const cmeta_data_desc *element_data;
    cmeta_range range = {0};
    cmeta_range_cursor cursor = {0};
    quickjs_aligned_storage element = {0};
    JSValue result;
    size_t length;
    size_t index;
    bool element_live = false;
    bool managed;
    (void)descriptor;
    (void)storage_type;
    (void)declared_type;
    (void)user;
    if (bridge == NULL || object == NULL)
        return JS_EXCEPTION;
    semantic = cmeta_container_data(object);
    if (semantic == NULL ||
        semantic->kind != CMETA_DATA_SEQUENCE ||
        !cmeta_container_type_application_valid(object) ||
        cmeta_container_type_arity(object) != 1u ||
        !cmeta_container_range_view(
            object, CMETA_CONTAINER_VIEW_DEFAULT, &range) ||
        range.size == NULL ||
        (range.flags &
         (CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED)) !=
            (CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED))
        return JS_EXCEPTION;
    element_type =
        cmeta_container_type_argument(object, 0u);
    element_data = cmeta_scope_find_data_for_type(
        bridge->root, element_type,
        bridge->limits.max_conversion_depth);
    length = cmeta_range_size(&range);
    managed =
        cmeta_container_range_constructs_values(
            element_type);
    if (element_data == NULL ||
        length > UINT32_MAX ||
        length > bridge->limits.max_array_items ||
        (managed &&
         (range.flags &
          CMETA_RANGE_CONSTRUCTS_VALUES) == 0u) ||
        !quickjs_aligned_storage_init(
            &element, element_type))
        return JS_EXCEPTION;
    result = JS_NewArray(bridge->context);
    if (JS_IsException(result)) {
        quickjs_aligned_storage_destroy(
            &element, element_type, false);
        return result;
    }
    for (index = 0u; index < length; ++index) {
        const cmeta_gen_status generated =
            cmeta_range_next(
                &range, &cursor, element.value);
        JSValue value;
        if ((generated != CMETA_GEN_VALUE &&
             generated != CMETA_GEN_VALUE_AND_DONE) ||
            bridge->properties >=
                bridge->limits.max_properties ||
            ++bridge->properties >
                bridge->limits.max_properties) {
            JS_FreeValue(bridge->context, result);
            quickjs_aligned_storage_destroy(
                &element, element_type, element_live);
            return JS_EXCEPTION;
        }
        element_live = managed;
        value = quickjs_cmeta_import_value(
            bridge, element_data, element_type,
            NULL, element.value, depth + 1u);
        if (element_live) {
            element_type->traits->destroy(
                element.value);
            memset(
                element.value, 0,
                element_type->size);
            element_live = false;
        }
        if (JS_IsException(value) ||
            JS_SetPropertyUint32(
                bridge->context, result,
                (uint32_t)index, value) < 0) {
            if (JS_IsException(value))
                JS_FreeValue(
                    bridge->context, value);
            JS_FreeValue(
                bridge->context, result);
            quickjs_aligned_storage_destroy(
                &element, element_type, false);
            return JS_EXCEPTION;
        }
        if (generated ==
                CMETA_GEN_VALUE_AND_DONE &&
            index + 1u != length) {
            JS_FreeValue(
                bridge->context, result);
            quickjs_aligned_storage_destroy(
                &element, element_type, false);
            return JS_EXCEPTION;
        }
    }
    quickjs_aligned_storage_destroy(
        &element, element_type, false);
    return result;
}

static bool scxml_quickjs_legacy_export_collection(
    quickjs_cmeta_bridge *bridge,
    const cmeta_data_desc *descriptor,
    const cmeta_type_desc *storage_type,
    const cmeta_declared_type *declared_type,
    JSValueConst value,
    void *object,
    size_t depth,
    void *user) {
    const cmeta_container_desc *container;
    const cmeta_type_desc *element_type;
    const cmeta_data_desc *element_data;
    quickjs_aligned_storage element = {0};
    cmeta_collector collector = {0};
    JSValue length_value;
    uint32_t length = 0u;
    uint32_t index;
    bool element_live = false;
    bool begun = false;
    bool ok = false;
    (void)descriptor;
    (void)user;
    if (bridge == NULL || object == NULL ||
        !JS_IsArray(value))
        return false;
    container = cmeta_container_descriptor(object);
    if (container == NULL ||
        container->collector == NULL ||
        cmeta_container_data(object) == NULL ||
        cmeta_container_data(object)->kind !=
            CMETA_DATA_SEQUENCE ||
        !cmeta_container_type_application_valid(object) ||
        cmeta_container_type_arity(object) != 1u ||
        !cmeta_declared_type_valid(declared_type) ||
        declared_type->arity != 1u ||
        !cmeta_type_equal(
            declared_type->storage_type,
            storage_type))
        return false;
    element_type =
        cmeta_container_type_argument(object, 0u);
    element_data = cmeta_scope_find_data_for_type(
        bridge->root, element_type,
        bridge->limits.max_conversion_depth);
    if (element_data == NULL ||
        !quickjs_aligned_storage_init(
            &element, element_type))
        goto cleanup;
    length_value = JS_GetPropertyStr(
        bridge->context, value, "length");
    if (JS_IsException(length_value) ||
        JS_ToUint32(
            bridge->context, &length,
            length_value) != 0) {
        if (!JS_IsException(length_value))
            JS_FreeValue(
                bridge->context, length_value);
        goto cleanup;
    }
    JS_FreeValue(
        bridge->context, length_value);
    if (length >
        bridge->limits.max_array_items)
        goto cleanup;
    if (cmeta_container_restore_zero(
            object, declared_type) != CMETA_OK ||
        cmeta_container_bind_types(
            object, declared_type) != CMETA_OK)
        goto cleanup;
    collector = container->collector(
        object, bridge->limits.max_array_items);
    if (cmeta_collector_begin(&collector) != CMETA_OK)
        goto cleanup;
    begun = true;
    for (index = 0u; index < length; ++index) {
        JSValue item;
        if (bridge->properties >=
                bridge->limits.max_properties ||
            ++bridge->properties >
                bridge->limits.max_properties)
            goto cleanup;
        item = JS_GetPropertyUint32(
            bridge->context, value, index);
        if (JS_IsException(item) ||
            !quickjs_cmeta_export_value(
                bridge, element_data,
                element_type, NULL, item,
                element.value, depth + 1u)) {
            if (!JS_IsException(item))
                JS_FreeValue(
                    bridge->context, item);
            goto cleanup;
        }
        JS_FreeValue(
            bridge->context, item);
        element_live =
            cmeta_type_require_traits(
                element_type,
                CMETA_TRAIT_TRIVIAL_DESTROY) !=
            CMETA_OK;
        if (cmeta_collector_accept(
                &collector, element_type,
                element.value) != CMETA_OK)
            goto cleanup;
        if (element_live) {
            element_type->traits->destroy(
                element.value);
            element_live = false;
        }
        memset(
            element.value, 0,
            element_type->size);
    }
    if (cmeta_collector_finish(
            &collector) != CMETA_OK)
        goto cleanup;
    begun = false;
    ok = true;

cleanup:
    if (begun)
        cmeta_collector_abort(&collector);
    quickjs_aligned_storage_destroy(
        &element, element_type,
        element_live);
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
    JSContext *context = (JSContext *)runtime->core.context;
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
        if (text == NULL || size >= runtime->core.result_string_capacity) {
            if (text != NULL) JS_FreeCString(context, text);
            return false;
        }
        memcpy(runtime->core.result_string, text, size);
        runtime->core.result_string[size] = '\0';
        JS_FreeCString(context, text);
        value.data.string.data = runtime->core.result_string;
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
    if (status != SCXML_QUICKJS_OK) {
#if defined(TURBOSCXML_QUICKJS_TRACE_FAILURES)
        (void)fprintf(
            stderr,
            "[scxml-quickjs] expression context recreate status=%d deadline=%llu interrupted=%d diagnostic=%s\n",
            (int)status,
            (unsigned long long)runtime->core.deadline_ms,
            runtime->core.interrupted ? 1 : 0,
            quickjs_diagnostic_text);
#endif
        goto cleanup;
    }
    if (!quickjs_conversion_init(
            &conversion, runtime,
            &program->quickjs_options,
            program->cmeta_root)) {
        quickjs_diagnostic(
            quickjs_diagnostic_text,
            sizeof(quickjs_diagnostic_text),
            "QuickJS CMeta bridge initialization failed");
        goto cleanup;
    }
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
        (JSContext *)runtime->core.context, wrapped, wrapped_size,
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
    if (runtime != NULL && runtime->core.interrupted &&
        expression_status != SCXML_EXPR_OK) {
        expression_status = SCXML_EXPR_LIMIT_EXCEEDED;
        quickjs_diagnostic(
            quickjs_diagnostic_text, sizeof(quickjs_diagnostic_text),
            "QuickJS expression transaction deadline exceeded");
    }
    if (runtime != NULL) quickjs_deadline_end(runtime, owns_deadline);
    if (runtime != NULL && runtime->core.context != NULL) {
        JS_SetContextOpaque((JSContext *)runtime->core.context, NULL);
        if (!JS_IsUndefined(result))
            JS_FreeValue((JSContext *)runtime->core.context, result);
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
#if defined(TURBOSCXML_QUICKJS_TRACE_FAILURES)
            (void)fprintf(
                stderr,
                "[scxml-quickjs] script context recreate status=%d deadline=%llu interrupted=%d diagnostic=%s\n",
                (int)status,
                (unsigned long long)runtime->core.deadline_ms,
                runtime->core.interrupted ? 1 : 0,
                diagnostic);
#endif
            *out_error = "QuickJS working context initialization failed";
            goto cleanup;
        }
        if (!quickjs_conversion_init(
                &conversion, runtime,
                &program->quickjs_options,
                program->cmeta_root)) {
            *out_error =
                "QuickJS CMeta bridge initialization failed";
            goto cleanup;
        }
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
        if (runtime->core.interrupted && *out_error != NULL)
            *out_error = "QuickJS script transaction deadline exceeded";
        quickjs_deadline_end(runtime, owns_deadline);
        if (runtime->core.context != NULL) {
            JS_SetContextOpaque((JSContext *)runtime->core.context, NULL);
            quickjs_context_destroy(runtime);
        }
        scxml_scope_view_clear(&scope_scratch);
        free(scope_scratch_allocation);
        quickjs_aligned_storage_destroy(
            &state_scratch, type, scratch_live);
        if (*out_error != NULL) {
#if defined(TURBOSCXML_QUICKJS_TRACE_FAILURES)
            (void)fprintf(
                stderr, "[scxml-quickjs] %s\n", *out_error);
#endif
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
