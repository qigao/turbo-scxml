#include "scxml_foreach.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static scxml_expr_status foreach_report(
    scxml_expr_diagnostic *diagnostic,
    scxml_expr_status status, const char *message) {
    if (diagnostic != NULL) {
        memset(diagnostic, 0, sizeof(*diagnostic));
        diagnostic->status = status;
        if (message != NULL)
            (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                           "%s", message);
    }
    return status;
}

scxml_expr_status scxml_foreach_compile(
    scxml_foreach_program *out,
    const char *array, size_t array_size,
    const char *item, size_t item_size,
    const char *index_or_null, size_t index_size,
    const cmeta_data_desc *root, size_t max_path_depth,
    size_t max_iterations,
    scxml_expr_diagnostic *diagnostic) {
    scxml_foreach_program compiled = {0};
    const cmeta_type_desc *element_type;
    const cmeta_trait_flags lifecycle_traits =
        CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY;
    scxml_expr_status status;
    if (out == NULL || out->sequence.root != NULL || array == NULL ||
        array_size == 0u || item == NULL || item_size == 0u ||
        (index_or_null == NULL) != (index_size == 0u) ||
        max_path_depth == 0u || max_iterations == 0u)
        return foreach_report(diagnostic,
                              SCXML_EXPR_INVALID_ARGUMENT,
                              "invalid CMeta foreach compile arguments");
    status = scxml_sequence_compile(
        &compiled.sequence, array, array_size, root, max_path_depth,
        diagnostic);
    if (status != SCXML_EXPR_OK) return status;
    status = scxml_location_compile(
        &compiled.item, item, item_size, root, max_path_depth, true,
        diagnostic);
    if (status != SCXML_EXPR_OK && status != SCXML_EXPR_UNKNOWN_LOCATION)
        return status;
    element_type = compiled.sequence.element_type;
    compiled.item_location_valid = status == SCXML_EXPR_OK;
    if (compiled.item_location_valid &&
        (!cmeta_type_equal(compiled.item.value->storage_type, element_type) ||
         compiled.item.storage_size != element_type->size))
        return foreach_report(
            diagnostic, SCXML_EXPR_TYPE_MISMATCH,
            "CMeta foreach item requires the exact element type");
    if (cmeta_type_require_traits(
            element_type,
            CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY) ==
        CMETA_OK) {
        compiled.managed_item = false;
    } else if (cmeta_type_require_traits(element_type, lifecycle_traits) ==
               CMETA_OK) {
        compiled.managed_item = true;
    } else {
        return foreach_report(
            diagnostic, SCXML_EXPR_TYPE_MISMATCH,
            "CMeta foreach element requires trivial storage or complete "
            "copy, move, and destroy traits");
    }
    if (index_or_null != NULL) {
        status = scxml_location_compile(
            &compiled.index, index_or_null, index_size, root,
            max_path_depth, true, diagnostic);
        if (status != SCXML_EXPR_OK) return status;
        if (compiled.index.value->kind != CMETA_DATA_UINT ||
            !cmeta_type_equal(compiled.index.value->storage_type,
                              &cmeta_type_size) ||
            compiled.index.storage_size != sizeof(size_t))
            return foreach_report(
                diagnostic, SCXML_EXPR_TYPE_MISMATCH,
                "CMeta foreach index requires an exact size_t location");
        compiled.has_index = true;
    }
    compiled.max_iterations = max_iterations;
    *out = compiled;
    return foreach_report(diagnostic, SCXML_EXPR_OK, NULL);
}

scxml_expr_status scxml_foreach_open(
    const scxml_foreach_program *program,
    void *staged_root, scxml_foreach_snapshot *snapshot,
    scxml_expr_diagnostic *diagnostic) {
    cmeta_range range = {0};
    cmeta_range_cursor cursor = {0};
    const cmeta_type_desc *element_type;
    size_t length = 0u;
    size_t stride;
    size_t storage_size;
    size_t allocation_size;
    size_t iteration;
    uintptr_t address;
    uintptr_t aligned;
    scxml_expr_status status;
    if (program == NULL || program->sequence.root == NULL ||
        program->max_iterations == 0u || staged_root == NULL ||
        snapshot == NULL || snapshot->allocation != NULL ||
        snapshot->storage != NULL || snapshot->length != 0u ||
        snapshot->stride != 0u)
        return foreach_report(diagnostic,
                              SCXML_EXPR_INVALID_ARGUMENT,
                              "invalid CMeta foreach open arguments");
    if (!program->item_location_valid)
        return foreach_report(diagnostic,
                              SCXML_EXPR_UNKNOWN_LOCATION,
                              "CMeta foreach item is not a writable location");
    status = scxml_sequence_open(
        &program->sequence, staged_root, &range, &length, diagnostic);
    if (status != SCXML_EXPR_OK) return status;
    if (program->managed_item &&
        (range.flags & CMETA_RANGE_CONSTRUCTS_VALUES) == 0u)
        return foreach_report(
            diagnostic, SCXML_EXPR_TYPE_MISMATCH,
            "CMeta foreach managed Range must construct owned values");
    if (length > program->max_iterations)
        return foreach_report(diagnostic,
                              SCXML_EXPR_LIMIT_EXCEEDED,
                              "CMeta foreach iteration limit exceeded");
    if (length == 0u)
        return foreach_report(diagnostic, SCXML_EXPR_OK, NULL);

    element_type = program->sequence.element_type;
    if (element_type == NULL || element_type->size == 0u ||
        element_type->align == 0u ||
        (element_type->align & (element_type->align - 1u)) != 0u ||
        element_type->size > SIZE_MAX - (element_type->align - 1u))
        return foreach_report(diagnostic,
                              SCXML_EXPR_INVALID_ARGUMENT,
                              "CMeta foreach snapshot element storage is invalid");
    stride = (element_type->size + element_type->align - 1u) &
             ~(element_type->align - 1u);
    if (stride == 0u || length > SIZE_MAX / stride)
        return foreach_report(diagnostic,
                              SCXML_EXPR_LIMIT_EXCEEDED,
                              "CMeta foreach snapshot size overflowed");
    storage_size = length * stride;
    if (storage_size > SIZE_MAX - (element_type->align - 1u))
        return foreach_report(diagnostic,
                              SCXML_EXPR_LIMIT_EXCEEDED,
                              "CMeta foreach snapshot allocation overflowed");
    allocation_size = storage_size + element_type->align - 1u;
    snapshot->allocation = malloc(allocation_size);
    if (snapshot->allocation == NULL)
        return foreach_report(diagnostic,
                              SCXML_EXPR_EVALUATION_ERROR,
                              "CMeta foreach snapshot allocation failed");
    address = (uintptr_t)snapshot->allocation;
    if (address > UINTPTR_MAX - (element_type->align - 1u)) {
        free(snapshot->allocation);
        memset(snapshot, 0, sizeof(*snapshot));
        return foreach_report(diagnostic,
                              SCXML_EXPR_INVALID_ARGUMENT,
                              "CMeta foreach snapshot alignment overflowed");
    }
    aligned = (address + element_type->align - 1u) &
              ~((uintptr_t)element_type->align - 1u);
    snapshot->storage = (void *)aligned;
    snapshot->stride = stride;
    for (iteration = 0u; iteration < length; ++iteration) {
        void *slot = (unsigned char *)snapshot->storage + iteration * stride;
        const cmeta_gen_status generated =
            cmeta_range_next(&range, &cursor, slot);
        if (generated != CMETA_GEN_VALUE &&
            generated != CMETA_GEN_VALUE_AND_DONE) {
            snapshot->length = iteration;
            scxml_foreach_snapshot_destroy(program, snapshot);
            return foreach_report(diagnostic,
                                  SCXML_EXPR_EVALUATION_ERROR,
                                  "CMeta foreach snapshot iteration failed");
        }
        snapshot->length = iteration + 1u;
        if (generated == CMETA_GEN_VALUE_AND_DONE &&
            snapshot->length != length) {
            scxml_foreach_snapshot_destroy(program, snapshot);
            return foreach_report(
                diagnostic, SCXML_EXPR_EVALUATION_ERROR,
                "CMeta foreach Range ended before its sized length");
        }
    }
    return foreach_report(diagnostic, SCXML_EXPR_OK, NULL);
}

void scxml_foreach_snapshot_destroy(
    const scxml_foreach_program *program,
    scxml_foreach_snapshot *snapshot) {
    size_t index;
    if (snapshot == NULL) return;
    if (program != NULL && program->managed_item &&
        program->sequence.element_type != NULL &&
        program->sequence.element_type->traits != NULL &&
        program->sequence.element_type->traits->destroy != NULL &&
        snapshot->storage != NULL && snapshot->stride != 0u) {
        for (index = 0u; index < snapshot->length; ++index)
            program->sequence.element_type->traits->destroy(
                (unsigned char *)snapshot->storage + index * snapshot->stride);
    }
    free(snapshot->allocation);
    memset(snapshot, 0, sizeof(*snapshot));
}

scxml_expr_status scxml_foreach_value_init(
    const scxml_foreach_program *program,
    scxml_foreach_value *value,
    scxml_expr_diagnostic *diagnostic) {
    const cmeta_type_desc *type;
    size_t allocation_size;
    uintptr_t address;
    uintptr_t aligned;
    if (program == NULL || program->sequence.element_type == NULL ||
        value == NULL || value->allocation != NULL || value->storage != NULL ||
        value->live)
        return foreach_report(diagnostic,
                              SCXML_EXPR_INVALID_ARGUMENT,
                              "invalid CMeta foreach scratch arguments");
    if (!program->managed_item)
        return foreach_report(diagnostic, SCXML_EXPR_OK, NULL);

    type = program->sequence.element_type;
    if (type->size == 0u || type->align == 0u ||
        (type->align & (type->align - 1u)) != 0u ||
        type->size > SIZE_MAX - (type->align - 1u))
        return foreach_report(diagnostic,
                              SCXML_EXPR_INVALID_ARGUMENT,
                              "CMeta foreach element storage is invalid");
    allocation_size = type->size + type->align - 1u;
    value->allocation = malloc(allocation_size);
    if (value->allocation == NULL)
        return foreach_report(diagnostic,
                              SCXML_EXPR_EVALUATION_ERROR,
                              "CMeta foreach scratch allocation failed");
    address = (uintptr_t)value->allocation;
    if (address > UINTPTR_MAX - (type->align - 1u)) {
        free(value->allocation);
        memset(value, 0, sizeof(*value));
        return foreach_report(diagnostic,
                              SCXML_EXPR_INVALID_ARGUMENT,
                              "CMeta foreach scratch alignment overflowed");
    }
    aligned = (address + type->align - 1u) &
              ~((uintptr_t)type->align - 1u);
    value->storage = (void *)aligned;
    return foreach_report(diagnostic, SCXML_EXPR_OK, NULL);
}

void scxml_foreach_value_destroy(
    const scxml_foreach_program *program,
    scxml_foreach_value *value) {
    if (value == NULL) return;
    if (value->live && program != NULL &&
        program->sequence.element_type != NULL &&
        program->sequence.element_type->traits != NULL &&
        program->sequence.element_type->traits->destroy != NULL)
        program->sequence.element_type->traits->destroy(value->storage);
    free(value->allocation);
    memset(value, 0, sizeof(*value));
}

scxml_expr_status scxml_foreach_next(
    const scxml_foreach_program *program,
    void *staged_root, const scxml_foreach_snapshot *snapshot,
    scxml_foreach_value *value, size_t iteration,
    scxml_expr_diagnostic *diagnostic) {
    unsigned char *root = (unsigned char *)staged_root;
    const cmeta_type_desc *element_type;
    const void *source;
    void *item;
    if (program == NULL || program->sequence.root == NULL ||
        program->sequence.root->storage_type == NULL || staged_root == NULL ||
        program->sequence.element_type == NULL ||
        program->sequence.element_type->size == 0u ||
        program->sequence.element_type->align == 0u ||
        snapshot == NULL || value == NULL ||
        snapshot->allocation == NULL || snapshot->storage == NULL ||
        snapshot->stride < program->sequence.element_type->size ||
        snapshot->stride % program->sequence.element_type->align != 0u ||
        (uintptr_t)snapshot->storage %
                program->sequence.element_type->align != 0u ||
        iteration >= snapshot->length ||
        iteration > SIZE_MAX / snapshot->stride ||
        snapshot->length > program->max_iterations ||
        program->item.offset > program->sequence.root->storage_type->size ||
        program->item.storage_size !=
            program->sequence.element_type->size ||
        program->item.storage_size >
            program->sequence.root->storage_type->size - program->item.offset)
        return foreach_report(diagnostic,
                              SCXML_EXPR_INVALID_ARGUMENT,
                              "invalid CMeta foreach iteration arguments");
    if (program->has_index &&
        (program->index.offset >
             program->sequence.root->storage_type->size ||
         program->index.storage_size >
             program->sequence.root->storage_type->size -
                 program->index.offset))
        return foreach_report(diagnostic,
                              SCXML_EXPR_INVALID_ARGUMENT,
                              "CMeta foreach index location is invalid");

    element_type = program->sequence.element_type;
    item = root + program->item.offset;
    source = (const unsigned char *)snapshot->storage +
             iteration * snapshot->stride;
    if (program->managed_item &&
        (cmeta_type_require_traits(
             element_type,
             CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY) !=
             CMETA_OK ||
         value->allocation == NULL || value->storage == NULL || value->live))
        return foreach_report(diagnostic,
                              SCXML_EXPR_INVALID_ARGUMENT,
                              "CMeta foreach managed scratch is invalid");
    if (program->managed_item) {
        if (!element_type->traits->copy_construct(value->storage, source))
            return foreach_report(
                diagnostic, SCXML_EXPR_EVALUATION_ERROR,
                "CMeta foreach snapshot value copy failed");
        value->live = true;
        element_type->traits->destroy(item);
        element_type->traits->move_construct(item, value->storage);
        element_type->traits->destroy(value->storage);
        value->live = false;
    } else {
        memcpy(item, source, element_type->size);
    }
    if (program->has_index) {
        memcpy(root + program->index.offset, &iteration, sizeof(iteration));
    }
    return foreach_report(diagnostic, SCXML_EXPR_OK, NULL);
}
