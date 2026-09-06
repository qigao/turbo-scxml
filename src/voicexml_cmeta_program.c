#include <voicexml/cmeta.h>

#include "voicexml_cmeta_internal.h"
#include "voicexml_allocator.h"

#include <stdint.h>
#include <string.h>

static bool cmeta_root_supported(const cmeta_data_desc *root) {
    const cmeta_data_struct_shape *shape;
    const cmeta_type_traits *traits;
    size_t index;

    if (!cmeta_data_desc_valid(root) || root->kind != CMETA_DATA_STRUCT ||
        root->storage_type == NULL || root->shape == NULL)
        return false;
    shape = (const cmeta_data_struct_shape *)root->shape;
    if (shape->layout == NULL || shape->field_count != shape->layout->field_count ||
        (shape->field_count != 0u && shape->fields == NULL) ||
        shape->layout->size != root->storage_type->size ||
        shape->layout->align != root->storage_type->align)
        return false;
    traits = root->storage_type->traits;
    if (traits == NULL ||
        ((traits->flags & (CMETA_TRAIT_TRIVIAL_COPY |
                           CMETA_TRAIT_TRIVIAL_DESTROY)) !=
         (CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY) &&
         cmeta_type_require_traits(root->storage_type,
             CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY) !=
             CMETA_OK))
        return false;
    for (index = 0u; index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &shape->fields[index];
        const cmeta_field_desc *layout_field =
            cmeta_struct_field(shape->layout, index);
        if (field->stable_id == NULL || field->name == NULL ||
            !cmeta_data_desc_valid(field->value) || layout_field == NULL ||
            layout_field->type == NULL || field->value->storage_type == NULL ||
            strcmp(field->name, layout_field->name) != 0 ||
            field->offset != layout_field->offset ||
            layout_field->offset > root->storage_type->size ||
            layout_field->size >
                root->storage_type->size - layout_field->offset ||
            !cmeta_type_equal(layout_field->type, field->value->storage_type) ||
            layout_field->size != field->value->storage_type->size ||
            layout_field->align != field->value->storage_type->align)
            return false;
    }
    return true;
}

static bool compile_options_valid(const vxml_cmeta_compile_options_v1 *options) {
    size_t index;
    if (options == NULL ||
        options->abi_version != VXML_CMETA_COMPILE_OPTIONS_ABI_V1 ||
        options->struct_size < sizeof(*options) ||
        !cmeta_root_supported(options->root) ||
        options->semantic_data_count >
            SIZE_MAX / sizeof(*options->semantic_data) ||
        ((options->semantic_data == NULL) != (options->semantic_data_count == 0u)) ||
        options->max_expression_bytes == 0u ||
        options->max_expression_instructions == 0u ||
        options->max_expression_operands == 0u ||
        options->max_expression_depth == 0u || options->max_path_depth == 0u ||
        options->max_literal_bytes == 0u || options->max_string_bytes == 0u ||
        options->max_scope_slots == 0u || options->max_scope_storage_bytes == 0u ||
        options->max_conditional_depth == 0u)
        return false;
    for (index = 0u; index < options->semantic_data_count; ++index) {
        size_t prior;
        const cmeta_data_desc *descriptor = options->semantic_data[index];
        if (!cmeta_data_desc_valid(descriptor) || descriptor->storage_type == NULL)
            return false;
        for (prior = 0u; prior < index; ++prior) {
            const cmeta_data_desc *previous = options->semantic_data[prior];
            if (cmeta_type_equal(descriptor->storage_type, previous->storage_type))
                return false;
        }
    }
    return true;
}

static bool session_options_valid(const vxml_cmeta_session_options_v1 *options) {
    return options != NULL &&
        options->abi_version == VXML_CMETA_SESSION_OPTIONS_ABI_V1 &&
        options->struct_size >= sizeof(*options) &&
        options->max_transaction_bytes != 0u &&
        options->max_execution_steps != 0u &&
        ((options->initially_undefined == NULL) ==
         (options->initially_undefined_count == 0u));
}

static bool cmeta_datamodel_attribute(
    const char *bytes, size_t size, size_t *out_first, size_t *out_last) {
    const char *tag = "<vxml";
    const char *name = "datamodel";
    size_t index;
    size_t tag_start = 0u;
    size_t tag_end;
    char quote = '\0';
    if (bytes == NULL || out_first == NULL || out_last == NULL) return false;
    while (tag_start < size && bytes[tag_start] != '<') ++tag_start;
    while (tag_start < size &&
           (tag_start + 5u > size || memcmp(bytes + tag_start, tag, 5u) != 0 ||
            (tag_start + 5u < size && bytes[tag_start + 5u] != ' ' &&
             bytes[tag_start + 5u] != '\t' && bytes[tag_start + 5u] != '\r' &&
             bytes[tag_start + 5u] != '\n' && bytes[tag_start + 5u] != '>'))) {
        ++tag_start;
        while (tag_start < size && bytes[tag_start] != '<') ++tag_start;
    }
    if (tag_start == size || tag_start + 5u > size) return false;
    tag_end = tag_start + 5u;
    while (tag_end < size) {
        const char current = bytes[tag_end];
        if (quote != '\0') {
            if (current == quote) quote = '\0';
        } else if (current == '\'' || current == '\"') {
            quote = current;
        } else if (current == '>') {
            break;
        }
        ++tag_end;
    }
    if (tag_end == size) return false;
    index = tag_start + 5u;
    while (index + 9u <= tag_end) {
        size_t value;
        char value_quote;
        if (memcmp(bytes + index, name, 9u) != 0) {
            ++index;
            continue;
        }
        value = index + 9u;
        while (value < tag_end && (bytes[value] == ' ' || bytes[value] == '\t' ||
                                   bytes[value] == '\r' || bytes[value] == '\n'))
            ++value;
        if (value == tag_end || bytes[value++] != '=') {
            ++index;
            continue;
        }
        while (value < tag_end && (bytes[value] == ' ' || bytes[value] == '\t' ||
                                   bytes[value] == '\r' || bytes[value] == '\n'))
            ++value;
        if (value == tag_end || (bytes[value] != '\'' && bytes[value] != '\"')) {
            ++index;
            continue;
        }
        value_quote = bytes[value++];
        if (value + 5u > tag_end || memcmp(bytes + value, "cmeta", 5u) != 0 ||
            value + 5u >= tag_end || bytes[value + 5u] != value_quote)
            return false;
        *out_first = index;
        *out_last = value + 6u;
        return true;
    }
    return false;
}

vxml_status vxml_compile_cmeta(
    const void *bytes, size_t size, const vxml_limits *limits,
    const vxml_cmeta_compile_options_v1 *options,
    vxml_program *out, vxml_diagnostic *diagnostic) {
    vxml_program temporary = {0};
    vxml_cmeta_program_data *profile = NULL;
    char *sanitized = NULL;
    size_t first;
    size_t last;
    vxml_status status;

    if (out != NULL) out->impl = NULL;
    if (!compile_options_valid(options)) return VXML_INVALID_CONTRACT;
    if (bytes == NULL || size == 0u || out == NULL ||
        !cmeta_datamodel_attribute((const char *)bytes, size, &first, &last))
        return VXML_INVALID_CONTRACT;
    sanitized = (char *)vxml_malloc(size);
    if (sanitized == NULL) return VXML_ALLOCATION_FAILED;
    memcpy(sanitized, bytes, size);
    memset(sanitized + first, ' ', last - first);
    status = vxml_compile(sanitized, size, limits, &temporary, diagnostic);
    vxml_free(sanitized);
    if (status != VXML_OK) return status;
    profile = (vxml_cmeta_program_data *)vxml_calloc(1u, sizeof(*profile));
    if (profile == NULL) {
        vxml_program_destroy(&temporary);
        return VXML_ALLOCATION_FAILED;
    }
    if (options->semantic_data_count != 0u) {
        profile->semantic_data = (const cmeta_data_desc **)vxml_malloc(
            options->semantic_data_count * sizeof(*profile->semantic_data));
        if (profile->semantic_data == NULL) {
            vxml_free(profile);
            vxml_program_destroy(&temporary);
            return VXML_ALLOCATION_FAILED;
        }
        memcpy(profile->semantic_data, options->semantic_data,
               options->semantic_data_count * sizeof(*profile->semantic_data));
    }
    profile->root = options->root;
    profile->semantic_data_count = options->semantic_data_count;
    ((vxml_program_impl *)temporary.impl)->profile_kind = VXML_PROFILE_CMETA;
    ((vxml_program_impl *)temporary.impl)->profile_data = profile;
    ((vxml_program_impl *)temporary.impl)->profile_session_init =
        vxml_cmeta_session_init_profile;
    ((vxml_program_impl *)temporary.impl)->profile_session_start =
        vxml_cmeta_session_start_profile;
    ((vxml_program_impl *)temporary.impl)->profile_session_destroy =
        vxml_cmeta_session_destroy_profile;
    ((vxml_program_impl *)temporary.impl)->profile_program_destroy =
        vxml_cmeta_program_destroy_profile;
    *out = temporary;
    return VXML_OK;
}

void vxml_cmeta_program_destroy_profile(vxml_program_impl *program) {
    vxml_cmeta_program_data *profile;
    if (program == NULL) return;
    profile = (vxml_cmeta_program_data *)program->profile_data;
    if (profile == NULL) return;
    vxml_free(profile->semantic_data);
    vxml_free(profile);
    program->profile_data = NULL;
}

vxml_status vxml_cmeta_session_init_profile(
    vxml_session_impl *session, const void *options) {
    (void)session;
    return session_options_valid((const vxml_cmeta_session_options_v1 *)options)
        ? VXML_OK : VXML_INVALID_CONTRACT;
}

vxml_status vxml_cmeta_session_start_profile(vxml_session_impl *session) {
    return vxml_session_start_literal(session);
}

void vxml_cmeta_session_destroy_profile(vxml_session_impl *session) {
    (void)session;
}

vxml_status vxml_session_init_cmeta(
    vxml_session *session, const vxml_program *program,
    const vxml_cmeta_session_options_v1 *options) {
    if (session == NULL) return VXML_INVALID_ARGUMENT;
    session->impl = NULL;
    if (!session_options_valid(options)) return VXML_INVALID_CONTRACT;
    if (program == NULL || program->impl == NULL) return VXML_INVALID_ARGUMENT;
    if (((const vxml_program_impl *)program->impl)->profile_kind !=
        VXML_PROFILE_CMETA)
        return VXML_INVALID_CONTRACT;
    return vxml_session_init_profile(session, program, options);
}

static const vxml_session_impl *cmeta_session(const vxml_session *session) {
    const vxml_session_impl *impl;
    if (session == NULL || session->impl == NULL) return NULL;
    impl = (const vxml_session_impl *)session->impl;
    return impl->program != NULL && impl->program->profile_kind == VXML_PROFILE_CMETA
        ? impl : NULL;
}

vxml_status vxml_session_cmeta_read(
    const vxml_session *session, const char *name, size_t name_size,
    vxml_cmeta_value_view *out_value) {
    if (out_value != NULL) *out_value = (vxml_cmeta_value_view){0};
    if (name == NULL || name_size == 0u || out_value == NULL)
        return VXML_INVALID_ARGUMENT;
    return cmeta_session(session) != NULL ? VXML_SEMANTIC_ERROR : VXML_INVALID_CONTRACT;
}

vxml_status vxml_session_cmeta_exit_kind(
    const vxml_session *session, vxml_cmeta_exit_kind *out_kind) {
    const vxml_session_impl *impl = cmeta_session(session);
    if (out_kind == NULL) return VXML_INVALID_ARGUMENT;
    if (impl == NULL) return VXML_INVALID_CONTRACT;
    if (impl->state != VXML_SESSION_EXITED) return VXML_INVALID_STATE;
    *out_kind = VXML_CMETA_EXIT_EMPTY;
    return VXML_OK;
}

size_t vxml_session_cmeta_exit_count(const vxml_session *session) {
    return cmeta_session(session) != NULL ? 0u : 0u;
}

vxml_status vxml_session_cmeta_exit_at(
    const vxml_session *session, size_t index, vxml_cmeta_name_view *out_name,
    vxml_cmeta_value_view *out_value) {
    if (out_name != NULL) *out_name = (vxml_cmeta_name_view){0};
    if (out_value != NULL) *out_value = (vxml_cmeta_value_view){0};
    if (out_name == NULL || out_value == NULL) return VXML_INVALID_ARGUMENT;
    if (cmeta_session(session) == NULL) return VXML_INVALID_CONTRACT;
    (void)index;
    return VXML_INVALID_ARGUMENT;
}
