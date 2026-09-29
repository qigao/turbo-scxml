#include <voicexml/document_store.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct vxml_document_entry {
    uint32_t generation;
    bool occupied;
    size_t pins;
    uint64_t last_used;
    char *allocation;
    size_t allocation_size;
    char *uri;
    size_t uri_size;
    unsigned char *source;
    size_t source_size;
    vxml_program program;
} vxml_document_entry;

typedef struct vxml_document_borrow {
    bool active;
    uint32_t generation;
    size_t entry_index;
    uint32_t entry_generation;
} vxml_document_borrow;

typedef struct vxml_document_store_impl {
    char *application_uri;
    size_t application_uri_size;
    size_t capacity;
    size_t max_uri_bytes;
    size_t max_document_bytes;
    size_t max_cache_bytes;
    size_t cached_bytes;
    size_t active_borrows;
    uint64_t clock;
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    vxml_limits voice_limits;
    vxml_dialog_document_adapter_v1 documents;
    void *document_user;
    vxml_document_entry *entries;
    vxml_document_borrow *borrows;
} vxml_document_store_impl;

typedef struct uri_parts {
    size_t scheme_end;
    size_t authority_start;
    size_t authority_end;
    size_t path_start;
    size_t path_end;
    size_t query_start;
    bool has_authority;
} uri_parts;

static bool checked_add(size_t a, size_t b, size_t *out) {
    if (out == NULL || a > SIZE_MAX - b) return false;
    *out = a + b;
    return true;
}

static uint32_t next_generation(uint32_t value) {
    ++value;
    return value != 0u ? value : 1u;
}

static bool uri_bytes_valid(const char *data, size_t size) {
    size_t i;
    if (data == NULL || size == 0u || memchr(data, '\0', size) != NULL)
        return false;
    for (i = 0u; i < size; ++i) {
        const unsigned char c = (unsigned char)data[i];
        if (c <= 0x20u || c == 0x7fu)
            return false;
    }
    return true;
}

static bool scheme_char(unsigned char c, bool first) {
    if (first)
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
}

static bool find_scheme(const char *uri, size_t size, size_t *out_colon) {
    size_t i;
    if (!uri_bytes_valid(uri, size) || out_colon == NULL)
        return false;
    for (i = 0u; i < size; ++i) {
        const unsigned char c = (unsigned char)uri[i];
        if (c == ':') {
            size_t j;
            if (i == 0u) return false;
            for (j = 0u; j < i; ++j)
                if (!scheme_char((unsigned char)uri[j], j == 0u))
                    return false;
            *out_colon = i;
            return true;
        }
        if (c == '/' || c == '?' || c == '#')
            return false;
    }
    return false;
}

static bool parse_absolute_uri(
    const char *uri, size_t size, uri_parts *out) {
    uri_parts parts = {0};
    size_t colon;
    size_t cursor;
    size_t query = size;
    if (out == NULL || !find_scheme(uri, size, &colon))
        return false;
    if (memchr(uri, '#', size) != NULL)
        return false;
    parts.scheme_end = colon;
    cursor = colon + 1u;
    if (cursor + 1u < size &&
        uri[cursor] == '/' && uri[cursor + 1u] == '/') {
        parts.has_authority = true;
        parts.authority_start = cursor + 2u;
        cursor = parts.authority_start;
        while (cursor < size && uri[cursor] != '/' && uri[cursor] != '?')
            ++cursor;
        parts.authority_end = cursor;
        if (parts.authority_end == parts.authority_start)
            return false;
    } else {
        parts.authority_start = parts.authority_end = cursor;
    }
    parts.path_start = cursor;
    while (cursor < size && uri[cursor] != '?')
        ++cursor;
    parts.path_end = cursor;
    if (cursor < size && uri[cursor] == '?')
        query = cursor;
    parts.query_start = query;
    *out = parts;
    return true;
}

static bool append_bytes(
    char *out, size_t capacity, size_t *cursor,
    const char *data, size_t size) {
    if (out == NULL || cursor == NULL ||
        size > capacity - *cursor ||
        (size != 0u && data == NULL))
        return false;
    if (size != 0u)
        memcpy(out + *cursor, data, size);
    *cursor += size;
    return true;
}

/*
 * Normalize only the path component. Scheme, authority and query are preserved
 * byte-for-byte so policy identity is not silently rewritten.
 */
static bool normalize_absolute_uri(
    const char *uri, size_t uri_size,
    char *out, size_t out_capacity,
    size_t *out_size) {
    uri_parts parts;
    size_t path_size;
    size_t *segments = NULL;
    size_t segment_count = 0u;
    size_t cursor = 0u;
    size_t i;
    bool leading_slash;
    bool trailing_slash;
    if (out_size != NULL) *out_size = 0u;
    if (!parse_absolute_uri(uri, uri_size, &parts) ||
        out == NULL || out_capacity == 0u || out_size == NULL)
        return false;
    path_size = parts.path_end - parts.path_start;
    segments = (size_t *)calloc(path_size + 1u, sizeof(*segments));
    if (segments == NULL)
        return false;

    if (!append_bytes(out, out_capacity - 1u, &cursor,
                      uri, parts.path_start)) {
        free(segments);
        return false;
    }

    leading_slash = path_size != 0u && uri[parts.path_start] == '/';
    trailing_slash = path_size != 0u &&
        uri[parts.path_end - 1u] == '/';
    if (leading_slash) {
        if (!append_bytes(out, out_capacity - 1u, &cursor, "/", 1u)) {
            free(segments);
            return false;
        }
    }

    i = parts.path_start + (leading_slash ? 1u : 0u);
    while (i <= parts.path_end) {
        size_t start = i;
        size_t len;
        while (i < parts.path_end && uri[i] != '/')
            ++i;
        len = i - start;
        if (len == 0u || (len == 1u && uri[start] == '.')) {
            /* omit */
        } else if (len == 2u && uri[start] == '.' && uri[start + 1u] == '.') {
            if (segment_count != 0u) {
                cursor = segments[--segment_count];
                if (cursor != 0u && out[cursor - 1u] == '/' &&
                    !(leading_slash && cursor == parts.path_start + 1u))
                    --cursor;
                if (leading_slash && cursor == parts.path_start)
                    ++cursor;
            } else if (!leading_slash) {
                if (cursor != 0u && out[cursor - 1u] != '/' &&
                    !append_bytes(out, out_capacity - 1u, &cursor, "/", 1u)) {
                    free(segments);
                    return false;
                }
                segments[segment_count++] = cursor;
                if (!append_bytes(out, out_capacity - 1u, &cursor, "..", 2u)) {
                    free(segments);
                    return false;
                }
            }
        } else {
            if (cursor != 0u && out[cursor - 1u] != '/' &&
                !append_bytes(out, out_capacity - 1u, &cursor, "/", 1u)) {
                free(segments);
                return false;
            }
            segments[segment_count++] = cursor;
            if (!append_bytes(out, out_capacity - 1u, &cursor,
                              uri + start, len)) {
                free(segments);
                return false;
            }
        }
        if (i == parts.path_end)
            break;
        ++i;
    }

    if (trailing_slash && cursor != 0u && out[cursor - 1u] != '/' &&
        !append_bytes(out, out_capacity - 1u, &cursor, "/", 1u)) {
        free(segments);
        return false;
    }

    if (parts.query_start < uri_size &&
        !append_bytes(out, out_capacity - 1u, &cursor,
                      uri + parts.query_start,
                      uri_size - parts.query_start)) {
        free(segments);
        return false;
    }
    out[cursor] = '\0';
    *out_size = cursor;
    free(segments);
    return true;
}

static bool absolute_reference(
    const char *ref, size_t size) {
    size_t colon = 0u;
    return size != 0u && find_scheme(ref, size, &colon);
}

static bool build_resolved_candidate(
    const char *base, size_t base_size,
    const char *ref, size_t ref_size,
    char *out, size_t capacity,
    size_t *out_size) {
    uri_parts base_parts;
    size_t cursor = 0u;
    size_t base_query;
    size_t base_path_dir_end;
    if (out_size != NULL) *out_size = 0u;
    if (!parse_absolute_uri(base, base_size, &base_parts) ||
        out == NULL || capacity == 0u || out_size == NULL ||
        (ref_size != 0u && !uri_bytes_valid(ref, ref_size)))
        return false;

    if (ref_size == 0u) {
        if (base_size >= capacity) return false;
        memcpy(out, base, base_size);
        out[base_size] = '\0';
        *out_size = base_size;
        return true;
    }

    if (absolute_reference(ref, ref_size)) {
        if (ref_size >= capacity) return false;
        memcpy(out, ref, ref_size);
        out[ref_size] = '\0';
        *out_size = ref_size;
        return true;
    }

    if (ref_size >= 2u && ref[0] == '/' && ref[1] == '/') {
        if (!append_bytes(out, capacity - 1u, &cursor,
                          base, base_parts.scheme_end + 1u) ||
            !append_bytes(out, capacity - 1u, &cursor, ref, ref_size))
            return false;
        out[cursor] = '\0';
        *out_size = cursor;
        return true;
    }

    if (ref[0] == '?') {
        base_query = base_parts.query_start < base_size
            ? base_parts.query_start : base_size;
        if (!append_bytes(out, capacity - 1u, &cursor,
                          base, base_query) ||
            !append_bytes(out, capacity - 1u, &cursor, ref, ref_size))
            return false;
        out[cursor] = '\0';
        *out_size = cursor;
        return true;
    }

    if (ref[0] == '/') {
        const size_t prefix_end = base_parts.has_authority
            ? base_parts.authority_end : base_parts.scheme_end + 1u;
        if (!append_bytes(out, capacity - 1u, &cursor,
                          base, prefix_end) ||
            !append_bytes(out, capacity - 1u, &cursor, ref, ref_size))
            return false;
        out[cursor] = '\0';
        *out_size = cursor;
        return true;
    }

    base_path_dir_end = base_parts.path_end;
    while (base_path_dir_end > base_parts.path_start &&
           base[base_path_dir_end - 1u] != '/')
        --base_path_dir_end;
    if (!append_bytes(out, capacity - 1u, &cursor,
                      base, base_path_dir_end))
        return false;
    if (base_parts.has_authority &&
        base_path_dir_end == base_parts.path_start &&
        (cursor == 0u || out[cursor - 1u] != '/') &&
        !append_bytes(out, capacity - 1u, &cursor, "/", 1u))
        return false;
    if (!append_bytes(out, capacity - 1u, &cursor, ref, ref_size))
        return false;
    out[cursor] = '\0';
    *out_size = cursor;
    return true;
}

static void error_clear(vxml_document_store_error *error) {
    if (error != NULL) {
        *error = (vxml_document_store_error){
            .status = VXML_DOCUMENT_STORE_OK,
            .resource_status = VXML_DIALOG_MANAGER_OK,
            .voice_status = VXML_OK};
    }
}

static void error_set(
    vxml_document_store_error *error,
    vxml_document_store_status status,
    vxml_dialog_manager_status resource_status,
    vxml_status voice_status) {
    if (error != NULL) {
        error->status = status;
        error->resource_status = resource_status;
        error->voice_status = voice_status;
    }
}

static void entry_evict(
    vxml_document_store_impl *impl,
    vxml_document_entry *entry) {
    uint32_t generation;
    if (impl == NULL || entry == NULL || !entry->occupied ||
        entry->pins != 0u)
        return;
    generation = entry->generation;
    vxml_program_destroy(&entry->program);
    if (entry->allocation_size <= impl->cached_bytes)
        impl->cached_bytes -= entry->allocation_size;
    free(entry->allocation);
    memset(entry, 0, sizeof(*entry));
    entry->generation = generation;
    ++impl->evictions;
}

static vxml_document_entry *find_entry(
    vxml_document_store_impl *impl,
    const char *uri, size_t uri_size) {
    size_t i;
    for (i = 0u; i < impl->capacity; ++i) {
        vxml_document_entry *entry = &impl->entries[i];
        if (entry->occupied &&
            entry->uri_size == uri_size &&
            memcmp(entry->uri, uri, uri_size) == 0)
            return entry;
    }
    return NULL;
}

static vxml_document_entry *find_empty(
    vxml_document_store_impl *impl) {
    size_t i;
    for (i = 0u; i < impl->capacity; ++i)
        if (!impl->entries[i].occupied)
            return &impl->entries[i];
    return NULL;
}

static vxml_document_entry *find_lru_unpinned(
    vxml_document_store_impl *impl) {
    vxml_document_entry *best = NULL;
    size_t i;
    for (i = 0u; i < impl->capacity; ++i) {
        vxml_document_entry *entry = &impl->entries[i];
        if (!entry->occupied || entry->pins != 0u)
            continue;
        if (best == NULL || entry->last_used < best->last_used)
            best = entry;
    }
    return best;
}

static bool has_insert_capacity(
    vxml_document_store_impl *impl) {
    return find_empty(impl) != NULL ||
           find_lru_unpinned(impl) != NULL;
}

static bool make_room(
    vxml_document_store_impl *impl,
    size_t new_bytes,
    vxml_document_entry **out_slot) {
    vxml_document_entry *slot;
    if (out_slot == NULL || new_bytes > impl->max_cache_bytes)
        return false;
    for (;;) {
        slot = find_empty(impl);
        if (slot != NULL &&
            impl->cached_bytes <= impl->max_cache_bytes - new_bytes) {
            *out_slot = slot;
            return true;
        }
        slot = find_lru_unpinned(impl);
        if (slot == NULL)
            return false;
        entry_evict(impl, slot);
    }
}

static vxml_document_borrow *borrow_from_ref(
    vxml_document_store_impl *impl,
    vxml_document_ref ref) {
    vxml_document_borrow *borrow;
    if (impl == NULL ||
        ref.borrow_slot == 0u ||
        ref.borrow_slot > impl->capacity ||
        ref.borrow_generation == 0u)
        return NULL;
    borrow = &impl->borrows[ref.borrow_slot - 1u];
    return borrow->active &&
           borrow->generation == ref.borrow_generation &&
           borrow->entry_index + 1u == ref.slot &&
           borrow->entry_generation == ref.generation
        ? borrow : NULL;
}

static vxml_document_entry *entry_from_ref(
    vxml_document_store_impl *impl,
    vxml_document_ref ref) {
    vxml_document_borrow *borrow = borrow_from_ref(impl, ref);
    vxml_document_entry *entry;
    if (borrow == NULL || ref.slot == 0u ||
        ref.slot > impl->capacity || ref.generation == 0u)
        return NULL;
    entry = &impl->entries[ref.slot - 1u];
    return entry->occupied &&
           entry->generation == ref.generation &&
           borrow->entry_index == (size_t)(ref.slot - 1u)
        ? entry : NULL;
}

static vxml_document_store_status allocate_borrow(
    vxml_document_store_impl *impl,
    vxml_document_entry *entry,
    vxml_document_ref *out_ref) {
    size_t i;
    size_t entry_index;
    if (impl == NULL || entry == NULL || out_ref == NULL ||
        !entry->occupied)
        return VXML_DOCUMENT_STORE_INVALID_ARGUMENT;
    entry_index = (size_t)(entry - impl->entries);
    for (i = 0u; i < impl->capacity; ++i) {
        vxml_document_borrow *borrow = &impl->borrows[i];
        if (borrow->active)
            continue;
        borrow->generation = next_generation(borrow->generation);
        borrow->active = true;
        borrow->entry_index = entry_index;
        borrow->entry_generation = entry->generation;
        ++entry->pins;
        ++impl->active_borrows;
        *out_ref = (vxml_document_ref){
            .slot = (uint32_t)entry_index + 1u,
            .generation = entry->generation,
            .borrow_slot = (uint32_t)i + 1u,
            .borrow_generation = borrow->generation};
        return VXML_DOCUMENT_STORE_OK;
    }
    return VXML_DOCUMENT_STORE_FULL;
}

const char *vxml_document_store_status_string(
    vxml_document_store_status status) {
    switch (status) {
    case VXML_DOCUMENT_STORE_OK:
        return "ok";
    case VXML_DOCUMENT_STORE_INVALID_ARGUMENT:
        return "invalid_argument";
    case VXML_DOCUMENT_STORE_ALLOCATION_FAILED:
        return "allocation_failed";
    case VXML_DOCUMENT_STORE_INVALID_URI:
        return "invalid_uri";
    case VXML_DOCUMENT_STORE_LIMIT_EXCEEDED:
        return "limit_exceeded";
    case VXML_DOCUMENT_STORE_FULL:
        return "full";
    case VXML_DOCUMENT_STORE_RESOURCE_ERROR:
        return "resource_error";
    case VXML_DOCUMENT_STORE_COMPILE_ERROR:
        return "compile_error";
    case VXML_DOCUMENT_STORE_STALE:
        return "stale";
    case VXML_DOCUMENT_STORE_BUSY:
        return "busy";
    default:
        return "unknown";
    }
}

vxml_document_store_status vxml_document_store_init(
    vxml_document_store *store,
    const vxml_document_store_config_v1 *config) {
    vxml_document_store_impl *impl;
    char *normalized;
    size_t normalized_size = 0u;
    if (store == NULL || store->impl != NULL ||
        config == NULL ||
        config->abi_version != VXML_DOCUMENT_STORE_CONFIG_ABI_V1 ||
        config->struct_size < sizeof(*config) ||
        config->capacity == 0u ||
        config->capacity > UINT32_MAX ||
        config->max_uri_bytes == 0u ||
        config->max_uri_bytes == SIZE_MAX ||
        config->max_document_bytes == 0u ||
        config->max_cache_bytes == 0u ||
        config->application_uri == NULL ||
        config->application_uri_size == 0u ||
        config->application_uri_size > config->max_uri_bytes ||
        config->documents == NULL ||
        config->documents->abi_version !=
            VXML_DIALOG_DOCUMENT_ADAPTER_ABI_V1 ||
        config->documents->struct_size < sizeof(*config->documents) ||
        config->documents->open == NULL ||
        config->documents->close == NULL)
        return VXML_DOCUMENT_STORE_INVALID_ARGUMENT;

    normalized = (char *)malloc(config->max_uri_bytes + 1u);
    if (normalized == NULL)
        return VXML_DOCUMENT_STORE_ALLOCATION_FAILED;
    if (!normalize_absolute_uri(
            config->application_uri,
            config->application_uri_size,
            normalized, config->max_uri_bytes + 1u,
            &normalized_size)) {
        free(normalized);
        return VXML_DOCUMENT_STORE_INVALID_URI;
    }

    impl = (vxml_document_store_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) {
        free(normalized);
        return VXML_DOCUMENT_STORE_ALLOCATION_FAILED;
    }
    impl->entries = (vxml_document_entry *)calloc(
        config->capacity, sizeof(*impl->entries));
    impl->borrows = (vxml_document_borrow *)calloc(
        config->capacity, sizeof(*impl->borrows));
    if (impl->entries == NULL || impl->borrows == NULL) {
        free(impl->borrows);
        free(impl->entries);
        free(normalized);
        free(impl);
        return VXML_DOCUMENT_STORE_ALLOCATION_FAILED;
    }
    impl->application_uri = normalized;
    impl->application_uri_size = normalized_size;
    impl->capacity = config->capacity;
    impl->max_uri_bytes = config->max_uri_bytes;
    impl->max_document_bytes = config->max_document_bytes;
    impl->max_cache_bytes = config->max_cache_bytes;
    impl->voice_limits = config->voice_limits;
    impl->documents = *config->documents;
    impl->document_user = config->document_user;
    store->impl = impl;
    return VXML_DOCUMENT_STORE_OK;
}

vxml_document_store_status vxml_document_store_resolve(
    const vxml_document_store *store,
    const char *base_document_uri,
    size_t base_document_uri_size,
    const char *reference,
    size_t reference_size,
    vxml_resolved_uri_v1 *out) {
    const vxml_document_store_impl *impl =
        store != NULL
            ? (const vxml_document_store_impl *)store->impl : NULL;
    const char *base;
    size_t base_size;
    const char *hash;
    size_t fetch_ref_size;
    const char *fragment = NULL;
    size_t fragment_size = 0u;
    char *candidate = NULL;
    char *normalized = NULL;
    size_t candidate_size = 0u;
    size_t normalized_size = 0u;
    vxml_document_store_status status = VXML_DOCUMENT_STORE_INVALID_URI;

    if (out != NULL) {
        out->document_uri_size = 0u;
        out->fragment_size = 0u;
    }
    if (impl == NULL || out == NULL ||
        out->abi_version != 1u ||
        out->struct_size < sizeof(*out) ||
        out->document_uri == NULL ||
        out->document_uri_capacity == 0u ||
        (out->fragment_capacity != 0u && out->fragment == NULL) ||
        (reference_size != 0u && reference == NULL))
        return VXML_DOCUMENT_STORE_INVALID_ARGUMENT;

    base = base_document_uri_size != 0u
        ? base_document_uri : impl->application_uri;
    base_size = base_document_uri_size != 0u
        ? base_document_uri_size : impl->application_uri_size;
    if (!uri_bytes_valid(base, base_size) ||
        base_size > impl->max_uri_bytes)
        return VXML_DOCUMENT_STORE_INVALID_URI;

    hash = reference_size != 0u
        ? (const char *)memchr(reference, '#', reference_size)
        : NULL;
    fetch_ref_size = hash != NULL
        ? (size_t)(hash - reference) : reference_size;
    if (hash != NULL) {
        fragment = hash + 1;
        fragment_size =
            reference_size - (size_t)(fragment - reference);
        if (fragment_size >= out->fragment_capacity)
            return VXML_DOCUMENT_STORE_LIMIT_EXCEEDED;
    }

    candidate = (char *)malloc(impl->max_uri_bytes + 1u);
    normalized = (char *)malloc(impl->max_uri_bytes + 1u);
    if (candidate == NULL || normalized == NULL) {
        status = VXML_DOCUMENT_STORE_ALLOCATION_FAILED;
        goto done;
    }

    if (!build_resolved_candidate(
            base, base_size,
            reference, fetch_ref_size,
            candidate, impl->max_uri_bytes + 1u,
            &candidate_size) ||
        !normalize_absolute_uri(
            candidate, candidate_size,
            normalized, impl->max_uri_bytes + 1u,
            &normalized_size)) {
        status = VXML_DOCUMENT_STORE_INVALID_URI;
        goto done;
    }
    if (normalized_size >= out->document_uri_capacity) {
        status = VXML_DOCUMENT_STORE_LIMIT_EXCEEDED;
        goto done;
    }
    memcpy(out->document_uri, normalized, normalized_size);
    out->document_uri[normalized_size] = '\0';
    out->document_uri_size = normalized_size;
    if (fragment_size != 0u)
        memcpy(out->fragment, fragment, fragment_size);
    if (out->fragment != NULL && out->fragment_capacity != 0u)
        out->fragment[fragment_size] = '\0';
    out->fragment_size = fragment_size;
    status = VXML_DOCUMENT_STORE_OK;

done:
    free(candidate);
    free(normalized);
    if (status != VXML_DOCUMENT_STORE_OK) {
        out->document_uri_size = 0u;
        out->fragment_size = 0u;
        if (out->document_uri != NULL && out->document_uri_capacity != 0u)
            out->document_uri[0] = '\0';
        if (out->fragment != NULL && out->fragment_capacity != 0u)
            out->fragment[0] = '\0';
    }
    return status;
}

vxml_document_store_status vxml_document_store_acquire(
    vxml_document_store *store,
    const char *document_uri,
    size_t document_uri_size,
    vxml_document_ref *out_ref,
    vxml_document_store_error *out_error) {
    vxml_document_store_impl *impl =
        store != NULL ? (vxml_document_store_impl *)store->impl : NULL;
    char *canonical = NULL;
    size_t canonical_size = 0u;
    vxml_document_entry *entry;
    vxml_dialog_document document = {0};
    vxml_dialog_manager_status resource_status;
    size_t allocation_size;
    size_t source_size;
    char *allocation = NULL;
    unsigned char *source_copy;
    vxml_program program = {0};
    vxml_diagnostic diagnostic = {0};
    vxml_status voice_status;
    vxml_document_entry *slot = NULL;
    size_t slot_index;

    if (out_ref != NULL) *out_ref = (vxml_document_ref){0};
    error_clear(out_error);
    if (impl == NULL || out_ref == NULL ||
        !uri_bytes_valid(document_uri, document_uri_size) ||
        document_uri_size > impl->max_uri_bytes)
        return VXML_DOCUMENT_STORE_INVALID_ARGUMENT;

    if (impl->active_borrows >= impl->capacity) {
        error_set(out_error, VXML_DOCUMENT_STORE_FULL,
                  VXML_DIALOG_MANAGER_OK, VXML_OK);
        return VXML_DOCUMENT_STORE_FULL;
    }

    canonical = (char *)malloc(impl->max_uri_bytes + 1u);
    if (canonical == NULL) {
        error_set(out_error, VXML_DOCUMENT_STORE_ALLOCATION_FAILED,
                  VXML_DIALOG_MANAGER_OK, VXML_OK);
        return VXML_DOCUMENT_STORE_ALLOCATION_FAILED;
    }
    if (!normalize_absolute_uri(
            document_uri, document_uri_size,
            canonical, impl->max_uri_bytes + 1u,
            &canonical_size)) {
        free(canonical);
        error_set(out_error, VXML_DOCUMENT_STORE_INVALID_URI,
                  VXML_DIALOG_MANAGER_OK, VXML_OK);
        return VXML_DOCUMENT_STORE_INVALID_URI;
    }

    entry = find_entry(impl, canonical, canonical_size);
    if (entry != NULL) {
        vxml_document_store_status borrow_status =
            allocate_borrow(impl, entry, out_ref);
        if (borrow_status != VXML_DOCUMENT_STORE_OK) {
            free(canonical);
            error_set(out_error, borrow_status,
                      VXML_DIALOG_MANAGER_OK, VXML_OK);
            return borrow_status;
        }
        entry->last_used = ++impl->clock;
        ++impl->hits;
        free(canonical);
        return VXML_DOCUMENT_STORE_OK;
    }

    ++impl->misses;
    if (!has_insert_capacity(impl)) {
        free(canonical);
        error_set(out_error, VXML_DOCUMENT_STORE_FULL,
                  VXML_DIALOG_MANAGER_OK, VXML_OK);
        return VXML_DOCUMENT_STORE_FULL;
    }

    resource_status = impl->documents.open(
        impl->document_user,
        canonical, canonical_size,
        "application/voicexml+xml",
        sizeof("application/voicexml+xml") - 1u,
        impl->max_document_bytes,
        &document);
    if (resource_status != VXML_DIALOG_MANAGER_OK) {
        free(canonical);
        error_set(out_error, VXML_DOCUMENT_STORE_RESOURCE_ERROR,
                  resource_status, VXML_OK);
        return VXML_DOCUMENT_STORE_RESOURCE_ERROR;
    }
    if (document.size > impl->max_document_bytes ||
        (document.size != 0u && document.data == NULL)) {
        impl->documents.close(impl->document_user, &document);
        free(canonical);
        error_set(out_error, VXML_DOCUMENT_STORE_LIMIT_EXCEEDED,
                  VXML_DIALOG_MANAGER_OK, VXML_OK);
        return VXML_DOCUMENT_STORE_LIMIT_EXCEEDED;
    }

    source_size = document.size;
    if (!checked_add(canonical_size, 1u, &allocation_size) ||
        !checked_add(allocation_size, source_size, &allocation_size) ||
        !checked_add(allocation_size, 1u, &allocation_size) ||
        allocation_size > impl->max_cache_bytes) {
        impl->documents.close(impl->document_user, &document);
        free(canonical);
        error_set(out_error, VXML_DOCUMENT_STORE_LIMIT_EXCEEDED,
                  VXML_DIALOG_MANAGER_OK, VXML_OK);
        return VXML_DOCUMENT_STORE_LIMIT_EXCEEDED;
    }

    allocation = (char *)malloc(allocation_size);
    if (allocation == NULL) {
        impl->documents.close(impl->document_user, &document);
        free(canonical);
        error_set(out_error, VXML_DOCUMENT_STORE_ALLOCATION_FAILED,
                  VXML_DIALOG_MANAGER_OK, VXML_OK);
        return VXML_DOCUMENT_STORE_ALLOCATION_FAILED;
    }
    memcpy(allocation, canonical, canonical_size);
    allocation[canonical_size] = '\0';
    source_copy = (unsigned char *)(allocation + canonical_size + 1u);
    if (source_size != 0u)
        memcpy(source_copy, document.data, source_size);
    source_copy[source_size] = '\0';
    impl->documents.close(impl->document_user, &document);

    voice_status = vxml_compile(
        source_copy, source_size,
        &impl->voice_limits, &program, &diagnostic);
    if (voice_status != VXML_OK) {
        free(allocation);
        free(canonical);
        error_set(out_error, VXML_DOCUMENT_STORE_COMPILE_ERROR,
                  VXML_DIALOG_MANAGER_OK, voice_status);
        return VXML_DOCUMENT_STORE_COMPILE_ERROR;
    }

    if (!make_room(impl, allocation_size, &slot)) {
        vxml_program_destroy(&program);
        free(allocation);
        free(canonical);
        error_set(out_error, VXML_DOCUMENT_STORE_FULL,
                  VXML_DIALOG_MANAGER_OK, VXML_OK);
        return VXML_DOCUMENT_STORE_FULL;
    }

    slot_index = (size_t)(slot - impl->entries);
    slot->generation = next_generation(slot->generation);
    slot->occupied = true;
    slot->pins = 0u;
    slot->last_used = ++impl->clock;
    slot->allocation = allocation;
    slot->allocation_size = allocation_size;
    slot->uri = allocation;
    slot->uri_size = canonical_size;
    slot->source = source_copy;
    slot->source_size = source_size;
    slot->program = program;
    impl->cached_bytes += allocation_size;
    {
        const vxml_document_store_status borrow_status =
            allocate_borrow(impl, slot, out_ref);
        if (borrow_status != VXML_DOCUMENT_STORE_OK) {
            entry_evict(impl, slot);
            free(canonical);
            error_set(out_error, borrow_status,
                      VXML_DIALOG_MANAGER_OK, VXML_OK);
            return borrow_status;
        }
    }
    (void)slot_index;
    free(canonical);
    return VXML_DOCUMENT_STORE_OK;
}

vxml_document_store_status vxml_document_store_acquire_reference(
    vxml_document_store *store,
    const char *base_document_uri,
    size_t base_document_uri_size,
    const char *reference,
    size_t reference_size,
    char *fragment,
    size_t fragment_capacity,
    size_t *out_fragment_size,
    vxml_document_ref *out_ref,
    vxml_document_store_error *out_error) {
    vxml_document_store_impl *impl =
        store != NULL ? (vxml_document_store_impl *)store->impl : NULL;
    char *document_uri = NULL;
    char *fragment_tmp = NULL;
    vxml_resolved_uri_v1 resolved;
    vxml_document_store_status status;

    if (out_fragment_size != NULL) *out_fragment_size = 0u;
    if (out_ref != NULL) *out_ref = (vxml_document_ref){0};
    if (fragment != NULL && fragment_capacity != 0u)
        fragment[0] = '\0';
    if (impl == NULL || out_fragment_size == NULL || out_ref == NULL ||
        (fragment_capacity != 0u && fragment == NULL))
        return VXML_DOCUMENT_STORE_INVALID_ARGUMENT;

    document_uri = (char *)malloc(impl->max_uri_bytes + 1u);
    fragment_tmp = fragment_capacity != 0u
        ? (char *)malloc(fragment_capacity) : NULL;
    if (document_uri == NULL ||
        (fragment_capacity != 0u && fragment_tmp == NULL)) {
        free(fragment_tmp);
        free(document_uri);
        error_set(out_error, VXML_DOCUMENT_STORE_ALLOCATION_FAILED,
                  VXML_DIALOG_MANAGER_OK, VXML_OK);
        return VXML_DOCUMENT_STORE_ALLOCATION_FAILED;
    }

    resolved = (vxml_resolved_uri_v1){
        .abi_version = 1u,
        .struct_size = sizeof(vxml_resolved_uri_v1),
        .document_uri = document_uri,
        .document_uri_capacity = impl->max_uri_bytes + 1u,
        .fragment = fragment_tmp,
        .fragment_capacity = fragment_capacity};
    status = vxml_document_store_resolve(
        store, base_document_uri, base_document_uri_size,
        reference, reference_size, &resolved);
    if (status == VXML_DOCUMENT_STORE_OK) {
        status = vxml_document_store_acquire(
            store, resolved.document_uri, resolved.document_uri_size,
            out_ref, out_error);
    }
    if (status == VXML_DOCUMENT_STORE_OK) {
        if (resolved.fragment_size != 0u)
            memcpy(fragment, fragment_tmp, resolved.fragment_size);
        if (fragment != NULL && fragment_capacity != 0u)
            fragment[resolved.fragment_size] = '\0';
        *out_fragment_size = resolved.fragment_size;
    } else {
        if (fragment != NULL && fragment_capacity != 0u)
            fragment[0] = '\0';
        *out_fragment_size = 0u;
    }
    free(fragment_tmp);
    free(document_uri);
    return status;
}

vxml_document_store_status vxml_document_store_view(
    const vxml_document_store *store,
    vxml_document_ref ref,
    vxml_document_view *out_view) {
    vxml_document_store_impl *impl =
        store != NULL ? (vxml_document_store_impl *)store->impl : NULL;
    vxml_document_entry *entry;
    if (out_view != NULL) *out_view = (vxml_document_view){0};
    if (impl == NULL || out_view == NULL)
        return VXML_DOCUMENT_STORE_INVALID_ARGUMENT;
    entry = entry_from_ref(impl, ref);
    if (entry == NULL || entry->pins == 0u)
        return VXML_DOCUMENT_STORE_STALE;
    *out_view = (vxml_document_view){
        .document_uri = entry->uri,
        .document_uri_size = entry->uri_size,
        .source = entry->source,
        .source_size = entry->source_size,
        .program = &entry->program};
    return VXML_DOCUMENT_STORE_OK;
}

vxml_document_store_status vxml_document_store_release(
    vxml_document_store *store,
    vxml_document_ref *ref) {
    vxml_document_store_impl *impl =
        store != NULL ? (vxml_document_store_impl *)store->impl : NULL;
    vxml_document_entry *entry;
    if (impl == NULL || ref == NULL)
        return VXML_DOCUMENT_STORE_INVALID_ARGUMENT;
    {
        vxml_document_borrow *borrow = borrow_from_ref(impl, *ref);
        entry = entry_from_ref(impl, *ref);
        if (borrow == NULL || entry == NULL || entry->pins == 0u)
            return VXML_DOCUMENT_STORE_STALE;
        borrow->active = false;
        --entry->pins;
        if (impl->active_borrows != 0u)
            --impl->active_borrows;
        entry->last_used = ++impl->clock;
    }
    *ref = (vxml_document_ref){0};
    return VXML_DOCUMENT_STORE_OK;
}

bool vxml_document_store_get_stats(
    const vxml_document_store *store,
    vxml_document_store_stats *out_stats) {
    const vxml_document_store_impl *impl =
        store != NULL
            ? (const vxml_document_store_impl *)store->impl : NULL;
    vxml_document_store_stats stats = {0};
    size_t i;
    if (impl == NULL || out_stats == NULL)
        return false;
    stats.capacity = impl->capacity;
    stats.cached_bytes = impl->cached_bytes;
    stats.active_borrows = impl->active_borrows;
    stats.hits = impl->hits;
    stats.misses = impl->misses;
    stats.evictions = impl->evictions;
    for (i = 0u; i < impl->capacity; ++i)
        if (impl->entries[i].occupied)
            ++stats.entries;
    *out_stats = stats;
    return true;
}

vxml_document_store_status vxml_document_store_clear(
    vxml_document_store *store) {
    vxml_document_store_impl *impl =
        store != NULL ? (vxml_document_store_impl *)store->impl : NULL;
    size_t i;
    bool busy = false;
    if (impl == NULL)
        return VXML_DOCUMENT_STORE_INVALID_ARGUMENT;
    for (i = 0u; i < impl->capacity; ++i) {
        if (!impl->entries[i].occupied)
            continue;
        if (impl->entries[i].pins != 0u) {
            busy = true;
            continue;
        }
        entry_evict(impl, &impl->entries[i]);
    }
    return busy ? VXML_DOCUMENT_STORE_BUSY : VXML_DOCUMENT_STORE_OK;
}

vxml_document_store_status vxml_document_store_destroy(
    vxml_document_store *store) {
    vxml_document_store_impl *impl;
    size_t i;
    if (store == NULL)
        return VXML_DOCUMENT_STORE_INVALID_ARGUMENT;
    impl = (vxml_document_store_impl *)store->impl;
    if (impl == NULL)
        return VXML_DOCUMENT_STORE_OK;
    if (impl->active_borrows != 0u)
        return VXML_DOCUMENT_STORE_BUSY;
    for (i = 0u; i < impl->capacity; ++i)
        if (impl->entries[i].occupied)
            entry_evict(impl, &impl->entries[i]);
    free(impl->borrows);
    free(impl->entries);
    free(impl->application_uri);
    free(impl);
    store->impl = NULL;
    return VXML_DOCUMENT_STORE_OK;
}
