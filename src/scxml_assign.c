#include "scxml_assign.h"
#include "scxml_quickjs.h"
#include "scxml_location.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCXML_ASSIGN_SINT64_UPPER_BOUND 9223372036854775808.0
#define SCXML_ASSIGN_UINT64_UPPER_BOUND 18446744073709551616.0

typedef enum scxml_assign_destination_kind {
    SCXML_ASSIGN_DESTINATION_MUTABLE = 0,
    SCXML_ASSIGN_DESTINATION_SUPPLEMENTAL,
    SCXML_ASSIGN_DESTINATION_READ_ONLY_SYSTEM,
    SCXML_ASSIGN_DESTINATION_INVALID
} scxml_assign_destination_kind;

typedef enum scxml_assign_source_kind {
    SCXML_ASSIGN_SOURCE_EXPRESSION = 0,
    SCXML_ASSIGN_SOURCE_STRING_LITERAL,
    SCXML_ASSIGN_SOURCE_EXTERNAL
} scxml_assign_source_kind;

typedef struct scxml_assign_program_impl {
    const cmeta_data_desc *destination;
    size_t destination_offset;
    size_t destination_slot;
    size_t max_string_bytes;
    scxml_assign_destination_kind destination_kind;
    scxml_assign_source_kind source_kind;
    scxml_expr_program expression;
    unsigned char *literal_bytes;
    size_t literal_byte_count;
    char *external_uri;
    size_t external_uri_size;
} scxml_assign_program_impl;

static scxml_expr_status assign_report(
    scxml_expr_diagnostic *diagnostic,
    scxml_expr_status status, size_t byte_offset,
    const char *message) {
    if (diagnostic != NULL) {
        memset(diagnostic, 0, sizeof(*diagnostic));
        diagnostic->status = status;
        diagnostic->byte_offset = byte_offset;
        if (message != NULL)
            (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                           "%s", message);
    }
    return status;
}

static bool assign_destination_accepts(
    const cmeta_data_desc *destination,
    scxml_expr_value_kind source_kind) {
    switch (destination->kind) {
        case CMETA_DATA_BOOL:
            return source_kind == SCXML_EXPR_VALUE_BOOL;
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT:
        case CMETA_DATA_FLOAT:
            return source_kind == SCXML_EXPR_VALUE_SINT ||
                   source_kind == SCXML_EXPR_VALUE_UINT ||
                   source_kind == SCXML_EXPR_VALUE_FLOAT;
        case CMETA_DATA_ENUM:
            return source_kind == SCXML_EXPR_VALUE_SINT ||
                   source_kind == SCXML_EXPR_VALUE_UINT;
        case CMETA_DATA_STRING:
            return source_kind == SCXML_EXPR_VALUE_STRING;
        default:
            return false;
    }
}

static scxml_expr_value_kind assign_destination_value_kind(
    const cmeta_data_desc *destination) {
    if (destination == NULL) return SCXML_EXPR_VALUE_INVALID;
    switch (destination->kind) {
        case CMETA_DATA_BOOL: return SCXML_EXPR_VALUE_BOOL;
        case CMETA_DATA_SINT:
        case CMETA_DATA_ENUM: return SCXML_EXPR_VALUE_SINT;
        case CMETA_DATA_UINT: return SCXML_EXPR_VALUE_UINT;
        case CMETA_DATA_FLOAT: return SCXML_EXPR_VALUE_FLOAT;
        case CMETA_DATA_STRING: return SCXML_EXPR_VALUE_STRING;
        default: return SCXML_EXPR_VALUE_INVALID;
    }
}

static scxml_expr_status assign_create_program(
    scxml_assign_program_impl **out_impl,
    const char *location, size_t location_size,
    const cmeta_data_desc *root,
    const scxml_scope_schema *supplemental,
    const scxml_expr_limits *limits,
    scxml_assign_location_policy location_policy,
    scxml_expr_diagnostic *diagnostic) {
    scxml_assign_program_impl *impl;
    const cmeta_data_desc *destination = NULL;
    scxml_assign_destination_kind destination_kind =
        SCXML_ASSIGN_DESTINATION_MUTABLE;
    scxml_location destination_location = {0};
    scxml_expr_status status;
    if (out_impl == NULL || location == NULL || location_size == 0u ||
        !cmeta_data_desc_valid(root) || root->kind != CMETA_DATA_STRUCT ||
        (location_policy != SCXML_ASSIGN_LOCATION_STRICT &&
         location_policy != SCXML_ASSIGN_LOCATION_RUNTIME) ||
        !scxml_expr_limits_valid(limits))
        return assign_report(diagnostic, SCXML_EXPR_INVALID_ARGUMENT, 0u,
                             "invalid CMeta assignment compile arguments");
    if (location_size > limits->max_source_bytes)
        return assign_report(diagnostic, SCXML_EXPR_LIMIT_EXCEEDED,
                             limits->max_source_bytes,
                             "CMeta assignment location byte limit exceeded");
    if (scxml_location_is_read_only_system(
            location, location_size, limits->max_path_depth)) {
        destination_kind = SCXML_ASSIGN_DESTINATION_READ_ONLY_SYSTEM;
    } else {
        status = scxml_location_compile_with_scope(
            &destination_location, location, location_size, root,
            supplemental, limits->max_path_depth, true, diagnostic);
        if (status != SCXML_EXPR_OK) {
            if (status != SCXML_EXPR_UNKNOWN_LOCATION ||
                location_policy != SCXML_ASSIGN_LOCATION_RUNTIME ||
                location[0] == '_')
                return status;
            destination_kind = SCXML_ASSIGN_DESTINATION_INVALID;
        } else {
            destination = destination_location.value;
            if (destination_location.kind == SCXML_LOCATION_SUPPLEMENTAL)
                destination_kind = SCXML_ASSIGN_DESTINATION_SUPPLEMENTAL;
            if ((destination->kind == CMETA_DATA_STRING &&
                 (cmeta_data_buffer_ops_of(destination) == NULL ||
                  cmeta_data_buffer_ops_of(destination)->ownership ==
                      CMETA_DATA_BUFFER_CUSTOM)) ||
                (destination->kind == CMETA_DATA_ENUM &&
                 cmeta_data_enum_ops_of(destination) == NULL))
                return assign_report(
                    diagnostic, SCXML_EXPR_TYPE_MISMATCH, 0u,
                    "CMeta assignment destination lacks a safe adapter");
        }
    }
    impl = (scxml_assign_program_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return assign_report(diagnostic, SCXML_EXPR_ALLOCATION_FAILED, 0u,
                             "CMeta assignment program allocation failed");
    impl->destination = destination;
    impl->destination_offset = destination_location.offset;
    impl->destination_slot = destination_location.slot;
    impl->max_string_bytes = limits->max_string_bytes;
    impl->destination_kind = destination_kind;
    *out_impl = impl;
    return SCXML_EXPR_OK;
}

scxml_expr_status scxml_assign_compile(
    scxml_assign_program *out,
    const char *location, size_t location_size,
    const char *expression, size_t expression_size,
    const cmeta_data_desc *root,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_limits *limits_or_null,
    scxml_assign_location_policy location_policy,
    scxml_expr_diagnostic *diagnostic) {
    return scxml_assign_compile_with_scope(
        out, location, location_size, expression, expression_size,
        root, NULL, resolve_state, resolve_user, limits_or_null,
        location_policy, diagnostic);
}

scxml_expr_status scxml_assign_compile_with_scope(
    scxml_assign_program *out,
    const char *location, size_t location_size,
    const char *expression, size_t expression_size,
    const cmeta_data_desc *root,
    const scxml_scope_schema *supplemental,
    scxml_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const scxml_expr_limits *limits_or_null,
    scxml_assign_location_policy location_policy,
    scxml_expr_diagnostic *diagnostic) {
    const scxml_expr_limits limits =
        limits_or_null != NULL ? *limits_or_null
                               : scxml_expr_default_limits();
    scxml_assign_program_impl *impl = NULL;
    scxml_expr_status status;
    if (out == NULL || out->impl != NULL || expression == NULL ||
        expression_size == 0u || resolve_state == NULL)
        return assign_report(diagnostic,
                             SCXML_EXPR_INVALID_ARGUMENT, 0u,
                             "invalid CMeta assignment compile arguments");
    status = assign_create_program(
        &impl, location, location_size, root, supplemental, &limits,
        location_policy,
        diagnostic);
    if (status != SCXML_EXPR_OK) return status;
    status = scxml_expr_compile_value_with_scope(
        &impl->expression, expression, expression_size, root, supplemental,
        resolve_state, resolve_user, &limits, diagnostic);
    if (status != SCXML_EXPR_OK) {
        free(impl);
        return status;
    }
    if ((impl->destination_kind == SCXML_ASSIGN_DESTINATION_MUTABLE ||
         impl->destination_kind == SCXML_ASSIGN_DESTINATION_SUPPLEMENTAL) &&
        !assign_destination_accepts(
            impl->destination,
            scxml_expr_program_value_kind(&impl->expression))) {
        scxml_expr_program_destroy(&impl->expression);
        free(impl);
        return assign_report(diagnostic,
                             SCXML_EXPR_TYPE_MISMATCH, 0u,
                             "CMeta assignment source and destination types differ");
    }
    impl->source_kind = SCXML_ASSIGN_SOURCE_EXPRESSION;
    out->impl = impl;
    return assign_report(diagnostic, SCXML_EXPR_OK, 0u, NULL);
}

scxml_expr_status scxml_assign_compile_quickjs_with_scope(
    scxml_assign_program *out,
    const char *location, size_t location_size,
    const char *expression, size_t expression_size,
    const cmeta_data_desc *root,
    const scxml_scope_schema *supplemental,
    const scxml_quickjs_compile_options_v1 *quickjs_options,
    const scxml_expr_limits *limits_or_null,
    scxml_assign_location_policy location_policy,
    scxml_expr_diagnostic *diagnostic) {
    const scxml_expr_limits limits = limits_or_null != NULL
        ? *limits_or_null : scxml_expr_default_limits();
    scxml_assign_program_impl *impl = NULL;
    char quickjs_diagnostic[SCXML_EXPR_DIAGNOSTIC_CAPACITY] = {0};
    scxml_quickjs_status quickjs_status;
    scxml_expr_status status;
    if (out == NULL || out->impl != NULL || expression == NULL ||
        expression_size == 0u || quickjs_options == NULL)
        return assign_report(diagnostic, SCXML_EXPR_INVALID_ARGUMENT, 0u,
                             "invalid QuickJS assignment compile arguments");
    status = assign_create_program(
        &impl, location, location_size, root, supplemental, &limits,
        location_policy, diagnostic);
    if (status != SCXML_EXPR_OK) return status;
    quickjs_status = scxml_quickjs_validate_expression(
        quickjs_options, expression, expression_size,
        quickjs_diagnostic, sizeof(quickjs_diagnostic));
    if (quickjs_status != SCXML_QUICKJS_OK) {
        free(impl);
        return assign_report(
            diagnostic,
            quickjs_status == SCXML_QUICKJS_LIMIT_EXCEEDED
                ? SCXML_EXPR_LIMIT_EXCEEDED : SCXML_EXPR_SYNTAX_ERROR,
            0u, quickjs_diagnostic[0] != '\0'
                ? quickjs_diagnostic : "QuickJS expression is invalid");
    }
    status = scxml_expr_compile_external(
        &impl->expression, expression, expression_size,
        assign_destination_value_kind(impl->destination),
        scxml_quickjs_evaluate_expression, &limits, diagnostic);
    if (status != SCXML_EXPR_OK) {
        free(impl);
        return status;
    }
    impl->source_kind = SCXML_ASSIGN_SOURCE_EXPRESSION;
    out->impl = impl;
    return assign_report(diagnostic, SCXML_EXPR_OK, 0u, NULL);
}

scxml_expr_status scxml_assign_compile_string_literal(
    scxml_assign_program *out,
    const char *location, size_t location_size,
    const char *bytes, size_t byte_count,
    const cmeta_data_desc *root,
    const scxml_expr_limits *limits_or_null,
    scxml_assign_location_policy location_policy,
    scxml_expr_diagnostic *diagnostic) {
    const scxml_expr_limits limits =
        limits_or_null != NULL ? *limits_or_null
                               : scxml_expr_default_limits();
    scxml_assign_program_impl *impl = NULL;
    scxml_expr_status status;
    if (out == NULL || out->impl != NULL ||
        (byte_count != 0u && bytes == NULL))
        return assign_report(diagnostic, SCXML_EXPR_INVALID_ARGUMENT, 0u,
                             "invalid CMeta literal assignment arguments");
    status = assign_create_program(
        &impl, location, location_size, root, NULL, &limits,
        location_policy,
        diagnostic);
    if (status != SCXML_EXPR_OK) return status;
    if (impl->destination_kind == SCXML_ASSIGN_DESTINATION_MUTABLE &&
        (impl->destination == NULL ||
         impl->destination->kind != CMETA_DATA_STRING)) {
        free(impl);
        return assign_report(
            diagnostic, SCXML_EXPR_TYPE_MISMATCH, 0u,
            "CMeta inline data content requires a string destination");
    }
    if (byte_count > limits.max_literal_bytes ||
        byte_count > limits.max_string_bytes) {
        free(impl);
        return assign_report(diagnostic, SCXML_EXPR_LIMIT_EXCEEDED, 0u,
                             "CMeta inline data content exceeds limits");
    }
    if (byte_count != 0u) {
        impl->literal_bytes = (unsigned char *)malloc(byte_count);
        if (impl->literal_bytes == NULL) {
            free(impl);
            return assign_report(
                diagnostic, SCXML_EXPR_ALLOCATION_FAILED, 0u,
                "CMeta inline data content allocation failed");
        }
        memcpy(impl->literal_bytes, bytes, byte_count);
    }
    impl->source_kind = SCXML_ASSIGN_SOURCE_STRING_LITERAL;
    impl->literal_byte_count = byte_count;
    out->impl = impl;
    return assign_report(diagnostic, SCXML_EXPR_OK, 0u, NULL);
}

scxml_expr_status scxml_assign_compile_external(
    scxml_assign_program *out,
    const char *location, size_t location_size,
    const char *uri, size_t uri_size,
    const cmeta_data_desc *root,
    const scxml_expr_limits *limits_or_null,
    scxml_expr_diagnostic *diagnostic) {
    const scxml_expr_limits limits =
        limits_or_null != NULL ? *limits_or_null
                               : scxml_expr_default_limits();
    scxml_assign_program_impl *impl = NULL;
    const cmeta_type_desc *type;
    scxml_expr_status status;
    if (out == NULL || out->impl != NULL || uri == NULL || uri_size == 0u)
        return assign_report(diagnostic, SCXML_EXPR_INVALID_ARGUMENT, 0u,
                             "invalid CMeta external data arguments");
    if (uri_size > limits.max_source_bytes)
        return assign_report(diagnostic, SCXML_EXPR_LIMIT_EXCEEDED,
                             limits.max_source_bytes,
                             "CMeta external data URI byte limit exceeded");
    status = assign_create_program(
        &impl, location, location_size, root, NULL, &limits,
        SCXML_ASSIGN_LOCATION_STRICT, diagnostic);
    if (status != SCXML_EXPR_OK) return status;
    if (impl->destination_kind != SCXML_ASSIGN_DESTINATION_MUTABLE ||
        impl->destination == NULL ||
        impl->destination->storage_type == NULL) {
        free(impl);
        return assign_report(diagnostic, SCXML_EXPR_TYPE_MISMATCH, 0u,
                             "CMeta external data destination is not mutable");
    }
    type = impl->destination->storage_type;
    if (type->size == 0u || type->align == 0u ||
        (type->align & (type->align - 1u)) != 0u ||
        (cmeta_type_require_traits(
             type, CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY) !=
             CMETA_OK &&
         cmeta_type_require_traits(
             type, CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY) != CMETA_OK)) {
        free(impl);
        return assign_report(
            diagnostic, SCXML_EXPR_TYPE_MISMATCH, 0u,
            "CMeta external data destination lacks replacement traits");
    }
    impl->external_uri = (char *)malloc(uri_size);
    if (impl->external_uri == NULL) {
        free(impl);
        return assign_report(diagnostic, SCXML_EXPR_ALLOCATION_FAILED, 0u,
                             "CMeta external data URI allocation failed");
    }
    memcpy(impl->external_uri, uri, uri_size);
    impl->external_uri_size = uri_size;
    impl->source_kind = SCXML_ASSIGN_SOURCE_EXTERNAL;
    out->impl = impl;
    return assign_report(diagnostic, SCXML_EXPR_OK, 0u, NULL);
}

static bool assign_sint_value(
    const scxml_expr_value *source, int64_t *out) {
    if (source->kind == SCXML_EXPR_VALUE_SINT) {
        *out = source->data.sint;
        return true;
    }
    if (source->kind == SCXML_EXPR_VALUE_UINT) {
        if (source->data.uint > INT64_MAX) return false;
        *out = (int64_t)source->data.uint;
        return true;
    }
    if (source->kind == SCXML_EXPR_VALUE_FLOAT &&
        isfinite(source->data.number) &&
        trunc(source->data.number) == source->data.number &&
        source->data.number >= -SCXML_ASSIGN_SINT64_UPPER_BOUND &&
        source->data.number < SCXML_ASSIGN_SINT64_UPPER_BOUND) {
        *out = (int64_t)source->data.number;
        return true;
    }
    return false;
}

static bool assign_uint_value(
    const scxml_expr_value *source, uint64_t *out) {
    if (source->kind == SCXML_EXPR_VALUE_UINT) {
        *out = source->data.uint;
        return true;
    }
    if (source->kind == SCXML_EXPR_VALUE_SINT) {
        if (source->data.sint < 0) return false;
        *out = (uint64_t)source->data.sint;
        return true;
    }
    if (source->kind == SCXML_EXPR_VALUE_FLOAT &&
        isfinite(source->data.number) &&
        trunc(source->data.number) == source->data.number &&
        source->data.number >= 0.0 &&
        source->data.number < SCXML_ASSIGN_UINT64_UPPER_BOUND) {
        *out = (uint64_t)source->data.number;
        return true;
    }
    return false;
}

static bool assign_store_sint(const cmeta_data_desc *destination,
                              void *object, int64_t value) {
    const uint8_t bits =
        ((const cmeta_data_integer_shape *)destination->shape)->bits;
    switch (bits) {
        case 8: {
            const int8_t converted = (int8_t)value;
            if ((int64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 16: {
            const int16_t converted = (int16_t)value;
            if ((int64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 32: {
            const int32_t converted = (int32_t)value;
            if ((int64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 64:
            memcpy(object, &value, sizeof(value));
            return true;
        default:
            return false;
    }
}

static bool assign_store_uint(const cmeta_data_desc *destination,
                              void *object, uint64_t value) {
    const uint8_t bits =
        ((const cmeta_data_integer_shape *)destination->shape)->bits;
    switch (bits) {
        case 8: {
            const uint8_t converted = (uint8_t)value;
            if ((uint64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 16: {
            const uint16_t converted = (uint16_t)value;
            if ((uint64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 32: {
            const uint32_t converted = (uint32_t)value;
            if ((uint64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 64:
            memcpy(object, &value, sizeof(value));
            return true;
        default:
            return false;
    }
}

static bool assign_float_value(
    const scxml_expr_value *source, double *out) {
    double value;
    if (source->kind == SCXML_EXPR_VALUE_FLOAT) {
        *out = source->data.number;
        return true;
    }
    if (source->kind == SCXML_EXPR_VALUE_SINT) {
        value = (double)source->data.sint;
        if (value < -SCXML_ASSIGN_SINT64_UPPER_BOUND ||
            value >= SCXML_ASSIGN_SINT64_UPPER_BOUND ||
            (int64_t)value != source->data.sint)
            return false;
        *out = value;
        return true;
    }
    if (source->kind == SCXML_EXPR_VALUE_UINT) {
        value = (double)source->data.uint;
        if (value >= SCXML_ASSIGN_UINT64_UPPER_BOUND ||
            (uint64_t)value != source->data.uint)
            return false;
        *out = value;
        return true;
    }
    return false;
}

static bool assign_store_float(const cmeta_data_desc *destination,
                               void *object, double value) {
    const uint8_t bits =
        ((const cmeta_data_float_shape *)destination->shape)->bits;
    if (bits == 32u) {
        const float converted = (float)value;
        if ((double)converted != value) return false;
        memcpy(object, &converted, sizeof(converted));
        return true;
    }
    if (bits == 64u) {
        memcpy(object, &value, sizeof(value));
        return true;
    }
    return false;
}

static scxml_expr_status assign_string(
    const scxml_assign_program_impl *impl, void *destination,
    const scxml_expr_value *source,
    scxml_expr_diagnostic *diagnostic) {
    const cmeta_data_buffer_ops *ops =
        cmeta_data_buffer_ops_of(impl->destination);
    const unsigned char *bytes =
        (const unsigned char *)source->data.string.data;
    unsigned char *copy = NULL;
    cmeta_status status;
    if (source->data.string.size > impl->max_string_bytes)
        return assign_report(diagnostic,
                             SCXML_EXPR_LIMIT_EXCEEDED, 0u,
                             "CMeta assignment string limit exceeded");
    if (ops->ownership == CMETA_DATA_BUFFER_OWNED &&
        source->data.string.size != 0u) {
        copy = (unsigned char *)malloc(source->data.string.size);
        if (copy == NULL)
            return assign_report(diagnostic,
                                 SCXML_EXPR_ALLOCATION_FAILED, 0u,
                                 "CMeta assignment string copy failed");
        memcpy(copy, bytes, source->data.string.size);
        bytes = copy;
    }
    status = cmeta_data_buffer_restore_zero(impl->destination, destination);
    if (status == CMETA_OK)
        status = cmeta_data_buffer_assign(
            impl->destination, destination, bytes,
            source->data.string.size, impl->max_string_bytes);
    free(copy);
    return status == CMETA_OK
               ? assign_report(diagnostic, SCXML_EXPR_OK, 0u, NULL)
               : assign_report(diagnostic,
                               SCXML_EXPR_EVALUATION_ERROR, 0u,
                               "CMeta string assignment adapter failed");
}

static scxml_expr_status assign_apply(
    const scxml_assign_program *program,
    const void *source_root,
    void *destination_root,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    const scxml_expr_system_values *system_values,
    bool check_destination_binding,
    scxml_expr_diagnostic *diagnostic) {
    const scxml_assign_program_impl *impl =
        program != NULL
            ? (const scxml_assign_program_impl *)program->impl
            : NULL;
    scxml_expr_value source;
    scxml_expr_status status;
    unsigned char *destination;
    int64_t sint;
    uint64_t uint;
    double number;
    if (impl == NULL || source_root == NULL || destination_root == NULL ||
        is_active == NULL)
        return assign_report(diagnostic,
                             SCXML_EXPR_INVALID_ARGUMENT, 0u,
                             "invalid CMeta assignment evaluation arguments");
    if (impl->destination_kind == SCXML_ASSIGN_DESTINATION_READ_ONLY_SYSTEM)
        return assign_report(diagnostic,
                             SCXML_EXPR_EVALUATION_ERROR, 0u,
                             "CMeta system locations are read-only");
    if (impl->destination_kind == SCXML_ASSIGN_DESTINATION_INVALID)
        return assign_report(diagnostic,
                             SCXML_EXPR_UNKNOWN_LOCATION, 0u,
                             "CMeta assignment destination is invalid");
    if (check_destination_binding &&
        impl->destination_kind == SCXML_ASSIGN_DESTINATION_MUTABLE) {
        if (impl->destination == NULL ||
            impl->destination->storage_type == NULL)
            return assign_report(
                diagnostic, SCXML_EXPR_INVALID_ARGUMENT, 0u,
                "CMeta assignment destination storage is invalid");
        status = scxml_expr_require_data_bound(
            system_values, impl->destination_offset,
            impl->destination->storage_type->size, diagnostic);
        if (status != SCXML_EXPR_OK) return status;
    }
    if (impl->source_kind == SCXML_ASSIGN_SOURCE_EXTERNAL)
        return assign_report(diagnostic, SCXML_EXPR_INVALID_ARGUMENT, 0u,
                             "CMeta external data requires a resource reader");
    if (impl->source_kind == SCXML_ASSIGN_SOURCE_STRING_LITERAL) {
        source = (scxml_expr_value){
            .kind = SCXML_EXPR_VALUE_STRING,
            .data.string = {
                (const char *)impl->literal_bytes,
                impl->literal_byte_count}};
    } else {
        status = scxml_expr_evaluate_value_with_system(
            &impl->expression, source_root, is_active, active_user,
            system_values, &source, diagnostic);
        if (status != SCXML_EXPR_OK) return status;
    }
    if (impl->destination_kind == SCXML_ASSIGN_DESTINATION_SUPPLEMENTAL) {
        const cmeta_data_desc *slot_value = NULL;
        const void *slot_object = NULL;
        if (system_values == NULL || system_values->supplemental == NULL ||
            !scxml_scope_view_read(
                system_values->supplemental, impl->destination_slot,
                &slot_value, &slot_object) || slot_value == NULL ||
            slot_value->storage_type == NULL ||
            impl->destination_offset > slot_value->storage_type->size ||
            impl->destination->storage_type->size >
                slot_value->storage_type->size - impl->destination_offset)
            return assign_report(
                diagnostic, SCXML_EXPR_UNKNOWN_LOCATION, 0u,
                "CMeta supplemental assignment destination is unbound");
        destination = (unsigned char *)(uintptr_t)slot_object +
                      impl->destination_offset;
    } else {
        destination = (unsigned char *)destination_root +
                      impl->destination_offset;
    }
    switch (impl->destination->kind) {
        case CMETA_DATA_BOOL:
            memcpy(destination, &source.data.boolean, sizeof(bool));
            return assign_report(diagnostic, SCXML_EXPR_OK, 0u,
                                 NULL);
        case CMETA_DATA_SINT:
            if (assign_sint_value(&source, &sint) &&
                assign_store_sint(impl->destination, destination, sint))
                return assign_report(diagnostic,
                                     SCXML_EXPR_OK, 0u, NULL);
            break;
        case CMETA_DATA_UINT:
            if (assign_uint_value(&source, &uint) &&
                assign_store_uint(impl->destination, destination, uint))
                return assign_report(diagnostic,
                                     SCXML_EXPR_OK, 0u, NULL);
            break;
        case CMETA_DATA_FLOAT:
            if (assign_float_value(&source, &number) &&
                assign_store_float(impl->destination, destination, number))
                return assign_report(diagnostic,
                                     SCXML_EXPR_OK, 0u, NULL);
            break;
        case CMETA_DATA_ENUM:
            if (!assign_sint_value(&source, &sint)) break;
            if (cmeta_data_enum_restore_zero(impl->destination, destination) ==
                    CMETA_OK &&
                cmeta_data_enum_assign(impl->destination, destination, sint) ==
                    CMETA_OK)
                return assign_report(diagnostic,
                                     SCXML_EXPR_OK, 0u, NULL);
            return assign_report(diagnostic,
                                 SCXML_EXPR_EVALUATION_ERROR, 0u,
                                 "CMeta enum assignment adapter failed");
        case CMETA_DATA_STRING:
            return assign_string(impl, destination, &source, diagnostic);
        default:
            break;
    }
    return assign_report(diagnostic, SCXML_EXPR_TYPE_MISMATCH, 0u,
                         "CMeta assignment conversion is not exact");
}

scxml_expr_status scxml_assign_apply(
    const scxml_assign_program *program,
    void *staged_root,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    scxml_expr_diagnostic *diagnostic) {
    return assign_apply(program, staged_root, staged_root, is_active,
                        active_user, NULL, true, diagnostic);
}

scxml_expr_status scxml_assign_apply_with_system(
    const scxml_assign_program *program,
    void *staged_root,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    const scxml_expr_system_values *system_values,
    scxml_expr_diagnostic *diagnostic) {
    return assign_apply(program, staged_root, staged_root, is_active,
                        active_user, system_values, true, diagnostic);
}

scxml_expr_status scxml_assign_apply_from_with_system(
    const scxml_assign_program *program,
    const void *source_root,
    void *destination_root,
    scxml_expr_is_active_fn is_active,
    void *active_user,
    const scxml_expr_system_values *system_values,
    scxml_expr_diagnostic *diagnostic) {
    return assign_apply(program, source_root, destination_root, is_active,
                        active_user, system_values, false, diagnostic);
}

bool scxml_assign_external_source(
    const scxml_assign_program *program,
    const char **out_uri, size_t *out_uri_size,
    const cmeta_data_desc **out_destination) {
    const scxml_assign_program_impl *impl = program != NULL
        ? (const scxml_assign_program_impl *)program->impl : NULL;
    if (impl == NULL || out_uri == NULL || out_uri_size == NULL ||
        out_destination == NULL ||
        impl->source_kind != SCXML_ASSIGN_SOURCE_EXTERNAL ||
        impl->external_uri == NULL || impl->external_uri_size == 0u ||
        impl->destination_kind != SCXML_ASSIGN_DESTINATION_MUTABLE ||
        impl->destination == NULL)
        return false;
    *out_uri = impl->external_uri;
    *out_uri_size = impl->external_uri_size;
    *out_destination = impl->destination;
    return true;
}

static scxml_expr_status assign_databind_failure(
    DataBindStatus status,
    const DataBindNativeDiagnostic *native_diagnostic,
    scxml_expr_diagnostic *diagnostic) {
    const char *message =
        native_diagnostic != NULL &&
                native_diagnostic->error.message[0] != '\0'
            ? native_diagnostic->error.message
            : "DataBind external data decode failed";
    return assign_report(
        diagnostic,
        status == DATA_BIND_ERR_LIMIT
            ? SCXML_EXPR_LIMIT_EXCEEDED
            : status == DATA_BIND_ERR_INVALID_ARG
                  ? SCXML_EXPR_INVALID_ARGUMENT
                  : SCXML_EXPR_EVALUATION_ERROR,
        0u, message);
}

scxml_expr_status scxml_assign_apply_external(
    const scxml_assign_program *program,
    cserde_reader *reader,
    const DataBindNativeOptions *options,
    size_t max_buffer_bytes,
    void *decode_storage, size_t decode_storage_size,
    void *staged_root,
    scxml_expr_diagnostic *diagnostic) {
    const scxml_assign_program_impl *impl = program != NULL
        ? (const scxml_assign_program_impl *)program->impl : NULL;
    const cmeta_type_desc *type;
    unsigned char *destination;
    DataBindNativeDiagnostic bind_diagnostic =
        DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    DataBindStatus bind_status;
    cserde_token trailing;
    cserde_status reader_status;
    bool trivial;
    if (impl == NULL || reader == NULL || options == NULL ||
        decode_storage == NULL || staged_root == NULL ||
        max_buffer_bytes == 0u ||
        impl->source_kind != SCXML_ASSIGN_SOURCE_EXTERNAL ||
        impl->destination_kind != SCXML_ASSIGN_DESTINATION_MUTABLE ||
        impl->destination == NULL ||
        impl->destination->storage_type == NULL)
        return assign_report(diagnostic, SCXML_EXPR_INVALID_ARGUMENT, 0u,
                             "invalid CMeta external data decode arguments");
    type = impl->destination->storage_type;
    if (type->size == 0u || type->size > decode_storage_size ||
        type->align == 0u ||
        (uintptr_t)decode_storage % type->align != 0u)
        return assign_report(diagnostic, SCXML_EXPR_INVALID_ARGUMENT, 0u,
                             "CMeta external data decode storage is invalid");
    trivial = cmeta_type_require_traits(
        type, CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY) ==
        CMETA_OK;
    if (!trivial && cmeta_type_require_traits(
            type, CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY) != CMETA_OK)
        return assign_report(diagnostic, SCXML_EXPR_INVALID_ARGUMENT, 0u,
                             "CMeta external data replacement traits are invalid");

    memset(decode_storage, 0, type->size);
    bind_status = data_bind_native_init(
        options, impl->destination, decode_storage, decode_storage_size,
        &bind_diagnostic);
    if (bind_status != DATA_BIND_OK) {
        memset(decode_storage, 0, type->size);
        return assign_databind_failure(
            bind_status, &bind_diagnostic, diagnostic);
    }

    bind_status = data_bind_native_decode_bounded(
        options, impl->destination, reader, decode_storage,
        decode_storage_size, max_buffer_bytes, &bind_diagnostic);
    if (bind_status != DATA_BIND_OK) {
        (void)data_bind_native_clear(
            options, impl->destination, decode_storage, decode_storage_size,
            &bind_diagnostic);
        memset(decode_storage, 0, type->size);
        return assign_databind_failure(
            bind_status, &bind_diagnostic, diagnostic);
    }

    reader_status = cserde_reader_next(reader, &trailing);
    if (reader_status != CSERDE_DONE) {
        (void)data_bind_native_clear(
            options, impl->destination, decode_storage, decode_storage_size,
            &bind_diagnostic);
        memset(decode_storage, 0, type->size);
        return assign_report(
            diagnostic,
            reader_status == CSERDE_LIMIT_EXCEEDED
                ? SCXML_EXPR_LIMIT_EXCEEDED
                : SCXML_EXPR_EVALUATION_ERROR,
            0u, reader_status == CSERDE_OK
                    ? "CMeta external data contains trailing tokens"
                    : "CMeta external data reader failed after one value");
    }

    destination = (unsigned char *)staged_root + impl->destination_offset;
    if (trivial) {
        memcpy(destination, decode_storage, type->size);
    } else {
        type->traits->destroy(destination);
        type->traits->move_construct(destination, decode_storage);
    }

    bind_diagnostic = (DataBindNativeDiagnostic)
        DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    bind_status = data_bind_native_clear(
        options, impl->destination, decode_storage, decode_storage_size,
        &bind_diagnostic);
    memset(decode_storage, 0, type->size);
    if (bind_status != DATA_BIND_OK)
        return assign_databind_failure(
            bind_status, &bind_diagnostic, diagnostic);
    return assign_report(diagnostic, SCXML_EXPR_OK, 0u, NULL);
}

bool scxml_assign_destination_is_read_only_system(
    const scxml_assign_program *program) {
    const scxml_assign_program_impl *impl = program != NULL
        ? (const scxml_assign_program_impl *)program->impl : NULL;
    return impl != NULL &&
           impl->destination_kind == SCXML_ASSIGN_DESTINATION_READ_ONLY_SYSTEM;
}

bool scxml_assign_destination_matches(
    const scxml_assign_program *program,
    const scxml_location *location) {
    const scxml_assign_program_impl *impl = program != NULL
        ? (const scxml_assign_program_impl *)program->impl : NULL;
    return impl != NULL && location != NULL &&
           impl->destination_kind == SCXML_ASSIGN_DESTINATION_MUTABLE &&
           impl->destination == location->value &&
           impl->destination_offset == location->offset &&
           impl->destination != NULL &&
           impl->destination->storage_type != NULL &&
           impl->destination->storage_type->size == location->storage_size;
}

bool scxml_assign_destination_range(
    const scxml_assign_program *program,
    size_t *out_offset, size_t *out_storage_size) {
    const scxml_assign_program_impl *impl = program != NULL
        ? (const scxml_assign_program_impl *)program->impl : NULL;
    if (impl == NULL || out_offset == NULL || out_storage_size == NULL ||
        impl->destination_kind != SCXML_ASSIGN_DESTINATION_MUTABLE ||
        impl->destination == NULL || impl->destination->storage_type == NULL ||
        impl->destination->storage_type->size == 0u)
        return false;
    *out_offset = impl->destination_offset;
    *out_storage_size = impl->destination->storage_type->size;
    return true;
}

void scxml_assign_program_destroy(
    scxml_assign_program *program) {
    scxml_assign_program_impl *impl;
    if (program == NULL || program->impl == NULL) return;
    impl = (scxml_assign_program_impl *)program->impl;
    scxml_expr_program_destroy(&impl->expression);
    free(impl->literal_bytes);
    free(impl->external_uri);
    free(impl);
    program->impl = NULL;
}
