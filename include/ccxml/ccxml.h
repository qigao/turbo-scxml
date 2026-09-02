#ifndef TURBO_CCXML_H
#define TURBO_CCXML_H

#include <scxml/scxml.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CCXML_DIAGNOSTIC_CAPACITY 256u

typedef enum ccxml_status {
    CCXML_OK = 0,
    CCXML_INVALID_ARGUMENT,
    CCXML_XML_ERROR,
    CCXML_ALLOCATION_FAILED,
    CCXML_LIMIT_EXCEEDED,
    CCXML_INVALID_NAMESPACE,
    CCXML_INVALID_VERSION,
    CCXML_INVALID_STRUCTURE,
    CCXML_UNSUPPORTED_FEATURE,
    CCXML_INVALID_EVENT,
    CCXML_ADAPTER_ERROR,
    CCXML_INVALID_CONTRACT,
    CCXML_CLOSED,
    CCXML_BUSY
} ccxml_status;

typedef struct ccxml_limits {
    turbo_xml_limits xml;
    size_t max_transitions;
    size_t max_actions;
    size_t max_name_bytes;
} ccxml_limits;

typedef struct ccxml_diagnostic {
    ccxml_status status;
    turbo_xml_location location;
    char message[CCXML_DIAGNOSTIC_CAPACITY];
} ccxml_diagnostic;

typedef struct ccxml_program {
    void *impl;
} ccxml_program;

ccxml_limits ccxml_default_limits(void);

ccxml_status ccxml_compile(
    ccxml_program *out,
    const char *input,
    size_t input_size,
    const ccxml_limits *limits,
    ccxml_diagnostic *diagnostic);

void ccxml_program_destroy(ccxml_program *program);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_CCXML_H */
