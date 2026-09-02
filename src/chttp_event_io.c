#include "chttp_event_io_internal.h"

#include <turbo/error_codes.h>
#include <turbo/platform.h>

#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static const char BASIC_HTTP_NAME[] = "basichttp";
static atomic_bool FAIL_NEXT_WORKER_CREATE;
static atomic_uint DELAY_NEXT_WORKER_EXIT_MS;
static atomic_bool FORCE_NEXT_SERVER_TERMINAL_ERROR;

typedef struct scxml_chttp_storage_layout {
    size_t total;
    size_t authority;
    size_t base_path;
    size_t route_path;
    size_t ingress_text;
    size_t ingress_entries;
    size_t endpoints;
    size_t endpoint_uris;
    size_t egress;
    size_t cancel_tickets;
    size_t connection_uris;
    size_t authorities;
    size_t targets;
    size_t send_ids;
    size_t bodies;
} scxml_chttp_storage_layout;

static bool checked_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) return false;
    *out = left + right;
    return true;
}

static bool checked_multiply(size_t left, size_t right, size_t *out) {
    if (out == NULL || (left != 0u && right > SIZE_MAX / left)) return false;
    *out = left * right;
    return true;
}

static bool layout_take(
    size_t *total, size_t alignment, size_t count, size_t width,
    size_t *out_offset) {
    size_t aligned;
    size_t bytes;
    if (total == NULL || out_offset == NULL || alignment == 0u ||
        !checked_add(*total, alignment - 1u, &aligned))
        return false;
    aligned -= aligned % alignment;
    if (!checked_multiply(count, width, &bytes) ||
        !checked_add(aligned, bytes, total))
        return false;
    *out_offset = aligned;
    return true;
}

static bool bounded_cstring_size(
    const char *text, size_t limit, size_t *out_size) {
    size_t size;
    if (text == NULL || limit == 0u || out_size == NULL) return false;
    for (size = 0u; size <= limit; ++size) {
        if (text[size] == '\0') {
            if (size == 0u) return false;
            *out_size = size;
            return true;
        }
    }
    return false;
}

static bool is_power_of_two(size_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

static bool text_equal(
    const char *left, size_t left_size, const char *right,
    size_t right_size) {
    return left_size == right_size &&
        (left_size == 0u ||
         (left != NULL && right != NULL &&
          memcmp(left, right, left_size) == 0));
}

static bool processor_config_valid(
    const scxml_chttp_processor_config_v1 *config,
    size_t *out_authority_size, size_t *out_base_path_size) {
    size_t index;
    size_t endpoint_target_size;
    if (config == NULL || out_authority_size == NULL ||
        out_base_path_size == NULL ||
        config->abi_version != SCXML_CHTTP_ABI_V1 ||
        config->struct_size < sizeof(*config) ||
        config->endpoint_capacity == 0u || config->egress_capacity == 0u ||
        config->max_access_uri_bytes == 0u ||
        config->max_event_name_bytes == 0u ||
        config->max_event_name_bytes > SCXML_EVENT_METADATA_CAPACITY ||
        config->max_form_entry_count == 0u ||
        config->max_form_name_bytes == 0u ||
        config->max_form_value_bytes == 0u ||
        config->max_encoded_body_bytes == 0u ||
        config->server.route_capacity == 0u ||
        config->server.max_route_param_count == 0u ||
        config->server.max_route_param_bytes <
            (sizeof("endpoint") - 1u) +
                (TURBO_UUID_STRING_SIZE - 1u) + 2u ||
        config->request_timeout_ms == 0u || config->worker_poll_ms == 0u ||
        config->resolve == NULL ||
        config->max_encoded_body_bytes >
            config->client.max_request_body_bytes ||
        config->max_encoded_body_bytes >
            config->server.max_request_body_bytes ||
        !is_power_of_two(config->server.network.command_capacity) ||
        !is_power_of_two(config->client.network.command_capacity) ||
        !bounded_cstring_size(
            config->advertised_authority,
            config->max_access_uri_bytes, out_authority_size) ||
        !bounded_cstring_size(
            config->base_path,
            config->max_access_uri_bytes, out_base_path_size) ||
        config->base_path[0] != '/')
        return false;
    endpoint_target_size = *out_base_path_size +
        (config->base_path[*out_base_path_size - 1u] == '/' ? 0u : 1u) +
        (TURBO_UUID_STRING_SIZE - 1u);
    if (config->server.max_target_bytes < endpoint_target_size)
        return false;
    for (index = 0u; index < *out_authority_size; ++index) {
        if (config->advertised_authority[index] == '/' ||
            config->advertised_authority[index] == '?' ||
            config->advertised_authority[index] == '#')
            return false;
    }
    for (index = 0u; index < *out_base_path_size; ++index) {
        if (config->base_path[index] == '?' || config->base_path[index] == '#')
            return false;
    }
    return true;
}

static bool calculate_storage_layout(
    const scxml_chttp_processor_config_v1 *config,
    size_t authority_size, size_t base_path_size,
    scxml_chttp_storage_layout *out) {
    scxml_chttp_storage_layout layout = {0};
    size_t authority_bytes;
    size_t base_path_bytes;
    size_t route_path_bytes;
    size_t ingress_text_bytes;
    size_t string_stride;
    if (config == NULL || out == NULL ||
        !checked_add(authority_size, 1u, &authority_bytes) ||
        !checked_add(base_path_size, 1u, &base_path_bytes) ||
        !checked_add(base_path_size,
                     config->base_path[base_path_size - 1u] == '/'
                         ? sizeof(":endpoint")
                         : sizeof("/:endpoint"),
                     &route_path_bytes) ||
        !checked_add(config->server.max_request_body_bytes, 1u,
                     &ingress_text_bytes) ||
        !checked_add(config->max_access_uri_bytes, 1u, &string_stride) ||
        !layout_take(&layout.total, 1u, 1u, authority_bytes,
                     &layout.authority) ||
        !layout_take(&layout.total, 1u, 1u, base_path_bytes,
                     &layout.base_path) ||
        !layout_take(&layout.total, 1u, 1u, route_path_bytes,
                     &layout.route_path) ||
        !layout_take(&layout.total, 1u, 1u, ingress_text_bytes,
                     &layout.ingress_text) ||
        !layout_take(&layout.total,
                     _Alignof(scxml_chttp_form_entry_view),
                     config->max_form_entry_count,
                     sizeof(scxml_chttp_form_entry_view),
                     &layout.ingress_entries) ||
        !layout_take(&layout.total, _Alignof(scxml_chttp_endpoint_row),
                     config->endpoint_capacity,
                     sizeof(scxml_chttp_endpoint_row), &layout.endpoints) ||
        !layout_take(&layout.total, 1u, config->endpoint_capacity,
                     string_stride, &layout.endpoint_uris) ||
        !layout_take(&layout.total, _Alignof(scxml_chttp_egress_row),
                     config->egress_capacity,
                     sizeof(scxml_chttp_egress_row), &layout.egress) ||
        !layout_take(&layout.total, _Alignof(scxml_chttp_cancel_ticket),
                     config->egress_capacity,
                     sizeof(scxml_chttp_cancel_ticket),
                     &layout.cancel_tickets) ||
        !layout_take(&layout.total, 1u, config->egress_capacity,
                     string_stride, &layout.connection_uris) ||
        !layout_take(&layout.total, 1u, config->egress_capacity,
                     string_stride, &layout.authorities) ||
        !layout_take(&layout.total, 1u, config->egress_capacity,
                     string_stride, &layout.targets) ||
        !layout_take(&layout.total, 1u, config->egress_capacity,
                     SCXML_EVENT_METADATA_CAPACITY + 1u,
                     &layout.send_ids) ||
        !layout_take(&layout.total, 1u, config->egress_capacity,
                     config->max_encoded_body_bytes, &layout.bodies))
        return false;
    *out = layout;
    return true;
}

static bool downstream_adapter_valid(
    const scxml_event_io_adapter *adapter) {
    const uint64_t known = SCXML_EVENT_IO_CAP_SEND |
        SCXML_EVENT_IO_CAP_DELAYED_SEND | SCXML_EVENT_IO_CAP_CANCEL |
        SCXML_EVENT_IO_CAP_PAYLOAD | SCXML_EVENT_IO_CAP_CONTENT;
    const uint64_t required = SCXML_EVENT_IO_CAP_SEND |
        SCXML_EVENT_IO_CAP_PAYLOAD | SCXML_EVENT_IO_CAP_CONTENT;
    const uint64_t optional_pair = SCXML_EVENT_IO_CAP_DELAYED_SEND |
        SCXML_EVENT_IO_CAP_CANCEL;
    return adapter != NULL && adapter->abi_version == SCXML_ADAPTER_ABI &&
        adapter->struct_size == sizeof(*adapter) &&
        (adapter->capabilities & ~known) == 0u &&
        (adapter->capabilities & required) == required &&
        adapter->prepare_send != NULL && adapter->close != NULL &&
        adapter->is_quiescent != NULL &&
        ((adapter->capabilities & optional_pair) == 0u ||
         ((adapter->capabilities & optional_pair) == optional_pair &&
          adapter->prepare_cancel != NULL));
}

static uint64_t stop_deadline(uint32_t timeout_ms) {
    const uint64_t now = turbo_monotonic_ms();
    return timeout_ms == 0u ? UINT64_MAX :
        now > UINT64_MAX - timeout_ms ? UINT64_MAX : now + timeout_ms;
}

static bool remaining_timeout_ms(
    uint64_t deadline_ms, uint32_t *out_timeout_ms) {
    uint64_t now;
    uint64_t remaining;
    if (out_timeout_ms == NULL) return false;
    if (deadline_ms == UINT64_MAX) {
        *out_timeout_ms = 0u;
        return true;
    }
    now = turbo_monotonic_ms();
    if (now >= deadline_ms) return false;
    remaining = deadline_ms - now;
    *out_timeout_ms = remaining > UINT32_MAX
        ? UINT32_MAX : (uint32_t)remaining;
    if (*out_timeout_ms == 0u) *out_timeout_ms = 1u;
    return true;
}

void scxml_chttp_test_fail_next_worker_create(void) {
    atomic_store_explicit(
        &FAIL_NEXT_WORKER_CREATE, true, memory_order_release);
}

void scxml_chttp_test_delay_next_worker_exit(uint32_t delay_ms) {
    atomic_store_explicit(
        &DELAY_NEXT_WORKER_EXIT_MS, delay_ms, memory_order_release);
}

void scxml_chttp_test_force_next_server_terminal_error(void) {
    atomic_store_explicit(
        &FORCE_NEXT_SERVER_TERMINAL_ERROR, true, memory_order_release);
}

static bool authority_has_port(const char *authority, size_t size) {
    const char *close;
    if (size == 0u) return false;
    if (authority[0] == '[') {
        close = (const char *)memchr(authority, ']', size);
        return close != NULL && (size_t)(close - authority) + 1u < size &&
            close[1] == ':';
    }
    return memchr(authority, ':', size) != NULL;
}

static int build_access_uri(
    scxml_chttp_processor_impl *processor,
    scxml_chttp_endpoint_row *endpoint) {
    const bool has_port = authority_has_port(
        processor->advertised_authority,
        processor->advertised_authority_size);
    const bool has_slash =
        processor->base_path[processor->base_path_size - 1u] == '/';
    const int written = has_port
        ? snprintf(
              endpoint->access_uri,
              processor->config.max_access_uri_bytes + 1u,
              "http://%s%s%s%s", processor->advertised_authority,
              processor->base_path, has_slash ? "" : "/",
              endpoint->endpoint)
        : snprintf(
              endpoint->access_uri,
              processor->config.max_access_uri_bytes + 1u,
              "http://%s:%u%s%s%s", processor->advertised_authority,
              (unsigned int)processor->bound_port, processor->base_path,
              has_slash ? "" : "/", endpoint->endpoint);
    if (written < 0 ||
        (size_t)written > processor->config.max_access_uri_bytes)
        return TURBO_ENOSPC;
    endpoint->access_uri_size = (size_t)written;
    return TURBO_OK;
}

static void processor_worker(void *user) {
    scxml_chttp_processor_impl *processor =
        (scxml_chttp_processor_impl *)user;
    for (;;) {
        size_t completions = 0u;
        int poll_status;
        turbo_mutex_lock(&processor->lock);
        if (processor->stop_requested) {
            const uint64_t generation = processor->stop_generation;
            const uint64_t deadline_ms = processor->stop_deadline_ms;
            bool client_stopped = processor->client_stopped;
            bool client_destroyed = processor->client_destroyed;
            uint32_t remaining_ms;
            int stop_status;
            turbo_mutex_unlock(&processor->lock);
            {
                const uint32_t delay_ms = atomic_exchange_explicit(
                    &DELAY_NEXT_WORKER_EXIT_MS, 0u, memory_order_acq_rel);
                if (delay_ms != 0u) turbo_sleep_ms(delay_ms);
            }
            if (!client_stopped) {
                stop_status = remaining_timeout_ms(
                                  deadline_ms, &remaining_ms)
                    ? chttp_async_client_stop(
                          &processor->client, remaining_ms)
                    : TURBO_ETIMEDOUT;
                if (stop_status == TURBO_OK) client_stopped = true;
            } else {
                stop_status = TURBO_OK;
            }
            if (stop_status == TURBO_OK && !client_destroyed) {
                stop_status = chttp_async_client_destroy(&processor->client);
                if (stop_status == TURBO_OK) client_destroyed = true;
            }
            turbo_mutex_lock(&processor->lock);
            processor->client_stopped = client_stopped;
            processor->client_destroyed = client_destroyed;
            if (stop_status == TURBO_OK) {
                processor->worker_stop_status = TURBO_OK;
                processor->stop_attempt_complete = true;
                processor->worker_exited = true;
                turbo_cond_broadcast(&processor->wake);
                turbo_mutex_unlock(&processor->lock);
                return;
            }
            if (processor->stop_generation == generation) {
                processor->worker_stop_status = stop_status;
                processor->stop_attempt_complete = true;
                turbo_cond_broadcast(&processor->wake);
                while (processor->stop_generation == generation)
                    turbo_cond_wait(&processor->wake, &processor->lock);
            }
            turbo_mutex_unlock(&processor->lock);
            continue;
        }
        turbo_mutex_unlock(&processor->lock);
        while (scxml_chttp_egress_cancel_one(processor)) {}
        (void)scxml_chttp_egress_submit_one(processor);
        poll_status = chttp_async_client_poll(
            &processor->client, 0u, &completions);
        if (poll_status != TURBO_OK && poll_status != TURBO_ESHUTDOWN) {
            turbo_mutex_lock(&processor->lock);
            ++processor->invariant_failures;
            turbo_mutex_unlock(&processor->lock);
        }
        turbo_mutex_lock(&processor->lock);
        if (!processor->stop_requested)
            (void)turbo_cond_timedwait(
                &processor->wake, &processor->lock,
                (uint64_t)processor->config.worker_poll_ms * UINT64_C(1000000));
        turbo_mutex_unlock(&processor->lock);
    }
}

int scxml_chttp_processor_init(
    scxml_chttp_processor *processor,
    const scxml_chttp_processor_config_v1 *config) {
    scxml_chttp_processor_impl *impl = NULL;
    scxml_chttp_storage_layout layout;
    unsigned char *storage;
    size_t authority_size;
    size_t base_path_size;
    size_t string_stride;
    size_t index;
    int status;
    if (processor == NULL || config == NULL) return TURBO_EINVAL;
    if (processor->impl != NULL) return TURBO_EALREADY;
    if (!processor_config_valid(
            config, &authority_size, &base_path_size))
        return TURBO_EINVAL;
    if (!calculate_storage_layout(
            config, authority_size, base_path_size, &layout))
        return TURBO_ERANGE;
    impl = (scxml_chttp_processor_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return TURBO_ENOMEM;
    impl->storage = calloc(1u, layout.total);
    if (impl->storage == NULL) {
        free(impl);
        return TURBO_ENOMEM;
    }
    storage = (unsigned char *)impl->storage;
    impl->config = *config;
    impl->advertised_authority = (char *)(storage + layout.authority);
    impl->base_path = (char *)(storage + layout.base_path);
    impl->route_path = (char *)(storage + layout.route_path);
    impl->ingress_text = (char *)(storage + layout.ingress_text);
    impl->ingress_entries =
        (scxml_chttp_form_entry_view *)(void *)(
            storage + layout.ingress_entries);
    impl->endpoints =
        (scxml_chttp_endpoint_row *)(void *)(storage + layout.endpoints);
    impl->egress =
        (scxml_chttp_egress_row *)(void *)(storage + layout.egress);
    impl->cancel_tickets = (scxml_chttp_cancel_ticket *)(void *)(
        storage + layout.cancel_tickets);
    impl->advertised_authority_size = authority_size;
    impl->base_path_size = base_path_size;
    memcpy(impl->advertised_authority, config->advertised_authority,
           authority_size + 1u);
    memcpy(impl->base_path, config->base_path, base_path_size + 1u);
    impl->ingress_text_capacity =
        config->server.max_request_body_bytes + 1u;
    (void)snprintf(
        impl->route_path, base_path_size +
            (impl->base_path[base_path_size - 1u] == '/'
                 ? sizeof(":endpoint") : sizeof("/:endpoint")),
        impl->base_path[base_path_size - 1u] == '/'
            ? "%s:endpoint" : "%s/:endpoint",
        impl->base_path);
    impl->config.advertised_authority = impl->advertised_authority;
    impl->config.base_path = impl->base_path;
    string_stride = config->max_access_uri_bytes + 1u;
    for (index = 0u; index < config->endpoint_capacity; ++index)
        impl->endpoints[index].access_uri =
            (char *)(storage + layout.endpoint_uris + index * string_stride);
    for (index = 0u; index < config->egress_capacity; ++index) {
        impl->egress[index].processor = impl;
        impl->egress[index].connection_uri =
            (char *)(storage + layout.connection_uris + index * string_stride);
        impl->egress[index].authority =
            (char *)(storage + layout.authorities + index * string_stride);
        impl->egress[index].target =
            (char *)(storage + layout.targets + index * string_stride);
        impl->egress[index].send_id =
            (char *)(storage + layout.send_ids +
                     index * (SCXML_EVENT_METADATA_CAPACITY + 1u));
        impl->egress[index].body =
            (char *)(storage + layout.bodies +
                     index * config->max_encoded_body_bytes);
        impl->cancel_tickets[index].processor = impl;
    }
    turbo_mutex_init(&impl->lock);
    if (impl->lock == NULL) {
        free(impl->storage);
        free(impl);
        return TURBO_ENOMEM;
    }
    turbo_cond_init(&impl->wake);
    if (impl->wake == NULL) {
        turbo_mutex_destroy(&impl->lock);
        free(impl->storage);
        free(impl);
        return TURBO_ENOMEM;
    }
    status = chttp_server_init(&impl->server, &config->server);
    if (status != TURBO_OK) goto fail;
    status = scxml_chttp_ingress_register(impl);
    if (status != TURBO_OK) {
        (void)chttp_server_destroy(&impl->server);
        goto fail;
    }
    status = chttp_async_client_init(&impl->client, &config->client);
    if (status != TURBO_OK) {
        (void)chttp_server_destroy(&impl->server);
        goto fail;
    }
    impl->state = SCXML_CHTTP_PROCESSOR_INITIALIZED;
    processor->impl = impl;
    return TURBO_OK;

fail:
    turbo_cond_destroy(&impl->wake);
    turbo_mutex_destroy(&impl->lock);
    free(impl->storage);
    free(impl);
    return status;
}

int scxml_chttp_processor_start(scxml_chttp_processor *processor) {
    scxml_chttp_processor_impl *impl;
    int status;
    if (processor == NULL || processor->impl == NULL) return TURBO_EINVAL;
    impl = (scxml_chttp_processor_impl *)processor->impl;
    turbo_mutex_lock(&impl->lock);
    if (impl->state != SCXML_CHTTP_PROCESSOR_INITIALIZED) {
        status = impl->state == SCXML_CHTTP_PROCESSOR_RUNNING
            ? TURBO_EALREADY : TURBO_EBUSY;
        turbo_mutex_unlock(&impl->lock);
        return status;
    }
    status = chttp_server_start(&impl->server);
    if (status != TURBO_OK) {
        turbo_mutex_unlock(&impl->lock);
        return status;
    }
    impl->server_started = true;
    status = chttp_server_port(&impl->server, &impl->bound_port);
    if (status != TURBO_OK) goto stop_server;
    impl->stop_requested = false;
    impl->worker_exited = false;
    status = atomic_exchange_explicit(
                 &FAIL_NEXT_WORKER_CREATE, false, memory_order_acq_rel)
        ? TURBO_ENOMEM
        : turbo_thread_create(&impl->worker, processor_worker, impl);
    if (status != TURBO_OK) goto stop_server;
    impl->worker_started = true;
    impl->state = SCXML_CHTTP_PROCESSOR_RUNNING;
    turbo_mutex_unlock(&impl->lock);
    return TURBO_OK;

stop_server:
    {
        const int server_status = chttp_server_stop(
            &impl->server, impl->config.request_timeout_ms);
        const int client_status = chttp_async_client_stop(
            &impl->client, impl->config.request_timeout_ms);
        int destroy_status = TURBO_EBUSY;
        if (server_status == TURBO_OK) impl->server_stopped = true;
        if (client_status == TURBO_OK) {
            impl->client_stopped = true;
            destroy_status = chttp_async_client_destroy(&impl->client);
            if (destroy_status == TURBO_OK) impl->client_destroyed = true;
        }
        impl->state = server_status == TURBO_OK &&
                      destroy_status == TURBO_OK
            ? SCXML_CHTTP_PROCESSOR_STOPPED
            : SCXML_CHTTP_PROCESSOR_STOPPING;
    }
    turbo_mutex_unlock(&impl->lock);
    return status;
}

int scxml_chttp_processor_stop(
    scxml_chttp_processor *processor, uint32_t timeout_ms) {
    scxml_chttp_processor_impl *impl;
    const uint64_t deadline_ms = stop_deadline(timeout_ms);
    uint32_t remaining_ms;
    int first_status = TURBO_OK;
    int status;
    if (processor == NULL || processor->impl == NULL) return TURBO_EINVAL;
    impl = (scxml_chttp_processor_impl *)processor->impl;
    turbo_mutex_lock(&impl->lock);
    if (impl->live_bindings != 0u) {
        turbo_mutex_unlock(&impl->lock);
        return TURBO_EBUSY;
    }
    if (impl->stop_active) {
        turbo_mutex_unlock(&impl->lock);
        return TURBO_EBUSY;
    }
    if (impl->state == SCXML_CHTTP_PROCESSOR_STOPPED) {
        turbo_mutex_unlock(&impl->lock);
        return TURBO_EALREADY;
    }
    if (impl->state == SCXML_CHTTP_PROCESSOR_INITIALIZED) {
        turbo_mutex_unlock(&impl->lock);
        return TURBO_EBUSY;
    }
    impl->stop_active = true;
    impl->state = SCXML_CHTTP_PROCESSOR_STOPPING;
    turbo_mutex_unlock(&impl->lock);
    if (impl->server_started && !impl->server_stopped) {
        chttp_server_stats server_stats;
        const bool force_terminal_error = atomic_exchange_explicit(
            &FORCE_NEXT_SERVER_TERMINAL_ERROR, false,
            memory_order_acq_rel);
        if (!remaining_timeout_ms(deadline_ms, &remaining_ms)) {
            status = TURBO_ETIMEDOUT;
            goto finish;
        }
        status = chttp_server_stop(&impl->server, remaining_ms);
        if (status == TURBO_OK && force_terminal_error)
            status = TURBO_EIO;
        if (status != TURBO_OK) {
            if (chttp_server_get_stats(&impl->server, &server_stats) !=
                    TURBO_OK ||
                server_stats.running || server_stats.stopping)
                goto finish;
            if (impl->shutdown_terminal_status == TURBO_OK)
                impl->shutdown_terminal_status = status;
            first_status = impl->shutdown_terminal_status;
        }
        impl->server_stopped = true;
    }
    if (impl->worker_started) {
        turbo_mutex_lock(&impl->lock);
        if (!impl->worker_exited) {
            ++impl->stop_generation;
            if (impl->stop_generation == 0u) ++impl->stop_generation;
            impl->stop_deadline_ms = deadline_ms;
            impl->stop_attempt_complete = false;
            impl->stop_requested = true;
            turbo_cond_broadcast(&impl->wake);
        }
        while (!impl->stop_attempt_complete && !impl->worker_exited) {
            if (deadline_ms == UINT64_MAX) {
                turbo_cond_wait(&impl->wake, &impl->lock);
            } else {
                if (!remaining_timeout_ms(deadline_ms, &remaining_ms)) {
                    turbo_mutex_unlock(&impl->lock);
                    status = TURBO_ETIMEDOUT;
                    goto finish;
                }
                status = turbo_cond_timedwait(
                    &impl->wake, &impl->lock,
                    (uint64_t)remaining_ms * UINT64_C(1000000));
                if (status != TURBO_OK && !impl->stop_attempt_complete &&
                    !impl->worker_exited) {
                    turbo_mutex_unlock(&impl->lock);
                    status = TURBO_ETIMEDOUT;
                    goto finish;
                }
            }
        }
        status = impl->worker_exited
            ? TURBO_OK : impl->worker_stop_status;
        turbo_mutex_unlock(&impl->lock);
        if (status != TURBO_OK) goto finish;
        status = turbo_thread_join(&impl->worker);
        if (status != TURBO_OK) goto finish;
        turbo_thread_destroy(&impl->worker);
        impl->worker_started = false;
    }
    status = first_status;

finish:
    turbo_mutex_lock(&impl->lock);
    if (impl->server_stopped && !impl->worker_started &&
        impl->client_destroyed) {
        impl->state = SCXML_CHTTP_PROCESSOR_STOPPED;
        if (impl->shutdown_terminal_status != TURBO_OK)
            status = impl->shutdown_terminal_status;
    }
    impl->stop_active = false;
    turbo_mutex_unlock(&impl->lock);
    return status;
}

int scxml_chttp_processor_destroy(scxml_chttp_processor *processor) {
    scxml_chttp_processor_impl *impl;
    int client_status;
    int server_status;
    if (processor == NULL) return TURBO_EINVAL;
    if (processor->impl == NULL) return TURBO_OK;
    impl = (scxml_chttp_processor_impl *)processor->impl;
    turbo_mutex_lock(&impl->lock);
    if (impl->state != SCXML_CHTTP_PROCESSOR_STOPPED ||
        impl->live_bindings != 0u) {
        turbo_mutex_unlock(&impl->lock);
        return TURBO_EBUSY;
    }
    turbo_mutex_unlock(&impl->lock);
    client_status = impl->client_destroyed
        ? TURBO_OK : chttp_async_client_destroy(&impl->client);
    server_status = chttp_server_destroy(&impl->server);
    if (client_status != TURBO_OK) return client_status;
    if (server_status != TURBO_OK) return server_status;
    turbo_cond_destroy(&impl->wake);
    turbo_mutex_destroy(&impl->lock);
    free(impl->storage);
    free(impl);
    processor->impl = NULL;
    return TURBO_OK;
}

static scxml_adapter_status composite_prepare_send(
    void *user, const scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    scxml_chttp_binding_impl *binding = (scxml_chttp_binding_impl *)user;
    bool open;
    if (binding == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    memset(out_ticket, 0, sizeof(*out_ticket));
    *out_error = NULL;
    turbo_mutex_lock(&binding->processor->lock);
    open = binding->state == SCXML_CHTTP_BINDING_RESERVED ||
        binding->state == SCXML_CHTTP_BINDING_ACTIVE;
    turbo_mutex_unlock(&binding->processor->lock);
    if (!open) return SCXML_ADAPTER_CLOSED;
    if (text_equal(
            request->type, request->type_size,
            SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI,
            sizeof(SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI) - 1u))
        return scxml_chttp_egress_prepare_send(
            binding, request, out_ticket, out_error);
    return binding->downstream.prepare_send(
        binding->downstream_user, request, out_ticket, out_error);
}

static scxml_adapter_status composite_prepare_cancel(
    void *user, const scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    scxml_chttp_binding_impl *binding = (scxml_chttp_binding_impl *)user;
    bool open;
    bool handled = false;
    scxml_adapter_status status;
    if (binding == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    memset(out_ticket, 0, sizeof(*out_ticket));
    *out_error = NULL;
    turbo_mutex_lock(&binding->processor->lock);
    open = binding->state == SCXML_CHTTP_BINDING_RESERVED ||
        binding->state == SCXML_CHTTP_BINDING_ACTIVE;
    turbo_mutex_unlock(&binding->processor->lock);
    if (!open) return SCXML_ADAPTER_CLOSED;
    if ((binding->downstream.capabilities & SCXML_EVENT_IO_CAP_CANCEL) == 0u ||
        binding->downstream.prepare_cancel == NULL)
        return SCXML_ADAPTER_INVALID_CONTRACT;
    status = scxml_chttp_egress_prepare_cancel(
        binding, request, out_ticket, out_error, &handled);
    if (handled || status != SCXML_ADAPTER_ACCEPTED) return status;
    return binding->downstream.prepare_cancel(
        binding->downstream_user, request, out_ticket, out_error);
}

static void composite_close(void *user) {
    scxml_chttp_binding_impl *binding = (scxml_chttp_binding_impl *)user;
    bool close_downstream = false;
    if (binding == NULL || binding->processor == NULL) return;
    turbo_mutex_lock(&binding->processor->lock);
    if (binding->state == SCXML_CHTTP_BINDING_RESERVED ||
        binding->state == SCXML_CHTTP_BINDING_ACTIVE) {
        binding->state = SCXML_CHTTP_BINDING_CLOSING;
        scxml_chttp_egress_close_binding_locked(binding);
    }
    if (!binding->downstream_close_called) {
        binding->downstream_close_called = true;
        close_downstream = true;
    }
    turbo_mutex_unlock(&binding->processor->lock);
    if (close_downstream)
        binding->downstream.close(binding->downstream_user);
}

static bool composite_is_quiescent(void *user) {
    scxml_chttp_binding_impl *binding = (scxml_chttp_binding_impl *)user;
    bool downstream_quiescent;
    bool quiescent;
    if (binding == NULL || binding->processor == NULL) return false;
    turbo_mutex_lock(&binding->processor->lock);
    if (binding->state == SCXML_CHTTP_BINDING_QUIESCENT) {
        turbo_mutex_unlock(&binding->processor->lock);
        return true;
    }
    if (binding->state != SCXML_CHTTP_BINDING_CLOSING) {
        turbo_mutex_unlock(&binding->processor->lock);
        return false;
    }
    turbo_mutex_unlock(&binding->processor->lock);
    downstream_quiescent =
        binding->downstream.is_quiescent(binding->downstream_user);
    turbo_mutex_lock(&binding->processor->lock);
    quiescent = binding->state == SCXML_CHTTP_BINDING_CLOSING &&
        downstream_quiescent && binding->active_callbacks == 0u &&
        binding->outbound_references == 0u;
    if (quiescent) {
        binding->state = SCXML_CHTTP_BINDING_QUIESCENT;
        binding->session = NULL;
        binding->program = NULL;
    }
    turbo_mutex_unlock(&binding->processor->lock);
    return quiescent;
}

int scxml_chttp_binding_init(
    scxml_chttp_binding *binding, scxml_chttp_processor *processor,
    const scxml_chttp_binding_config_v1 *config) {
    scxml_chttp_processor_impl *processor_impl;
    scxml_chttp_binding_impl *impl;
    scxml_chttp_endpoint_row *endpoint = NULL;
    turbo_uuid_t uuid;
    uint64_t composite_capabilities;
    size_t index;
    int status;
    if (binding == NULL || processor == NULL || processor->impl == NULL ||
        config == NULL)
        return TURBO_EINVAL;
    if (binding->impl != NULL) return TURBO_EALREADY;
    if (config->abi_version != SCXML_CHTTP_ABI_V1 ||
        config->struct_size < sizeof(*config) ||
        !downstream_adapter_valid(config->scxml_adapter))
        return TURBO_EINVAL;
    status = turbo_uuid_v4_generate(&uuid);
    if (status != TURBO_OK) return status;
    impl = (scxml_chttp_binding_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return TURBO_ENOMEM;
    processor_impl = (scxml_chttp_processor_impl *)processor->impl;
    turbo_mutex_lock(&processor_impl->lock);
    if (processor_impl->state != SCXML_CHTTP_PROCESSOR_RUNNING) {
        turbo_mutex_unlock(&processor_impl->lock);
        free(impl);
        return TURBO_EBUSY;
    }
    for (index = 0u; index < processor_impl->config.endpoint_capacity;
         ++index) {
        if (processor_impl->endpoints[index].binding == NULL) {
            endpoint = &processor_impl->endpoints[index];
            break;
        }
    }
    if (endpoint == NULL) {
        turbo_mutex_unlock(&processor_impl->lock);
        free(impl);
        return TURBO_ENOBUFS;
    }
    ++endpoint->generation;
    if (endpoint->generation == 0u) ++endpoint->generation;
    status = turbo_uuid_format(
        &uuid, endpoint->endpoint, sizeof(endpoint->endpoint));
    if (status != TURBO_OK) {
        turbo_mutex_unlock(&processor_impl->lock);
        free(impl);
        return status;
    }
    impl->processor = processor_impl;
    impl->endpoint_index = index;
    impl->endpoint_generation = endpoint->generation;
    impl->state = SCXML_CHTTP_BINDING_RESERVED;
    impl->next_commit_sequence = 1u;
    composite_capabilities = SCXML_EVENT_IO_CAP_SEND |
        SCXML_EVENT_IO_CAP_PAYLOAD | SCXML_EVENT_IO_CAP_CONTENT;
    if ((config->scxml_adapter->capabilities &
         (SCXML_EVENT_IO_CAP_DELAYED_SEND | SCXML_EVENT_IO_CAP_CANCEL)) ==
        (SCXML_EVENT_IO_CAP_DELAYED_SEND | SCXML_EVENT_IO_CAP_CANCEL))
        composite_capabilities |= SCXML_EVENT_IO_CAP_DELAYED_SEND |
            SCXML_EVENT_IO_CAP_CANCEL;
    impl->composite = (scxml_event_io_adapter){
        .abi_version = SCXML_ADAPTER_ABI,
        .struct_size = sizeof(scxml_event_io_adapter),
        .capabilities = composite_capabilities,
        .prepare_send = composite_prepare_send,
        .prepare_cancel = composite_prepare_cancel,
        .close = composite_close,
        .is_quiescent = composite_is_quiescent};
    impl->downstream = *config->scxml_adapter;
    impl->downstream_user = config->scxml_adapter_user;
    impl->decode = config->decode;
    impl->decode_user = config->decode_user;
    endpoint->binding = impl;
    status = build_access_uri(processor_impl, endpoint);
    if (status != TURBO_OK) {
        endpoint->binding = NULL;
        endpoint->endpoint[0] = '\0';
        endpoint->access_uri[0] = '\0';
        endpoint->access_uri_size = 0u;
        turbo_mutex_unlock(&processor_impl->lock);
        free(impl);
        return status;
    }
    ++processor_impl->live_bindings;
    binding->impl = impl;
    turbo_mutex_unlock(&processor_impl->lock);
    return TURBO_OK;
}

const scxml_event_io_adapter *scxml_chttp_binding_event_io_adapter(
    const scxml_chttp_binding *binding) {
    const scxml_chttp_binding_impl *impl = binding != NULL
        ? (const scxml_chttp_binding_impl *)binding->impl : NULL;
    return impl != NULL ? &impl->composite : NULL;
}

void *scxml_chttp_binding_adapter_user(scxml_chttp_binding *binding) {
    return binding != NULL ? binding->impl : NULL;
}

bool scxml_chttp_binding_ioprocessor(
    const scxml_chttp_binding *binding,
    scxml_ioprocessor_descriptor *out_descriptor) {
    scxml_chttp_binding_impl *impl;
    scxml_chttp_endpoint_row *endpoint;
    bool available;
    if (out_descriptor != NULL) memset(out_descriptor, 0, sizeof(*out_descriptor));
    if (binding == NULL || binding->impl == NULL || out_descriptor == NULL)
        return false;
    impl = (scxml_chttp_binding_impl *)binding->impl;
    turbo_mutex_lock(&impl->processor->lock);
    endpoint = &impl->processor->endpoints[impl->endpoint_index];
    available = endpoint->binding == impl &&
        endpoint->generation == impl->endpoint_generation &&
        (impl->state == SCXML_CHTTP_BINDING_RESERVED ||
         impl->state == SCXML_CHTTP_BINDING_ACTIVE);
    if (available) {
        *out_descriptor = (scxml_ioprocessor_descriptor){
            BASIC_HTTP_NAME, sizeof(BASIC_HTTP_NAME) - 1u,
            SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI,
            sizeof(SCXML_BASIC_HTTP_EVENT_PROCESSOR_URI) - 1u,
            endpoint->access_uri, endpoint->access_uri_size};
    }
    turbo_mutex_unlock(&impl->processor->lock);
    return available;
}

int scxml_chttp_binding_activate(
    scxml_chttp_binding *binding, scxml_session *session,
    const scxml_program *program) {
    scxml_chttp_binding_impl *impl;
    scxml_ioprocessor_descriptor descriptor;
    int status = TURBO_OK;
    if (binding == NULL || binding->impl == NULL || session == NULL ||
        session->impl == NULL || program == NULL || program->impl == NULL)
        return TURBO_EINVAL;
    impl = (scxml_chttp_binding_impl *)binding->impl;
    if (!scxml_chttp_binding_ioprocessor(binding, &descriptor) ||
        !scxml_session_matches_event_io(
            session, program, &impl->composite, impl, &descriptor))
        return TURBO_EINVAL;
    turbo_mutex_lock(&impl->processor->lock);
    if (impl->state == SCXML_CHTTP_BINDING_ACTIVE)
        status = TURBO_EALREADY;
    else if (impl->state != SCXML_CHTTP_BINDING_RESERVED ||
             impl->processor->state != SCXML_CHTTP_PROCESSOR_RUNNING)
        status = TURBO_EBUSY;
    else {
        impl->session = session;
        impl->program = program;
        impl->state = SCXML_CHTTP_BINDING_ACTIVE;
        turbo_cond_signal(&impl->processor->wake);
    }
    turbo_mutex_unlock(&impl->processor->lock);
    return status;
}

int scxml_chttp_binding_destroy(scxml_chttp_binding *binding) {
    scxml_chttp_binding_impl *impl;
    scxml_chttp_endpoint_row *endpoint;
    if (binding == NULL) return TURBO_EINVAL;
    if (binding->impl == NULL) return TURBO_OK;
    impl = (scxml_chttp_binding_impl *)binding->impl;
    if (!composite_is_quiescent(impl)) return TURBO_EBUSY;
    turbo_mutex_lock(&impl->processor->lock);
    endpoint = &impl->processor->endpoints[impl->endpoint_index];
    if (endpoint->binding != impl ||
        endpoint->generation != impl->endpoint_generation) {
        ++impl->processor->invariant_failures;
        turbo_mutex_unlock(&impl->processor->lock);
        return TURBO_EIO;
    }
    endpoint->binding = NULL;
    endpoint->endpoint[0] = '\0';
    endpoint->access_uri[0] = '\0';
    endpoint->access_uri_size = 0u;
    --impl->processor->live_bindings;
    turbo_mutex_unlock(&impl->processor->lock);
    free(impl);
    binding->impl = NULL;
    return TURBO_OK;
}

bool scxml_chttp_processor_get_stats(
    const scxml_chttp_processor *processor,
    scxml_chttp_processor_stats *out_stats) {
    scxml_chttp_processor_impl *impl;
    size_t active_callbacks = 0u;
    size_t outbound_references = 0u;
    size_t index;
    if (out_stats != NULL) memset(out_stats, 0, sizeof(*out_stats));
    if (processor == NULL || processor->impl == NULL || out_stats == NULL)
        return false;
    impl = (scxml_chttp_processor_impl *)processor->impl;
    turbo_mutex_lock(&impl->lock);
    for (index = 0u; index < impl->config.endpoint_capacity; ++index) {
        const scxml_chttp_binding_impl *binding = impl->endpoints[index].binding;
        if (binding != NULL) {
            active_callbacks += binding->active_callbacks;
            outbound_references += binding->outbound_references;
        }
    }
    *out_stats = (scxml_chttp_processor_stats){
        .egress_accepted = impl->egress_accepted,
        .egress_completed = impl->egress_completed,
        .egress_failed = impl->egress_failed,
        .egress_cancelled = impl->egress_cancelled,
        .ingress_requests = impl->ingress_requests,
        .ingress_admitted = impl->ingress_admitted,
        .ingress_rejected = impl->ingress_rejected,
        .invariant_failures = impl->invariant_failures,
        .live_bindings = impl->live_bindings,
        .queued_egress = impl->queued_egress,
        .in_flight_egress = impl->in_flight_egress,
        .active_callbacks = active_callbacks,
        .outbound_references = outbound_references,
        .running = impl->state == SCXML_CHTTP_PROCESSOR_RUNNING,
        .stopping = impl->state == SCXML_CHTTP_PROCESSOR_STOPPING};
    turbo_mutex_unlock(&impl->lock);
    return true;
}
