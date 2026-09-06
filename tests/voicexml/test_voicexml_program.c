#include <voicexml/voicexml.h>

#include "voicexml_internal.h"
#include "tinytest.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static vxml_status compile_text(
    const char *source, const vxml_limits *limits,
    vxml_program *program, vxml_diagnostic *diagnostic) {
    return vxml_compile(
        source, strlen(source), limits, program, diagnostic);
}

static void check_empty_failure(
    const char *source, vxml_status expected,
    const vxml_limits *limits, uint32_t expected_line) {
    vxml_program program = {(void *)(uintptr_t)1u};
    vxml_diagnostic diagnostic = {0};

    check_equal(
        compile_text(source, limits, &program, &diagnostic), expected);
    check_null(program.impl);
    check_equal(diagnostic.status, expected);
    check_true(diagnostic.message[0] != '\0');
    if (expected_line != 0u)
        check_equal(diagnostic.location.line, expected_line);
}

static void check_empty_failure_at(
    const char *source, vxml_status expected,
    salts_xml_location expected_location) {
    vxml_program program = {(void *)(uintptr_t)1u};
    vxml_diagnostic diagnostic = {0};

    check_equal(
        compile_text(source, NULL, &program, &diagnostic), expected);
    check_null(program.impl);
    check_equal(diagnostic.status, expected);
    check_equal(
        diagnostic.location.byte_offset, expected_location.byte_offset);
    check_equal(diagnostic.location.line, expected_location.line);
    check_equal(diagnostic.location.column, expected_location.column);
}

spec("VoiceXML program compiler") {
    group("accepted bounded documents") {
        it("compiles explicit exit and empty blocks into ordered rows") {
            const char *sources[] = {
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form id='main'><block><exit/></block></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.0'>"
                "<form><block/></form></vxml>"};
            const size_t expected_actions[] = {1u, 0u};
            size_t index;

            for (index = 0u; index < 2u; ++index) {
                vxml_program program = {0};
                vxml_diagnostic diagnostic = {0};
                vxml_program_impl *impl;

                check_equal(
                    compile_text(sources[index], NULL, &program, &diagnostic),
                    VXML_OK);
                check_not_null(program.impl);
                if (program.impl == NULL) continue;
                impl = (vxml_program_impl *)program.impl;
                check_equal(impl->form_count, (size_t)1u);
                check_equal(impl->block_count, (size_t)1u);
                check_equal(impl->action_count, expected_actions[index]);
                check_equal(impl->forms[0].first_block, (size_t)0u);
                check_equal(impl->forms[0].block_count, (size_t)1u);
                check_equal(impl->blocks[0].first_action, (size_t)0u);
                check_equal(
                    impl->blocks[0].action_count, expected_actions[index]);
                if (expected_actions[index] != 0u)
                    check_equal(impl->actions[0].kind, VXML_ACTION_EXIT);
                vxml_program_destroy(&program);
                check_null(program.impl);
            }
        }

        it("stores multiple forms and decoded IDs independently of source") {
            char source[] =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form id='main&#46;one'><block/></form>"
                "<form id='_second'><block/><block><exit/></block></form>"
                "</vxml>";
            vxml_program program = {0};
            vxml_diagnostic diagnostic = {0};
            vxml_program_impl *impl;

            check_equal(
                compile_text(source, NULL, &program, &diagnostic), VXML_OK);
            check_not_null(program.impl);
            if (program.impl != NULL) {
                memset(source, 'x', sizeof(source) - 1u);
                impl = (vxml_program_impl *)program.impl;
                check_equal(impl->form_count, (size_t)2u);
                check_equal(impl->block_count, (size_t)3u);
                check_equal(impl->action_count, (size_t)1u);
                check_equal(impl->storage_size, (size_t)17u);
                check_true((const char *)impl->forms >= (const char *)impl);
                check_true(
                    (const char *)(impl->forms + impl->form_count) <=
                    (const char *)impl + impl->allocation_size);
                check_true(
                    (const char *)(impl->blocks + impl->block_count) <=
                    (const char *)impl + impl->allocation_size);
                check_true(
                    (const char *)(impl->actions + impl->action_count) <=
                    (const char *)impl + impl->allocation_size);
                check_true(
                    impl->storage + impl->storage_size <=
                    (const char *)impl + impl->allocation_size);
                check_equal(impl->forms[0].id, "main.one");
                check_equal(impl->forms[0].id_size, (size_t)8u);
                check_equal(impl->forms[0].first_block, (size_t)0u);
                check_equal(impl->forms[0].block_count, (size_t)1u);
                check_equal(impl->forms[1].id, "_second");
                check_true(
                    impl->forms[1].id ==
                    impl->forms[0].id + impl->forms[0].id_size + 1u);
                check_equal(impl->forms[1].first_block, (size_t)1u);
                check_equal(impl->forms[1].block_count, (size_t)2u);
                check_equal(impl->blocks[2].first_action, (size_t)0u);
                check_equal(impl->blocks[2].action_count, (size_t)1u);
                vxml_program_destroy(&program);
            }
        }
    }

    group("document validation") {
        it("rejects missing or wrong VoiceXML namespaces") {
            const char *sources[] = {
                "<vxml version='2.1'><form><block/></form></vxml>",
                "<vxml xmlns='urn:not-vxml' version='2.1'>"
                "<form><block/></form></vxml>",
                "<notvxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block/></form></notvxml>"};
            size_t index;
            for (index = 0u; index < 3u; ++index)
                check_empty_failure(
                    sources[index], VXML_INVALID_NAMESPACE, NULL, 1u);
        }

        it("rejects missing or unsupported VoiceXML versions") {
            const char *sources[] = {
                "<vxml xmlns='http://www.w3.org/2001/vxml'>"
                "<form><block/></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='1.0'>"
                "<form><block/></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version=''>"
                "<form><block/></form></vxml>"};
            size_t index;
            for (index = 0u; index < 3u; ++index)
                check_empty_failure(
                    sources[index], VXML_INVALID_VERSION, NULL, 1u);
        }

        it("rejects duplicate IDs after XML decoding at the second ID") {
            const char *source =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>\n"
                "<form id='main.one'><block/></form>\n"
                "<form id='main&#46;one'><block/></form>\n"
                "</vxml>";
            check_empty_failure(source, VXML_DUPLICATE_ID, NULL, 3u);
        }

        it("rejects empty and invalid XML NCName form IDs") {
            const char *sources[] = {
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form id=''><block/></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form id='1bad'><block/></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form id='bad:name'><block/></form></vxml>"};
            size_t index;
            for (index = 0u; index < 3u; ++index)
                check_empty_failure(
                    sources[index], VXML_INVALID_STRUCTURE, NULL, 1u);
        }

        it("requires at least one form and one block per form") {
            const char *sources[] = {
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'/>"
                ,
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form/></vxml>"};
            size_t index;
            for (index = 0u; index < 2u; ++index)
                check_empty_failure(
                    sources[index], VXML_INVALID_STRUCTURE, NULL, 1u);
        }

        it("rejects unsupported and foreign elements including prompt") {
            const char *sources[] = {
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<prompt/><form><block/></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><prompt/><block/></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block><audio/></block></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' "
                "xmlns:x='urn:foreign'><form><block/><x:item/></form></vxml>"};
            size_t index;
            for (index = 0u; index < 4u; ++index)
                check_empty_failure(
                    sources[index], VXML_UNSUPPORTED_FEATURE, NULL, 1u);
        }

        it("rejects foreign namespaces at every nested grammar level") {
            const char *sources[] = {
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form xmlns='urn:foreign'>"
                "<block xmlns='http://www.w3.org/2001/vxml'/></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block xmlns='urn:foreign'/></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block><exit xmlns='urn:foreign'/></block></form>"
                "</vxml>"};
            size_t index;
            for (index = 0u; index < 3u; ++index)
                check_empty_failure(
                    sources[index], VXML_UNSUPPORTED_FEATURE, NULL, 1u);
        }

        it("rejects unsupported attributes on every admitted element") {
            const char *sources[] = {
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1' foo='x'>"
                "<form><block/></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form name='x'><block/></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block name='x'/></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block><exit expr='x'/></block></form></vxml>"};
            size_t index;
            for (index = 0u; index < 4u; ++index)
                check_empty_failure(
                    sources[index], VXML_UNSUPPORTED_FEATURE, NULL, 1u);
        }

        it("rejects same-local-name attributes in a foreign namespace") {
            const char *sources[] = {
                "<vxml xmlns='http://www.w3.org/2001/vxml' "
                "xmlns:x='urn:foreign' x:version='2.1'>"
                "<form><block/></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form xmlns:x='urn:foreign' x:id='other'>"
                "<block/></form></vxml>"};
            size_t index;
            for (index = 0u; index < 2u; ++index)
                check_empty_failure(
                    sources[index], VXML_UNSUPPORTED_FEATURE, NULL, 1u);
        }

        it("rejects non-whitespace text throughout the profile") {
            const char *sources[] = {
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>text"
                "<form><block/></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form>text<block/></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block>text</block></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block><exit>text</exit></block></form></vxml>"};
            size_t index;
            for (index = 0u; index < 4u; ++index)
                check_empty_failure(
                    sources[index], VXML_INVALID_STRUCTURE, NULL, 1u);
        }

        it("allows at most one empty exit child per block") {
            const char *source =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block><exit/><exit/></block></form></vxml>";
            check_empty_failure(source, VXML_INVALID_STRUCTURE, NULL, 1u);
        }

        it("rejects known profile elements used in illegal positions") {
            const char *sources[] = {
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<block/><form><block/></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><exit/><block/></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block><form><block/></form></block></form></vxml>",
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block><exit><exit/></exit></block></form></vxml>"};
            size_t index;
            for (index = 0u; index < 4u; ++index)
                check_empty_failure(
                    sources[index], VXML_INVALID_STRUCTURE, NULL, 1u);
        }

        it("preserves exact element attribute and text failure locations") {
            check_empty_failure_at(
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>\n"
                "  <form xmlns='urn:foreign'><block/></form>\n"
                "</vxml>",
                VXML_UNSUPPORTED_FEATURE,
                (salts_xml_location){59u, 2u, 3u});
            check_empty_failure_at(
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>\n"
                "  <form x:id='main' xmlns:x='urn:f'><block/></form>\n"
                "</vxml>",
                VXML_UNSUPPORTED_FEATURE,
                (salts_xml_location){65u, 2u, 9u});
            check_empty_failure_at(
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>\n"
                "  <form><block>text</block></form>\n"
                "</vxml>",
                VXML_INVALID_STRUCTURE,
                (salts_xml_location){72u, 2u, 16u});
        }
    }

    group("configured limits") {
        it("accepts exact row limits and rejects one over") {
            const char *source =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form><block/><block><exit/></block></form>"
                "<form><block><exit/></block></form></vxml>";
            vxml_limits limits = vxml_default_limits();
            vxml_program program = {0};
            vxml_diagnostic diagnostic = {0};

            limits.max_forms = 2u;
            limits.max_blocks = 3u;
            limits.max_actions = 2u;
            check_equal(
                compile_text(source, &limits, &program, &diagnostic), VXML_OK);
            vxml_program_destroy(&program);

            limits.max_forms = 1u;
            check_empty_failure(source, VXML_LIMIT_EXCEEDED, &limits, 1u);
            limits.max_forms = 2u;
            limits.max_blocks = 2u;
            check_empty_failure(source, VXML_LIMIT_EXCEEDED, &limits, 1u);
            limits.max_blocks = 3u;
            limits.max_actions = 1u;
            check_empty_failure(source, VXML_LIMIT_EXCEEDED, &limits, 1u);
        }

        it("charges decoded ID bytes plus one NUL to the name limit") {
            const char *source =
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                "<form id='a&#46;b'><block/></form></vxml>";
            vxml_limits limits = vxml_default_limits();
            vxml_program program = {0};
            vxml_diagnostic diagnostic = {0};

            limits.max_name_bytes = 4u;
            check_equal(
                compile_text(source, &limits, &program, &diagnostic), VXML_OK);
            vxml_program_destroy(&program);
            limits.max_name_bytes = 3u;
            check_empty_failure(source, VXML_LIMIT_EXCEEDED, &limits, 1u);
        }

        it("rejects every zero compiler and XML limit") {
            vxml_limits limits = vxml_default_limits();
            size_t *profile_limits[] = {
                &limits.max_forms, &limits.max_blocks,
                &limits.max_actions, &limits.max_name_bytes};
            size_t index;

            for (index = 0u; index < 4u; ++index) {
                const size_t saved = *profile_limits[index];
                *profile_limits[index] = 0u;
                check_empty_failure(
                    "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                    "<form><block/></form></vxml>",
                    VXML_INVALID_ARGUMENT, &limits, 0u);
                *profile_limits[index] = saved;
            }
            {
                size_t *xml_limits[] = {
                    &limits.xml.max_input_bytes, &limits.xml.max_nodes,
                    &limits.xml.max_attributes, &limits.xml.max_depth,
                    &limits.xml.max_retained_string_bytes};
                for (index = 0u; index < 5u; ++index) {
                    const size_t saved = *xml_limits[index];
                    *xml_limits[index] = 0u;
                    check_empty_failure(
                        "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
                        "<form><block/></form></vxml>",
                        VXML_INVALID_ARGUMENT, &limits, 0u);
                    *xml_limits[index] = saved;
                }
            }
        }

        it("maps malformed XML diagnostics") {
            check_empty_failure(
                "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>\n"
                "<form><block></form></vxml>",
                VXML_XML_ERROR, NULL, 2u);
        }
    }
}
