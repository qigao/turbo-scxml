#ifndef TURBO_VOICEXML_QUICKJS_FIA_H
#define TURBO_VOICEXML_QUICKJS_FIA_H

#include "voicexml_cmeta_backend.h"
#include "quickjs_cmeta_bridge.h"

/* Internal, not installed: public composition waits for the complete script,
 * media-transaction and package qualification described in the architecture. */
typedef struct vxml_quickjs_fia_limits {
    quickjs_sandbox_options engine;
    quickjs_cmeta_limits values;
} vxml_quickjs_fia_limits;

extern const vxml_cmeta_expression_backend vxml_quickjs_fia_backend;

#endif
