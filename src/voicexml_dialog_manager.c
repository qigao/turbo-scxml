#include <voicexml/dialog_manager.h>
#include <voicexml/document_store.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char VXML_MEDIA_TYPE[] = "application/voicexml+xml";
static const char EVENT_DIALOG_PREPARED[] = "dialog.prepared";
static const char EVENT_DIALOG_STARTED[] = "dialog.started";
static const char EVENT_DIALOG_EXIT[] = "dialog.exit";
static const char EVENT_ERROR_PREPARE[] = "error.dialog.prepare";
static const char EVENT_ERROR_START[] = "error.dialog.start";
static const char EVENT_ERROR_TERMINATE[] = "error.dialog.terminate";
static const char EVENT_CONNECTION_DISCONNECT_HANGUP[] =
    "connection.disconnect.hangup";

typedef enum vxml_dialog_row_state {
    VXML_DIALOG_ROW_EMPTY = 0,
    VXML_DIALOG_ROW_RESERVED_PREPARE,
    VXML_DIALOG_ROW_PENDING_PREPARE,
    VXML_DIALOG_ROW_PREPARED,
    VXML_DIALOG_ROW_RESERVED_DIRECT_START,
    VXML_DIALOG_ROW_PENDING_DIRECT_START,
    VXML_DIALOG_ROW_RESERVED_PREPARED_START,
    VXML_DIALOG_ROW_PENDING_PREPARED_START,
    VXML_DIALOG_ROW_RUNNING,
    VXML_DIALOG_ROW_RESERVED_TERMINATE,
    VXML_DIALOG_ROW_PENDING_TERMINATE,
    VXML_DIALOG_ROW_EVENT_PENDING
} vxml_dialog_row_state;

typedef enum vxml_dialog_ticket_kind {
    VXML_DIALOG_TICKET_NONE = 0,
    VXML_DIALOG_TICKET_PREPARE,
    VXML_DIALOG_TICKET_DIRECT_START,
    VXML_DIALOG_TICKET_PREPARED_START,
    VXML_DIALOG_TICKET_TERMINATE
} vxml_dialog_ticket_kind;

typedef enum vxml_dialog_event_kind {
    VXML_DIALOG_EVENT_NONE = 0,
    VXML_DIALOG_EVENT_PREPARED,
    VXML_DIALOG_EVENT_STARTED,
    VXML_DIALOG_EVENT_EXIT,
    VXML_DIALOG_EVENT_ERROR_PREPARE,
    VXML_DIALOG_EVENT_ERROR_START,
    VXML_DIALOG_EVENT_ERROR_TERMINATE
} vxml_dialog_event_kind;

typedef struct vxml_dialog_manager_impl vxml_dialog_manager_impl;

typedef struct vxml_dialog_row {
    vxml_dialog_manager_impl *owner;
    size_t slot;
    uint32_t generation;
    vxml_dialog_row_state state;
    vxml_dialog_row_state restore_state;
    vxml_dialog_event_kind event_kind;
    vxml_dialog_event_kind restore_event_kind;
    vxml_dialog_ticket_kind ticket_kind;
    bool ticket_live;

    char *dialog_id;
    size_t dialog_id_size;
    char *source;
    size_t source_size;
    char *media_type;
    size_t media_type_size;
    char *connection_id;
    size_t connection_id_size;
    char *start_fragment;
    size_t start_fragment_size;
    char *current_document_uri;
    size_t current_document_uri_size;
    size_t navigation_hops;

    vxml_program program;
    bool program_live;
    vxml_document_ref document_ref;
    bool document_ref_live;
    const vxml_program *program_view;
    vxml_session session;
    bool session_live;
    vxml_status voice_status;
} vxml_dialog_row;

struct vxml_dialog_manager_impl {
    size_t capacity;
    size_t max_source_bytes;
    size_t max_media_type_bytes;
    size_t max_connection_id_bytes;
    size_t max_dialog_id_bytes;
    size_t max_document_bytes;
    vxml_limits voice_limits;

    ccxml_telephony_adapter_v1 upstream;
    void *upstream_user;
    vxml_dialog_document_adapter_v1 documents;
    void *document_user;
    vxml_document_store *document_store;
    bool store_backed;
    bool navigation_enabled;
    bool submit_enabled;
    vxml_submit_resource_adapter_v1 submit;
    void *submit_user;
    size_t max_submit_response_bytes;
    size_t max_navigation_hops;
    char *resolve_uri_scratch;
    char *resolve_fragment_scratch;
    char *resolve_fetchaudio_uri_scratch;
    char *resolve_fetchaudio_fragment_scratch;
    vxml_dialog_event_sink_v1 events;
    void *event_user;
    vxml_session_factory_v1 session_factory;
    void *session_factory_user;
    bool session_factory_enabled;

    vxml_dialog_row *rows;
    bool closed;
    bool upstream_close_called;

    uint64_t accepted_operations;
    uint64_t discarded_operations;
    uint64_t rejected_full;
    uint64_t stale_operations;
};

static uint32_t next_generation(uint32_t generation) {
    ++generation;
    return generation != 0u ? generation : 1u;
}

static size_t min_size(size_t left, size_t right) {
    return left < right ? left : right;
}

static bool bytes_valid(const char *data, size_t size) {
    return size != 0u && data != NULL &&
           memchr(data, '\0', size) == NULL;
}

static bool media_type_valid(const char *data, size_t size) {
    return data != NULL &&
           size == sizeof(VXML_MEDIA_TYPE) - 1u &&
           memcmp(data, VXML_MEDIA_TYPE, size) == 0;
}

static bool session_factory_prefix_valid(
    const vxml_session_factory_v1 *factory) {
    const size_t prefix =
        offsetof(vxml_session_factory_v1, init) +
        sizeof(factory->init);
    return factory != NULL &&
        factory->abi_version == VXML_SESSION_FACTORY_ABI_V1 &&
        factory->struct_size >= prefix &&
        factory->init != NULL;
}

static bool submit_prefix_valid(
    const vxml_submit_resource_adapter_v1 *adapter) {
    const size_t required =
        offsetof(vxml_submit_resource_adapter_v1, close) +
        sizeof(adapter->close);
    return adapter != NULL &&
           adapter->abi_version ==
               VXML_SUBMIT_RESOURCE_ADAPTER_ABI_V1 &&
           adapter->struct_size >= required &&
           adapter->execute != NULL &&
           adapter->close != NULL;
}

static bool upstream_prefix_valid(
    const ccxml_telephony_adapter_v1 *adapter) {
    const size_t required =
        offsetof(ccxml_telephony_adapter_v1, is_quiescent) +
        sizeof(adapter->is_quiescent);
    return adapter != NULL &&
           adapter->abi_version == CCXML_TELEPHONY_ADAPTER_ABI_V1 &&
           adapter->struct_size >= required &&
           adapter->prepare_accept != NULL &&
           adapter->close != NULL &&
           adapter->is_quiescent != NULL;
}

static void row_destroy_runtime(vxml_dialog_row *row) {
    if (row == NULL) return;
    if (row->session_live) {
        (void)vxml_session_close(&row->session);
        vxml_session_destroy(&row->session);
        row->session_live = false;
    }
    if (row->program_live) {
        vxml_program_destroy(&row->program);
        row->program_live = false;
    }
    if (row->document_ref_live && row->owner != NULL &&
        row->owner->document_store != NULL) {
        (void)vxml_document_store_release(
            row->owner->document_store, &row->document_ref);
        row->document_ref_live = false;
    }
    row->program_view = NULL;
    row->voice_status = VXML_OK;
}

static void row_clear(vxml_dialog_row *row) {
    uint32_t generation;
    vxml_dialog_manager_impl *owner;
    size_t slot;
    char *dialog_id;
    char *source;
    char *media_type;
    char *connection_id;
    char *start_fragment;
    char *current_document_uri;
    if (row == NULL) return;
    row_destroy_runtime(row);
    generation = row->generation;
    owner = row->owner;
    slot = row->slot;
    dialog_id = row->dialog_id;
    source = row->source;
    media_type = row->media_type;
    connection_id = row->connection_id;
    start_fragment = row->start_fragment;
    current_document_uri = row->current_document_uri;
    memset(row, 0, sizeof(*row));
    row->generation = generation;
    row->owner = owner;
    row->slot = slot;
    row->dialog_id = dialog_id;
    row->source = source;
    row->media_type = media_type;
    row->connection_id = connection_id;
    row->start_fragment = start_fragment;
    row->current_document_uri = current_document_uri;
    row->state = VXML_DIALOG_ROW_EMPTY;
}

static bool row_allocate_buffers(
    vxml_dialog_row *row,
    size_t source_bytes,
    size_t media_type_bytes,
    size_t connection_bytes,
    size_t dialog_id_bytes) {
    row->source = (char *)calloc(source_bytes + 1u, 1u);
    row->media_type = (char *)calloc(media_type_bytes + 1u, 1u);
    row->connection_id = (char *)calloc(connection_bytes + 1u, 1u);
    row->dialog_id = (char *)calloc(dialog_id_bytes + 1u, 1u);
    row->start_fragment = (char *)calloc(source_bytes + 1u, 1u);
    row->current_document_uri =
        (char *)calloc(source_bytes + 1u, 1u);
    return row->source != NULL && row->media_type != NULL &&
           row->connection_id != NULL && row->dialog_id != NULL &&
           row->start_fragment != NULL &&
           row->current_document_uri != NULL;
}

static void row_free_buffers(vxml_dialog_row *row) {
    if (row == NULL) return;
    free(row->source);
    free(row->media_type);
    free(row->connection_id);
    free(row->dialog_id);
    free(row->start_fragment);
    free(row->current_document_uri);
    row->source = NULL;
    row->media_type = NULL;
    row->connection_id = NULL;
    row->dialog_id = NULL;
    row->start_fragment = NULL;
    row->current_document_uri = NULL;
}

static bool row_copy(
    char *destination, size_t capacity,
    const char *source, size_t source_size,
    size_t *out_size) {
    if (destination == NULL || out_size == NULL ||
        source_size > capacity ||
        (source_size != 0u && source == NULL))
        return false;
    if (source_size != 0u)
        memcpy(destination, source, source_size);
    destination[source_size] = '\0';
    *out_size = source_size;
    return true;
}

static bool row_make_id(vxml_dialog_row *row) {
    int size;
    if (row == NULL || row->owner == NULL ||
        row->dialog_id == NULL)
        return false;
    size = snprintf(
        row->dialog_id, row->owner->max_dialog_id_bytes + 1u,
        "vxml-%zu-%u", row->slot + 1u, row->generation);
    if (size <= 0 ||
        (size_t)size > row->owner->max_dialog_id_bytes)
        return false;
    row->dialog_id_size = (size_t)size;
    return true;
}

static vxml_dialog_row *find_empty_row(
    vxml_dialog_manager_impl *impl) {
    size_t index;
    if (impl == NULL) return NULL;
    for (index = 0u; index < impl->capacity; ++index)
        if (impl->rows[index].state == VXML_DIALOG_ROW_EMPTY)
            return &impl->rows[index];
    ++impl->rejected_full;
    return NULL;
}

static vxml_dialog_row *find_dialog(
    vxml_dialog_manager_impl *impl,
    const char *dialog_id, size_t dialog_id_size) {
    size_t index;
    if (impl == NULL || !bytes_valid(dialog_id, dialog_id_size))
        return NULL;
    for (index = 0u; index < impl->capacity; ++index) {
        vxml_dialog_row *row = &impl->rows[index];
        if (row->state != VXML_DIALOG_ROW_EMPTY &&
            row->dialog_id_size == dialog_id_size &&
            memcmp(row->dialog_id, dialog_id, dialog_id_size) == 0)
            return row;
    }
    return NULL;
}

static void ticket_commit(void *user) {
    vxml_dialog_row *row = (vxml_dialog_row *)user;
    vxml_dialog_manager_impl *impl =
        row != NULL ? row->owner : NULL;
    if (row == NULL || impl == NULL || !row->ticket_live) {
        if (impl != NULL) ++impl->stale_operations;
        return;
    }
    row->ticket_live = false;
    ++impl->accepted_operations;
    switch (row->ticket_kind) {
    case VXML_DIALOG_TICKET_PREPARE:
        if (row->state == VXML_DIALOG_ROW_RESERVED_PREPARE)
            row->state = VXML_DIALOG_ROW_PENDING_PREPARE;
        else
            ++impl->stale_operations;
        break;
    case VXML_DIALOG_TICKET_DIRECT_START:
        if (row->state == VXML_DIALOG_ROW_RESERVED_DIRECT_START)
            row->state = VXML_DIALOG_ROW_PENDING_DIRECT_START;
        else
            ++impl->stale_operations;
        break;
    case VXML_DIALOG_TICKET_PREPARED_START:
        if (row->state == VXML_DIALOG_ROW_RESERVED_PREPARED_START)
            row->state = VXML_DIALOG_ROW_PENDING_PREPARED_START;
        else
            ++impl->stale_operations;
        break;
    case VXML_DIALOG_TICKET_TERMINATE:
        if (row->state == VXML_DIALOG_ROW_RESERVED_TERMINATE)
            row->state = VXML_DIALOG_ROW_PENDING_TERMINATE;
        else
            ++impl->stale_operations;
        break;
    default:
        ++impl->stale_operations;
        break;
    }
    row->ticket_kind = VXML_DIALOG_TICKET_NONE;
}

static void ticket_discard(void *user) {
    vxml_dialog_row *row = (vxml_dialog_row *)user;
    vxml_dialog_manager_impl *impl =
        row != NULL ? row->owner : NULL;
    if (row == NULL || impl == NULL || !row->ticket_live) {
        if (impl != NULL) ++impl->stale_operations;
        return;
    }
    row->ticket_live = false;
    ++impl->discarded_operations;
    switch (row->ticket_kind) {
    case VXML_DIALOG_TICKET_PREPARE:
    case VXML_DIALOG_TICKET_DIRECT_START:
        row_clear(row);
        break;
    case VXML_DIALOG_TICKET_PREPARED_START:
    case VXML_DIALOG_TICKET_TERMINATE:
        row->state = row->restore_state;
        row->event_kind = row->restore_event_kind;
        break;
    default:
        ++impl->stale_operations;
        break;
    }
    row->ticket_kind = VXML_DIALOG_TICKET_NONE;
}

static cflow_statechart_effect_ticket row_ticket(vxml_dialog_row *row) {
    return (cflow_statechart_effect_ticket){
        .commit = ticket_commit,
        .discard = ticket_discard,
        .user = row};
}

static scxml_adapter_status prepare_new_dialog(
    vxml_dialog_manager_impl *impl,
    vxml_dialog_ticket_kind kind,
    const char *source, size_t source_size,
    const char *media_type, size_t media_type_size,
    const char *connection_id, size_t connection_id_size,
    ccxml_string_view *out_dialog_id,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    vxml_dialog_row *row;
    if (out_error != NULL) *out_error = NULL;
    if (impl == NULL || out_dialog_id == NULL || out_ticket == NULL ||
        impl->closed)
        return impl != NULL && impl->closed
            ? SCXML_ADAPTER_CLOSED : SCXML_ADAPTER_INVALID_CONTRACT;
    if (!bytes_valid(source, source_size) ||
        source_size > impl->max_source_bytes ||
        !media_type_valid(media_type, media_type_size) ||
        media_type_size > impl->max_media_type_bytes ||
        connection_id_size > impl->max_connection_id_bytes ||
        (connection_id_size != 0u &&
         !bytes_valid(connection_id, connection_id_size)))
        return SCXML_ADAPTER_INVALID_CONTRACT;

    row = find_empty_row(impl);
    if (row == NULL)
        return SCXML_ADAPTER_FULL;

    row->generation = next_generation(row->generation);
    if (!row_make_id(row) ||
        !row_copy(
            row->source, impl->max_source_bytes,
            source, source_size, &row->source_size) ||
        !row_copy(
            row->media_type, impl->max_media_type_bytes,
            media_type, media_type_size, &row->media_type_size) ||
        !row_copy(
            row->connection_id, impl->max_connection_id_bytes,
            connection_id, connection_id_size,
            &row->connection_id_size)) {
        row_clear(row);
        return SCXML_ADAPTER_INVALID_CONTRACT;
    }

    row->ticket_kind = kind;
    row->ticket_live = true;
    row->voice_status = VXML_OK;
    row->state = kind == VXML_DIALOG_TICKET_PREPARE
        ? VXML_DIALOG_ROW_RESERVED_PREPARE
        : VXML_DIALOG_ROW_RESERVED_DIRECT_START;
    *out_dialog_id = (ccxml_string_view){
        .data = row->dialog_id,
        .size = row->dialog_id_size};
    *out_ticket = row_ticket(row);
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status manager_prepare_dialog_prepare(
    void *user,
    const ccxml_dialog_prepare_request *request,
    ccxml_string_view *out_dialog_id,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    vxml_dialog_manager_impl *impl =
        (vxml_dialog_manager_impl *)user;
    if (request == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    return prepare_new_dialog(
        impl, VXML_DIALOG_TICKET_PREPARE,
        request->source, request->source_size,
        request->media_type, request->media_type_size,
        NULL, 0u, out_dialog_id, out_ticket, out_error);
}

static scxml_adapter_status manager_prepare_dialog_start(
    void *user,
    const ccxml_dialog_start_request *request,
    ccxml_string_view *out_dialog_id,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    vxml_dialog_manager_impl *impl =
        (vxml_dialog_manager_impl *)user;
    if (request == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    return prepare_new_dialog(
        impl, VXML_DIALOG_TICKET_DIRECT_START,
        request->source, request->source_size,
        request->media_type, request->media_type_size,
        request->connection_id, request->connection_id_size,
        out_dialog_id, out_ticket, out_error);
}

static scxml_adapter_status manager_prepare_prepared_dialog_start(
    void *user,
    const ccxml_prepared_dialog_start_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    vxml_dialog_manager_impl *impl =
        (vxml_dialog_manager_impl *)user;
    vxml_dialog_row *row;
    if (out_error != NULL) *out_error = NULL;
    if (impl == NULL || request == NULL || out_ticket == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    if (impl->closed)
        return SCXML_ADAPTER_CLOSED;
    row = find_dialog(
        impl, request->dialog_id, request->dialog_id_size);
    if (row == NULL)
        return SCXML_ADAPTER_ERROR_EXECUTION;
    if (row->state != VXML_DIALOG_ROW_PREPARED ||
        !bytes_valid(
            request->connection_id, request->connection_id_size) ||
        request->connection_id_size > impl->max_connection_id_bytes)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    if (!row_copy(
            row->connection_id, impl->max_connection_id_bytes,
            request->connection_id, request->connection_id_size,
            &row->connection_id_size))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    row->restore_state = row->state;
    row->restore_event_kind = row->event_kind;
    row->ticket_kind = VXML_DIALOG_TICKET_PREPARED_START;
    row->ticket_live = true;
    row->state = VXML_DIALOG_ROW_RESERVED_PREPARED_START;
    *out_ticket = row_ticket(row);
    return SCXML_ADAPTER_ACCEPTED;
}

static scxml_adapter_status manager_prepare_dialog_terminate(
    void *user,
    const ccxml_dialog_terminate_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    vxml_dialog_manager_impl *impl =
        (vxml_dialog_manager_impl *)user;
    vxml_dialog_row *row;
    if (out_error != NULL) *out_error = NULL;
    if (impl == NULL || request == NULL || out_ticket == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    if (impl->closed)
        return SCXML_ADAPTER_CLOSED;
    if (request->immediate)
        return SCXML_ADAPTER_ERROR_EXECUTION;
    row = find_dialog(
        impl, request->dialog_id, request->dialog_id_size);
    if (row == NULL)
        return SCXML_ADAPTER_ERROR_EXECUTION;
    if (row->ticket_live ||
        (row->state != VXML_DIALOG_ROW_PREPARED &&
         row->state != VXML_DIALOG_ROW_RUNNING &&
         row->state != VXML_DIALOG_ROW_EVENT_PENDING))
        return SCXML_ADAPTER_INVALID_CONTRACT;
    row->restore_state = row->state;
    row->restore_event_kind = row->event_kind;
    row->ticket_kind = VXML_DIALOG_TICKET_TERMINATE;
    row->ticket_live = true;
    row->state = VXML_DIALOG_ROW_RESERVED_TERMINATE;
    *out_ticket = row_ticket(row);
    return SCXML_ADAPTER_ACCEPTED;
}

#define FORWARD_PREPARE_1(name, request_type) \
    static scxml_adapter_status manager_##name( \
        void *user, const request_type *request, \
        cflow_statechart_effect_ticket *out_ticket, \
        const char **out_error) { \
        vxml_dialog_manager_impl *impl = (vxml_dialog_manager_impl *)user; \
        if (impl == NULL || impl->closed) \
            return impl != NULL && impl->closed \
                ? SCXML_ADAPTER_CLOSED : SCXML_ADAPTER_INVALID_CONTRACT; \
        if (impl->upstream.name == NULL) \
            return SCXML_ADAPTER_INVALID_CONTRACT; \
        return impl->upstream.name( \
            impl->upstream_user, request, out_ticket, out_error); \
    }

FORWARD_PREPARE_1(prepare_accept, ccxml_accept_request)
FORWARD_PREPARE_1(prepare_create_call, ccxml_create_call_request)
FORWARD_PREPARE_1(prepare_disconnect, ccxml_disconnect_request)
FORWARD_PREPARE_1(prepare_reject, ccxml_reject_request)
FORWARD_PREPARE_1(prepare_redirect, ccxml_redirect_request)
FORWARD_PREPARE_1(prepare_join, ccxml_join_request)
FORWARD_PREPARE_1(prepare_unjoin, ccxml_unjoin_request)
FORWARD_PREPARE_1(prepare_merge, ccxml_merge_request)
FORWARD_PREPARE_1(
    prepare_destroy_conference, ccxml_destroy_conference_request)

#undef FORWARD_PREPARE_1

static scxml_adapter_status manager_prepare_create_conference(
    void *user,
    const ccxml_create_conference_request *request,
    ccxml_string_view *out_conference_id,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    vxml_dialog_manager_impl *impl =
        (vxml_dialog_manager_impl *)user;
    if (impl == NULL || impl->closed)
        return impl != NULL && impl->closed
            ? SCXML_ADAPTER_CLOSED : SCXML_ADAPTER_INVALID_CONTRACT;
    if (impl->upstream.prepare_create_conference == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    return impl->upstream.prepare_create_conference(
        impl->upstream_user, request, out_conference_id,
        out_ticket, out_error);
}

static void manager_adapter_close(void *user) {
    vxml_dialog_manager wrapper = {user};
    vxml_dialog_manager_close(&wrapper);
}

static bool manager_adapter_is_quiescent(void *user) {
    const vxml_dialog_manager wrapper = {user};
    return vxml_dialog_manager_is_quiescent(&wrapper);
}

static const ccxml_telephony_adapter_v1 MANAGER_ADAPTER = {
    .abi_version = CCXML_TELEPHONY_ADAPTER_ABI_V1,
    .struct_size = sizeof(ccxml_telephony_adapter_v1),
    .prepare_accept = manager_prepare_accept,
    .close = manager_adapter_close,
    .is_quiescent = manager_adapter_is_quiescent,
    .prepare_create_call = manager_prepare_create_call,
    .prepare_disconnect = manager_prepare_disconnect,
    .prepare_reject = manager_prepare_reject,
    .prepare_redirect = manager_prepare_redirect,
    .prepare_join = manager_prepare_join,
    .prepare_unjoin = manager_prepare_unjoin,
    .prepare_merge = manager_prepare_merge,
    .prepare_create_conference = manager_prepare_create_conference,
    .prepare_destroy_conference = manager_prepare_destroy_conference,
    .prepare_dialog_start = manager_prepare_dialog_start,
    .prepare_dialog_terminate = manager_prepare_dialog_terminate,
    .prepare_dialog_prepare = manager_prepare_dialog_prepare,
    .prepare_prepared_dialog_start =
        manager_prepare_prepared_dialog_start};

static const char *event_name(vxml_dialog_event_kind kind) {
    switch (kind) {
    case VXML_DIALOG_EVENT_PREPARED:
        return EVENT_DIALOG_PREPARED;
    case VXML_DIALOG_EVENT_STARTED:
        return EVENT_DIALOG_STARTED;
    case VXML_DIALOG_EVENT_EXIT:
        return EVENT_DIALOG_EXIT;
    case VXML_DIALOG_EVENT_ERROR_PREPARE:
        return EVENT_ERROR_PREPARE;
    case VXML_DIALOG_EVENT_ERROR_START:
        return EVENT_ERROR_START;
    case VXML_DIALOG_EVENT_ERROR_TERMINATE:
        return EVENT_ERROR_TERMINATE;
    default:
        return NULL;
    }
}

static vxml_dialog_manager_status queue_event(
    vxml_dialog_row *row,
    vxml_dialog_event_kind kind,
    vxml_status voice_status) {
    if (row == NULL || kind == VXML_DIALOG_EVENT_NONE)
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;
    row->event_kind = kind;
    row->voice_status = voice_status;
    row->state = VXML_DIALOG_ROW_EVENT_PENDING;
    return VXML_DIALOG_MANAGER_OK;
}

static vxml_dialog_manager_status publish_event(
    vxml_dialog_row *row) {
    vxml_dialog_manager_impl *impl;
    const char *name;
    vxml_dialog_event_v1 event;
    vxml_dialog_event_sink_status status;
    if (row == NULL || row->owner == NULL ||
        row->state != VXML_DIALOG_ROW_EVENT_PENDING)
        return VXML_DIALOG_MANAGER_INVALID_STATE;
    impl = row->owner;
    name = event_name(row->event_kind);
    if (name == NULL)
        return VXML_DIALOG_MANAGER_INVALID_STATE;
    event = (vxml_dialog_event_v1){
        .abi_version = 1u,
        .struct_size = sizeof(vxml_dialog_event_v1),
        .name = name,
        .name_size = strlen(name),
        .dialog_id = row->dialog_id,
        .dialog_id_size = row->dialog_id_size,
        .connection_id = row->connection_id,
        .connection_id_size = row->connection_id_size,
        .voice_status = row->voice_status};
    status = impl->events.try_publish(impl->event_user, &event);
    if (status == VXML_DIALOG_EVENT_FULL)
        return VXML_DIALOG_MANAGER_EVENT_FULL;
    if (status != VXML_DIALOG_EVENT_ACCEPTED)
        return VXML_DIALOG_MANAGER_EVENT_CLOSED;

    switch (row->event_kind) {
    case VXML_DIALOG_EVENT_PREPARED:
        row->event_kind = VXML_DIALOG_EVENT_NONE;
        row->state = VXML_DIALOG_ROW_PREPARED;
        break;
    case VXML_DIALOG_EVENT_STARTED:
        if (row->session_live &&
            vxml_session_get_state(&row->session) == VXML_SESSION_EXITED)
            (void)queue_event(row, VXML_DIALOG_EVENT_EXIT, VXML_OK);
        else {
            row->event_kind = VXML_DIALOG_EVENT_NONE;
            row->state = VXML_DIALOG_ROW_RUNNING;
        }
        break;
    case VXML_DIALOG_EVENT_EXIT:
    case VXML_DIALOG_EVENT_ERROR_PREPARE:
    case VXML_DIALOG_EVENT_ERROR_START:
    case VXML_DIALOG_EVENT_ERROR_TERMINATE:
        row_clear(row);
        break;
    default:
        return VXML_DIALOG_MANAGER_INVALID_STATE;
    }
    return VXML_DIALOG_MANAGER_OK;
}

static vxml_dialog_manager_status compile_document(
    vxml_dialog_row *row,
    vxml_dialog_event_kind failure_event) {
    vxml_dialog_manager_impl *impl;
    vxml_dialog_document document = {0};
    vxml_dialog_manager_status resource_status;
    vxml_diagnostic diagnostic = {0};
    vxml_status status;
    if (row == NULL || row->owner == NULL)
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;
    impl = row->owner;
    resource_status = impl->documents.open(
        impl->document_user,
        row->source, row->source_size,
        row->media_type, row->media_type_size,
        impl->max_document_bytes, &document);
    if (resource_status != VXML_DIALOG_MANAGER_OK) {
        (void)queue_event(row, failure_event, VXML_INVALID_STATE);
        return VXML_DIALOG_MANAGER_OK;
    }
    if (document.size > impl->max_document_bytes ||
        (document.size != 0u && document.data == NULL)) {
        impl->documents.close(impl->document_user, &document);
        (void)queue_event(row, failure_event, VXML_LIMIT_EXCEEDED);
        return VXML_DIALOG_MANAGER_OK;
    }

    status = vxml_compile(
        document.data, document.size,
        &impl->voice_limits, &row->program, &diagnostic);
    impl->documents.close(impl->document_user, &document);
    if (status != VXML_OK) {
        row->program = (vxml_program){0};
        (void)queue_event(row, failure_event, status);
        return VXML_DIALOG_MANAGER_OK;
    }
    row->program_live = true;
    row->program_view = &row->program;
    return VXML_DIALOG_MANAGER_OK;
}

static vxml_status store_failure_voice_status(
    vxml_document_store_status status,
    const vxml_document_store_error *error) {
    if (status == VXML_DOCUMENT_STORE_COMPILE_ERROR &&
        error != NULL && error->voice_status != VXML_OK)
        return error->voice_status;
    switch (status) {
    case VXML_DOCUMENT_STORE_LIMIT_EXCEEDED:
        return VXML_LIMIT_EXCEEDED;
    case VXML_DOCUMENT_STORE_ALLOCATION_FAILED:
        return VXML_ALLOCATION_FAILED;
    case VXML_DOCUMENT_STORE_INVALID_URI:
    case VXML_DOCUMENT_STORE_INVALID_ARGUMENT:
        return VXML_INVALID_ARGUMENT;
    default:
        return VXML_INVALID_STATE;
    }
}

static vxml_dialog_manager_status acquire_store_document(
    vxml_dialog_row *row,
    vxml_dialog_event_kind failure_event) {
    vxml_dialog_manager_impl *impl;
    vxml_resolved_uri_v1 resolved;
    vxml_document_store_status store_status;
    vxml_document_store_error error = {0};
    vxml_document_view view = {0};
    if (row == NULL || row->owner == NULL)
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;
    impl = row->owner;
    if (!impl->store_backed || impl->document_store == NULL ||
        impl->resolve_uri_scratch == NULL ||
        impl->resolve_fragment_scratch == NULL)
        return VXML_DIALOG_MANAGER_INVALID_STATE;

    resolved = (vxml_resolved_uri_v1){
        .abi_version = 1u,
        .struct_size = sizeof(vxml_resolved_uri_v1),
        .document_uri = impl->resolve_uri_scratch,
        .document_uri_capacity = impl->max_source_bytes + 1u,
        .fragment = impl->resolve_fragment_scratch,
        .fragment_capacity = impl->max_source_bytes + 1u};

    store_status = vxml_document_store_resolve(
        impl->document_store,
        NULL, 0u,
        row->source, row->source_size,
        &resolved);
    if (store_status != VXML_DOCUMENT_STORE_OK) {
        (void)queue_event(
            row, failure_event,
            store_failure_voice_status(store_status, NULL));
        return VXML_DIALOG_MANAGER_OK;
    }
    if (resolved.fragment_size != 0u) {
        if (!impl->navigation_enabled) {
            (void)queue_event(
                row, failure_event, VXML_UNSUPPORTED_FEATURE);
            return VXML_DIALOG_MANAGER_OK;
        }
        if (!row_copy(
                row->start_fragment, impl->max_source_bytes,
                resolved.fragment, resolved.fragment_size,
                &row->start_fragment_size)) {
            (void)queue_event(
                row, failure_event, VXML_LIMIT_EXCEEDED);
            return VXML_DIALOG_MANAGER_OK;
        }
    } else {
        row->start_fragment_size = 0u;
        row->start_fragment[0] = '\0';
    }

    store_status = vxml_document_store_acquire(
        impl->document_store,
        resolved.document_uri, resolved.document_uri_size,
        &row->document_ref, &error);
    if (store_status != VXML_DOCUMENT_STORE_OK) {
        row->document_ref = (vxml_document_ref){0};
        (void)queue_event(
            row, failure_event,
            store_failure_voice_status(store_status, &error));
        return VXML_DIALOG_MANAGER_OK;
    }
    row->document_ref_live = true;

    store_status = vxml_document_store_view(
        impl->document_store, row->document_ref, &view);
    if (store_status != VXML_DOCUMENT_STORE_OK ||
        view.program == NULL) {
        (void)vxml_document_store_release(
            impl->document_store, &row->document_ref);
        row->document_ref_live = false;
        row->document_ref = (vxml_document_ref){0};
        (void)queue_event(row, failure_event, VXML_INVALID_STATE);
        return VXML_DIALOG_MANAGER_OK;
    }
    if (!row_copy(
            row->current_document_uri,
            impl->max_source_bytes,
            resolved.document_uri,
            resolved.document_uri_size,
            &row->current_document_uri_size)) {
        (void)vxml_document_store_release(
            impl->document_store, &row->document_ref);
        row->document_ref_live = false;
        row->document_ref = (vxml_document_ref){0};
        (void)queue_event(
            row, failure_event, VXML_LIMIT_EXCEEDED);
        return VXML_DIALOG_MANAGER_OK;
    }
    row->program_view = view.program;
    return VXML_DIALOG_MANAGER_OK;
}

static vxml_dialog_manager_status load_document_program(
    vxml_dialog_row *row,
    vxml_dialog_event_kind failure_event) {
    if (row == NULL || row->owner == NULL)
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;
    return row->owner->store_backed
        ? acquire_store_document(row, failure_event)
        : compile_document(row, failure_event);
}

static vxml_status start_current_session(
    vxml_dialog_row *row,
    const char *fragment,
    size_t fragment_size) {
    vxml_dialog_manager_impl *impl;
    vxml_status status;
    if (row == NULL || row->owner == NULL ||
        row->program_view == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = row->owner;
    status = impl->session_factory_enabled
        ? impl->session_factory.init(
              impl->session_factory_user,
              &row->session, row->program_view)
        : vxml_session_init(
              &row->session, row->program_view);
    if (status != VXML_OK)
        return status;
    row->session_live = true;
    return fragment_size != 0u
        ? vxml_session_start_at_form(
              &row->session, fragment, fragment_size)
        : vxml_session_start(&row->session);
}

static vxml_status submit_failure_voice_status(
    vxml_submit_resource_status status) {
    switch (status) {
    case VXML_SUBMIT_RESOURCE_ALLOCATION_FAILED:
        return VXML_ALLOCATION_FAILED;
    case VXML_SUBMIT_RESOURCE_LIMIT_EXCEEDED:
        return VXML_LIMIT_EXCEEDED;
    case VXML_SUBMIT_RESOURCE_INVALID_URI:
    case VXML_SUBMIT_RESOURCE_INVALID_ARGUMENT:
        return VXML_INVALID_ARGUMENT;
    case VXML_SUBMIT_RESOURCE_UNSUPPORTED_ENCODING:
    case VXML_SUBMIT_RESOURCE_UNSUPPORTED_POLICY:
        return VXML_UNSUPPORTED_FEATURE;
    case VXML_SUBMIT_RESOURCE_PROVIDER_ERROR:
    case VXML_SUBMIT_RESOURCE_POSSIBLY_PROCESSED:
    case VXML_SUBMIT_RESOURCE_INVALID_RESPONSE:
        return VXML_INVALID_STATE;
    case VXML_SUBMIT_RESOURCE_OK:
        return VXML_OK;
    }
    return VXML_INVALID_STATE;
}

static vxml_dialog_manager_status follow_one_submit(
    vxml_dialog_row *row) {
    static const char urlencoded[] =
        "application/x-www-form-urlencoded";
    vxml_dialog_manager_impl *impl;
    vxml_submit_target_v3 submit = {0};
    vxml_submit_request_v1 request = VXML_SUBMIT_REQUEST_V1_INIT;
    vxml_submit_response response = {0};
    vxml_resolved_uri_v1 target_resolved;
    vxml_resolved_uri_v1 effective_resolved;
    const char *next_base = NULL;
    size_t next_base_size = 0u;
    const char *next_fragment = NULL;
    size_t next_fragment_size = 0u;
    vxml_submit_resource_status submit_status;
    vxml_document_store_status store_status;
    vxml_program next_program = {0};
    vxml_diagnostic diagnostic = {0};
    vxml_status voice_status;
    vxml_fetch_audio_ticket_v1 fetch_audio_ticket = {0};
    bool fetch_audio_started = false;

    if (row == NULL || row->owner == NULL)
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;
    impl = row->owner;
    if (!impl->submit_enabled ||
        !impl->store_backed ||
        impl->document_store == NULL ||
        row->current_document_uri == NULL ||
        row->current_document_uri_size == 0u)
        return queue_event(
            row, VXML_DIALOG_EVENT_ERROR_START,
            VXML_UNSUPPORTED_FEATURE);
    if (row->navigation_hops >= impl->max_navigation_hops)
        return queue_event(
            row, VXML_DIALOG_EVENT_ERROR_START,
            VXML_LIMIT_EXCEEDED);

    voice_status = vxml_session_submit_v3(
        &row->session, &submit);
    if (voice_status != VXML_OK)
        return queue_event(
            row, VXML_DIALOG_EVENT_ERROR_START,
            voice_status);
    if (submit.enctype != VXML_SUBMIT_ENCTYPE_URLENCODED)
        return queue_event(
            row, VXML_DIALOG_EVENT_ERROR_START,
            VXML_UNSUPPORTED_FEATURE);

    target_resolved = (vxml_resolved_uri_v1){
        .abi_version = 1u,
        .struct_size = sizeof(vxml_resolved_uri_v1),
        .document_uri = impl->resolve_uri_scratch,
        .document_uri_capacity = impl->max_source_bytes + 1u,
        .fragment = impl->resolve_fragment_scratch,
        .fragment_capacity = impl->max_source_bytes + 1u};
    store_status = vxml_document_store_resolve(
        impl->document_store,
        row->current_document_uri,
        row->current_document_uri_size,
        submit.uri, submit.uri_size,
        &target_resolved);
    if (store_status != VXML_DOCUMENT_STORE_OK)
        return queue_event(
            row, VXML_DIALOG_EVENT_ERROR_START,
            store_failure_voice_status(store_status, NULL));

    if (submit.fetchaudio_uri_size != 0u) {
        vxml_resolved_uri_v1 audio_resolved = {
            .abi_version = 1u,
            .struct_size = sizeof(vxml_resolved_uri_v1),
            .document_uri =
                impl->resolve_fetchaudio_uri_scratch,
            .document_uri_capacity =
                impl->max_source_bytes + 1u,
            .fragment =
                impl->resolve_fetchaudio_fragment_scratch,
            .fragment_capacity =
                impl->max_source_bytes + 1u};
        vxml_fetch_audio_request_v1 audio_request = {
            .abi_version = VXML_FETCH_AUDIO_REQUEST_ABI_V1,
            .struct_size = sizeof(vxml_fetch_audio_request_v1),
            .has_delay = submit.has_fetchaudio_delay,
            .delay_us = submit.fetchaudio_delay_us,
            .has_minimum =
                submit.has_fetchaudio_minimum,
            .minimum_us =
                submit.fetchaudio_minimum_us};
        vxml_fetch_audio_begin_result audio_result =
            VXML_FETCH_AUDIO_SKIPPED;
        store_status = vxml_document_store_resolve(
            impl->document_store,
            row->current_document_uri,
            row->current_document_uri_size,
            submit.fetchaudio_uri,
            submit.fetchaudio_uri_size,
            &audio_resolved);
        if (store_status != VXML_DOCUMENT_STORE_OK)
            return queue_event(
                row, VXML_DIALOG_EVENT_ERROR_START,
                store_failure_voice_status(
                    store_status, NULL));
        if (audio_resolved.fragment_size != 0u)
            return queue_event(
                row, VXML_DIALOG_EVENT_ERROR_START,
                VXML_INVALID_ARGUMENT);
        audio_request.uri =
            audio_resolved.document_uri;
        audio_request.uri_size =
            audio_resolved.document_uri_size;
        store_status =
            vxml_document_store_fetch_audio_begin(
                impl->document_store,
                &audio_request,
                &audio_result,
                &fetch_audio_ticket);
        if (store_status != VXML_DOCUMENT_STORE_OK)
            return queue_event(
                row, VXML_DIALOG_EVENT_ERROR_START,
                store_failure_voice_status(
                    store_status, NULL));
        fetch_audio_started =
            audio_result == VXML_FETCH_AUDIO_STARTED;
    }

    request.base_document_uri = row->current_document_uri;
    request.base_document_uri_size =
        row->current_document_uri_size;
    request.target = submit.uri;
    request.target_size = submit.uri_size;
    request.method = submit.method;
    request.fields = submit.fields;
    request.field_count = submit.field_count;
    if (submit.method == VXML_SUBMIT_METHOD_POST) {
        request.enctype = urlencoded;
        request.enctype_size = sizeof(urlencoded) - 1u;
    }
    request.max_uri_bytes = impl->max_source_bytes;
    request.max_body_bytes = impl->max_source_bytes;
    request.max_response_bytes =
        impl->max_submit_response_bytes;
    request.has_timeout = submit.has_timeout;
    request.timeout_us = submit.timeout_us;

    submit_status = vxml_submit_resource_execute(
        impl->document_store,
        &impl->submit, impl->submit_user,
        &request, &response);
    if (fetch_audio_started) {
        fetch_audio_ticket.finish(
            fetch_audio_ticket.user);
        fetch_audio_ticket =
            (vxml_fetch_audio_ticket_v1){0};
        fetch_audio_started = false;
    }
    if (submit_status != VXML_SUBMIT_RESOURCE_OK)
        return queue_event(
            row, VXML_DIALOG_EVENT_ERROR_START,
            submit_failure_voice_status(submit_status));

    next_base = target_resolved.document_uri;
    next_base_size = target_resolved.document_uri_size;
    next_fragment =
        target_resolved.fragment_size != 0u
            ? target_resolved.fragment : NULL;
    next_fragment_size = target_resolved.fragment_size;

    if (response.effective_uri_size != 0u) {
        effective_resolved = (vxml_resolved_uri_v1){
            .abi_version = 1u,
            .struct_size = sizeof(vxml_resolved_uri_v1),
            .document_uri =
                impl->resolve_fetchaudio_uri_scratch,
            .document_uri_capacity =
                impl->max_source_bytes + 1u,
            .fragment =
                impl->resolve_fetchaudio_fragment_scratch,
            .fragment_capacity =
                impl->max_source_bytes + 1u};
        store_status = vxml_document_store_resolve(
            impl->document_store,
            row->current_document_uri,
            row->current_document_uri_size,
            response.effective_uri,
            response.effective_uri_size,
            &effective_resolved);
        if (store_status != VXML_DOCUMENT_STORE_OK) {
            (void)vxml_submit_resource_close(
                &impl->submit, impl->submit_user,
                &response);
            return queue_event(
                row, VXML_DIALOG_EVENT_ERROR_START,
                store_failure_voice_status(store_status, NULL));
        }
        next_base = effective_resolved.document_uri;
        next_base_size = effective_resolved.document_uri_size;
        if (effective_resolved.fragment_size != 0u) {
            next_fragment = effective_resolved.fragment;
            next_fragment_size =
                effective_resolved.fragment_size;
        }
    }

    store_status = vxml_document_store_compile_source(
        impl->document_store,
        response.data, response.size,
        &impl->voice_limits,
        &next_program, &diagnostic);
    (void)vxml_submit_resource_close(
        &impl->submit, impl->submit_user, &response);
    if (store_status != VXML_DOCUMENT_STORE_OK) {
        voice_status =
            diagnostic.status != VXML_OK
                ? diagnostic.status
                : store_failure_voice_status(
                    store_status, NULL);
        vxml_program_destroy(&next_program);
        return queue_event(
            row, VXML_DIALOG_EVENT_ERROR_START,
            voice_status);
    }

    if (!row_copy(
            row->current_document_uri,
            impl->max_source_bytes,
            next_base, next_base_size,
            &row->current_document_uri_size)) {
        vxml_program_destroy(&next_program);
        return queue_event(
            row, VXML_DIALOG_EVENT_ERROR_START,
            VXML_LIMIT_EXCEEDED);
    }

    row_destroy_runtime(row);
    row->program = next_program;
    row->program_live = true;
    row->program_view = &row->program;
    ++row->navigation_hops;

    voice_status = start_current_session(
        row, next_fragment, next_fragment_size);
    if (voice_status != VXML_OK)
        return queue_event(
            row, VXML_DIALOG_EVENT_ERROR_START,
            voice_status);
    return VXML_DIALOG_MANAGER_OK;
}

static vxml_dialog_manager_status follow_external_navigation(
    vxml_dialog_row *row) {
    vxml_dialog_manager_impl *impl;
    if (row == NULL || row->owner == NULL)
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;
    impl = row->owner;

    while (row->session_live &&
           (vxml_session_get_state(&row->session) ==
                VXML_SESSION_NAVIGATING ||
            vxml_session_get_state(&row->session) ==
                VXML_SESSION_SUBMITTING)) {
        vxml_navigation_request_v1 navigation = {0};
        vxml_document_view next_view = {0};
        vxml_document_ref next_ref = {0};
        vxml_document_store_error error = {0};
        vxml_resolved_uri_v1 resolved;
        vxml_document_store_status store_status;
        vxml_status voice_status;

        if (vxml_session_get_state(&row->session) ==
            VXML_SESSION_SUBMITTING) {
            vxml_dialog_manager_status submit_progress =
                follow_one_submit(row);
            if (submit_progress != VXML_DIALOG_MANAGER_OK)
                return submit_progress;
            if (row->state == VXML_DIALOG_ROW_EVENT_PENDING)
                return VXML_DIALOG_MANAGER_OK;
            continue;
        }

        if (!impl->navigation_enabled ||
            !impl->store_backed ||
            impl->document_store == NULL ||
            row->current_document_uri == NULL ||
            row->current_document_uri_size == 0u) {
            (void)queue_event(
                row, VXML_DIALOG_EVENT_ERROR_START,
                VXML_UNSUPPORTED_FEATURE);
            return VXML_DIALOG_MANAGER_OK;
        }
        if (row->navigation_hops >= impl->max_navigation_hops) {
            (void)queue_event(
                row, VXML_DIALOG_EVENT_ERROR_START,
                VXML_LIMIT_EXCEEDED);
            return VXML_DIALOG_MANAGER_OK;
        }
        voice_status = vxml_session_navigation_request(
            &row->session, &navigation);
        if (voice_status != VXML_OK) {
            (void)queue_event(
                row, VXML_DIALOG_EVENT_ERROR_START,
                voice_status);
            return VXML_DIALOG_MANAGER_OK;
        }
        resolved = (vxml_resolved_uri_v1){
            .abi_version = 1u,
            .struct_size = sizeof(vxml_resolved_uri_v1),
            .document_uri = impl->resolve_uri_scratch,
            .document_uri_capacity = impl->max_source_bytes + 1u,
            .fragment = impl->resolve_fragment_scratch,
            .fragment_capacity = impl->max_source_bytes + 1u};
        store_status = vxml_document_store_resolve(
            impl->document_store,
            row->current_document_uri,
            row->current_document_uri_size,
            navigation.uri,
            navigation.uri_size,
            &resolved);
        if (store_status != VXML_DOCUMENT_STORE_OK) {
            (void)queue_event(
                row, VXML_DIALOG_EVENT_ERROR_START,
                store_failure_voice_status(store_status, NULL));
            return VXML_DIALOG_MANAGER_OK;
        }

        if (navigation.fetchaudio_uri_size != 0u) {
            vxml_resolved_uri_v1 fetchaudio_resolved = {
                .abi_version = 1u,
                .struct_size = sizeof(vxml_resolved_uri_v1),
                .document_uri =
                    impl->resolve_fetchaudio_uri_scratch,
                .document_uri_capacity =
                    impl->max_source_bytes + 1u,
                .fragment =
                    impl->resolve_fetchaudio_fragment_scratch,
                .fragment_capacity =
                    impl->max_source_bytes + 1u};
            vxml_document_fetch_policy_v1 policy =
                VXML_DOCUMENT_FETCH_POLICY_V1_INIT;
            store_status = vxml_document_store_resolve(
                impl->document_store,
                row->current_document_uri,
                row->current_document_uri_size,
                navigation.fetchaudio_uri,
                navigation.fetchaudio_uri_size,
                &fetchaudio_resolved);
            if (store_status != VXML_DOCUMENT_STORE_OK) {
                (void)queue_event(
                    row, VXML_DIALOG_EVENT_ERROR_START,
                    store_failure_voice_status(store_status, NULL));
                return VXML_DIALOG_MANAGER_OK;
            }
            policy.has_fetchaudio = true;
            policy.fetchaudio_uri =
                fetchaudio_resolved.document_uri;
            policy.fetchaudio_uri_size =
                fetchaudio_resolved.document_uri_size;
            policy.has_fetchaudio_delay =
                navigation.has_fetchaudio_delay;
            policy.fetchaudio_delay_us =
                navigation.fetchaudio_delay_us;
            policy.has_fetchaudio_minimum =
                navigation.has_fetchaudio_minimum;
            policy.fetchaudio_minimum_us =
                navigation.fetchaudio_minimum_us;
            store_status =
                vxml_document_store_acquire_with_policy(
                    impl->document_store,
                    resolved.document_uri,
                    resolved.document_uri_size,
                    &policy,
                    &next_ref,
                    &error);
        } else {
            store_status = vxml_document_store_acquire(
                impl->document_store,
                resolved.document_uri,
                resolved.document_uri_size,
                &next_ref,
                &error);
        }
        if (store_status != VXML_DOCUMENT_STORE_OK) {
            (void)queue_event(
                row, VXML_DIALOG_EVENT_ERROR_START,
                store_failure_voice_status(store_status, &error));
            return VXML_DIALOG_MANAGER_OK;
        }
        store_status = vxml_document_store_view(
            impl->document_store, next_ref, &next_view);
        if (store_status != VXML_DOCUMENT_STORE_OK ||
            next_view.program == NULL) {
            (void)vxml_document_store_release(
                impl->document_store, &next_ref);
            (void)queue_event(
                row, VXML_DIALOG_EVENT_ERROR_START,
                VXML_INVALID_STATE);
            return VXML_DIALOG_MANAGER_OK;
        }

        if (!row_copy(
                row->current_document_uri,
                impl->max_source_bytes,
                resolved.document_uri,
                resolved.document_uri_size,
                &row->current_document_uri_size)) {
            (void)vxml_document_store_release(
                impl->document_store, &next_ref);
            (void)queue_event(
                row, VXML_DIALOG_EVENT_ERROR_START,
                VXML_LIMIT_EXCEEDED);
            return VXML_DIALOG_MANAGER_OK;
        }
        row_destroy_runtime(row);
        row->document_ref = next_ref;
        row->document_ref_live = true;
        row->program_view = next_view.program;
        ++row->navigation_hops;

        voice_status = start_current_session(
            row,
            resolved.fragment,
            resolved.fragment_size);
        if (voice_status != VXML_OK) {
            (void)queue_event(
                row, VXML_DIALOG_EVENT_ERROR_START,
                voice_status);
            return VXML_DIALOG_MANAGER_OK;
        }
    }
    return queue_event(
        row, VXML_DIALOG_EVENT_STARTED, VXML_OK);
}

static vxml_dialog_manager_status start_session(
    vxml_dialog_row *row) {
    vxml_status status;
    size_t fragment_size;
    if (row == NULL || row->program_view == NULL)
        return VXML_DIALOG_MANAGER_INVALID_STATE;

    fragment_size = row->start_fragment_size;
    status = start_current_session(
        row,
        row->start_fragment,
        fragment_size);
    row->start_fragment_size = 0u;
    if (row->start_fragment != NULL)
        row->start_fragment[0] = '\0';

    if (status != VXML_OK) {
        (void)queue_event(
            row, VXML_DIALOG_EVENT_ERROR_START, status);
        return VXML_DIALOG_MANAGER_OK;
    }
    if (vxml_session_get_state(&row->session) ==
            VXML_SESSION_NAVIGATING ||
        vxml_session_get_state(&row->session) ==
            VXML_SESSION_SUBMITTING)
        return follow_external_navigation(row);
    return queue_event(
        row, VXML_DIALOG_EVENT_STARTED, VXML_OK);
}

static vxml_status raise_normal_termination_event(
    vxml_dialog_row *row) {
    vxml_status status;
    if (row == NULL || !row->session_live)
        return VXML_OK;
    if (vxml_session_get_state(&row->session) !=
        VXML_SESSION_RUNNING)
        return VXML_OK;
    status = vxml_session_raise_event(
        &row->session,
        EVENT_CONNECTION_DISCONNECT_HANGUP,
        sizeof(EVENT_CONNECTION_DISCONNECT_HANGUP) - 1u);
    return status == VXML_UNSUPPORTED_FEATURE
        ? VXML_OK : status;
}

static vxml_dialog_manager_status progress_row(
    vxml_dialog_row *row) {
    vxml_dialog_manager_status status;
    if (row == NULL) return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;
    switch (row->state) {
    case VXML_DIALOG_ROW_PENDING_PREPARE:
        status = load_document_program(row, VXML_DIALOG_EVENT_ERROR_PREPARE);
        if (status != VXML_DIALOG_MANAGER_OK)
            return status;
        if (row->state == VXML_DIALOG_ROW_PENDING_PREPARE)
            (void)queue_event(row, VXML_DIALOG_EVENT_PREPARED, VXML_OK);
        break;

    case VXML_DIALOG_ROW_PENDING_DIRECT_START:
        status = load_document_program(row, VXML_DIALOG_EVENT_ERROR_START);
        if (status != VXML_DIALOG_MANAGER_OK)
            return status;
        if (row->state == VXML_DIALOG_ROW_PENDING_DIRECT_START) {
            status = start_session(row);
            if (status != VXML_DIALOG_MANAGER_OK)
                return status;
        }
        break;

    case VXML_DIALOG_ROW_PENDING_PREPARED_START:
        status = start_session(row);
        if (status != VXML_DIALOG_MANAGER_OK)
            return status;
        break;

    case VXML_DIALOG_ROW_PENDING_TERMINATE: {
        const vxml_status voice_status =
            raise_normal_termination_event(row);
        row_destroy_runtime(row);
        (void)queue_event(
            row, VXML_DIALOG_EVENT_EXIT, voice_status);
        break;
    }

    case VXML_DIALOG_ROW_EVENT_PENDING:
        break;

    default:
        return VXML_DIALOG_MANAGER_INVALID_STATE;
    }

    while (row->state == VXML_DIALOG_ROW_EVENT_PENDING) {
        status = publish_event(row);
        if (status != VXML_DIALOG_MANAGER_OK)
            return status;
    }
    return VXML_DIALOG_MANAGER_OK;
}

vxml_dialog_manager_config_v1 vxml_dialog_manager_default_config_v1(void) {
    vxml_dialog_manager_config_v1 config;
    memset(&config, 0, sizeof(config));
    config.abi_version = VXML_DIALOG_MANAGER_CONFIG_ABI_V1;
    config.struct_size = sizeof(config);
    config.capacity = 32u;
    config.max_source_bytes = 4096u;
    config.max_media_type_bytes = 127u;
    config.max_connection_id_bytes = 511u;
    config.max_dialog_id_bytes = 63u;
    config.max_document_bytes = 1024u * 1024u;
    config.voice_limits = vxml_default_limits();
    return config;
}

vxml_dialog_manager_config_v2 vxml_dialog_manager_default_config_v2(void) {
    vxml_dialog_manager_config_v2 config;
    memset(&config, 0, sizeof(config));
    config.abi_version = VXML_DIALOG_MANAGER_CONFIG_ABI_V2;
    config.struct_size = sizeof(config);
    config.capacity = 32u;
    config.max_source_bytes = 4096u;
    config.max_media_type_bytes = 127u;
    config.max_connection_id_bytes = 511u;
    config.max_dialog_id_bytes = 63u;
    return config;
}

vxml_dialog_manager_config_v4 vxml_dialog_manager_default_config_v4(void) {
    vxml_dialog_manager_config_v4 config;
    memset(&config, 0, sizeof(config));
    config.abi_version = VXML_DIALOG_MANAGER_CONFIG_ABI_V4;
    config.struct_size = sizeof(config);
    config.capacity = 32u;
    config.max_source_bytes = 4096u;
    config.max_media_type_bytes = 127u;
    config.max_connection_id_bytes = 511u;
    config.max_dialog_id_bytes = 63u;
    config.max_navigation_hops = 32u;
    config.max_submit_response_bytes = 1024u * 1024u;
    config.voice_limits = vxml_default_limits();
    return config;
}

vxml_dialog_manager_config_v3 vxml_dialog_manager_default_config_v3(void) {
    vxml_dialog_manager_config_v3 config;
    memset(&config, 0, sizeof(config));
    config.abi_version = VXML_DIALOG_MANAGER_CONFIG_ABI_V3;
    config.struct_size = sizeof(config);
    config.capacity = 32u;
    config.max_source_bytes = 4096u;
    config.max_media_type_bytes = 127u;
    config.max_connection_id_bytes = 511u;
    config.max_dialog_id_bytes = 63u;
    config.max_navigation_hops = 32u;
    return config;
}

const char *vxml_dialog_manager_status_string(
    vxml_dialog_manager_status status) {
    switch (status) {
    case VXML_DIALOG_MANAGER_OK:
        return "ok";
    case VXML_DIALOG_MANAGER_INVALID_ARGUMENT:
        return "invalid_argument";
    case VXML_DIALOG_MANAGER_ALLOCATION_FAILED:
        return "allocation_failed";
    case VXML_DIALOG_MANAGER_FULL:
        return "full";
    case VXML_DIALOG_MANAGER_CLOSED:
        return "closed";
    case VXML_DIALOG_MANAGER_NOT_FOUND:
        return "not_found";
    case VXML_DIALOG_MANAGER_INVALID_STATE:
        return "invalid_state";
    case VXML_DIALOG_MANAGER_DOCUMENT_ERROR:
        return "document_error";
    case VXML_DIALOG_MANAGER_VXML_ERROR:
        return "vxml_error";
    case VXML_DIALOG_MANAGER_EVENT_FULL:
        return "event_full";
    case VXML_DIALOG_MANAGER_EVENT_CLOSED:
        return "event_closed";
    case VXML_DIALOG_MANAGER_BUSY:
        return "busy";
    default:
        return "unknown";
    }
}

vxml_dialog_manager_status vxml_dialog_manager_init(
    vxml_dialog_manager *manager,
    const vxml_dialog_manager_config_v1 *config) {
    vxml_dialog_manager_impl *impl;
    size_t index;
    if (manager == NULL || manager->impl != NULL ||
        config == NULL ||
        config->abi_version != VXML_DIALOG_MANAGER_CONFIG_ABI_V1 ||
        config->struct_size < sizeof(*config) ||
        config->capacity == 0u ||
        config->max_source_bytes == 0u ||
        config->max_media_type_bytes < sizeof(VXML_MEDIA_TYPE) - 1u ||
        config->max_connection_id_bytes == 0u ||
        config->max_dialog_id_bytes < 16u ||
        config->max_document_bytes == 0u ||
        !upstream_prefix_valid(config->upstream) ||
        config->documents == NULL ||
        config->documents->abi_version !=
            VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1 ||
        config->documents->struct_size < sizeof(*config->documents) ||
        config->documents->open == NULL ||
        config->documents->close == NULL ||
        config->events == NULL ||
        config->events->abi_version != VXML_DIALOG_EVENT_SINK_ABI_V1 ||
        config->events->struct_size < sizeof(*config->events) ||
        config->events->try_publish == NULL)
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;

    impl = (vxml_dialog_manager_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return VXML_DIALOG_MANAGER_ALLOCATION_FAILED;
    impl->rows = (vxml_dialog_row *)calloc(
        config->capacity, sizeof(*impl->rows));
    if (impl->rows == NULL) {
        free(impl);
        return VXML_DIALOG_MANAGER_ALLOCATION_FAILED;
    }

    impl->capacity = config->capacity;
    impl->max_source_bytes = config->max_source_bytes;
    impl->max_media_type_bytes = config->max_media_type_bytes;
    impl->max_connection_id_bytes = config->max_connection_id_bytes;
    impl->max_dialog_id_bytes = config->max_dialog_id_bytes;
    impl->max_document_bytes = config->max_document_bytes;
    impl->voice_limits = config->voice_limits;
    memset(&impl->upstream, 0, sizeof(impl->upstream));
    memcpy(
        &impl->upstream, config->upstream,
        min_size(config->upstream->struct_size, sizeof(impl->upstream)));
    impl->upstream_user = config->upstream_user;
    impl->documents = *config->documents;
    impl->document_user = config->document_user;
    impl->events = *config->events;
    impl->event_user = config->event_user;

    for (index = 0u; index < impl->capacity; ++index) {
        vxml_dialog_row *row = &impl->rows[index];
        row->owner = impl;
        row->slot = index;
        row->state = VXML_DIALOG_ROW_EMPTY;
        if (!row_allocate_buffers(
                row, impl->max_source_bytes,
                impl->max_media_type_bytes,
                impl->max_connection_id_bytes,
                impl->max_dialog_id_bytes)) {
            size_t cleanup;
            for (cleanup = 0u; cleanup <= index; ++cleanup)
                row_free_buffers(&impl->rows[cleanup]);
            free(impl->rows);
            free(impl);
            return VXML_DIALOG_MANAGER_ALLOCATION_FAILED;
        }
    }

    manager->impl = impl;
    return VXML_DIALOG_MANAGER_OK;
}

vxml_dialog_manager_status vxml_dialog_manager_init_v2(
    vxml_dialog_manager *manager,
    const vxml_dialog_manager_config_v2 *config) {
    const size_t historical_prefix =
        offsetof(vxml_dialog_manager_config_v2, event_user) +
        sizeof(config->event_user);
    const size_t factory_tail =
        offsetof(vxml_dialog_manager_config_v2, session_factory_user) +
        sizeof(config->session_factory_user);
    vxml_dialog_manager_impl *impl;
    size_t index;
    if (manager == NULL || manager->impl != NULL ||
        config == NULL ||
        config->abi_version != VXML_DIALOG_MANAGER_CONFIG_ABI_V2 ||
        config->struct_size < historical_prefix ||
        config->capacity == 0u ||
        config->max_source_bytes == 0u ||
        config->max_source_bytes == SIZE_MAX ||
        config->max_media_type_bytes < sizeof(VXML_MEDIA_TYPE) - 1u ||
        config->max_connection_id_bytes == 0u ||
        config->max_dialog_id_bytes < 16u ||
        !upstream_prefix_valid(config->upstream) ||
        config->document_store == NULL ||
        config->document_store->impl == NULL ||
        config->events == NULL ||
        config->events->abi_version != VXML_DIALOG_EVENT_SINK_ABI_V1 ||
        config->events->struct_size < sizeof(*config->events) ||
        config->events->try_publish == NULL ||
        (config->struct_size >= factory_tail &&
         config->session_factory != NULL &&
         !session_factory_prefix_valid(
             config->session_factory)))
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;

    impl = (vxml_dialog_manager_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return VXML_DIALOG_MANAGER_ALLOCATION_FAILED;
    impl->rows = (vxml_dialog_row *)calloc(
        config->capacity, sizeof(*impl->rows));
    impl->resolve_uri_scratch =
        (char *)calloc(config->max_source_bytes + 1u, 1u);
    impl->resolve_fragment_scratch =
        (char *)calloc(config->max_source_bytes + 1u, 1u);
    impl->resolve_fetchaudio_uri_scratch =
        (char *)calloc(config->max_source_bytes + 1u, 1u);
    impl->resolve_fetchaudio_fragment_scratch =
        (char *)calloc(config->max_source_bytes + 1u, 1u);
    if (impl->rows == NULL ||
        impl->resolve_uri_scratch == NULL ||
        impl->resolve_fragment_scratch == NULL ||
        impl->resolve_fetchaudio_uri_scratch == NULL ||
        impl->resolve_fetchaudio_fragment_scratch == NULL) {
        free(impl->resolve_fetchaudio_fragment_scratch);
        free(impl->resolve_fetchaudio_uri_scratch);
        free(impl->resolve_fragment_scratch);
        free(impl->resolve_uri_scratch);
        free(impl->rows);
        free(impl);
        return VXML_DIALOG_MANAGER_ALLOCATION_FAILED;
    }

    impl->capacity = config->capacity;
    impl->max_source_bytes = config->max_source_bytes;
    impl->max_media_type_bytes = config->max_media_type_bytes;
    impl->max_connection_id_bytes = config->max_connection_id_bytes;
    impl->max_dialog_id_bytes = config->max_dialog_id_bytes;
    impl->document_store = config->document_store;
    impl->store_backed = true;
    memset(&impl->upstream, 0, sizeof(impl->upstream));
    memcpy(
        &impl->upstream, config->upstream,
        min_size(config->upstream->struct_size, sizeof(impl->upstream)));
    impl->upstream_user = config->upstream_user;
    impl->events = *config->events;
    impl->event_user = config->event_user;
    if (config->struct_size >= factory_tail &&
        config->session_factory != NULL) {
        memset(
            &impl->session_factory, 0,
            sizeof(impl->session_factory));
        memcpy(
            &impl->session_factory,
            config->session_factory,
            min_size(
                config->session_factory->struct_size,
                sizeof(impl->session_factory)));
        impl->session_factory_user =
            config->session_factory_user;
        impl->session_factory_enabled = true;
    }

    for (index = 0u; index < impl->capacity; ++index) {
        vxml_dialog_row *row = &impl->rows[index];
        row->owner = impl;
        row->slot = index;
        row->state = VXML_DIALOG_ROW_EMPTY;
        if (!row_allocate_buffers(
                row, impl->max_source_bytes,
                impl->max_media_type_bytes,
                impl->max_connection_id_bytes,
                impl->max_dialog_id_bytes)) {
            size_t cleanup;
            for (cleanup = 0u; cleanup <= index; ++cleanup)
                row_free_buffers(&impl->rows[cleanup]);
            free(impl->resolve_fetchaudio_fragment_scratch);
            free(impl->resolve_fetchaudio_uri_scratch);
            free(impl->resolve_fragment_scratch);
            free(impl->resolve_uri_scratch);
            free(impl->rows);
            free(impl);
            return VXML_DIALOG_MANAGER_ALLOCATION_FAILED;
        }
    }

    manager->impl = impl;
    return VXML_DIALOG_MANAGER_OK;
}

vxml_dialog_manager_status vxml_dialog_manager_init_v4(
    vxml_dialog_manager *manager,
    const vxml_dialog_manager_config_v4 *config) {
    const size_t historical_prefix =
        offsetof(vxml_dialog_manager_config_v4, event_user) +
        sizeof(config->event_user);
    const size_t factory_tail =
        offsetof(vxml_dialog_manager_config_v4, session_factory_user) +
        sizeof(config->session_factory_user);
    vxml_dialog_manager_config_v3 v3;
    vxml_dialog_manager_impl *impl;
    vxml_dialog_manager_status status;

    if (manager == NULL || config == NULL ||
        config->abi_version != VXML_DIALOG_MANAGER_CONFIG_ABI_V4 ||
        config->struct_size < historical_prefix ||
        config->max_navigation_hops == 0u ||
        config->max_submit_response_bytes == 0u ||
        config->max_submit_response_bytes == SIZE_MAX ||
        !submit_prefix_valid(config->submit) ||
        (config->struct_size >= factory_tail &&
         config->session_factory != NULL &&
         !session_factory_prefix_valid(
             config->session_factory)))
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;

    v3 = vxml_dialog_manager_default_config_v3();
    v3.capacity = config->capacity;
    v3.max_source_bytes = config->max_source_bytes;
    v3.max_media_type_bytes = config->max_media_type_bytes;
    v3.max_connection_id_bytes = config->max_connection_id_bytes;
    v3.max_dialog_id_bytes = config->max_dialog_id_bytes;
    v3.max_navigation_hops = config->max_navigation_hops;
    v3.upstream = config->upstream;
    v3.upstream_user = config->upstream_user;
    v3.document_store = config->document_store;
    v3.events = config->events;
    v3.event_user = config->event_user;
    if (config->struct_size >= factory_tail) {
        v3.session_factory = config->session_factory;
        v3.session_factory_user =
            config->session_factory_user;
    }

    status = vxml_dialog_manager_init_v3(manager, &v3);
    if (status != VXML_DIALOG_MANAGER_OK)
        return status;

    impl = (vxml_dialog_manager_impl *)manager->impl;
    memset(&impl->submit, 0, sizeof(impl->submit));
    memcpy(
        &impl->submit, config->submit,
        min_size(config->submit->struct_size, sizeof(impl->submit)));
    impl->submit_user = config->submit_user;
    impl->submit_enabled = true;
    impl->max_submit_response_bytes =
        config->max_submit_response_bytes;
    impl->voice_limits = config->voice_limits;
    return VXML_DIALOG_MANAGER_OK;
}

vxml_dialog_manager_status vxml_dialog_manager_init_v3(
    vxml_dialog_manager *manager,
    const vxml_dialog_manager_config_v3 *config) {
    const size_t historical_prefix =
        offsetof(vxml_dialog_manager_config_v3, event_user) +
        sizeof(config->event_user);
    const size_t factory_tail =
        offsetof(vxml_dialog_manager_config_v3, session_factory_user) +
        sizeof(config->session_factory_user);
    vxml_dialog_manager_config_v2 v2;
    vxml_dialog_manager_impl *impl;
    vxml_dialog_manager_status status;

    if (manager == NULL || config == NULL ||
        config->abi_version != VXML_DIALOG_MANAGER_CONFIG_ABI_V3 ||
        config->struct_size < historical_prefix ||
        config->max_navigation_hops == 0u ||
        (config->struct_size >= factory_tail &&
         config->session_factory != NULL &&
         !session_factory_prefix_valid(
             config->session_factory)))
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;

    v2 = vxml_dialog_manager_default_config_v2();
    v2.capacity = config->capacity;
    v2.max_source_bytes = config->max_source_bytes;
    v2.max_media_type_bytes = config->max_media_type_bytes;
    v2.max_connection_id_bytes = config->max_connection_id_bytes;
    v2.max_dialog_id_bytes = config->max_dialog_id_bytes;
    v2.upstream = config->upstream;
    v2.upstream_user = config->upstream_user;
    v2.document_store = config->document_store;
    v2.events = config->events;
    v2.event_user = config->event_user;
    if (config->struct_size >= factory_tail) {
        v2.session_factory = config->session_factory;
        v2.session_factory_user =
            config->session_factory_user;
    }

    status = vxml_dialog_manager_init_v2(manager, &v2);
    if (status != VXML_DIALOG_MANAGER_OK)
        return status;
    impl = (vxml_dialog_manager_impl *)manager->impl;
    impl->navigation_enabled = true;
    impl->max_navigation_hops = config->max_navigation_hops;
    return VXML_DIALOG_MANAGER_OK;
}

const ccxml_telephony_adapter_v1 *vxml_dialog_manager_ccxml_adapter(void) {
    return &MANAGER_ADAPTER;
}

void *vxml_dialog_manager_ccxml_user(vxml_dialog_manager *manager) {
    return manager != NULL ? manager->impl : NULL;
}

vxml_dialog_manager_status vxml_dialog_manager_run_ready(
    vxml_dialog_manager *manager,
    size_t max_work,
    size_t *out_processed) {
    vxml_dialog_manager_impl *impl;
    size_t processed = 0u;
    size_t index;
    if (out_processed != NULL) *out_processed = 0u;
    if (manager == NULL || manager->impl == NULL || max_work == 0u)
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;
    impl = (vxml_dialog_manager_impl *)manager->impl;
    if (impl->closed)
        return VXML_DIALOG_MANAGER_CLOSED;

    for (index = 0u; index < impl->capacity && processed < max_work; ++index) {
        vxml_dialog_row *row = &impl->rows[index];
        vxml_dialog_manager_status status;
        if (row->state != VXML_DIALOG_ROW_PENDING_PREPARE &&
            row->state != VXML_DIALOG_ROW_PENDING_DIRECT_START &&
            row->state != VXML_DIALOG_ROW_PENDING_PREPARED_START &&
            row->state != VXML_DIALOG_ROW_PENDING_TERMINATE &&
            row->state != VXML_DIALOG_ROW_EVENT_PENDING)
            continue;
        ++processed;
        status = progress_row(row);
        if (status != VXML_DIALOG_MANAGER_OK) {
            if (out_processed != NULL) *out_processed = processed;
            return status;
        }
    }
    if (out_processed != NULL) *out_processed = processed;
    return VXML_DIALOG_MANAGER_OK;
}

bool vxml_dialog_manager_get_stats(
    const vxml_dialog_manager *manager,
    vxml_dialog_manager_stats *out_stats) {
    const vxml_dialog_manager_impl *impl;
    size_t index;
    vxml_dialog_manager_stats stats = {0};
    if (manager == NULL || manager->impl == NULL || out_stats == NULL)
        return false;
    impl = (const vxml_dialog_manager_impl *)manager->impl;
    stats.capacity = impl->capacity;
    stats.accepted_operations = impl->accepted_operations;
    stats.discarded_operations = impl->discarded_operations;
    stats.rejected_full = impl->rejected_full;
    stats.stale_operations = impl->stale_operations;
    stats.closed = impl->closed;
    for (index = 0u; index < impl->capacity; ++index) {
        switch (impl->rows[index].state) {
        case VXML_DIALOG_ROW_EMPTY:
            break;
        case VXML_DIALOG_ROW_RESERVED_PREPARE:
        case VXML_DIALOG_ROW_RESERVED_DIRECT_START:
        case VXML_DIALOG_ROW_RESERVED_PREPARED_START:
        case VXML_DIALOG_ROW_RESERVED_TERMINATE:
            ++stats.active;
            ++stats.reserved;
            break;
        case VXML_DIALOG_ROW_PENDING_PREPARE:
        case VXML_DIALOG_ROW_PENDING_DIRECT_START:
        case VXML_DIALOG_ROW_PENDING_PREPARED_START:
        case VXML_DIALOG_ROW_PENDING_TERMINATE:
            ++stats.active;
            ++stats.pending;
            break;
        case VXML_DIALOG_ROW_PREPARED:
            ++stats.active;
            ++stats.prepared;
            break;
        case VXML_DIALOG_ROW_RUNNING:
            ++stats.active;
            break;
        case VXML_DIALOG_ROW_EVENT_PENDING:
            ++stats.active;
            ++stats.event_pending;
            break;
        }
    }
    *out_stats = stats;
    return true;
}

void vxml_dialog_manager_close(vxml_dialog_manager *manager) {
    vxml_dialog_manager_impl *impl;
    size_t index;
    if (manager == NULL || manager->impl == NULL) return;
    impl = (vxml_dialog_manager_impl *)manager->impl;
    if (impl->closed) return;
    impl->closed = true;
    for (index = 0u; index < impl->capacity; ++index)
        if (impl->rows[index].state != VXML_DIALOG_ROW_EMPTY)
            row_clear(&impl->rows[index]);
    if (!impl->upstream_close_called) {
        impl->upstream_close_called = true;
        impl->upstream.close(impl->upstream_user);
    }
}

bool vxml_dialog_manager_is_quiescent(
    const vxml_dialog_manager *manager) {
    const vxml_dialog_manager_impl *impl;
    size_t index;
    if (manager == NULL || manager->impl == NULL)
        return false;
    impl = (const vxml_dialog_manager_impl *)manager->impl;
    if (!impl->closed)
        return false;
    for (index = 0u; index < impl->capacity; ++index)
        if (impl->rows[index].state != VXML_DIALOG_ROW_EMPTY)
            return false;
    return impl->upstream.is_quiescent(impl->upstream_user);
}

vxml_dialog_manager_status vxml_dialog_manager_destroy(
    vxml_dialog_manager *manager) {
    vxml_dialog_manager_impl *impl;
    size_t index;
    if (manager == NULL || manager->impl == NULL)
        return VXML_DIALOG_MANAGER_INVALID_ARGUMENT;
    if (!vxml_dialog_manager_is_quiescent(manager))
        return VXML_DIALOG_MANAGER_BUSY;
    impl = (vxml_dialog_manager_impl *)manager->impl;
    for (index = 0u; index < impl->capacity; ++index)
        row_free_buffers(&impl->rows[index]);
    free(impl->resolve_fetchaudio_fragment_scratch);
    free(impl->resolve_fetchaudio_uri_scratch);
    free(impl->resolve_fragment_scratch);
    free(impl->resolve_uri_scratch);
    free(impl->rows);
    free(impl);
    manager->impl = NULL;
    return VXML_DIALOG_MANAGER_OK;
}
