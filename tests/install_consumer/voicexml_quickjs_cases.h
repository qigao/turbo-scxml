#ifndef TURBOSCXML_INSTALL_CONSUMER_VOICEXML_QUICKJS_CASES_H
#define TURBOSCXML_INSTALL_CONSUMER_VOICEXML_QUICKJS_CASES_H

#include <voicexml/quickjs.h>
#include <cmeta/data_reflect.h>
#include <stdio.h>
#include <string.h>

typedef struct installed_voice_state { int value; } installed_voice_state;
cmeta_reflect_value(installed_voice_state, "turboscxml.install.voicexml.quickjs.state",
    cmeta_field(int, value)
);

typedef struct installed_voice_scripts {
    size_t opened, closed, unexpected_documents;
    bool live, bad_close, fail_second;
} installed_voice_scripts;

static bool installed_voice_matches(const char *data, size_t size, const char *expected) {
    return data != NULL && size == strlen(expected) && memcmp(data, expected, size) == 0;
}

/* The resolver needs a document provider, but script execution must never fetch
 * a VoiceXML document. Count unexpected access rather than hiding it. */
static vxml_dialog_manager_status installed_voice_document_open(void *user,
    const char *uri, size_t uri_size, const char *media, size_t media_size,
    size_t max_bytes, vxml_dialog_document *out) {
    installed_voice_scripts *probe = (installed_voice_scripts *)user;
    (void)uri; (void)uri_size; (void)media; (void)media_size; (void)max_bytes; (void)out;
    ++probe->unexpected_documents;
    return VXML_DIALOG_MANAGER_DOCUMENT_ERROR;
}

static void installed_voice_document_close(void *user, vxml_dialog_document *document) {
    (void)document;
    ++((installed_voice_scripts *)user)->unexpected_documents;
}

/* One synchronous lease of immutable fixture text. Callback URI/charset views
 * are checked in place; the provider does not retain them. */
static vxml_script_resource_status installed_voice_script_open(void *user,
    const char *uri, size_t uri_size, const char *charset, size_t charset_size,
    size_t max_bytes, vxml_script_source *out) {
    static const char first[] = "globalThis.previousAction = true; value += 6;";
    static const char second[] =
        "if (typeof previousAction !== 'undefined') throw new Error('stale context');"
        "value += 2;";
    static const char failing[] = "value = 77; throw new Error('rollback');";
    installed_voice_scripts *probe = (installed_voice_scripts *)user;
    const char *expected_uri = probe->opened == 0u
        ? "https://voice.example/app/scripts/first.js"
        : "https://voice.example/app/scripts/7.js";
    const char *body = probe->opened == 0u ? first : probe->fail_second ? failing : second;
    const size_t size = strlen(body);
    if (probe->live || probe->opened >= 2u ||
        !installed_voice_matches(uri, uri_size, expected_uri) ||
        !installed_voice_matches(charset, charset_size, "UTF-8"))
        return VXML_SCRIPT_RESOURCE_INVALID_ARGUMENT;
    if (size > max_bytes) return VXML_SCRIPT_RESOURCE_LIMIT_EXCEEDED;
    out->data = body;
    out->size = size;
    out->lease = probe;
    probe->live = true;
    ++probe->opened;
    return VXML_SCRIPT_RESOURCE_OK;
}

static void installed_voice_script_close(void *user, vxml_script_source *source) {
    installed_voice_scripts *probe = (installed_voice_scripts *)user;
    if (!probe->live || source->lease != probe) probe->bad_close = true;
    ++probe->closed;
    probe->live = false;
    source->data = NULL;
    source->size = 0u;
    source->lease = NULL;
}

static bool installed_voice_state_is(const vxml_session *session, int expected) {
    const void *state = NULL;
    return vxml_quickjs_session_state(session, &state) == VXML_OK && state != NULL &&
        ((const installed_voice_state *)state)->value == expected;
}

/* Run identically through the installed C11 and C++17 API. Each Session owns
 * its state snapshot; destroy it before the Program and borrowed resolver. */
static int installed_voice_quickjs_case(bool fail_second) {
    static const char document[] =
        "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'><form>"
        "<block><script src='scripts/first.js'/></block>"
        "<block><script srcexpr=\"'scripts/' + value + '.js'\"/></block>"
        "<block><exit/></block></form></vxml>";
    static const char base_uri[] = "https://voice.example/app/current.vxml";
    installed_voice_state initial = {1};
    installed_voice_scripts probe = {0};
    vxml_program program = {0};
    vxml_session session = {0};
    vxml_document_store store = {0};
    vxml_document_store_config_v1 store_config = {0};
    vxml_dialog_document_adapter_v1 documents = {0};
    vxml_script_resource_adapter_v1 scripts = {0};
    vxml_external_script_target_v1 target = {0};
    vxml_quickjs_compile_options_v1 compile = vxml_quickjs_default_compile_options();
    vxml_quickjs_session_options_v1 options = vxml_quickjs_default_session_options();
    vxml_quickjs_script_execution_v1 execution = VXML_QUICKJS_SCRIPT_EXECUTION_V1_INIT;
    const char *event = NULL;
    size_t event_size = 0u;
    int result = 10;

    probe.fail_second = fail_second;
    documents.abi_version = VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1;
    documents.struct_size = sizeof(documents);
    documents.open = installed_voice_document_open;
    documents.close = installed_voice_document_close;
    store_config.abi_version = VXML_DOCUMENT_STORE_CONFIG_ABI_V1;
    store_config.struct_size = sizeof(store_config);
    store_config.application_uri = base_uri;
    store_config.application_uri_size = sizeof(base_uri) - 1u;
    store_config.capacity = 1u;
    store_config.max_uri_bytes = 255u;
    store_config.max_document_bytes = 1024u;
    store_config.max_cache_bytes = 2048u;
    store_config.voice_limits = vxml_default_limits();
    store_config.documents = &documents;
    store_config.document_user = &probe;
    if (vxml_document_store_init(&store, &store_config) != VXML_DOCUMENT_STORE_OK)
        goto cleanup;

    compile.root = cmeta_reflected_data(installed_voice_state);
    result = 11;
    if (vxml_compile_quickjs_script_profile(document, sizeof(document) - 1u,
            NULL, &compile, &program, NULL) != VXML_OK) goto cleanup;
    options.initial_state = &initial;
    result = 12;
    if (vxml_session_init_quickjs(&session, &program, &options) != VXML_OK) goto cleanup;
    initial.value = 100; /* Init copied application storage into the Session. */
    result = 13;
    if (vxml_session_start(&session) != VXML_OK ||
        vxml_session_get_state(&session) != VXML_SESSION_SCRIPTING ||
        !installed_voice_state_is(&session, 1) ||
        vxml_session_script(&session, &target) != VXML_OK ||
        !installed_voice_matches(target.src, target.src_size, "scripts/first.js"))
        goto cleanup;

    scripts.abi_version = VXML_SCRIPT_RESOURCE_ADAPTER_ABI_V1;
    scripts.struct_size = sizeof(scripts);
    scripts.open = installed_voice_script_open;
    scripts.close = installed_voice_script_close;
    execution.resolver = &store;
    execution.script_resources = &scripts;
    execution.script_resource_user = &probe;
    execution.base_document_uri = base_uri;
    execution.base_document_uri_size = sizeof(base_uri) - 1u;
    result = 14;
    if (vxml_quickjs_session_execute_script(&session, &execution) != VXML_OK ||
        probe.opened != 1u || probe.closed != 1u || probe.live || probe.bad_close ||
        !installed_voice_state_is(&session, 7) ||
        vxml_session_get_state(&session) != VXML_SESSION_SCRIPTING ||
        vxml_session_script(&session, &target) != VXML_OK ||
        !installed_voice_matches(target.src, target.src_size, "scripts/7.js")) goto cleanup;

    result = 15;
    if (vxml_quickjs_session_execute_script(&session, &execution) !=
            (fail_second ? VXML_SEMANTIC_ERROR : VXML_OK) ||
        probe.opened != 2u || probe.closed != 2u || probe.live || probe.bad_close ||
        !installed_voice_state_is(&session, fail_second ? 7 : 9) ||
        vxml_session_get_state(&session) !=
            (fail_second ? VXML_SESSION_FAILED : VXML_SESSION_EXITED) ||
        initial.value != 100) goto cleanup;
    result = 16;
    if (fail_second &&
        (vxml_quickjs_session_last_event(&session, &event, &event_size) != VXML_OK ||
         !installed_voice_matches(event, event_size, "error.semantic"))) goto cleanup;
    result = 17;
    if (vxml_quickjs_session_execute_script(&session, &execution) != VXML_INVALID_STATE ||
        probe.opened != 2u || probe.closed != 2u) goto cleanup;
    result = 0;

cleanup:
    vxml_session_destroy(&session);
    vxml_program_destroy(&program);
    if (vxml_document_store_destroy(&store) != VXML_DOCUMENT_STORE_OK && result == 0)
        result = 18;
    if (result == 0 && (probe.live || probe.bad_close || probe.opened != probe.closed ||
        probe.unexpected_documents != 0u)) result = 19;
    if (result != 0)
        fprintf(stderr, "VoiceXML QuickJS %s: check %d failed (opened=%zu, closed=%zu)\n",
            fail_second ? "rollback" : "commit", result, probe.opened, probe.closed);
    return result;
}

static int installed_voice_quickjs_cases(void) {
    int result = installed_voice_quickjs_case(false);
    if (result != 0) return result;
    result = installed_voice_quickjs_case(true);
    return result == 0 ? 0 : result + 20;
}

#endif
