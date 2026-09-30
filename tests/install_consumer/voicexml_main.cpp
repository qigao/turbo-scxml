#include <voicexml/voicexml.h>

#include <cstring>
#include <type_traits>

static_assert(
    std::is_standard_layout<vxml_navigation_request_v1>::value,
    "navigation request must remain C-compatible");
static_assert(
    std::is_standard_layout<vxml_submit_target_v1>::value,
    "submit target must remain C-compatible");

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
    auto submit_fn = &vxml_session_submit;
    auto raise_event = &vxml_session_raise_event;
    int result = 1;

    if (raise_event == nullptr || submit_fn == nullptr ||
        VXML_SUBMIT_TARGET_ABI_V1 == 0u ||
        VXML_SUBMIT_METHOD_GET == VXML_SUBMIT_METHOD_POST ||
        submit.abi_version != 0u)
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
