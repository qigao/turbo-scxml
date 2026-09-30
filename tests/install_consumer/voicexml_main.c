#include <voicexml/voicexml.h>

#include <string.h>

int main(void) {
    static const char document[] =
        "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
        "<form id='main'><block>"
        "<goto next='next.vxml#target'/>"
        "</block></form></vxml>";
    static const char expected[] = "next.vxml#target";
    vxml_program program = {0};
    vxml_session session = {0};
    vxml_navigation_target target = {0};
    vxml_status (*raise_event)(
        vxml_session *, const char *, size_t) = vxml_session_raise_event;
    int result = 1;

    if (raise_event == NULL)
        return 2;

    if (vxml_compile(document, strlen(document), NULL, &program, NULL) !=
        VXML_OK)
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
        memcmp(target.uri, expected, target.uri_size) != 0)
        goto cleanup;
    result = 0;

cleanup:
    vxml_session_destroy(&session);
    vxml_program_destroy(&program);
    return result;
}
