#include "chttp_event_io_internal.h"

#include <turbo/error_codes.h>
#include <turbo_uuid.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char BASICHTTP_NAME[] = "basichttp";
static const uint64_t KNOWN_CAPABILITIES =
    SCXML_EVENT_IO_CAP_SEND |
    SCXML_EVENT_IO_CAP_DELAYED_SEND |
    SCXML_EVENT_IO_CAP_CANCEL |
    SCXML_EVENT_IO_CAP_PAYLOAD |
    SCXML_EVENT_IO_CAP_CONTENT;

static bool checked_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) return false;
    *out = left + right;
    return true;
}

static bool checked_multiply(size_t left, size_t right, size_t *out) {
    if (out == NULL || (right != 0u && left > SIZE_MAX / right)) return false;
    *out = left * right;
    return true;
}

static bool checked_align(size_t value, size_t alignment, size_t *out) {
    size_t expanded;
    if (alignment == 0u || !checked_add(value, alignment - 1u, &expanded))
        return false;
    *out = expanded - expanded % alignment;
    return true;
}

static bool is_power_of_two(size_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

static bool valid_authority(const char *authority) {
    const unsigned char *cursor = (const unsigned char *)authority;
    if (cursor == NULL || *cursor == 0u) return false;
    while (*cursor != 0u) {
        if (*cursor == '/' || *cursor == '?' || *cursor == '#' ||
            isspace(*cursor))
            return false;
        ++cursor;
    }
    return true;
}

static bool valid_base_path(const char *path) {
    const unsigned char *cursor = (const unsigned char *)path;
    if (cursor == NULL || cursor[0] != '/') return false;
    while (*cursor != 0u) {
        if (*cursor == '?' || *cursor == '#') return false;
        ++cursor;
    }
    return true;
}

static bool adapter_valid(const scxml_event_io_adapter *adapter) {
    if (adapter == NULL || adapter->abi_version != SCXML_ADAPTER_ABI ||
        adapter->struct_size != sizeof(*adapter) ||
        (adapter->capabilities & ~KNOWN_CAPABILITIES) != 0u ||
        (adapter->capabilities & KNOWN_CAPABILITIES) != KNOWN_CAPABILITIES ||
        adapter->prepare_send == NULL || adapter->close == NULL ||
        adapter->is_quiescent == NULL)
        return false;
    if ((adapter->capabilities & SCXML_EVENT_IO_CAP_DELAYED_SEND) != 0u &&
        (adapter->capabilities & SCXML_EVENT_IO_CAP_SEND) == 0u)
        return false;
    if ((adapter->capabilities & SCXML_EVENT_IO_CAP_CANCEL) != 0u &&
        ((adapter->capabilities & SCXML_EVENT_IO_CAP_DELAYED_SEND) == 0u ||
         adapter->prepare_cancel == NULL))
        return false;
    if ((adapter->capabilities &
         (SCXML_EVENT_IO_CAP_PAYLOAD | SCXML_EVENT_IO_CAP_CONTENT)) != 0u &&
        (adapter->capabilities & SCXML_EVENT_IO_CAP_SEND) == 0u)
        return false;
    return true;
}

static int validate_processor_config(
    const scxml_chttp_processor_config_v1 *config,
    size_t *out_authority_size, size_t *out_base_path_size,
    size_t *out_access_stride, size_t *out_total_size) {
    size_t authority_size;
    size_t base_path_size;
    size_t access_stride;
    size_t body_stride;
    size_t endpoint_rows_size;
    size_t endpoint_uri_size;
    size_t egress_rows_size;
    size_t cancel_rows_size;
    size_t egress_strings_per_row;
    size_t egress_strings_size;
    size_t total;
    size_t uri_required;
    if (config == NULL || out_authority_size == NULL ||
        out_base_path_size == NULL || out_access_stride == NULL ||
        out_total_size == NULL ||
        config->abi_version != SCXML_CHTTP_ABI_V1 ||
        config->struct_size < sizeof(*config) ||
        !valid_authority(config->advertised_authority) ||
        !valid_base_path(config->base_path) ||
        config->endpoint_capacity == 0u || config->egress_capacity == 0u ||
        config->max_access_uri_bytes == 0u ||
        config->max_event_name_bytes == 0u ||
        config->max_form_entry_count == 0u ||
        config->max_form_name_bytes == 0u ||
        config->max_form_value_bytes == 0u ||
        config->max_encoded_body_bytes == 0u ||
        config->worker_poll_ms == 0u || config->resolve == NULL ||
        !is_power_of_two(config->server.network.command_capacity) ||
        !is_power_of_two(config->client.network.command_capacity) ||
        config->max_encoded_body_bytes >
            config->client.max_request_body_bytes)
        return TURBO_EINVAL;
    authority_size = strlen(config->advertised_authority);
    base_path_size = strlen(config->base_path);
    if (!checked_add(config->max_access_uri_bytes, 1u, &access_stride) ||
        !checked_add(config->max_encoded_body_bytes, 1u, &body_stride) ||
        !checked_add(7u, authority_size, &uri_required) ||
        !checked_add(uri_required, 6u, &uri_required) ||
        !checked_add(uri_required, base_path_size, &uri_required) ||
        !checked_add(uri_required, 1u + TURBO_UUID_STRING_LENGTH,
                     &uri_required) ||
        uri_required > config->max_access_uri_bytes ||
        !checked_multiply(config->endpoint_capacity,
                          sizeof(scxml_chttp_binding_impl),
                          &endpoint_rows_size) ||
        !checked_multiply(config->endpoint_capacity, access_stride,
                          &endpoint_uri_size) ||
        !checked_multiply(config->egress_capacity,
                          sizeof(scxml_chttp_egress_row),
                          &egress_rows_size) ||
        !checked_multiply(config->egress_capacity,
                          sizeof(scxml_chttp_cancel_ticket),
                          &cancel_rows_size) ||
        !checked_multiply(access_stride, 3u, &egress_strings_per_row) ||
        !checked_add(egress_strings_per_row, body_stride,
                     &egress_strings_per_row) ||
        !checked_multiply(config->egress_capacity, egress_strings_per_row,
                          &egress_strings_size) ||
        !checked_add(endpoint_rows_size, endpoint_uri_size, &total) ||
        !checked_align(total, _Alignof(scxml_chttp_egress_row), &total) ||
        !checked_add(total, egress_rows_size, &total) ||
        !checked_align(total, _Alignof(scxml_chttp_cancel_ticket), &total) ||
        !checked_add(total, cancel_rows_size, &total) ||
        !checked_add(total, egress_strings_size, &total) ||
        !checked_add(total, authority_size + 1u, &total) ||
        !checked_add(total, base_path_size + 1u, &total))
        return TURBO_ERANGE;
    *out_authority_size = authority_size;
    *out_base_path_size = base_path_size;
    *out_access_stride = access_stride;
    *out_total_size = total;
    return TURBO_OK;
}

static void initialize_storage(
    scxml_chttp_processor_impl *impl, size_t total_size,
    const scxml_chttp_processor_config_v1 *config) {
    unsigned char *cursor = (unsigned char *)impl->storage;
    size_t index;
    size_t endpoint_rows_size =
        config->endpoint_capacity * sizeof(scxml_chttp_binding_impl);
    size_t endpoint_uri_size =
        config->endpoint_capacity * impl->access_uri_stride;
    size_t row_offset = endpoint_rows_size + endpoint_uri_size;
    size_t body_stride = config->max_encoded_body_bytes + 1u;
    (void)total_size;
    checked_align(row_offset, _Alignof(scxml_chttp_egress_row), &row_offset);
    impl->bindings = (scxml_chttp_binding_impl *)cursor;
    impl->egress = (scxml_chttp_egress_row *)(cursor + row_offset);
    cursor += row_offset +
        config->egress_capacity * sizeof(scxml_chttp_egress_row);
    {
        size_t offset = (size_t)(cursor - (unsigned char *)impl->storage);
        checked_align(offset, _Alignof(scxml_chttp_cancel_ticket), &offset);
        impl->cancel_tickets = (scxml_chttp_cancel_ticket *)
            ((unsigned char *)impl->storage + offset);
        cursor = (unsigned char *)(impl->cancel_tickets +
            config->egress_capacity);
    }
    for (index = 0u; index < config->endpoint_capacity; ++index) {
        scxml_chttp_binding_impl *binding = &impl->bindings[index];
        binding->processor = impl;
        binding->slot = index;
        binding->access_uri = (char *)impl->storage + endpoint_rows_size +
            index * impl->access_uri_stride;
    }
    for (index = 0u; index < config->egress_capacity; ++index) {
        scxml_chttp_egress_row *row = &impl->egress[index];
        impl->cancel_tickets[index].processor = impl;
        row->processor = impl;
        row->connection_uri = (char *)cursor;
        cursor += impl->egress_string_stride;
        row->authority = (char *)cursor;
        cursor += impl->egress_string_stride;
        row->target = (char *)cursor;
        cursor += impl->egress_string_stride;
        row->body = (char *)cursor;
        cursor += body_stride;
    }
    impl->advertised_authority = (char *)cursor;
    memcpy(impl->advertised_authority, config->advertised_authority,
           impl->advertised_authority_size + 1u);
    cursor += impl->advertised_authority_size + 1u;
    impl->base_path = (char *)cursor;
    memcpy(impl->base_path, config->base_path, impl->base_path_size + 1u);
}

static void cleanup_initialized_processor(scxml_chttp_processor_impl *impl) {
    if (impl == NULL) return;
    if (impl->client.impl != NULL) {
        (void)chttp_async_client_stop(&impl->client, 0u);
        (void)chttp_async_client_destroy(&impl->client);
    }
    if (impl->server.impl != NULL)
        (void)chttp_server_destroy(&impl->server);
    if (impl->condition != NULL) turbo_cond_destroy(&impl->condition);
    if (impl->mutex != NULL) turbo_mutex_destroy(&impl->mutex);
    free(impl->storage);
    free(impl);
}

int scxml_chttp_processor_init(
    scxml_chttp_processor *processor,
    const scxml_chttp_processor_config_v1 *config) {
    scxml_chttp_processor_impl *impl;
    size_t authority_size = 0u;
    size_t base_path_size = 0u;
    size_t access_stride = 0u;
    size_t total_size = 0u;
    int status;
    if (processor == NULL) return TURBO_EINVAL;
    if (processor->impl != NULL) return TURBO_EALREADY;
    status = validate_processor_config(
        config, &authority_size, &base_path_size,
        &access_stride, &total_size);
    if (status != TURBO_OK) return status;
    impl = (scxml_chttp_processor_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return TURBO_ENOMEM;
    impl->storage = calloc(1u, total_size);
    if (impl->storage == NULL) {
        free(impl);
        return TURBO_ENOMEM;
    }
    impl->state = SCXML_CHTTP_PROCESSOR_INITIALIZED;
    impl->worker_status = TURBO_OK;
    impl->worker_poll_ms = config->worker_poll_ms;
    impl->request_timeout_ms = config->request_timeout_ms;
    impl->resolve = config->resolve;
    impl->resolve_user = config->resolve_user;
    impl->advertised_authority_size = authority_size;
    impl->base_path_size = base_path_size;
    impl->endpoint_capacity = config->endpoint_capacity;
    impl->egress_capacity = config->egress_capacity;
    impl->max_access_uri_bytes = config->max_access_uri_bytes;
    impl->max_event_name_bytes = config->max_event_name_bytes;
    impl->max_form_entry_count = config->max_form_entry_count;
    impl->max_form_name_bytes = config->max_form_name_bytes;
    impl->max_form_value_bytes = config->max_form_value_bytes;
    impl->max_encoded_body_bytes = config->max_encoded_body_bytes;
    impl->access_uri_stride = access_stride;
    impl->egress_string_stride = access_stride;
    initialize_storage(impl, total_size, config);
    turbo_mutex_init(&impl->mutex);
    if (impl->mutex == NULL) {
        cleanup_initialized_processor(impl);
        return TURBO_ENOMEM;
    }
    turbo_cond_init(&impl->condition);
    if (impl->condition == NULL) {
        cleanup_initialized_processor(impl);
        return TURBO_ENOMEM;
    }
    status = chttp_server_init(&impl->server, &config->server);
    if (status != TURBO_OK) {
        cleanup_initialized_processor(impl);
        return status;
    }
    status = chttp_async_client_init(&impl->client, &config->client);
    if (status != TURBO_OK) {
        cleanup_initialized_processor(impl);
        return status;
    }
    processor->impl = impl;
    return TURBO_OK;
}

int scxml_chttp_processor_start(scxml_chttp_processor *processor) {
    scxml_chttp_processor_impl *impl;
    int status;
    if (processor == NULL || processor->impl == NULL) return TURBO_EINVAL;
    impl = (scxml_chttp_processor_impl *)processor->impl;
    turbo_mutex_lock(&impl->mutex);
    if (impl->state != SCXML_CHTTP_PROCESSOR_INITIALIZED) {
        turbo_mutex_unlock(&impl->mutex);
        return TURBO_EALREADY;
    }
    turbo_mutex_unlock(&impl->mutex);
    status = chttp_server_start(&impl->server);
    if (status != TURBO_OK) return status;
    status = turbo_thread_create(
        &impl->worker, scxml_chttp_egress_worker, impl);
    if (status != TURBO_OK) {
        (void)chttp_server_stop(&impl->server, 0u);
        return status;
    }
    turbo_mutex_lock(&impl->mutex);
    impl->worker_started = true;
    impl->state = SCXML_CHTTP_PROCESSOR_RUNNING;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
}

int scxml_chttp_processor_stop(
    scxml_chttp_processor *processor, uint32_t timeout_ms) {
    scxml_chttp_processor_impl *impl;
    scxml_chttp_processor_state initial_state;
    int server_status = TURBO_OK;
    int client_status = TURBO_OK;
    if (processor == NULL || processor->impl == NULL) return TURBO_EINVAL;
    impl = (scxml_chttp_processor_impl *)processor->impl;
    turbo_mutex_lock(&impl->mutex);
    if (impl->state == SCXML_CHTTP_PROCESSOR_STOPPED) {
        turbo_mutex_unlock(&impl->mutex);
        return TURBO_EALREADY;
    }
    if (impl->live_bindings != 0u) {
        turbo_mutex_unlock(&impl->mutex);
        return TURBO_EBUSY;
    }
    initial_state = impl->state;
    impl->state = SCXML_CHTTP_PROCESSOR_STOPPING;
    impl->stop_timeout_ms = timeout_ms;
    turbo_mutex_unlock(&impl->mutex);
    if (initial_state == SCXML_CHTTP_PROCESSOR_RUNNING ||
        initial_state == SCXML_CHTTP_PROCESSOR_STOPPING) {
        server_status = chttp_server_stop(&impl->server, timeout_ms);
        if (server_status != TURBO_OK) return server_status;
        impl->server_stopped = true;
    }
    if (impl->worker_started) {
        turbo_mutex_lock(&impl->mutex);
        impl->stop_requested = true;
        turbo_cond_broadcast(&impl->condition);
        turbo_mutex_unlock(&impl->mutex);
        client_status = turbo_thread_join(&impl->worker);
        turbo_thread_destroy(&impl->worker);
        impl->worker_started = false;
        if (client_status == TURBO_OK) client_status = impl->worker_status;
    } else if (!impl->client_destroyed) {
        client_status = chttp_async_client_stop(&impl->client, timeout_ms);
        if (client_status == TURBO_OK) {
            client_status = chttp_async_client_destroy(&impl->client);
            if (client_status == TURBO_OK) impl->client_destroyed = true;
        }
    }
    if (client_status != TURBO_OK) return client_status;
    turbo_mutex_lock(&impl->mutex);
    impl->state = SCXML_CHTTP_PROCESSOR_STOPPED;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
}

int scxml_chttp_processor_destroy(scxml_chttp_processor *processor) {
    scxml_chttp_processor_impl *impl;
    int status;
    if (processor == NULL) return TURBO_EINVAL;
    if (processor->impl == NULL) return TURBO_OK;
    impl = (scxml_chttp_processor_impl *)processor->impl;
    turbo_mutex_lock(&impl->mutex);
    if (impl->state != SCXML_CHTTP_PROCESSOR_STOPPED ||
        impl->live_bindings != 0u) {
        turbo_mutex_unlock(&impl->mutex);
        return TURBO_EBUSY;
    }
    turbo_mutex_unlock(&impl->mutex);
    status = chttp_server_destroy(&impl->server);
    if (status != TURBO_OK) return status;
    turbo_cond_destroy(&impl->condition);
    turbo_mutex_destroy(&impl->mutex);
    free(impl->storage);
    free(impl);
    processor->impl = NULL;
    return TURBO_OK;
}

static bool authority_has_port(const char *authority) {
    const char *colon;
    const char *cursor;
    if (authority == NULL) return false;
    colon = strrchr(authority, ':');
    if (colon == NULL || colon[1] == '\0') return false;
    for (cursor = colon + 1; *cursor != '\0'; ++cursor) {
        if (!isdigit((unsigned char)*cursor)) return false;
    }
    return true;
}

static int reserve_binding_uri(scxml_chttp_binding_impl *binding) {
    scxml_chttp_processor_impl *impl = binding->processor;
    turbo_uuid_t uuid;
    char uuid_text[TURBO_UUID_STRING_SIZE];
    uint16_t port = 0u;
    const bool slash = impl->base_path_size == 0u ||
        impl->base_path[impl->base_path_size - 1u] != '/';
    int written;
    int status = chttp_server_port(&impl->server, &port);
    if (status != TURBO_OK ||
        turbo_uuid_v4_generate(&uuid) != TURBO_OK ||
        turbo_uuid_format(&uuid, uuid_text, sizeof(uuid_text)) != TURBO_OK)
        return status != TURBO_OK ? status : TURBO_EIO;
    if (authority_has_port(impl->advertised_authority)) {
        written = snprintf(
            binding->access_uri, impl->access_uri_stride,
            "http://%s%s%s%s", impl->advertised_authority,
            impl->base_path, slash ? "/" : "", uuid_text);
    } else {
        written = snprintf(
            binding->access_uri, impl->access_uri_stride,
            "http://%s:%u%s%s%s", impl->advertised_authority,
            (unsigned int)port, impl->base_path,
            slash ? "/" : "", uuid_text);
    }
    if (written <= 0 || (size_t)written > impl->max_access_uri_bytes)
        return TURBO_ERANGE;
    binding->access_uri_size = (size_t)written;
    binding->endpoint = binding->access_uri +
        binding->access_uri_size - TURBO_UUID_STRING_LENGTH;
    binding->endpoint_size = TURBO_UUID_STRING_LENGTH;
    return TURBO_OK;
}

int scxml_chttp_binding_init(
    scxml_chttp_binding *binding, scxml_chttp_processor *processor,
    const scxml_chttp_binding_config_v1 *config) {
    scxml_chttp_processor_impl *impl;
    scxml_chttp_binding_impl *selected = NULL;
    size_t index;
    int status;
    if (binding == NULL || processor == NULL || processor->impl == NULL ||
        config == NULL || config->abi_version != SCXML_CHTTP_ABI_V1 ||
        config->struct_size < sizeof(*config) ||
        !adapter_valid(config->scxml_adapter) || config->decode == NULL)
        return TURBO_EINVAL;
    if (binding->impl != NULL) return TURBO_EALREADY;
    impl = (scxml_chttp_processor_impl *)processor->impl;
    turbo_mutex_lock(&impl->mutex);
    if (impl->state != SCXML_CHTTP_PROCESSOR_RUNNING) {
        turbo_mutex_unlock(&impl->mutex);
        return TURBO_ESHUTDOWN;
    }
    for (index = 0u; index < impl->endpoint_capacity; ++index) {
        if (impl->bindings[index].state == SCXML_CHTTP_BINDING_FREE) {
            selected = &impl->bindings[index];
            break;
        }
    }
    if (selected == NULL) {
        turbo_mutex_unlock(&impl->mutex);
        return TURBO_ENOBUFS;
    }
    selected->state = SCXML_CHTTP_BINDING_RESERVED;
    selected->downstream = *config->scxml_adapter;
    selected->downstream_user = config->scxml_adapter_user;
    selected->decode = config->decode;
    selected->decode_user = config->decode_user;
    selected->next_commit_sequence = 1u;
    status = reserve_binding_uri(selected);
    if (status != TURBO_OK) {
        selected->state = SCXML_CHTTP_BINDING_FREE;
        memset(&selected->downstream, 0, sizeof(selected->downstream));
        selected->downstream_user = NULL;
        selected->decode = NULL;
        selected->decode_user = NULL;
        turbo_mutex_unlock(&impl->mutex);
        return status;
    }
    ++impl->live_bindings;
    impl->stats.active_bindings = impl->live_bindings;
    binding->impl = selected;
    turbo_mutex_unlock(&impl->mutex);
    return TURBO_OK;
}

static bool is_basic_http_type(const scxml_send_request *request) {
    const size_t size = sizeof(SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI) - 1u;
    return request != NULL && request->type != NULL &&
        request->type_size == size &&
        memcmp(request->type, SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI, size) == 0;
}

static scxml_adapter_status composite_prepare_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    scxml_chttp_binding_impl *binding =
        (scxml_chttp_binding_impl *)user;
    scxml_event_io_adapter downstream;
    void *downstream_user;
    if (out_error != NULL) *out_error = NULL;
    if (binding == NULL || request == NULL || out_ticket == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    turbo_mutex_lock(&binding->processor->mutex);
    if (binding->state == SCXML_CHTTP_BINDING_CLOSING ||
        binding->state == SCXML_CHTTP_BINDING_QUIESCENT ||
        binding->state == SCXML_CHTTP_BINDING_FREE) {
        turbo_mutex_unlock(&binding->processor->mutex);
        return SCXML_ADAPTER_CLOSED;
    }
    downstream = binding->downstream;
    downstream_user = binding->downstream_user;
    turbo_mutex_unlock(&binding->processor->mutex);
    if (is_basic_http_type(request))
        return scxml_chttp_egress_prepare_send(
            binding, request, out_ticket, out_error);
    return downstream.prepare_send(
        downstream_user, request, out_ticket, out_error);
}

static scxml_adapter_status composite_prepare_cancel(
    void *user, const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    scxml_chttp_binding_impl *binding =
        (scxml_chttp_binding_impl *)user;
    scxml_event_io_adapter downstream;
    void *downstream_user;
    bool handled = false;
    scxml_adapter_status status;
    if (out_error != NULL) *out_error = NULL;
    if (binding == NULL || request == NULL || out_ticket == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    turbo_mutex_lock(&binding->processor->mutex);
    if (binding->state == SCXML_CHTTP_BINDING_CLOSING ||
        binding->state == SCXML_CHTTP_BINDING_QUIESCENT ||
        binding->state == SCXML_CHTTP_BINDING_FREE) {
        turbo_mutex_unlock(&binding->processor->mutex);
        return SCXML_ADAPTER_CLOSED;
    }
    downstream = binding->downstream;
    downstream_user = binding->downstream_user;
    turbo_mutex_unlock(&binding->processor->mutex);
    status = scxml_chttp_egress_prepare_cancel(
        binding, request, out_ticket, out_error, &handled);
    if (handled) return status;
    return downstream.prepare_cancel(
        downstream_user, request, out_ticket, out_error);
}

static void composite_close(void *user) {
    scxml_chttp_binding_impl *binding =
        (scxml_chttp_binding_impl *)user;
    void (*close_callback)(void *) = NULL;
    void *downstream_user = NULL;
    if (binding == NULL || binding->processor == NULL) return;
    turbo_mutex_lock(&binding->processor->mutex);
    if (binding->state != SCXML_CHTTP_BINDING_FREE &&
        !binding->downstream_closed) {
        binding->downstream_closed = true;
        binding->state = SCXML_CHTTP_BINDING_CLOSING;
        scxml_chttp_egress_close_binding_locked(binding);
        close_callback = binding->downstream.close;
        downstream_user = binding->downstream_user;
    }
    turbo_mutex_unlock(&binding->processor->mutex);
    if (close_callback != NULL) close_callback(downstream_user);
}

static bool composite_is_quiescent(void *user) {
    scxml_chttp_binding_impl *binding =
        (scxml_chttp_binding_impl *)user;
    bool (*quiescent_callback)(void *);
    void *downstream_user;
    bool local_quiescent;
    if (binding == NULL || binding->processor == NULL) return false;
    turbo_mutex_lock(&binding->processor->mutex);
    if (binding->state == SCXML_CHTTP_BINDING_QUIESCENT) {
        turbo_mutex_unlock(&binding->processor->mutex);
        return true;
    }
    local_quiescent =
        binding->state == SCXML_CHTTP_BINDING_CLOSING &&
        binding->active_handlers == 0u && binding->outbound_refs == 0u;
    quiescent_callback = binding->downstream.is_quiescent;
    downstream_user = binding->downstream_user;
    turbo_mutex_unlock(&binding->processor->mutex);
    if (!local_quiescent || quiescent_callback == NULL ||
        !quiescent_callback(downstream_user))
        return false;
    turbo_mutex_lock(&binding->processor->mutex);
    local_quiescent =
        binding->state == SCXML_CHTTP_BINDING_CLOSING &&
        binding->active_handlers == 0u && binding->outbound_refs == 0u;
    if (local_quiescent) binding->state = SCXML_CHTTP_BINDING_QUIESCENT;
    turbo_mutex_unlock(&binding->processor->mutex);
    return local_quiescent;
}

static const scxml_event_io_adapter COMPOSITE_ADAPTER = {
    .abi_version = SCXML_ADAPTER_ABI,
    .struct_size = sizeof(scxml_event_io_adapter),
    .capabilities = SCXML_EVENT_IO_CAP_SEND |
        SCXML_EVENT_IO_CAP_DELAYED_SEND |
        SCXML_EVENT_IO_CAP_CANCEL |
        SCXML_EVENT_IO_CAP_PAYLOAD |
        SCXML_EVENT_IO_CAP_CONTENT,
    .prepare_send = composite_prepare_send,
    .prepare_cancel = composite_prepare_cancel,
    .close = composite_close,
    .is_quiescent = composite_is_quiescent};

const scxml_event_io_adapter *scxml_chttp_event_io_adapter(void) {
    return &COMPOSITE_ADAPTER;
}

void *scxml_chttp_binding_adapter_user(scxml_chttp_binding *binding) {
    return binding != NULL ? binding->impl : NULL;
}

bool scxml_chttp_binding_ioprocessor(
    const scxml_chttp_binding *binding,
    scxml_ioprocessor_descriptor *out_descriptor) {
    scxml_chttp_binding_impl *impl;
    if (binding == NULL || binding->impl == NULL || out_descriptor == NULL)
        return false;
    impl = (scxml_chttp_binding_impl *)binding->impl;
    turbo_mutex_lock(&impl->processor->mutex);
    if (impl->state == SCXML_CHTTP_BINDING_FREE) {
        turbo_mutex_unlock(&impl->processor->mutex);
        return false;
    }
    *out_descriptor = (scxml_ioprocessor_descriptor){
        BASICHTTP_NAME, sizeof(BASICHTTP_NAME) - 1u,
        SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI,
        sizeof(SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI) - 1u,
        impl->access_uri, impl->access_uri_size};
    turbo_mutex_unlock(&impl->processor->mutex);
    return true;
}

int scxml_chttp_binding_activate(
    scxml_chttp_binding *binding, scxml_session *session,
    const scxml_program *program) {
    scxml_chttp_binding_impl *impl;
    if (binding == NULL || binding->impl == NULL || session == NULL ||
        session->impl == NULL || program == NULL || program->impl == NULL)
        return TURBO_EINVAL;
    impl = (scxml_chttp_binding_impl *)binding->impl;
    turbo_mutex_lock(&impl->processor->mutex);
    if (impl->state != SCXML_CHTTP_BINDING_RESERVED) {
        turbo_mutex_unlock(&impl->processor->mutex);
        return TURBO_EALREADY;
    }
    impl->session = session;
    impl->program = program;
    impl->state = SCXML_CHTTP_BINDING_ACTIVE;
    turbo_mutex_unlock(&impl->processor->mutex);
    return TURBO_OK;
}

int scxml_chttp_binding_destroy(scxml_chttp_binding *binding) {
    scxml_chttp_binding_impl *impl;
    scxml_chttp_processor_impl *processor;
    char *access_uri;
    size_t slot;
    if (binding == NULL) return TURBO_EINVAL;
    if (binding->impl == NULL) return TURBO_OK;
    impl = (scxml_chttp_binding_impl *)binding->impl;
    if (!composite_is_quiescent(impl)) return TURBO_EBUSY;
    processor = impl->processor;
    turbo_mutex_lock(&processor->mutex);
    if (impl->state != SCXML_CHTTP_BINDING_QUIESCENT ||
        impl->active_handlers != 0u || impl->outbound_refs != 0u) {
        turbo_mutex_unlock(&processor->mutex);
        return TURBO_EBUSY;
    }
    access_uri = impl->access_uri;
    slot = impl->slot;
    memset(impl, 0, sizeof(*impl));
    impl->processor = processor;
    impl->access_uri = access_uri;
    impl->slot = slot;
    --processor->live_bindings;
    processor->stats.active_bindings = processor->live_bindings;
    binding->impl = NULL;
    turbo_mutex_unlock(&processor->mutex);
    return TURBO_OK;
}

bool scxml_chttp_processor_get_stats(
    const scxml_chttp_processor *processor,
    scxml_chttp_processor_stats *out_stats) {
    scxml_chttp_processor_impl *impl;
    if (processor == NULL || processor->impl == NULL || out_stats == NULL)
        return false;
    impl = (scxml_chttp_processor_impl *)processor->impl;
    turbo_mutex_lock(&impl->mutex);
    *out_stats = impl->stats;
    turbo_mutex_unlock(&impl->mutex);
    return true;
}
