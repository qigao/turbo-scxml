#include <voicexml/document_store.h>
#include <tinytest.h>

#include <string.h>

typedef struct document_probe {
    size_t open_calls;
    size_t policy_open_calls;
    size_t close_calls;
    size_t sequence;
    size_t open_sequence;
    bool observed_has_timeout;
    uint64_t observed_timeout_us;
    vxml_dialog_manager_status open_status;
    const char *body;
    size_t body_size;
    char last_uri[256];
    size_t last_uri_size;
} document_probe;

static const char valid_document[] =
    "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
    "<form id='main'><block><exit/></block></form></vxml>";

static vxml_dialog_manager_status probe_open(
    void *user,
    const char *source, size_t source_size,
    const char *media_type, size_t media_type_size,
    size_t max_bytes,
    vxml_dialog_document *out_document) {
    document_probe *probe = (document_probe *)user;
    static const char expected_media[] = "application/voicexml+xml";
    if (probe == NULL || out_document == NULL ||
        source == NULL || source_size == 0u ||
        source_size >= sizeof(probe->last_uri) ||
        media_type == NULL ||
        media_type_size != sizeof(expected_media) - 1u ||
        memcmp(media_type, expected_media, media_type_size) != 0)
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;
    ++probe->open_calls;
    probe->open_sequence = ++probe->sequence;
    memcpy(probe->last_uri, source, source_size);
    probe->last_uri[source_size] = '\0';
    probe->last_uri_size = source_size;
    if (probe->open_status != VXML_DIALOG_MANAGER_OK)
        return probe->open_status;
    if (probe->body_size > max_bytes)
        return VXML_DIALOG_MANAGER_DOCUMENT_ERROR;
    *out_document = (vxml_dialog_document){
        .data = probe->body,
        .size = probe->body_size,
        .lease = probe};
    return VXML_DIALOG_MANAGER_OK;
}

static vxml_dialog_manager_status probe_open_with_policy(
    void *user,
    const char *source, size_t source_size,
    const char *media_type, size_t media_type_size,
    size_t max_bytes,
    const vxml_document_fetch_policy_v1 *policy,
    vxml_dialog_document *out_document) {
    document_probe *probe = (document_probe *)user;
    if (probe == NULL || policy == NULL ||
        policy->abi_version != VXML_DOCUMENT_FETCH_POLICY_ABI_V1 ||
        policy->struct_size <
            offsetof(vxml_document_fetch_policy_v1, timeout_us) +
            sizeof(policy->timeout_us))
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;
    ++probe->policy_open_calls;
    probe->observed_has_timeout = policy->has_timeout;
    probe->observed_timeout_us = policy->timeout_us;
    return probe_open(
        user, source, source_size, media_type, media_type_size,
        max_bytes, out_document);
}

static void probe_close(
    void *user, vxml_dialog_document *document) {
    document_probe *probe = (document_probe *)user;
    if (probe != NULL && document != NULL &&
        document->lease == probe)
        ++probe->close_calls;
    if (document != NULL)
        memset(document, 0, sizeof(*document));
}

static const vxml_dialog_document_adapter_v1 document_adapter = {
    .abi_version = VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1,
    .struct_size = sizeof(vxml_dialog_document_adapter_v1),
    .open = probe_open,
    .close = probe_close,
    .open_with_policy = probe_open_with_policy};

static const vxml_dialog_document_adapter_v1 legacy_document_adapter = {
    .abi_version = VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1,
    .struct_size =
        offsetof(vxml_dialog_document_adapter_v1, close) +
        sizeof(((vxml_dialog_document_adapter_v1 *)0)->close),
    .open = probe_open,
    .close = probe_close};

typedef struct fetch_audio_probe {
    document_probe *document;
    vxml_fetch_audio_begin_result result;
    size_t begin_calls;
    size_t finish_calls;
    size_t begin_sequence;
    size_t finish_sequence;
    char uri[256];
    size_t uri_size;
    bool has_delay;
    uint64_t delay_us;
    bool has_minimum;
    uint64_t minimum_us;
} fetch_audio_probe;

static void fetch_audio_finish(void *user) {
    fetch_audio_probe *probe = (fetch_audio_probe *)user;
    if (probe == NULL) return;
    ++probe->finish_calls;
    if (probe->document != NULL)
        probe->finish_sequence = ++probe->document->sequence;
}

static vxml_fetch_audio_begin_result fetch_audio_begin(
    void *user,
    const vxml_fetch_audio_request_v1 *request,
    vxml_fetch_audio_ticket_v1 *out_ticket) {
    fetch_audio_probe *probe = (fetch_audio_probe *)user;
    if (out_ticket != NULL)
        *out_ticket = (vxml_fetch_audio_ticket_v1){0};
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        request->abi_version != VXML_FETCH_AUDIO_REQUEST_ABI_V1 ||
        request->struct_size < sizeof(*request) ||
        request->uri == NULL || request->uri_size == 0u ||
        request->uri_size >= sizeof(probe->uri))
        return (vxml_fetch_audio_begin_result)99;
    ++probe->begin_calls;
    if (probe->document != NULL)
        probe->begin_sequence = ++probe->document->sequence;
    memcpy(probe->uri, request->uri, request->uri_size);
    probe->uri[request->uri_size] = '\0';
    probe->uri_size = request->uri_size;
    probe->has_delay = request->has_delay;
    probe->delay_us = request->delay_us;
    probe->has_minimum = request->has_minimum;
    probe->minimum_us = request->minimum_us;
    if (probe->result == VXML_FETCH_AUDIO_STARTED)
        *out_ticket = (vxml_fetch_audio_ticket_v1){
            .finish = fetch_audio_finish,
            .user = probe};
    return probe->result;
}

static const vxml_fetch_audio_adapter_v1 fetch_audio_adapter = {
    .abi_version = VXML_FETCH_AUDIO_ADAPTER_ABI_V1,
    .struct_size = sizeof(vxml_fetch_audio_adapter_v1),
    .begin = fetch_audio_begin};

static vxml_document_store_config_v1 store_config(
    document_probe *probe,
    size_t capacity,
    size_t max_cache_bytes) {
    return (vxml_document_store_config_v1){
        .abi_version = VXML_DOCUMENT_STORE_CONFIG_ABI_V1,
        .struct_size = sizeof(vxml_document_store_config_v1),
        .application_uri = "https://voice.example/app/root.vxml",
        .application_uri_size =
            sizeof("https://voice.example/app/root.vxml") - 1u,
        .capacity = capacity,
        .max_uri_bytes = 255u,
        .max_document_bytes = 1024u,
        .max_cache_bytes = max_cache_bytes,
        .voice_limits = vxml_default_limits(),
        .documents = &document_adapter,
        .document_user = probe};
}

static void init_store(
    vxml_document_store *store,
    document_probe *probe,
    size_t capacity,
    size_t max_cache_bytes) {
    const vxml_document_store_config_v1 config =
        store_config(probe, capacity, max_cache_bytes);
    check_equal(
        vxml_document_store_init(store, &config),
        VXML_DOCUMENT_STORE_OK);
}

static vxml_document_store_status resolve_uri(
    const vxml_document_store *store,
    const char *base,
    const char *reference,
    char document_uri[256],
    char fragment[128],
    vxml_resolved_uri_v1 *out) {
    *out = (vxml_resolved_uri_v1){
        .abi_version = 1u,
        .struct_size = sizeof(vxml_resolved_uri_v1),
        .document_uri = document_uri,
        .document_uri_capacity = 256u,
        .fragment = fragment,
        .fragment_capacity = 128u};
    return vxml_document_store_resolve(
        store,
        base, base != NULL ? strlen(base) : 0u,
        reference, reference != NULL ? strlen(reference) : 0u,
        out);
}

spec("VoiceXML bounded document store") {
    it("resolves relative paths, queries, network paths and fragments separately") {
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_OK,
            .body = valid_document,
            .body_size = sizeof(valid_document) - 1u};
        vxml_document_store store = {0};
        char uri[256] = {0};
        char fragment[128] = {0};
        vxml_resolved_uri_v1 resolved;

        init_store(&store, &probe, 2u, 2048u);

        check_equal(
            resolve_uri(
                &store,
                "https://voice.example/app/dialogs/menu.vxml?old=1",
                "../next/./step.vxml?x=1#confirm",
                uri, fragment, &resolved),
            VXML_DOCUMENT_STORE_OK);
        check_equal(uri, "https://voice.example/app/next/step.vxml?x=1");
        check_equal(fragment, "confirm");

        check_equal(
            resolve_uri(
                &store,
                "https://voice.example/app/dialogs/menu.vxml",
                "/shared/main.vxml#top",
                uri, fragment, &resolved),
            VXML_DOCUMENT_STORE_OK);
        check_equal(uri, "https://voice.example/shared/main.vxml");
        check_equal(fragment, "top");

        check_equal(
            resolve_uri(
                &store,
                "https://voice.example/app/dialogs/menu.vxml",
                "//cdn.example/voice.vxml",
                uri, fragment, &resolved),
            VXML_DOCUMENT_STORE_OK);
        check_equal(uri, "https://cdn.example/voice.vxml");
        check_equal(resolved.fragment_size, (size_t)0u);

        check_equal(
            resolve_uri(
                &store,
                "https://voice.example/app/dialogs/menu.vxml?old=1",
                "?lang=ja#section",
                uri, fragment, &resolved),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            uri,
            "https://voice.example/app/dialogs/menu.vxml?lang=ja");
        check_equal(fragment, "section");

        check_equal(
            resolve_uri(
                &store,
                "https://voice.example/app/dialogs/menu.vxml?old=1",
                "#local",
                uri, fragment, &resolved),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            uri,
            "https://voice.example/app/dialogs/menu.vxml?old=1");
        check_equal(fragment, "local");

        check_equal(
            resolve_uri(
                &store,
                "https://voice.example",
                "root.vxml",
                uri, fragment, &resolved),
            VXML_DOCUMENT_STORE_OK);
        check_equal(uri, "https://voice.example/root.vxml");

        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("shares one cached program across fragment-only navigation") {
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_OK,
            .body = valid_document,
            .body_size = sizeof(valid_document) - 1u};
        vxml_document_store store = {0};
        char uri[256] = {0};
        char fragment[128] = {0};
        vxml_resolved_uri_v1 resolved;
        vxml_document_ref first = {0};
        vxml_document_ref second = {0};
        vxml_document_view first_view = {0};
        vxml_document_view second_view = {0};
        vxml_document_store_stats stats = {0};
        const vxml_program *first_program;

        init_store(&store, &probe, 2u, 2048u);
        check_equal(
            resolve_uri(
                &store, NULL, "dialogs/main.vxml#a",
                uri, fragment, &resolved),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_acquire(
                &store, uri, resolved.document_uri_size,
                &first, NULL),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_view(&store, first, &first_view),
            VXML_DOCUMENT_STORE_OK);
        first_program = first_view.program;
        check_equal(probe.open_calls, (size_t)1u);
        check_equal(probe.close_calls, (size_t)1u);
        check_equal(
            vxml_document_store_release(&store, &first),
            VXML_DOCUMENT_STORE_OK);

        check_equal(
            resolve_uri(
                &store, NULL, "dialogs/main.vxml#b",
                uri, fragment, &resolved),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_acquire(
                &store, uri, resolved.document_uri_size,
                &second, NULL),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_view(&store, second, &second_view),
            VXML_DOCUMENT_STORE_OK);
        check_true(second_view.program == first_program);
        check_equal(probe.open_calls, (size_t)1u);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.hits, UINT64_C(1));
        check_equal(stats.misses, UINT64_C(1));
        check_equal(stats.entries, (size_t)1u);

        check_equal(
            vxml_document_store_release(&store, &second),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("passes exact timed policy on cache miss including explicit zero") {
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_OK,
            .body = valid_document,
            .body_size = sizeof(valid_document) - 1u};
        vxml_document_store store = {0};
        vxml_document_ref ref = {0};
        vxml_document_fetch_policy_v1 policy =
            VXML_DOCUMENT_FETCH_POLICY_V1_INIT;

        init_store(&store, &probe, 2u, 4096u);
        policy.has_timeout = true;
        policy.timeout_us = UINT64_C(1500000);
        check_equal(
            vxml_document_store_acquire_with_policy(
                &store, "https://voice.example/timed.vxml",
                sizeof("https://voice.example/timed.vxml") - 1u,
                &policy, &ref, NULL),
            VXML_DOCUMENT_STORE_OK);
        check_equal(probe.policy_open_calls, (size_t)1u);
        check_true(probe.observed_has_timeout);
        check_equal(probe.observed_timeout_us, UINT64_C(1500000));
        check_equal(vxml_document_store_release(&store, &ref),
                    VXML_DOCUMENT_STORE_OK);

        policy.timeout_us = UINT64_C(0);
        check_equal(
            vxml_document_store_acquire_with_policy(
                &store, "https://voice.example/zero.vxml",
                sizeof("https://voice.example/zero.vxml") - 1u,
                &policy, &ref, NULL),
            VXML_DOCUMENT_STORE_OK);
        check_equal(probe.policy_open_calls, (size_t)2u);
        check_true(probe.observed_has_timeout);
        check_equal(probe.observed_timeout_us, UINT64_C(0));
        check_equal(vxml_document_store_release(&store, &ref),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("serves a timed cache hit without requiring the policy adapter tail") {
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_OK,
            .body = valid_document,
            .body_size = sizeof(valid_document) - 1u};
        vxml_document_store_config_v1 config =
            store_config(&probe, 1u, 2048u);
        vxml_document_store store = {0};
        vxml_document_ref ref = {0};
        vxml_document_fetch_policy_v1 policy =
            VXML_DOCUMENT_FETCH_POLICY_V1_INIT;
        static const char uri[] = "https://voice.example/cached.vxml";

        config.documents = &legacy_document_adapter;
        check_equal(vxml_document_store_init(&store, &config),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(vxml_document_store_acquire(
                        &store, uri, sizeof(uri) - 1u, &ref, NULL),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(vxml_document_store_release(&store, &ref),
                    VXML_DOCUMENT_STORE_OK);
        policy.has_timeout = true;
        policy.timeout_us = UINT64_C(7);
        check_equal(vxml_document_store_acquire_with_policy(
                        &store, uri, sizeof(uri) - 1u,
                        &policy, &ref, NULL),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(probe.open_calls, (size_t)1u);
        check_equal(probe.policy_open_calls, (size_t)0u);
        check_equal(vxml_document_store_release(&store, &ref),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("fails a timed cache miss before provider publication when tail is absent") {
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_OK,
            .body = valid_document,
            .body_size = sizeof(valid_document) - 1u};
        vxml_document_store_config_v1 config =
            store_config(&probe, 1u, 2048u);
        vxml_document_store store = {0};
        vxml_document_ref ref = {0};
        vxml_document_store_error error = {0};
        vxml_document_store_stats stats = {0};
        vxml_document_fetch_policy_v1 policy =
            VXML_DOCUMENT_FETCH_POLICY_V1_INIT;

        config.documents = &legacy_document_adapter;
        check_equal(vxml_document_store_init(&store, &config),
                    VXML_DOCUMENT_STORE_OK);
        policy.has_timeout = true;
        policy.timeout_us = UINT64_C(1000);
        check_equal(vxml_document_store_acquire_with_policy(
                        &store, "https://voice.example/miss.vxml",
                        sizeof("https://voice.example/miss.vxml") - 1u,
                        &policy, &ref, &error),
                    VXML_DOCUMENT_STORE_RESOURCE_ERROR);
        check_equal(error.resource_status,
                    VXML_DIALOG_MANAGER_DOCUMENT_ERROR);
        check_equal(probe.open_calls, (size_t)0u);
        check_equal(probe.policy_open_calls, (size_t)0u);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.entries, (size_t)0u);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("closes timed fetch leases when compilation rejects the document") {
        static const char malformed[] = "<vxml";
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_OK,
            .body = malformed,
            .body_size = sizeof(malformed) - 1u};
        vxml_document_store store = {0};
        vxml_document_ref ref = {0};
        vxml_document_store_stats stats = {0};
        vxml_document_fetch_policy_v1 policy =
            VXML_DOCUMENT_FETCH_POLICY_V1_INIT;

        init_store(&store, &probe, 1u, 2048u);
        policy.has_timeout = true;
        policy.timeout_us = UINT64_C(500000);
        check_equal(vxml_document_store_acquire_with_policy(
                        &store, "https://voice.example/bad-timed.vxml",
                        sizeof("https://voice.example/bad-timed.vxml") - 1u,
                        &policy, &ref, NULL),
                    VXML_DOCUMENT_STORE_COMPILE_ERROR);
        check_equal(probe.policy_open_calls, (size_t)1u);
        check_equal(probe.open_calls, (size_t)1u);
        check_equal(probe.close_calls, (size_t)1u);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.entries, (size_t)0u);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("brackets a real cache miss with exact fetchaudio timing") {
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_OK,
            .body = valid_document,
            .body_size = sizeof(valid_document) - 1u};
        fetch_audio_probe audio = {
            .document = &probe,
            .result = VXML_FETCH_AUDIO_STARTED};
        vxml_document_store_config_v1 config =
            store_config(&probe, 2u, 4096u);
        vxml_document_store store = {0};
        vxml_document_ref ref = {0};
        vxml_document_fetch_policy_v1 policy =
            VXML_DOCUMENT_FETCH_POLICY_V1_INIT;
        static const char target[] =
            "https://voice.example/slow.vxml";
        static const char audio_uri[] =
            "https://voice.example/media/wait.wav";

        config.fetch_audio = &fetch_audio_adapter;
        config.fetch_audio_user = &audio;
        check_equal(vxml_document_store_init(&store, &config),
                    VXML_DOCUMENT_STORE_OK);

        policy.has_fetchaudio = true;
        policy.fetchaudio_uri = audio_uri;
        policy.fetchaudio_uri_size = sizeof(audio_uri) - 1u;
        policy.has_fetchaudio_delay = true;
        policy.fetchaudio_delay_us = UINT64_C(250000);
        policy.has_fetchaudio_minimum = true;
        policy.fetchaudio_minimum_us = UINT64_C(1500000);

        check_equal(vxml_document_store_acquire_with_policy(
                        &store, target, sizeof(target) - 1u,
                        &policy, &ref, NULL),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(audio.begin_calls, (size_t)1u);
        check_equal(audio.finish_calls, (size_t)1u);
        check_equal(audio.uri, audio_uri);
        check_true(audio.has_delay);
        check_equal(audio.delay_us, UINT64_C(250000));
        check_true(audio.has_minimum);
        check_equal(audio.minimum_us, UINT64_C(1500000));
        check_true(audio.begin_sequence < probe.open_sequence);
        check_true(probe.open_sequence < audio.finish_sequence);
        check_equal(vxml_document_store_release(&store, &ref),
                    VXML_DOCUMENT_STORE_OK);

        /* A cache hit performs no document fetch and no fetchaudio handoff. */
        check_equal(vxml_document_store_acquire_with_policy(
                        &store, target, sizeof(target) - 1u,
                        &policy, &ref, NULL),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(audio.begin_calls, (size_t)1u);
        check_equal(audio.finish_calls, (size_t)1u);
        check_equal(probe.open_calls, (size_t)1u);
        check_equal(vxml_document_store_release(&store, &ref),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("preserves explicit zero versus absent fetchaudio timing") {
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_OK,
            .body = valid_document,
            .body_size = sizeof(valid_document) - 1u};
        fetch_audio_probe audio = {
            .document = &probe,
            .result = VXML_FETCH_AUDIO_STARTED};
        vxml_document_store_config_v1 config =
            store_config(&probe, 2u, 4096u);
        vxml_document_store store = {0};
        vxml_document_ref ref = {0};
        vxml_document_fetch_policy_v1 policy =
            VXML_DOCUMENT_FETCH_POLICY_V1_INIT;
        static const char audio_uri[] =
            "https://voice.example/media/wait.wav";

        config.fetch_audio = &fetch_audio_adapter;
        config.fetch_audio_user = &audio;
        check_equal(vxml_document_store_init(&store, &config),
                    VXML_DOCUMENT_STORE_OK);
        policy.has_fetchaudio = true;
        policy.fetchaudio_uri = audio_uri;
        policy.fetchaudio_uri_size = sizeof(audio_uri) - 1u;
        policy.has_fetchaudio_delay = true;
        policy.fetchaudio_delay_us = UINT64_C(0);
        policy.has_fetchaudio_minimum = true;
        policy.fetchaudio_minimum_us = UINT64_C(0);

        check_equal(vxml_document_store_acquire_with_policy(
                        &store, "https://voice.example/zero-audio.vxml",
                        sizeof("https://voice.example/zero-audio.vxml") - 1u,
                        &policy, &ref, NULL),
                    VXML_DOCUMENT_STORE_OK);
        check_true(audio.has_delay);
        check_equal(audio.delay_us, UINT64_C(0));
        check_true(audio.has_minimum);
        check_equal(audio.minimum_us, UINT64_C(0));
        check_equal(vxml_document_store_release(&store, &ref),
                    VXML_DOCUMENT_STORE_OK);

        policy.has_fetchaudio_delay = false;
        policy.has_fetchaudio_minimum = false;
        check_equal(vxml_document_store_acquire_with_policy(
                        &store, "https://voice.example/absent-audio.vxml",
                        sizeof("https://voice.example/absent-audio.vxml") - 1u,
                        &policy, &ref, NULL),
                    VXML_DOCUMENT_STORE_OK);
        check_false(audio.has_delay);
        check_false(audio.has_minimum);
        check_equal(audio.begin_calls, (size_t)2u);
        check_equal(audio.finish_calls, (size_t)2u);
        check_equal(vxml_document_store_release(&store, &ref),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("finishes started fetchaudio when the main document fetch fails") {
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_DOCUMENT_ERROR,
            .body = valid_document,
            .body_size = sizeof(valid_document) - 1u};
        fetch_audio_probe audio = {
            .document = &probe,
            .result = VXML_FETCH_AUDIO_STARTED};
        vxml_document_store_config_v1 config =
            store_config(&probe, 1u, 2048u);
        vxml_document_store store = {0};
        vxml_document_ref ref = {0};
        vxml_document_store_error error = {0};
        vxml_document_fetch_policy_v1 policy =
            VXML_DOCUMENT_FETCH_POLICY_V1_INIT;

        config.fetch_audio = &fetch_audio_adapter;
        config.fetch_audio_user = &audio;
        check_equal(vxml_document_store_init(&store, &config),
                    VXML_DOCUMENT_STORE_OK);
        policy.has_fetchaudio = true;
        policy.fetchaudio_uri = "https://voice.example/wait.wav";
        policy.fetchaudio_uri_size =
            sizeof("https://voice.example/wait.wav") - 1u;

        check_equal(vxml_document_store_acquire_with_policy(
                        &store, "https://voice.example/fail.vxml",
                        sizeof("https://voice.example/fail.vxml") - 1u,
                        &policy, &ref, &error),
                    VXML_DOCUMENT_STORE_RESOURCE_ERROR);
        check_equal(error.resource_status,
                    VXML_DIALOG_MANAGER_DOCUMENT_ERROR);
        check_equal(audio.begin_calls, (size_t)1u);
        check_equal(audio.finish_calls, (size_t)1u);
        check_true(audio.begin_sequence < probe.open_sequence);
        check_true(probe.open_sequence < audio.finish_sequence);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("treats unavailable fetchaudio as silent for main fetch success or failure") {
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_OK,
            .body = valid_document,
            .body_size = sizeof(valid_document) - 1u};
        fetch_audio_probe audio = {
            .document = &probe,
            .result = VXML_FETCH_AUDIO_SKIPPED};
        vxml_document_store_config_v1 config =
            store_config(&probe, 2u, 4096u);
        vxml_document_store store = {0};
        vxml_document_ref ref = {0};
        vxml_document_store_error error = {0};
        vxml_document_fetch_policy_v1 policy =
            VXML_DOCUMENT_FETCH_POLICY_V1_INIT;

        config.fetch_audio = &fetch_audio_adapter;
        config.fetch_audio_user = &audio;
        check_equal(vxml_document_store_init(&store, &config),
                    VXML_DOCUMENT_STORE_OK);
        policy.has_fetchaudio = true;
        policy.fetchaudio_uri = "https://voice.example/missing-wait.wav";
        policy.fetchaudio_uri_size =
            sizeof("https://voice.example/missing-wait.wav") - 1u;

        check_equal(vxml_document_store_acquire_with_policy(
                        &store, "https://voice.example/ok.vxml",
                        sizeof("https://voice.example/ok.vxml") - 1u,
                        &policy, &ref, &error),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(audio.begin_calls, (size_t)1u);
        check_equal(audio.finish_calls, (size_t)0u);
        check_equal(vxml_document_store_release(&store, &ref),
                    VXML_DOCUMENT_STORE_OK);

        probe.open_status = VXML_DIALOG_MANAGER_DOCUMENT_ERROR;
        error = (vxml_document_store_error){0};
        check_equal(vxml_document_store_acquire_with_policy(
                        &store, "https://voice.example/fail-after-skip.vxml",
                        sizeof("https://voice.example/fail-after-skip.vxml") - 1u,
                        &policy, &ref, &error),
                    VXML_DOCUMENT_STORE_RESOURCE_ERROR);
        check_equal(error.resource_status,
                    VXML_DIALOG_MANAGER_DOCUMENT_ERROR);
        check_equal(audio.begin_calls, (size_t)2u);
        check_equal(audio.finish_calls, (size_t)0u);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("accepts historical config and fetch-policy prefixes without fetchaudio") {
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_OK,
            .body = valid_document,
            .body_size = sizeof(valid_document) - 1u};
        fetch_audio_probe audio = {
            .document = &probe,
            .result = VXML_FETCH_AUDIO_STARTED};
        vxml_document_store_config_v1 config =
            store_config(&probe, 1u, 2048u);
        vxml_document_store store = {0};
        vxml_document_ref ref = {0};
        vxml_document_fetch_policy_v1 policy =
            VXML_DOCUMENT_FETCH_POLICY_V1_INIT;

        config.struct_size =
            offsetof(vxml_document_store_config_v1, document_user) +
            sizeof(config.document_user);
        config.fetch_audio = &fetch_audio_adapter;
        config.fetch_audio_user = &audio;
        check_equal(vxml_document_store_init(&store, &config),
                    VXML_DOCUMENT_STORE_OK);

        policy.struct_size =
            offsetof(vxml_document_fetch_policy_v1, timeout_us) +
            sizeof(policy.timeout_us);
        policy.has_timeout = true;
        policy.timeout_us = UINT64_C(99);
        policy.has_fetchaudio = true;
        policy.fetchaudio_uri = "https://voice.example/ignored.wav";
        policy.fetchaudio_uri_size =
            sizeof("https://voice.example/ignored.wav") - 1u;

        check_equal(vxml_document_store_acquire_with_policy(
                        &store, "https://voice.example/legacy.vxml",
                        sizeof("https://voice.example/legacy.vxml") - 1u,
                        &policy, &ref, NULL),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(probe.policy_open_calls, (size_t)1u);
        check_true(probe.observed_has_timeout);
        check_equal(probe.observed_timeout_us, UINT64_C(99));
        check_equal(audio.begin_calls, (size_t)0u);
        check_equal(vxml_document_store_release(&store, &ref),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("invalidates one released borrow without consuming a peer borrow") {
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_OK,
            .body = valid_document,
            .body_size = sizeof(valid_document) - 1u};
        vxml_document_store store = {0};
        vxml_document_ref first = {0};
        vxml_document_ref second = {0};
        vxml_document_ref stale_copy;
        vxml_document_view view = {0};
        vxml_document_store_stats stats = {0};
        static const char uri[] = "https://voice.example/shared.vxml";

        init_store(&store, &probe, 2u, 2048u);
        check_equal(
            vxml_document_store_acquire(
                &store, uri, sizeof(uri) - 1u, &first, NULL),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_acquire(
                &store, uri, sizeof(uri) - 1u, &second, NULL),
            VXML_DOCUMENT_STORE_OK);
        check_equal(probe.open_calls, (size_t)1u);
        stale_copy = first;

        check_equal(
            vxml_document_store_release(&store, &first),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_view(&store, stale_copy, &view),
            VXML_DOCUMENT_STORE_STALE);
        check_equal(
            vxml_document_store_release(&store, &stale_copy),
            VXML_DOCUMENT_STORE_STALE);
        check_equal(
            vxml_document_store_view(&store, second, &view),
            VXML_DOCUMENT_STORE_OK);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.active_borrows, (size_t)1u);

        check_equal(
            vxml_document_store_release(&store, &second),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("bounds live borrows and generation-protects an evicted row") {
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_OK,
            .body = valid_document,
            .body_size = sizeof(valid_document) - 1u};
        vxml_document_store store = {0};
        vxml_document_ref first = {0};
        vxml_document_ref saved_first;
        vxml_document_ref second = {0};
        vxml_document_view view = {0};

        init_store(&store, &probe, 1u, 2048u);
        check_equal(
            vxml_document_store_acquire(
                &store,
                "https://voice.example/app/a.vxml",
                sizeof("https://voice.example/app/a.vxml") - 1u,
                &first, NULL),
            VXML_DOCUMENT_STORE_OK);
        saved_first = first;

        check_equal(
            vxml_document_store_acquire(
                &store,
                "https://voice.example/app/b.vxml",
                sizeof("https://voice.example/app/b.vxml") - 1u,
                &second, NULL),
            VXML_DOCUMENT_STORE_FULL);
        check_equal(probe.open_calls, (size_t)1u);

        check_equal(
            vxml_document_store_release(&store, &first),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_acquire(
                &store,
                "https://voice.example/app/b.vxml",
                sizeof("https://voice.example/app/b.vxml") - 1u,
                &second, NULL),
            VXML_DOCUMENT_STORE_OK);
        check_equal(probe.open_calls, (size_t)2u);
        check_equal(
            vxml_document_store_view(&store, saved_first, &view),
            VXML_DOCUMENT_STORE_STALE);

        check_equal(
            vxml_document_store_release(&store, &second),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("evicts the least-recently-used unpinned entry") {
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_OK,
            .body = valid_document,
            .body_size = sizeof(valid_document) - 1u};
        vxml_document_store store = {0};
        vxml_document_ref ref = {0};
        vxml_document_store_stats stats = {0};

        init_store(&store, &probe, 2u, 4096u);

        check_equal(vxml_document_store_acquire(
            &store, "https://voice.example/a.vxml",
            sizeof("https://voice.example/a.vxml") - 1u,
            &ref, NULL), VXML_DOCUMENT_STORE_OK);
        check_equal(vxml_document_store_release(&store, &ref),
                    VXML_DOCUMENT_STORE_OK);

        check_equal(vxml_document_store_acquire(
            &store, "https://voice.example/b.vxml",
            sizeof("https://voice.example/b.vxml") - 1u,
            &ref, NULL), VXML_DOCUMENT_STORE_OK);
        check_equal(vxml_document_store_release(&store, &ref),
                    VXML_DOCUMENT_STORE_OK);

        check_equal(vxml_document_store_acquire(
            &store, "https://voice.example/a.vxml",
            sizeof("https://voice.example/a.vxml") - 1u,
            &ref, NULL), VXML_DOCUMENT_STORE_OK);
        check_equal(vxml_document_store_release(&store, &ref),
                    VXML_DOCUMENT_STORE_OK);

        check_equal(vxml_document_store_acquire(
            &store, "https://voice.example/c.vxml",
            sizeof("https://voice.example/c.vxml") - 1u,
            &ref, NULL), VXML_DOCUMENT_STORE_OK);
        check_equal(vxml_document_store_release(&store, &ref),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(probe.open_calls, (size_t)3u);

        check_equal(vxml_document_store_acquire(
            &store, "https://voice.example/b.vxml",
            sizeof("https://voice.example/b.vxml") - 1u,
            &ref, NULL), VXML_DOCUMENT_STORE_OK);
        check_equal(probe.open_calls, (size_t)4u);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_true(stats.evictions >= UINT64_C(2));

        check_equal(vxml_document_store_release(&store, &ref),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("closes a fetched lease and publishes nothing when VoiceXML compilation fails") {
        static const char malformed[] = "<vxml";
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_OK,
            .body = malformed,
            .body_size = sizeof(malformed) - 1u};
        vxml_document_store store = {0};
        vxml_document_ref ref = {0};
        vxml_document_store_error error = {0};
        vxml_document_store_stats stats = {0};

        init_store(&store, &probe, 1u, 2048u);
        check_equal(
            vxml_document_store_acquire(
                &store,
                "https://voice.example/bad.vxml",
                sizeof("https://voice.example/bad.vxml") - 1u,
                &ref, &error),
            VXML_DOCUMENT_STORE_COMPILE_ERROR);
        check_equal(probe.open_calls, (size_t)1u);
        check_equal(probe.close_calls, (size_t)1u);
        check_true(error.voice_status != VXML_OK);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.entries, (size_t)0u);
        check_equal(stats.active_borrows, (size_t)0u);

        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("enforces aggregate cache-byte bounds before publication") {
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_OK,
            .body = valid_document,
            .body_size = sizeof(valid_document) - 1u};
        vxml_document_store store = {0};
        vxml_document_ref ref = {0};
        vxml_document_store_stats stats = {0};

        init_store(&store, &probe, 2u, 64u);
        check_equal(
            vxml_document_store_acquire(
                &store,
                "https://voice.example/app/large.vxml",
                sizeof("https://voice.example/app/large.vxml") - 1u,
                &ref, NULL),
            VXML_DOCUMENT_STORE_LIMIT_EXCEEDED);
        check_equal(probe.open_calls, (size_t)1u);
        check_equal(probe.close_calls, (size_t)1u);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.entries, (size_t)0u);
        check_equal(stats.cached_bytes, (size_t)0u);

        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("retains the first provider failure without creating a cache row") {
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_DOCUMENT_ERROR,
            .body = valid_document,
            .body_size = sizeof(valid_document) - 1u};
        vxml_document_store store = {0};
        vxml_document_ref ref = {0};
        vxml_document_store_error error = {0};
        vxml_document_store_stats stats = {0};

        init_store(&store, &probe, 1u, 2048u);
        check_equal(
            vxml_document_store_acquire(
                &store,
                "https://voice.example/missing.vxml",
                sizeof("https://voice.example/missing.vxml") - 1u,
                &ref, &error),
            VXML_DOCUMENT_STORE_RESOURCE_ERROR);
        check_equal(
            error.resource_status,
            VXML_DIALOG_MANAGER_DOCUMENT_ERROR);
        check_equal(probe.open_calls, (size_t)1u);
        check_equal(probe.close_calls, (size_t)0u);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.entries, (size_t)0u);

        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("clear and destroy refuse live borrows then succeed after release") {
        document_probe probe = {
            .open_status = VXML_DIALOG_MANAGER_OK,
            .body = valid_document,
            .body_size = sizeof(valid_document) - 1u};
        vxml_document_store store = {0};
        vxml_document_ref ref = {0};
        vxml_document_ref saved;
        vxml_document_view view = {0};
        vxml_document_store_stats stats = {0};

        init_store(&store, &probe, 1u, 2048u);
        check_equal(vxml_document_store_acquire(
            &store, "https://voice.example/a.vxml",
            sizeof("https://voice.example/a.vxml") - 1u,
            &ref, NULL), VXML_DOCUMENT_STORE_OK);
        saved = ref;

        check_equal(vxml_document_store_clear(&store),
                    VXML_DOCUMENT_STORE_BUSY);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_BUSY);
        check_equal(vxml_document_store_release(&store, &ref),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(vxml_document_store_clear(&store),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(vxml_document_store_view(&store, saved, &view),
                    VXML_DOCUMENT_STORE_STALE);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.entries, (size_t)0u);
        check_equal(stats.cached_bytes, (size_t)0u);

        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }
}
