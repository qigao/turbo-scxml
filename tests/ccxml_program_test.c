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
            "<createcall/></transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};

        check_equal(
            compile_source(&program, source, &diagnostic),
            CCXML_UNSUPPORTED_FEATURE);
        check_null(program.impl);
    }
}
