#include "voicexml_internal.h"
#include "voicexml_allocator.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static bool range_is_valid(size_t first, size_t count, size_t total) {
    return first <= total && count <= total - first;
}

static vxml_status fail_structure(vxml_session_impl *impl) {
    impl->state = VXML_SESSION_FAILED;
    impl->error = VXML_INVALID_STRUCTURE;
    return impl->error;
}

static bool program_uri_view_valid(
    const vxml_program_impl *program,
    const char *data,
    size_t size) {
    uintptr_t begin;
    uintptr_t end;
    uintptr_t value;
    if (program == NULL || program->storage == NULL ||
        data == NULL || size == 0u)
        return false;
    begin = (uintptr_t)program->storage;
    if (program->storage_size > UINTPTR_MAX - begin)
        return false;
    end = begin + program->storage_size;
    value = (uintptr_t)data;
    if (value < begin || value >= end ||
        size > (size_t)(end - value) ||
        size == (size_t)(end - value))
        return false;
    return data[size] == '\0';
}

vxml_status vxml_session_start_literal_at(
    vxml_session_impl *impl, size_t form_index) {
    const vxml_program_impl *program = impl->program;
    size_t transitions = 0u;

    if (program == NULL || program->forms == NULL ||
        program->form_count == 0u)
        return fail_structure(impl);

    for (;;) {
        const vxml_form_row *form;
        size_t block_index;
        bool jumped = false;
        if (form_index >= program->form_count)
            return fail_structure(impl);
        form = &program->forms[form_index];
        if (form->block_count == 0u || program->blocks == NULL ||
            !range_is_valid(form->first_block, form->block_count,
                            program->block_count))
            return fail_structure(impl);

        for (block_index = form->first_block;
             block_index < form->first_block + form->block_count;
             ++block_index) {
            const vxml_block_row *block = &program->blocks[block_index];
            const vxml_action_row *action;
            if (block->action_count > 1u ||
                !range_is_valid(block->first_action, block->action_count,
                                program->action_count) ||
                (block->action_count != 0u && program->actions == NULL))
                return fail_structure(impl);
            if (block->action_count == 0u) continue;
            action = &program->actions[block->first_action];
            if (action->kind == VXML_ACTION_EXIT) {
                impl->state = VXML_SESSION_EXITED;
                return VXML_OK;
            }
            if (action->kind == VXML_ACTION_SCRIPT_EXTERNAL) {
                if (!program_uri_view_valid(
                        program, action->script_src,
                        action->script_src_size) ||
                    !program_uri_view_valid(
                        program, action->script_charset,
                        action->script_charset_size))
                    return fail_structure(impl);
                impl->script_src = action->script_src;
                impl->script_src_size = action->script_src_size;
                impl->script_charset = action->script_charset;
                impl->script_charset_size =
                    action->script_charset_size;
                impl->state = VXML_SESSION_SCRIPTING;
                return VXML_OK;
            }
            if (action->kind == VXML_ACTION_SUBMIT) {
                if (!program_uri_view_valid(
                        program, action->target_uri,
                        action->target_uri_size) ||
                    (action->submit_method !=
                         VXML_SUBMIT_METHOD_GET &&
                     action->submit_method !=
                         VXML_SUBMIT_METHOD_POST) ||
                    action->submit_enctype !=
                        VXML_SUBMIT_ENCTYPE_URLENCODED)
                    return fail_structure(impl);
                impl->submit_uri = action->target_uri;
                impl->submit_uri_size = action->target_uri_size;
                impl->submit_method = action->submit_method;
                impl->submit_enctype = action->submit_enctype;
                impl->submit_fields = NULL;
                impl->submit_field_count = 0u;
                impl->state = VXML_SESSION_SUBMITTING;
                return VXML_OK;
            }
            if (action->kind == VXML_ACTION_GOTO_EXTERNAL) {
                if (!program_uri_view_valid(
                        program, action->target_uri,
                        action->target_uri_size) ||
                    ((action->fetchaudio_uri == NULL) !=
                     (action->fetchaudio_uri_size == 0u)) ||
                    (action->fetchaudio_uri != NULL &&
                     !program_uri_view_valid(
                         program, action->fetchaudio_uri,
                         action->fetchaudio_uri_size)))
                    return fail_structure(impl);
                impl->navigation_uri = action->target_uri;
                impl->navigation_uri_size = action->target_uri_size;
                impl->navigation_fetchaudio_uri =
                    action->fetchaudio_uri;
                impl->navigation_fetchaudio_uri_size =
                    action->fetchaudio_uri_size;
                impl->navigation_has_fetchaudio_delay = false;
                impl->navigation_fetchaudio_delay_us = UINT64_C(0);
                impl->navigation_has_fetchaudio_minimum = false;
                impl->navigation_fetchaudio_minimum_us = UINT64_C(0);
                impl->state = VXML_SESSION_NAVIGATING;
                return VXML_OK;
            }
            if (action->kind != VXML_ACTION_GOTO ||
                action->target_form >= program->form_count)
                return fail_structure(impl);
            if (transitions >= program->form_count)
                return fail_structure(impl);
            ++transitions;
            form_index = action->target_form;
            jumped = true;
            break;
        }

        if (!jumped) {
            impl->state = VXML_SESSION_EXITED;
            return VXML_OK;
        }
    }
}

vxml_status vxml_session_start_literal(vxml_session_impl *impl) {
    return vxml_session_start_literal_at(impl, 0u);
}

vxml_status vxml_session_init(vxml_session *session,
                              const vxml_program *program) {
    if (session == NULL) return VXML_INVALID_ARGUMENT;
    session->impl = NULL;
    if (program == NULL || program->impl == NULL) return VXML_INVALID_ARGUMENT;
    if (((const vxml_program_impl *)program->impl)->profile_kind !=
        VXML_PROFILE_LITERAL)
        return VXML_INVALID_CONTRACT;
    return vxml_session_init_profile(session, program, NULL);
}

vxml_status vxml_session_init_profile(
    vxml_session *session, const vxml_program *program, const void *options) {
    vxml_session_impl *impl;
    const vxml_program_impl *program_impl;
    vxml_status status;
    if (session == NULL) return VXML_INVALID_ARGUMENT;
    session->impl = NULL;
    if (program == NULL || program->impl == NULL) return VXML_INVALID_ARGUMENT;
    program_impl = (const vxml_program_impl *)program->impl;
    if (program_impl->profile_kind == VXML_PROFILE_CMETA &&
        program_impl->profile_session_init == NULL)
        return VXML_INVALID_CONTRACT;
    impl = (vxml_session_impl *)vxml_malloc(sizeof(*impl));
    if (impl == NULL) return VXML_ALLOCATION_FAILED;
    impl->program = program_impl;
    impl->state = VXML_SESSION_READY;
    impl->error = VXML_OK;
    impl->navigation_uri = NULL;
    impl->navigation_uri_size = 0u;
    impl->navigation_fetchaudio_uri = NULL;
    impl->navigation_fetchaudio_uri_size = 0u;
    impl->navigation_has_fetchaudio_delay = false;
    impl->navigation_fetchaudio_delay_us = UINT64_C(0);
    impl->navigation_has_fetchaudio_minimum = false;
    impl->navigation_fetchaudio_minimum_us = UINT64_C(0);
    impl->submit_uri = NULL;
    impl->submit_uri_size = 0u;
    impl->submit_method = 0;
    impl->submit_enctype = 0;
    impl->submit_fields = NULL;
    impl->submit_field_count = 0u;
    impl->script_src = NULL;
    impl->script_src_size = 0u;
    impl->script_charset = NULL;
    impl->script_charset_size = 0u;
    impl->profile_data = NULL;
    if (program_impl->profile_session_init != NULL) {
        status = program_impl->profile_session_init(impl, options);
        if (status != VXML_OK) {
            vxml_free(impl);
            return status;
        }
    }
    session->impl = impl;
    return VXML_OK;
}

vxml_status vxml_session_start(vxml_session *session) {
    vxml_session_impl *impl;
    if (session == NULL) return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl == NULL) return VXML_INVALID_STATE;
    if (impl->state == VXML_SESSION_CLOSED) return VXML_CLOSED;
    if (impl->state != VXML_SESSION_READY) return VXML_INVALID_STATE;
    impl->state = VXML_SESSION_RUNNING;
    impl->error = VXML_OK;
    impl->navigation_uri = NULL;
    impl->navigation_uri_size = 0u;
    impl->navigation_fetchaudio_uri = NULL;
    impl->navigation_fetchaudio_uri_size = 0u;
    impl->navigation_has_fetchaudio_delay = false;
    impl->navigation_fetchaudio_delay_us = UINT64_C(0);
    impl->navigation_has_fetchaudio_minimum = false;
    impl->navigation_fetchaudio_minimum_us = UINT64_C(0);
    impl->submit_uri = NULL;
    impl->submit_uri_size = 0u;
    impl->submit_method = 0;
    impl->submit_enctype = 0;
    impl->submit_fields = NULL;
    impl->submit_field_count = 0u;
    impl->script_src = NULL;
    impl->script_src_size = 0u;
    impl->script_charset = NULL;
    impl->script_charset_size = 0u;
    return impl->program->profile_session_start != NULL
        ? impl->program->profile_session_start(impl)
        : vxml_session_start_literal(impl);
}

vxml_status vxml_session_start_at_form(
    vxml_session *session,
    const char *form_id,
    size_t form_id_size) {
    vxml_session_impl *impl;
    size_t form_index;
    if (session == NULL || form_id == NULL ||
        form_id_size == 0u ||
        memchr(form_id, '\0', form_id_size) != NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl == NULL) return VXML_INVALID_STATE;
    if (impl->state == VXML_SESSION_CLOSED) return VXML_CLOSED;
    if (impl->state != VXML_SESSION_READY) return VXML_INVALID_STATE;
    if (impl->program == NULL || impl->program->forms == NULL)
        return VXML_INVALID_CONTRACT;
    for (form_index = 0u;
         form_index < impl->program->form_count;
         ++form_index) {
        const vxml_form_row *form = &impl->program->forms[form_index];
        if (form->id != NULL &&
            form->id_size == form_id_size &&
            memcmp(form->id, form_id, form_id_size) == 0)
            break;
    }
    impl->state = VXML_SESSION_RUNNING;
    impl->error = VXML_OK;
    impl->navigation_uri = NULL;
    impl->navigation_uri_size = 0u;
    impl->navigation_fetchaudio_uri = NULL;
    impl->navigation_fetchaudio_uri_size = 0u;
    impl->navigation_has_fetchaudio_delay = false;
    impl->navigation_fetchaudio_delay_us = UINT64_C(0);
    impl->navigation_has_fetchaudio_minimum = false;
    impl->navigation_fetchaudio_minimum_us = UINT64_C(0);
    impl->submit_uri = NULL;
    impl->submit_uri_size = 0u;
    impl->submit_method = 0;
    impl->submit_enctype = 0;
    impl->submit_fields = NULL;
    impl->submit_field_count = 0u;
    impl->script_src = NULL;
    impl->script_src_size = 0u;
    impl->script_charset = NULL;
    impl->script_charset_size = 0u;
    if (form_index == impl->program->form_count)
        return fail_structure(impl);
    if (impl->program->profile_session_start_at != NULL)
        return impl->program->profile_session_start_at(impl, form_index);
    if (impl->program->profile_session_start != NULL)
        return VXML_INVALID_CONTRACT;
    return vxml_session_start_literal_at(impl, form_index);
}

vxml_status vxml_session_navigation_request(
    const vxml_session *session,
    vxml_navigation_request_v1 *out_request) {
    const vxml_session_impl *impl;
    if (session == NULL || out_request == NULL)
        return VXML_INVALID_ARGUMENT;
    *out_request = (vxml_navigation_request_v1){0};
    impl = (const vxml_session_impl *)session->impl;
    if (impl == NULL) return VXML_INVALID_STATE;
    if (impl->state == VXML_SESSION_CLOSED) return VXML_CLOSED;
    if (impl->state != VXML_SESSION_NAVIGATING)
        return VXML_INVALID_STATE;
    if (impl->navigation_uri == NULL ||
        impl->navigation_uri_size == 0u ||
        ((impl->navigation_fetchaudio_uri == NULL) !=
         (impl->navigation_fetchaudio_uri_size == 0u)))
        return VXML_INVALID_CONTRACT;
    *out_request = (vxml_navigation_request_v1){
        .abi_version = VXML_NAVIGATION_REQUEST_ABI_V1,
        .struct_size = sizeof(vxml_navigation_request_v1),
        .uri = impl->navigation_uri,
        .uri_size = impl->navigation_uri_size,
        .fetchaudio_uri = impl->navigation_fetchaudio_uri,
        .fetchaudio_uri_size =
            impl->navigation_fetchaudio_uri_size,
        .has_fetchaudio_delay =
            impl->navigation_has_fetchaudio_delay,
        .fetchaudio_delay_us =
            impl->navigation_fetchaudio_delay_us,
        .has_fetchaudio_minimum =
            impl->navigation_has_fetchaudio_minimum,
        .fetchaudio_minimum_us =
            impl->navigation_fetchaudio_minimum_us};
    return VXML_OK;
}

vxml_status vxml_session_navigation(
    const vxml_session *session,
    vxml_navigation_target *out_target) {
    vxml_navigation_request_v1 request = {0};
    vxml_status status;
    if (out_target == NULL) return VXML_INVALID_ARGUMENT;
    *out_target = (vxml_navigation_target){0};
    status = vxml_session_navigation_request(
        session, &request);
    if (status != VXML_OK) return status;
    out_target->uri = request.uri;
    out_target->uri_size = request.uri_size;
    return VXML_OK;
}

static bool submit_field_view_valid(
    const vxml_submit_field_v1 *field) {
    if (field == NULL ||
        field->name == NULL ||
        field->name_size == 0u ||
        memchr(field->name, '\0', field->name_size) != NULL ||
        (field->value_size != 0u && field->value == NULL) ||
        (field->value_size != 0u &&
         memchr(field->value, '\0', field->value_size) != NULL))
        return false;
    return true;
}

vxml_status vxml_session_submit_v2(
    const vxml_session *session,
    vxml_submit_target_v2 *out_target) {
    const vxml_session_impl *impl;
    size_t index;
    if (session == NULL || out_target == NULL)
        return VXML_INVALID_ARGUMENT;
    *out_target = (vxml_submit_target_v2){0};
    impl = (const vxml_session_impl *)session->impl;
    if (impl == NULL) return VXML_INVALID_STATE;
    if (impl->state == VXML_SESSION_CLOSED) return VXML_CLOSED;
    if (impl->state != VXML_SESSION_SUBMITTING)
        return VXML_INVALID_STATE;
    if (impl->submit_uri == NULL ||
        impl->submit_uri_size == 0u ||
        memchr(impl->submit_uri, '\0', impl->submit_uri_size) != NULL ||
        (impl->submit_method != VXML_SUBMIT_METHOD_GET &&
         impl->submit_method != VXML_SUBMIT_METHOD_POST) ||
        (impl->submit_enctype != VXML_SUBMIT_ENCTYPE_URLENCODED &&
         impl->submit_enctype !=
            VXML_SUBMIT_ENCTYPE_MULTIPART_FORM_DATA) ||
        ((impl->submit_fields == NULL) !=
         (impl->submit_field_count == 0u)))
        return VXML_INVALID_CONTRACT;
    for (index = 0u; index < impl->submit_field_count; ++index)
        if (!submit_field_view_valid(
                &impl->submit_fields[index]))
            return VXML_INVALID_CONTRACT;
    *out_target = (vxml_submit_target_v2){
        .abi_version = VXML_SUBMIT_TARGET_ABI_V2,
        .struct_size = sizeof(vxml_submit_target_v2),
        .uri = impl->submit_uri,
        .uri_size = impl->submit_uri_size,
        .method = impl->submit_method,
        .enctype = impl->submit_enctype,
        .fields = impl->submit_fields,
        .field_count = impl->submit_field_count};
    return VXML_OK;
}

vxml_status vxml_session_submit(
    const vxml_session *session,
    vxml_submit_target_v1 *out_target) {
    vxml_submit_target_v2 submit = {0};
    vxml_status status;
    if (out_target == NULL)
        return VXML_INVALID_ARGUMENT;
    *out_target = (vxml_submit_target_v1){0};
    status = vxml_session_submit_v2(
        session, &submit);
    if (status != VXML_OK)
        return status;
    if (submit.enctype != VXML_SUBMIT_ENCTYPE_URLENCODED ||
        submit.field_count != 0u)
        return VXML_UNSUPPORTED_FEATURE;
    *out_target = (vxml_submit_target_v1){
        .abi_version = VXML_SUBMIT_TARGET_ABI_V1,
        .struct_size = sizeof(vxml_submit_target_v1),
        .uri = submit.uri,
        .uri_size = submit.uri_size,
        .method = submit.method,
        .enctype = submit.enctype};
    return VXML_OK;
}

vxml_status vxml_session_script(
    const vxml_session *session,
    vxml_external_script_target_v1 *out_target) {
    const vxml_session_impl *impl;
    if (session == NULL || out_target == NULL)
        return VXML_INVALID_ARGUMENT;
    *out_target = (vxml_external_script_target_v1){0};
    impl = (const vxml_session_impl *)session->impl;
    if (impl == NULL) return VXML_INVALID_STATE;
    if (impl->state == VXML_SESSION_CLOSED) return VXML_CLOSED;
    if (impl->state != VXML_SESSION_SCRIPTING)
        return VXML_INVALID_STATE;
    if (impl->script_src == NULL ||
        impl->script_src_size == 0u ||
        impl->script_charset == NULL ||
        impl->script_charset_size == 0u)
        return VXML_INVALID_CONTRACT;
    *out_target = (vxml_external_script_target_v1){
        .abi_version = VXML_EXTERNAL_SCRIPT_TARGET_ABI_V1,
        .struct_size = sizeof(vxml_external_script_target_v1),
        .src = impl->script_src,
        .src_size = impl->script_src_size,
        .charset = impl->script_charset,
        .charset_size = impl->script_charset_size};
    return VXML_OK;
}

vxml_status vxml_session_raise_event(
    vxml_session *session,
    const char *event_name,
    size_t event_name_size) {
    vxml_session_impl *impl;
    if (session == NULL || session->impl == NULL ||
        event_name == NULL || event_name_size == 0u ||
        memchr(event_name, '\0', event_name_size) != NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl->state == VXML_SESSION_CLOSED)
        return VXML_CLOSED;
    if (impl->state != VXML_SESSION_RUNNING)
        return VXML_INVALID_STATE;
    if (impl->program == NULL)
        return VXML_INVALID_CONTRACT;
    if (impl->program->profile_session_raise_event == NULL)
        return VXML_UNSUPPORTED_FEATURE;
    return impl->program->profile_session_raise_event(
        impl, event_name, event_name_size);
}

vxml_session_state vxml_session_get_state(const vxml_session *session) {
    const vxml_session_impl *impl;
    if (session == NULL || session->impl == NULL) return VXML_SESSION_CLOSED;
    impl = (const vxml_session_impl *)session->impl;
    return impl->state;
}

vxml_status vxml_session_error(const vxml_session *session) {
    const vxml_session_impl *impl;
    if (session == NULL) return VXML_INVALID_ARGUMENT;
    if (session->impl == NULL) return VXML_INVALID_STATE;
    impl = (const vxml_session_impl *)session->impl;
    return impl->state == VXML_SESSION_CLOSED ? VXML_CLOSED : impl->error;
}

vxml_status vxml_session_close(vxml_session *session) {
    vxml_session_impl *impl;
    if (session == NULL) return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)session->impl;
    if (impl == NULL) return VXML_INVALID_STATE;
    if (impl->state != VXML_SESSION_CLOSED) {
        if (impl->program != NULL &&
            impl->program->profile_session_destroy != NULL)
            impl->program->profile_session_destroy(impl);
        impl->navigation_uri = NULL;
        impl->navigation_uri_size = 0u;
        impl->navigation_fetchaudio_uri = NULL;
        impl->navigation_fetchaudio_uri_size = 0u;
    impl->navigation_has_fetchaudio_delay = false;
    impl->navigation_fetchaudio_delay_us = UINT64_C(0);
    impl->navigation_has_fetchaudio_minimum = false;
    impl->navigation_fetchaudio_minimum_us = UINT64_C(0);
        impl->submit_uri = NULL;
        impl->submit_uri_size = 0u;
        impl->submit_method = 0;
        impl->submit_enctype = 0;
        impl->submit_fields = NULL;
        impl->submit_field_count = 0u;
        impl->state = VXML_SESSION_CLOSED;
    }
    return VXML_OK;
}

void vxml_session_destroy(vxml_session *session) {
    if (session == NULL || session->impl == NULL) return;
    {
        vxml_session_impl *impl = (vxml_session_impl *)session->impl;
        if (impl->program != NULL && impl->program->profile_session_destroy != NULL)
            impl->program->profile_session_destroy(impl);
    }
    vxml_free(session->impl);
    session->impl = NULL;
}
