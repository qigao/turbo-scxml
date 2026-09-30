#include <voicexml/submit_resource.h>

#include <type_traits>

static_assert(
    std::is_standard_layout<vxml_submit_field_v1>::value,
    "submit field must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_request_v1>::value,
    "submit request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_wire_request_v1>::value,
    "submit wire request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_response>::value,
    "submit response must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_resource_adapter_v1>::value,
    "submit adapter must remain C-compatible");

int main() {
    auto request = vxml_submit_request_v1 VXML_SUBMIT_REQUEST_V1_INIT;
    auto execute_fn = &vxml_submit_resource_execute;
    auto close_fn = &vxml_submit_resource_close;
    return request.abi_version == VXML_SUBMIT_REQUEST_ABI_V1 &&
           request.struct_size == sizeof(request) &&
           request.method == VXML_SUBMIT_METHOD_GET &&
           execute_fn != nullptr && close_fn != nullptr
        ? 0 : 1;
}
