#include <voicexml/cmeta.h>

int turboscxml_voicexml_cmeta_header_cpp_probe()
{
    vxml_program program{};
    vxml_cmeta_compile_options_v1 options{};
    return program.impl == nullptr && options.root == nullptr ? 0 : 1;
}
