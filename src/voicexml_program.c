#include "voicexml_internal.h"

#include <string.h>

vxml_limits vxml_default_limits(void) {
    const vxml_limits limits = {
        salts_xml_default_limits(),
        VXML_DEFAULT_MAX_FORMS,
        VXML_DEFAULT_MAX_BLOCKS,
        VXML_DEFAULT_MAX_ACTIONS,
        VXML_DEFAULT_MAX_NAME_BYTES};
    return limits;
}

vxml_status vxml_compile(const void *bytes, size_t size,
                         const vxml_limits *limits,
                         vxml_program *out,
                         vxml_diagnostic *diagnostic) {
    (void)limits;
    if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
    if (bytes == NULL || size == 0u || out == NULL || out->impl != NULL)
        return VXML_INVALID_ARGUMENT;

    return VXML_UNSUPPORTED_FEATURE;
}

void vxml_program_destroy(vxml_program *program) {
    if (program != NULL) program->impl = NULL;
}
