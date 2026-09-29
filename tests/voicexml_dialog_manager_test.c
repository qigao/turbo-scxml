#include <voicexml/dialog_manager.h>
#include <tinytest.h>

#include <string.h>

typedef struct upstream_probe {
    size_t accept_calls;
    size_t close_calls;
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
    static const char expected_source[] = "mem:voice";
    static const char expected_media[] = "application/voicexml+xml";
    if (probe == NULL || out_document == NULL ||
        source == NULL || media_type == NULL ||
        source_size != sizeof(expected_source) - 1u ||
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
        check_equal(stats.terminal_pending, (size_t)1u);

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
        first.discard(first.user);
        check_equal(
            adapter->prepare_dialog_prepare(
                vxml_dialog_manager_ccxml_user(&manager),
                &request, &second_id, &second, NULL),
            SCXML_ADAPTER_ACCEPTED);
        check_true(
            first_id.size != second_id.size ||
            memcmp(first_id.data, second_id.data, second_id.size) != 0);
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
}
