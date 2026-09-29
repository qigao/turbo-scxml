#include <voicexml/voicexml.h>

#include <cstring>

int main() {
    static constexpr char document[] =
        "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.0'>"
        "<form id='main'><block>"
        "<goto next='next.vxml#target'/>"
        "</block></form></vxml>";
    static constexpr char expected[] = "next.vxml#target";
    vxml_program program{};
    vxml_session session{};
    vxml_navigation_target target{};
    int result = 1;

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
    result = 0;

cleanup:
    vxml_session_destroy(&session);
    vxml_program_destroy(&program);
    return result;
}
