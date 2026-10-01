#include <voicexml/subdialog_owner.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef enum owner_row_state {
    OWNER_ROW_EMPTY = 0,
    OWNER_ROW_WRITING,
    OWNER_ROW_RESERVED,
    OWNER_ROW_PENDING,
    OWNER_ROW_ACTIVE,
    OWNER_ROW_CANCELED
} owner_row_state;

typedef struct owner_impl owner_impl;
typedef struct owner_row owner_row;

typedef struct owner_parent_context {
    owner_impl *owner;
    vxml_session *session;
    const char *document_uri;
    size_t document_uri_size;
    size_t depth;
} owner_parent_context;

struct owner_row {
    owner_impl *owner;
    size_t slot;
    atomic_uint state;

    owner_parent_context *parent;
    uint64_t generation;
    size_t depth;

    char *src;
    size_t src_size;
    vxml_cmeta_subdialog_param_v1 *params;
    size_t param_count;
    char *param_storage;
    size_t param_storage_size;

    char *current_document_uri;
    size_t current_document_uri_size;
    size_t navigation_hops;

    vxml_document_ref document_ref;
    bool document_ref_live;
    const vxml_program *program_view;

    vxml_session session;
    bool session_live;
    owner_parent_context child_context;

    vxml_cmeta_subdialog_result_entry_v1 *completion_entries;
    const char *synthetic_event;
    size_t synthetic_event_size;
};

struct owner_impl {
    size_t capacity;
    size_t max_uri_bytes;
    size_t max_params;
    size_t max_param_bytes;
    size_t max_completion_entries;
    size_t max_navigation_hops;
    size_t max_nesting_depth;

    vxml_document_store *document_store;
    vxml_cmeta_session_options_v1 child_options;

    owner_parent_context root_context;
    char *root_document_uri;

    char *resolve_uri;
    char *resolve_fragment;

    owner_row *rows;

    atomic_uint_fast64_t accepted;
    atomic_uint_fast64_t discarded;
    atomic_uint_fast64_t stale;
    bool closed;
};

static const char owner_error_badfetch[] = "error.badfetch";
static const char owner_error_semantic[] = "error.semantic";

static bool bytes_valid(const char *data, size_t size) {
    return data != NULL && size != 0u &&
        memchr(data, '\0', size) == NULL;
}

static bool copy_bytes(
    char *destination, size_t capacity,
    const char *source, size_t size,
    size_t *out_size) {
    if (destination == NULL || out_size == NULL ||
        size > capacity ||
        (size != 0u && source == NULL))
        return false;
    if (size != 0u)
        memcpy(destination, source, size);
    destination[size] = '\0';
    *out_size = size;
    return true;
}

static bool append_storage(
    owner_row *row, size_t capacity,
    const char *source, size_t size,
    const char **out) {
    char *destination;
    if (out != NULL) *out = NULL;
    if (row == NULL || out == NULL ||
        row->param_storage_size > capacity ||
        size > capacity - row->param_storage_size ||
        (size != 0u && source == NULL))
        return false;
    if (size == 0u)
        return true;
    destination = row->param_storage + row->param_storage_size;
    memcpy(destination, source, size);
    row->param_storage_size += size;
    *out = destination;
    return true;
}

static void row_request_reset(owner_row *row) {
    if (row == NULL) return;
    row->parent = NULL;
    row->generation = UINT64_C(0);
    row->depth = 0u;
    row->src_size = 0u;
    row->param_count = 0u;
    row->param_storage_size = 0u;
    row->synthetic_event = NULL;
    row->synthetic_event_size = 0u;
    if (row->src != NULL)
        row->src[0] = '\0';
    if (row->params != NULL && row->owner != NULL &&
        row->owner->max_params != 0u)
        memset(
            row->params, 0,
            row->owner->max_params * sizeof(*row->params));
}

static void row_destroy_runtime(owner_row *row) {
    if (row == NULL) return;
    if (row->session_live) {
        (void)vxml_session_close(&row->session);
        vxml_session_destroy(&row->session);
        row->session_live = false;
    }
    row->child_context.session = NULL;
    row->child_context.document_uri = NULL;
    row->child_context.document_uri_size = 0u;
    if (row->document_ref_live && row->owner != NULL &&
        row->owner->document_store != NULL) {
        (void)vxml_document_store_release(
            row->owner->document_store, &row->document_ref);
        row->document_ref_live = false;
    }
    row->document_ref = (vxml_document_ref){0};
    row->program_view = NULL;
    row->current_document_uri_size = 0u;
    row->navigation_hops = 0u;
    if (row->current_document_uri != NULL)
        row->current_document_uri[0] = '\0';
}

static void row_clear(owner_row *row) {
    if (row == NULL) return;
    row_destroy_runtime(row);
    row_request_reset(row);
    atomic_store_explicit(
        &row->state, OWNER_ROW_EMPTY, memory_order_release);
}

static owner_row *find_empty_row(owner_impl *impl) {
    size_t index;
    if (impl == NULL) return NULL;
    for (index = 0u; index < impl->capacity; ++index) {
        owner_row *row = &impl->rows[index];
        unsigned expected = OWNER_ROW_EMPTY;
        if (atomic_compare_exchange_strong_explicit(
                &row->state, &expected, OWNER_ROW_WRITING,
                memory_order_acq_rel, memory_order_acquire))
            return row;
    }
    return NULL;
}

static bool copy_param(
    owner_row *row,
    const vxml_cmeta_subdialog_param_v1 *input) {
    owner_impl *impl;
    vxml_cmeta_subdialog_param_v1 *output;
    const char *owned = NULL;
    if (row == NULL || row->owner == NULL || input == NULL ||
        row->param_count >= row->owner->max_params ||
        input->name.data == NULL || input->name.size == 0u ||
        memchr(input->name.data, '\0', input->name.size) != NULL)
        return false;
    impl = row->owner;
    output = &row->params[row->param_count];
    if (!append_storage(
            row, impl->max_param_bytes,
            input->name.data, input->name.size, &owned))
        return false;
    output->name = (vxml_cmeta_name_view){owned, input->name.size};
    output->source = input->source;

    if (input->source == VXML_CMETA_SUBDIALOG_PARAM_TYPED) {
        if (input->value.kind == VXML_CMETA_VALUE_UNDEFINED ||
            input->literal.data != NULL ||
            input->literal.size != 0u)
            return false;
        output->value = input->value;
        if (input->value.kind == VXML_CMETA_VALUE_STRING) {
            if (input->value.data.string.size != 0u &&
                input->value.data.string.data == NULL)
                return false;
            owned = NULL;
            if (!append_storage(
                    row, impl->max_param_bytes,
                    input->value.data.string.data,
                    input->value.data.string.size, &owned))
                return false;
            output->value.data.string.data = owned;
        } else if (input->value.kind < VXML_CMETA_VALUE_BOOL ||
                   input->value.kind > VXML_CMETA_VALUE_FLOAT) {
            return false;
        }
    } else if (input->source == VXML_CMETA_SUBDIALOG_PARAM_LITERAL) {
        if (input->value.kind != VXML_CMETA_VALUE_UNDEFINED ||
            (input->literal.size != 0u &&
             input->literal.data == NULL))
            return false;
        owned = NULL;
        if (!append_storage(
                row, impl->max_param_bytes,
                input->literal.data, input->literal.size, &owned))
            return false;
        output->literal =
            (vxml_cmeta_name_view){owned, input->literal.size};
    } else {
        return false;
    }

    ++row->param_count;
    return true;
}

static void row_ticket_commit(void *user) {
    owner_row *row = (owner_row *)user;
    unsigned expected = OWNER_ROW_RESERVED;
    if (row == NULL || row->owner == NULL)
        return;
    if (!atomic_compare_exchange_strong_explicit(
            &row->state, &expected, OWNER_ROW_PENDING,
            memory_order_acq_rel, memory_order_acquire))
        atomic_fetch_add_explicit(
            &row->owner->stale, UINT64_C(1), memory_order_relaxed);
}

static void row_ticket_discard(void *user) {
    owner_row *row = (owner_row *)user;
    unsigned expected = OWNER_ROW_RESERVED;
    if (row == NULL || row->owner == NULL)
        return;
    if (!atomic_compare_exchange_strong_explicit(
            &row->state, &expected, OWNER_ROW_WRITING,
            memory_order_acq_rel, memory_order_acquire)) {
        atomic_fetch_add_explicit(
            &row->owner->stale, UINT64_C(1), memory_order_relaxed);
        return;
    }
    atomic_fetch_add_explicit(
        &row->owner->discarded, UINT64_C(1), memory_order_relaxed);
    row_request_reset(row);
    atomic_store_explicit(
        &row->state, OWNER_ROW_EMPTY, memory_order_release);
}

static vxml_status owner_prepare(
    void *user,
    const vxml_cmeta_subdialog_request_v1 *request,
    vxml_cmeta_subdialog_ticket_v1 *out_ticket,
    const char **out_error) {
    owner_parent_context *parent =
        (owner_parent_context *)user;
    owner_impl *impl =
        parent != NULL ? parent->owner : NULL;
    owner_row *row;
    size_t index;

    if (out_error != NULL) *out_error = NULL;
    if (out_ticket != NULL)
        *out_ticket = (vxml_cmeta_subdialog_ticket_v1){0};
    if (impl == NULL || request == NULL || out_ticket == NULL ||
        request->abi_version != VXML_CMETA_SUBDIALOG_REQUEST_ABI_V1 ||
        request->struct_size < sizeof(*request) ||
        request->generation == UINT64_C(0) ||
        !bytes_valid(request->src.data, request->src.size) ||
        ((request->params == NULL) != (request->param_count == 0u)))
        return VXML_INVALID_ARGUMENT;
    if (impl->closed || parent->session == NULL ||
        !bytes_valid(parent->document_uri, parent->document_uri_size))
        return impl->closed ? VXML_CLOSED : VXML_INVALID_STATE;
    if (request->src.size > impl->max_uri_bytes ||
        request->param_count > impl->max_params ||
        parent->depth >= impl->max_nesting_depth)
        return VXML_LIMIT_EXCEEDED;

    row = find_empty_row(impl);
    if (row == NULL)
        return VXML_LIMIT_EXCEEDED;
    row_request_reset(row);
    row->parent = parent;
    row->generation = request->generation;
    row->depth = parent->depth + 1u;
    if (!copy_bytes(
            row->src, impl->max_uri_bytes,
            request->src.data, request->src.size,
            &row->src_size)) {
        row_request_reset(row);
        atomic_store_explicit(
            &row->state, OWNER_ROW_EMPTY, memory_order_release);
        return VXML_LIMIT_EXCEEDED;
    }
    for (index = 0u; index < request->param_count; ++index) {
        if (!copy_param(row, &request->params[index])) {
            row_request_reset(row);
            atomic_store_explicit(
                &row->state, OWNER_ROW_EMPTY, memory_order_release);
            return VXML_LIMIT_EXCEEDED;
        }
    }

    atomic_store_explicit(
        &row->state, OWNER_ROW_RESERVED, memory_order_release);
    atomic_fetch_add_explicit(
        &impl->accepted, UINT64_C(1), memory_order_relaxed);
    *out_ticket = (vxml_cmeta_subdialog_ticket_v1){
        .commit = row_ticket_commit,
        .discard = row_ticket_discard,
        .user = row};
    return VXML_OK;
}

static void owner_cancel(void *user, uint64_t generation) {
    owner_parent_context *parent =
        (owner_parent_context *)user;
    owner_impl *impl =
        parent != NULL ? parent->owner : NULL;
    size_t index;
    bool found = false;
    if (impl == NULL || generation == UINT64_C(0))
        return;

    for (index = 0u; index < impl->capacity; ++index) {
        owner_row *row = &impl->rows[index];
        unsigned state = atomic_load_explicit(
            &row->state, memory_order_acquire);
        if (state == OWNER_ROW_EMPTY ||
            state == OWNER_ROW_WRITING ||
            row->parent != parent ||
            row->generation != generation)
            continue;
        for (;;) {
            unsigned expected = state;
            if (state == OWNER_ROW_CANCELED) {
                found = true;
                break;
            }
            if (state != OWNER_ROW_RESERVED &&
                state != OWNER_ROW_PENDING &&
                state != OWNER_ROW_ACTIVE)
                break;
            if (atomic_compare_exchange_weak_explicit(
                    &row->state, &expected,
                    OWNER_ROW_CANCELED,
                    memory_order_acq_rel, memory_order_acquire)) {
                found = true;
                break;
            }
            state = expected;
        }
        if (found) break;
    }
    if (!found)
        atomic_fetch_add_explicit(
            &impl->stale, UINT64_C(1), memory_order_relaxed);
}

static const vxml_cmeta_subdialog_adapter_v1 owner_adapter = {
    .abi_version = VXML_CMETA_SUBDIALOG_ADAPTER_ABI_V1,
    .struct_size = sizeof(vxml_cmeta_subdialog_adapter_v1),
    .prepare = owner_prepare,
    .cancel = owner_cancel};

const vxml_cmeta_subdialog_adapter_v1 *
vxml_cmeta_subdialog_owner_adapter(void) {
    return &owner_adapter;
}

static void row_set_synthetic_event(
    owner_row *row, const char *event, size_t event_size) {
    if (row == NULL) return;
    row->synthetic_event = event;
    row->synthetic_event_size = event_size;
}

static vxml_status child_options(
    owner_row *row,
    vxml_cmeta_session_options_v1 *out) {
    if (row == NULL || row->owner == NULL || out == NULL)
        return VXML_INVALID_ARGUMENT;
    *out = row->owner->child_options;
    out->abi_version = VXML_CMETA_SESSION_OPTIONS_ABI_V1;
    out->struct_size = sizeof(*out);
    out->subdialog = &owner_adapter;
    out->subdialog_user = &row->child_context;
    return VXML_OK;
}

static vxml_status row_init_session(
    owner_row *row,
    const vxml_program *program,
    const char *fragment,
    size_t fragment_size,
    bool import_parameters) {
    vxml_cmeta_session_options_v1 options;
    vxml_cmeta_child_entry_v1 entry = {
        .abi_version = VXML_CMETA_CHILD_ENTRY_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_child_entry_v1)};
    vxml_status status;
    if (row == NULL || program == NULL)
        return VXML_INVALID_ARGUMENT;
    status = child_options(row, &options);
    if (status != VXML_OK) return status;
    status = vxml_session_init_cmeta(
        &row->session, program, &options);
    if (status != VXML_OK)
        return status;
    row->session_live = true;
    row->child_context.owner = row->owner;
    row->child_context.session = &row->session;
    row->child_context.document_uri =
        row->current_document_uri;
    row->child_context.document_uri_size =
        row->current_document_uri_size;
    row->child_context.depth = row->depth;

    if (!import_parameters) {
        return fragment_size != 0u
            ? vxml_session_start_at_form(
                  &row->session, fragment, fragment_size)
            : vxml_session_start(&row->session);
    }

    entry.form_id = (vxml_cmeta_name_view){
        fragment_size != 0u ? fragment : NULL,
        fragment_size};
    entry.params =
        row->param_count != 0u ? row->params : NULL;
    entry.param_count = row->param_count;
    return vxml_session_cmeta_start_child(
        &row->session, &entry);
}

static vxml_status row_acquire_initial(owner_row *row) {
    owner_impl *impl;
    vxml_resolved_uri_v1 resolved;
    vxml_document_store_error error = {0};
    vxml_document_view view = {0};
    vxml_document_store_status store_status;
    vxml_status status;
    if (row == NULL || row->owner == NULL ||
        row->parent == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = row->owner;
    resolved = (vxml_resolved_uri_v1){
        .abi_version = 1u,
        .struct_size = sizeof(vxml_resolved_uri_v1),
        .document_uri = impl->resolve_uri,
        .document_uri_capacity = impl->max_uri_bytes + 1u,
        .fragment = impl->resolve_fragment,
        .fragment_capacity = impl->max_uri_bytes + 1u};

    store_status = vxml_document_store_resolve(
        impl->document_store,
        row->parent->document_uri,
        row->parent->document_uri_size,
        row->src, row->src_size,
        &resolved);
    if (store_status != VXML_DOCUMENT_STORE_OK) {
        row_set_synthetic_event(
            row, owner_error_badfetch,
            sizeof(owner_error_badfetch) - 1u);
        return VXML_OK;
    }

    store_status = vxml_document_store_acquire(
        impl->document_store,
        resolved.document_uri, resolved.document_uri_size,
        &row->document_ref, &error);
    if (store_status != VXML_DOCUMENT_STORE_OK) {
        row->document_ref = (vxml_document_ref){0};
        row_set_synthetic_event(
            row, owner_error_badfetch,
            sizeof(owner_error_badfetch) - 1u);
        return VXML_OK;
    }
    row->document_ref_live = true;
    store_status = vxml_document_store_view(
        impl->document_store, row->document_ref, &view);
    if (store_status != VXML_DOCUMENT_STORE_OK ||
        view.program == NULL) {
        row_destroy_runtime(row);
        row_set_synthetic_event(
            row, owner_error_badfetch,
            sizeof(owner_error_badfetch) - 1u);
        return VXML_OK;
    }
    if (!copy_bytes(
            row->current_document_uri,
            impl->max_uri_bytes,
            resolved.document_uri, resolved.document_uri_size,
            &row->current_document_uri_size)) {
        row_destroy_runtime(row);
        row_set_synthetic_event(
            row, owner_error_badfetch,
            sizeof(owner_error_badfetch) - 1u);
        return VXML_OK;
    }
    row->program_view = view.program;

    status = row_init_session(
        row, row->program_view,
        resolved.fragment, resolved.fragment_size,
        true);
    if (status != VXML_OK) {
        row_destroy_runtime(row);
        row_set_synthetic_event(
            row, owner_error_semantic,
            sizeof(owner_error_semantic) - 1u);
        return VXML_OK;
    }
    return VXML_OK;
}

static vxml_status row_follow_navigation(owner_row *row) {
    owner_impl *impl;
    if (row == NULL || row->owner == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = row->owner;

    while (row->session_live &&
           vxml_session_get_state(&row->session) ==
               VXML_SESSION_NAVIGATING) {
        vxml_navigation_request_v1 navigation = {0};
        vxml_resolved_uri_v1 resolved;
        vxml_document_ref next_ref = {0};
        vxml_document_view next_view = {0};
        vxml_document_store_error error = {0};
        vxml_document_store_status store_status;
        vxml_status status;

        if (row->navigation_hops >=
            impl->max_navigation_hops)
            return VXML_LIMIT_EXCEEDED;
        status = vxml_session_navigation_request(
            &row->session, &navigation);
        if (status != VXML_OK)
            return status;
        resolved = (vxml_resolved_uri_v1){
            .abi_version = 1u,
            .struct_size = sizeof(vxml_resolved_uri_v1),
            .document_uri = impl->resolve_uri,
            .document_uri_capacity = impl->max_uri_bytes + 1u,
            .fragment = impl->resolve_fragment,
            .fragment_capacity = impl->max_uri_bytes + 1u};
        store_status = vxml_document_store_resolve(
            impl->document_store,
            row->current_document_uri,
            row->current_document_uri_size,
            navigation.uri, navigation.uri_size,
            &resolved);
        if (store_status != VXML_DOCUMENT_STORE_OK) {
            row_set_synthetic_event(
                row, owner_error_badfetch,
                sizeof(owner_error_badfetch) - 1u);
            return VXML_OK;
        }
        store_status = vxml_document_store_acquire(
            impl->document_store,
            resolved.document_uri, resolved.document_uri_size,
            &next_ref, &error);
        if (store_status != VXML_DOCUMENT_STORE_OK) {
            row_set_synthetic_event(
                row, owner_error_badfetch,
                sizeof(owner_error_badfetch) - 1u);
            return VXML_OK;
        }
        store_status = vxml_document_store_view(
            impl->document_store, next_ref, &next_view);
        if (store_status != VXML_DOCUMENT_STORE_OK ||
            next_view.program == NULL) {
            (void)vxml_document_store_release(
                impl->document_store, &next_ref);
            row_set_synthetic_event(
                row, owner_error_badfetch,
                sizeof(owner_error_badfetch) - 1u);
            return VXML_OK;
        }
        if (!copy_bytes(
                row->current_document_uri,
                impl->max_uri_bytes,
                resolved.document_uri, resolved.document_uri_size,
                &row->current_document_uri_size)) {
            (void)vxml_document_store_release(
                impl->document_store, &next_ref);
            row_set_synthetic_event(
                row, owner_error_badfetch,
                sizeof(owner_error_badfetch) - 1u);
            return VXML_OK;
        }

        {
            const size_t next_hop =
                row->navigation_hops + 1u;
            row_destroy_runtime(row);
            row->document_ref = next_ref;
            row->document_ref_live = true;
            row->program_view = next_view.program;
            row->current_document_uri_size =
                resolved.document_uri_size;
            memcpy(
                row->current_document_uri,
                resolved.document_uri,
                resolved.document_uri_size);
            row->current_document_uri[
                resolved.document_uri_size] = '\0';
            row->navigation_hops = next_hop;
        }

        status = row_init_session(
            row, row->program_view,
            resolved.fragment, resolved.fragment_size,
            false);
        if (status != VXML_OK) {
            row_destroy_runtime(row);
            row_set_synthetic_event(
                row, owner_error_semantic,
                sizeof(owner_error_semantic) - 1u);
            return VXML_OK;
        }
    }

    if (row->session_live) {
        const vxml_session_state state =
            vxml_session_get_state(&row->session);
        if (state == VXML_SESSION_SUBMITTING ||
            state == VXML_SESSION_SCRIPTING)
            return VXML_UNSUPPORTED_FEATURE;
    }
    return VXML_OK;
}

static vxml_status drive_parent_context(
    owner_parent_context *context,
    bool *out_progressed) {
    bool progressed = false;
    vxml_status status;
    if (out_progressed != NULL) *out_progressed = false;
    if (context == NULL || context->session == NULL)
        return VXML_INVALID_STATE;

    status = vxml_session_cmeta_subdialog_run_ready(
        context->session, &progressed);
    if (status != VXML_OK && status != VXML_INVALID_STATE)
        return status;
    if (status == VXML_OK && progressed) {
        if (out_progressed != NULL) *out_progressed = true;
        return VXML_OK;
    }

    status = vxml_session_cmeta_subdialog_prepare(
        context->session, NULL);
    if (status == VXML_INVALID_STATE)
        return VXML_OK;
    if (status != VXML_OK)
        return status;
    status = vxml_session_cmeta_subdialog_commit(
        context->session);
    if (status != VXML_OK)
        return status;
    if (out_progressed != NULL) *out_progressed = true;
    return VXML_OK;
}

static vxml_status build_terminal_completion(
    owner_row *row,
    vxml_cmeta_subdialog_completion_v1 *completion) {
    vxml_cmeta_terminal_kind terminal =
        VXML_CMETA_TERMINAL_NONE;
    vxml_cmeta_exit_kind exit_kind =
        VXML_CMETA_EXIT_EMPTY;
    size_t count;
    size_t index;
    vxml_status status;

    if (row == NULL || completion == NULL ||
        !row->session_live)
        return VXML_INVALID_STATE;
    *completion = (vxml_cmeta_subdialog_completion_v1){
        .abi_version = VXML_CMETA_SUBDIALOG_COMPLETION_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_subdialog_completion_v1),
        .generation = row->generation,
        .global_exit_kind =
            VXML_CMETA_SUBDIALOG_GLOBAL_NATURAL};

    status = vxml_session_cmeta_terminal_kind(
        &row->session, &terminal);
    if (status != VXML_OK)
        return status;

    if (terminal == VXML_CMETA_TERMINAL_NONE) {
        completion->kind =
            VXML_CMETA_SUBDIALOG_GLOBAL_EXIT;
        return VXML_OK;
    }
    if (terminal == VXML_CMETA_TERMINAL_RETURN_EVENT) {
        vxml_cmeta_name_view event = {0};
        status = vxml_session_cmeta_terminal_event(
            &row->session, &event);
        if (status != VXML_OK)
            return status;
        completion->kind =
            VXML_CMETA_SUBDIALOG_RETURN_EVENT;
        completion->event = event;
        return VXML_OK;
    }
    if (terminal == VXML_CMETA_TERMINAL_DISCONNECT) {
        completion->kind =
            VXML_CMETA_SUBDIALOG_GLOBAL_EXIT;
        completion->global_exit_kind =
            VXML_CMETA_SUBDIALOG_GLOBAL_DISCONNECT;
        return VXML_OK;
    }

    status = vxml_session_cmeta_exit_kind(
        &row->session, &exit_kind);
    if (status != VXML_OK)
        return status;
    count = vxml_session_cmeta_exit_count(
        &row->session);
    if (count > row->owner->max_completion_entries)
        return VXML_LIMIT_EXCEEDED;
    for (index = 0u; index < count; ++index) {
        vxml_cmeta_name_view name = {0};
        vxml_cmeta_value_view value = {0};
        status = vxml_session_cmeta_exit_at(
            &row->session, index, &name, &value);
        if (status != VXML_OK)
            return status;
        row->completion_entries[index] =
            (vxml_cmeta_subdialog_result_entry_v1){
                .name = name,
                .value = value};
    }

    if (terminal == VXML_CMETA_TERMINAL_RETURN) {
        if (exit_kind == VXML_CMETA_EXIT_EXPRESSION)
            return VXML_UNSUPPORTED_FEATURE;
        completion->kind =
            VXML_CMETA_SUBDIALOG_RETURN_DATA;
        completion->entries =
            count != 0u ? row->completion_entries : NULL;
        completion->entry_count = count;
        return VXML_OK;
    }
    if (terminal != VXML_CMETA_TERMINAL_EXIT)
        return VXML_INVALID_STRUCTURE;

    completion->kind =
        VXML_CMETA_SUBDIALOG_GLOBAL_EXIT;
    completion->entries =
        count != 0u ? row->completion_entries : NULL;
    completion->entry_count = count;
    switch (exit_kind) {
    case VXML_CMETA_EXIT_EMPTY:
        completion->global_exit_kind =
            VXML_CMETA_SUBDIALOG_GLOBAL_EXIT_EMPTY;
        break;
    case VXML_CMETA_EXIT_EXPRESSION:
        completion->global_exit_kind =
            VXML_CMETA_SUBDIALOG_GLOBAL_EXIT_EXPRESSION;
        break;
    case VXML_CMETA_EXIT_NAMELIST:
        completion->global_exit_kind =
            VXML_CMETA_SUBDIALOG_GLOBAL_EXIT_NAMELIST;
        break;
    default:
        return VXML_INVALID_STRUCTURE;
    }
    return VXML_OK;
}

static vxml_status publish_row_completion(
    owner_row *row,
    bool *out_settled) {
    vxml_cmeta_subdialog_completion_v1 completion = {
        .abi_version = VXML_CMETA_SUBDIALOG_COMPLETION_ABI_V1,
        .struct_size = sizeof(vxml_cmeta_subdialog_completion_v1)};
    vxml_cmeta_subdialog_ingress_result ingress;
    bool parent_progressed = false;
    vxml_status status;

    if (out_settled != NULL) *out_settled = false;
    if (row == NULL || row->parent == NULL ||
        row->parent->session == NULL)
        return VXML_INVALID_STATE;

    if (row->synthetic_event != NULL) {
        completion.generation = row->generation;
        completion.kind =
            VXML_CMETA_SUBDIALOG_RETURN_EVENT;
        completion.event = (vxml_cmeta_name_view){
            row->synthetic_event,
            row->synthetic_event_size};
        completion.global_exit_kind =
            VXML_CMETA_SUBDIALOG_GLOBAL_NATURAL;
    } else {
        status = build_terminal_completion(
            row, &completion);
        if (status == VXML_UNSUPPORTED_FEATURE) {
            row_set_synthetic_event(
                row, owner_error_semantic,
                sizeof(owner_error_semantic) - 1u);
            return publish_row_completion(
                row, out_settled);
        }
        if (status != VXML_OK)
            return status;
    }

    ingress = vxml_session_cmeta_subdialog_try_complete(
        row->parent->session, &completion);
    if (ingress == VXML_CMETA_SUBDIALOG_INGRESS_FULL)
        return VXML_LIMIT_EXCEEDED;
    if (ingress == VXML_CMETA_SUBDIALOG_INGRESS_STALE ||
        ingress == VXML_CMETA_SUBDIALOG_INGRESS_CLOSED) {
        if (ingress == VXML_CMETA_SUBDIALOG_INGRESS_STALE)
            atomic_fetch_add_explicit(
                &row->owner->stale,
                UINT64_C(1), memory_order_relaxed);
        row_clear(row);
        if (out_settled != NULL) *out_settled = true;
        return VXML_OK;
    }
    if (ingress != VXML_CMETA_SUBDIALOG_INGRESS_ACCEPTED)
        return VXML_INVALID_CONTRACT;

    row_destroy_runtime(row);
    status = vxml_session_cmeta_subdialog_run_ready(
        row->parent->session, &parent_progressed);
    row_request_reset(row);
    atomic_store_explicit(
        &row->state, OWNER_ROW_EMPTY, memory_order_release);
    if (out_settled != NULL) *out_settled = true;
    if (status != VXML_OK)
        return status;
    return parent_progressed
        ? VXML_OK : VXML_INVALID_STRUCTURE;
}

static vxml_status progress_active_row(
    owner_row *row,
    bool *out_progressed) {
    bool progressed = false;
    bool settled = false;
    vxml_status status;
    vxml_session_state state;

    if (out_progressed != NULL) *out_progressed = false;
    if (row == NULL)
        return VXML_INVALID_ARGUMENT;
    if (row->synthetic_event != NULL) {
        status = publish_row_completion(
            row, &settled);
        if (status == VXML_OK && settled &&
            out_progressed != NULL)
            *out_progressed = true;
        return status;
    }
    if (!row->session_live)
        return VXML_INVALID_STRUCTURE;

    state = vxml_session_get_state(&row->session);
    if (state == VXML_SESSION_NAVIGATING) {
        status = row_follow_navigation(row);
        if (status != VXML_OK)
            return status;
        progressed = true;
        if (row->synthetic_event != NULL) {
            status = publish_row_completion(
                row, &settled);
            if (status == VXML_OK && settled)
                progressed = true;
            if (out_progressed != NULL)
                *out_progressed = progressed;
            return status;
        }
        state = vxml_session_get_state(&row->session);
    }

    if (state == VXML_SESSION_EXITED) {
        status = publish_row_completion(
            row, &settled);
        if (status == VXML_OK && settled)
            progressed = true;
        if (out_progressed != NULL)
            *out_progressed = progressed;
        return status;
    }
    if (state == VXML_SESSION_FAILED) {
        row_set_synthetic_event(
            row, owner_error_semantic,
            sizeof(owner_error_semantic) - 1u);
        status = publish_row_completion(
            row, &settled);
        if (status == VXML_OK && settled)
            progressed = true;
        if (out_progressed != NULL)
            *out_progressed = progressed;
        return status;
    }
    if (state != VXML_SESSION_RUNNING)
        return VXML_UNSUPPORTED_FEATURE;

    status = drive_parent_context(
        &row->child_context, &progressed);
    if (status != VXML_OK)
        return status;
    state = vxml_session_get_state(&row->session);
    if (state == VXML_SESSION_NAVIGATING) {
        status = row_follow_navigation(row);
        if (status != VXML_OK)
            return status;
        progressed = true;
        state = row->session_live
            ? vxml_session_get_state(&row->session)
            : VXML_SESSION_FAILED;
    }
    if (row->synthetic_event != NULL ||
        state == VXML_SESSION_EXITED ||
        state == VXML_SESSION_FAILED) {
        if (state == VXML_SESSION_FAILED &&
            row->synthetic_event == NULL)
            row_set_synthetic_event(
                row, owner_error_semantic,
                sizeof(owner_error_semantic) - 1u);
        status = publish_row_completion(
            row, &settled);
        if (status == VXML_OK && settled)
            progressed = true;
        if (out_progressed != NULL)
            *out_progressed = progressed;
        return status;
    }
    if (out_progressed != NULL)
        *out_progressed = progressed;
    return VXML_OK;
}

vxml_cmeta_subdialog_owner_config_v1
vxml_cmeta_subdialog_owner_default_config_v1(void) {
    vxml_cmeta_subdialog_owner_config_v1 config;
    memset(&config, 0, sizeof(config));
    config.abi_version =
        VXML_CMETA_SUBDIALOG_OWNER_CONFIG_ABI_V1;
    config.struct_size = sizeof(config);
    config.capacity = 8u;
    config.max_uri_bytes = 4096u;
    config.max_params = 16u;
    config.max_param_bytes = 4096u;
    config.max_completion_entries = 16u;
    config.max_navigation_hops = 32u;
    config.max_nesting_depth = 8u;
    return config;
}

vxml_status vxml_cmeta_subdialog_owner_init(
    vxml_cmeta_subdialog_owner *owner,
    const vxml_cmeta_subdialog_owner_config_v1 *config) {
    owner_impl *impl;
    size_t index;
    const size_t child_prefix =
        offsetof(vxml_cmeta_session_options_v1, max_execution_steps) +
        sizeof(config->child_session_options->max_execution_steps);

    if (owner == NULL || owner->impl != NULL ||
        config == NULL ||
        config->abi_version !=
            VXML_CMETA_SUBDIALOG_OWNER_CONFIG_ABI_V1 ||
        config->struct_size < sizeof(*config) ||
        config->capacity == 0u ||
        config->max_uri_bytes == 0u ||
        config->max_uri_bytes == SIZE_MAX ||
        config->max_params == 0u ||
        config->max_param_bytes == 0u ||
        config->max_completion_entries == 0u ||
        config->max_navigation_hops == 0u ||
        config->max_nesting_depth == 0u ||
        config->document_store == NULL ||
        config->document_store->impl == NULL ||
        config->child_session_options == NULL ||
        config->child_session_options->abi_version !=
            VXML_CMETA_SESSION_OPTIONS_ABI_V1 ||
        config->child_session_options->struct_size < child_prefix ||
        config->child_session_options->initial_root == NULL ||
        config->child_session_options->max_transaction_bytes == 0u ||
        config->child_session_options->max_execution_steps == 0u ||
        config->child_session_options->struct_size <
            offsetof(
                vxml_cmeta_session_options_v1,
                max_subdialog_completion_bytes) +
                sizeof(
                    config->child_session_options
                        ->max_subdialog_completion_bytes) ||
        config->child_session_options->max_subdialog_snapshot_bytes == 0u ||
        config->child_session_options->max_subdialog_completion_bytes == 0u)
        return VXML_INVALID_ARGUMENT;

    impl = (owner_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return VXML_ALLOCATION_FAILED;
    impl->rows = (owner_row *)calloc(
        config->capacity, sizeof(*impl->rows));
    impl->root_document_uri =
        (char *)calloc(config->max_uri_bytes + 1u, 1u);
    impl->resolve_uri =
        (char *)calloc(config->max_uri_bytes + 1u, 1u);
    impl->resolve_fragment =
        (char *)calloc(config->max_uri_bytes + 1u, 1u);
    if (impl->rows == NULL ||
        impl->root_document_uri == NULL ||
        impl->resolve_uri == NULL ||
        impl->resolve_fragment == NULL)
        goto allocation_failure;

    impl->capacity = config->capacity;
    impl->max_uri_bytes = config->max_uri_bytes;
    impl->max_params = config->max_params;
    impl->max_param_bytes = config->max_param_bytes;
    impl->max_completion_entries =
        config->max_completion_entries;
    impl->max_navigation_hops =
        config->max_navigation_hops;
    impl->max_nesting_depth =
        config->max_nesting_depth;
    impl->document_store = config->document_store;
    memset(&impl->child_options, 0, sizeof(impl->child_options));
    memcpy(
        &impl->child_options,
        config->child_session_options,
        config->child_session_options->struct_size <
                sizeof(impl->child_options)
            ? config->child_session_options->struct_size
            : sizeof(impl->child_options));
    impl->child_options.abi_version =
        VXML_CMETA_SESSION_OPTIONS_ABI_V1;
    impl->child_options.struct_size =
        sizeof(impl->child_options);

    impl->root_context.owner = impl;
    impl->root_context.document_uri =
        impl->root_document_uri;
    impl->root_context.depth = 0u;

    for (index = 0u; index < impl->capacity; ++index) {
        owner_row *row = &impl->rows[index];
        row->owner = impl;
        row->slot = index;
        atomic_init(&row->state, OWNER_ROW_EMPTY);
        row->src = (char *)calloc(
            impl->max_uri_bytes + 1u, 1u);
        row->current_document_uri = (char *)calloc(
            impl->max_uri_bytes + 1u, 1u);
        row->params = (vxml_cmeta_subdialog_param_v1 *)calloc(
            impl->max_params, sizeof(*row->params));
        row->param_storage = (char *)calloc(
            impl->max_param_bytes, 1u);
        row->completion_entries =
            (vxml_cmeta_subdialog_result_entry_v1 *)calloc(
                impl->max_completion_entries,
                sizeof(*row->completion_entries));
        if (row->src == NULL ||
            row->current_document_uri == NULL ||
            row->params == NULL ||
            row->param_storage == NULL ||
            row->completion_entries == NULL)
            goto allocation_failure;
        row->child_context.owner = impl;
        row->child_context.depth = 1u;
    }

    atomic_init(&impl->accepted, UINT64_C(0));
    atomic_init(&impl->discarded, UINT64_C(0));
    atomic_init(&impl->stale, UINT64_C(0));
    owner->impl = impl;
    return VXML_OK;

allocation_failure:
    if (impl != NULL) {
        if (impl->rows != NULL) {
            for (index = 0u;
                 index < config->capacity; ++index) {
                free(impl->rows[index].completion_entries);
                free(impl->rows[index].param_storage);
                free(impl->rows[index].params);
                free(impl->rows[index].current_document_uri);
                free(impl->rows[index].src);
            }
        }
        free(impl->resolve_fragment);
        free(impl->resolve_uri);
        free(impl->root_document_uri);
        free(impl->rows);
        free(impl);
    }
    return VXML_ALLOCATION_FAILED;
}

void *vxml_cmeta_subdialog_owner_root_user(
    vxml_cmeta_subdialog_owner *owner) {
    owner_impl *impl =
        owner != NULL ? (owner_impl *)owner->impl : NULL;
    return impl != NULL ? &impl->root_context : NULL;
}

static bool owner_has_live_rows(
    const owner_impl *impl) {
    size_t index;
    if (impl == NULL) return false;
    for (index = 0u; index < impl->capacity; ++index)
        if (atomic_load_explicit(
                &impl->rows[index].state,
                memory_order_acquire) != OWNER_ROW_EMPTY)
            return true;
    return false;
}

vxml_status vxml_cmeta_subdialog_owner_bind_root(
    vxml_cmeta_subdialog_owner *owner,
    vxml_session *parent_session,
    const char *current_document_uri,
    size_t current_document_uri_size) {
    owner_impl *impl =
        owner != NULL ? (owner_impl *)owner->impl : NULL;
    if (impl == NULL || parent_session == NULL ||
        !bytes_valid(
            current_document_uri,
            current_document_uri_size))
        return VXML_INVALID_ARGUMENT;
    if (impl->closed)
        return VXML_CLOSED;
    if (owner_has_live_rows(impl))
        return VXML_INVALID_STATE;
    if (!copy_bytes(
            impl->root_document_uri,
            impl->max_uri_bytes,
            current_document_uri,
            current_document_uri_size,
            &impl->root_context.document_uri_size))
        return VXML_LIMIT_EXCEEDED;
    impl->root_context.session = parent_session;
    impl->root_context.document_uri =
        impl->root_document_uri;
    impl->root_context.depth = 0u;
    return VXML_OK;
}

vxml_status vxml_cmeta_subdialog_owner_run_ready(
    vxml_cmeta_subdialog_owner *owner,
    size_t max_work,
    size_t *out_processed) {
    owner_impl *impl =
        owner != NULL ? (owner_impl *)owner->impl : NULL;
    size_t processed = 0u;
    size_t index;
    bool root_progressed = false;
    vxml_status status;

    if (out_processed != NULL) *out_processed = 0u;
    if (impl == NULL || out_processed == NULL ||
        max_work == 0u)
        return VXML_INVALID_ARGUMENT;

    if (!impl->closed &&
        impl->root_context.session != NULL) {
        status = drive_parent_context(
            &impl->root_context,
            &root_progressed);
        if (status != VXML_OK)
            return status;
        if (root_progressed) {
            ++processed;
            if (processed >= max_work) {
                *out_processed = processed;
                return VXML_OK;
            }
        }
    }

    for (index = 0u;
         index < impl->capacity && processed < max_work;
         ++index) {
        owner_row *row = &impl->rows[index];
        unsigned state = atomic_load_explicit(
            &row->state, memory_order_acquire);
        bool progressed = false;

        if (state == OWNER_ROW_EMPTY ||
            state == OWNER_ROW_RESERVED ||
            state == OWNER_ROW_WRITING)
            continue;
        if (state == OWNER_ROW_CANCELED) {
            row_clear(row);
            ++processed;
            continue;
        }
        if (state == OWNER_ROW_PENDING) {
            unsigned expected = OWNER_ROW_PENDING;
            if (!atomic_compare_exchange_strong_explicit(
                    &row->state, &expected,
                    OWNER_ROW_ACTIVE,
                    memory_order_acq_rel,
                    memory_order_acquire))
                continue;
            status = row_acquire_initial(row);
            if (status != VXML_OK)
                return status;
            progressed = true;
        }
        if (atomic_load_explicit(
                &row->state,
                memory_order_acquire) == OWNER_ROW_ACTIVE) {
            bool active_progressed = false;
            status = progress_active_row(
                row, &active_progressed);
            if (status != VXML_OK)
                return status;
            progressed = progressed || active_progressed;
        }
        if (progressed)
            ++processed;
    }
    *out_processed = processed;
    return VXML_OK;
}

void vxml_cmeta_subdialog_owner_close(
    vxml_cmeta_subdialog_owner *owner) {
    owner_impl *impl =
        owner != NULL ? (owner_impl *)owner->impl : NULL;
    size_t index;
    if (impl == NULL || impl->closed)
        return;
    impl->closed = true;
    for (index = 0u; index < impl->capacity; ++index) {
        owner_row *row = &impl->rows[index];
        unsigned state = atomic_load_explicit(
            &row->state, memory_order_acquire);
        while (state == OWNER_ROW_RESERVED ||
               state == OWNER_ROW_PENDING ||
               state == OWNER_ROW_ACTIVE) {
            unsigned expected = state;
            if (atomic_compare_exchange_weak_explicit(
                    &row->state, &expected,
                    OWNER_ROW_CANCELED,
                    memory_order_acq_rel,
                    memory_order_acquire))
                break;
            state = expected;
        }
    }
}

bool vxml_cmeta_subdialog_owner_is_quiescent(
    const vxml_cmeta_subdialog_owner *owner) {
    const owner_impl *impl =
        owner != NULL
            ? (const owner_impl *)owner->impl : NULL;
    return impl != NULL &&
        !owner_has_live_rows(impl);
}

bool vxml_cmeta_subdialog_owner_get_stats(
    const vxml_cmeta_subdialog_owner *owner,
    vxml_cmeta_subdialog_owner_stats *out_stats) {
    const owner_impl *impl =
        owner != NULL
            ? (const owner_impl *)owner->impl : NULL;
    vxml_cmeta_subdialog_owner_stats stats = {0};
    size_t index;
    if (impl == NULL || out_stats == NULL)
        return false;
    stats.capacity = impl->capacity;
    stats.closed = impl->closed;
    stats.root_bound =
        impl->root_context.session != NULL &&
        impl->root_context.document_uri_size != 0u;
    stats.accepted = atomic_load_explicit(
        &impl->accepted, memory_order_relaxed);
    stats.discarded = atomic_load_explicit(
        &impl->discarded, memory_order_relaxed);
    stats.stale = atomic_load_explicit(
        &impl->stale, memory_order_relaxed);
    for (index = 0u; index < impl->capacity; ++index) {
        const unsigned state = atomic_load_explicit(
            &impl->rows[index].state,
            memory_order_acquire);
        if (state == OWNER_ROW_RESERVED)
            ++stats.reserved;
        else if (state == OWNER_ROW_PENDING)
            ++stats.pending;
        else if (state == OWNER_ROW_CANCELED)
            ++stats.canceled;
        else if (state == OWNER_ROW_ACTIVE)
            ++stats.active;
    }
    *out_stats = stats;
    return true;
}

vxml_status vxml_cmeta_subdialog_owner_destroy(
    vxml_cmeta_subdialog_owner *owner) {
    owner_impl *impl;
    size_t index;
    if (owner == NULL)
        return VXML_INVALID_ARGUMENT;
    impl = (owner_impl *)owner->impl;
    if (impl == NULL)
        return VXML_OK;
    if (!impl->closed || owner_has_live_rows(impl))
        return VXML_INVALID_STATE;
    for (index = 0u; index < impl->capacity; ++index) {
        owner_row *row = &impl->rows[index];
        row_destroy_runtime(row);
        free(row->completion_entries);
        free(row->param_storage);
        free(row->params);
        free(row->current_document_uri);
        free(row->src);
    }
    free(impl->resolve_fragment);
    free(impl->resolve_uri);
    free(impl->root_document_uri);
    free(impl->rows);
    free(impl);
    owner->impl = NULL;
    return VXML_OK;
}
