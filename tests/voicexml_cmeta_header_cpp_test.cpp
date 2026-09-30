#include <voicexml/cmeta.h>

#include <type_traits>

static_assert(std::is_standard_layout<vxml_cmeta_prompt_view_v1>::value,
              "prompt view must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_media_segment_v1>::value,
    "prompt media segment must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_media_ticket_v1>::value,
    "prompt media ticket must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_media_request_v1>::value,
    "prompt media request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_prompt_media_adapter_v1>::value,
    "prompt media adapter must remain C-compatible");
static_assert(std::is_standard_layout<vxml_cmeta_collect_ticket_v1>::value,
              "collect ticket must remain C-compatible");
static_assert(std::is_standard_layout<vxml_cmeta_collect_request_v1>::value,
              "collect request must remain C-compatible");
static_assert(std::is_standard_layout<vxml_cmeta_collect_completion_v1>::value,
              "collect completion must remain C-compatible");
static_assert(std::is_standard_layout<vxml_cmeta_collect_result_slot_v1>::value,
              "collect result slot must remain C-compatible");
static_assert(std::is_standard_layout<vxml_cmeta_collect_completion_v2>::value,
              "collect completion V2 must remain C-compatible");
static_assert(std::is_standard_layout<vxml_cmeta_collect_adapter_v1>::value,
              "collect adapter must remain C-compatible");

int turboscxml_voicexml_cmeta_header_cpp_probe()
{
    vxml_program program{};
    vxml_cmeta_prompt_view_v1 prompt{};
    vxml_cmeta_compile_options_v1 options{};
    vxml_cmeta_collect_request_v1 request{};
    vxml_cmeta_collect_completion_v1 completion{};
    return program.impl == nullptr &&
           prompt.abi_version == 0u &&
           options.root == nullptr &&
           request.abi_version == 0u &&
           completion.abi_version == 0u ? 0 : 1;
}
