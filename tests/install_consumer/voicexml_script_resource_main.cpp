#include <voicexml/script_resource.h>

#include <type_traits>

static_assert(
    std::is_standard_layout<vxml_script_request_v1>::value,
    "script request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_external_script_target_v1>::value,
    "external script target must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_script_source>::value,
    "script source must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_script_resource_adapter_v1>::value,
    "script adapter must remain C-compatible");

int main() {
    auto request = vxml_script_request_v1 VXML_SCRIPT_REQUEST_V1_INIT;
    vxml_external_script_target_v1 target{};
    auto compile_profile = &vxml_compile_external_script_profile;
    auto acquire_fn = &vxml_script_resource_acquire;
    auto close_fn = &vxml_script_resource_close;
    auto failure_event_fn = &vxml_script_resource_failure_event;
    return request.abi_version == VXML_SCRIPT_REQUEST_ABI_V1 &&
           request.struct_size == sizeof(request) &&
           target.abi_version == 0u &&
           VXML_EXTERNAL_SCRIPT_TARGET_ABI_V1 != 0u &&
           compile_profile != nullptr &&
           acquire_fn != nullptr && close_fn != nullptr &&
           failure_event_fn != nullptr
        ? 0 : 1;
}
