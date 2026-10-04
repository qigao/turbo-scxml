#include <voicexml/dialog_manager.h>
#include <voicexml/document_store.h>
#include <tinytest.h>

#include "voicexml_internal.h"

#include <string.h>

typedef struct fake_profile_probe {
    size_t sequence;
    size_t init_sequence;
    size_t start_sequence;
    size_t raise_sequence;
    size_t destroy_sequence;
    size_t raise_calls;
    size_t destroy_calls;
    vxml_status raise_status;
    bool submit_on_start;
    const char *submit_uri;
    size_t submit_uri_size;
    const vxml_submit_field_v1 *submit_fields;
    size_t submit_field_count;
    vxml_submit_enctype submit_enctype;
    const vxml_submit_recording_field_v1 *submit_recordings;
    size_t submit_recording_count;
    const vxml_submit_multipart_part_ref_v1 *submit_parts;
    size_t submit_part_count;
    bool submit_has_timeout;
    uint64_t submit_timeout_us;
    const char *submit_fetchaudio_uri;
    size_t submit_fetchaudio_uri_size;
    char event[96];
} fake_profile_probe;

static fake_profile_probe *active_fake_profile;

static vxml_status fake_profile_init(
    vxml_session_impl *session, const void *options) {
    fake_profile_probe *probe = active_fake_profile;
    (void)options;
    if (session == NULL || probe == NULL)
        return VXML_INVALID_CONTRACT;
    session->profile_data = probe;
    probe->init_sequence = ++probe->sequence;
    return VXML_OK;
}

static vxml_status fake_profile_start(vxml_session_impl *session) {
    fake_profile_probe *probe = session != NULL
        ? (fake_profile_probe *)session->profile_data : NULL;
    if (probe == NULL)
        return VXML_INVALID_CONTRACT;
    probe->start_sequence = ++probe->sequence;
    if (probe->submit_on_start) {
        if (probe->submit_uri == NULL ||
            probe->submit_uri_size == 0u)
            return VXML_INVALID_CONTRACT;
        session->submit_uri = probe->submit_uri;
        session->submit_uri_size = probe->submit_uri_size;
        session->submit_method = VXML_SUBMIT_METHOD_POST;
        session->submit_enctype =
            probe->submit_enctype != 0
                ? probe->submit_enctype
                : VXML_SUBMIT_ENCTYPE_URLENCODED;
        session->submit_fields = probe->submit_fields;
        session->submit_field_count =
            probe->submit_field_count;
        session->submit_recordings =
            probe->submit_recordings;
        session->submit_recording_count =
            probe->submit_recording_count;
        session->submit_parts = probe->submit_parts;
        session->submit_part_count =
            probe->submit_part_count;
        session->submit_has_timeout =
            probe->submit_has_timeout;
        session->submit_timeout_us =
            probe->submit_timeout_us;
        session->submit_fetchaudio_uri =
            probe->submit_fetchaudio_uri;
        session->submit_fetchaudio_uri_size =
            probe->submit_fetchaudio_uri_size;
        session->submit_has_fetchaudio_delay = false;
        session->submit_fetchaudio_delay_us = UINT64_C(0);
        session->submit_has_fetchaudio_minimum = false;
        session->submit_fetchaudio_minimum_us = UINT64_C(0);
        session->state = VXML_SESSION_SUBMITTING;
    }
    return VXML_OK;
}

static vxml_status fake_profile_raise(
    vxml_session_impl *session,
    const char *event_name,
    size_t event_name_size) {
    fake_profile_probe *probe = session != NULL
        ? (fake_profile_probe *)session->profile_data : NULL;
    if (probe == NULL || event_name == NULL ||
        event_name_size >= sizeof(probe->event))
        return VXML_INVALID_CONTRACT;
    ++probe->raise_calls;
    probe->raise_sequence = ++probe->sequence;
    memcpy(probe->event, event_name, event_name_size);
    probe->event[event_name_size] = '\0';
    return probe->raise_status;
}

static void fake_profile_destroy(vxml_session_impl *session) {
    fake_profile_probe *probe = session != NULL
        ? (fake_profile_probe *)session->profile_data : NULL;
    if (probe == NULL) return;
    ++probe->destroy_calls;
    probe->destroy_sequence = ++probe->sequence;
    session->profile_data = NULL;
}

typedef struct upstream_probe {
    size_t accept_calls;
    size_t close_calls;
    bool reject_accept;
    bool quiescent;
} upstream_probe;

static void noop_ticket(void *user) {
    (void)user;
}

static scxml_adapter_status upstream_prepare_accept(
    void *user,
    const ccxml_accept_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    upstream_probe *probe = (upstream_probe *)user;
    (void)request;
    if (probe == NULL || out_ticket == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    ++probe->accept_calls;
    if (probe->reject_accept) {
        if (out_error != NULL) *out_error = "upstream refused";
        return SCXML_ADAPTER_ERROR_EXECUTION;
    }
    *out_ticket = (cflow_statechart_effect_ticket){
        .commit = noop_ticket,
        .discard = noop_ticket,
        .user = probe};
    if (out_error != NULL) *out_error = NULL;
    return SCXML_ADAPTER_ACCEPTED;
}

static void upstream_close(void *user) {
    upstream_probe *probe = (upstream_probe *)user;
    if (probe != NULL) ++probe->close_calls;
}

static bool upstream_is_quiescent(void *user) {
    upstream_probe *probe = (upstream_probe *)user;
    return probe != NULL && probe->quiescent;
}

static const ccxml_telephony_adapter_v1 upstream_adapter = {
    .abi_version = CCXML_TELEPHONY_ADAPTER_ABI_V1,
    .struct_size = sizeof(ccxml_telephony_adapter_v1),
    .prepare_accept = upstream_prepare_accept,
    .close = upstream_close,
    .is_quiescent = upstream_is_quiescent};

typedef struct document_probe {
    size_t open_calls;
    size_t close_calls;
    vxml_dialog_manager_status status;
    const char *expected_source;
    size_t expected_source_size;
} document_probe;

static const char voice_document[] =
    "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
    "<form id='main'><block><exit/></block></form></vxml>";

static vxml_dialog_manager_status document_open(
    void *user,
    const char *source, size_t source_size,
    const char *media_type, size_t media_type_size,
    size_t max_bytes,
    vxml_dialog_document *out_document) {
    document_probe *probe = (document_probe *)user;
    static const char default_source[] = "mem:voice";
    static const char expected_media[] = "application/voicexml+xml";
    const char *expected_source;
    size_t expected_source_size;
    if (probe == NULL) return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;
    expected_source = probe->expected_source != NULL
        ? probe->expected_source : default_source;
    expected_source_size = probe->expected_source != NULL
        ? probe->expected_source_size : sizeof(default_source) - 1u;
    if (out_document == NULL ||
        source == NULL || media_type == NULL ||
        source_size != expected_source_size ||
        memcmp(source, expected_source, source_size) != 0 ||
        media_type_size != sizeof(expected_media) - 1u ||
        memcmp(media_type, expected_media, media_type_size) != 0)
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;
    ++probe->open_calls;
    if (probe->status != VXML_DIALOG_MANAGER_OK)
        return probe->status;
    if (sizeof(voice_document) - 1u > max_bytes)
        return VXML_DIALOG_MANAGER_DOCUMENT_ERROR;
    *out_document = (vxml_dialog_document){
        .data = voice_document,
        .size = sizeof(voice_document) - 1u,
        .lease = probe};
    return VXML_DIALOG_MANAGER_OK;
}

static void document_close(
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
    .open = document_open,
    .close = document_close};

typedef struct event_row {
    char name[64];
    char dialog_id[96];
    char connection_id[96];
    vxml_status voice_status;
} event_row;

typedef struct event_probe {
    event_row rows[16];
    size_t count;
    bool full;
    bool closed;
} event_probe;

static vxml_dialog_event_sink_status event_publish(
    void *user, const vxml_dialog_event_v1 *event) {
    event_probe *probe = (event_probe *)user;
    event_row *row;
    if (probe == NULL || event == NULL)
        return VXML_DIALOG_EVENT_INVALID_ARGUMENT;
    if (probe->closed)
        return VXML_DIALOG_EVENT_CLOSED;
    if (probe->full || probe->count >= 16u)
        return VXML_DIALOG_EVENT_FULL;
    if (event->name_size >= sizeof(probe->rows[0].name) ||
        event->dialog_id_size >= sizeof(probe->rows[0].dialog_id) ||
        event->connection_id_size >=
            sizeof(probe->rows[0].connection_id))
        return VXML_DIALOG_EVENT_INVALID_ARGUMENT;
    row = &probe->rows[probe->count++];
    memcpy(row->name, event->name, event->name_size);
    row->name[event->name_size] = '\0';
    memcpy(row->dialog_id, event->dialog_id, event->dialog_id_size);
    row->dialog_id[event->dialog_id_size] = '\0';
    if (event->connection_id_size != 0u)
        memcpy(
            row->connection_id,
            event->connection_id,
            event->connection_id_size);
    row->connection_id[event->connection_id_size] = '\0';
    row->voice_status = event->voice_status;
    return VXML_DIALOG_EVENT_ACCEPTED;
}

static const vxml_dialog_event_sink_v1 event_sink = {
    .abi_version = VXML_DIALOG_EVENT_SINK_ABI_V1,
    .struct_size = sizeof(vxml_dialog_event_sink_v1),
    .try_publish = event_publish};

static vxml_dialog_manager_status manager_init(
    vxml_dialog_manager *manager,
    size_t capacity,
    upstream_probe *upstream,
    document_probe *documents,
    event_probe *events) {
    vxml_dialog_manager_config_v1 config =
        vxml_dialog_manager_default_config_v1();
    config.capacity = capacity;
    config.upstream = &upstream_adapter;
    config.upstream_user = upstream;
    config.documents = &document_adapter;
    config.document_user = documents;
    config.events = &event_sink;
    config.event_user = events;
    return vxml_dialog_manager_init(manager, &config);
}

static vxml_document_store_status store_init(
    vxml_document_store *store,
    document_probe *documents,
    size_t capacity) {
    static const char application_uri[] =
        "https://voice.example/app/root.vxml";
    const vxml_document_store_config_v1 config = {
        .abi_version = VXML_DOCUMENT_STORE_CONFIG_ABI_V1,
        .struct_size = sizeof(vxml_document_store_config_v1),
        .application_uri = application_uri,
        .application_uri_size = sizeof(application_uri) - 1u,
        .capacity = capacity,
        .max_uri_bytes = 256u,
        .max_document_bytes = 512u,
        .max_cache_bytes = 4096u,
        .voice_limits = {
            .xml = {0}
        },
        .documents = &document_adapter,
        .document_user = documents};
    vxml_document_store_config_v1 normalized = config;
    normalized.voice_limits = vxml_default_limits();
    return vxml_document_store_init(store, &normalized);
}

static vxml_dialog_manager_status manager_init_v2(
    vxml_dialog_manager *manager,
    size_t capacity,
    upstream_probe *upstream,
    vxml_document_store *store,
    event_probe *events) {
    vxml_dialog_manager_config_v2 config =
        vxml_dialog_manager_default_config_v2();
    config.capacity = capacity;
    config.max_source_bytes = 256u;
    config.upstream = &upstream_adapter;
    config.upstream_user = upstream;
    config.document_store = store;
    config.events = &event_sink;
    config.event_user = events;
    return vxml_dialog_manager_init_v2(manager, &config);
}

typedef struct navigation_document_entry {
    const char *uri;
    const char *body;
    vxml_dialog_manager_status status;
} navigation_document_entry;

typedef struct navigation_document_probe {
    const navigation_document_entry *entries;
    size_t entry_count;
    size_t open_calls;
    size_t close_calls;
    size_t fetch_audio_begin_calls;
    size_t fetch_audio_finish_calls;
    size_t sequence;
    size_t fetch_audio_begin_sequence;
    size_t fetch_audio_finish_sequence;
    char fetch_audio_uri[256];
    size_t fetch_audio_uri_size;
} navigation_document_probe;

static vxml_dialog_manager_status navigation_document_open(
    void *user,
    const char *source, size_t source_size,
    const char *media_type, size_t media_type_size,
    size_t max_bytes,
    vxml_dialog_document *out_document) {
    static const char expected_media[] = "application/voicexml+xml";
    navigation_document_probe *probe =
        (navigation_document_probe *)user;
    size_t index;
    if (probe == NULL || source == NULL ||
        out_document == NULL || media_type == NULL ||
        media_type_size != sizeof(expected_media) - 1u ||
        memcmp(media_type, expected_media, media_type_size) != 0)
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;
    ++probe->open_calls;
    for (index = 0u; index < probe->entry_count; ++index) {
        const navigation_document_entry *entry =
            &probe->entries[index];
        const size_t uri_size = strlen(entry->uri);
        const size_t body_size =
            entry->body != NULL ? strlen(entry->body) : 0u;
        if (uri_size != source_size ||
            memcmp(entry->uri, source, source_size) != 0)
            continue;
        if (entry->status != VXML_DIALOG_MANAGER_OK)
            return entry->status;
        if (entry->body == NULL || body_size > max_bytes)
            return VXML_DIALOG_MANAGER_DOCUMENT_ERROR;
        *out_document = (vxml_dialog_document){
            .data = entry->body,
            .size = body_size,
            .lease = (void *)entry};
        return VXML_DIALOG_MANAGER_OK;
    }
    return VXML_DIALOG_MANAGER_DOCUMENT_ERROR;
}

static void navigation_document_close(
    void *user, vxml_dialog_document *document) {
    navigation_document_probe *probe =
        (navigation_document_probe *)user;
    if (probe != NULL && document != NULL &&
        document->lease != NULL)
        ++probe->close_calls;
    if (document != NULL)
        memset(document, 0, sizeof(*document));
}

static void navigation_fetch_audio_finish(void *user) {
    navigation_document_probe *probe =
        (navigation_document_probe *)user;
    if (probe != NULL) {
        ++probe->fetch_audio_finish_calls;
        probe->fetch_audio_finish_sequence =
            ++probe->sequence;
    }
}

static vxml_fetch_audio_begin_result navigation_fetch_audio_begin(
    void *user,
    const vxml_fetch_audio_request_v1 *request,
    vxml_fetch_audio_ticket_v1 *out_ticket) {
    navigation_document_probe *probe =
        (navigation_document_probe *)user;
    if (out_ticket != NULL)
        *out_ticket = (vxml_fetch_audio_ticket_v1){0};
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        request->abi_version != VXML_FETCH_AUDIO_REQUEST_ABI_V1 ||
        request->struct_size < sizeof(*request) ||
        request->uri == NULL || request->uri_size == 0u ||
        request->uri_size >= sizeof(probe->fetch_audio_uri) ||
        request->has_delay || request->has_minimum)
        return (vxml_fetch_audio_begin_result)99;
    ++probe->fetch_audio_begin_calls;
    probe->fetch_audio_begin_sequence =
        ++probe->sequence;
    memcpy(
        probe->fetch_audio_uri,
        request->uri,
        request->uri_size);
    probe->fetch_audio_uri[request->uri_size] = '\0';
    probe->fetch_audio_uri_size = request->uri_size;
    *out_ticket = (vxml_fetch_audio_ticket_v1){
        .finish = navigation_fetch_audio_finish,
        .user = probe};
    return VXML_FETCH_AUDIO_STARTED;
}

static const vxml_fetch_audio_adapter_v1
navigation_fetch_audio_adapter = {
    .abi_version = VXML_FETCH_AUDIO_ADAPTER_ABI_V1,
    .struct_size = sizeof(vxml_fetch_audio_adapter_v1),
    .begin = navigation_fetch_audio_begin};

static const vxml_dialog_document_adapter_v1
navigation_document_adapter = {
    .abi_version = VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1,
    .struct_size = sizeof(vxml_dialog_document_adapter_v1),
    .open = navigation_document_open,
    .close = navigation_document_close};

static vxml_document_store_status navigation_store_init(
    vxml_document_store *store,
    navigation_document_probe *documents,
    size_t capacity) {
    static const char application_uri[] =
        "https://voice.example/app/root.vxml";
    vxml_document_store_config_v1 config = {
        .abi_version = VXML_DOCUMENT_STORE_CONFIG_ABI_V1,
        .struct_size = sizeof(vxml_document_store_config_v1),
        .application_uri = application_uri,
        .application_uri_size = sizeof(application_uri) - 1u,
        .capacity = capacity,
        .max_uri_bytes = 256u,
        .max_document_bytes = 1024u,
        .max_cache_bytes = 8192u,
        .voice_limits = {0},
        .documents = &navigation_document_adapter,
        .document_user = documents,
        .fetch_audio = &navigation_fetch_audio_adapter,
        .fetch_audio_user = documents};
    config.voice_limits = vxml_default_limits();
    return vxml_document_store_init(store, &config);
}


typedef struct profile_pair_probe {
    size_t compile_calls;
    size_t factory_calls;
    size_t start_calls;
    size_t start_at_calls;
    size_t destroy_calls;
    size_t last_form_index;
    const void *compile_sources[4];
    size_t compile_sizes[4];
} profile_pair_probe;

static vxml_status profile_pair_init(
    vxml_session_impl *session,
    const void *options) {
    if (session == NULL || options == NULL)
        return VXML_INVALID_CONTRACT;
    session->profile_data = (void *)options;
    return VXML_OK;
}

static vxml_status profile_pair_start(
    vxml_session_impl *session) {
    profile_pair_probe *probe = session != NULL
        ? (profile_pair_probe *)session->profile_data : NULL;
    if (probe == NULL)
        return VXML_INVALID_CONTRACT;
    ++probe->start_calls;
    if (probe->start_calls == 1u) {
        static const char target[] = "../submit";
        session->submit_uri = target;
        session->submit_uri_size = sizeof(target) - 1u;
        session->submit_method = VXML_SUBMIT_METHOD_POST;
        session->submit_enctype =
            VXML_SUBMIT_ENCTYPE_URLENCODED;
        session->submit_fields = NULL;
        session->submit_field_count = 0u;
        session->state = VXML_SESSION_SUBMITTING;
    } else {
        session->state = VXML_SESSION_EXITED;
    }
    return VXML_OK;
}

static vxml_status profile_pair_start_at(
    vxml_session_impl *session,
    size_t form_index) {
    profile_pair_probe *probe = session != NULL
        ? (profile_pair_probe *)session->profile_data : NULL;
    if (probe == NULL)
        return VXML_INVALID_CONTRACT;
    ++probe->start_at_calls;
    probe->last_form_index = form_index;
    return profile_pair_start(session);
}

static void profile_pair_destroy(
    vxml_session_impl *session) {
    profile_pair_probe *probe = session != NULL
        ? (profile_pair_probe *)session->profile_data : NULL;
    if (probe != NULL)
        ++probe->destroy_calls;
    if (session != NULL)
        session->profile_data = NULL;
}

static vxml_status profile_pair_compile(
    void *user,
    const void *source, size_t source_size,
    vxml_program *out_program,
    vxml_diagnostic *diagnostic) {
    profile_pair_probe *probe =
        (profile_pair_probe *)user;
    vxml_limits limits = vxml_default_limits();
    vxml_program_impl *impl;
    vxml_status status;
    if (probe == NULL || source == NULL ||
        source_size == 0u || out_program == NULL)
        return VXML_INVALID_ARGUMENT;
    if (probe->compile_calls <
        sizeof(probe->compile_sources) /
            sizeof(probe->compile_sources[0])) {
        probe->compile_sources[probe->compile_calls] =
            source;
        probe->compile_sizes[probe->compile_calls] =
            source_size;
    }
    ++probe->compile_calls;
    status = vxml_compile(
        source, source_size, &limits,
        out_program, diagnostic);
    if (status != VXML_OK)
        return status;
    impl = (vxml_program_impl *)out_program->impl;
    if (impl == NULL) {
        vxml_program_destroy(out_program);
        return VXML_INVALID_CONTRACT;
    }
    impl->profile_kind = VXML_PROFILE_CMETA;
    impl->profile_session_init = profile_pair_init;
    impl->profile_session_start = profile_pair_start;
    impl->profile_session_start_at = profile_pair_start_at;
    impl->profile_session_destroy = profile_pair_destroy;
    return VXML_OK;
}

static const vxml_document_compile_adapter_v1
profile_pair_compiler = {
    .abi_version = VXML_DOCUMENT_COMPILE_ADAPTER_ABI_V1,
    .struct_size =
        sizeof(vxml_document_compile_adapter_v1),
    .compile = profile_pair_compile};

static vxml_status profile_pair_factory_init(
    void *user,
    vxml_session *session,
    const vxml_program *program) {
    profile_pair_probe *probe =
        (profile_pair_probe *)user;
    if (probe == NULL)
        return VXML_INVALID_ARGUMENT;
    ++probe->factory_calls;
    return vxml_session_init_profile(
        session, program, probe);
}

static const vxml_session_factory_v1
profile_pair_factory = {
    .abi_version = VXML_SESSION_FACTORY_ABI_V1,
    .struct_size = sizeof(vxml_session_factory_v1),
    .init = profile_pair_factory_init};

static vxml_document_store_status
profile_pair_store_init(
    vxml_document_store *store,
    navigation_document_probe *documents,
    profile_pair_probe *profile,
    size_t capacity) {
    static const char application_uri[] =
        "https://voice.example/app/root.vxml";
    vxml_document_store_config_v1 config = {
        .abi_version = VXML_DOCUMENT_STORE_CONFIG_ABI_V1,
        .struct_size =
            sizeof(vxml_document_store_config_v1),
        .application_uri = application_uri,
        .application_uri_size =
            sizeof(application_uri) - 1u,
        .capacity = capacity,
        .max_uri_bytes = 256u,
        .max_document_bytes = 1024u,
        .max_cache_bytes = 8192u,
        .voice_limits = {0},
        .documents = &navigation_document_adapter,
        .document_user = documents,
        .compiler = &profile_pair_compiler,
        .compiler_user = profile};
    config.voice_limits = vxml_default_limits();
    return vxml_document_store_init(store, &config);
}

typedef struct submit_probe {
    vxml_submit_resource_status status;
    const char *response_body;
    const char *effective_uri;
    size_t execute_calls;
    size_t execute_v2_calls;
    size_t close_calls;
    vxml_submit_method method;
    bool has_timeout;
    uint64_t timeout_us;
    navigation_document_probe *ordering_documents;
    size_t execute_sequence;
    const void *expected_borrowed;
    bool saw_borrowed;
    char uri[256];
    char content_type[128];
    char body[4096];
} submit_probe;

static vxml_submit_resource_status submit_execute(
    void *user,
    const vxml_submit_wire_request_v1 *request,
    vxml_submit_response *out_response) {
    static const char media[] = "application/voicexml+xml";
    submit_probe *probe = (submit_probe *)user;
    const size_t body_size =
        probe != NULL && probe->response_body != NULL
            ? strlen(probe->response_body) : 0u;
    const size_t historical_prefix =
        offsetof(vxml_submit_wire_request_v1, has_timeout);
    const size_t timeout_tail =
        offsetof(vxml_submit_wire_request_v1, timeout_us) +
        sizeof(request->timeout_us);
    if (probe == NULL || request == NULL ||
        out_response == NULL ||
        request->abi_version != VXML_SUBMIT_WIRE_REQUEST_ABI_V1 ||
        (request->struct_size != historical_prefix &&
         request->struct_size < timeout_tail) ||
        request->uri == NULL ||
        request->uri_size == 0u ||
        request->uri_size >= sizeof(probe->uri) ||
        request->content_type_size >=
            sizeof(probe->content_type) ||
        request->body_size >= sizeof(probe->body))
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    ++probe->execute_calls;
    probe->method = request->method;
    probe->has_timeout =
        request->struct_size >= timeout_tail
            ? request->has_timeout : false;
    probe->timeout_us =
        request->struct_size >= timeout_tail
            ? request->timeout_us : UINT64_C(0);
    if (probe->ordering_documents != NULL)
        probe->execute_sequence =
            ++probe->ordering_documents->sequence;
    memcpy(probe->uri, request->uri, request->uri_size);
    probe->uri[request->uri_size] = '\0';
    if (request->content_type_size != 0u) {
        memcpy(
            probe->content_type,
            request->content_type,
            request->content_type_size);
    }
    probe->content_type[request->content_type_size] = '\0';
    if (request->body_size != 0u)
        memcpy(probe->body, request->body, request->body_size);
    probe->body[request->body_size] = '\0';
    if (probe->status != VXML_SUBMIT_RESOURCE_OK)
        return probe->status;
    *out_response = (vxml_submit_response){
        .data = probe->response_body,
        .size = body_size,
        .media_type = media,
        .media_type_size = sizeof(media) - 1u,
        .effective_uri = probe->effective_uri,
        .effective_uri_size =
            probe->effective_uri != NULL
                ? strlen(probe->effective_uri) : 0u,
        .lease = probe};
    return VXML_SUBMIT_RESOURCE_OK;
}


static vxml_submit_resource_status submit_execute_v2(
    void *user,
    const vxml_submit_wire_request_v2 *request,
    vxml_submit_response *out_response) {
    static const char media[] = "application/voicexml+xml";
    const size_t historical_prefix =
        offsetof(vxml_submit_wire_request_v2, has_timeout);
    const size_t timeout_tail =
        offsetof(vxml_submit_wire_request_v2, timeout_us) +
        sizeof(request->timeout_us);
    submit_probe *probe = (submit_probe *)user;
    const size_t response_size =
        probe != NULL && probe->response_body != NULL
            ? strlen(probe->response_body) : 0u;
    size_t cursor = 0u;
    size_t index;

    if (probe == NULL || request == NULL ||
        out_response == NULL ||
        request->abi_version != VXML_SUBMIT_WIRE_REQUEST_ABI_V2 ||
        (request->struct_size != historical_prefix &&
         request->struct_size < timeout_tail) ||
        request->uri == NULL ||
        request->uri_size == 0u ||
        request->uri_size >= sizeof(probe->uri) ||
        request->content_type_size >=
            sizeof(probe->content_type) ||
        request->body_size >= sizeof(probe->body) ||
        request->segments == NULL ||
        request->segment_count == 0u ||
        request->method != VXML_SUBMIT_METHOD_POST)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;

    ++probe->execute_v2_calls;
    probe->method = request->method;
    probe->has_timeout =
        request->struct_size >= timeout_tail
            ? request->has_timeout : false;
    probe->timeout_us =
        request->struct_size >= timeout_tail
            ? request->timeout_us : UINT64_C(0);
    if (probe->ordering_documents != NULL)
        probe->execute_sequence =
            ++probe->ordering_documents->sequence;
    memcpy(probe->uri, request->uri, request->uri_size);
    probe->uri[request->uri_size] = '\0';
    memcpy(
        probe->content_type,
        request->content_type,
        request->content_type_size);
    probe->content_type[request->content_type_size] = '\0';

    for (index = 0u; index < request->segment_count; ++index) {
        const vxml_submit_body_segment_v1 *segment =
            &request->segments[index];
        if ((segment->size != 0u && segment->data == NULL) ||
            segment->size > request->body_size - cursor)
            return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
        if (segment->data == probe->expected_borrowed &&
            segment->size != 0u)
            probe->saw_borrowed = true;
        if (segment->size != 0u)
            memcpy(
                probe->body + cursor,
                segment->data, segment->size);
        cursor += segment->size;
    }
    if (cursor != request->body_size)
        return VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT;
    probe->body[cursor] = '\0';

    if (probe->status != VXML_SUBMIT_RESOURCE_OK)
        return probe->status;
    *out_response = (vxml_submit_response){
        .data = probe->response_body,
        .size = response_size,
        .media_type = media,
        .media_type_size = sizeof(media) - 1u,
        .effective_uri = probe->effective_uri,
        .effective_uri_size =
            probe->effective_uri != NULL
                ? strlen(probe->effective_uri) : 0u,
        .lease = probe};
    return VXML_SUBMIT_RESOURCE_OK;
}

static void submit_close(
    void *user, vxml_submit_response *response) {
    submit_probe *probe = (submit_probe *)user;
    if (probe != NULL && response != NULL &&
        response->lease == probe)
        ++probe->close_calls;
    if (response != NULL)
        *response = (vxml_submit_response){0};
}

static const vxml_submit_resource_adapter_v1 submit_adapter = {
    .abi_version = VXML_SUBMIT_RESOURCE_ADAPTER_ABI_V1,
    .struct_size = sizeof(vxml_submit_resource_adapter_v1),
    .execute = submit_execute,
    .close = submit_close,
    .execute_v2 = submit_execute_v2,
    .capabilities = VXML_SUBMIT_RESOURCE_CAP_TIMEOUT};

static vxml_dialog_manager_status manager_init_v4(
    vxml_dialog_manager *manager,
    size_t capacity,
    size_t max_navigation_hops,
    upstream_probe *upstream,
    vxml_document_store *store,
    submit_probe *submit,
    event_probe *events) {
    vxml_dialog_manager_config_v4 config =
        vxml_dialog_manager_default_config_v4();
    config.capacity = capacity;
    config.max_source_bytes = 256u;
    config.max_navigation_hops = max_navigation_hops;
    config.max_submit_response_bytes = 2048u;
    config.upstream = &upstream_adapter;
    config.upstream_user = upstream;
    config.document_store = store;
    config.submit = &submit_adapter;
    config.submit_user = submit;
    config.events = &event_sink;
    config.event_user = events;
    return vxml_dialog_manager_init_v4(manager, &config);
}

static vxml_dialog_manager_status manager_init_v4_profile(
    vxml_dialog_manager *manager,
    size_t capacity,
    size_t max_navigation_hops,
    upstream_probe *upstream,
    vxml_document_store *store,
    submit_probe *submit,
    event_probe *events,
    profile_pair_probe *profile) {
    vxml_dialog_manager_config_v4 config =
        vxml_dialog_manager_default_config_v4();
    config.capacity = capacity;
    config.max_source_bytes = 256u;
    config.max_navigation_hops =
        max_navigation_hops;
    config.max_submit_response_bytes = 2048u;
    config.upstream = &upstream_adapter;
    config.upstream_user = upstream;
    config.document_store = store;
    config.submit = &submit_adapter;
    config.submit_user = submit;
    config.events = &event_sink;
    config.event_user = events;
    config.session_factory = &profile_pair_factory;
    config.session_factory_user = profile;
    return vxml_dialog_manager_init_v4(
        manager, &config);
}

static vxml_dialog_manager_status manager_init_v3(
    vxml_dialog_manager *manager,
    size_t capacity,
    size_t max_navigation_hops,
    upstream_probe *upstream,
    vxml_document_store *store,
    event_probe *events) {
    vxml_dialog_manager_config_v3 config =
        vxml_dialog_manager_default_config_v3();
    config.capacity = capacity;
    config.max_source_bytes = 256u;
    config.max_navigation_hops = max_navigation_hops;
    config.upstream = &upstream_adapter;
    config.upstream_user = upstream;
    config.document_store = store;
    config.events = &event_sink;
    config.event_user = events;
    return vxml_dialog_manager_init_v3(manager, &config);
}

static void manager_close_destroy(
    vxml_dialog_manager *manager,
    upstream_probe *upstream) {
    vxml_dialog_manager_close(manager);
    upstream->quiescent = true;
    check_true(vxml_dialog_manager_is_quiescent(manager));
    check_equal(
        vxml_dialog_manager_destroy(manager),
        VXML_DIALOG_MANAGER_OK);
}

spec("VoiceXML dialog manager") {
    it("prepares then starts one dialog without doing work in ticket commit") {
        static const char source[] = "mem:voice";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-7";
        upstream_probe upstream = {0};
        document_probe documents = {
            .status = VXML_DIALOG_MANAGER_OK};
        event_probe events = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_prepare_request prepare = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u};
        ccxml_prepared_dialog_start_request start = {0};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;
        size_t processed = 0u;
        vxml_dialog_manager_stats stats = {0};

        check_equal(
            manager_init(
                &manager, 2u, &upstream, &documents, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();

        check_equal(
            adapter->prepare_dialog_prepare(
                vxml_dialog_manager_ccxml_user(&manager),
                &prepare, &dialog_id, &ticket, &error),
            SCXML_ADAPTER_ACCEPTED);
        check_not_null(dialog_id.data);
        check_true(dialog_id.size != 0u);
        check_equal(documents.open_calls, (size_t)0u);
        check_true(vxml_dialog_manager_get_stats(&manager, &stats));
        check_equal(stats.reserved, (size_t)1u);

        ticket.commit(ticket.user);
        check_equal(documents.open_calls, (size_t)0u);
        check_true(vxml_dialog_manager_get_stats(&manager, &stats));
        check_equal(stats.pending, (size_t)1u);

        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);
        check_equal(processed, (size_t)1u);
        check_equal(documents.open_calls, (size_t)1u);
        check_equal(documents.close_calls, (size_t)1u);
        check_equal(events.count, (size_t)1u);
        check_equal(events.rows[0].name, "dialog.prepared");
        check_true(vxml_dialog_manager_get_stats(&manager, &stats));
        check_equal(stats.prepared, (size_t)1u);

        start.dialog_id = dialog_id.data;
        start.dialog_id_size = dialog_id.size;
        start.connection_id = connection;
        start.connection_id_size = sizeof(connection) - 1u;
        ticket = (cflow_statechart_effect_ticket){0};
        check_equal(
            adapter->prepare_prepared_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &start, &ticket, &error),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(documents.open_calls, (size_t)1u);

        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);
        check_equal(events.count, (size_t)3u);
        check_equal(events.rows[1].name, "dialog.started");
        check_equal(events.rows[1].connection_id, "call-7");
        check_equal(events.rows[2].name, "dialog.exit");
        check_equal(documents.open_calls, (size_t)1u);
        check_true(vxml_dialog_manager_get_stats(&manager, &stats));
        check_equal(stats.active, (size_t)0u);

        manager_close_destroy(&manager, &upstream);
        check_equal(upstream.close_calls, (size_t)1u);
    }

    it("direct start shares the same compile and session path") {
        static const char source[] = "mem:voice";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-direct";
        upstream_probe upstream = {0};
        document_probe documents = {
            .status = VXML_DIALOG_MANAGER_OK};
        event_probe events = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        const ccxml_dialog_start_request request = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;
        size_t processed = 0u;

        check_equal(
            manager_init(
                &manager, 1u, &upstream, &documents, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &dialog_id, &ticket, &error),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(documents.open_calls, (size_t)0u);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);
        check_equal(documents.open_calls, (size_t)1u);
        check_equal(documents.close_calls, (size_t)1u);
        check_equal(events.count, (size_t)2u);
        check_equal(events.rows[0].name, "dialog.started");
        check_equal(events.rows[1].name, "dialog.exit");
        check_equal(events.rows[0].dialog_id, events.rows[1].dialog_id);

        manager_close_destroy(&manager, &upstream);
    }

    it("keeps terminal publication pending when the Event sink is full without reopening the document") {
        static const char source[] = "mem:voice";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-full";
        upstream_probe upstream = {0};
        document_probe documents = {
            .status = VXML_DIALOG_MANAGER_OK};
        event_probe events = {.full = true};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        const ccxml_dialog_start_request request = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;
        vxml_dialog_manager_stats stats = {0};

        check_equal(
            manager_init(
                &manager, 1u, &upstream, &documents, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);

        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_EVENT_FULL);
        check_equal(documents.open_calls, (size_t)1u);
        check_equal(events.count, (size_t)0u);
        check_true(vxml_dialog_manager_get_stats(&manager, &stats));
        check_equal(stats.event_pending, (size_t)1u);

        events.full = false;
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);
        check_equal(documents.open_calls, (size_t)1u);
        check_equal(events.count, (size_t)2u);
        check_equal(events.rows[0].name, "dialog.started");
        check_equal(events.rows[1].name, "dialog.exit");

        manager_close_destroy(&manager, &upstream);
    }

    it("publishes error.dialog.start and releases the row when document acquisition fails") {
        static const char source[] = "mem:voice";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-error";
        upstream_probe upstream = {0};
        document_probe documents = {
            .status = VXML_DIALOG_MANAGER_DOCUMENT_ERROR};
        event_probe events = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        const ccxml_dialog_start_request request = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;
        vxml_dialog_manager_stats stats = {0};

        check_equal(
            manager_init(
                &manager, 1u, &upstream, &documents, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);

        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);
        check_equal(processed, (size_t)1u);
        check_equal(documents.open_calls, (size_t)1u);
        check_equal(documents.close_calls, (size_t)0u);
        check_equal(events.count, (size_t)1u);
        check_equal(events.rows[0].name, "error.dialog.start");
        check_equal(events.rows[0].voice_status, VXML_INVALID_STATE);
        check_true(vxml_dialog_manager_get_stats(&manager, &stats));
        check_equal(stats.active, (size_t)0u);

        manager_close_destroy(&manager, &upstream);
    }

    it("reserves fixed capacity and discard makes the row reusable") {
        static const char source[] = "mem:voice";
        static const char media[] = "application/voicexml+xml";
        upstream_probe upstream = {0};
        document_probe documents = {
            .status = VXML_DIALOG_MANAGER_OK};
        event_probe events = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        const ccxml_dialog_prepare_request request = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u};
        ccxml_string_view first_id = {0};
        ccxml_string_view second_id = {0};
        char first_id_copy[96] = {0};
        size_t first_id_copy_size = 0u;
        cflow_statechart_effect_ticket first = {0};
        cflow_statechart_effect_ticket second = {0};

        check_equal(
            manager_init(
                &manager, 1u, &upstream, &documents, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(
            adapter->prepare_dialog_prepare(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &first_id, &first, NULL),
            SCXML_ADAPTER_ACCEPTED);
        check_equal(
            adapter->prepare_dialog_prepare(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &second_id, &second, NULL),
            SCXML_ADAPTER_FULL);
        check_true(first_id.size < sizeof(first_id_copy));
        first_id_copy_size = first_id.size;
        memcpy(first_id_copy, first_id.data, first_id.size);
        first_id_copy[first_id.size] = '\0';
        first.discard(first.user);
        check_equal(
            adapter->prepare_dialog_prepare(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &second_id, &second, NULL),
            SCXML_ADAPTER_ACCEPTED);
        check_true(
            first_id_copy_size != second_id.size ||
            memcmp(first_id_copy, second_id.data, second_id.size) != 0);
        second.discard(second.user);

        manager_close_destroy(&manager, &upstream);
    }

    it("raises hangup before destroying a running dialog and surfaces handler failure") {
        static const char source[] = "dialogs/hangup.vxml";
        static const char absolute[] =
            "https://voice.example/app/dialogs/hangup.vxml";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-hangup";
        upstream_probe upstream = {0};
        document_probe documents = {
            .status = VXML_DIALOG_MANAGER_OK,
            .expected_source = absolute,
            .expected_source_size = sizeof(absolute) - 1u};
        event_probe events = {0};
        fake_profile_probe profile_probe = {
            .raise_status = VXML_SEMANTIC_ERROR};
        vxml_document_store store = {0};
        vxml_document_ref preload = {0};
        vxml_document_view view = {0};
        vxml_document_store_error store_error = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request start = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_dialog_terminate_request terminate = {0};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;

        check_equal(store_init(&store, &documents, 1u),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(vxml_document_store_acquire(
                        &store, absolute, sizeof(absolute) - 1u,
                        &preload, &store_error),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(vxml_document_store_view(
                        &store, preload, &view),
                    VXML_DOCUMENT_STORE_OK);
        check_not_null(view.program);
        {
            vxml_program_impl *impl =
                (vxml_program_impl *)view.program->impl;
            check_not_null(impl);
            impl->profile_session_init = fake_profile_init;
            impl->profile_session_start = fake_profile_start;
            impl->profile_session_raise_event = fake_profile_raise;
            impl->profile_session_destroy = fake_profile_destroy;
        }
        check_equal(vxml_document_store_release(&store, &preload),
                    VXML_DOCUMENT_STORE_OK);

        active_fake_profile = &profile_probe;
        check_equal(manager_init_v2(
                        &manager, 1u, &upstream, &store, &events),
                    VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(adapter->prepare_dialog_start(
                        vxml_dialog_manager_ccxml_user(&manager),
                        &start, &dialog_id, &ticket, NULL),
                    SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(vxml_dialog_manager_run_ready(
                        &manager, 1u, &processed),
                    VXML_DIALOG_MANAGER_OK);
        check_equal(events.count, (size_t)1u);
        check_equal(events.rows[0].name, "dialog.started");
        check_true(profile_probe.init_sequence != 0u);
        check_true(profile_probe.start_sequence >
                   profile_probe.init_sequence);
        check_equal(profile_probe.raise_calls, (size_t)0u);
        check_equal(profile_probe.destroy_calls, (size_t)0u);

        terminate.dialog_id = dialog_id.data;
        terminate.dialog_id_size = dialog_id.size;
        terminate.immediate = false;
        ticket = (cflow_statechart_effect_ticket){0};
        check_equal(adapter->prepare_dialog_terminate(
                        vxml_dialog_manager_ccxml_user(&manager),
                        &terminate, &ticket, NULL),
                    SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(vxml_dialog_manager_run_ready(
                        &manager, 1u, &processed),
                    VXML_DIALOG_MANAGER_OK);
        check_equal(profile_probe.raise_calls, (size_t)1u);
        check_equal(profile_probe.event,
                    "connection.disconnect.hangup");
        check_equal(profile_probe.destroy_calls, (size_t)1u);
        check_true(profile_probe.raise_sequence <
                   profile_probe.destroy_sequence);
        check_equal(events.count, (size_t)2u);
        check_equal(events.rows[1].name, "dialog.exit");
        check_equal(events.rows[1].voice_status,
                    VXML_SEMANTIC_ERROR);

        active_fake_profile = NULL;
        manager_close_destroy(&manager, &upstream);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("terminates a prepared dialog without refetching its source") {
        static const char source[] = "mem:voice";
        static const char media[] = "application/voicexml+xml";
        upstream_probe upstream = {0};
        document_probe documents = {
            .status = VXML_DIALOG_MANAGER_OK};
        event_probe events = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        const ccxml_dialog_prepare_request prepare = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u};
        ccxml_dialog_terminate_request terminate = {0};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;

        check_equal(
            manager_init(
                &manager, 1u, &upstream, &documents, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(
            adapter->prepare_dialog_prepare(
                vxml_dialog_manager_ccxml_user(&manager),
                &prepare, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);
        check_equal(events.count, (size_t)1u);

        terminate.dialog_id = dialog_id.data;
        terminate.dialog_id_size = dialog_id.size;
        terminate.immediate = false;
        ticket = (cflow_statechart_effect_ticket){0};
        check_equal(
            adapter->prepare_dialog_terminate(
                vxml_dialog_manager_ccxml_user(&manager),
                &terminate, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);
        check_equal(documents.open_calls, (size_t)1u);
        check_equal(events.count, (size_t)2u);
        check_equal(events.rows[1].name, "dialog.exit");

        manager_close_destroy(&manager, &upstream);
    }

    it("forwards upstream non-dialog refusal without manufacturing a ticket") {
        upstream_probe upstream = {.reject_accept = true};
        document_probe documents = {
            .status = VXML_DIALOG_MANAGER_OK};
        event_probe events = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        const ccxml_accept_request accept = {
            .connection_id = "call-refused",
            .connection_id_size = sizeof("call-refused") - 1u};
        cflow_statechart_effect_ticket ticket = {0};
        const char *error = NULL;

        check_equal(
            manager_init(
                &manager, 1u, &upstream, &documents, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(
            adapter->prepare_accept(
                vxml_dialog_manager_ccxml_user(&manager),
                &accept, &ticket, &error),
            SCXML_ADAPTER_ERROR_EXECUTION);
        check_equal(upstream.accept_calls, (size_t)1u);
        check_null(ticket.commit);
        check_null(ticket.discard);
        check_not_null(error);

        manager_close_destroy(&manager, &upstream);
    }

    it("forwards non-dialog telephony and composes close quiescence exactly once") {
        upstream_probe upstream = {0};
        document_probe documents = {
            .status = VXML_DIALOG_MANAGER_OK};
        event_probe events = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        const ccxml_accept_request accept = {
            .connection_id = "call-1",
            .connection_id_size = sizeof("call-1") - 1u};
        cflow_statechart_effect_ticket ticket = {0};

        check_equal(
            manager_init(
                &manager, 1u, &upstream, &documents, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(
            adapter->prepare_accept(
                vxml_dialog_manager_ccxml_user(&manager),
                &accept, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        check_equal(upstream.accept_calls, (size_t)1u);
        ticket.commit(ticket.user);

        adapter->close(vxml_dialog_manager_ccxml_user(&manager));
        adapter->close(vxml_dialog_manager_ccxml_user(&manager));
        check_equal(upstream.close_calls, (size_t)1u);
        check_false(adapter->is_quiescent(
            vxml_dialog_manager_ccxml_user(&manager)));
        check_equal(
            vxml_dialog_manager_destroy(&manager),
            VXML_DIALOG_MANAGER_BUSY);

        upstream.quiescent = true;
        check_true(adapter->is_quiescent(
            vxml_dialog_manager_ccxml_user(&manager)));
        check_equal(
            vxml_dialog_manager_destroy(&manager),
            VXML_DIALOG_MANAGER_OK);
    }

    it("V2 direct start reuses one cached immutable program across sequential dialogs") {
        static const char expected_source[] =
            "https://voice.example/app/dialogs/main.vxml";
        static const char source[] = "dialogs/main.vxml";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-v2";
        upstream_probe upstream = {0};
        document_probe documents = {
            .status = VXML_DIALOG_MANAGER_OK,
            .expected_source = expected_source,
            .expected_source_size = sizeof(expected_source) - 1u};
        event_probe events = {0};
        vxml_document_store store = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request request = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;
        vxml_document_store_stats stats = {0};

        check_equal(store_init(&store, &documents, 2u),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(manager_init_v2(
                        &manager, 2u, &upstream, &store, &events),
                    VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();

        check_equal(adapter->prepare_dialog_start(
                        vxml_dialog_manager_ccxml_user(&manager),
                        &request, &dialog_id, &ticket, NULL),
                    SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(vxml_dialog_manager_run_ready(
                        &manager, 1u, &processed),
                    VXML_DIALOG_MANAGER_OK);
        check_equal(documents.open_calls, (size_t)1u);
        check_equal(documents.close_calls, (size_t)1u);
        check_equal(events.count, (size_t)2u);
        check_equal(events.rows[0].name, "dialog.started");
        check_equal(events.rows[1].name, "dialog.exit");

        ticket = (cflow_statechart_effect_ticket){0};
        dialog_id = (ccxml_string_view){0};
        check_equal(adapter->prepare_dialog_start(
                        vxml_dialog_manager_ccxml_user(&manager),
                        &request, &dialog_id, &ticket, NULL),
                    SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(vxml_dialog_manager_run_ready(
                        &manager, 1u, &processed),
                    VXML_DIALOG_MANAGER_OK);
        check_equal(documents.open_calls, (size_t)1u);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.hits, UINT64_C(1));
        check_equal(stats.misses, UINT64_C(1));
        check_equal(stats.entries, (size_t)1u);
        check_equal(stats.active_borrows, (size_t)0u);

        manager_close_destroy(&manager, &upstream);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("V2 prepared dialog pins its cached program until prepared start exits") {
        static const char expected_source[] =
            "https://voice.example/app/dialogs/prepared.vxml";
        static const char source[] = "dialogs/prepared.vxml";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-prepared-v2";
        upstream_probe upstream = {0};
        document_probe documents = {
            .status = VXML_DIALOG_MANAGER_OK,
            .expected_source = expected_source,
            .expected_source_size = sizeof(expected_source) - 1u};
        event_probe events = {0};
        vxml_document_store store = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_prepare_request prepare = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u};
        ccxml_prepared_dialog_start_request start = {0};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;
        vxml_document_store_stats stats = {0};

        check_equal(store_init(&store, &documents, 2u),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(manager_init_v2(
                        &manager, 1u, &upstream, &store, &events),
                    VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();

        check_equal(adapter->prepare_dialog_prepare(
                        vxml_dialog_manager_ccxml_user(&manager),
                        &prepare, &dialog_id, &ticket, NULL),
                    SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(vxml_dialog_manager_run_ready(
                        &manager, 1u, &processed),
                    VXML_DIALOG_MANAGER_OK);
        check_equal(events.count, (size_t)1u);
        check_equal(events.rows[0].name, "dialog.prepared");
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.active_borrows, (size_t)1u);
        check_equal(vxml_document_store_clear(&store),
                    VXML_DOCUMENT_STORE_BUSY);

        start.dialog_id = dialog_id.data;
        start.dialog_id_size = dialog_id.size;
        start.connection_id = connection;
        start.connection_id_size = sizeof(connection) - 1u;
        ticket = (cflow_statechart_effect_ticket){0};
        check_equal(adapter->prepare_prepared_dialog_start(
                        vxml_dialog_manager_ccxml_user(&manager),
                        &start, &ticket, NULL),
                    SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(vxml_dialog_manager_run_ready(
                        &manager, 1u, &processed),
                    VXML_DIALOG_MANAGER_OK);
        check_equal(events.count, (size_t)3u);
        check_equal(events.rows[1].name, "dialog.started");
        check_equal(events.rows[2].name, "dialog.exit");
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.active_borrows, (size_t)0u);
        check_equal(vxml_document_store_clear(&store),
                    VXML_DOCUMENT_STORE_OK);

        manager_close_destroy(&manager, &upstream);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("V2 rejects a fragment source instead of silently entering the wrong form") {
        static const char source[] = "dialogs/main.vxml#alternate";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-fragment";
        upstream_probe upstream = {0};
        document_probe documents = {.status = VXML_DIALOG_MANAGER_OK};
        event_probe events = {0};
        vxml_document_store store = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request request = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;
        vxml_document_store_stats stats = {0};

        check_equal(store_init(&store, &documents, 1u),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(manager_init_v2(
                        &manager, 1u, &upstream, &store, &events),
                    VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(adapter->prepare_dialog_start(
                        vxml_dialog_manager_ccxml_user(&manager),
                        &request, &dialog_id, &ticket, NULL),
                    SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(vxml_dialog_manager_run_ready(
                        &manager, 1u, &processed),
                    VXML_DIALOG_MANAGER_OK);
        check_equal(documents.open_calls, (size_t)0u);
        check_equal(events.count, (size_t)1u);
        check_equal(events.rows[0].name, "error.dialog.start");
        check_equal(events.rows[0].voice_status, VXML_UNSUPPORTED_FEATURE);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.entries, (size_t)0u);
        check_equal(stats.misses, UINT64_C(0));

        manager_close_destroy(&manager, &upstream);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }

    it("V2 manager close releases a prepared DocumentStore borrow") {
        static const char expected_source[] =
            "https://voice.example/app/dialogs/held.vxml";
        static const char source[] = "dialogs/held.vxml";
        static const char media[] = "application/voicexml+xml";
        upstream_probe upstream = {0};
        document_probe documents = {
            .status = VXML_DIALOG_MANAGER_OK,
            .expected_source = expected_source,
            .expected_source_size = sizeof(expected_source) - 1u};
        event_probe events = {0};
        vxml_document_store store = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_prepare_request prepare = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;
        vxml_document_store_stats stats = {0};

        check_equal(store_init(&store, &documents, 1u),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(manager_init_v2(
                        &manager, 1u, &upstream, &store, &events),
                    VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(adapter->prepare_dialog_prepare(
                        vxml_dialog_manager_ccxml_user(&manager),
                        &prepare, &dialog_id, &ticket, NULL),
                    SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(vxml_dialog_manager_run_ready(
                        &manager, 1u, &processed),
                    VXML_DIALOG_MANAGER_OK);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.active_borrows, (size_t)1u);

        vxml_dialog_manager_close(&manager);
        upstream.quiescent = true;
        check_true(vxml_dialog_manager_is_quiescent(&manager));
        check_equal(vxml_dialog_manager_destroy(&manager),
                    VXML_DIALOG_MANAGER_OK);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.active_borrows, (size_t)0u);
        check_equal(vxml_document_store_clear(&store),
                    VXML_DOCUMENT_STORE_OK);
        check_equal(vxml_document_store_destroy(&store),
                    VXML_DOCUMENT_STORE_OK);
    }


    it("V2 keeps external goto fail-closed without fetching the target document") {
        static const char a_uri[] =
            "https://voice.example/app/dialogs/a.vxml";
        static const char b_uri[] =
            "https://voice.example/app/dialogs/b.vxml";
        static const char a_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='main'><block>"
            "<goto next='b.vxml'/>"
            "</block></form></vxml>";
        static const char b_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='main'><block><exit/></block></form></vxml>";
        static const navigation_document_entry entries[] = {
            {a_uri, a_body, VXML_DIALOG_MANAGER_OK},
            {b_uri, b_body, VXML_DIALOG_MANAGER_OK}};
        static const char source[] = "dialogs/a.vxml";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-v2-external";
        navigation_document_probe documents = {
            .entries = entries,
            .entry_count = 2u};
        upstream_probe upstream = {0};
        event_probe events = {0};
        vxml_document_store store = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request request = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;
        vxml_document_store_stats stats = {0};

        check_equal(
            navigation_store_init(&store, &documents, 2u),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            manager_init_v2(
                &manager, 1u, &upstream, &store, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);
        check_equal(documents.open_calls, (size_t)1u);
        check_equal(events.count, (size_t)1u);
        check_equal(events.rows[0].name, "error.dialog.start");
        check_equal(
            events.rows[0].voice_status,
            VXML_UNSUPPORTED_FEATURE);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.entries, (size_t)1u);
        check_equal(stats.active_borrows, (size_t)0u);

        manager_close_destroy(&manager, &upstream);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("V3 resolves goto fetchaudio and skips it when target documents are cached") {
        static const char a_uri[] =
            "https://voice.example/app/dialogs/a.vxml";
        static const char b_uri[] =
            "https://voice.example/app/dialogs/b.vxml";
        static const char wait_audio_uri[] =
            "https://voice.example/app/media/wait.wav";
        static const char a_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='main'><block>"
            "<goto next='b.vxml#target' "
            "fetchaudio='../media/wait.wav'/>"
            "</block></form></vxml>";
        static const char b_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='entry'><block>"
            "<goto next='should-not-open.vxml'/>"
            "</block></form>"
            "<form id='target'><block><exit/></block></form>"
            "</vxml>";
        static const navigation_document_entry entries[] = {
            {a_uri, a_body, VXML_DIALOG_MANAGER_OK},
            {b_uri, b_body, VXML_DIALOG_MANAGER_OK}};
        static const char source[] = "dialogs/a.vxml";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-nav";
        navigation_document_probe documents = {
            .entries = entries,
            .entry_count = sizeof(entries) / sizeof(entries[0])};
        upstream_probe upstream = {0};
        event_probe events = {0};
        vxml_document_store store = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request request = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;
        vxml_document_store_stats stats = {0};

        check_equal(
            navigation_store_init(&store, &documents, 3u),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            manager_init_v3(
                &manager, 1u, 4u, &upstream, &store, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();

        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);
        check_equal(documents.open_calls, (size_t)2u);
        check_equal(documents.close_calls, (size_t)2u);
        check_equal(
            documents.fetch_audio_begin_calls, (size_t)1u);
        check_equal(
            documents.fetch_audio_finish_calls, (size_t)1u);
        check_equal(
            documents.fetch_audio_uri_size,
            sizeof(wait_audio_uri) - 1u);
        check_equal(
            documents.fetch_audio_uri,
            wait_audio_uri);
        check_equal(events.count, (size_t)2u);
        check_equal(events.rows[0].name, "dialog.started");
        check_equal(events.rows[1].name, "dialog.exit");

        ticket = (cflow_statechart_effect_ticket){0};
        dialog_id = (ccxml_string_view){0};
        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);
        check_equal(documents.open_calls, (size_t)2u);
        check_equal(
            documents.fetch_audio_begin_calls, (size_t)1u);
        check_equal(
            documents.fetch_audio_finish_calls, (size_t)1u);
        check_equal(events.count, (size_t)4u);
        check_equal(events.rows[2].name, "dialog.started");
        check_equal(events.rows[3].name, "dialog.exit");
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.hits, UINT64_C(2));
        check_equal(stats.misses, UINT64_C(2));
        check_equal(stats.active_borrows, (size_t)0u);

        manager_close_destroy(&manager, &upstream);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("V4 projects submit timeout and brackets one attempt with Store-owned fetchaudio") {
        static const char absolute[] =
            "https://voice.example/app/dialogs/submit-policy.vxml";
        static const char source[] =
            "dialogs/submit-policy.vxml";
        static const char initial_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><exit/></block></form></vxml>";
        static const char response_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><exit/></block></form></vxml>";
        static const navigation_document_entry entries[] = {
            {absolute, initial_body, VXML_DIALOG_MANAGER_OK}};
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-submit-policy";
        navigation_document_probe documents = {
            .entries = entries,
            .entry_count = 1u};
        fake_profile_probe profile_probe = {
            .submit_on_start = true,
            .submit_uri = "../submit",
            .submit_uri_size = sizeof("../submit") - 1u,
            .submit_has_timeout = true,
            .submit_timeout_us = UINT64_C(3500000),
            .submit_fetchaudio_uri = "../media/wait.wav",
            .submit_fetchaudio_uri_size =
                sizeof("../media/wait.wav") - 1u};
        submit_probe submit = {
            .status = VXML_SUBMIT_RESOURCE_OK,
            .response_body = response_body,
            .effective_uri =
                "https://voice.example/app/result/response.vxml",
            .ordering_documents = &documents};
        upstream_probe upstream = {0};
        event_probe events = {0};
        vxml_document_store store = {0};
        vxml_document_ref preload = {0};
        vxml_document_view view = {0};
        vxml_document_store_error store_error = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request start = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;

        check_equal(
            navigation_store_init(
                &store, &documents, 2u),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_acquire(
                &store, absolute, sizeof(absolute) - 1u,
                &preload, &store_error),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_view(
                &store, preload, &view),
            VXML_DOCUMENT_STORE_OK);
        check_not_null(view.program);
        if (view.program != NULL) {
            vxml_program_impl *impl =
                (vxml_program_impl *)view.program->impl;
            check_not_null(impl);
            impl->profile_session_init = fake_profile_init;
            impl->profile_session_start = fake_profile_start;
            impl->profile_session_destroy = fake_profile_destroy;
        }
        check_equal(
            vxml_document_store_release(
                &store, &preload),
            VXML_DOCUMENT_STORE_OK);

        active_fake_profile = &profile_probe;
        check_equal(
            manager_init_v4(
                &manager, 1u, 4u, &upstream, &store,
                &submit, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &start, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);

        check_equal(submit.execute_calls, (size_t)1u);
        check_true(submit.has_timeout);
        check_equal(
            submit.timeout_us, UINT64_C(3500000));
        check_equal(
            documents.fetch_audio_begin_calls, (size_t)1u);
        check_equal(
            documents.fetch_audio_finish_calls, (size_t)1u);
        check_equal(
            documents.fetch_audio_uri,
            "https://voice.example/app/media/wait.wav");
        check_true(
            documents.fetch_audio_begin_sequence <
            submit.execute_sequence);
        check_true(
            submit.execute_sequence <
            documents.fetch_audio_finish_sequence);
        check_equal(submit.close_calls, (size_t)1u);
        check_equal(events.count, (size_t)2u);
        check_equal(events.rows[0].name, "dialog.started");
        check_equal(events.rows[1].name, "dialog.exit");

        active_fake_profile = NULL;
        manager_close_destroy(&manager, &upstream);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("V4 rejects unsupported submit timeout before fetchaudio or provider admission") {
        static const char absolute[] =
            "https://voice.example/app/dialogs/submit-policy-unsupported.vxml";
        static const char source[] =
            "dialogs/submit-policy-unsupported.vxml";
        static const char initial_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><exit/></block></form></vxml>";
        static const navigation_document_entry entries[] = {
            {absolute, initial_body, VXML_DIALOG_MANAGER_OK}};
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-submit-policy-unsupported";
        navigation_document_probe documents = {
            .entries = entries,
            .entry_count = 1u};
        fake_profile_probe profile_probe = {
            .submit_on_start = true,
            .submit_uri = "../submit",
            .submit_uri_size = sizeof("../submit") - 1u,
            .submit_has_timeout = true,
            .submit_timeout_us = UINT64_C(3500000),
            .submit_fetchaudio_uri = "../media/wait.wav",
            .submit_fetchaudio_uri_size =
                sizeof("../media/wait.wav") - 1u};
        submit_probe submit = {
            .status = VXML_SUBMIT_RESOURCE_OK};
        vxml_submit_resource_adapter_v1 no_timeout =
            submit_adapter;
        upstream_probe upstream = {0};
        event_probe events = {0};
        vxml_document_store store = {0};
        vxml_document_ref preload = {0};
        vxml_document_view view = {0};
        vxml_document_store_error store_error = {0};
        vxml_dialog_manager manager = {0};
        vxml_dialog_manager_config_v4 config =
            vxml_dialog_manager_default_config_v4();
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request start = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;

        no_timeout.capabilities = 0u;

        check_equal(
            navigation_store_init(
                &store, &documents, 2u),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_acquire(
                &store, absolute, sizeof(absolute) - 1u,
                &preload, &store_error),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_view(
                &store, preload, &view),
            VXML_DOCUMENT_STORE_OK);
        check_not_null(view.program);
        if (view.program != NULL) {
            vxml_program_impl *impl =
                (vxml_program_impl *)view.program->impl;
            check_not_null(impl);
            impl->profile_session_init = fake_profile_init;
            impl->profile_session_start = fake_profile_start;
            impl->profile_session_destroy = fake_profile_destroy;
        }
        check_equal(
            vxml_document_store_release(
                &store, &preload),
            VXML_DOCUMENT_STORE_OK);

        config.capacity = 1u;
        config.max_source_bytes = 256u;
        config.max_navigation_hops = 4u;
        config.max_submit_response_bytes = 2048u;
        config.upstream = &upstream_adapter;
        config.upstream_user = &upstream;
        config.document_store = &store;
        config.submit = &no_timeout;
        config.submit_user = &submit;
        config.events = &event_sink;
        config.event_user = &events;

        active_fake_profile = &profile_probe;
        check_equal(
            vxml_dialog_manager_init_v4(
                &manager, &config),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &start, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);

        check_equal(submit.execute_calls, (size_t)0u);
        check_equal(
            documents.fetch_audio_begin_calls, (size_t)0u);
        check_equal(
            documents.fetch_audio_finish_calls, (size_t)0u);
        check_equal(events.count, (size_t)1u);
        check_equal(events.rows[0].name, "error.dialog.start");
        check_equal(
            events.rows[0].voice_status,
            VXML_UNSUPPORTED_FEATURE);

        active_fake_profile = NULL;
        manager_close_destroy(&manager, &upstream);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("V4 forwards generic submit V2 fields in order without profile coupling") {
        static const char source[] = "dialogs/submit-fields.vxml";
        static const char absolute[] =
            "https://voice.example/app/dialogs/submit-fields.vxml";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-submit-fields";
        static const char response_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><exit/></block></form></vxml>";
        static const vxml_submit_field_v1 fields[] = {
            {"alpha", sizeof("alpha") - 1u,
             "one", sizeof("one") - 1u},
            {"beta", sizeof("beta") - 1u,
             "two", sizeof("two") - 1u}};
        upstream_probe upstream = {0};
        document_probe documents = {
            .status = VXML_DIALOG_MANAGER_OK,
            .expected_source = absolute,
            .expected_source_size = sizeof(absolute) - 1u};
        event_probe events = {0};
        fake_profile_probe profile_probe = {
            .submit_on_start = true,
            .submit_uri = "../submit",
            .submit_uri_size = sizeof("../submit") - 1u,
            .submit_fields = fields,
            .submit_field_count =
                sizeof(fields) / sizeof(fields[0])};
        submit_probe submit = {
            .status = VXML_SUBMIT_RESOURCE_OK,
            .response_body = response_body,
            .effective_uri =
                "https://voice.example/app/result/response.vxml"};
        vxml_document_store store = {0};
        vxml_document_ref preload = {0};
        vxml_document_view view = {0};
        vxml_document_store_error store_error = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request start = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;

        check_equal(
            store_init(&store, &documents, 1u),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_acquire(
                &store, absolute, sizeof(absolute) - 1u,
                &preload, &store_error),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_view(
                &store, preload, &view),
            VXML_DOCUMENT_STORE_OK);
        check_not_null(view.program);
        if (view.program != NULL) {
            vxml_program_impl *impl =
                (vxml_program_impl *)view.program->impl;
            check_not_null(impl);
            impl->profile_session_init = fake_profile_init;
            impl->profile_session_start = fake_profile_start;
            impl->profile_session_destroy = fake_profile_destroy;
        }
        check_equal(
            vxml_document_store_release(&store, &preload),
            VXML_DOCUMENT_STORE_OK);

        active_fake_profile = &profile_probe;
        check_equal(
            manager_init_v4(
                &manager, 1u, 4u, &upstream, &store,
                &submit, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &start, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);

        check_equal(submit.execute_calls, (size_t)1u);
        check_equal(submit.close_calls, (size_t)1u);
        check_equal(submit.method, VXML_SUBMIT_METHOD_POST);
        check_equal(
            strcmp(
                submit.uri,
                "https://voice.example/app/submit"), 0);
        check_equal(
            strcmp(
                submit.content_type,
                "application/x-www-form-urlencoded"), 0);
        check_equal(
            strcmp(submit.body, "alpha=one&beta=two"), 0);
        check_equal(events.count, (size_t)2u);
        check_equal(events.rows[0].name, "dialog.started");
        check_equal(events.rows[1].name, "dialog.exit");

        active_fake_profile = NULL;
        manager_close_destroy(&manager, &upstream);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("V4 forwards ordered scalar and recording multipart views through one segmented attempt") {
        static const char source[] =
            "dialogs/submit-multipart.vxml";
        static const char absolute[] =
            "https://voice.example/app/dialogs/submit-multipart.vxml";
        static const char media[] =
            "application/voicexml+xml";
        static const char connection[] =
            "call-submit-multipart";
        static const char response_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><exit/></block></form></vxml>";
        static const char recording_bytes[] = "REC";
        static const vxml_submit_field_v1 fields[] = {
            {"alpha", sizeof("alpha") - 1u,
             "one", sizeof("one") - 1u},
            {"beta", sizeof("beta") - 1u,
             "two", sizeof("two") - 1u}};
        static const vxml_submit_recording_field_v1 recordings[] = {
            {
                "voice", sizeof("voice") - 1u,
                "voice.wav", sizeof("voice.wav") - 1u,
                "audio/wav", sizeof("audio/wav") - 1u,
                recording_bytes, sizeof(recording_bytes) - 1u
            }};
        static const vxml_submit_multipart_part_ref_v1 parts[] = {
            {VXML_SUBMIT_MULTIPART_PART_TEXT, 0u},
            {VXML_SUBMIT_MULTIPART_PART_RECORDING, 0u},
            {VXML_SUBMIT_MULTIPART_PART_TEXT, 1u}};
        document_probe documents = {
            .status = VXML_DIALOG_MANAGER_OK,
            .expected_source = absolute,
            .expected_source_size = sizeof(absolute) - 1u};
        event_probe events = {0};
        fake_profile_probe profile_probe = {
            .submit_on_start = true,
            .submit_uri = "../submit",
            .submit_uri_size = sizeof("../submit") - 1u,
            .submit_fields = fields,
            .submit_field_count =
                sizeof(fields) / sizeof(fields[0]),
            .submit_enctype =
                VXML_SUBMIT_ENCTYPE_MULTIPART_FORM_DATA,
            .submit_recordings = recordings,
            .submit_recording_count = 1u,
            .submit_parts = parts,
            .submit_part_count =
                sizeof(parts) / sizeof(parts[0])};
        submit_probe submit = {
            .status = VXML_SUBMIT_RESOURCE_OK,
            .response_body = response_body,
            .effective_uri =
                "https://voice.example/app/result/multipart-response.vxml",
            .expected_borrowed = recording_bytes};
        upstream_probe upstream = {0};
        vxml_document_store store = {0};
        vxml_document_ref preload = {0};
        vxml_document_view view = {0};
        vxml_document_store_error store_error = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request start = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size =
                sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;
        const char *alpha;
        const char *voice;
        const char *beta;

        check_equal(
            store_init(&store, &documents, 1u),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_acquire(
                &store, absolute, sizeof(absolute) - 1u,
                &preload, &store_error),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_view(
                &store, preload, &view),
            VXML_DOCUMENT_STORE_OK);
        check_not_null(view.program);
        if (view.program != NULL) {
            vxml_program_impl *impl =
                (vxml_program_impl *)view.program->impl;
            check_not_null(impl);
            impl->profile_session_init =
                fake_profile_init;
            impl->profile_session_start =
                fake_profile_start;
            impl->profile_session_destroy =
                fake_profile_destroy;
        }
        check_equal(
            vxml_document_store_release(
                &store, &preload),
            VXML_DOCUMENT_STORE_OK);

        active_fake_profile = &profile_probe;
        check_equal(
            manager_init_v4(
                &manager, 1u, 4u, &upstream, &store,
                &submit, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &start, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);

        check_equal(submit.execute_calls, (size_t)0u);
        check_equal(submit.execute_v2_calls, (size_t)1u);
        check_true(submit.saw_borrowed);
        check_true(
            strncmp(
                submit.content_type,
                "multipart/form-data; boundary=",
                sizeof("multipart/form-data; boundary=") - 1u) == 0);
        alpha = strstr(submit.body, "name=\"alpha\"");
        voice = strstr(submit.body, "name=\"voice\"");
        beta = strstr(submit.body, "name=\"beta\"");
        check_not_null(alpha);
        check_not_null(voice);
        check_not_null(beta);
        check_true(alpha < voice);
        check_true(voice < beta);
        check_not_null(strstr(submit.body, "REC"));
        check_equal(submit.close_calls, (size_t)1u);
        check_equal(events.count, (size_t)2u);
        check_equal(events.rows[0].name, "dialog.started");
        check_equal(events.rows[1].name, "dialog.exit");

        active_fake_profile = NULL;
        manager_close_destroy(&manager, &upstream);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("pairs one Store compiler with one Session factory across initial and submit-response Programs") {
        static const char a_uri[] =
            "https://voice.example/app/dialogs/profile.vxml";
        static const char a_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='entry'><block><exit/></block></form></vxml>";
        static const char submit_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='response'><block><exit/></block></form></vxml>";
        static const navigation_document_entry entries[] = {
            {a_uri, a_body, VXML_DIALOG_MANAGER_OK}};
        static const char source[] =
            "dialogs/profile.vxml#entry";
        static const char media[] =
            "application/voicexml+xml";
        static const char connection[] =
            "call-profile-pair";
        navigation_document_probe documents = {
            .entries = entries,
            .entry_count = 1u};
        profile_pair_probe profile = {
            .last_form_index = SIZE_MAX};
        submit_probe submit = {
            .status = VXML_SUBMIT_RESOURCE_OK,
            .response_body = submit_body,
            .effective_uri =
                "https://voice.example/app/result/profile-response.vxml"};
        upstream_probe upstream = {0};
        event_probe events = {0};
        vxml_document_store store = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request request = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size =
                sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;

        check_equal(
            profile_pair_store_init(
                &store, &documents, &profile, 2u),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            manager_init_v4_profile(
                &manager, 1u, 4u, &upstream, &store,
                &submit, &events, &profile),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();

        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);

        check_equal(profile.compile_calls, (size_t)2u);
        check_equal(profile.factory_calls, (size_t)2u);
        check_equal(profile.start_calls, (size_t)2u);
        check_equal(profile.start_at_calls, (size_t)1u);
        check_equal(profile.last_form_index, (size_t)0u);
        check_equal(profile.destroy_calls, (size_t)2u);
        check_equal(documents.open_calls, (size_t)1u);
        check_equal(documents.close_calls, (size_t)1u);
        check_equal(submit.execute_calls, (size_t)1u);
        check_equal(submit.close_calls, (size_t)1u);
        check_equal(events.count, (size_t)2u);
        check_equal(events.rows[0].name, "dialog.started");
        check_equal(events.rows[1].name, "dialog.exit");

        manager_close_destroy(&manager, &upstream);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("accepts the historical V4 config prefix without a Session factory") {
        static const char a_uri[] =
            "https://voice.example/app/dialogs/prefix.vxml";
        static const char a_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><exit/></block></form></vxml>";
        static const navigation_document_entry entries[] = {
            {a_uri, a_body, VXML_DIALOG_MANAGER_OK}};
        navigation_document_probe documents = {
            .entries = entries,
            .entry_count = 1u};
        submit_probe submit = {
            .status = VXML_SUBMIT_RESOURCE_OK};
        upstream_probe upstream = {0};
        event_probe events = {0};
        vxml_document_store store = {0};
        vxml_dialog_manager manager = {0};
        vxml_dialog_manager_config_v4 config =
            vxml_dialog_manager_default_config_v4();

        check_equal(
            navigation_store_init(
                &store, &documents, 1u),
            VXML_DOCUMENT_STORE_OK);
        config.struct_size =
            offsetof(
                vxml_dialog_manager_config_v4,
                event_user) +
            sizeof(config.event_user);
        config.capacity = 1u;
        config.max_source_bytes = 256u;
        config.max_navigation_hops = 4u;
        config.max_submit_response_bytes = 2048u;
        config.upstream = &upstream_adapter;
        config.upstream_user = &upstream;
        config.document_store = &store;
        config.submit = &submit_adapter;
        config.submit_user = &submit;
        config.events = &event_sink;
        config.event_user = &events;
        check_equal(
            vxml_dialog_manager_init_v4(
                &manager, &config),
            VXML_DIALOG_MANAGER_OK);

        manager_close_destroy(&manager, &upstream);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("V4 executes POST submit once, compiles its response directly, and continues from effective URI") {
        static const char a_uri[] =
            "https://voice.example/app/dialogs/a.vxml";
        static const char next_uri[] =
            "https://voice.example/app/result/next.vxml";
        static const char a_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='main'><block>"
            "<submit next='../submit' method='post'/>"
            "</block></form></vxml>";
        static const char submit_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='response'><block>"
            "<goto next='next.vxml'/>"
            "</block></form></vxml>";
        static const char next_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='done'><block><exit/></block></form>"
            "</vxml>";
        static const navigation_document_entry entries[] = {
            {a_uri, a_body, VXML_DIALOG_MANAGER_OK},
            {next_uri, next_body, VXML_DIALOG_MANAGER_OK}};
        static const char source[] = "dialogs/a.vxml";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-submit";
        navigation_document_probe documents = {
            .entries = entries,
            .entry_count = 2u};
        submit_probe submit = {
            .status = VXML_SUBMIT_RESOURCE_OK,
            .response_body = submit_body,
            .effective_uri =
                "https://voice.example/app/result/response.vxml"};
        upstream_probe upstream = {0};
        event_probe events = {0};
        vxml_document_store store = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request request = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;

        check_equal(
            navigation_store_init(&store, &documents, 3u),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            manager_init_v4(
                &manager, 1u, 4u, &upstream, &store,
                &submit, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();

        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);

        check_equal(submit.execute_calls, (size_t)1u);
        check_equal(submit.close_calls, (size_t)1u);
        check_equal(submit.method, VXML_SUBMIT_METHOD_POST);
        check_equal(
            strcmp(
                submit.uri,
                "https://voice.example/app/submit"), 0);
        check_equal(
            strcmp(
                submit.content_type,
                "application/x-www-form-urlencoded"), 0);
        check_equal(strcmp(submit.body, ""), 0);
        check_equal(documents.open_calls, (size_t)2u);
        check_equal(documents.close_calls, (size_t)2u);
        check_equal(events.count, (size_t)2u);
        check_equal(events.rows[0].name, "dialog.started");
        check_equal(events.rows[1].name, "dialog.exit");

        manager_close_destroy(&manager, &upstream);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("V4 counts submit and goto against one shared navigation-hop budget") {
        static const char a_uri[] =
            "https://voice.example/app/dialogs/a.vxml";
        static const char a_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><submit next='../submit'/></block></form>"
            "</vxml>";
        static const char submit_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><goto next='next.vxml'/></block></form>"
            "</vxml>";
        static const navigation_document_entry entries[] = {
            {a_uri, a_body, VXML_DIALOG_MANAGER_OK}};
        static const char source[] = "dialogs/a.vxml";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-submit-hop";
        navigation_document_probe documents = {
            .entries = entries,
            .entry_count = 1u};
        submit_probe submit = {
            .status = VXML_SUBMIT_RESOURCE_OK,
            .response_body = submit_body,
            .effective_uri =
                "https://voice.example/app/result/response.vxml"};
        upstream_probe upstream = {0};
        event_probe events = {0};
        vxml_document_store store = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request request = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;

        check_equal(
            navigation_store_init(&store, &documents, 2u),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            manager_init_v4(
                &manager, 1u, 1u, &upstream, &store,
                &submit, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();

        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);

        check_equal(submit.execute_calls, (size_t)1u);
        check_equal(submit.close_calls, (size_t)1u);
        check_equal(documents.open_calls, (size_t)1u);
        check_equal(events.count, (size_t)1u);
        check_equal(events.rows[0].name, "error.dialog.start");
        check_equal(
            events.rows[0].voice_status,
            VXML_LIMIT_EXCEEDED);

        manager_close_destroy(&manager, &upstream);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("V4 closes a malformed submit response exactly once without retry") {
        static const char a_uri[] =
            "https://voice.example/app/dialogs/a.vxml";
        static const char a_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form><block><submit next='../submit' method='post'/>"
            "</block></form></vxml>";
        static const navigation_document_entry entries[] = {
            {a_uri, a_body, VXML_DIALOG_MANAGER_OK}};
        static const char source[] = "dialogs/a.vxml";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-submit-bad";
        navigation_document_probe documents = {
            .entries = entries,
            .entry_count = 1u};
        submit_probe submit = {
            .status = VXML_SUBMIT_RESOURCE_OK,
            .response_body = "<vxml",
            .effective_uri =
                "https://voice.example/app/result/bad.vxml"};
        upstream_probe upstream = {0};
        event_probe events = {0};
        vxml_document_store store = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request request = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;

        check_equal(
            navigation_store_init(&store, &documents, 2u),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            manager_init_v4(
                &manager, 1u, 4u, &upstream, &store,
                &submit, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();

        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);

        check_equal(submit.execute_calls, (size_t)1u);
        check_equal(submit.close_calls, (size_t)1u);
        check_equal(documents.open_calls, (size_t)1u);
        check_equal(events.count, (size_t)1u);
        check_equal(events.rows[0].name, "error.dialog.start");
        check_equal(events.rows[0].voice_status, VXML_XML_ERROR);

        manager_close_destroy(&manager, &upstream);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("V3 honors an initial source fragment before any external navigation") {
        static const char b_uri[] =
            "https://voice.example/app/dialogs/b.vxml";
        static const char b_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='entry'><block>"
            "<goto next='should-not-open.vxml'/>"
            "</block></form>"
            "<form id='target'><block><exit/></block></form>"
            "</vxml>";
        static const navigation_document_entry entries[] = {
            {b_uri, b_body, VXML_DIALOG_MANAGER_OK}};
        static const char source[] = "dialogs/b.vxml#target";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-initial-fragment";
        navigation_document_probe documents = {
            .entries = entries,
            .entry_count = 1u};
        upstream_probe upstream = {0};
        event_probe events = {0};
        vxml_document_store store = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request request = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;

        check_equal(
            navigation_store_init(&store, &documents, 2u),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            manager_init_v3(
                &manager, 1u, 4u, &upstream, &store, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);
        check_equal(documents.open_calls, (size_t)1u);
        check_equal(events.count, (size_t)2u);
        check_equal(events.rows[0].name, "dialog.started");
        check_equal(events.rows[1].name, "dialog.exit");

        manager_close_destroy(&manager, &upstream);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("V3 publishes error.dialog.start when an external target form is missing") {
        static const char a_uri[] =
            "https://voice.example/app/dialogs/a.vxml";
        static const char b_uri[] =
            "https://voice.example/app/dialogs/b.vxml";
        static const char a_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='main'><block>"
            "<goto next='b.vxml#missing'/>"
            "</block></form></vxml>";
        static const char b_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='target'><block><exit/></block></form>"
            "</vxml>";
        static const navigation_document_entry entries[] = {
            {a_uri, a_body, VXML_DIALOG_MANAGER_OK},
            {b_uri, b_body, VXML_DIALOG_MANAGER_OK}};
        static const char source[] = "dialogs/a.vxml";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-missing-form";
        navigation_document_probe documents = {
            .entries = entries,
            .entry_count = 2u};
        upstream_probe upstream = {0};
        event_probe events = {0};
        vxml_document_store store = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request request = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;
        vxml_document_store_stats stats = {0};

        check_equal(
            navigation_store_init(&store, &documents, 3u),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            manager_init_v3(
                &manager, 1u, 4u, &upstream, &store, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);
        check_equal(events.count, (size_t)1u);
        check_equal(events.rows[0].name, "error.dialog.start");
        check_equal(
            events.rows[0].voice_status,
            VXML_INVALID_STRUCTURE);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.active_borrows, (size_t)0u);

        manager_close_destroy(&manager, &upstream);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("V3 publishes error.dialog.start when a navigated document cannot be acquired") {
        static const char a_uri[] =
            "https://voice.example/app/dialogs/a.vxml";
        static const char b_uri[] =
            "https://voice.example/app/dialogs/b.vxml";
        static const char a_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='main'><block>"
            "<goto next='b.vxml'/>"
            "</block></form></vxml>";
        static const navigation_document_entry entries[] = {
            {a_uri, a_body, VXML_DIALOG_MANAGER_OK},
            {b_uri, NULL, VXML_DIALOG_MANAGER_DOCUMENT_ERROR}};
        static const char source[] = "dialogs/a.vxml";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-doc-error";
        navigation_document_probe documents = {
            .entries = entries,
            .entry_count = 2u};
        upstream_probe upstream = {0};
        event_probe events = {0};
        vxml_document_store store = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request request = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;
        vxml_document_store_stats stats = {0};

        check_equal(
            navigation_store_init(&store, &documents, 3u),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            manager_init_v3(
                &manager, 1u, 4u, &upstream, &store, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);
        check_equal(documents.open_calls, (size_t)2u);
        check_equal(events.count, (size_t)1u);
        check_equal(events.rows[0].name, "error.dialog.start");
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.active_borrows, (size_t)0u);

        manager_close_destroy(&manager, &upstream);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("V3 bounds an external A-B-A navigation cycle by max_navigation_hops") {
        static const char a_uri[] =
            "https://voice.example/app/dialogs/a.vxml";
        static const char b_uri[] =
            "https://voice.example/app/dialogs/b.vxml";
        static const char a_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='main'><block>"
            "<goto next='b.vxml'/>"
            "</block></form></vxml>";
        static const char b_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='main'><block>"
            "<goto next='a.vxml'/>"
            "</block></form></vxml>";
        static const navigation_document_entry entries[] = {
            {a_uri, a_body, VXML_DIALOG_MANAGER_OK},
            {b_uri, b_body, VXML_DIALOG_MANAGER_OK}};
        static const char source[] = "dialogs/a.vxml";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-cycle";
        navigation_document_probe documents = {
            .entries = entries,
            .entry_count = 2u};
        upstream_probe upstream = {0};
        event_probe events = {0};
        vxml_document_store store = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request request = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;
        vxml_document_store_stats stats = {0};

        check_equal(
            navigation_store_init(&store, &documents, 3u),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            manager_init_v3(
                &manager, 1u, 2u, &upstream, &store, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_OK);
        check_equal(documents.open_calls, (size_t)2u);
        check_equal(events.count, (size_t)1u);
        check_equal(events.rows[0].name, "error.dialog.start");
        check_equal(
            events.rows[0].voice_status,
            VXML_LIMIT_EXCEEDED);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_true(stats.hits >= UINT64_C(1));
        check_equal(stats.misses, UINT64_C(2));
        check_equal(stats.active_borrows, (size_t)0u);

        manager_close_destroy(&manager, &upstream);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

    it("V3 close releases the final navigated document while Event publication is backpressured") {
        static const char a_uri[] =
            "https://voice.example/app/dialogs/a.vxml";
        static const char b_uri[] =
            "https://voice.example/app/dialogs/b.vxml";
        static const char a_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='main'><block>"
            "<goto next='b.vxml'/>"
            "</block></form></vxml>";
        static const char b_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='main'><block><exit/></block></form></vxml>";
        static const navigation_document_entry entries[] = {
            {a_uri, a_body, VXML_DIALOG_MANAGER_OK},
            {b_uri, b_body, VXML_DIALOG_MANAGER_OK}};
        static const char source[] = "dialogs/a.vxml";
        static const char media[] = "application/voicexml+xml";
        static const char connection[] = "call-close-nav";
        navigation_document_probe documents = {
            .entries = entries,
            .entry_count = 2u};
        upstream_probe upstream = {0};
        event_probe events = {.full = true};
        vxml_document_store store = {0};
        vxml_dialog_manager manager = {0};
        const ccxml_telephony_adapter_v1 *adapter;
        ccxml_dialog_start_request request = {
            .source = source,
            .source_size = sizeof(source) - 1u,
            .media_type = media,
            .media_type_size = sizeof(media) - 1u,
            .connection_id = connection,
            .connection_id_size = sizeof(connection) - 1u};
        ccxml_string_view dialog_id = {0};
        cflow_statechart_effect_ticket ticket = {0};
        size_t processed = 0u;
        vxml_document_store_stats stats = {0};

        check_equal(
            navigation_store_init(&store, &documents, 3u),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            manager_init_v3(
                &manager, 1u, 4u, &upstream, &store, &events),
            VXML_DIALOG_MANAGER_OK);
        adapter = vxml_dialog_manager_ccxml_adapter();
        check_equal(
            adapter->prepare_dialog_start(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &dialog_id, &ticket, NULL),
            SCXML_ADAPTER_ACCEPTED);
        ticket.commit(ticket.user);
        check_equal(
            vxml_dialog_manager_run_ready(
                &manager, 1u, &processed),
            VXML_DIALOG_MANAGER_EVENT_FULL);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.active_borrows, (size_t)1u);

        vxml_dialog_manager_close(&manager);
        upstream.quiescent = true;
        check_true(vxml_dialog_manager_is_quiescent(&manager));
        check_equal(
            vxml_dialog_manager_destroy(&manager),
            VXML_DIALOG_MANAGER_OK);
        check_true(vxml_document_store_get_stats(&store, &stats));
        check_equal(stats.active_borrows, (size_t)0u);
        check_equal(
            vxml_document_store_clear(&store),
            VXML_DOCUMENT_STORE_OK);
        check_equal(
            vxml_document_store_destroy(&store),
            VXML_DOCUMENT_STORE_OK);
    }

}
