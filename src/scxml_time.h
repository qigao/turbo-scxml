#ifndef TURBO_SCXML_TIME_H
#define TURBO_SCXML_TIME_H

#include <scxml/scxml.h>

bool scxml_time_parse_ms(
    salts_xml_string_view value, uint64_t *out_ms);

#endif /* TURBO_SCXML_TIME_H */
