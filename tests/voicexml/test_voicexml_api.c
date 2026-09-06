#include <voicexml/voicexml.h>

#include "tinytest.h"

spec("VoiceXML public API") {
    it("returns the documented default limits") {
        const vxml_limits limits = vxml_default_limits();
        const salts_xml_limits xml_limits = salts_xml_default_limits();

        check_equal(limits.xml.max_input_bytes, xml_limits.max_input_bytes);
        check_equal(limits.xml.max_nodes, xml_limits.max_nodes);
        check_equal(limits.xml.max_attributes, xml_limits.max_attributes);
        check_equal(limits.xml.max_depth, xml_limits.max_depth);
        check_equal(limits.xml.max_retained_string_bytes,
                    xml_limits.max_retained_string_bytes);
        check_equal(limits.max_forms, (size_t)64u);
        check_equal(limits.max_blocks, (size_t)1024u);
        check_equal(limits.max_actions, (size_t)4096u);
        check_equal(limits.max_name_bytes, (size_t)(256u * 1024u));
    }

    it("rejects null compile arguments") {
        static const char source[] = "<vxml/>";
        vxml_program program;
        vxml_diagnostic diagnostic = {0};

        check_equal(vxml_compile(NULL, 0u, NULL, &program, &diagnostic),
                    VXML_INVALID_ARGUMENT);
        check_null(program.impl);
        check_equal(diagnostic.status, VXML_INVALID_ARGUMENT);
        diagnostic = (vxml_diagnostic){0};
        check_equal(vxml_compile(source, sizeof(source) - 1u, NULL, NULL,
                                 &diagnostic),
                    VXML_INVALID_ARGUMENT);
        check_equal(diagnostic.status, VXML_INVALID_ARGUMENT);
    }

    it("initializes an output handle before reporting a compile failure") {
        static const char source[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<prompt/></vxml>";
        vxml_program program;
        vxml_diagnostic diagnostic = {0};

        check_equal(vxml_compile(source, sizeof(source) - 1u, NULL, &program,
                                 &diagnostic),
                    VXML_UNSUPPORTED_FEATURE);
        check_null(program.impl);
        check_equal(diagnostic.status, VXML_UNSUPPORTED_FEATURE);
    }

    it("safely destroys a zero program") {
        vxml_program program = {0};

        vxml_program_destroy(&program);
        check_null(program.impl);
    }
}
