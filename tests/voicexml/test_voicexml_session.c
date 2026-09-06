#include <voicexml/voicexml.h>

#include "voicexml_internal.h"
#include "tinytest.h"

#include <stddef.h>
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
