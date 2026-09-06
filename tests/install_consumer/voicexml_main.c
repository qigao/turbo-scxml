#include <voicexml/voicexml.h>

#include <string.h>

int main(void) {
    static const char document[] =
        "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
        "<form id='main'><block><exit/></block></form></vxml>";
    vxml_program program = {0};
    vxml_session session = {0};
    int result = 1;

    if (vxml_compile(document, strlen(document), NULL, &program, NULL) !=
        VXML_OK)
        goto cleanup;
    if (vxml_session_init(&session, &program) != VXML_OK)
        goto cleanup;
    if (vxml_session_start(&session) != VXML_OK)
        goto cleanup;
    if (vxml_session_get_state(&session) != VXML_SESSION_EXITED)
        goto cleanup;
    result = 0;

cleanup:
    vxml_session_destroy(&session);
    vxml_program_destroy(&program);
    return result;
}
