#include <voicexml/quickjs.h>
#include <voicexml/script_resource.h>

#include "tinytest.h"
#include "voicexml_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static vxml_quickjs_session_options_v1 session_options(void) {
    return vxml_quickjs_default_session_options();
}

spec("VoiceXML QuickJS script target profile") {
    it("keeps base and static script profiles fail-closed for srcexpr") {
        static const char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script srcexpr=\"'scripts/main.js'\"/>"
            "</block></form></vxml>";
        vxml_program program = {0};

        check_equal(
            vxml_compile(
                document, sizeof(document) - 1u,
                NULL, &program, NULL),
            VXML_UNSUPPORTED_FEATURE);
        check_null(program.impl);
        check_equal(
            vxml_compile_external_script_profile(
                document, sizeof(document) - 1u,
                NULL, &program, NULL),
            VXML_UNSUPPORTED_FEATURE);
        check_null(program.impl);
    }

    it("compiles immutable srcexpr metadata and validates syntax") {
        char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script "
            "srcexpr=\"'scripts/' + 'main.js'\" charset='utf-8'/>"
            "</block></form></vxml>";
        const vxml_quickjs_compile_options_v1 options =
            vxml_quickjs_default_compile_options();
        vxml_program program = {0};

        check_equal(
            vxml_compile_quickjs_script_profile(
                document, strlen(document),
                NULL, &options, &program, NULL),
            VXML_OK);
        memset(document, 'x', sizeof(document) - 1u);
        {
            const vxml_program_impl *impl =
                (const vxml_program_impl *)program.impl;
            const vxml_action_row *action;
            check_not_null(impl);
            check_equal(impl->profile_kind, VXML_PROFILE_QUICKJS);
            check_equal(impl->action_count, (size_t)1u);
            action = &impl->actions[0];
            check_equal(action->kind, VXML_ACTION_SCRIPT_EXTERNAL);
            check_null(action->script_src);
            check_equal(action->script_src_size, (size_t)0u);
            check_not_null(action->script_srcexpr);
            check_equal(
                action->script_srcexpr_size,
                sizeof("'scripts/' + 'main.js'") - 1u);
            check_equal(
                memcmp(
                    action->script_srcexpr,
                    "'scripts/' + 'main.js'",
                    action->script_srcexpr_size),
                0);
            check_equal(
                action->script_charset_size,
                sizeof("UTF-8") - 1u);
            check_equal(
                memcmp(
                    action->script_charset, "UTF-8",
                    action->script_charset_size),
                0);
        }
        vxml_program_destroy(&program);
    }

    it("keeps static src target handoff unchanged") {
        static const char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script src='scripts/main.js'/></block>"
            "</form></vxml>";
        const vxml_quickjs_compile_options_v1 compile =
            vxml_quickjs_default_compile_options();
        const vxml_quickjs_session_options_v1 options =
            session_options();
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_external_script_target_v1 target = {0};

        check_equal(
            vxml_compile_quickjs_script_profile(
                document, sizeof(document) - 1u,
                NULL, &compile, &program, NULL),
            VXML_OK);
        check_equal(
            vxml_session_init_quickjs(
                &session, &program, &options),
            VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(
            vxml_session_get_state(&session),
            VXML_SESSION_SCRIPTING);
        check_equal(
            vxml_session_script(&session, &target), VXML_OK);
        check_equal(
            target.src_size, sizeof("scripts/main.js") - 1u);
        check_equal(
            memcmp(
                target.src, "scripts/main.js",
                target.src_size),
            0);
        check_equal(
            target.charset_size, sizeof("UTF-8") - 1u);
        check_equal(
            memcmp(target.charset, "UTF-8", target.charset_size),
            0);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }

    it("evaluates srcexpr once and publishes Session-owned target bytes") {
        char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script "
            "srcexpr=\"(globalThis.n=(globalThis.n||0)+1,"
            "'scripts/'+globalThis.n+'.js')\"/>"
            "</block></form></vxml>";
        const vxml_quickjs_compile_options_v1 compile =
            vxml_quickjs_default_compile_options();
        const vxml_quickjs_session_options_v1 options =
            session_options();
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_external_script_target_v1 first = {0};
        vxml_external_script_target_v1 second = {0};

        check_equal(
            vxml_compile_quickjs_script_profile(
                document, strlen(document),
                NULL, &compile, &program, NULL),
            VXML_OK);
        memset(document, 'x', sizeof(document) - 1u);
        check_equal(
            vxml_session_init_quickjs(
                &session, &program, &options),
            VXML_OK);
        check_equal(vxml_session_start(&session), VXML_OK);
        check_equal(
            vxml_session_get_state(&session),
            VXML_SESSION_SCRIPTING);
        check_equal(
            vxml_session_script(&session, &first), VXML_OK);
        check_equal(
            first.src_size, sizeof("scripts/1.js") - 1u);
        check_equal(
            memcmp(first.src, "scripts/1.js", first.src_size), 0);
        check_equal(
            vxml_session_script(&session, &second), VXML_OK);
        check_equal(first.src, second.src);
        check_equal(first.src_size, second.src_size);
        check_equal(
            memcmp(second.src, "scripts/1.js", second.src_size), 0);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }

    it("rejects invalid srcexpr language and syntax before Session init") {
        static const char *const documents[] = {
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script src='a.js' srcexpr=\"'b.js'\"/>"
            "</block></form></vxml>",
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script srcexpr='('/></block></form></vxml>",
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script srcexpr=\"'a.js'\">inline()</script>"
            "</block></form></vxml>"
        };
        static const vxml_status expected[] = {
            VXML_INVALID_STRUCTURE,
            VXML_SEMANTIC_ERROR,
            VXML_INVALID_STRUCTURE
        };
        const vxml_quickjs_compile_options_v1 options =
            vxml_quickjs_default_compile_options();
        size_t index;

        for (index = 0u;
             index < sizeof(documents) / sizeof(documents[0]);
             ++index) {
            vxml_program program = {0};
            vxml_diagnostic diagnostic = {0};
            check_equal(
                vxml_compile_quickjs_script_profile(
                    documents[index], strlen(documents[index]),
                    NULL, &options, &program, &diagnostic),
                expected[index]);
            check_null(program.impl);
            check_equal(diagnostic.status, expected[index]);
        }
    }

    it("rejects srcexpr source overflow during compile") {
        static const char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script srcexpr=\"'scripts/main.js'\"/>"
            "</block></form></vxml>";
        vxml_quickjs_compile_options_v1 options =
            vxml_quickjs_default_compile_options();
        vxml_program program = {0};
        vxml_diagnostic diagnostic = {0};

        options.max_expression_bytes = 4u;
        check_equal(
            vxml_compile_quickjs_script_profile(
                document, sizeof(document) - 1u,
                NULL, &options, &program, &diagnostic),
            VXML_LIMIT_EXCEEDED);
        check_null(program.impl);
        check_equal(diagnostic.status, VXML_LIMIT_EXCEEDED);
    }

    it("fails non-string empty exception and deadline expressions before target publication") {
        static const char *const expressions[] = {
            "42",
            "''",
            "(()=>{throw new Error('boom')})()",
            "(()=>{for(;;){} })()"
        };
        static const vxml_status expected[] = {
            VXML_SEMANTIC_ERROR,
            VXML_SEMANTIC_ERROR,
            VXML_SEMANTIC_ERROR,
            VXML_LIMIT_EXCEEDED
        };
        size_t index;

        for (index = 0u;
             index < sizeof(expressions) / sizeof(expressions[0]);
             ++index) {
            char document[512];
            vxml_quickjs_compile_options_v1 compile =
                vxml_quickjs_default_compile_options();
            const vxml_quickjs_session_options_v1 options =
                session_options();
            vxml_program program = {0};
            vxml_session session = {0};
            const char *event = NULL;
            size_t event_size = 0u;
            int written;

            compile.max_eval_milliseconds = UINT64_C(5);
            written = snprintf(
                document, sizeof(document),
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block><script srcexpr=\"%s\"/></block>"
                "</form></vxml>",
                expressions[index]);
            check_true(written > 0);
            check_true((size_t)written < sizeof(document));
            check_equal(
                vxml_compile_quickjs_script_profile(
                    document, (size_t)written,
                    NULL, &compile, &program, NULL),
                VXML_OK);
            check_equal(
                vxml_session_init_quickjs(
                    &session, &program, &options),
                VXML_OK);
            check_equal(
                vxml_session_start(&session),
                expected[index]);
            check_equal(
                vxml_session_get_state(&session),
                VXML_SESSION_FAILED);
            check_equal(
                vxml_quickjs_session_last_event(
                    &session, &event, &event_size),
                VXML_OK);
            check_equal(
                event_size, sizeof("error.semantic") - 1u);
            check_equal(
                memcmp(event, "error.semantic", event_size), 0);
            vxml_session_destroy(&session);
            vxml_program_destroy(&program);
        }
    }

    it("rejects oversized dynamic URI before SCRIPTING target publication") {
        static const char document[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><script srcexpr=\"'abcde'\"/></block>"
            "</form></vxml>";
        vxml_quickjs_compile_options_v1 compile =
            vxml_quickjs_default_compile_options();
        const vxml_quickjs_session_options_v1 options =
            session_options();
        vxml_program program = {0};
        vxml_session session = {0};
        vxml_external_script_target_v1 target = {0};

        compile.max_dynamic_script_uri_bytes = 4u;
        check_equal(
            vxml_compile_quickjs_script_profile(
                document, sizeof(document) - 1u,
                NULL, &compile, &program, NULL),
            VXML_OK);
        check_equal(
            vxml_session_init_quickjs(
                &session, &program, &options),
            VXML_OK);
        check_equal(
            vxml_session_start(&session),
            VXML_LIMIT_EXCEEDED);
        check_equal(
            vxml_session_get_state(&session),
            VXML_SESSION_FAILED);
        check_equal(
            vxml_session_script(&session, &target),
            VXML_INVALID_STATE);

        vxml_session_destroy(&session);
        vxml_program_destroy(&program);
    }
}
