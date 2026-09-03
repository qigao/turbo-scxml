#include <ccxml/ccxml.h>

#include "ccxml_internal.h"
#include "tinytest.h"

#include <string.h>

static ccxml_status compile_source(
    ccxml_program *program, const char *source, ccxml_diagnostic *diagnostic) {
    return ccxml_compile(
        program, source, strlen(source), NULL, diagnostic);
}

spec("CCXML program") {
    it("copies a bounded eventprocessor program") {
        char source[] =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor>"
            "<transition event='connection.alerting'><accept/></transition>"
            "<transition event='connection.disconnected'><exit/></transition>"
            "</eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};

        check_equal(
            compile_source(&program, source, &diagnostic), CCXML_OK);
        memset(source, 'x', sizeof(source) - 1u);
        check_equal(ccxml_program_transition_count(&program), (size_t)2);
        check_equal(
            ccxml_program_transition_event(&program, 0u),
            "connection.alerting");
        check_equal(
            ccxml_program_transition_event(&program, 1u),
            "connection.disconnected");
        check_equal(ccxml_program_action_count(&program), (size_t)2);

        ccxml_program_destroy(&program);
    }

    it("rejects a non-CCXML namespace") {
        const char *source =
            "<ccxml xmlns='urn:not-ccxml' version='1.0'>"
            "<eventprocessor/></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};

        check_equal(
            compile_source(&program, source, &diagnostic),
            CCXML_INVALID_NAMESPACE);
        check_null(program.impl);
    }

    it("rejects a version other than 1.0") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='2.0'>"
            "<eventprocessor/></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};

        check_equal(
            compile_source(&program, source, &diagnostic),
            CCXML_INVALID_VERSION);
        check_null(program.impl);
    }

    it("requires exactly one eventprocessor") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor/><eventprocessor/></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};

        check_equal(
            compile_source(&program, source, &diagnostic),
            CCXML_INVALID_STRUCTURE);
        check_null(program.impl);
    }

    it("requires a nonempty transition event") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition><accept/></transition>"
            "</eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};

        check_equal(
            compile_source(&program, source, &diagnostic),
            CCXML_INVALID_STRUCTURE);
        check_null(program.impl);
    }

    it("rejects unsupported executable content") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='connection.alerting'>"
            "<join/></transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};

        check_equal(
            compile_source(&program, source, &diagnostic),
            CCXML_UNSUPPORTED_FEATURE);
        check_null(program.impl);
    }

    group("createcall") {
        it("accepts a quoted destination expression") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='ccxml.loaded'>"
                "<createcall dest=\"'tel:+12025550123'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            check_not_null(program.impl);
            check_equal(ccxml_program_action_count(&program), (size_t)1);

            ccxml_program_destroy(&program);
        }

        it("requires a destination") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='ccxml.loaded'>"
                "<createcall/></transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects an empty destination literal") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='ccxml.loaded'>"
                "<createcall dest=\"''\"/></transition>"
                "</eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects a nonliteral destination expression") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='ccxml.loaded'>"
                "<createcall dest='destination'/></transition>"
                "</eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects an escaped destination literal") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='ccxml.loaded'>"
                "<createcall dest=\"'tel:12\\\\34'\"/></transition>"
                "</eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects optional attributes outside the slice") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='ccxml.loaded'>"
                "<createcall dest=\"'tel:123'\" callerid=\"'tel:456'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects nested content") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='ccxml.loaded'>"
                "<createcall dest=\"'tel:123'\"><accept/></createcall>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }
    }

    group("disconnect") {
        it("accepts the empty default-target form") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<disconnect/></transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            check_not_null(program.impl);
            check_equal(ccxml_program_action_count(&program), (size_t)1);

            ccxml_program_destroy(&program);
        }

        it("rejects an explicit connection expression") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<disconnect connectionid=\"'call-7'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects optional attributes outside the slice") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<disconnect reason=\"'normal'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects nested executable content") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<disconnect><exit/></disconnect>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }
    }
}
