#ifndef SCXML_PROGRAM_H
#define SCXML_PROGRAM_H

#include "scxml_impl.h"

const scxml_program_name *scxml_program_find_name(
    const scxml_program_name *names, size_t count,
    const char *name, size_t name_size);

scxml_status scxml_program_compile_quickjs_model(
    scxml_program *out, const char *input, size_t input_size,
    const scxml_limits *limits,
    const scxml_quickjs_compile_options_v1 *options,
    scxml_diagnostic *diagnostic);

#endif
