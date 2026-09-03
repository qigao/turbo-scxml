#include "scxml_ast.h"

#include "tinytest.h"

#include <string.h>

static bool ast_view_is(scxml_ast_string_view view, const char *expected) {
    const size_t expected_size = strlen(expected);
    return view.data != NULL && view.size == expected_size &&
        memcmp(view.data, expected, expected_size) == 0;
}

suite("SCXML typed immutable AST") {
    it("owns typed nodes attributes and text after XML destruction") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' "
            "version='1.0' initial='ready'>"
            "<state id='ready'><onentry><log label='boot'>hello</log>"
            "</onentry><transition event='go' target='done'/></state>"
            "<final id='done'/></scxml>";
        salts_xml_document document = {0};
        salts_xml_diagnostic xml_diagnostic = {0};
        scxml_ast ast = {0};
        scxml_diagnostic diagnostic = {0};
        const scxml_ast_node *root;
        const scxml_ast_node *state;
        const scxml_ast_node *onentry;
        const scxml_ast_node *log;
        const scxml_ast_node *transition;
        const scxml_ast_node *final_state;
        const scxml_ast_attribute *attribute;
        check_equal(salts_xml_parse(
                        &document, source, sizeof(source) - 1u, NULL,
                        &xml_diagnostic),
                    SALTS_XML_OK);
        check_equal(scxml_ast_build(
                        &ast, salts_xml_document_root(&document), NULL,
                        &diagnostic),
                    SCXML_OK);
        salts_xml_document_destroy(&document);

        check_equal(scxml_ast_node_count(&ast), (size_t)7u);
        check_equal(scxml_ast_root(&ast), (scxml_ast_node_id)0u);
        root = scxml_ast_node_at(&ast, scxml_ast_root(&ast));
        check_not_null(root);
        check_equal(root->kind, SCXML_ELEMENT_SCXML);
        check_equal(root->parent, SCXML_AST_NODE_NONE);
        check_equal(root->first_child, (scxml_ast_node_id)1u);
        check_true(root->location.line > 0u);
        attribute = scxml_ast_node_find_attribute(
            &ast, root, SCXML_ATTRIBUTE_VERSION);
        check_not_null(attribute);
        check_true(ast_view_is(attribute->value, "1.0"));

        state = scxml_ast_node_at(&ast, root->first_child);
        check_not_null(state);
        check_equal(state->kind, SCXML_ELEMENT_STATE);
        check_equal(state->parent, (scxml_ast_node_id)0u);
        check_equal(state->next_sibling, (scxml_ast_node_id)6u);
        attribute = scxml_ast_node_find_attribute(
            &ast, state, SCXML_ATTRIBUTE_ID);
        check_not_null(attribute);
        check_true(ast_view_is(attribute->value, "ready"));

        onentry = scxml_ast_node_at(&ast, state->first_child);
        check_not_null(onentry);
        check_equal(onentry->kind, SCXML_ELEMENT_ONENTRY);
        transition = scxml_ast_node_at(&ast, onentry->next_sibling);
        check_not_null(transition);
        check_equal(transition->kind, SCXML_ELEMENT_TRANSITION);
        attribute = scxml_ast_node_find_attribute(
            &ast, transition, SCXML_ATTRIBUTE_EVENT);
        check_not_null(attribute);
        check_true(ast_view_is(attribute->value, "go"));

        log = scxml_ast_node_at(&ast, onentry->first_child);
        check_not_null(log);
        check_equal(log->kind, SCXML_ELEMENT_LOG);
        check_true(ast_view_is(log->text, "hello"));
        final_state = scxml_ast_node_at(&ast, state->next_sibling);
        check_not_null(final_state);
        check_equal(final_state->kind, SCXML_ELEMENT_FINAL);
        check_equal(final_state->next_sibling, SCXML_AST_NODE_NONE);
        scxml_ast_destroy(&ast);
    }

    it("fails atomically when the node limit is exceeded") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'/><state id='b'/></scxml>";
        salts_xml_document document = {0};
        salts_xml_diagnostic xml_diagnostic = {0};
        scxml_ast ast = {0};
        scxml_diagnostic diagnostic = {0};
        scxml_ast_limits limits = scxml_ast_default_limits();
        limits.max_nodes = 2u;
        check_equal(salts_xml_parse(
                        &document, source, sizeof(source) - 1u, NULL,
                        &xml_diagnostic),
                    SALTS_XML_OK);
        check_equal(scxml_ast_build(
                        &ast, salts_xml_document_root(&document), &limits,
                        &diagnostic),
                    SCXML_LIMIT_EXCEEDED);
        check_null(ast.impl);
        check_equal(diagnostic.status, SCXML_LIMIT_EXCEEDED);
        salts_xml_document_destroy(&document);
    }

    it("rejects a root outside the SCXML namespace") {
        static const char source[] =
            "<scxml xmlns='urn:not-scxml' version='1.0'/>";
        salts_xml_document document = {0};
        salts_xml_diagnostic xml_diagnostic = {0};
        scxml_ast ast = {0};
        scxml_diagnostic diagnostic = {0};
        check_equal(salts_xml_parse(
                        &document, source, sizeof(source) - 1u, NULL,
                        &xml_diagnostic),
                    SALTS_XML_OK);
        check_equal(scxml_ast_build(
                        &ast, salts_xml_document_root(&document), NULL,
                        &diagnostic),
                    SCXML_INVALID_NAMESPACE);
        check_null(ast.impl);
        check_equal(diagnostic.status, SCXML_INVALID_NAMESPACE);
        salts_xml_document_destroy(&document);
    }

    it("retains non-element children and serialized inline content") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='ready'><!--note--><onentry><log>hello</log>"
            "</onentry></state><content><p xmlns='urn:payload'>x&amp;y</p>"
            "</content></scxml>";
        salts_xml_document document = {0};
        salts_xml_diagnostic xml_diagnostic = {0};
        scxml_ast ast = {0};
        scxml_diagnostic diagnostic = {0};
        const scxml_ast_node *root;
        const scxml_ast_node *state;
        const scxml_ast_node *comment;
        const scxml_ast_node *onentry;
        const scxml_ast_node *log;
        const scxml_ast_node *text_node;
        const scxml_ast_node *content;
        check_equal(salts_xml_parse(
                        &document, source, sizeof(source) - 1u, NULL,
                        &xml_diagnostic),
                    SALTS_XML_OK);
        check_equal(scxml_ast_build(
                        &ast, salts_xml_document_root(&document), NULL,
                        &diagnostic),
                    SCXML_OK);
        salts_xml_document_destroy(&document);

        root = scxml_ast_node_at(&ast, scxml_ast_root(&ast));
        state = scxml_ast_node_at(&ast, root->first_child);
        comment = scxml_ast_node_at(&ast, state->first_child);
        onentry = scxml_ast_node_at(&ast, comment->next_sibling);
        log = scxml_ast_node_at(&ast, onentry->first_child);
        text_node = scxml_ast_node_at(&ast, log->first_child);
        content = scxml_ast_node_at(&ast, state->next_sibling);
        check_equal(comment->xml_kind, SALTS_XML_COMMENT);
        check_true(ast_view_is(comment->value, "note"));
        check_equal(text_node->xml_kind, SALTS_XML_TEXT);
        check_true(ast_view_is(text_node->value, "hello"));
        check_true(scxml_ast_node_child_at(&ast, log, 0u) == text_node);
        check_equal(content->kind, SCXML_ELEMENT_CONTENT);
        check_true(content->serialized_children.size != 0u);
        check_not_null(memchr(content->serialized_children.data, '&',
                              content->serialized_children.size));
        scxml_ast_destroy(&ast);
    }
}
