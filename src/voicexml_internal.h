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

typedef struct vxml_program_impl {
    vxml_form_row *forms;
    vxml_block_row *blocks;
    vxml_action_row *actions;
    char *storage;
    size_t form_count;
    size_t block_count;
    size_t action_count;
    size_t storage_size;
    size_t allocation_size;
} vxml_program_impl;

typedef struct vxml_session_impl vxml_session_impl;

#endif /* TURBO_VOICEXML_INTERNAL_H */
