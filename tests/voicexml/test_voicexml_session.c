#include <voicexml/voicexml.h>

#include "voicexml_internal.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static vxml_status compile_program(const char *source, vxml_program *program) {
    return vxml_compile(source, strlen(source), NULL, program, NULL);
}

spec("VoiceXML session") {
    group("initialization") {
        it("starts READY with no error") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block/></form></vxml>";
            vxml_program program = {0};
            vxml_session session = {0};

            check_equal(compile_program(source, &program), VXML_OK);
            check_equal(vxml_session_init(&session, &program), VXML_OK);
            check_equal(vxml_session_get_state(&session), VXML_SESSION_READY);
            check_equal(vxml_session_error(&session), VXML_OK);

            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }

        it("rejects missing program handles with an empty session") {
            vxml_program empty_program = {0};
            vxml_session session = {(void *)1};

            check_equal(vxml_session_init(&session, NULL),
                        VXML_INVALID_ARGUMENT);
            check_null(session.impl);
            check_equal(vxml_session_init(&session, &empty_program),
                        VXML_INVALID_ARGUMENT);
            check_null(session.impl);
            check_equal(vxml_session_start(NULL), VXML_INVALID_ARGUMENT);
            check_equal(vxml_session_start(&session), VXML_INVALID_STATE);
            check_equal(vxml_session_get_state(NULL), VXML_SESSION_CLOSED);
            check_equal(vxml_session_get_state(&session), VXML_SESSION_CLOSED);
            check_equal(vxml_session_error(NULL), VXML_INVALID_ARGUMENT);
            check_equal(vxml_session_error(&session), VXML_INVALID_STATE);
            check_equal(vxml_session_close(NULL), VXML_INVALID_ARGUMENT);
            check_equal(vxml_session_close(&session), VXML_INVALID_STATE);
        }
    }

    group("execution") {
        it("exits immediately for an explicit exit action") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block><exit/></block></form></vxml>";
            vxml_program program = {0};
            vxml_session session = {0};

            check_equal(compile_program(source, &program), VXML_OK);
            check_equal(vxml_session_init(&session, &program), VXML_OK);
            check_equal(vxml_session_start(&session), VXML_OK);
            check_equal(vxml_session_get_state(&session), VXML_SESSION_EXITED);
            check_equal(vxml_session_error(&session), VXML_OK);

            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }

        it("exits after exhausting empty blocks in the entry form") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block/><block/></form></vxml>";
            vxml_program program = {0};
            vxml_session session = {0};

            check_equal(compile_program(source, &program), VXML_OK);
            check_equal(vxml_session_init(&session, &program), VXML_OK);
            check_equal(vxml_session_start(&session), VXML_OK);
            check_equal(vxml_session_get_state(&session), VXML_SESSION_EXITED);

            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }

        it("stops at exit before validating the next entry-form block") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block><exit/></block><block><exit/></block></form>"
                "</vxml>";
            vxml_program program = {0};
            vxml_program_impl *impl;
            vxml_session session = {0};
            const vxml_status status = compile_program(source, &program);

            check_equal(status, VXML_OK);
            if (status == VXML_OK) {
                impl = (vxml_program_impl *)program.impl;
                impl->blocks[1].first_action = impl->action_count;
                check_equal(vxml_session_init(&session, &program), VXML_OK);
                check_equal(vxml_session_start(&session), VXML_OK);
                check_equal(vxml_session_get_state(&session),
                            VXML_SESSION_EXITED);
                check_equal(vxml_session_error(&session), VXML_OK);
            }

            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }

        it("fails when the entry form exceeds its declared block range") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block/><block><exit/></block></form></vxml>";
            vxml_program program = {0};
            vxml_program_impl *impl;
            vxml_session session = {0};
            const vxml_status status = compile_program(source, &program);

            check_equal(status, VXML_OK);
            if (status == VXML_OK) {
                impl = (vxml_program_impl *)program.impl;
                impl->block_count = 1u;
                check_equal(vxml_session_init(&session, &program), VXML_OK);
                check_equal(vxml_session_start(&session),
                            VXML_INVALID_STRUCTURE);
                check_equal(vxml_session_get_state(&session),
                            VXML_SESSION_FAILED);
                check_equal(vxml_session_error(&session),
                            VXML_INVALID_STRUCTURE);
            }

            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }

        it("fails when a block action exceeds its declared action range") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block><exit/></block><block><exit/></block></form>"
                "</vxml>";
            vxml_program program = {0};
            vxml_program_impl *impl;
            vxml_session session = {0};
            const vxml_status status = compile_program(source, &program);

            check_equal(status, VXML_OK);
            if (status == VXML_OK) {
                impl = (vxml_program_impl *)program.impl;
                impl->actions[0].kind = (vxml_action_kind)99;
                impl->blocks[0].first_action = 1u;
                impl->action_count = 1u;
                check_equal(vxml_session_init(&session, &program), VXML_OK);
                check_equal(vxml_session_start(&session),
                            VXML_INVALID_STRUCTURE);
                check_equal(vxml_session_get_state(&session),
                            VXML_SESSION_FAILED);
                check_equal(vxml_session_error(&session),
                            VXML_INVALID_STRUCTURE);
            }

            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }

        it("rejects an overflowing action start without dereferencing it") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block><exit/></block></form></vxml>";
            vxml_program program = {0};
            vxml_program_impl *impl;
            vxml_session session = {0};
            const vxml_status status = compile_program(source, &program);

            check_equal(status, VXML_OK);
            if (status == VXML_OK) {
                impl = (vxml_program_impl *)program.impl;
                impl->blocks[0].first_action = SIZE_MAX;
                check_equal(vxml_session_init(&session, &program), VXML_OK);
                check_equal(vxml_session_start(&session),
                            VXML_INVALID_STRUCTURE);
                check_equal(vxml_session_get_state(&session),
                            VXML_SESSION_FAILED);
                check_equal(vxml_session_error(&session),
                            VXML_INVALID_STRUCTURE);
            }

            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }

        it("does not traverse a later form") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block/></form><form><block><exit/></block></form></vxml>";
            vxml_program program = {0};
            vxml_program_impl *impl;
            vxml_session session = {0};
            const vxml_status status = compile_program(source, &program);

            check_equal(status, VXML_OK);
            if (status == VXML_OK) {
                impl = (vxml_program_impl *)program.impl;
                impl->blocks[1].first_action = impl->action_count + 1u;
                check_equal(vxml_session_init(&session, &program), VXML_OK);
                check_equal(vxml_session_start(&session), VXML_OK);
                check_equal(vxml_session_get_state(&session),
                            VXML_SESSION_EXITED);
            }

            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }

        it("fails stably when an entry-form action is structurally impossible") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block><exit/></block></form></vxml>";
            vxml_program program = {0};
            vxml_program_impl *impl;
            vxml_session session = {0};
            const vxml_status status = compile_program(source, &program);

            check_equal(status, VXML_OK);
            if (status == VXML_OK) {
                impl = (vxml_program_impl *)program.impl;
                impl->actions[0].kind = (vxml_action_kind)99;
                check_equal(vxml_session_init(&session, &program), VXML_OK);
                check_equal(vxml_session_start(&session),
                            VXML_INVALID_STRUCTURE);
                check_equal(vxml_session_get_state(&session),
                            VXML_SESSION_FAILED);
                check_equal(vxml_session_error(&session),
                            VXML_INVALID_STRUCTURE);
                check_equal(vxml_session_start(&session), VXML_INVALID_STATE);
                check_equal(vxml_session_error(&session),
                            VXML_INVALID_STRUCTURE);
            }

            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }
    }

        it("follows local goto with fetchaudio without entering fetch navigation") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form id='first'><block>"
                "<goto next='#third' fetchaudio='wait.wav'/>"
                "</block></form>"
                "<form id='second'><block><exit/></block></form>"
                "<form id='third'><block><goto next='#second'/></block></form>"
                "</vxml>";
            vxml_program program = {0};
            vxml_session session = {0};

            check_equal(compile_program(source, &program), VXML_OK);
            check_equal(vxml_session_init(&session, &program), VXML_OK);
            check_equal(vxml_session_start(&session), VXML_OK);
            check_equal(vxml_session_get_state(&session), VXML_SESSION_EXITED);
            check_equal(vxml_session_error(&session), VXML_OK);

            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }

        it("exposes external goto fetchaudio through the versioned navigation request") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form id='first'><block>"
                "<goto next='dialogs/next.vxml#target' "
                "fetchaudio='media/wait.wav'/>"
                "</block></form></vxml>";
            static const char expected[] = "dialogs/next.vxml#target";
            static const char wait_audio[] = "media/wait.wav";
            vxml_program program = {0};
            vxml_session session = {0};
            vxml_navigation_target target = {0};
            vxml_navigation_request_v1 navigation = {0};

            check_equal(compile_program(source, &program), VXML_OK);
            check_equal(vxml_session_init(&session, &program), VXML_OK);
            check_equal(vxml_session_start(&session), VXML_OK);
            check_equal(
                vxml_session_get_state(&session),
                VXML_SESSION_NAVIGATING);
            check_equal(
                vxml_session_navigation(&session, &target), VXML_OK);
            check_equal(target.uri_size, sizeof(expected) - 1u);
            check_equal(target.uri, expected);
            check_equal(
                vxml_session_navigation_request(
                    &session, &navigation), VXML_OK);
            check_equal(
                navigation.abi_version,
                VXML_NAVIGATION_REQUEST_ABI_V1);
            check_equal(
                navigation.struct_size,
                sizeof(vxml_navigation_request_v1));
            check_equal(navigation.uri, expected);
            check_equal(
                navigation.fetchaudio_uri_size,
                sizeof(wait_audio) - 1u);
            check_equal(
                navigation.fetchaudio_uri,
                wait_audio);
            check_equal(
                vxml_session_start(&session),
                VXML_INVALID_STATE);
            check_equal(vxml_session_close(&session), VXML_OK);
            check_equal(
                vxml_session_navigation_request(
                    &session, &navigation),
                VXML_CLOSED);

            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }

        it("yields one immutable literal submit handoff") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form id='first'><block>"
                "<submit next='result.vxml#done' method='post' "
                "enctype='application/x-www-form-urlencoded'/>"
                "</block></form></vxml>";
            static const char expected[] = "result.vxml#done";
            static const vxml_submit_field_v1 fields[] = {
                {"alpha", sizeof("alpha") - 1u,
                 "one", sizeof("one") - 1u}};
            vxml_program program = {0};
            vxml_session session = {0};
            vxml_submit_target_v1 submit = {0};
            vxml_submit_target_v2 submit_v2 = {0};
            vxml_submit_target_v3 submit_v3 = {0};

            check_equal(compile_program(source, &program), VXML_OK);
            check_equal(vxml_session_init(&session, &program), VXML_OK);
            check_equal(vxml_session_start(&session), VXML_OK);
            check_equal(
                vxml_session_get_state(&session),
                VXML_SESSION_SUBMITTING);
            check_equal(
                vxml_session_submit(&session, &submit), VXML_OK);
            check_equal(
                submit.abi_version, VXML_SUBMIT_TARGET_ABI_V1);
            check_equal(
                submit.struct_size, sizeof(vxml_submit_target_v1));
            check_equal(submit.uri, expected);
            check_equal(submit.uri_size, sizeof(expected) - 1u);
            check_equal(
                submit.method, VXML_SUBMIT_METHOD_POST);
            check_equal(
                submit.enctype, VXML_SUBMIT_ENCTYPE_URLENCODED);
            check_equal(
                vxml_session_submit_v2(&session, &submit_v2),
                VXML_OK);
            check_equal(
                submit_v2.abi_version, VXML_SUBMIT_TARGET_ABI_V2);
            check_equal(
                submit_v2.struct_size, sizeof(vxml_submit_target_v2));
            check_equal(submit_v2.uri, expected);
            check_equal(submit_v2.uri_size, sizeof(expected) - 1u);
            check_equal(
                submit_v2.method, VXML_SUBMIT_METHOD_POST);
            check_equal(
                submit_v2.enctype, VXML_SUBMIT_ENCTYPE_URLENCODED);
            check_null(submit_v2.fields);
            check_equal(submit_v2.field_count, (size_t)0u);
            check_equal(
                vxml_session_submit_v3(&session, &submit_v3),
                VXML_OK);
            check_equal(
                submit_v3.abi_version, VXML_SUBMIT_TARGET_ABI_V3);
            check_false(submit_v3.has_timeout);
            check_equal(submit_v3.timeout_us, UINT64_C(0));
            check_null(submit_v3.fetchaudio_uri);
            check_equal(submit_v3.fetchaudio_uri_size, (size_t)0u);
            {
                vxml_session_impl *impl =
                    (vxml_session_impl *)session.impl;
                check_not_null(impl);
                impl->submit_fields = fields;
                impl->submit_field_count = 1u;
                check_equal(
                    vxml_session_submit(&session, &submit),
                    VXML_UNSUPPORTED_FEATURE);
                check_equal(
                    vxml_session_submit_v2(&session, &submit_v2),
                    VXML_OK);
                check_true(submit_v2.fields == fields);
                check_equal(submit_v2.field_count, (size_t)1u);

                impl->submit_has_timeout = true;
                impl->submit_timeout_us = UINT64_C(2500000);
                impl->submit_fetchaudio_uri = "wait.wav";
                impl->submit_fetchaudio_uri_size =
                    sizeof("wait.wav") - 1u;
                impl->submit_has_fetchaudio_delay = true;
                impl->submit_fetchaudio_delay_us =
                    UINT64_C(100000);
                check_equal(
                    vxml_session_submit_v2(&session, &submit_v2),
                    VXML_UNSUPPORTED_FEATURE);
                check_equal(
                    vxml_session_submit(&session, &submit),
                    VXML_UNSUPPORTED_FEATURE);
                check_equal(
                    vxml_session_submit_v3(&session, &submit_v3),
                    VXML_OK);
                check_true(submit_v3.has_timeout);
                check_equal(
                    submit_v3.timeout_us, UINT64_C(2500000));
                check_equal(
                    submit_v3.fetchaudio_uri_size,
                    sizeof("wait.wav") - 1u);
                check_equal(
                    memcmp(
                        submit_v3.fetchaudio_uri, "wait.wav",
                        submit_v3.fetchaudio_uri_size),
                    0);
                check_true(submit_v3.has_fetchaudio_delay);
                check_equal(
                    submit_v3.fetchaudio_delay_us,
                    UINT64_C(100000));
            }
            check_equal(
                vxml_session_navigation(
                    &session, &(vxml_navigation_target){0}),
                VXML_INVALID_STATE);
            check_equal(vxml_session_close(&session), VXML_OK);
            check_equal(
                vxml_session_submit(&session, &submit), VXML_CLOSED);
            check_equal(
                vxml_session_submit_v2(&session, &submit_v2),
                VXML_CLOSED);
            check_equal(
                vxml_session_submit_v3(&session, &submit_v3),
                VXML_CLOSED);

            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }

        it("starts a literal session at one named form for external fragment handoff") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form id='first'><block><exit/></block></form>"
                "<form id='second'><block>"
                "<goto next='next.vxml'/>"
                "</block></form></vxml>";
            static const char expected[] = "next.vxml";
            vxml_program program = {0};
            vxml_session session = {0};
            vxml_navigation_target target = {0};

            check_equal(compile_program(source, &program), VXML_OK);
            check_equal(vxml_session_init(&session, &program), VXML_OK);
            check_equal(
                vxml_session_start_at_form(
                    &session, "second", sizeof("second") - 1u),
                VXML_OK);
            check_equal(
                vxml_session_get_state(&session),
                VXML_SESSION_NAVIGATING);
            check_equal(
                vxml_session_navigation(&session, &target), VXML_OK);
            check_equal(target.uri, expected);
            check_equal(target.uri_size, sizeof(expected) - 1u);

            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }

        it("fails a start-at-form request when the compiled form ID is absent") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form id='first'><block><exit/></block></form></vxml>";
            vxml_program program = {0};
            vxml_session session = {0};

            check_equal(compile_program(source, &program), VXML_OK);
            check_equal(vxml_session_init(&session, &program), VXML_OK);
            check_equal(
                vxml_session_start_at_form(
                    &session, "missing", sizeof("missing") - 1u),
                VXML_INVALID_STRUCTURE);
            check_equal(
                vxml_session_get_state(&session),
                VXML_SESSION_FAILED);
            check_equal(
                vxml_session_error(&session),
                VXML_INVALID_STRUCTURE);

            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }

        it("fails closed when a compiled goto graph is corrupted into a cycle") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form id='first'><block><goto next='#second'/></block></form>"
                "<form id='second'><block><exit/></block></form>"
                "</vxml>";
            vxml_program program = {0};
            vxml_program_impl *impl;
            vxml_session session = {0};

            check_equal(compile_program(source, &program), VXML_OK);
            impl = (vxml_program_impl *)program.impl;
            check_not_null(impl);
            if (impl != NULL) {
                impl->actions[1].kind = VXML_ACTION_GOTO;
                impl->actions[1].target_form = 0u;
                check_equal(vxml_session_init(&session, &program), VXML_OK);
                check_equal(
                    vxml_session_start(&session),
                    VXML_INVALID_STRUCTURE);
                check_equal(
                    vxml_session_get_state(&session),
                    VXML_SESSION_FAILED);
                check_equal(
                    vxml_session_error(&session),
                    VXML_INVALID_STRUCTURE);
            }

            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }

    group("terminal lifecycle") {
        it("rejects repeated start without changing an exited session") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block/></form></vxml>";
            vxml_program program = {0};
            vxml_session session = {0};

            check_equal(compile_program(source, &program), VXML_OK);
            check_equal(vxml_session_init(&session, &program), VXML_OK);
            check_equal(vxml_session_start(&session), VXML_OK);
            check_equal(vxml_session_start(&session), VXML_INVALID_STATE);
            check_equal(vxml_session_get_state(&session), VXML_SESSION_EXITED);
            check_equal(vxml_session_error(&session), VXML_OK);

            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }

        it("closes idempotently from READY EXITED and FAILED") {
            static const char exited_source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block/></form></vxml>";
            static const char failed_source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block><exit/></block></form></vxml>";
            vxml_program exited_program = {0};
            vxml_program failed_program = {0};
            vxml_program_impl *failed_impl;
            vxml_session ready = {0};
            vxml_session exited = {0};
            vxml_session failed = {0};
            const vxml_status exited_status =
                compile_program(exited_source, &exited_program);
            const vxml_status failed_status =
                compile_program(failed_source, &failed_program);

            check_equal(exited_status, VXML_OK);
            check_equal(failed_status, VXML_OK);
            if (exited_status == VXML_OK && failed_status == VXML_OK) {
                failed_impl = (vxml_program_impl *)failed_program.impl;
                failed_impl->actions[0].kind = (vxml_action_kind)99;
                check_equal(vxml_session_init(&ready, &exited_program), VXML_OK);
                check_equal(vxml_session_init(&exited, &exited_program), VXML_OK);
                check_equal(vxml_session_init(&failed, &failed_program), VXML_OK);
                check_equal(vxml_session_start(&exited), VXML_OK);
                check_equal(vxml_session_start(&failed), VXML_INVALID_STRUCTURE);
                check_equal(vxml_session_close(&ready), VXML_OK);
                check_equal(vxml_session_close(&exited), VXML_OK);
                check_equal(vxml_session_close(&failed), VXML_OK);
                check_equal(vxml_session_close(&ready), VXML_OK);
                check_equal(vxml_session_close(&exited), VXML_OK);
                check_equal(vxml_session_close(&failed), VXML_OK);
                check_equal(vxml_session_get_state(&ready), VXML_SESSION_CLOSED);
                check_equal(vxml_session_get_state(&exited), VXML_SESSION_CLOSED);
                check_equal(vxml_session_get_state(&failed), VXML_SESSION_CLOSED);
                check_equal(vxml_session_start(&ready), VXML_CLOSED);
                check_equal(vxml_session_start(&exited), VXML_CLOSED);
                check_equal(vxml_session_start(&failed), VXML_CLOSED);
                check_equal(vxml_session_error(&ready), VXML_CLOSED);
                check_equal(vxml_session_error(&exited), VXML_CLOSED);
                check_equal(vxml_session_error(&failed), VXML_CLOSED);
            }

            vxml_session_destroy(&ready);
            vxml_session_destroy(&exited);
            vxml_session_destroy(&failed);
            vxml_program_destroy(&exited_program);
            vxml_program_destroy(&failed_program);
        }

        it("destroys only session storage and leaves the program usable") {
            static const char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block><exit/></block></form></vxml>";
            vxml_program program = {0};
            vxml_session first = {0};
            vxml_session second = {0};

            check_equal(compile_program(source, &program), VXML_OK);
            check_equal(vxml_session_init(&first, &program), VXML_OK);
            vxml_session_destroy(&first);
            check_null(first.impl);
            check_not_null(program.impl);
            check_equal(vxml_session_init(&second, &program), VXML_OK);
            check_equal(vxml_session_start(&second), VXML_OK);
            check_equal(vxml_session_get_state(&second), VXML_SESSION_EXITED);

            vxml_session_destroy(&first);
            vxml_session_destroy(&second);
            vxml_program_destroy(&program);
        }
    }
}
