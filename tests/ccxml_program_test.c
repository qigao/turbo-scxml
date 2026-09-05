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

    it("defaults an omitted transition event to a wildcard") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition><accept/></transition>"
            "</eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};

        check_equal(compile_source(&program, source, &diagnostic), CCXML_OK);
        check_equal(ccxml_program_transition_count(&program), (size_t)1);
        check_equal(ccxml_program_transition_event(&program, 0u), "*");

        ccxml_program_destroy(&program);
    }

    it("requires transition state to have an eventprocessor statevariable") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition state='waiting' event='advance'>"
            "<exit/></transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};

        check_equal(
            compile_source(&program, source, &diagnostic),
            CCXML_INVALID_STRUCTURE);
        check_null(program.impl);
    }

    it("requires statevariable to name the root string variable") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<var name='other' expr=\"'waiting'\"/>"
            "<eventprocessor statevariable='mode'>"
            "<transition state='waiting' event='advance'><exit/></transition>"
            "</eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};

        check_equal(
            compile_source(&program, source, &diagnostic),
            CCXML_INVALID_STRUCTURE);
        check_null(program.impl);
    }

    it("accepts a retained transition condition") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<var name='mode' expr=\"'idle'\"/>"
            "<eventprocessor><transition event='advance' "
            "cond='mode == &quot;idle&quot;'><exit/></transition>"
            "</eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};

        check_equal(compile_source(&program, source, &diagnostic), CCXML_OK);
        check_equal(ccxml_program_transition_count(&program), (size_t)1);

        ccxml_program_destroy(&program);
    }

    it("rejects an empty transition condition") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='advance' cond=''>"
            "<exit/></transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};

        check_equal(
            compile_source(&program, source, &diagnostic),
            CCXML_INVALID_STRUCTURE);
        check_null(program.impl);
    }

    it("lowers an if elseif else action chain") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='connection.alerting'>"
            "<if cond='first'><accept/><elseif cond='second'/><reject/>"
            "<else/><disconnect/></if>"
            "</transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};

        check_equal(compile_source(&program, source, &diagnostic), CCXML_OK);
        check_equal(ccxml_program_action_count(&program), (size_t)7);

        ccxml_program_destroy(&program);
    }

    it("lowers conditional foreach content into bounded control rows") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='go'>"
            "<foreach array='targets' item='target' index='position'><accept/>"
            "<if cond='target == 2'><accept/></if>"
            "</foreach></transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};
        ccxml_program_impl *impl;

        check_equal(compile_source(&program, source, &diagnostic), CCXML_OK);
        check_equal(ccxml_program_action_count(&program), (size_t)6u);
        impl = (ccxml_program_impl *)program.impl;
        check_equal(impl->actions[0].kind, CCXML_ACTION_FOREACH);
        check_equal(impl->actions[1].kind, CCXML_ACTION_ACCEPT);
        check_equal(impl->actions[2].kind, CCXML_ACTION_IF);
        check_equal(impl->actions[3].kind, CCXML_ACTION_ACCEPT);
        check_equal(impl->actions[4].kind, CCXML_ACTION_ENDIF);
        check_equal(impl->actions[5].kind, CCXML_ACTION_ENDFOREACH);
        check_equal(impl->actions[0].branch_next, (size_t)1u);
        check_equal(impl->actions[0].block_end, (size_t)6u);
        check_equal(impl->actions[0].index, "position");
        check_equal(impl->actions[0].index_size, (size_t)8u);
        check_equal(impl->actions[2].branch_next, (size_t)4u);
        check_equal(impl->actions[2].block_end, (size_t)4u);
        check_equal(impl->actions[5].branch_next, (size_t)0u);
        check_equal(impl->max_transition_effects, (size_t)512u);
        check_equal(impl->max_foreach_iterations, (size_t)256u);

        ccxml_program_destroy(&program);
    }

    it("uses the configured foreach bound for effect admission") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition><foreach array='a' item='x'>"
            "<accept/></foreach></transition></eventprocessor></ccxml>";
        ccxml_limits limits = ccxml_default_limits();
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};
        ccxml_program_impl *impl;

        limits.max_foreach_iterations = 3u;
        check_equal(
            ccxml_compile(
                &program, source, strlen(source), &limits, &diagnostic),
            CCXML_OK);
        impl = (ccxml_program_impl *)program.impl;
        check_equal(impl->max_transition_effects, (size_t)3u);
        check_equal(impl->max_foreach_iterations, (size_t)3u);

        ccxml_program_destroy(&program);
    }

    it("rejects a nested foreach") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition><foreach array='a' item='x'>"
            "<if cond='x == 1'><foreach array='b' item='y'>"
            "<accept/></foreach></if></foreach>"
            "</transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};

        check_equal(
            compile_source(&program, source, &diagnostic),
            CCXML_UNSUPPORTED_FEATURE);
        check_null(program.impl);
    }

    it("rejects a dotted foreach item name") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition><foreach array='a' item='scope.x'>"
            "<accept/></foreach></transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};

        check_equal(
            compile_source(&program, source, &diagnostic),
            CCXML_UNSUPPORTED_FEATURE);
        check_null(program.impl);
    }

    it("rejects a dotted foreach index name") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition><foreach array='a' item='x' "
            "index='scope.i'><accept/></foreach>"
            "</transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};

        check_equal(
            compile_source(&program, source, &diagnostic),
            CCXML_UNSUPPORTED_FEATURE);
        check_null(program.impl);
    }

    it("rejects nonliteral string assignment expressions") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<eventprocessor><transition event='advance'>"
            "<assign name='mode' expr='nextMode'/>"
            "</transition></eventprocessor></ccxml>";
        ccxml_program program = {0};
        ccxml_diagnostic diagnostic = {0};

        check_equal(
            compile_source(&program, source, &diagnostic),
            CCXML_UNSUPPORTED_FEATURE);
        check_null(program.impl);
    }

    it("rejects assignment to an undeclared variable") {
        const char *source =
            "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
            "<var name='mode' expr=\"'waiting'\"/>"
            "<eventprocessor><transition event='advance'>"
            "<assign name='other' expr=\"'active'\"/>"
            "</transition></eventprocessor></ccxml>";
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
            "<dialogfoo/></transition></eventprocessor></ccxml>";
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
            const ccxml_program_impl *impl;

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            check_not_null(program.impl);
            check_equal(ccxml_program_action_count(&program), (size_t)1);
            impl = (const ccxml_program_impl *)program.impl;
            check_equal(impl->actions[0].destination, "tel:+12025550123");
            check_equal(impl->actions[0].destination_size, (size_t)16);
            check_false(impl->actions[0].destination_is_dynamic);
            check_false(impl->uses_string_expression);

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

        it("retains a nonliteral destination expression") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='ccxml.loaded'>"
                "<createcall dest='destination'/></transition>"
                "</eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            const ccxml_program_impl *impl;

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            impl = (const ccxml_program_impl *)program.impl;
            check_not_null(impl);
            check_equal(impl->actions[0].destination, "destination");
            check_equal(impl->actions[0].destination_size, (size_t)11);
            check_true(impl->actions[0].destination_is_dynamic);
            check_true(impl->uses_string_expression);

            ccxml_program_destroy(&program);
        }

        it("classifies entity-encoded quotes as a literal destination") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='ccxml.loaded'>"
                "<createcall dest='&apos;tel:123&apos;'/></transition>"
                "</eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            const ccxml_program_impl *impl;

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            impl = (const ccxml_program_impl *)program.impl;
            check_not_null(impl);
            check_equal(impl->actions[0].destination, "tel:123");
            check_equal(impl->actions[0].destination_size, (size_t)7);
            check_false(impl->actions[0].destination_is_dynamic);
            check_false(impl->uses_string_expression);

            ccxml_program_destroy(&program);
        }

        it("rejects an invalid entity in a dynamic destination") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='ccxml.loaded'>"
                "<createcall dest='destination&bogus;'/></transition>"
                "</eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
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

    group("reject") {
        it("accepts the empty default-target form") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.alerting'>"
                "<reject/></transition></eventprocessor></ccxml>";
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
                "<eventprocessor><transition event='connection.alerting'>"
                "<reject connectionid=\"'call-7'\"/>"
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
                "<eventprocessor><transition event='connection.alerting'>"
                "<reject reason=\"'busy'\"/>"
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
                "<eventprocessor><transition event='connection.alerting'>"
                "<reject><exit/></reject>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }
    }

    group("redirect") {
        it("accepts and retains a quoted destination expression") {
            char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.alerting'>"
                "<redirect dest=\"'tel:+12025550123'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            memset(source, 'x', sizeof(source) - 1u);
            check_not_null(program.impl);
            check_equal(ccxml_program_action_count(&program), (size_t)1);

            ccxml_program_destroy(&program);
        }

        it("requires a destination") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.alerting'>"
                "<redirect/></transition></eventprocessor></ccxml>";
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
                "<eventprocessor><transition event='connection.alerting'>"
                "<redirect dest=\"''\"/></transition>"
                "</eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("retains a nonliteral destination expression") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.alerting'>"
                "<redirect dest='route.destination'/></transition>"
                "</eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            const ccxml_program_impl *impl;

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            impl = (const ccxml_program_impl *)program.impl;
            check_not_null(impl);
            check_equal(
                impl->actions[0].destination, "route.destination");
            check_equal(
                impl->actions[0].destination_size,
                sizeof("route.destination") - 1u);
            check_true(impl->actions[0].destination_is_dynamic);
            check_true(impl->uses_string_expression);

            ccxml_program_destroy(&program);
        }

        it("decodes a dynamic destination before retaining it") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.alerting'>"
                "<redirect dest='route&#46;destination'/></transition>"
                "</eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            const ccxml_program_impl *impl;

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            impl = (const ccxml_program_impl *)program.impl;
            check_not_null(impl);
            check_equal(
                impl->actions[0].destination, "route.destination");
            check_equal(
                impl->actions[0].destination_size,
                sizeof("route.destination") - 1u);
            check_true(impl->actions[0].destination_is_dynamic);
            check_true(impl->uses_string_expression);

            ccxml_program_destroy(&program);
        }

        it("rejects an escaped destination literal") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.alerting'>"
                "<redirect dest=\"'tel:12\\\\34'\"/></transition>"
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
                "<eventprocessor><transition event='connection.alerting'>"
                "<redirect dest=\"'tel:123'\" connectionid=\"'call-7'\"/>"
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
                "<eventprocessor><transition event='connection.alerting'>"
                "<redirect dest=\"'tel:123'\"><exit/></redirect>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }
    }

    group("join") {
        it("accepts and retains two quoted resource identifiers") {
            char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<join id1=\"'call-a'\" id2=\"'call-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            memset(source, 'x', sizeof(source) - 1u);
            check_not_null(program.impl);
            check_equal(ccxml_program_action_count(&program), (size_t)1);

            ccxml_program_destroy(&program);
        }

        it("requires id1") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<join id2=\"'call-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("requires id2") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<join id1=\"'call-a'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects an empty id1 literal") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<join id1=\"''\" id2=\"'call-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects an empty id2 literal") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<join id1=\"'call-a'\" id2=\"''\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects a nonliteral resource expression") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<join id1='call_a' id2=\"'call-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects an escaped resource literal") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<join id1=\"'call-a'\" id2=\"'call\\\\b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects duplicate resource attributes") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<join id1=\"'call-a'\" id1=\"'call-c'\" "
                "id2=\"'call-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_XML_ERROR);
            check_null(program.impl);
        }

        it("rejects optional attributes outside the slice") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<join id1=\"'call-a'\" id2=\"'call-b'\" duplex=\"'half'\"/>"
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
                "<eventprocessor><transition event='conference.request'>"
                "<join id1=\"'call-a'\" id2=\"'call-b'\"><exit/></join>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("charges both identifiers to the retained-name budget") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<join id1=\"'call-a'\" id2=\"'call-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            ccxml_limits limits = ccxml_default_limits();
            limits.max_name_bytes = 32u;

            check_equal(
                ccxml_compile(
                    &program, source, strlen(source), &limits, &diagnostic),
                CCXML_LIMIT_EXCEEDED);
            check_null(program.impl);
        }
    }

    group("unjoin") {
        it("accepts and retains two quoted resource identifiers") {
            char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<unjoin id1=\"'call-a'\" id2=\"'conference-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            memset(source, 'x', sizeof(source) - 1u);
            check_not_null(program.impl);
            check_equal(ccxml_program_action_count(&program), (size_t)1);

            ccxml_program_destroy(&program);
        }

        it("requires id1") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<unjoin id2=\"'conference-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("requires id2") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<unjoin id1=\"'call-a'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects an empty id1 literal") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<unjoin id1=\"''\" id2=\"'conference-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects an empty id2 literal") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<unjoin id1=\"'call-a'\" id2=\"''\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects a nonliteral resource expression") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<unjoin id1='call_a' id2=\"'conference-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects an escaped resource literal") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<unjoin id1=\"'call-a'\" id2=\"'conference\\\\b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects duplicate resource attributes") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<unjoin id1=\"'call-a'\" id1=\"'call-c'\" "
                "id2=\"'conference-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_XML_ERROR);
            check_null(program.impl);
        }

        it("rejects optional attributes outside the slice") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<unjoin id1=\"'call-a'\" id2=\"'conference-b'\" "
                "hints=\"'fast'\"/>"
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
                "<eventprocessor><transition event='conference.request'>"
                "<unjoin id1=\"'call-a'\" id2=\"'conference-b'\">"
                "<exit/></unjoin></transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("charges both identifiers to the retained-name budget") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<unjoin id1=\"'call-a'\" id2=\"'conference-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            ccxml_limits limits = ccxml_default_limits();
            limits.max_name_bytes = 38u;

            check_equal(
                ccxml_compile(
                    &program, source, strlen(source), &limits, &diagnostic),
                CCXML_LIMIT_EXCEEDED);
            check_null(program.impl);
        }
    }

    group("createconference") {
        it("accepts a location and optional quoted conference name") {
            char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<createconference conferenceid='conference.id' "
                "confname=\"'support'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            memset(source, 'x', sizeof(source) - 1u);
            check_not_null(program.impl);
            check_equal(ccxml_program_action_count(&program), (size_t)1);

            ccxml_program_destroy(&program);
        }

        it("accepts an omitted conference name") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<createconference conferenceid='conference_id'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            ccxml_program_destroy(&program);
        }

        it("requires conferenceid") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<createconference confname=\"'support'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects an empty conferenceid") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<createconference conferenceid=''/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects invalid dotted location syntax") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<createconference conferenceid='conference..id'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects an empty conference name") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<createconference conferenceid='conference_id' "
                "confname=\"''\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects a nonliteral conference name expression") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<createconference conferenceid='conference_id' "
                "confname='conference_name'/>"
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
                "<eventprocessor><transition event='conference.request'>"
                "<createconference conferenceid='conference_id' "
                "reservedtalkers='2'/>"
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
                "<eventprocessor><transition event='conference.request'>"
                "<createconference conferenceid='conference_id'>"
                "<exit/></createconference>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("charges location and name to the retained-name budget") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<createconference conferenceid='conference.id' "
                "confname=\"'support'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            ccxml_limits limits = ccxml_default_limits();
            limits.max_name_bytes = 40u;

            check_equal(
                ccxml_compile(
                    &program, source, strlen(source), &limits, &diagnostic),
                CCXML_LIMIT_EXCEEDED);
            check_null(program.impl);
        }
    }

    group("destroyconference") {
        it("retains a dotted conference identifier location") {
            char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.release'>"
                "<destroyconference conferenceid='conference.id'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            const ccxml_program_impl *impl;

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            memset(source, 'x', sizeof(source) - 1u);
            impl = (const ccxml_program_impl *)program.impl;
            check_not_null(impl);
            check_equal(impl->actions[0].kind,
                        CCXML_ACTION_DESTROY_CONFERENCE);
            check_equal(impl->actions[0].location, "conference.id");
            check_equal(impl->actions[0].location_size, (size_t)13);
            check_equal(impl->max_transition_effects, (size_t)1);

            ccxml_program_destroy(&program);
        }

        it("retains a quoted conference identifier value") {
            char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.release'>"
                "<destroyconference conferenceid=\"'conference-42'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            const ccxml_program_impl *impl;

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            memset(source, 'x', sizeof(source) - 1u);
            impl = (const ccxml_program_impl *)program.impl;
            check_not_null(impl);
            check_equal(impl->actions[0].id1, "conference-42");
            check_equal(impl->actions[0].id1_size, (size_t)13);
            check_null(impl->actions[0].location);

            ccxml_program_destroy(&program);
        }

        it("requires conferenceid") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.release'>"
                "<destroyconference/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects an empty conference identifier literal") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.release'>"
                "<destroyconference conferenceid=\"''\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects expressions outside the bounded profile") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.release'>"
                "<destroyconference conferenceid='conference.id + suffix'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects invalid dotted location syntax") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.release'>"
                "<destroyconference conferenceid='conference..id'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects hints outside the bounded profile") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.release'>"
                "<destroyconference conferenceid='conference.id' hints='x'/>"
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
                "<eventprocessor><transition event='conference.release'>"
                "<destroyconference conferenceid='conference.id'>"
                "<exit/></destroyconference>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("charges the identifier NUL to the retained-name budget") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.release'>"
                "<destroyconference conferenceid='conference.id'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            ccxml_limits limits = ccxml_default_limits();
            limits.max_name_bytes = 32u;

            check_equal(
                ccxml_compile(
                    &program, source, strlen(source), &limits, &diagnostic),
                CCXML_LIMIT_EXCEEDED);
            check_null(program.impl);
        }
    }

    group("dialogprepare") {
        it("retains a detached source and dialog ID write location") {
            char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='dialog.prepare'>"
                "<dialogprepare dialogid='dialog.prepared' "
                "src=\"'app.vxml'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            const ccxml_program_impl *impl;

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            memset(source, 'x', sizeof(source) - 1u);
            impl = (const ccxml_program_impl *)program.impl;
            check_not_null(impl);
            check_equal(
                impl->actions[0].kind, CCXML_ACTION_DIALOG_PREPARE);
            check_equal(impl->actions[0].destination, "app.vxml");
            check_equal(impl->actions[0].destination_size, (size_t)8);
            check_equal(impl->actions[0].location, "dialog.prepared");
            check_equal(impl->actions[0].location_size, (size_t)15);
            check_equal(impl->max_transition_effects, (size_t)2);

            ccxml_program_destroy(&program);
        }

        it("requires dialogid") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='dialog.prepare'>"
                "<dialogprepare src=\"'app.vxml'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("requires src") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='dialog.prepare'>"
                "<dialogprepare dialogid='dialog.prepared'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects a nonliteral source expression") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='dialog.prepare'>"
                "<dialogprepare dialogid='dialog.prepared' src='dialog_uri'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects an empty source literal") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='dialog.prepare'>"
                "<dialogprepare dialogid='dialog.prepared' src=\"''\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects an invalid dialog ID write location") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='dialog.prepare'>"
                "<dialogprepare dialogid='dialog..prepared' "
                "src=\"'app.vxml'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects media targets") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='dialog.prepare'>"
                "<dialogprepare dialogid='dialog.prepared' "
                "src=\"'app.vxml'\" connectionid='event$.connectionid'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects options outside the detached VoiceXML profile") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='dialog.prepare'>"
                "<dialogprepare dialogid='dialog.prepared' "
                "src=\"'app.vxml'\" type=\"'application/voicexml+xml'\"/>"
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
                "<eventprocessor><transition event='dialog.prepare'>"
                "<dialogprepare dialogid='dialog.prepared' "
                "src=\"'app.vxml'\"><exit/></dialogprepare>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("charges both retained values including their NUL bytes") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='dialog.prepare'>"
                "<dialogprepare dialogid='dialog.prepared' "
                "src=\"'app.vxml'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            ccxml_limits limits = ccxml_default_limits();
            limits.max_name_bytes = 39u;

            check_equal(
                ccxml_compile(
                    &program, source, strlen(source), &limits, &diagnostic),
                CCXML_LIMIT_EXCEEDED);
            check_null(program.impl);
        }
    }

    group("dialogstart") {
        it("retains a direct source and dialog ID write location") {
            char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart dialogid='dialog.id' src=\"'app.vxml'\" "
                "connectionid='event$.connectionid'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            const ccxml_program_impl *impl;

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            memset(source, 'x', sizeof(source) - 1u);
            impl = (const ccxml_program_impl *)program.impl;
            check_not_null(impl);
            check_equal(impl->actions[0].kind, CCXML_ACTION_DIALOG_START);
            check_equal(impl->actions[0].destination, "app.vxml");
            check_equal(impl->actions[0].destination_size, (size_t)8);
            check_equal(impl->actions[0].location, "dialog.id");
            check_equal(impl->actions[0].location_size, (size_t)9);
            check_equal(impl->max_transition_effects, (size_t)2);

            ccxml_program_destroy(&program);
        }

        it("requires dialogid") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart src=\"'app.vxml'\" "
                "connectionid='event$.connectionid'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("requires src") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart dialogid='dialog.id' "
                "connectionid='event$.connectionid'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("requires connectionid") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart dialogid='dialog.id' src=\"'app.vxml'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("requires the exact current event connection expression") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart dialogid='dialog.id' src=\"'app.vxml'\" "
                "connectionid='connection.id'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects a nonliteral source expression") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart dialogid='dialog.id' src='dialog_uri' "
                "connectionid='event$.connectionid'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects an empty source literal") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart dialogid='dialog.id' src=\"''\" "
                "connectionid='event$.connectionid'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects an invalid dialog ID write location") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart dialogid='dialog..id' src=\"'app.vxml'\" "
                "connectionid='event$.connectionid'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects options outside the direct VoiceXML profile") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart dialogid='dialog.id' src=\"'app.vxml'\" "
                "connectionid='event$.connectionid' "
                "type=\"'application/voicexml+xml'\"/>"
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
                "<dialogstart dialogid='dialog.id' src=\"'app.vxml'\" "
                "connectionid='event$.connectionid'><exit/></dialogstart>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("charges both retained values including their NUL bytes") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart dialogid='dialog.id' src=\"'app.vxml'\" "
                "connectionid='event$.connectionid'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            ccxml_limits limits = ccxml_default_limits();
            limits.max_name_bytes = 39u;

            check_equal(
                ccxml_compile(
                    &program, source, strlen(source), &limits, &diagnostic),
                CCXML_LIMIT_EXCEEDED);
            check_null(program.impl);
        }
    }

    group("prepared dialogstart") {
        it("retains a prepared dialog ID read location") {
            char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart prepareddialogid='dialog.prepared' "
                "connectionid='event$.connectionid'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            const ccxml_program_impl *impl;

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            memset(source, 'x', sizeof(source) - 1u);
            impl = (const ccxml_program_impl *)program.impl;
            check_not_null(impl);
            check_equal(
                impl->actions[0].kind,
                CCXML_ACTION_PREPARED_DIALOG_START);
            check_equal(impl->actions[0].location, "dialog.prepared");
            check_equal(impl->actions[0].location_size, (size_t)15);
            check_null(impl->actions[0].destination);
            check_true(impl->uses_datamodel_read);
            check_true(impl->uses_prepared_dialog_start);
            check_equal(impl->max_transition_effects, (size_t)1);

            ccxml_program_destroy(&program);
        }

        it("requires prepareddialogid") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart connectionid='event$.connectionid'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("requires connectionid") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart prepareddialogid='dialog.prepared'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("requires a dotted prepared dialog ID location") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart prepareddialogid='dialog..prepared' "
                "connectionid='event$.connectionid'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects a literal prepared dialog ID") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart prepareddialogid=\"'dialog-42'\" "
                "connectionid='event$.connectionid'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("requires the exact current event connection expression") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart prepareddialogid='dialog.prepared' "
                "connectionid='connection.id'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects direct-start and optional attributes") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart prepareddialogid='dialog.prepared' "
                "connectionid='event$.connectionid' src=\"'app.vxml'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects dialogid and nested executable content") {
            const char *with_dialog_id =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart prepareddialogid='dialog.prepared' "
                "dialogid='dialog.new' "
                "connectionid='event$.connectionid'/>"
                "</transition></eventprocessor></ccxml>";
            const char *with_content =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart prepareddialogid='dialog.prepared' "
                "connectionid='event$.connectionid'><exit/></dialogstart>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, with_dialog_id, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
            check_equal(
                compile_source(&program, with_content, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("charges the retained location including its NUL byte") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='connection.connected'>"
                "<dialogstart prepareddialogid='dialog.prepared' "
                "connectionid='event$.connectionid'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            ccxml_limits limits = ccxml_default_limits();
            limits.max_name_bytes = 36u;

            check_equal(
                ccxml_compile(
                    &program, source, strlen(source), &limits, &diagnostic),
                CCXML_LIMIT_EXCEEDED);
            check_null(program.impl);
        }
    }

    group("dialogterminate") {
        it("retains a dotted dialog identifier location") {
            char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='dialog.stop'>"
                "<dialogterminate dialogid='dialog.id'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            const ccxml_program_impl *impl;

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            memset(source, 'x', sizeof(source) - 1u);
            impl = (const ccxml_program_impl *)program.impl;
            check_not_null(impl);
            check_equal(
                impl->actions[0].kind,
                CCXML_ACTION_DIALOG_TERMINATE);
            check_equal(impl->actions[0].location, "dialog.id");
            check_equal(impl->actions[0].location_size, (size_t)9);
            check_null(impl->actions[0].id1);
            check_true(impl->uses_datamodel_read);
            check_equal(impl->max_transition_effects, (size_t)1);

            ccxml_program_destroy(&program);
        }

        it("retains a quoted dialog identifier value") {
            char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='dialog.stop'>"
                "<dialogterminate dialogid=\"'dialog-42'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            const ccxml_program_impl *impl;

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            memset(source, 'x', sizeof(source) - 1u);
            impl = (const ccxml_program_impl *)program.impl;
            check_not_null(impl);
            check_equal(impl->actions[0].id1, "dialog-42");
            check_equal(impl->actions[0].id1_size, (size_t)9);
            check_null(impl->actions[0].location);
            check_false(impl->uses_datamodel_read);

            ccxml_program_destroy(&program);
        }

        it("requires dialogid") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='dialog.stop'>"
                "<dialogterminate/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects an empty dialog identifier literal") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='dialog.stop'>"
                "<dialogterminate dialogid=\"''\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects expressions outside the bounded profile") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='dialog.stop'>"
                "<dialogterminate dialogid='dialog.id + suffix'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects explicit immediate mode") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='dialog.stop'>"
                "<dialogterminate dialogid='dialog.id' immediate='false'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects hints outside the bounded profile") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='dialog.stop'>"
                "<dialogterminate dialogid='dialog.id' hints='x'/>"
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
                "<eventprocessor><transition event='dialog.stop'>"
                "<dialogterminate dialogid='dialog.id'>"
                "<exit/></dialogterminate>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("charges the identifier NUL to the retained-name budget") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='dialog.stop'>"
                "<dialogterminate dialogid='dialog.id'/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            ccxml_limits limits = ccxml_default_limits();
            limits.max_name_bytes = 21u;

            check_equal(
                ccxml_compile(
                    &program, source, strlen(source), &limits, &diagnostic),
                CCXML_LIMIT_EXCEEDED);
            check_null(program.impl);
        }
    }

    group("merge") {
        it("accepts and retains two quoted connection identifiers") {
            char source[] =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<merge connectionid1=\"'call-a'\" "
                "connectionid2=\"'call-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic), CCXML_OK);
            memset(source, 'x', sizeof(source) - 1u);
            check_not_null(program.impl);
            check_equal(ccxml_program_action_count(&program), (size_t)1);

            ccxml_program_destroy(&program);
        }

        it("requires connectionid1") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<merge connectionid2=\"'call-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("requires connectionid2") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<merge connectionid1=\"'call-a'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects an empty connectionid1 literal") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<merge connectionid1=\"''\" connectionid2=\"'call-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects an empty connectionid2 literal") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<merge connectionid1=\"'call-a'\" connectionid2=\"''\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_INVALID_STRUCTURE);
            check_null(program.impl);
        }

        it("rejects a nonliteral connection expression") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<merge connectionid1='call_a' "
                "connectionid2=\"'call-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects an escaped connection literal") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<merge connectionid1=\"'call-a'\" "
                "connectionid2=\"'call\\\\b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("rejects duplicate connection attributes") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<merge connectionid1=\"'call-a'\" "
                "connectionid1=\"'call-c'\" connectionid2=\"'call-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_XML_ERROR);
            check_null(program.impl);
        }

        it("rejects optional attributes outside the slice") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<merge connectionid1=\"'call-a'\" "
                "connectionid2=\"'call-b'\" hints=\"'fast'\"/>"
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
                "<eventprocessor><transition event='conference.request'>"
                "<merge connectionid1=\"'call-a'\" "
                "connectionid2=\"'call-b'\"><exit/></merge>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};

            check_equal(
                compile_source(&program, source, &diagnostic),
                CCXML_UNSUPPORTED_FEATURE);
            check_null(program.impl);
        }

        it("charges both identifiers to the retained-name budget") {
            const char *source =
                "<ccxml xmlns='http://www.w3.org/2002/09/ccxml' version='1.0'>"
                "<eventprocessor><transition event='conference.request'>"
                "<merge connectionid1=\"'call-a'\" "
                "connectionid2=\"'call-b'\"/>"
                "</transition></eventprocessor></ccxml>";
            ccxml_program program = {0};
            ccxml_diagnostic diagnostic = {0};
            ccxml_limits limits = ccxml_default_limits();
            limits.max_name_bytes = 32u;

            check_equal(
                ccxml_compile(
                    &program, source, strlen(source), &limits, &diagnostic),
                CCXML_LIMIT_EXCEEDED);
            check_null(program.impl);
        }
    }
}
