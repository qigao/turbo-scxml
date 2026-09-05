#include <ccxml/ccxml.h>

#include "scxml_expr.h"
#include "scxml_foreach.h"
#include "scxml_scope.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct ccxml_cmeta_datamodel_impl {
    const cmeta_data_desc *root;
    const cmeta_data_desc **semantic_data;
    void *state;
    size_t semantic_data_count;
    size_t max_path_depth;
    size_t max_string_bytes;
} ccxml_cmeta_datamodel_impl;

typedef struct ccxml_cmeta_assignment_ticket {
    const cmeta_data_buffer_ops *buffer_ops;
    const cmeta_type_traits *traits;
    void *destination;
    void *allocation;
    void *replacement;
} ccxml_cmeta_assignment_ticket;

typedef struct ccxml_cmeta_condition {
    scxml_expr_program program;
} ccxml_cmeta_condition;

typedef struct ccxml_cmeta_string_expression {
    scxml_expr_program program;
} ccxml_cmeta_string_expression;

static void set_error(const char **out_error, const char *message) {
    if (out_error != NULL) *out_error = message;
}

static const cmeta_data_field_desc *find_field(
    const cmeta_data_struct_shape *shape,
    const char *name, size_t name_size) {
    size_t index;
    if (shape == NULL || name == NULL) return NULL;
    for (index = 0u; index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &shape->fields[index];
        if (field->name != NULL && strlen(field->name) == name_size &&
            memcmp(field->name, name, name_size) == 0)
            return field;
    }
    return NULL;
}

static bool resolve_value(
    const ccxml_cmeta_datamodel_impl *impl,
    const char *location, size_t location_size,
    const cmeta_data_desc **out_value, void **out_destination) {
    const cmeta_data_desc *current;
    size_t absolute_offset = 0u;
    size_t segment_start = 0u;
    size_t depth = 0u;
    size_t index;
    if (impl == NULL || location == NULL || location_size == 0u ||
        out_value == NULL || out_destination == NULL)
        return false;
    current = impl->root;
    for (index = 0u; index <= location_size; ++index) {
        const bool at_end = index == location_size;
        const cmeta_data_struct_shape *shape;
        const cmeta_data_field_desc *field;
        size_t field_end;
        if (!at_end && location[index] != '.') continue;
        if (index == segment_start || ++depth > impl->max_path_depth ||
            !cmeta_data_desc_valid(current) ||
            current->kind != CMETA_DATA_STRUCT || current->shape == NULL ||
            current->storage_type == NULL)
            return false;
        shape = (const cmeta_data_struct_shape *)current->shape;
        field = find_field(
            shape, location + segment_start, index - segment_start);
        if (field == NULL || !cmeta_data_desc_valid(field->value) ||
            field->value->storage_type == NULL ||
            field->offset > current->storage_type->size ||
            field->value->storage_type->size >
                current->storage_type->size - field->offset ||
            absolute_offset > SIZE_MAX - field->offset)
            return false;
        absolute_offset += field->offset;
        if (absolute_offset > impl->root->storage_type->size ||
            field->value->storage_type->size >
                impl->root->storage_type->size - absolute_offset)
            return false;
        field_end = absolute_offset + field->value->storage_type->size;
        if (field_end > impl->root->storage_type->size) return false;
        current = field->value;
        if (at_end) break;
        segment_start = index + 1u;
    }
    *out_value = current;
    *out_destination = (unsigned char *)impl->state + absolute_offset;
    return true;
}

static bool resolve_owned_string(
    const ccxml_cmeta_datamodel_impl *impl,
    const char *location, size_t location_size,
    const cmeta_data_desc **out_value, void **out_destination) {
    const cmeta_data_desc *value = NULL;
    void *destination = NULL;
    if (!resolve_value(
            impl, location, location_size, &value, &destination) ||
        value->kind != CMETA_DATA_STRING ||
        cmeta_data_buffer_ops_of(value) == NULL ||
        cmeta_data_buffer_ops_of(value)->ownership !=
            CMETA_DATA_BUFFER_OWNED ||
        cmeta_type_require_traits(
            value->storage_type,
            CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY) != CMETA_OK)
        return false;
    *out_value = value;
    *out_destination = destination;
    return true;
}

static bool resolve_readable_string(
    const ccxml_cmeta_datamodel_impl *impl,
    const char *location, size_t location_size,
    const cmeta_data_desc **out_value, void **out_source) {
    const cmeta_data_desc *value = NULL;
    const cmeta_data_buffer_ops *ops;
    void *source = NULL;
    const size_t read_size =
        offsetof(cmeta_data_buffer_ops, read) + sizeof(ops->read);
    if (!resolve_value(
            impl, location, location_size, &value, &source) ||
        value->kind != CMETA_DATA_STRING)
        return false;
    ops = cmeta_data_buffer_ops_of(value);
    if (ops == NULL || ops->struct_size < read_size || ops->read == NULL)
        return false;
    *out_value = value;
    *out_source = source;
    return true;
}

static scxml_adapter_status cmeta_validate_string_location(
    void *user, const char *location, size_t location_size,
    const char **out_error) {
    const ccxml_cmeta_datamodel *datamodel =
        (const ccxml_cmeta_datamodel *)user;
    const ccxml_cmeta_datamodel_impl *impl = datamodel != NULL
        ? (const ccxml_cmeta_datamodel_impl *)datamodel->impl : NULL;
    const cmeta_data_desc *value = NULL;
    void *destination = NULL;
    set_error(out_error, NULL);
    if (!resolve_owned_string(
            impl, location, location_size, &value, &destination)) {
        set_error(
            out_error,
            "CCXML CMeta location is not a writable owned string");
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    (void)value;
    (void)destination;
    return SCXML_ADAPTER_ACCEPTED;
}

static void assignment_ticket_discard(void *user) {
    ccxml_cmeta_assignment_ticket *ticket =
        (ccxml_cmeta_assignment_ticket *)user;
    if (ticket == NULL) return;
    ticket->traits->destroy(ticket->replacement);
    free(ticket->allocation);
    free(ticket);
}

static void assignment_ticket_commit(void *user) {
    ccxml_cmeta_assignment_ticket *ticket =
        (ccxml_cmeta_assignment_ticket *)user;
    if (ticket == NULL) return;
    ticket->buffer_ops->restore_zero(ticket->destination);
    ticket->traits->move_construct(
        ticket->destination, ticket->replacement);
    ticket->traits->destroy(ticket->replacement);
    free(ticket->allocation);
    free(ticket);
}

static scxml_adapter_status cmeta_prepare_assign_string(
    void *user, const char *location, size_t location_size,
    const char *value_bytes, size_t value_size,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    const ccxml_cmeta_datamodel *datamodel =
        (const ccxml_cmeta_datamodel *)user;
    const ccxml_cmeta_datamodel_impl *impl = datamodel != NULL
        ? (const ccxml_cmeta_datamodel_impl *)datamodel->impl : NULL;
    const cmeta_data_desc *value = NULL;
    ccxml_cmeta_assignment_ticket *ticket;
    void *destination = NULL;
    uintptr_t raw_address;
    uintptr_t aligned_address;
    size_t allocation_size;
    cmeta_status status;
    if (out_ticket != NULL)
        *out_ticket = (cflow_statechart_effect_ticket){0};
    set_error(out_error, NULL);
    if (out_ticket == NULL || (value_bytes == NULL && value_size != 0u) ||
        !resolve_owned_string(
            impl, location, location_size, &value, &destination)) {
        set_error(out_error, "invalid CCXML CMeta string assignment");
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    if (value_size > impl->max_string_bytes) {
        set_error(out_error, "CCXML CMeta string write exceeds its bound");
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    if (value->storage_type->align == 0u ||
        value->storage_type->size >
            SIZE_MAX - (value->storage_type->align - 1u)) {
        set_error(out_error, "invalid CMeta string storage alignment");
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    allocation_size = value->storage_type->size +
        value->storage_type->align - 1u;
    ticket = (ccxml_cmeta_assignment_ticket *)calloc(1u, sizeof(*ticket));
    if (ticket == NULL) {
        set_error(out_error, "CCXML CMeta assignment ticket allocation failed");
        return SCXML_ADAPTER_FULL;
    }
    ticket->allocation = malloc(allocation_size);
    if (ticket->allocation == NULL) {
        free(ticket);
        set_error(out_error, "CCXML CMeta replacement allocation failed");
        return SCXML_ADAPTER_FULL;
    }
    raw_address = (uintptr_t)ticket->allocation;
    if (raw_address > UINTPTR_MAX - (value->storage_type->align - 1u)) {
        free(ticket->allocation);
        free(ticket);
        set_error(out_error, "CMeta replacement address cannot be aligned");
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    aligned_address = raw_address;
    if (aligned_address % value->storage_type->align != 0u) {
        aligned_address += value->storage_type->align -
            aligned_address % value->storage_type->align;
    }
    ticket->buffer_ops = cmeta_data_buffer_ops_of(value);
    ticket->traits = value->storage_type->traits;
    ticket->destination = destination;
    ticket->replacement = (void *)aligned_address;
    memset(ticket->replacement, 0, value->storage_type->size);
    ticket->buffer_ops->restore_zero(ticket->replacement);
    status = cmeta_data_buffer_assign(
        value, ticket->replacement,
        (const unsigned char *)value_bytes, value_size,
        impl->max_string_bytes);
    if (status != CMETA_OK) {
        assignment_ticket_discard(ticket);
        set_error(out_error, "CCXML CMeta string staging failed");
        return status == CMETA_OUT_OF_MEMORY
            ? SCXML_ADAPTER_FULL : SCXML_ADAPTER_ERROR_EXECUTION;
    }
    *out_ticket = (cflow_statechart_effect_ticket){
        .commit = assignment_ticket_commit,
        .discard = assignment_ticket_discard,
        .user = ticket};
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status cmeta_validate_readable_string_location(
    void *user, const char *location, size_t location_size,
    const char **out_error) {
    const ccxml_cmeta_datamodel *datamodel =
        (const ccxml_cmeta_datamodel *)user;
    const ccxml_cmeta_datamodel_impl *impl = datamodel != NULL
        ? (const ccxml_cmeta_datamodel_impl *)datamodel->impl : NULL;
    const cmeta_data_desc *value = NULL;
    void *source = NULL;
    set_error(out_error, NULL);
    if (!resolve_readable_string(
            impl, location, location_size, &value, &source)) {
        set_error(
            out_error,
            "CCXML CMeta location is not a readable string");
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    (void)value;
    (void)source;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status cmeta_read_string(
    void *user, const char *location, size_t location_size,
    ccxml_string_view *out_value, const char **out_error) {
    const ccxml_cmeta_datamodel *datamodel =
        (const ccxml_cmeta_datamodel *)user;
    const ccxml_cmeta_datamodel_impl *impl = datamodel != NULL
        ? (const ccxml_cmeta_datamodel_impl *)datamodel->impl : NULL;
    const cmeta_data_desc *value = NULL;
    void *source = NULL;
    const unsigned char *bytes = NULL;
    size_t size = 0u;
    cmeta_status status;
    if (out_value != NULL) *out_value = (ccxml_string_view){0};
    set_error(out_error, NULL);
    if (out_value == NULL ||
        !resolve_readable_string(
            impl, location, location_size, &value, &source)) {
        set_error(out_error, "invalid CCXML CMeta string read");
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    status = cmeta_data_buffer_read(
        value, source, impl->max_string_bytes, &bytes, &size);
    if (status != CMETA_OK) {
        set_error(out_error, "CCXML CMeta string read failed");
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    if (bytes == NULL || size == 0u || memchr(bytes, '\0', size) != NULL) {
        set_error(out_error, "CCXML CMeta string value is not a valid ID");
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    *out_value = (ccxml_string_view){
        .data = (const char *)bytes,
        .size = size};
    return SCXML_ADAPTER_ACCEPTED;
}

static bool cmeta_payload_location_supported(
    const cmeta_data_desc *value) {
    const cmeta_data_buffer_ops *ops;
    const size_t read_size =
        offsetof(cmeta_data_buffer_ops, read) + sizeof(ops->read);
    if (value == NULL || !cmeta_data_desc_valid(value) ||
        value->storage_type == NULL)
        return false;
    if (value->kind == CMETA_DATA_BOOL)
        return value->storage_type->size == sizeof(bool);
    if (value->kind == CMETA_DATA_SINT ||
        value->kind == CMETA_DATA_UINT) {
        const uint8_t bits =
            ((const cmeta_data_integer_shape *)value->shape)->bits;
        return bits % CHAR_BIT == 0u &&
               value->storage_type->size == (size_t)bits / CHAR_BIT;
    }
    if (value->kind == CMETA_DATA_FLOAT) {
        const uint8_t bits =
            ((const cmeta_data_float_shape *)value->shape)->bits;
        return bits % CHAR_BIT == 0u &&
               value->storage_type->size == (size_t)bits / CHAR_BIT;
    }
    if (value->kind == CMETA_DATA_STRING) {
        ops = cmeta_data_buffer_ops_of(value);
        return ops != NULL && ops->struct_size >= read_size &&
               ops->read != NULL;
    }
    if (value->kind == CMETA_DATA_ENUM)
        return cmeta_data_enum_ops_of(value) != NULL;
    return true;
}

static scxml_adapter_status cmeta_validate_payload_location(
    void *user, const char *location, size_t location_size,
    const char **out_error) {
    const ccxml_cmeta_datamodel *datamodel =
        (const ccxml_cmeta_datamodel *)user;
    const ccxml_cmeta_datamodel_impl *impl = datamodel != NULL
        ? (const ccxml_cmeta_datamodel_impl *)datamodel->impl : NULL;
    const cmeta_data_desc *value = NULL;
    void *source = NULL;
    set_error(out_error, NULL);
    if (!resolve_value(
            impl, location, location_size, &value, &source) ||
        !cmeta_payload_location_supported(value)) {
        set_error(out_error, "CCXML CMeta payload location is not readable");
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    (void)source;
    return SCXML_ADAPTER_ACCEPTED;
}

static bool cmeta_read_integer_payload(
    const cmeta_data_desc *value, const void *source,
    scxml_payload_value *out) {
    const uint8_t bits =
        ((const cmeta_data_integer_shape *)value->shape)->bits;
    if (value->kind == CMETA_DATA_SINT) {
        out->kind = SCXML_PAYLOAD_VALUE_SINT;
        switch (bits) {
            case 8: {
                int8_t item;
                memcpy(&item, source, sizeof(item));
                out->data.sint = item;
                return true;
            }
            case 16: {
                int16_t item;
                memcpy(&item, source, sizeof(item));
                out->data.sint = item;
                return true;
            }
            case 32: {
                int32_t item;
                memcpy(&item, source, sizeof(item));
                out->data.sint = item;
                return true;
            }
            case 64:
                memcpy(&out->data.sint, source, sizeof(out->data.sint));
                return true;
            default: return false;
        }
    }
    out->kind = SCXML_PAYLOAD_VALUE_UINT;
    switch (bits) {
        case 8: {
            uint8_t item;
            memcpy(&item, source, sizeof(item));
            out->data.uint = item;
            return true;
        }
        case 16: {
            uint16_t item;
            memcpy(&item, source, sizeof(item));
            out->data.uint = item;
            return true;
        }
        case 32: {
            uint32_t item;
            memcpy(&item, source, sizeof(item));
            out->data.uint = item;
            return true;
        }
        case 64:
            memcpy(&out->data.uint, source, sizeof(out->data.uint));
            return true;
        default: return false;
    }
}

static scxml_adapter_status cmeta_read_payload(
    void *user, const char *location, size_t location_size,
    scxml_content_view *out_value, const char **out_error) {
    const ccxml_cmeta_datamodel *datamodel =
        (const ccxml_cmeta_datamodel *)user;
    const ccxml_cmeta_datamodel_impl *impl = datamodel != NULL
        ? (const ccxml_cmeta_datamodel_impl *)datamodel->impl : NULL;
    const cmeta_data_desc *value = NULL;
    void *source = NULL;
    scxml_payload_value scalar = {0};
    set_error(out_error, NULL);
    if (out_value != NULL) *out_value = (scxml_content_view){0};
    if (out_value == NULL ||
        !resolve_value(
            impl, location, location_size, &value, &source) ||
        !cmeta_payload_location_supported(value)) {
        set_error(out_error, "invalid CCXML CMeta payload read");
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    switch (value->kind) {
        case CMETA_DATA_BOOL:
            scalar.kind = SCXML_PAYLOAD_VALUE_BOOL;
            memcpy(&scalar.data.boolean, source, sizeof(bool));
            break;
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT:
            if (!cmeta_read_integer_payload(value, source, &scalar)) {
                set_error(out_error, "CCXML CMeta integer payload read failed");
                return SCXML_ADAPTER_ERROR_EXECUTION;
            }
            break;
        case CMETA_DATA_FLOAT: {
            const uint8_t bits =
                ((const cmeta_data_float_shape *)value->shape)->bits;
            scalar.kind = SCXML_PAYLOAD_VALUE_FLOAT;
            if (bits == 32u) {
                float item;
                memcpy(&item, source, sizeof(item));
                scalar.data.number = item;
            } else if (bits == 64u) {
                memcpy(
                    &scalar.data.number, source,
                    sizeof(scalar.data.number));
            } else {
                set_error(out_error, "CCXML CMeta float payload read failed");
                return SCXML_ADAPTER_ERROR_EXECUTION;
            }
            break;
        }
        case CMETA_DATA_ENUM:
            scalar.kind = SCXML_PAYLOAD_VALUE_SINT;
            if (cmeta_data_enum_read(
                    value, source, &scalar.data.sint) != CMETA_OK) {
                set_error(out_error, "CCXML CMeta enum payload read failed");
                return SCXML_ADAPTER_ERROR_EXECUTION;
            }
            break;
        case CMETA_DATA_STRING: {
            const unsigned char *bytes = NULL;
            size_t size = 0u;
            if (cmeta_data_buffer_read(
                    value, source, impl->max_string_bytes,
                    &bytes, &size) != CMETA_OK) {
                set_error(out_error, "CCXML CMeta string payload read failed");
                return SCXML_ADAPTER_ERROR_EXECUTION;
            }
            scalar.kind = SCXML_PAYLOAD_VALUE_STRING;
            scalar.data.string.data = (const char *)bytes;
            scalar.data.string.size = size;
            break;
        }
        default:
            *out_value = (scxml_content_view){
                .kind = SCXML_CONTENT_CMETA,
                .schema = value,
                .object = source};
            return SCXML_ADAPTER_ACCEPTED;
    }
    *out_value = (scxml_content_view){
        .kind = SCXML_CONTENT_SCALAR,
        .scalar = scalar};
    return SCXML_ADAPTER_ACCEPTED;
}

static bool reject_active_state(
    void *user, const char *name, size_t name_size,
    cflow_machine_state_id *out_state) {
    (void)user;
    (void)name;
    (void)name_size;
    (void)out_state;
    return false;
}

static bool no_active_state(
    void *user, cflow_machine_state_id state, bool *out_active) {
    (void)user;
    (void)state;
    if (out_active == NULL) return false;
    *out_active = false;
    return true;
}

static scxml_adapter_status map_expression_status(
    scxml_expr_status status) {
    if (status == SCXML_EXPR_OK) return SCXML_ADAPTER_ACCEPTED;
    if (status == SCXML_EXPR_ALLOCATION_FAILED)
        return SCXML_ADAPTER_FULL;
    if (status == SCXML_EXPR_INVALID_ARGUMENT)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    return SCXML_ADAPTER_ERROR_EXECUTION;
}

scxml_adapter_status ccxml_cmeta_compile_condition_with_scope(
    void *user, const char *source, size_t source_size,
    const scxml_scope_schema *scope,
    ccxml_condition *out_condition, const char **out_error) {
    const ccxml_cmeta_datamodel *datamodel =
        (const ccxml_cmeta_datamodel *)user;
    const ccxml_cmeta_datamodel_impl *impl = datamodel != NULL
        ? (const ccxml_cmeta_datamodel_impl *)datamodel->impl : NULL;
    ccxml_cmeta_condition *condition;
    const scxml_expr_compile_policy policy = {
        .allowed_system_operands = SCXML_EXPR_SYSTEM_EVENT_NAME};
    scxml_expr_limits limits;
    scxml_expr_diagnostic diagnostic = {0};
    scxml_expr_status status;
    set_error(out_error, NULL);
    if (impl == NULL || source == NULL || source_size == 0u ||
        out_condition == NULL || out_condition->impl != NULL) {
        set_error(out_error, "invalid CCXML CMeta condition compile");
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    condition = (ccxml_cmeta_condition *)calloc(1u, sizeof(*condition));
    if (condition == NULL) {
        set_error(out_error, "CCXML CMeta condition allocation failed");
        return SCXML_ADAPTER_FULL;
    }
    limits = scxml_expr_default_limits();
    limits.max_path_depth = impl->max_path_depth;
    limits.max_string_bytes = impl->max_string_bytes;
    status = scxml_expr_compile_with_scope_policy(
        &condition->program, source, source_size, impl->root, scope,
        reject_active_state, NULL, &policy, &limits, &diagnostic);
    if (status != SCXML_EXPR_OK) {
        scxml_expr_program_destroy(&condition->program);
        free(condition);
        set_error(out_error, "CCXML CMeta condition is invalid");
        return map_expression_status(status);
    }
    out_condition->impl = condition;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status cmeta_compile_condition(
    void *user, const char *source, size_t source_size,
    ccxml_condition *out_condition, const char **out_error) {
    return ccxml_cmeta_compile_condition_with_scope(
        user, source, source_size, NULL, out_condition, out_error);
}

static const cmeta_data_desc *find_semantic_data_for_type(
    const ccxml_cmeta_datamodel_impl *impl,
    const cmeta_type_desc *type) {
    size_t index;
    if (impl == NULL || type == NULL) return NULL;
    for (index = 0u; index < impl->semantic_data_count; ++index)
        if (cmeta_type_equal(
                impl->semantic_data[index]->storage_type, type))
            return impl->semantic_data[index];
    return scxml_scope_find_data_for_type(
        impl->root, type, impl->max_path_depth);
}

scxml_adapter_status ccxml_cmeta_compile_foreach_scope(
    void *user, const char *array, size_t array_size,
    const char *item, size_t item_size,
    const char *index_or_null, size_t index_size,
    size_t max_iterations,
    scxml_scope_schema *scope, scxml_foreach_program *out_program,
    const char **out_error) {
    const ccxml_cmeta_datamodel *datamodel =
        (const ccxml_cmeta_datamodel *)user;
    const ccxml_cmeta_datamodel_impl *impl = datamodel != NULL
        ? (const ccxml_cmeta_datamodel_impl *)datamodel->impl : NULL;
    scxml_sequence_program sequence = {0};
    scxml_expr_diagnostic diagnostic = {0};
    const cmeta_data_desc *item_data;
    size_t item_slot = SIZE_MAX;
    bool conflict = false;
    scxml_expr_status status;
    set_error(out_error, NULL);
    if (impl == NULL || scope == NULL || out_program == NULL ||
        out_program->sequence.root != NULL || array == NULL ||
        array_size == 0u || item == NULL || item_size == 0u ||
        (index_or_null == NULL) != (index_size == 0u) ||
        max_iterations == 0u) {
        set_error(out_error, "invalid CCXML CMeta foreach scope request");
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    status = scxml_sequence_compile(
        &sequence, array, array_size, impl->root, impl->max_path_depth,
        &diagnostic);
    if (status != SCXML_EXPR_OK) {
        set_error(out_error, "CCXML foreach array is not a CMeta sequence");
        return map_expression_status(status);
    }
    if (cmeta_type_require_traits(
            sequence.element_type,
            CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY) !=
        CMETA_OK) {
        set_error(
            out_error,
            "CCXML foreach requires a bounded trivial-storage element");
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    item_data = find_semantic_data_for_type(impl, sequence.element_type);
    if (item_data == NULL) {
        set_error(
            out_error,
            "CCXML foreach element has no semantic CMeta data descriptor");
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    if (item_data == NULL || !scxml_scope_register(
            scope, item, item_size, item_data, &item_slot, &conflict)) {
        set_error(out_error, conflict
            ? "CCXML foreach item conflicts with the typed scope"
            : "CCXML foreach item type has no CMeta data descriptor");
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    if (index_or_null != NULL && !scxml_scope_register(
            scope, index_or_null, index_size, &cmeta_data_size,
            &item_slot, &conflict)) {
        set_error(out_error, conflict
            ? "CCXML foreach index conflicts with the typed scope"
            : "CCXML foreach index could not be registered");
        return conflict
            ? SCXML_ADAPTER_INVALID_CONTRACT
            : SCXML_ADAPTER_ERROR_EXECUTION;
    }
    status = scxml_foreach_compile_with_scope(
        out_program, array, array_size, item, item_size,
        index_or_null, index_size,
        impl->root, scope, impl->max_path_depth, max_iterations, &diagnostic);
    if (status != SCXML_EXPR_OK) {
        set_error(out_error, "CCXML CMeta foreach compilation failed");
        return map_expression_status(status);
    }
    if (out_program->item.kind != SCXML_LOCATION_SUPPLEMENTAL) {
        memset(out_program, 0, sizeof(*out_program));
        set_error(
            out_error,
            "CCXML foreach item conflicts with an application root location");
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    if (index_or_null != NULL &&
        out_program->index.kind != SCXML_LOCATION_SUPPLEMENTAL) {
        memset(out_program, 0, sizeof(*out_program));
        set_error(
            out_error,
            "CCXML foreach index conflicts with an application root location");
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    return SCXML_ADAPTER_ACCEPTED;
}

bool ccxml_cmeta_datamodel_state(void *user, void **out_state) {
    const ccxml_cmeta_datamodel *datamodel =
        (const ccxml_cmeta_datamodel *)user;
    const ccxml_cmeta_datamodel_impl *impl = datamodel != NULL
        ? (const ccxml_cmeta_datamodel_impl *)datamodel->impl : NULL;
    if (out_state != NULL) *out_state = NULL;
    if (impl == NULL || out_state == NULL || impl->state == NULL)
        return false;
    *out_state = impl->state;
    return true;
}

scxml_adapter_status ccxml_cmeta_evaluate_condition_with_scope(
    void *user, const ccxml_condition *condition,
    const ccxml_event *event, scxml_scope_view *scope, bool *out_value,
    const char **out_error) {
    const ccxml_cmeta_datamodel *datamodel =
        (const ccxml_cmeta_datamodel *)user;
    const ccxml_cmeta_datamodel_impl *impl = datamodel != NULL
        ? (const ccxml_cmeta_datamodel_impl *)datamodel->impl : NULL;
    const ccxml_cmeta_condition *compiled = condition != NULL
        ? (const ccxml_cmeta_condition *)condition->impl : NULL;
    scxml_expr_diagnostic diagnostic = {0};
    scxml_expr_system_values system_values = {0};
    scxml_expr_status status;
    set_error(out_error, NULL);
    if (impl == NULL || compiled == NULL || event == NULL ||
        event->name == NULL || event->name_size == 0u || out_value == NULL) {
        set_error(out_error, "invalid CCXML CMeta condition evaluation");
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    system_values.event_name = (scxml_expr_string_view){
        .data = event->name, .size = event->name_size};
    system_values.supplemental = scope;
    status = scxml_expr_evaluate_with_system(
        &compiled->program, impl->state, no_active_state, NULL,
        &system_values, out_value, &diagnostic);
    if (status != SCXML_EXPR_OK) {
        set_error(out_error, "CCXML CMeta condition failed");
    }
    return map_expression_status(status);
}

static scxml_adapter_status cmeta_evaluate_condition(
    void *user, const ccxml_condition *condition,
    const ccxml_event *event, bool *out_value,
    const char **out_error) {
    return ccxml_cmeta_evaluate_condition_with_scope(
        user, condition, event, NULL, out_value, out_error);
}

static void cmeta_destroy_condition(
    void *user, ccxml_condition *condition) {
    ccxml_cmeta_condition *compiled = condition != NULL
        ? (ccxml_cmeta_condition *)condition->impl : NULL;
    (void)user;
    if (compiled == NULL) return;
    scxml_expr_program_destroy(&compiled->program);
    free(compiled);
    condition->impl = NULL;
}

scxml_adapter_status ccxml_cmeta_compile_string_expression_with_scope(
    void *user, const char *source, size_t source_size,
    const scxml_scope_schema *scope,
    ccxml_string_expression *out_expression, const char **out_error) {
    const ccxml_cmeta_datamodel *datamodel =
        (const ccxml_cmeta_datamodel *)user;
    const ccxml_cmeta_datamodel_impl *impl = datamodel != NULL
        ? (const ccxml_cmeta_datamodel_impl *)datamodel->impl : NULL;
    ccxml_cmeta_string_expression *expression;
    const scxml_expr_compile_policy policy = {
        .allowed_system_operands = SCXML_EXPR_SYSTEM_EVENT_NAME};
    scxml_expr_limits limits;
    scxml_expr_diagnostic diagnostic = {0};
    scxml_expr_status status;
    set_error(out_error, NULL);
    if (impl == NULL || source == NULL || source_size == 0u ||
        out_expression == NULL || out_expression->impl != NULL) {
        set_error(out_error, "invalid CCXML CMeta string expression compile");
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    expression = (ccxml_cmeta_string_expression *)calloc(
        1u, sizeof(*expression));
    if (expression == NULL) {
        set_error(out_error, "CCXML CMeta string expression allocation failed");
        return SCXML_ADAPTER_FULL;
    }
    limits = scxml_expr_default_limits();
    limits.max_path_depth = impl->max_path_depth;
    limits.max_string_bytes = impl->max_string_bytes;
    status = scxml_expr_compile_value_with_scope_and_policy(
        &expression->program, source, source_size, impl->root, scope,
        reject_active_state, NULL, &policy, &limits, &diagnostic);
    if (status != SCXML_EXPR_OK ||
        scxml_expr_program_value_kind(&expression->program) !=
            SCXML_EXPR_VALUE_STRING) {
        scxml_expr_program_destroy(&expression->program);
        free(expression);
        set_error(out_error, status == SCXML_EXPR_OK
            ? "CCXML CMeta expression must produce a string"
            : "CCXML CMeta string expression is invalid");
        return status == SCXML_EXPR_OK
            ? SCXML_ADAPTER_INVALID_CONTRACT
            : map_expression_status(status);
    }
    out_expression->impl = expression;
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status cmeta_compile_string_expression(
    void *user, const char *source, size_t source_size,
    ccxml_string_expression *out_expression, const char **out_error) {
    return ccxml_cmeta_compile_string_expression_with_scope(
        user, source, source_size, NULL, out_expression, out_error);
}

scxml_adapter_status ccxml_cmeta_evaluate_string_expression_with_scope(
    void *user, const ccxml_string_expression *expression,
    const ccxml_event *event, scxml_scope_view *scope,
    ccxml_string_view *out_value, const char **out_error) {
    const ccxml_cmeta_datamodel *datamodel =
        (const ccxml_cmeta_datamodel *)user;
    const ccxml_cmeta_datamodel_impl *impl = datamodel != NULL
        ? (const ccxml_cmeta_datamodel_impl *)datamodel->impl : NULL;
    const ccxml_cmeta_string_expression *compiled = expression != NULL
        ? (const ccxml_cmeta_string_expression *)expression->impl : NULL;
    scxml_expr_diagnostic diagnostic = {0};
    scxml_expr_system_values system_values = {0};
    scxml_expr_value value = {0};
    scxml_expr_status status;
    set_error(out_error, NULL);
    if (out_value != NULL) *out_value = (ccxml_string_view){0};
    if (impl == NULL || compiled == NULL || event == NULL ||
        event->name == NULL || event->name_size == 0u || out_value == NULL) {
        set_error(out_error, "invalid CCXML CMeta string expression evaluation");
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    system_values.event_name = (scxml_expr_string_view){
        .data = event->name, .size = event->name_size};
    system_values.supplemental = scope;
    status = scxml_expr_evaluate_value_with_system(
        &compiled->program, impl->state, no_active_state, NULL,
        &system_values, &value, &diagnostic);
    if (status != SCXML_EXPR_OK) {
        set_error(out_error, "CCXML CMeta string expression failed");
        return map_expression_status(status);
    }
    if (value.kind != SCXML_EXPR_VALUE_STRING ||
        value.data.string.data == NULL || value.data.string.size == 0u ||
        value.data.string.size > impl->max_string_bytes ||
        memchr(
            value.data.string.data, '\0', value.data.string.size) != NULL) {
        set_error(out_error, "CCXML CMeta string expression result is invalid");
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }
    *out_value = (ccxml_string_view){
        .data = value.data.string.data,
        .size = value.data.string.size};
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status cmeta_evaluate_string_expression(
    void *user, const ccxml_string_expression *expression,
    const ccxml_event *event, ccxml_string_view *out_value,
    const char **out_error) {
    return ccxml_cmeta_evaluate_string_expression_with_scope(
        user, expression, event, NULL, out_value, out_error);
}

static void cmeta_destroy_string_expression(
    void *user, ccxml_string_expression *expression) {
    ccxml_cmeta_string_expression *compiled = expression != NULL
        ? (ccxml_cmeta_string_expression *)expression->impl : NULL;
    (void)user;
    if (compiled == NULL) return;
    scxml_expr_program_destroy(&compiled->program);
    free(compiled);
    expression->impl = NULL;
}

static const ccxml_datamodel_adapter_v1 cmeta_adapter = {
    .abi_version = CCXML_DATAMODEL_ADAPTER_ABI_V1,
    .struct_size = sizeof(ccxml_datamodel_adapter_v1),
    .validate_string_location = cmeta_validate_string_location,
    .prepare_assign_string = cmeta_prepare_assign_string,
    .validate_readable_string_location =
        cmeta_validate_readable_string_location,
    .read_string = cmeta_read_string,
    .compile_condition = cmeta_compile_condition,
    .evaluate_condition = cmeta_evaluate_condition,
    .destroy_condition = cmeta_destroy_condition,
    .validate_payload_location = cmeta_validate_payload_location,
    .read_payload = cmeta_read_payload,
    .compile_string_expression = cmeta_compile_string_expression,
    .evaluate_string_expression = cmeta_evaluate_string_expression,
    .destroy_string_expression = cmeta_destroy_string_expression};

static bool semantic_data_registry_valid(
    const cmeta_data_desc *const *semantic_data,
    size_t semantic_data_count) {
    size_t index;
    size_t other;
    if ((semantic_data == NULL) != (semantic_data_count == 0u) ||
        semantic_data_count > SIZE_MAX / sizeof(*semantic_data))
        return false;
    for (index = 0u; index < semantic_data_count; ++index) {
        const cmeta_data_desc *descriptor = semantic_data[index];
        if (!cmeta_data_desc_valid(descriptor) ||
            descriptor->storage_type == NULL)
            return false;
        for (other = 0u; other < index; ++other)
            if (cmeta_type_equal(
                    descriptor->storage_type,
                    semantic_data[other]->storage_type))
                return false;
    }
    return true;
}

ccxml_status ccxml_cmeta_datamodel_init(
    ccxml_cmeta_datamodel *datamodel,
    const ccxml_cmeta_datamodel_config_v1 *config) {
    const size_t legacy_struct_size = offsetof(
        ccxml_cmeta_datamodel_config_v1, semantic_data);
    const cmeta_data_desc *const *semantic_data = NULL;
    size_t semantic_data_count = 0u;
    ccxml_cmeta_datamodel_impl *impl;
    if (datamodel == NULL || datamodel->impl != NULL || config == NULL ||
        config->abi_version != CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1 ||
        (config->struct_size != legacy_struct_size &&
         config->struct_size < sizeof(*config)) ||
        config->root == NULL ||
        config->state == NULL || config->max_path_depth == 0u ||
        config->max_string_bytes == 0u ||
        !cmeta_data_desc_valid(config->root) ||
        config->root->kind != CMETA_DATA_STRUCT ||
        config->root->storage_type == NULL ||
        config->root->storage_type->align == 0u ||
        (uintptr_t)config->state % config->root->storage_type->align != 0u)
        return CCXML_INVALID_ARGUMENT;
    if (config->struct_size >= sizeof(*config)) {
        semantic_data = config->semantic_data;
        semantic_data_count = config->semantic_data_count;
    }
    if (!semantic_data_registry_valid(
            semantic_data, semantic_data_count))
        return CCXML_INVALID_ARGUMENT;
    impl = (ccxml_cmeta_datamodel_impl *)malloc(sizeof(*impl));
    if (impl == NULL) return CCXML_ALLOCATION_FAILED;
    *impl = (ccxml_cmeta_datamodel_impl){
        .root = config->root,
        .state = config->state,
        .semantic_data_count = semantic_data_count,
        .max_path_depth = config->max_path_depth,
        .max_string_bytes = config->max_string_bytes};
    if (semantic_data_count != 0u) {
        impl->semantic_data = (const cmeta_data_desc **)malloc(
            semantic_data_count * sizeof(*impl->semantic_data));
        if (impl->semantic_data == NULL) {
            free(impl);
            return CCXML_ALLOCATION_FAILED;
        }
        memcpy(
            impl->semantic_data, semantic_data,
            semantic_data_count * sizeof(*impl->semantic_data));
    }
    datamodel->impl = impl;
    return CCXML_OK;
}

const ccxml_datamodel_adapter_v1 *ccxml_cmeta_datamodel_adapter(void) {
    return &cmeta_adapter;
}

void ccxml_cmeta_datamodel_destroy(ccxml_cmeta_datamodel *datamodel) {
    ccxml_cmeta_datamodel_impl *impl;
    if (datamodel == NULL) return;
    impl = (ccxml_cmeta_datamodel_impl *)datamodel->impl;
    if (impl == NULL) return;
    free(impl->semantic_data);
    free(impl);
    datamodel->impl = NULL;
}
