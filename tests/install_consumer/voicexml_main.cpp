#include <voicexml/voicexml.h>

#include <cstring>

int main() {
    static constexpr char document[] =
        "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.0'>"
        "<form><block><exit/></block></form></vxml>";
    vxml_program program{};
    vxml_session session{};
    int result = 1;

    if (vxml_compile(document, std::strlen(document), nullptr, &program,
                     nullptr) != VXML_OK)
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
