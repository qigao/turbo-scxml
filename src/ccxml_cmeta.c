#include <ccxml/ccxml.h>

#include "scxml_expr.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct ccxml_cmeta_datamodel_impl {
    const cmeta_data_desc *root;
    void *state;
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

static scxml_adapter_status cmeta_compile_condition(
    void *user, const char *source, size_t source_size,
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
    status = scxml_expr_compile_with_policy(
        &condition->program, source, source_size, impl->root,
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

static scxml_adapter_status cmeta_evaluate_condition(
    void *user, const ccxml_condition *condition,
    const ccxml_event *event, bool *out_value,
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
    status = scxml_expr_evaluate_with_system(
        &compiled->program, impl->state, no_active_state, NULL,
        &system_values, out_value, &diagnostic);
    if (status != SCXML_EXPR_OK) {
        set_error(out_error, "CCXML CMeta condition failed");
    }
    return map_expression_status(status);
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
    .destroy_condition = cmeta_destroy_condition};

ccxml_status ccxml_cmeta_datamodel_init(
    ccxml_cmeta_datamodel *datamodel,
    const ccxml_cmeta_datamodel_config_v1 *config) {
    ccxml_cmeta_datamodel_impl *impl;
    if (datamodel == NULL || datamodel->impl != NULL || config == NULL ||
        config->abi_version != CCXML_CMETA_DATAMODEL_CONFIG_ABI_V1 ||
        config->struct_size < sizeof(*config) || config->root == NULL ||
        config->state == NULL || config->max_path_depth == 0u ||
        config->max_string_bytes == 0u ||
        !cmeta_data_desc_valid(config->root) ||
        config->root->kind != CMETA_DATA_STRUCT ||
        config->root->storage_type == NULL ||
        config->root->storage_type->align == 0u ||
        (uintptr_t)config->state % config->root->storage_type->align != 0u)
        return CCXML_INVALID_ARGUMENT;
    impl = (ccxml_cmeta_datamodel_impl *)malloc(sizeof(*impl));
    if (impl == NULL) return CCXML_ALLOCATION_FAILED;
    *impl = (ccxml_cmeta_datamodel_impl){
        .root = config->root,
        .state = config->state,
        .max_path_depth = config->max_path_depth,
        .max_string_bytes = config->max_string_bytes};
    datamodel->impl = impl;
    return CCXML_OK;
}

const ccxml_datamodel_adapter_v1 *ccxml_cmeta_datamodel_adapter(void) {
    return &cmeta_adapter;
}

void ccxml_cmeta_datamodel_destroy(ccxml_cmeta_datamodel *datamodel) {
    if (datamodel == NULL) return;
    free(datamodel->impl);
    datamodel->impl = NULL;
}
