#ifndef TURBO_VOICEXML_QUICKJS_FIA_H
#define TURBO_VOICEXML_QUICKJS_FIA_H

#include "voicexml_cmeta_backend.h"
#include "quickjs_cmeta_bridge.h"
#include <voicexml/script_resource.h>

/* Internal, not installed: public composition waits for the complete script,
 * media-transaction and package qualification described in the architecture. */
typedef struct vxml_quickjs_fia_limits {
    quickjs_sandbox_options engine;
    quickjs_cmeta_limits values;
} vxml_quickjs_fia_limits;

extern const vxml_cmeta_expression_backend vxml_quickjs_fia_backend;

/* Copied into the Session. Provider user and document resolver remain borrowed
 * through close. Script acquisition is synchronous on the Session owner. */
typedef struct vxml_quickjs_fia_scripts {
    const vxml_script_resource_adapter_v1 *resources;
    void *user;
    size_t max_uri_bytes;
    size_t max_source_bytes;
} vxml_quickjs_fia_scripts;

vxml_status vxml_session_init_quickjs_fia(
    vxml_session *session, const vxml_program *program,
    const vxml_cmeta_session_options_v1 *options,
    const vxml_quickjs_fia_scripts *scripts);

#endif
