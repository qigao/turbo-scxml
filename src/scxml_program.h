#ifndef SCXML_PROGRAM_H
#define SCXML_PROGRAM_H

#include "scxml_impl.h"

const scxml_program_name *scxml_program_find_name(
    const scxml_program_name *names, size_t count,
    const char *name, size_t name_size);

#endif
