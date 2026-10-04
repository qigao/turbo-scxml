#include <voicexml/voicexml.h>
#include <voicexml/data_resource.h>

#include <cstring>
#include <type_traits>

static_assert(
    std::is_standard_layout<vxml_navigation_request_v1>::value,
    "navigation request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_target_v1>::value,
    "submit target must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_target_v2>::value,
    "submit target V2 must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_target_v3>::value,
    "submit target V3 must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_target_v4>::value,
    "submit target V4 must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_recording_field_v1>::value,
    "submit recording view must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_multipart_part_ref_v1>::value,
    "submit multipart part ref must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_field_v1>::value,
    "submit field view must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_data_request_v2>::value,
    "data request V2 must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_data_request_v3>::value,
    "data request V3 must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_cmeta_data_resource_adapter_v1>::value,
    "data resource adapter must remain C-compatible");

int main() {
    static constexpr char document[] =
        "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.0'>"
        "<form id='main'><block>"
        "<goto next='next.vxml#target' fetchaudio='wait.wav'/>"
        "</block></form></vxml>";
    static constexpr char expected[] = "next.vxml#target";
    static constexpr char wait_audio[] = "wait.wav";
    vxml_program program{};
    vxml_session session{};
    vxml_navigation_target target{};
    vxml_navigation_request_v1 navigation{};
    vxml_submit_target_v1 submit{};
    vxml_submit_target_v2 submit_v2{};
    vxml_submit_target_v3 submit_v3{};
    vxml_submit_target_v4 submit_v4{};
    vxml_cmeta_data_request_v2 data_v2 =
        VXML_CMETA_DATA_REQUEST_V2_INIT;
    vxml_cmeta_data_request_v3 data_v3 =
        VXML_CMETA_DATA_REQUEST_V3_INIT;
    auto submit_fn = &vxml_session_submit;
    auto submit_v2_fn = &vxml_session_submit_v2;
    auto submit_v3_fn = &vxml_session_submit_v3;
    auto submit_v4_fn = &vxml_session_submit_v4;
    auto raise_event = &vxml_session_raise_event;
    int result = 1;

    if (raise_event == nullptr || submit_fn == nullptr ||
        submit_v2_fn == nullptr || submit_v3_fn == nullptr ||
        submit_v4_fn == nullptr ||
        VXML_SUBMIT_TARGET_ABI_V1 == 0u ||
        VXML_SUBMIT_TARGET_ABI_V2 <= VXML_SUBMIT_TARGET_ABI_V1 ||
        VXML_SUBMIT_TARGET_ABI_V3 <= VXML_SUBMIT_TARGET_ABI_V2 ||
        VXML_SUBMIT_TARGET_ABI_V4 <= VXML_SUBMIT_TARGET_ABI_V3 ||
        VXML_SUBMIT_METHOD_GET == VXML_SUBMIT_METHOD_POST ||
        submit.abi_version != 0u ||
        submit_v2.abi_version != 0u ||
        submit_v2.fields != nullptr ||
        submit_v2.field_count != 0u ||
        submit_v3.abi_version != 0u ||
        submit_v3.has_timeout ||
        submit_v3.timeout_us != UINT64_C(0) ||
        submit_v3.fetchaudio_uri != nullptr ||
        submit_v4.abi_version != 0u ||
        submit_v4.recordings != nullptr ||
        submit_v4.recording_count != 0u ||
        submit_v4.parts != nullptr ||
        submit_v4.part_count != 0u ||
        data_v2.abi_version != VXML_CMETA_DATA_REQUEST_ABI_V2 ||
        data_v2.struct_size != sizeof(data_v2) ||
        data_v3.abi_version != VXML_CMETA_DATA_REQUEST_ABI_V3 ||
        data_v3.struct_size != sizeof(data_v3) ||
        data_v3.method != VXML_SUBMIT_METHOD_GET ||
        data_v3.enctype != VXML_SUBMIT_ENCTYPE_URLENCODED)
        return 2;

    if (vxml_compile(document, std::strlen(document), nullptr, &program,
                     nullptr) != VXML_OK)
        goto cleanup;
    if (vxml_session_init(&session, &program) != VXML_OK)
        goto cleanup;
    if (vxml_session_start_at_form(
            &session, "main", sizeof("main") - 1u) != VXML_OK)
        goto cleanup;
    if (vxml_session_get_state(&session) != VXML_SESSION_NAVIGATING)
        goto cleanup;
    if (vxml_session_navigation(&session, &target) != VXML_OK)
        goto cleanup;
    if (target.uri_size != sizeof(expected) - 1u ||
        std::memcmp(target.uri, expected, target.uri_size) != 0)
        goto cleanup;
    if (vxml_session_navigation_request(
            &session, &navigation) != VXML_OK ||
        navigation.abi_version != VXML_NAVIGATION_REQUEST_ABI_V1 ||
        navigation.struct_size != sizeof(navigation) ||
        navigation.fetchaudio_uri_size != sizeof(wait_audio) - 1u ||
        std::memcmp(
            navigation.fetchaudio_uri, wait_audio,
            navigation.fetchaudio_uri_size) != 0)
        goto cleanup;
    result = 0;

cleanup:
    vxml_session_destroy(&session);
    vxml_program_destroy(&program);
    return result;
}
