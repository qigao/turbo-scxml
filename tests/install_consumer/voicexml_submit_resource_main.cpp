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
    std::is_standard_layout<vxml_submit_body_segment_v1>::value,
    "submit body segment must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_wire_request_v2>::value,
    "segmented submit wire request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_recording_field_v1>::value,
    "submit recording field must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_multipart_part_ref_v1>::value,
    "multipart part ref must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_multipart_request_v1>::value,
    "multipart submit request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_response>::value,
    "submit response must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_resource_adapter_v1>::value,
    "submit adapter must remain C-compatible");

int main() {
    auto request = vxml_submit_request_v1 VXML_SUBMIT_REQUEST_V1_INIT;
    auto multipart =
        vxml_submit_multipart_request_v1
            VXML_SUBMIT_MULTIPART_REQUEST_V1_INIT;
    auto execute_fn = &vxml_submit_resource_execute;
    auto multipart_fn = &vxml_submit_resource_execute_multipart;
    auto close_fn = &vxml_submit_resource_close;
    return request.abi_version == VXML_SUBMIT_REQUEST_ABI_V1 &&
           request.struct_size == sizeof(request) &&
           request.method == VXML_SUBMIT_METHOD_GET &&
           multipart.abi_version ==
               VXML_SUBMIT_MULTIPART_REQUEST_ABI_V1 &&
           multipart.struct_size == sizeof(multipart) &&
           multipart.parts == nullptr &&
           multipart.part_count == 0u &&
           execute_fn != nullptr && multipart_fn != nullptr &&
           close_fn != nullptr
        ? 0 : 1;
}
