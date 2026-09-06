#ifndef TURBO_VOICEXML_CMETA_INTERNAL_H
#define TURBO_VOICEXML_CMETA_INTERNAL_H

#include "voicexml_cmeta_expr.h"
#include "voicexml_internal.h"

typedef struct vxml_cmeta_program_data {
    const cmeta_data_desc *root;
    const cmeta_data_desc **semantic_data;
    size_t semantic_data_count;
} vxml_cmeta_program_data;

vxml_status vxml_cmeta_session_init_profile(
    vxml_session_impl *session, const void *options);
vxml_status vxml_cmeta_session_start_profile(vxml_session_impl *session);
void vxml_cmeta_session_destroy_profile(vxml_session_impl *session);
void vxml_cmeta_program_destroy_profile(vxml_program_impl *program);

#endif /* TURBO_VOICEXML_CMETA_INTERNAL_H */
