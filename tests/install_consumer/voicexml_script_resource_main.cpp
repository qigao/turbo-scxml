#include <voicexml/script_resource.h>

#include <type_traits>

static_assert(
    std::is_standard_layout<vxml_script_request_v1>::value,
    "script request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_script_source>::value,
    "script source must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_script_resource_adapter_v1>::value,
    "script adapter must remain C-compatible");

int main() {
    auto request = vxml_script_request_v1 VXML_SCRIPT_REQUEST_V1_INIT;
    auto acquire_fn = &vxml_script_resource_acquire;
    auto close_fn = &vxml_script_resource_close;
    return request.abi_version == VXML_SCRIPT_REQUEST_ABI_V1 &&
           request.struct_size == sizeof(request) &&
           acquire_fn != nullptr && close_fn != nullptr
        ? 0 : 1;
}
