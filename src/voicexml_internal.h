#ifndef TURBO_VOICEXML_INTERNAL_H
#define TURBO_VOICEXML_INTERNAL_H

#include <voicexml/voicexml.h>

#define VXML_DEFAULT_MAX_FORMS 64u
#define VXML_DEFAULT_MAX_BLOCKS 1024u
#define VXML_DEFAULT_MAX_ACTIONS 4096u
#define VXML_DEFAULT_MAX_NAME_BYTES (256u * 1024u)

typedef enum vxml_action_kind {
    VXML_ACTION_EXIT = 1
} vxml_action_kind;

typedef struct vxml_action_row {
    vxml_action_kind kind;
} vxml_action_row;

typedef struct vxml_block_row {
    size_t first_action;
    size_t action_count;
} vxml_block_row;

typedef struct vxml_form_row {
    const char *id;
    size_t id_size;
    size_t first_block;
    size_t block_count;
} vxml_form_row;

typedef enum vxml_profile_kind {
    VXML_PROFILE_LITERAL = 0,
    VXML_PROFILE_CMETA
} vxml_profile_kind;

typedef struct vxml_session_impl vxml_session_impl;
typedef struct vxml_program_impl vxml_program_impl;

typedef vxml_status (*vxml_profile_session_init_fn)(
    vxml_session_impl *session, const void *options);
typedef vxml_status (*vxml_profile_session_start_fn)(
    vxml_session_impl *session);
typedef void (*vxml_profile_session_destroy_fn)(vxml_session_impl *session);
typedef void (*vxml_profile_program_destroy_fn)(vxml_program_impl *program);

struct vxml_session_impl {
    const vxml_program_impl *program;
    vxml_session_state state;
    vxml_status error;
    void *profile_data;
};

struct vxml_program_impl {
    vxml_form_row *forms;
    vxml_block_row *blocks;
    vxml_action_row *actions;
    char *storage;
    size_t form_count;
    size_t block_count;
    size_t action_count;
    size_t storage_size;
    size_t allocation_size;
    vxml_profile_kind profile_kind;
    void *profile_data;
    vxml_profile_session_init_fn profile_session_init;
    vxml_profile_session_start_fn profile_session_start;
    vxml_profile_session_destroy_fn profile_session_destroy;
    vxml_profile_program_destroy_fn profile_program_destroy;
};

vxml_status vxml_session_init_profile(
    vxml_session *session, const vxml_program *program, const void *options);
vxml_status vxml_session_start_literal(vxml_session_impl *impl);

#endif /* TURBO_VOICEXML_INTERNAL_H */
