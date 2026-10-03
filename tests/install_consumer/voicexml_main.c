#include <voicexml/voicexml.h>
#include <voicexml/data_resource.h>

#include <string.h>

int main(void) {
    static const char document[] =
        "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
        "<form id='main'><block>"
        "<goto next='next.vxml#target' fetchaudio='wait.wav'/>"
        "</block></form></vxml>";
    static const char expected[] = "next.vxml#target";
    static const char wait_audio[] = "wait.wav";
    vxml_program program = {0};
    vxml_session session = {0};
    vxml_navigation_target target = {0};
    vxml_navigation_request_v1 navigation = {0};
    vxml_submit_target_v1 submit = {0};
    vxml_submit_target_v2 submit_v2 = {0};
    vxml_cmeta_data_request_v2 data_v2 =
        VXML_CMETA_DATA_REQUEST_V2_INIT;
    vxml_cmeta_data_request_v3 data_v3 =
        VXML_CMETA_DATA_REQUEST_V3_INIT;
    vxml_status (*submit_fn)(
        const vxml_session *,
        vxml_submit_target_v1 *) = vxml_session_submit;
    vxml_status (*submit_v2_fn)(
        const vxml_session *,
        vxml_submit_target_v2 *) = vxml_session_submit_v2;
    vxml_status (*raise_event)(
        vxml_session *, const char *, size_t) = vxml_session_raise_event;
    int result = 1;

    if (raise_event == NULL || submit_fn == NULL ||
        submit_v2_fn == NULL ||
        VXML_SUBMIT_TARGET_ABI_V1 == 0u ||
        VXML_SUBMIT_TARGET_ABI_V2 <= VXML_SUBMIT_TARGET_ABI_V1 ||
        VXML_SUBMIT_METHOD_GET == VXML_SUBMIT_METHOD_POST ||
        submit.abi_version != 0u ||
        submit_v2.abi_version != 0u ||
        submit_v2.fields != NULL ||
        submit_v2.field_count != 0u ||
        data_v2.abi_version != VXML_CMETA_DATA_REQUEST_ABI_V2 ||
        data_v2.struct_size != sizeof(data_v2) ||
        data_v3.abi_version != VXML_CMETA_DATA_REQUEST_ABI_V3 ||
        data_v3.struct_size != sizeof(data_v3) ||
        data_v3.method != VXML_SUBMIT_METHOD_GET ||
        data_v3.enctype != VXML_SUBMIT_ENCTYPE_URLENCODED)
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
    if (vxml_session_navigation_request(
            &session, &navigation) != VXML_OK ||
        navigation.abi_version != VXML_NAVIGATION_REQUEST_ABI_V1 ||
        navigation.struct_size != sizeof(navigation) ||
        navigation.fetchaudio_uri_size != sizeof(wait_audio) - 1u ||
        memcmp(
            navigation.fetchaudio_uri, wait_audio,
            navigation.fetchaudio_uri_size) != 0)
        goto cleanup;
    result = 0;

cleanup:
    vxml_session_destroy(&session);
    vxml_program_destroy(&program);
    return result;
}
