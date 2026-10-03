#include "quickjs_cmeta_bridge.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const int64_t quickjs_cmeta_max_safe_integer =
    INT64_C(9007199254740991);
static const int64_t quickjs_cmeta_min_safe_integer =
    -INT64_C(9007199254740991);

typedef struct quickjs_cmeta_aligned_storage {
    void *allocation;
    void *value;
} quickjs_cmeta_aligned_storage;

static bool quickjs_cmeta_aligned_storage_init(
    quickjs_cmeta_aligned_storage *storage,
    const cmeta_type_desc *type) {
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

static void quickjs_cmeta_aligned_storage_destroy(
    quickjs_cmeta_aligned_storage *storage,
    const cmeta_data_desc *data,
    bool live) {
    if (storage == NULL) return;
    if (live && storage->value != NULL && data != NULL)
        (void)cmeta_data_value_restore_zero(data, storage->value);
    free(storage->allocation);
    memset(storage, 0, sizeof(*storage));
}

bool quickjs_cmeta_limits_valid(
    const quickjs_cmeta_limits *limits) {
    return limits != NULL &&
        limits->max_conversion_depth != 0u &&
        limits->max_properties != 0u &&
        limits->max_array_items != 0u &&
        limits->max_snapshot_bytes != 0u &&
        limits->max_string_bytes != 0u;
}

static bool quickjs_cmeta_property_budget(
    quickjs_cmeta_bridge *bridge) {
    return bridge != NULL &&
        bridge->properties < bridge->limits.max_properties &&
        ++bridge->properties <= bridge->limits.max_properties;
}

static bool quickjs_cmeta_schema_supported_impl(
    const cmeta_data_desc *root,
    const cmeta_data_desc *descriptor,
    const cmeta_declared_type *declared_type,
    size_t depth,
    const quickjs_cmeta_limits *limits,
    const quickjs_cmeta_collection_adapter *collections,
    void *collection_user,
    bool inside_collection,
    size_t *properties) {
    if (!cmeta_data_desc_valid(descriptor) ||
        depth > limits->max_conversion_depth ||
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
        return cmeta_data_enum_ops_of(descriptor) != NULL ||
            descriptor->enum_bits_ops != NULL;
    case CMETA_DATA_STRUCT: {
        const cmeta_data_struct_shape *shape =
            (const cmeta_data_struct_shape *)descriptor->shape;
        size_t index;
        if (shape == NULL || shape->layout == NULL ||
            shape->fields == NULL ||
            shape->field_count != shape->layout->field_count ||
            shape->field_count > limits->max_properties)
            return false;
        for (index = 0u; index < shape->field_count; ++index) {
            const cmeta_data_field_desc *field = &shape->fields[index];
            const cmeta_field_desc *layout_field =
                cmeta_struct_field(shape->layout, index);
            if (field->name == NULL || field->value == NULL ||
                layout_field == NULL ||
                layout_field->offset != field->offset ||
                (!inside_collection &&
                 (*properties >= limits->max_properties ||
                  ++*properties > limits->max_properties)) ||
                (inside_collection &&
                 field->value->kind == CMETA_DATA_SEQUENCE) ||
                !quickjs_cmeta_schema_supported_impl(
                    root, field->value,
                    layout_field->declared_type,
                    depth + 1u, limits,
                    collections, collection_user,
                    inside_collection, properties))
                return false;
        }
        return true;
    }
    case CMETA_DATA_SEQUENCE:
        return collections != NULL &&
            collections->schema_supported != NULL &&
            collections->schema_supported(
                root, descriptor, declared_type,
                depth, limits, properties,
                collection_user);
    default:
        return false;
    }
}

bool quickjs_cmeta_schema_supported(
    const cmeta_data_desc *root,
    const quickjs_cmeta_limits *limits,
    const quickjs_cmeta_collection_adapter *collections,
    void *collection_user,
    size_t supplemental_properties) {
    size_t properties = supplemental_properties;
    if (!quickjs_cmeta_limits_valid(limits) ||
        !cmeta_data_desc_valid(root) ||
        root->kind != CMETA_DATA_STRUCT ||
        root->storage_type == NULL ||
        supplemental_properties > limits->max_properties)
        return false;
    if (collections == NULL)
        collections = quickjs_cmeta_default_collection_adapter();
    return quickjs_cmeta_schema_supported_impl(
        root, root, NULL, 0u, limits,
        collections, collection_user,
        false, &properties);
}

bool quickjs_cmeta_bridge_init(
    quickjs_cmeta_bridge *bridge,
    quickjs_sandbox_runtime *runtime,
    const cmeta_data_desc *root,
    const quickjs_cmeta_limits *limits,
    const quickjs_cmeta_collection_adapter *collections,
    void *collection_user) {
    if (bridge == NULL || runtime == NULL ||
        runtime->context == NULL ||
        !quickjs_cmeta_schema_supported(
            root, limits, collections, collection_user, 0u))
        return false;
    memset(bridge, 0, sizeof(*bridge));
#if TURBOSCXML_HAS_QUICKJS
    bridge->context = (JSContext *)runtime->context;
#endif
    bridge->limits = *limits;
    bridge->root = root;
    bridge->collections = collections != NULL
        ? collections : quickjs_cmeta_default_collection_adapter();
    bridge->collection_user = collection_user;
    return true;
}

#if TURBOSCXML_HAS_QUICKJS

static bool quickjs_cmeta_read_signed(
    const cmeta_data_desc *descriptor,
    const void *object,
    int64_t *out) {
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

static bool quickjs_cmeta_read_unsigned(
    const cmeta_data_desc *descriptor,
    const void *object,
    uint64_t *out) {
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

static bool quickjs_cmeta_write_signed(
    const cmeta_data_desc *descriptor,
    void *object,
    int64_t value) {
    const size_t size = descriptor->storage_type->size;
    const cmeta_data_integer_shape *shape =
        (const cmeta_data_integer_shape *)descriptor->shape;
    const uint8_t bits =
        shape != NULL ? shape->bits : (uint8_t)(size * 8u);
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

static bool quickjs_cmeta_write_unsigned(
    const cmeta_data_desc *descriptor,
    void *object,
    uint64_t value) {
    const size_t size = descriptor->storage_type->size;
    const cmeta_data_integer_shape *shape =
        (const cmeta_data_integer_shape *)descriptor->shape;
    const uint8_t bits =
        shape != NULL ? shape->bits : (uint8_t)(size * 8u);
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

JSValue quickjs_cmeta_import_value(
    quickjs_cmeta_bridge *bridge,
    const cmeta_data_desc *descriptor,
    const cmeta_type_desc *storage_type,
    const cmeta_declared_type *declared_type,
    const void *object,
    size_t depth) {
    JSContext *context;
    if (bridge == NULL || descriptor == NULL ||
        object == NULL || bridge->context == NULL ||
        depth > bridge->limits.max_conversion_depth)
        return JS_EXCEPTION;
    context = bridge->context;
    if (descriptor->kind != CMETA_DATA_SEQUENCE &&
        (storage_type == NULL ||
         descriptor->storage_type == NULL ||
         !cmeta_type_equal(descriptor->storage_type, storage_type)))
        return JS_EXCEPTION;
    switch (descriptor->kind) {
    case CMETA_DATA_BOOL: {
        bool value;
        memcpy(&value, object, sizeof(value));
        return JS_NewBool(context, value);
    }
    case CMETA_DATA_SINT: {
        int64_t value;
        if (!quickjs_cmeta_read_signed(descriptor, object, &value) ||
            value < quickjs_cmeta_min_safe_integer ||
            value > quickjs_cmeta_max_safe_integer)
            return JS_EXCEPTION;
        return JS_NewFloat64(context, (double)value);
    }
    case CMETA_DATA_UINT: {
        uint64_t value;
        if (!quickjs_cmeta_read_unsigned(descriptor, object, &value) ||
            value > UINT64_C(9007199254740991))
            return JS_EXCEPTION;
        return JS_NewFloat64(context, (double)value);
    }
    case CMETA_DATA_ENUM: {
        int64_t value;
        if (cmeta_data_enum_read(descriptor, object, &value) != CMETA_OK ||
            value < quickjs_cmeta_min_safe_integer ||
            value > quickjs_cmeta_max_safe_integer)
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
                bridge->limits.max_string_bytes,
                &data, &size) != CMETA_OK)
            return JS_EXCEPTION;
        return JS_NewStringLen(
            context, (const char *)data, size);
    }
    case CMETA_DATA_STRUCT: {
        const cmeta_data_struct_shape *shape =
            (const cmeta_data_struct_shape *)descriptor->shape;
        JSValue result;
        size_t index;
        if (shape == NULL || shape->layout == NULL ||
            shape->fields == NULL ||
            shape->field_count != shape->layout->field_count)
            return JS_EXCEPTION;
        result = JS_NewObject(context);
        if (JS_IsException(result)) return result;
        for (index = 0u; index < shape->field_count; ++index) {
            const cmeta_data_field_desc *field =
                &shape->fields[index];
            const cmeta_field_desc *layout_field =
                cmeta_struct_field(shape->layout, index);
            JSValue value;
            if (field->name == NULL || field->value == NULL ||
                layout_field == NULL ||
                layout_field->offset != field->offset ||
                !quickjs_cmeta_property_budget(bridge)) {
                JS_FreeValue(context, result);
                return JS_EXCEPTION;
            }
            value = quickjs_cmeta_import_value(
                bridge, field->value,
                layout_field->type,
                layout_field->declared_type,
                (const unsigned char *)object + field->offset,
                depth + 1u);
            if (JS_IsException(value) ||
                JS_SetPropertyStr(
                    context, result, field->name, value) < 0) {
                if (JS_IsException(value))
                    JS_FreeValue(context, value);
                JS_FreeValue(context, result);
                return JS_EXCEPTION;
            }
        }
        return result;
    }
    case CMETA_DATA_SEQUENCE:
        if (bridge->collections == NULL ||
            bridge->collections->import_collection == NULL)
            return JS_EXCEPTION;
        return bridge->collections->import_collection(
            bridge, descriptor, storage_type,
            declared_type, object, depth,
            bridge->collection_user);
    default:
        return JS_EXCEPTION;
    }
}

bool quickjs_cmeta_export_value(
    quickjs_cmeta_bridge *bridge,
    const cmeta_data_desc *descriptor,
    const cmeta_type_desc *storage_type,
    const cmeta_declared_type *declared_type,
    JSValueConst value,
    void *object,
    size_t depth) {
    JSContext *context;
    if (bridge == NULL || descriptor == NULL ||
        object == NULL || bridge->context == NULL ||
        !cmeta_type_desc_valid(storage_type) ||
        (descriptor->kind != CMETA_DATA_SEQUENCE &&
         (descriptor->storage_type == NULL ||
          !cmeta_type_equal(
              descriptor->storage_type, storage_type))) ||
        depth > bridge->limits.max_conversion_depth)
        return false;
    context = bridge->context;
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
            number < (double)quickjs_cmeta_min_safe_integer ||
            number > (double)quickjs_cmeta_max_safe_integer ||
            number != (double)(native = (int64_t)number))
            return false;
        return quickjs_cmeta_write_signed(
            descriptor, object, native);
    }
    case CMETA_DATA_UINT: {
        double number;
        uint64_t native;
        if (!JS_IsNumber(value) ||
            JS_ToFloat64(context, &number, value) != 0 ||
            !isfinite(number) || number < 0.0 ||
            number > (double)quickjs_cmeta_max_safe_integer ||
            number != (double)(native = (uint64_t)number))
            return false;
        return quickjs_cmeta_write_unsigned(
            descriptor, object, native);
    }
    case CMETA_DATA_ENUM: {
        double number;
        int64_t native;
        if (!JS_IsNumber(value) ||
            JS_ToFloat64(context, &number, value) != 0 ||
            !isfinite(number) ||
            number < (double)quickjs_cmeta_min_safe_integer ||
            number > (double)quickjs_cmeta_max_safe_integer ||
            number != (double)(native = (int64_t)number) ||
            cmeta_data_enum_restore_zero(
                descriptor, object) != CMETA_OK)
            return false;
        return cmeta_data_enum_assign(
            descriptor, object, native) == CMETA_OK;
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
            descriptor, object,
            (const unsigned char *)text, size,
            bridge->limits.max_string_bytes);
        JS_FreeCString(context, text);
        return status == CMETA_OK;
    }
    case CMETA_DATA_STRUCT: {
        const cmeta_data_struct_shape *shape =
            (const cmeta_data_struct_shape *)descriptor->shape;
        size_t index;
        if (!JS_IsObject(value) || shape == NULL ||
            shape->layout == NULL || shape->fields == NULL ||
            shape->field_count != shape->layout->field_count)
            return false;
        for (index = 0u; index < shape->field_count; ++index) {
            const cmeta_data_field_desc *field =
                &shape->fields[index];
            const cmeta_field_desc *layout_field =
                cmeta_struct_field(shape->layout, index);
            JSValue property;
            bool ok;
            if (field->name == NULL || field->value == NULL ||
                layout_field == NULL ||
                layout_field->offset != field->offset ||
                !quickjs_cmeta_property_budget(bridge))
                return false;
            property = JS_GetPropertyStr(
                context, value, field->name);
            if (JS_IsException(property)) return false;
            ok = quickjs_cmeta_export_value(
                bridge, field->value,
                layout_field->type,
                layout_field->declared_type,
                property,
                (unsigned char *)object + field->offset,
                depth + 1u);
            JS_FreeValue(context, property);
            if (!ok) return false;
        }
        return true;
    }
    case CMETA_DATA_SEQUENCE:
        if (bridge->collections == NULL ||
            bridge->collections->export_collection == NULL)
            return false;
        return bridge->collections->export_collection(
            bridge, descriptor, storage_type,
            declared_type, value, object, depth,
            bridge->collection_user);
    default:
        return false;
    }
}

bool quickjs_cmeta_import_root(
    quickjs_cmeta_bridge *bridge,
    const void *state) {
    const cmeta_data_struct_shape *shape;
    JSValue global;
    size_t index;
    bool ok;
    if (bridge == NULL || state == NULL ||
        bridge->root == NULL ||
        bridge->root->kind != CMETA_DATA_STRUCT)
        return false;
    shape =
        (const cmeta_data_struct_shape *)bridge->root->shape;
    if (shape == NULL || shape->layout == NULL ||
        shape->fields == NULL ||
        shape->field_count != shape->layout->field_count)
        return false;
    global = JS_GetGlobalObject(bridge->context);
    ok = !JS_IsException(global);
    for (index = 0u; ok && index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field =
            &shape->fields[index];
        const cmeta_field_desc *layout_field =
            cmeta_struct_field(shape->layout, index);
        JSValue value;
        if (field->name == NULL || field->value == NULL ||
            layout_field == NULL ||
            layout_field->offset != field->offset ||
            !quickjs_cmeta_property_budget(bridge)) {
            ok = false;
            break;
        }
        value = quickjs_cmeta_import_value(
            bridge, field->value,
            layout_field->type,
            layout_field->declared_type,
            (const unsigned char *)state + field->offset,
            1u);
        if (JS_IsException(value) ||
            JS_SetPropertyStr(
                bridge->context, global,
                field->name, value) < 0) {
            if (JS_IsException(value))
                JS_FreeValue(bridge->context, value);
            ok = false;
        }
    }
    JS_FreeValue(bridge->context, global);
    return ok;
}

bool quickjs_cmeta_export_root(
    quickjs_cmeta_bridge *bridge,
    void *state) {
    const cmeta_data_struct_shape *shape;
    JSValue global;
    size_t index;
    bool ok;
    if (bridge == NULL || state == NULL ||
        bridge->root == NULL ||
        bridge->root->kind != CMETA_DATA_STRUCT)
        return false;
    shape =
        (const cmeta_data_struct_shape *)bridge->root->shape;
    if (shape == NULL || shape->layout == NULL ||
        shape->fields == NULL ||
        shape->field_count != shape->layout->field_count)
        return false;
    global = JS_GetGlobalObject(bridge->context);
    ok = !JS_IsException(global);
    for (index = 0u; ok && index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field =
            &shape->fields[index];
        const cmeta_field_desc *layout_field =
            cmeta_struct_field(shape->layout, index);
        JSValue value;
        if (field->name == NULL || field->value == NULL ||
            layout_field == NULL ||
            layout_field->offset != field->offset ||
            !quickjs_cmeta_property_budget(bridge)) {
            ok = false;
            break;
        }
        value = JS_GetPropertyStr(
            bridge->context, global, field->name);
        if (JS_IsException(value)) {
            ok = false;
            break;
        }
        ok = quickjs_cmeta_export_value(
            bridge, field->value,
            layout_field->type,
            layout_field->declared_type,
            value,
            (unsigned char *)state + field->offset,
            1u);
        JS_FreeValue(bridge->context, value);
    }
    JS_FreeValue(bridge->context, global);
    return ok;
}

static bool quickjs_cmeta_default_schema_supported(
    const cmeta_data_desc *root,
    const cmeta_data_desc *descriptor,
    const cmeta_declared_type *declared_type,
    size_t depth,
    const quickjs_cmeta_limits *limits,
    size_t *properties,
    void *user) {
    const cmeta_data_desc *element;
    (void)root;
    (void)declared_type;
    (void)user;
    if (descriptor == NULL ||
        cmeta_data_collection_ops_of(descriptor) == NULL ||
        descriptor->storage_type == NULL)
        return false;
    element = cmeta_data_collection_element_data(descriptor);
    return element != NULL &&
        element->kind != CMETA_DATA_SEQUENCE &&
        quickjs_cmeta_schema_supported_impl(
            root, element, NULL,
            depth + 1u, limits,
            quickjs_cmeta_default_collection_adapter(),
            NULL, true, properties);
}

static JSValue quickjs_cmeta_default_import_collection(
    quickjs_cmeta_bridge *bridge,
    const cmeta_data_desc *descriptor,
    const cmeta_type_desc *storage_type,
    const cmeta_declared_type *declared_type,
    const void *object,
    size_t depth,
    void *user) {
    cmeta_data_collection_borrow_cursor cursor = {0};
    const cmeta_data_desc *element;
    JSValue result;
    size_t length = 0u;
    size_t index = 0u;
    cmeta_status status;
    (void)storage_type;
    (void)declared_type;
    (void)user;
    if (bridge == NULL || descriptor == NULL ||
        object == NULL ||
        cmeta_data_collection_ops_of(descriptor) == NULL)
        return JS_EXCEPTION;
    element = cmeta_data_collection_element_data(descriptor);
    if (element == NULL || element->storage_type == NULL)
        return JS_EXCEPTION;
    status = cmeta_data_collection_borrow_begin(
        descriptor, object, &cursor);
    if (status != CMETA_OK ||
        cmeta_data_collection_borrow_size(
            &cursor, &length) != CMETA_OK ||
        length > bridge->limits.max_array_items)
        return JS_EXCEPTION;
    result = JS_NewArray(bridge->context);
    if (JS_IsException(result)) return result;
    for (;;) {
        const void *value = NULL;
        cmeta_gen_status generated =
            cmeta_data_collection_borrow_next(
                &cursor, &value);
        JSValue js_value;
        if (generated == CMETA_GEN_DONE) break;
        if ((generated != CMETA_GEN_VALUE &&
             generated != CMETA_GEN_VALUE_AND_DONE) ||
            value == NULL || index >= length ||
            !quickjs_cmeta_property_budget(bridge)) {
            JS_FreeValue(bridge->context, result);
            return JS_EXCEPTION;
        }
        js_value = quickjs_cmeta_import_value(
            bridge, element, element->storage_type,
            NULL, value, depth + 1u);
        if (JS_IsException(js_value) ||
            JS_SetPropertyUint32(
                bridge->context, result,
                (uint32_t)index, js_value) < 0) {
            if (JS_IsException(js_value))
                JS_FreeValue(bridge->context, js_value);
            JS_FreeValue(bridge->context, result);
            return JS_EXCEPTION;
        }
        ++index;
        if (generated == CMETA_GEN_VALUE_AND_DONE) break;
    }
    if (index != length) {
        JS_FreeValue(bridge->context, result);
        return JS_EXCEPTION;
    }
    return result;
}

static bool quickjs_cmeta_default_export_collection(
    quickjs_cmeta_bridge *bridge,
    const cmeta_data_desc *descriptor,
    const cmeta_type_desc *storage_type,
    const cmeta_declared_type *declared_type,
    JSValueConst value,
    void *object,
    size_t depth,
    void *user) {
    const cmeta_data_desc *element;
    quickjs_cmeta_aligned_storage scratch = {0};
    cmeta_collector collector = {0};
    JSValue length_value;
    uint32_t length = 0u;
    uint32_t index;
    bool element_live = false;
    bool begun = false;
    bool ok = false;
    (void)storage_type;
    (void)declared_type;
    (void)user;
    if (bridge == NULL || descriptor == NULL ||
        object == NULL || !JS_IsArray(value))
        return false;
    element = cmeta_data_collection_element_data(descriptor);
    if (element == NULL || element->storage_type == NULL ||
        cmeta_data_value_restore_zero(
            descriptor, object) != CMETA_OK ||
        cmeta_data_collection_collector(
            descriptor, object,
            bridge->limits.max_array_items,
            &collector) != CMETA_OK ||
        !quickjs_cmeta_aligned_storage_init(
            &scratch, element->storage_type))
        goto cleanup;
    length_value = JS_GetPropertyStr(
        bridge->context, value, "length");
    if (JS_IsException(length_value) ||
        JS_ToUint32(
            bridge->context, &length, length_value) != 0) {
        if (!JS_IsException(length_value))
            JS_FreeValue(bridge->context, length_value);
        goto cleanup;
    }
    JS_FreeValue(bridge->context, length_value);
    if (length > bridge->limits.max_array_items ||
        cmeta_collector_begin(&collector) != CMETA_OK)
        goto cleanup;
    begun = true;
    for (index = 0u; index < length; ++index) {
        JSValue item;
        if (!quickjs_cmeta_property_budget(bridge) ||
            cmeta_data_value_init_zero(
                element, scratch.value) != CMETA_OK)
            goto cleanup;
        element_live = true;
        item = JS_GetPropertyUint32(
            bridge->context, value, index);
        if (JS_IsException(item) ||
            !quickjs_cmeta_export_value(
                bridge, element, element->storage_type,
                NULL, item, scratch.value, depth + 1u)) {
            if (!JS_IsException(item))
                JS_FreeValue(bridge->context, item);
            goto cleanup;
        }
        JS_FreeValue(bridge->context, item);
        if (cmeta_data_collection_accept(
                descriptor, &collector,
                element, scratch.value) != CMETA_OK)
            goto cleanup;
        if (cmeta_data_value_restore_zero(
                element, scratch.value) != CMETA_OK)
            goto cleanup;
        element_live = false;
    }
    if (cmeta_collector_finish(&collector) != CMETA_OK)
        goto cleanup;
    begun = false;
    ok = true;

cleanup:
    if (begun) cmeta_collector_abort(&collector);
    quickjs_cmeta_aligned_storage_destroy(
        &scratch, element, element_live);
    if (!ok)
        (void)cmeta_data_value_restore_zero(
            descriptor, object);
    return ok;
}

#endif

const quickjs_cmeta_collection_adapter *
quickjs_cmeta_default_collection_adapter(void) {
    static const quickjs_cmeta_collection_adapter adapter = {
        quickjs_cmeta_default_schema_supported,
#if TURBOSCXML_HAS_QUICKJS
        quickjs_cmeta_default_import_collection,
        quickjs_cmeta_default_export_collection
#endif
    };
    return &adapter;
}

static bool quickjs_cmeta_state_type_supported(
    const cmeta_type_desc *type,
    bool *managed) {
    if (!cmeta_type_desc_valid(type) || managed == NULL)
        return false;
    if (cmeta_type_require_traits(
            type,
            CMETA_TRAIT_TRIVIAL_COPY |
                CMETA_TRAIT_TRIVIAL_DESTROY) == CMETA_OK) {
        *managed = false;
        return true;
    }
    if (cmeta_type_require_traits(
            type,
            CMETA_TRAIT_COPY |
                CMETA_TRAIT_MOVE |
                CMETA_TRAIT_DESTROY) != CMETA_OK)
        return false;
    *managed = true;
    return true;
}

bool quickjs_cmeta_state_snapshot(
    quickjs_cmeta_state_scratch *scratch,
    const cmeta_data_desc *root,
    const void *committed,
    size_t max_snapshot_bytes) {
    const cmeta_type_desc *type;
    uintptr_t address;
    uintptr_t aligned;
    size_t allocation_size;
    bool managed;
    if (scratch == NULL || scratch->allocation != NULL ||
        scratch->value != NULL || committed == NULL ||
        !cmeta_data_desc_valid(root) ||
        root->kind != CMETA_DATA_STRUCT ||
        root->storage_type == NULL ||
        max_snapshot_bytes == 0u)
        return false;
    type = root->storage_type;
    if (!quickjs_cmeta_state_type_supported(type, &managed) ||
        type->size > max_snapshot_bytes ||
        type->align == 0u ||
        (type->align & (type->align - 1u)) != 0u ||
        type->size > SIZE_MAX - (type->align - 1u))
        return false;
    allocation_size = type->size + type->align - 1u;
    scratch->allocation = calloc(1u, allocation_size);
    if (scratch->allocation == NULL) return false;
    address = (uintptr_t)scratch->allocation;
    if (address > UINTPTR_MAX - (type->align - 1u)) {
        free(scratch->allocation);
        memset(scratch, 0, sizeof(*scratch));
        return false;
    }
    aligned = (address + type->align - 1u) &
        ~((uintptr_t)type->align - 1u);
    scratch->value = (void *)aligned;
    if (managed) {
        if (!type->traits->copy_construct(
                scratch->value, committed)) {
            free(scratch->allocation);
            memset(scratch, 0, sizeof(*scratch));
            return false;
        }
    } else {
        memcpy(scratch->value, committed, type->size);
    }
    scratch->live = true;
    return true;
}

void quickjs_cmeta_state_scratch_destroy(
    quickjs_cmeta_state_scratch *scratch,
    const cmeta_data_desc *root) {
    const cmeta_type_desc *type =
        root != NULL ? root->storage_type : NULL;
    bool managed = false;
    if (scratch == NULL) return;
    if (scratch->live && scratch->value != NULL &&
        quickjs_cmeta_state_type_supported(type, &managed) &&
        managed)
        type->traits->destroy(scratch->value);
    free(scratch->allocation);
    memset(scratch, 0, sizeof(*scratch));
}

bool quickjs_cmeta_state_publish(
    const cmeta_data_desc *root,
    void *committed,
    quickjs_cmeta_state_scratch *scratch) {
    const cmeta_type_desc *type;
    bool managed;
    if (committed == NULL || scratch == NULL ||
        !scratch->live || scratch->value == NULL ||
        !cmeta_data_desc_valid(root) ||
        root->storage_type == NULL)
        return false;
    type = root->storage_type;
    if (!quickjs_cmeta_state_type_supported(type, &managed))
        return false;
    if (managed) {
        type->traits->destroy(committed);
        type->traits->move_construct(
            committed, scratch->value);
        scratch->live = false;
    } else {
        memcpy(committed, scratch->value, type->size);
    }
    return true;
}
