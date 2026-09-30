#include <voicexml/cmeta.h>

#include <type_traits>

static_assert(std::is_standard_layout<vxml_cmeta_collect_ticket_v1>::value,
              "collect ticket must remain C-compatible");
static_assert(std::is_standard_layout<vxml_cmeta_collect_request_v1>::value,
              "collect request must remain C-compatible");
static_assert(std::is_standard_layout<vxml_cmeta_collect_adapter_v1>::value,
              "collect adapter must remain C-compatible");

int turboscxml_voicexml_cmeta_header_cpp_probe()
{
    vxml_program program{};
    vxml_cmeta_compile_options_v1 options{};
    vxml_cmeta_collect_request_v1 request{};
    return program.impl == nullptr && options.root == nullptr &&
           request.abi_version == 0u ? 0 : 1;
}
