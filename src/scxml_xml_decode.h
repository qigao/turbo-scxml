#ifndef TURBO_SCXML_XML_DECODE_H
#define TURBO_SCXML_XML_DECODE_H

#include <stddef.h>

typedef enum scxml_xml_decode_status {
    SCXML_XML_DECODE_OK = 0,
    SCXML_XML_DECODE_INVALID_ARGUMENT,
    SCXML_XML_DECODE_INVALID_CHARACTER_REFERENCE,
    SCXML_XML_DECODE_UNSUPPORTED_ENTITY,
    SCXML_XML_DECODE_CAPACITY_EXCEEDED
} scxml_xml_decode_status;

/* Decode XML attribute entities. A NULL output performs a measurement pass. */
scxml_xml_decode_status scxml_xml_decode_attribute_entities(
    const char *input, size_t input_size,
    char *output, size_t output_capacity,
    size_t *out_size);

#endif /* TURBO_SCXML_XML_DECODE_H */
