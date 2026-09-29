#include <voicexml/dialog_manager.h>
#include <voicexml/document_store.h>
#include <tinytest.h>

#include <string.h>

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
        .document_user = documents};
    config.voice_limits = vxml_default_limits();
    return vxml_document_store_init(store, &config);
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


    it("V3 follows relative external goto to a target fragment and reuses both cached programs") {
        static const char a_uri[] =
            "https://voice.example/app/dialogs/a.vxml";
        static const char b_uri[] =
            "https://voice.example/app/dialogs/b.vxml";
        static const char a_body[] =
            "<vxml xmlns='http://www.w3.org/2001/vxml' version='2.1'>"
            "<form id='main'><block>"
            "<goto next='b.vxml#target'/>"
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
