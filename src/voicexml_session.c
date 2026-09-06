#include "voicexml_internal.h"
#include "voicexml_allocator.h"

#include <stdbool.h>

struct vxml_session_impl {
    const vxml_program_impl *program;
    vxml_session_state state;
    vxml_status error;
};

static bool range_is_valid(size_t first, size_t count, size_t total) {
    return first <= total && count <= total - first;
}

static vxml_status fail_structure(vxml_session_impl *impl) {
    impl->state = VXML_SESSION_FAILED;
    impl->error = VXML_INVALID_STRUCTURE;
    return impl->error;
}

static vxml_status execute_entry_form(vxml_session_impl *impl) {
    const vxml_program_impl *program = impl->program;
    const vxml_form_row *form;
    size_t block_index;

    if (program == NULL || program->forms == NULL || program->form_count == 0u)
        return fail_structure(impl);
    form = &program->forms[0];
    if (form->block_count == 0u || program->blocks == NULL ||
        !range_is_valid(form->first_block, form->block_count,
                        program->block_count))
        return fail_structure(impl);

    for (block_index = form->first_block;
         block_index < form->first_block + form->block_count;
         ++block_index) {
        const vxml_block_row *block = &program->blocks[block_index];
        if (block->action_count > 1u ||
            !range_is_valid(block->first_action, block->action_count,
                            program->action_count) ||
            (block->action_count != 0u && program->actions == NULL))
            return fail_structure(impl);
        if (block->action_count == 0u) continue;
        if (program->actions[block->first_action].kind != VXML_ACTION_EXIT)
            return fail_structure(impl);
        impl->state = VXML_SESSION_EXITED;
        return VXML_OK;
    }

    impl->state = VXML_SESSION_EXITED;
    return VXML_OK;
}

vxml_status vxml_session_init(vxml_session *session,
                              const vxml_program *program) {
    vxml_session_impl *impl;
    if (session == NULL) return VXML_INVALID_ARGUMENT;
    session->impl = NULL;
    if (program == NULL || program->impl == NULL) return VXML_INVALID_ARGUMENT;
    impl = (vxml_session_impl *)vxml_malloc(sizeof(*impl));
    if (impl == NULL) return VXML_ALLOCATION_FAILED;
    impl->program = (const vxml_program_impl *)program->impl;
    impl->state = VXML_SESSION_READY;
    impl->error = VXML_OK;
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
    return execute_entry_form(impl);
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
    if (impl->state != VXML_SESSION_CLOSED)
        impl->state = VXML_SESSION_CLOSED;
    return VXML_OK;
}

void vxml_session_destroy(vxml_session *session) {
    if (session == NULL || session->impl == NULL) return;
    vxml_free(session->impl);
    session->impl = NULL;
}
